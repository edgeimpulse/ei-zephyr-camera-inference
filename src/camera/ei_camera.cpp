/* The Clear BSD License
 *
 * Copyright (c) 2026 EdgeImpulse Inc.
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

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/video/video.h>

#ifdef CONFIG_VIDEO_STM32_DCMI
#include <zephyr/drivers/video/stm32_dcmi.h>
#endif

#include "camera/ei_camera.h"
#include "model-parameters/model_metadata.h"

LOG_MODULE_REGISTER(ei_camera, CONFIG_LOG_DEFAULT_LEVEL);

#if !DT_HAS_CHOSEN(zephyr_camera)
#error "No camera chosen in the devicetree. Add a 'zephyr,camera' chosen node, " \
       "pass --shield <camera shield> or use --snippet video-sw-generator."
#endif

#if EI_CLASSIFIER_SENSOR != EI_CLASSIFIER_SENSOR_CAMERA
#error "The impulse in model-parameters was not trained on camera data."
#endif

/* The model input is always RGB888 packed, one byte per channel */
#define EI_CAMERA_BYTES_PER_PIXEL 3
#define EI_CAMERA_SNAPSHOT_SIZE                                                                    \
	(EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT * EI_CAMERA_BYTES_PER_PIXEL)

/* Frames dropped after starting the stream, to let auto exposure/white balance settle */
#define EI_CAMERA_WARMUP_FRAMES 4

/*
 * VIDEO_FOURCC_TO_STR() expands to a stack compound literal, which is already
 * out of scope by the time deferred logging formats the message. Expand the
 * four characters as arguments instead so nothing has to outlive the call.
 */
#define EI_FOURCC_FMT "%c%c%c%c"
#define EI_FOURCC_ARGS(f)                                                                          \
	(char)((f) & 0xff), (char)(((f) >> 8) & 0xff), (char)(((f) >> 16) & 0xff),                  \
		(char)(((f) >> 24) & 0xff)

static const struct device *const camera_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_camera));

static struct video_format cam_fmt;
static bool camera_streaming;

/* The cropped and rescaled frame handed to the classifier, as packed RGB888 */
static uint8_t snapshot_buf[EI_CAMERA_SNAPSHOT_SIZE];

/**
 * @brief Read one source pixel and expand it to RGB888
 *
 * Only the formats we ask the sensor for are handled; ei_camera_init() rejects
 * anything else so this never sees an unknown pixel format.
 */
static inline void src_get_pixel(const uint8_t *const buf, const uint32_t pitch, const int x,
				 const int y, uint8_t *const rgb)
{
	switch (cam_fmt.pixelformat) {
	case VIDEO_PIX_FMT_RGB565:
	case VIDEO_PIX_FMT_RGB565X: {
		const uint8_t *px = &buf[(y * pitch) + (x * 2)];
		uint16_t val;

		/* RGB565 is little endian, RGB565X is big endian; the swap option
		 * forces the big endian reading for drivers that mislabel the frame.
		 */
		if (cam_fmt.pixelformat == VIDEO_PIX_FMT_RGB565 &&
		    !IS_ENABLED(CONFIG_EI_CAMERA_RGB565_BYTE_SWAP)) {
			val = (uint16_t)px[0] | ((uint16_t)px[1] << 8);
		} else {
			val = (uint16_t)px[1] | ((uint16_t)px[0] << 8);
		}

		const uint8_t r5 = (val >> 11) & 0x1f;
		const uint8_t g6 = (val >> 5) & 0x3f;
		const uint8_t b5 = val & 0x1f;

		/* replicate the top bits into the padding so 0x1f maps to 0xff */
		rgb[0] = (uint8_t)((r5 << 3) | (r5 >> 2));
		rgb[1] = (uint8_t)((g6 << 2) | (g6 >> 4));
		rgb[2] = (uint8_t)((b5 << 3) | (b5 >> 2));
		break;
	}
	case VIDEO_PIX_FMT_RGB24: {
		const uint8_t *px = &buf[(y * pitch) + (x * 3)];

		rgb[0] = px[0];
		rgb[1] = px[1];
		rgb[2] = px[2];
		break;
	}
	default:
		rgb[0] = rgb[1] = rgb[2] = 0;
		break;
	}
}

