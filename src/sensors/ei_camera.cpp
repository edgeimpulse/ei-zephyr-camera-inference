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
#include "camera_drv.h"
#include "edge-impulse-sdk/porting/ei_classifier_porting.h"
#include "edge-impulse-sdk/dsp/image/image.hpp"
#include <string.h>
/**
 * @brief Initialize the camera with specified width and height.
 */
bool ei_camera_init(uint16_t width, uint16_t height)
{
    int ret = camera_drv_init(width, height);
    if (ret != 0) {
        ei_printf("Camera init error cam width %d height %d error %d!\n", width, height, ret);
        return false;
    }
    else {
        ei_printf("Camera initialised!\n");
    }
    
    return (ret == 0);
}

/**
 * @brief Capture an image from the camera into the provided buffer.
 */
int ei_camera_capture(uint8_t* buffer, uint16_t width, uint16_t height, size_t* out_size)
{
    uint8_t* p_buffer = nullptr;

    if (buffer == nullptr || width == 0 || height == 0) {
        ei_printf("Invalid buffer\n");
        return -1;
    }

    camera_drv_capture(&p_buffer, out_size);

    if (p_buffer == nullptr) {
        ei_printf("buffer zero\n");
        return -1;
    }

    if (*out_size == 0) {
        ei_printf("out_size zero\n");
        return -1;
    }
#if 0
    ei::image::processing::crop_and_interpolate_image(
            p_buffer,
            CONFIG_VIDEO_FRAME_WIDTH,
            CONFIG_VIDEO_FRAME_HEIGHT,
            buffer,
            width,
            height,
            2);
#else
    memset(buffer, 0, width * height * 3);
#endif
    return 0;
}

/**
 * @brief Check if the frame buffer is ready.
 */
bool ei_camera_get_fb_ready(void)
{
    return false;
}
