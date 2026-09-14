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

#ifndef EI_CAMERA_H
#define EI_CAMERA_H

#include <stddef.h>
#include <stdint.h>

/*
 * Board agnostic camera front-end for Edge Impulse image models.
 *
 * The camera is taken from the devicetree `zephyr,camera` chosen node, so any
 * board (or board + camera shield) supported by the Zephyr video API works
 * without touching this code.
 */

/**
 * @brief Open the camera, negotiate a format and allocate the frame buffers.
 * @return true if successful
 */
bool ei_camera_init(void);

/**
 * @brief Release the frame buffers taken by ei_camera_init().
 * @return true if successful
 */
bool ei_camera_deinit(void);

/**
 * @brief Start the video stream.
 * @return true if successful
 */
bool ei_camera_start(void);

/**
 * @brief Stop the video stream.
 * @return true if successful
 */
bool ei_camera_stop(void);

/**
 * @brief Grab one frame, convert it to RGB888 and rescale it to the model
 *        input size. The result stays in an internal buffer that
 *        ei_camera_get_data() reads from.
 * @param timeout_ms how long to wait for a frame, -1 waits forever
 * @return true if a frame was captured and prepared
 */
bool ei_camera_capture(int32_t timeout_ms);

/**
 * @brief signal_t callback handing pixels to the impulse, as 0x00RRGGBB floats.
 * @param offset first pixel to return (in pixels, not bytes)
 * @param length number of pixels to return
 * @param out_ptr destination buffer
 * @return 0 if successful
 */
int ei_camera_get_data(size_t offset, size_t length, float *out_ptr);

/**
 * @brief Resolution the camera is actually streaming at, which is not the
 *        resolution the impulse runs on.
 */
void ei_camera_get_resolution(uint32_t *width, uint32_t *height);

#endif /* EI_CAMERA_H */
