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

#include "inference/inferencing.h"
#include "camera/ei_camera.h"
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"

static bool ei_run_inference(void);
static bool ei_start_impulse(void);
static bool ei_stop_impulse(void);

typedef enum {
    INFERENCE_STATE_RUNNING,
    INFERENCE_STATE_SAMPLING,
    INFERENCE_STATE_DATA_READY,
    INFERENCE_STATE_STOP
} inference_state_t;

static inference_state_t state = INFERENCE_STATE_SAMPLING;

/**
 * @brief Inference state machine
 * @return true
 */
bool ei_inference_sm(void)
{
    if (ei_start_impulse() == false) {
        return false;
    }
    state = INFERENCE_STATE_SAMPLING;

    while (INFERENCE_STATE_STOP != state) {
        switch (state) {
            case INFERENCE_STATE_SAMPLING:
                /* one frame per inference, no windowing to do like on a
                 * time series sensor
                 */
                if (ei_camera_capture(CONFIG_EI_CAMERA_CAPTURE_TIMEOUT_MS) == false) {
                    /* dropped frame, try again with the next one */
                    break;
                }
                state = INFERENCE_STATE_DATA_READY;
                break;
            case INFERENCE_STATE_DATA_READY:
                state = INFERENCE_STATE_RUNNING;
                break;
            case INFERENCE_STATE_RUNNING:
                if (ei_run_inference() == false) {
                    ei_printf("ERR: Inference failed\n");
                }
                if (CONFIG_EI_CAMERA_INFERENCE_DELAY_MS > 0) {
                    ei_sleep(CONFIG_EI_CAMERA_INFERENCE_DELAY_MS);
                }
                state = INFERENCE_STATE_SAMPLING;   // and back grabbing frames
                break;
            case INFERENCE_STATE_STOP:  // in this example we never reach this state
                                        // but could be useful for your application
                break;
        }
    }

    ei_stop_impulse();

    return true;
}

/**
 * @brief Run inference on the frame currently held by the camera driver
 * @return true if successful
 */
static bool ei_run_inference(void)
{
    ei_impulse_result_t result = {nullptr};
    ei::signal_t features_signal;

    /* the signal is one float per pixel, pulled straight out of the camera
     * buffer by ei_camera_get_data() - no intermediate feature array
     */
    features_signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
    features_signal.get_data = &ei_camera_get_data;

    EI_IMPULSE_ERROR res = run_classifier(&features_signal, &result, false);

    if (res != EI_IMPULSE_OK) {
        ei_printf("ERR: Failed to run classifier\n");
        ei_printf("ERR: %d\n", res);
        return false;
    }

    display_results(&ei_default_impulse, &result);

    return true;
}

/**
 * @brief Start inference process
 * @return true if successful
 */
static bool ei_start_impulse(void)
{
    ei_printf("Edge Impulse camera inferencing on Zephyr\n");

    ei_printf("Inferencing settings:\n");
    ei_printf("\tImage resolution: %dx%d\n", EI_CLASSIFIER_INPUT_WIDTH,
              EI_CLASSIFIER_INPUT_HEIGHT);
    ei_printf("\tFrame size: %d\n", EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE);
    ei_printf("\tNumber of output classes: %d\n",
              sizeof(ei_classifier_inferencing_categories) /
                  sizeof(ei_classifier_inferencing_categories[0]));

    if (ei_camera_start() == false) {
        ei_printf("ERR: Failed to start the camera\n");
        return false;
    }

    return true;
}

/**
 * @brief Stop inference process
 * @return true if successful
 */
static bool ei_stop_impulse(void)
{
    ei_printf("Stopping inferencing\n");
    ei_camera_stop();
    state = INFERENCE_STATE_STOP;

    return true;
}
