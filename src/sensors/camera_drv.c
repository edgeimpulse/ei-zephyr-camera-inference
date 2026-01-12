/* The Clear BSD License
 *
 * Copyright (c) 2025 EdgeImpulse Inc.
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

#include "ei_camera.h"
#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/drivers/video.h>

#if !DT_HAS_CHOSEN(zephyr_camera)
#error No camera chosen in devicetree. Missing "--shield" or "--snippet video-sw-generator" flag?
#endif

static int app_query_video_info(const struct device *const video_dev,
				struct video_caps *const caps,
				struct video_format *const fmt);
static int app_setup_video_selection(const struct device *const video_dev,
				     const struct video_format *const fmt);
static int app_setup_video_format(const struct device *const video_dev,
				  struct video_format *const fmt);
static int app_setup_video_frmival(const struct device *const video_dev,
				   struct video_format *const fmt);
static int app_setup_video_buffers(const struct device *const video_dev,
				   struct video_caps *const caps,
				   struct video_format *const fmt);
static bool RBG565ToRGB888(uint8_t *src_buf, uint8_t *dst_buf, uint32_t src_len);

const static struct device *const video_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_camera));
static struct video_buffer *vbuf = NULL;
static struct video_format fmt = {
    .type = VIDEO_BUF_TYPE_OUTPUT,
};
static struct video_caps caps = {
    .type = VIDEO_BUF_TYPE_OUTPUT,
};

static void local_print(const char *format, ...) {
    static char print_buf[1024] = { 0 };

    va_list args;
    va_start(args, format);
    int r = vsnprintf(print_buf, sizeof(print_buf), format, args);
    va_end(args);

    if(r > 0) {
        printf("%s", print_buf);
    }
}

uint8_t* camera_drv_capture(size_t* out_size)
{
    int ret;
    int attempts = 0;
    const int max_attempts = 3;

    while (attempts < max_attempts) {
        ret = video_dequeue(video_dev, &vbuf, K_MSEC(5000));
        
        if (ret == 0 && vbuf != NULL) {
            if (attempts == 0) {
                local_print("Frame captured! Size: %u bytes\n", vbuf->bytesused);
            }
            *out_size = vbuf->bytesused;
            return vbuf->buffer;
        }
        
        if (attempts == 0) {
            local_print("Capture failed (ret=%d), retrying...\n", ret);
        }
        attempts++;
        k_msleep(100);
    }
    
    local_print("Failed to capture after %d attempts\n", max_attempts);
    return NULL;
}

bool camera_drv_start_capture(void)
{
	int ret;

	if (vbuf == NULL) {
		local_print("vbuf is NULL in start_capture\n");
		return false;
	}

	ret = video_enqueue(video_dev, vbuf);
	if (ret < 0) {
		local_print("Unable to requeue video buf\n");
		return false;
	}

	return (ret == 0);
}

static int app_query_video_info(const struct device *const video_dev,
				struct video_caps *const caps,
				struct video_format *const fmt)
{
	int ret;

	local_print("Video device: %s\n", video_dev->name);

	if (!device_is_ready(video_dev)) {
		local_print("%s: video device is not ready\n", video_dev->name);
		return -ENOSYS;
	}

	/* Get capabilities */
	ret = video_get_caps(video_dev, caps);
	if (ret < 0) {
		local_print("Unable to retrieve video capabilities\n");
		return ret;
	}

	/* Capabilities available but not printed to reduce console spam */

	/* Get default/native format */
	ret = video_get_format(video_dev, fmt);
	if (ret < 0) {
		local_print("Unable to retrieve video format\n");
	}

	/* Adjust video format according to the configuration */
	fmt->width = CONFIG_VIDEO_FRAME_WIDTH;
	fmt->height = CONFIG_VIDEO_FRAME_HEIGHT;	
    
#if defined CONFIG_VIDEO_PIXEL_FORMAT
	if (strcmp(CONFIG_VIDEO_PIXEL_FORMAT, "") != 0) {
		fmt->pixelformat = VIDEO_FOURCC_FROM_STR(CONFIG_VIDEO_PIXEL_FORMAT);
	}
#else
	fmt->pixelformat = VIDEO_FOURCC_FROM_STR(CAMERA_DEFAULT_PIXEL_FORMAT);
#endif
	return 0;
}

static int app_setup_video_frmival(const struct device *const video_dev,
				   struct video_format *const fmt)
{
	struct video_frmival frmival = {};
	int ret;

	/* Skip frame interval enumeration to reduce console spam */

	ret = video_get_frmival(video_dev, &frmival);
	if (ret == -ENOTSUP || ret == -ENOSYS) {
		/* Frame rate control not supported - this is fine */
	} else if (ret < 0) {
		local_print("Error getting frame interval\n");
		return ret;
	}

	return 0;
}

