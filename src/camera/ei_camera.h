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

#ifndef EI_CAMERA_H
#define EI_CAMERA_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configure the camera chosen in devicetree and allocate the frame
 *        buffers. Does not start the stream yet.
 * @return true if the camera is ready to be started
 */
bool ei_camera_init(void);

/**
 * @brief Start streaming frames from the camera.
 * @return true if successful
 */
bool ei_camera_start(void);

/**
 * @brief Stop streaming frames from the camera.
 * @return true if successful
 */
bool ei_camera_stop(void);

/**
 * @brief Grab one frame, centre-crop it to the model aspect ratio and rescale
 *        it into the internal RGB888 snapshot buffer used by
 *        ei_camera_get_data().
 * @return true if a frame was captured and converted
 */
bool ei_camera_capture(void);

/**
 * @brief signal_t::get_data callback handing the last captured frame to the
 *        classifier. Every feature is one pixel packed as 0x00RRGGBB.
 * @param offset   index of the first pixel to return
 * @param length   number of pixels to return
 * @param out_ptr  destination buffer, at least @p length floats
 * @return 0 on success
 */
int ei_camera_get_data(size_t offset, size_t length, float *out_ptr);

#ifdef __cplusplus
}
#endif

#endif // EI_CAMERA_H
