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

#include "inference/inferencing.h"
#include "camera/ei_camera.h"
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"
#include "edge-impulse-sdk/dsp/numpy.hpp"

static bool ei_run_inference(void);

typedef enum {
    INFERENCE_STATE_SAMPLING,
    INFERENCE_STATE_DATA_READY,
    INFERENCE_STATE_RUNNING,
    INFERENCE_STATE_STOP
} inference_state_t;

static inference_state_t state = INFERENCE_STATE_STOP;

/**
 * @brief Inference state machine
 * @return true if the state machine exited cleanly
 */
bool ei_inference_sm(void)
{
    if (ei_start_inference() == false) {
        return false;
    }

    state = INFERENCE_STATE_SAMPLING;

    while (INFERENCE_STATE_STOP != state) {
        switch (state) {
            case INFERENCE_STATE_SAMPLING:
                /* unlike a sensor feeding samples in over time, a camera gives us a
                 * whole frame in one go, so one capture fills the input buffer */
                if (ei_camera_capture() == false) {
                    ei_printf("ERR: Failed to capture a frame\n");
                    state = INFERENCE_STATE_STOP;
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
                if (CONFIG_EI_INFERENCE_INTERVAL_MS > 0) {
                    ei_sleep(CONFIG_EI_INFERENCE_INTERVAL_MS);
                }
                state = INFERENCE_STATE_SAMPLING;   // and back to grabbing a frame
                break;

            case INFERENCE_STATE_STOP:
                break;
        }
    }

    ei_camera_stop();
    ei_printf("Stopped inferencing\n");

    return true;
}

/**
 * @brief Run the impulse over the last captured frame
 * @return true if successful
 */
static bool ei_run_inference(void)
{
    ei_impulse_result_t result = { 0 };
    signal_t signal;

    signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
    signal.get_data = &ei_camera_get_data;

    EI_IMPULSE_ERROR res = run_classifier(&signal, &result,
                                          IS_ENABLED(CONFIG_EI_INFERENCE_DEBUG));

    if (res != EI_IMPULSE_OK) {
        ei_printf("ERR: Failed to run classifier (%d)\n", res);
        return false;
    }

    display_results(&ei_default_impulse, &result);

#if EI_CLASSIFIER_OBJECT_DETECTION == 1
    /* FOMO prints nothing when the frame is empty, so say so explicitly */
    uint32_t found = 0;

    for (uint32_t i = 0; i < result.bounding_boxes_count; i++) {
        if (result.bounding_boxes[i].value > 0) {
            found++;
        }
    }

    if (found == 0) {
        ei_printf("  No objects found\n");
    }
#endif

    return true;
}

/**
 * @brief Print the impulse settings and get the camera and classifier ready
 * @return true if successful
 */
bool ei_start_inference(void)
{
    ei_printf("Edge Impulse start inferencing on Zephyr\n");

    ei_printf("Inferencing settings:\n");
    ei_printf("\tImage resolution: %dx%d\n", EI_CLASSIFIER_INPUT_WIDTH, EI_CLASSIFIER_INPUT_HEIGHT);
    ei_printf("\tFrame size: %d\n", EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE);
    ei_printf("\tNo. of classes: %d\n",
              (int)(sizeof(ei_classifier_inferencing_categories) /
                    sizeof(ei_classifier_inferencing_categories[0])));
    ei_printf("\tObject detection: %s\n", EI_CLASSIFIER_OBJECT_DETECTION ? "yes" : "no");

    if (ei_camera_start() == false) {
        ei_printf("ERR: Failed to start the camera\n");
        return false;
    }

    run_classifier_init();

    return true;
}

/**
 * @brief Ask the state machine to stop
 * @return true if successful
 */
bool ei_stop_inference(void)
{
    ei_printf("Stopping inferencing\n");
    state = INFERENCE_STATE_STOP;

    return true;
}