static int app_setup_video_selection(const struct device *const video_dev,
				     const struct video_format *const fmt)
{
	struct video_selection sel = {
		.type = VIDEO_BUF_TYPE_OUTPUT,
	};
	int ret;

	/* Set the crop setting only if configured */
	if (CONFIG_VIDEO_SOURCE_CROP_WIDTH > 0) {
		sel.target = VIDEO_SEL_TGT_CROP;
		sel.rect.left = CONFIG_VIDEO_SOURCE_CROP_LEFT;
		sel.rect.top = CONFIG_VIDEO_SOURCE_CROP_TOP;
		sel.rect.width = CONFIG_VIDEO_SOURCE_CROP_WIDTH;
		sel.rect.height = CONFIG_VIDEO_SOURCE_CROP_HEIGHT;

		ret = video_set_selection(video_dev, &sel);
		if (ret < 0) {
			local_print("Unable to set selection crop\n");
			return ret;
		}

		local_print("Crop window set to (%u,%u)/%ux%u",
			sel.rect.left, sel.rect.top, sel.rect.width, sel.rect.height);
	}


	/*
	 * Check (if possible) if targeted size is same as crop
	 * and if compose is necessary
	 */
	sel.target = VIDEO_SEL_TGT_CROP;
	ret = video_get_selection(video_dev, &sel);
	if (ret < 0 && ret != -ENOSYS) {
		local_print("Unable to get selection crop\n");
		return ret;
	}

	if (ret == 0 && (sel.rect.width != fmt->width || sel.rect.height != fmt->height)) {
		sel.target = VIDEO_SEL_TGT_COMPOSE;
		sel.rect.left = 0;
		sel.rect.top = 0;
		sel.rect.width = fmt->width;
		sel.rect.height = fmt->height;

		ret = video_set_selection(video_dev, &sel);
		if (ret < 0 && ret != -ENOSYS) {
			local_print("Unable to set selection compose\n");
			return ret;
		}

		local_print("Compose window set to (%u,%u)/%ux%u\n",
			sel.rect.left, sel.rect.top, sel.rect.width, sel.rect.height);
	}

	return 0;
}

static int app_setup_video_format(const struct device *const video_dev,
				  struct video_format *const fmt)
{
	int ret;

	local_print("- Video format: %s %ux%u\n",
		VIDEO_FOURCC_TO_STR(fmt->pixelformat), fmt->width, fmt->height);

	ret = video_set_format(video_dev, fmt);
	if (ret < 0) {
		local_print("Unable to set format (error: %d)\n", ret);
		return ret;
	}

	return 0;
}

static int app_setup_video_buffers(const struct device *const video_dev,
				   struct video_caps *const caps,
				   struct video_format *const fmt)
{
	int ret;

	/* Alloc video buffers and enqueue for capture */
	if (caps->min_vbuf_count > CONFIG_VIDEO_BUFFER_POOL_NUM_MAX) {
		local_print("Not enough buffers to start streaming\n");
		return -EINVAL;
	}

	for (int i = 0; i < CONFIG_VIDEO_BUFFER_POOL_NUM_MAX; i++) {
		struct video_buffer *vbuf;

		/*
		 * For some hardwares, such as the PxP used on i.MX RT1170 to do image rotation,
		 * buffer alignment is needed in order to achieve the best performance
		 */
		local_print("Allocating video buffer %d size %u\n", i, fmt->size);
		vbuf = video_buffer_aligned_alloc(fmt->size, CONFIG_VIDEO_BUFFER_POOL_ALIGN,
						  K_NO_WAIT);
		if (vbuf == NULL) {
			local_print("Unable to alloc video buffer\n");
			return -ENOMEM;
		}

		vbuf->type = VIDEO_BUF_TYPE_OUTPUT;

		ret = video_enqueue(video_dev, vbuf);
		if (ret < 0) {
			local_print("Failed to enqueue video buffer\n");
			return ret;
		}
	}

	return 0;
}

int camera_drv_init(uint16_t width, uint16_t height)
{
    int ret;

    local_print("Initializing camera...\n");
    ret = app_query_video_info(video_dev, &caps, &fmt);
	if (ret < 0) {
        local_print("Error in video info: %d\n", ret);
		return ret;
	}

	ret = app_setup_video_selection(video_dev, &fmt);
	if (ret < 0) {
        local_print("Error in video selection: %d\n", ret);
		return ret;
	}
    
	ret = app_setup_video_format(video_dev, &fmt);
	if (ret < 0) {
        local_print("Error in video format: %d\n", ret);
		return ret;
	}

    ret = app_setup_video_frmival(video_dev, &fmt);
	if (ret < 0) {
        local_print("Error in frame interval: %d\n", ret);
		return ret;
	}

    ret = app_setup_video_buffers(video_dev, &caps, &fmt);
	if (ret < 0) {
        local_print("Error in video buffers: %d\n", ret);
		return ret;
	}

	ret = video_stream_start(video_dev, VIDEO_BUF_TYPE_OUTPUT);
	if (ret < 0) {
		local_print("Unable to start capture\n");
		return ret;
	}
	
	/* Give camera time to stabilize */
	local_print("Camera stabilizing (10s)...\n");
	k_msleep(10000);
	
	local_print("Camera ready!\n");

    return ret;
}

static bool RBG565ToRGB888(uint8_t *src_buf, uint8_t *dst_buf, uint32_t src_len)
{
    uint8_t hb, lb;
    uint32_t pix_count = src_len / 2;

    for(uint32_t i = 0; i < pix_count; i ++) {
        hb = *src_buf++;
        lb = *src_buf++;

        *dst_buf++ = hb & 0xF8;
        *dst_buf++ = (hb & 0x07) << 5 | (lb & 0xE0) >> 3;
        *dst_buf++ = (lb & 0x1F) << 3;
    }

    return true;
}
