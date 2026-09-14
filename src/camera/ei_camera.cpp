/* The Clear BSD License
 *
Copyright (c) 2026 EdgeImpulse Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted (subject to the limitations in the disclaimer
 * below) provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 *   * Neither the name of the copyright holder nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED BY
 * THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "camera/ei_camera.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/video.h>
#include <zephyr/drivers/video-controls.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <string.h>

#include "edge-impulse-sdk/dsp/image/processing.hpp"
#include "edge-impulse-sdk/porting/ei_classifier_porting.h"
#include "model-parameters/model_metadata.h"

#if !DT_HAS_CHOSEN(zephyr_camera)
#error "No zephyr,camera chosen in devicetree. Pick a board with a camera, or add \
one with --shield / a board overlay."
#endif

#if EI_CLASSIFIER_SENSOR != EI_CLASSIFIER_SENSOR_CAMERA
#error "The model in model/ is not an image model. Export an image impulse from \
Edge Impulse Studio (Deployment > Zephyr library)."
#endif

/* Size the impulse expects, always fed as packed RGB888. Grayscale models are
 * handled by the image DSP block itself, which does the RGB -> gray reduction.
 */
#define EI_CAMERA_MODEL_WIDTH   EI_CLASSIFIER_INPUT_WIDTH
#define EI_CAMERA_MODEL_HEIGHT  EI_CLASSIFIER_INPUT_HEIGHT

/* Pixel formats we know how to turn into RGB888, best first. RGB565 comes ahead
 * of RGB24 on purpose: nearly every DVP sensor supports it and it halves the
 * capture buffer and the DMA bandwidth.
 */
static const uint32_t supported_pixfmt[] = {
    VIDEO_PIX_FMT_RGB565,
    VIDEO_PIX_FMT_RGB565X,
    VIDEO_PIX_FMT_RGB24,
    VIDEO_PIX_FMT_YUYV,
    VIDEO_PIX_FMT_GREY,
};

static const struct device *const camera_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_camera));

static struct video_format cam_fmt;
static struct video_buffer *vbufs[CONFIG_EI_CAMERA_NUM_BUFS];
static uint8_t vbufs_allocated;
static bool streaming;

/* Holds the captured frame as RGB888, then the rescaled model input (rescaling
 * is done in place, the model input is always smaller than the capture).
 */
static uint8_t *rgb888_buf;

/* VIDEO_FOURCC_TO_STR() expands to a C compound literal, so spell it out here
 * rather than rely on the GNU C++ extension.
 */
static void fourcc_to_str(uint32_t fourcc, char out[5])
{
    out[0] = (char)(fourcc & 0xff);
    out[1] = (char)((fourcc >> 8) & 0xff);
    out[2] = (char)((fourcc >> 16) & 0xff);
    out[3] = (char)((fourcc >> 24) & 0xff);
    out[4] = '\0';
}

/**
 * @brief Fit a desired dimension into what the capability advertises.
 *        A step of 0 means only the min and max values are selectable.
 */
static uint32_t fit_dim(uint32_t desired, uint32_t min, uint32_t max, uint16_t step)
{
    if (desired <= min) {
        return min;
    }
    if (desired >= max) {
        return max;
    }
    if (step == 0) {
        return max;
    }

    return min + ROUND_UP(desired - min, step);
}

/**
 * @brief Pick the pixel format and the smallest resolution that still covers
 *        the model input, so we downscale rather than upscale.
 * @return true if a usable format was found
 */