/**
 * @brief Centre-crop the captured frame to the model aspect ratio and rescale it
 *        into snapshot_buf as RGB888, using bilinear interpolation.
 *
 * Doing the colour conversion while resampling means we never need a full
 * resolution RGB888 copy of the frame, which does not fit in SRAM on the
 * smaller targets.
 */
static void frame_to_snapshot(const uint8_t *const buf)
{
	const int src_w = (int)cam_fmt.width;
	const int src_h = (int)cam_fmt.height;
	const int dst_w = EI_CLASSIFIER_INPUT_WIDTH;
	const int dst_h = EI_CLASSIFIER_INPUT_HEIGHT;

	/* largest centred rectangle with the aspect ratio of the model input */
	int crop_w = src_w;
	int crop_h = (int)(((int64_t)src_w * dst_h) / dst_w);

	if (crop_h > src_h) {
		crop_h = src_h;
		crop_w = (int)(((int64_t)src_h * dst_w) / dst_h);
	}

	const float crop_x0 = (src_w - crop_w) / 2.0f;
	const float crop_y0 = (src_h - crop_h) / 2.0f;
	const float x_ratio = (float)crop_w / (float)dst_w;
	const float y_ratio = (float)crop_h / (float)dst_h;

	uint8_t *out = snapshot_buf;

	for (int dy = 0; dy < dst_h; dy++) {
		/* map the centre of the destination pixel back into the source */
		float sy = crop_y0 + ((dy + 0.5f) * y_ratio) - 0.5f;
		int y0 = (int)sy;
		float wy = sy - (float)y0;

		if (y0 < 0) {
			y0 = 0;
			wy = 0.0f;
		}
		int y1 = (y0 + 1 < src_h) ? y0 + 1 : y0;

		for (int dx = 0; dx < dst_w; dx++) {
			float sx = crop_x0 + ((dx + 0.5f) * x_ratio) - 0.5f;
			int x0 = (int)sx;
			float wx = sx - (float)x0;

			if (x0 < 0) {
				x0 = 0;
				wx = 0.0f;
			}
			int x1 = (x0 + 1 < src_w) ? x0 + 1 : x0;

			uint8_t p00[3], p01[3], p10[3], p11[3];

			src_get_pixel(buf, cam_fmt.pitch, x0, y0, p00);
			src_get_pixel(buf, cam_fmt.pitch, x1, y0, p01);
			src_get_pixel(buf, cam_fmt.pitch, x0, y1, p10);
			src_get_pixel(buf, cam_fmt.pitch, x1, y1, p11);

			for (int c = 0; c < EI_CAMERA_BYTES_PER_PIXEL; c++) {
				const float top = p00[c] + ((p01[c] - p00[c]) * wx);
				const float bot = p10[c] + ((p11[c] - p10[c]) * wx);

				*out++ = (uint8_t)(top + ((bot - top) * wy) + 0.5f);
			}
		}
	}
}

