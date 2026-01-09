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
#
#include "inference/inferencing.h"
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"
#include "edge-impulse-sdk/dsp/numpy.hpp"
#include "sensors/ei_camera.h"

#if (defined(EI_CLASSIFIER_SENSOR) && (EI_CLASSIFIER_SENSOR != EI_CLASSIFIER_SENSOR_CAMERA))
#error "This inference project is only for camera sensor"
#endif

static uint8_t snapshot_buf[EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT * 3] __attribute__((aligned(32), section(".ext_ram.bss")));

static bool ei_run_inference(void);
static bool ei_start_impulse(void);
static bool ei_stop_impulse(void);
static int ei_camera_get_data(size_t offset, size_t length, float *out_ptr);

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
    size_t out_size;
    ei_start_impulse();
    
    state = INFERENCE_STATE_SAMPLING;

    while(INFERENCE_STATE_STOP != state) {
        switch(state){
            case INFERENCE_STATE_SAMPLING:
                // capture image from camera
                if (ei_camera_capture(snapshot_buf, EI_CLASSIFIER_INPUT_WIDTH, EI_CLASSIFIER_INPUT_HEIGHT, &out_size) != 0) {
                    ei_printf("ERR: Failed to capture image from camera\n");
                    state = INFERENCE_STATE_STOP;
                    break;
                }
                memset(snapshot_buf, 0, sizeof(snapshot_buf));     
            case INFERENCE_STATE_DATA_READY:
                ei_printf("Data ready\n");
                // run inference, not much to do in this example
                state = INFERENCE_STATE_RUNNING;
                break;
            case INFERENCE_STATE_RUNNING:
                ei_printf("run inference\n");
                if (ei_run_inference() == false) {
                    ei_printf("ERR: Inference failed\n");
                    state = INFERENCE_STATE_STOP;
                    break;
                }
                state = INFERENCE_STATE_SAMPLING;   // and back sampling
                break;
            case INFERENCE_STATE_STOP:  // in this example we never reach this state
                                        // but could be useful for your application
                break;
        }
    }

    ei_stop_impulse();

    return state;
}

/**
 * @brief Run inference process
 * @return true if successful
 */
static bool ei_run_inference(void)
{
    bool ret = true;
    ei_impulse_result_t result = { 0};

    signal_t features_signal;
    features_signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
    features_signal.get_data = &ei_camera_get_data;

    // invoke the impulse
    EI_IMPULSE_ERROR res = run_classifier(&features_signal, &result, false);

    if (res != 0) {
        ei_printf("ERR: Failed to run classifier\n");
        ei_printf("ERR: %d\n", res);
        ret = false;
    }
    else {
        display_results(&ei_default_impulse, &result);
    }

    return ret;
}

/**
 * @brief Start inference process
 * @return true if successful
 */
static bool ei_start_impulse(void)
{
    ei_printf("Edge Impulse start inferencing on Zephyr\n");

    ei_printf("Inferencing settings:\n");
    ei_printf("\tClassifier interval: %.2f ms.\n", (float)EI_CLASSIFIER_INTERVAL_MS);
    ei_printf("\tInput frame size: %d\n", EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE);
    ei_printf("\tNumber of output classes: %d\n", sizeof(ei_classifier_inferencing_categories) / sizeof(ei_classifier_inferencing_categories[0]));

    // let's start, we will continuously run inference
    run_classifier_init();

    return true;
}

/**
 * @brief Stop inference process
 * @return true if successful
 */
static bool ei_stop_impulse(void)
{
    ei_printf("Stopping inferencing\n");
    state = INFERENCE_STATE_STOP;

    return true;
}

/**
 *
 * @param offset
 * @param length
 * @param out_ptr
 * @return
 */
static int ei_camera_get_data(size_t offset, size_t length, float *out_ptr)
{    
    // we already have a RGB888 buffer, so recalculate offset into pixel index
    size_t pixel_ix = offset * 3;
    size_t pixels_left = length;
    size_t out_ptr_ix = 0;

    while (pixels_left != 0) {
        out_ptr[out_ptr_ix] = (snapshot_buf[pixel_ix] << 16) + (snapshot_buf[pixel_ix + 1] << 8) + snapshot_buf[pixel_ix + 2];

        // go to the next pixel
        out_ptr_ix++;
        pixel_ix+=3;
        pixels_left--;
    }

    // and done!
    return 0;
}