static bool select_format(const struct video_caps *caps, struct video_format *fmt)
{
    uint32_t want_w = CONFIG_EI_CAMERA_CAPTURE_WIDTH > 0 ? CONFIG_EI_CAMERA_CAPTURE_WIDTH
                                                         : EI_CAMERA_MODEL_WIDTH;
    uint32_t want_h = CONFIG_EI_CAMERA_CAPTURE_HEIGHT > 0 ? CONFIG_EI_CAMERA_CAPTURE_HEIGHT
                                                          : EI_CAMERA_MODEL_HEIGHT;

    for (size_t p = 0; p < ARRAY_SIZE(supported_pixfmt); p++) {
        uint64_t best_area = UINT64_MAX;
        uint64_t fallback_area = 0;
        uint32_t best_w = 0, best_h = 0;
        uint32_t fallback_w = 0, fallback_h = 0;

        for (size_t i = 0; caps->format_caps[i].pixelformat != 0; i++) {
            const struct video_format_cap *fcap = &caps->format_caps[i];

            if (fcap->pixelformat != supported_pixfmt[p]) {
                continue;
            }

            uint32_t w = fit_dim(want_w, fcap->width_min, fcap->width_max, fcap->width_step);
            uint32_t h = fit_dim(want_h, fcap->height_min, fcap->height_max, fcap->height_step);
            uint64_t area = (uint64_t)w * h;

            if (w >= want_w && h >= want_h) {
                /* covers the model input: keep the cheapest one */
                if (area < best_area) {
                    best_area = area;
                    best_w = w;
                    best_h = h;
                }
            } else if (area > fallback_area) {
                /* nothing covers it: keep the largest, the impulse will upscale */
                fallback_area = area;
                fallback_w = w;
                fallback_h = h;
            }
        }

        if (best_w == 0) {
            best_w = fallback_w;
            best_h = fallback_h;
        }

        if (best_w != 0) {
            fmt->pixelformat = supported_pixfmt[p];
            fmt->width = best_w;
            fmt->height = best_h;
            return true;
        }
    }

    return false;
}

static void rgb565le_row_to_rgb888(const uint8_t *src, uint8_t *dst, uint32_t pixels)
{
    for (uint32_t i = 0; i < pixels; i++) {
        uint16_t v = sys_get_le16(src);

        src += 2;
        *dst++ = (uint8_t)(((v >> 11) & 0x1f) << 3 | ((v >> 11) & 0x1f) >> 2);
        *dst++ = (uint8_t)(((v >> 5) & 0x3f) << 2 | ((v >> 5) & 0x3f) >> 4);
        *dst++ = (uint8_t)((v & 0x1f) << 3 | (v & 0x1f) >> 2);
    }
}

static void rgb565be_row_to_rgb888(const uint8_t *src, uint8_t *dst, uint32_t pixels)
{
    for (uint32_t i = 0; i < pixels; i++) {
        uint16_t v = sys_get_be16(src);

        src += 2;
        *dst++ = (uint8_t)(((v >> 11) & 0x1f) << 3 | ((v >> 11) & 0x1f) >> 2);
        *dst++ = (uint8_t)(((v >> 5) & 0x3f) << 2 | ((v >> 5) & 0x3f) >> 4);
        *dst++ = (uint8_t)((v & 0x1f) << 3 | (v & 0x1f) >> 2);
    }
}

static void grey_row_to_rgb888(const uint8_t *src, uint8_t *dst, uint32_t pixels)
{
    for (uint32_t i = 0; i < pixels; i++) {
        uint8_t g = *src++;

        *dst++ = g;
        *dst++ = g;
        *dst++ = g;
    }
}

/**
 * @brief Turn one captured frame into packed RGB888 in rgb888_buf.
 * @return true if successful
 */
static bool frame_to_rgb888(const struct video_buffer *vbuf)
{
    uint32_t lines = cam_fmt.pitch ? (vbuf->bytesused / cam_fmt.pitch) : 0;

    if (vbuf->line_offset != 0 || lines < cam_fmt.height) {
        /* partial frame: the driver is handing lines out piecemeal, which this
         * app does not reassemble - drop it and take the next one
         */
        ei_printf("ERR: partial frame (offset %u, %u/%u lines)\n",
                  vbuf->line_offset, lines, cam_fmt.height);
        return false;
    }

    for (uint32_t y = 0; y < cam_fmt.height; y++) {
        const uint8_t *src = vbuf->buffer + (size_t)y * cam_fmt.pitch;
        uint8_t *dst = rgb888_buf + (size_t)y * cam_fmt.width * 3;

        switch (cam_fmt.pixelformat) {
        case VIDEO_PIX_FMT_RGB565:
            rgb565le_row_to_rgb888(src, dst, cam_fmt.width);
            break;
        case VIDEO_PIX_FMT_RGB565X:
            rgb565be_row_to_rgb888(src, dst, cam_fmt.width);
            break;
        case VIDEO_PIX_FMT_RGB24:
            memcpy(dst, src, (size_t)cam_fmt.width * 3);
            break;
        case VIDEO_PIX_FMT_YUYV:
            ei::image::processing::yuv422_to_rgb888(dst, src, cam_fmt.width * 2,
                                                    ei::image::processing::BIG_ENDIAN_ORDER);
            break;
        case VIDEO_PIX_FMT_GREY:
            grey_row_to_rgb888(src, dst, cam_fmt.width);
            break;
        default:
            return false;
        }
    }

    return true;
}