static int camera_setup_format(void)
{
	struct video_caps caps = {
		.type = VIDEO_BUF_TYPE_OUTPUT,
	};
	int ret;

	ret = video_get_caps(camera_dev, &caps);
	if (ret < 0) {
		LOG_ERR("Unable to retrieve video capabilities (%d)", ret);
		return ret;
	}

	LOG_INF("- Capabilities:");
	for (int i = 0; caps.format_caps[i].pixelformat; i++) {
		const struct video_format_cap *fcap = &caps.format_caps[i];

		LOG_INF("  " EI_FOURCC_FMT " width [%u; %u; %u] height [%u; %u; %u]",
			EI_FOURCC_ARGS(fcap->pixelformat), fcap->width_min, fcap->width_max,
			fcap->width_step, fcap->height_min, fcap->height_max, fcap->height_step);
	}

	if (caps.min_vbuf_count > CONFIG_EI_CAMERA_NUM_BUFS) {
		LOG_ERR("Camera needs %u buffers, CONFIG_EI_CAMERA_NUM_BUFS is %u",
			caps.min_vbuf_count, CONFIG_EI_CAMERA_NUM_BUFS);
		return -EINVAL;
	}

	/* start from the sensor default so that fields we do not set stay sane */
	cam_fmt.type = VIDEO_BUF_TYPE_OUTPUT;
	ret = video_get_format(camera_dev, &cam_fmt);
	if (ret < 0) {
		LOG_ERR("Unable to retrieve video format (%d)", ret);
		return ret;
	}

	cam_fmt.width = CONFIG_EI_CAMERA_WIDTH;
	cam_fmt.height = CONFIG_EI_CAMERA_HEIGHT;
	cam_fmt.pixelformat = VIDEO_PIX_FMT_RGB565;

	/*
	 * Prefer the compose helper: on capture paths that can scale (the ESP32
	 * LCD_CAM) it sets the compose rectangle before the format, which is what
	 * makes an arbitrary output size work.
	 *
	 * It is not usable everywhere though. video_set_compose_format() only
	 * tolerates -ENOSYS from video_set_selection() and gives up on any other
	 * error, and STM32 DCMI -- which has no compose support at all -- returns
	 * -EINVAL. Fall back to setting the format directly in that case.
	 */
	ret = video_set_compose_format(camera_dev, &cam_fmt);
	if (ret < 0) {
		LOG_DBG("Compose not usable (%d), setting the format directly", ret);
		ret = video_set_format(camera_dev, &cam_fmt);
	}
	if (ret < 0) {
		LOG_ERR("Unable to set format " EI_FOURCC_FMT " %ux%u (%d)",
			EI_FOURCC_ARGS(cam_fmt.pixelformat), cam_fmt.width, cam_fmt.height, ret);
		return ret;
	}

	/* read back: the driver is free to round the geometry to what it supports */
	ret = video_get_format(camera_dev, &cam_fmt);
	if (ret < 0) {
		LOG_ERR("Unable to read back video format (%d)", ret);
		return ret;
	}

	if (cam_fmt.pitch == 0) {
		/* not every driver fills the pitch in; assume tightly packed lines */
		cam_fmt.pitch =
			cam_fmt.width * video_bits_per_pixel(cam_fmt.pixelformat) / BITS_PER_BYTE;
	}

	LOG_INF("- Capturing " EI_FOURCC_FMT " %ux%u (pitch %u, %u bytes/frame)",
		EI_FOURCC_ARGS(cam_fmt.pixelformat), cam_fmt.width, cam_fmt.height, cam_fmt.pitch,
		cam_fmt.size);

	switch (cam_fmt.pixelformat) {
	case VIDEO_PIX_FMT_RGB565:
	case VIDEO_PIX_FMT_RGB565X:
	case VIDEO_PIX_FMT_RGB24:
		break;
	default:
		LOG_ERR("Pixel format " EI_FOURCC_FMT " is not supported by this sample",
			EI_FOURCC_ARGS(cam_fmt.pixelformat));
		return -ENOTSUP;
	}

	if (cam_fmt.width < EI_CLASSIFIER_INPUT_WIDTH ||
	    cam_fmt.height < EI_CLASSIFIER_INPUT_HEIGHT) {
		LOG_WRN("Capturing %ux%u, smaller than the %ux%u model input: the frame will be "
			"upscaled",
			cam_fmt.width, cam_fmt.height, EI_CLASSIFIER_INPUT_WIDTH,
			EI_CLASSIFIER_INPUT_HEIGHT);
	}

	return 0;
}

static void camera_setup_controls(void)
{
	struct video_control ctrl;
	int ret;

#ifdef CONFIG_VIDEO_STM32_DCMI
	/*
	 * Ask the DCMI for snapshot mode.
	 *
	 * In continuous mode it holds one buffer permanently as the DMA target and
	 * needs a second one to copy into from HAL_DCMI_FrameEventCallback(), and
	 * it streams non-stop while we are busy classifying. Snapshot mode instead
	 * captures a single frame per video_dequeue(), which is exactly this
	 * application's pattern: grab a frame, then spend ~200 ms on inference with
	 * nothing running in the background.
	 */
	ctrl.id = VIDEO_CID_ST_SNAPSHOT_MODE;
	ctrl.val = 1;
	ret = video_set_ctrl(camera_dev, &ctrl);
	if (ret < 0) {
		LOG_WRN("Unable to enable DCMI snapshot mode (%d)", ret);
	} else {
		LOG_INF("DCMI snapshot mode enabled");
	}
#endif

	if (IS_ENABLED(CONFIG_EI_CAMERA_HFLIP)) {
		ctrl.id = VIDEO_CID_HFLIP;
		ctrl.val = 1;
		ret = video_set_ctrl(camera_dev, &ctrl);
		if (ret < 0) {
			LOG_WRN("Failed to set horizontal flip (%d)", ret);
		}
	}

	if (IS_ENABLED(CONFIG_EI_CAMERA_VFLIP)) {
		ctrl.id = VIDEO_CID_VFLIP;
		ctrl.val = 1;
		ret = video_set_ctrl(camera_dev, &ctrl);
		if (ret < 0) {
			LOG_WRN("Failed to set vertical flip (%d)", ret);
		}
	}
}

static int camera_setup_buffers(void)
{
	for (uint8_t i = 0; i < CONFIG_EI_CAMERA_NUM_BUFS; i++) {
		struct video_buffer *vbuf;
		int ret;

		vbuf = video_buffer_aligned_alloc(cam_fmt.size, CONFIG_VIDEO_BUFFER_POOL_ALIGN,
						  K_NO_WAIT);
		if (vbuf == NULL) {
			LOG_ERR("Unable to alloc video buffer %u of %u bytes, increase "
				"CONFIG_VIDEO_BUFFER_POOL_HEAP_SIZE",
				i, cam_fmt.size);
			return -ENOMEM;
		}

		vbuf->type = VIDEO_BUF_TYPE_OUTPUT;

		ret = video_enqueue(camera_dev, vbuf);
		if (ret < 0) {
			LOG_ERR("Failed to enqueue video buffer (%d)", ret);
			return ret;
		}
	}

	return 0;
}

bool ei_camera_init(void)
{
	if ((CONFIG_EI_CAMERA_WIDTH < EI_CLASSIFIER_INPUT_WIDTH) || (CONFIG_EI_CAMERA_HEIGHT < EI_CLASSIFIER_INPUT_HEIGHT)) {
		LOG_ERR("Camera resolution is smaller than model input, please set EI_CAMERA_WIDTH and EI_CAMERA_HEIGHT accordingly");
		return 0;
	}

	if (!device_is_ready(camera_dev)) {
		LOG_ERR("%s: camera device is not ready", camera_dev->name);
		return false;
	}

	LOG_INF("Camera device: %s", camera_dev->name);

	if (camera_setup_format() < 0) {
		return false;
	}

	camera_setup_controls();

	if (camera_setup_buffers() < 0) {
		return false;
	}

	return true;
}

/*
 * Hand every buffer we own back to the driver's input queue.
 *
 * video_esp32_dvp's ISR gives up and stops reloading the DMA the moment its
 * input queue is empty, and enqueue() never restarts it, so streaming dies
 * permanently the first time the application does not keep up. We therefore
 * only stream while actually grabbing a frame, which means every buffer has to
 * be back on the input side before the next start.
 */