bool ei_camera_init(void)
{
    struct video_caps caps = {};
    int err;

    if (!device_is_ready(camera_dev)) {
        ei_printf("ERR: camera %s is not ready\n", camera_dev->name);
        return false;
    }

    caps.type = VIDEO_BUF_TYPE_OUTPUT;
    err = video_get_caps(camera_dev, &caps);
    if (err < 0) {
        ei_printf("ERR: unable to read video caps (%d)\n", err);
        return false;
    }

    cam_fmt.type = VIDEO_BUF_TYPE_OUTPUT;
    if (select_format(&caps, &cam_fmt) == false) {
        ei_printf("ERR: camera %s offers no pixel format this app can convert\n",
                  camera_dev->name);
        return false;
    }

    err = video_set_compose_format(camera_dev, &cam_fmt);
    if (err < 0) {
        ei_printf("ERR: unable to set video format (%d)\n", err);
        return false;
    }

    /* read it back: the driver is free to give us something else */
    err = video_get_format(camera_dev, &cam_fmt);
    if (err < 0) {
        ei_printf("ERR: unable to read back video format (%d)\n", err);
        return false;
    }

    char fourcc[5];

    fourcc_to_str(cam_fmt.pixelformat, fourcc);
    ei_printf("Camera: %s\n", camera_dev->name);
    ei_printf("\tCapture format: %s %ux%u (pitch %u, %u bytes)\n",
              fourcc, cam_fmt.width, cam_fmt.height, cam_fmt.pitch, cam_fmt.size);
    ei_printf("\tModel input: %dx%d\n", EI_CAMERA_MODEL_WIDTH, EI_CAMERA_MODEL_HEIGHT);

    if (IS_ENABLED(CONFIG_EI_CAMERA_HFLIP) || IS_ENABLED(CONFIG_EI_CAMERA_VFLIP)) {
        struct video_control ctrl = {};

        if (IS_ENABLED(CONFIG_EI_CAMERA_HFLIP)) {
            ctrl.id = VIDEO_CID_HFLIP;
            ctrl.val = 1;
            err = video_set_ctrl(camera_dev, &ctrl);
            if (err < 0) {
                ei_printf("WARN: horizontal flip not applied (%d)\n", err);
            }
        }
        if (IS_ENABLED(CONFIG_EI_CAMERA_VFLIP)) {
            ctrl.id = VIDEO_CID_VFLIP;
            ctrl.val = 1;
            err = video_set_ctrl(camera_dev, &ctrl);
            if (err < 0) {
                ei_printf("WARN: vertical flip not applied (%d)\n", err);
            }
        }
    }

    if (caps.min_vbuf_count > CONFIG_EI_CAMERA_NUM_BUFS) {
        ei_printf("ERR: %s needs %u buffers, CONFIG_EI_CAMERA_NUM_BUFS is %d\n",
                  camera_dev->name, caps.min_vbuf_count, CONFIG_EI_CAMERA_NUM_BUFS);
        return false;
    }

    rgb888_buf = (uint8_t *)ei_malloc((size_t)cam_fmt.width * cam_fmt.height * 3);
    if (rgb888_buf == NULL) {
        ei_printf("ERR: out of memory for the %ux%u RGB888 frame (%u bytes)\n",
                  cam_fmt.width, cam_fmt.height,
                  (unsigned)((size_t)cam_fmt.width * cam_fmt.height * 3));
        return false;
    }

    for (vbufs_allocated = 0; vbufs_allocated < CONFIG_EI_CAMERA_NUM_BUFS; vbufs_allocated++) {
        size_t align = caps.buf_align ? caps.buf_align : CONFIG_VIDEO_BUFFER_POOL_ALIGN;

        vbufs[vbufs_allocated] = video_buffer_aligned_alloc(cam_fmt.size, align, K_NO_WAIT);
        if (vbufs[vbufs_allocated] == NULL) {
            ei_printf("ERR: out of video buffers, raise CONFIG_VIDEO_BUFFER_POOL_HEAP_SIZE\n");
            ei_camera_deinit();
            return false;
        }
        vbufs[vbufs_allocated]->type = VIDEO_BUF_TYPE_OUTPUT;
    }

    return true;
}