static void camera_requeue_all(void)
{
	while (true) {
		struct video_buffer req = {};
		struct video_buffer *vbuf = &req;

		req.type = VIDEO_BUF_TYPE_OUTPUT;

		if (video_dequeue(camera_dev, &vbuf, K_NO_WAIT) < 0) {
			return;
		}

		video_enqueue(camera_dev, vbuf);
	}
}

static int camera_stream_on(void)
{
	int ret;

	if (camera_streaming) {
		return 0;
	}

	ret = video_stream_start(camera_dev, VIDEO_BUF_TYPE_OUTPUT);
	if (ret < 0) {
		LOG_ERR("Unable to start capture (%d)", ret);
		return ret;
	}

	camera_streaming = true;

	return 0;
}

static int camera_stream_off(void)
{
	int ret;

	ret = video_stream_stop(camera_dev, VIDEO_BUF_TYPE_OUTPUT);
	if (ret < 0) {
		LOG_ERR("Unable to stop capture (%d)", ret);
		return ret;
	}

	camera_streaming = false;
	camera_requeue_all();

	return 0;
}

bool ei_camera_start(void)
{
	if (camera_stream_on() < 0) {
		return false;
	}

	/* the first frames come out before auto exposure has converged */
	for (int i = 0; i < EI_CAMERA_WARMUP_FRAMES; i++) {
		struct video_buffer req = {};
		struct video_buffer *vbuf = &req;

		req.type = VIDEO_BUF_TYPE_OUTPUT;

		if (video_dequeue(camera_dev, &vbuf, K_MSEC(1000)) < 0) {
			break;
		}
		video_enqueue(camera_dev, vbuf);
	}

	if (IS_ENABLED(CONFIG_EI_CAMERA_STREAM_PER_FRAME)) {
		camera_stream_off();
	}

	LOG_INF("Camera ready");

	return true;
}

bool ei_camera_stop(void)
{
	return camera_stream_off() == 0;
}

bool ei_camera_capture(void)
{
	/* video_dequeue() replaces the pointer, it only reads .type from it */
	struct video_buffer req = {};
	struct video_buffer *vbuf = &req;
	bool ok = false;
	int ret;

	req.type = VIDEO_BUF_TYPE_OUTPUT;

	if (camera_stream_on() < 0) {
		return false;
	}

	ret = video_dequeue(camera_dev, &vbuf, K_MSEC(2000));
	if (ret < 0) {
		LOG_ERR("Unable to dequeue video buffer (%d)", ret);
		goto out;
	}

	if (vbuf->bytesused < cam_fmt.size) {
		LOG_WRN("Short frame: %u of %u bytes", vbuf->bytesused, cam_fmt.size);
	}

	frame_to_snapshot(vbuf->buffer);

	ret = video_enqueue(camera_dev, vbuf);
	if (ret < 0) {
		LOG_ERR("Unable to requeue video buffer (%d)", ret);
		goto out;
	}

	ok = true;
out:
	if (IS_ENABLED(CONFIG_EI_CAMERA_STREAM_PER_FRAME)) {
		/* stop before the caller goes off to run the classifier */
		camera_stream_off();
	}

	return ok;
}

int ei_camera_get_data(size_t offset, size_t length, float *out_ptr)
{
	/* the classifier asks for pixels, snapshot_buf holds 3 bytes per pixel */
	size_t pixel_ix = offset * EI_CAMERA_BYTES_PER_PIXEL;

	if ((offset + length) * EI_CAMERA_BYTES_PER_PIXEL > sizeof(snapshot_buf)) {
		return -1;
	}

	for (size_t i = 0; i < length; i++) {
		out_ptr[i] = (float)((snapshot_buf[pixel_ix] << 16) +
				     (snapshot_buf[pixel_ix + 1] << 8) + snapshot_buf[pixel_ix + 2]);
		pixel_ix += EI_CAMERA_BYTES_PER_PIXEL;
	}

	return 0;
}