bool ei_camera_deinit(void)
{
    ei_camera_stop();

    while (vbufs_allocated > 0) {
        video_buffer_release(vbufs[--vbufs_allocated]);
        vbufs[vbufs_allocated] = NULL;
    }

    if (rgb888_buf != NULL) {
        ei_free(rgb888_buf);
        rgb888_buf = NULL;
    }

    return true;
}

bool ei_camera_start(void)
{
    int err;

    if (streaming) {
        return true;
    }

    for (uint8_t i = 0; i < vbufs_allocated; i++) {
        err = video_enqueue(camera_dev, vbufs[i]);
        if (err < 0) {
            ei_printf("ERR: unable to enqueue video buffer (%d)\n", err);
            return false;
        }
    }

    err = video_stream_start(camera_dev, VIDEO_BUF_TYPE_OUTPUT);
    if (err < 0) {
        ei_printf("ERR: unable to start the video stream (%d)\n", err);
        return false;
    }

    streaming = true;

    return true;
}

bool ei_camera_stop(void)
{
    int err;

    if (streaming == false) {
        return true;
    }

    err = video_stream_stop(camera_dev, VIDEO_BUF_TYPE_OUTPUT);
    if (err < 0) {
        ei_printf("ERR: unable to stop the video stream (%d)\n", err);
        return false;
    }

    /* hand the buffers back to us so they can be re-enqueued or released */
    video_flush(camera_dev, true);
    streaming = false;

    return true;
}

bool ei_camera_capture(int32_t timeout_ms)
{
    struct video_buffer dq = {};
    struct video_buffer *vbuf = &dq;
    bool converted;
    int err;

    if (streaming == false) {
        ei_printf("ERR: capture requested while the stream is stopped\n");
        return false;
    }

    vbuf->type = VIDEO_BUF_TYPE_OUTPUT;
    err = video_dequeue(camera_dev, &vbuf, timeout_ms < 0 ? K_FOREVER : K_MSEC(timeout_ms));
    if (err < 0) {
        ei_printf("ERR: unable to dequeue a frame (%d)\n", err);
        return false;
    }

    converted = frame_to_rgb888(vbuf);

    /* requeue before doing any work on the frame, so the sensor keeps running */
    err = video_enqueue(camera_dev, vbuf);
    if (err < 0) {
        ei_printf("ERR: unable to requeue the video buffer (%d)\n", err);
        return false;
    }

    if (converted == false) {
        return false;
    }

    /* crop to the model aspect ratio, then scale down to the model input,
     * in place - the destination is always the smaller of the two
     */
    if (cam_fmt.width != EI_CAMERA_MODEL_WIDTH || cam_fmt.height != EI_CAMERA_MODEL_HEIGHT) {
        err = ei::image::processing::crop_and_interpolate_rgb888(
            rgb888_buf, cam_fmt.width, cam_fmt.height,
            rgb888_buf, EI_CAMERA_MODEL_WIDTH, EI_CAMERA_MODEL_HEIGHT);
        if (err != 0) {
            ei_printf("ERR: unable to rescale the frame (%d)\n", err);
            return false;
        }
    }

    return true;
}

int ei_camera_get_data(size_t offset, size_t length, float *out_ptr)
{
    size_t pixel_ix = offset * 3;

    if (rgb888_buf == NULL) {
        return -1;
    }

    for (size_t i = 0; i < length; i++) {
        /* the impulse wants each pixel as a single 0x00RRGGBB float */
        out_ptr[i] = (float)((rgb888_buf[pixel_ix] << 16) + (rgb888_buf[pixel_ix + 1] << 8) +
                             rgb888_buf[pixel_ix + 2]);
        pixel_ix += 3;
    }

    return 0;
}

void ei_camera_get_resolution(uint32_t *width, uint32_t *height)
{
    if (width != NULL) {
        *width = cam_fmt.width;
    }
    if (height != NULL) {
        *height = cam_fmt.height;
    }
}
