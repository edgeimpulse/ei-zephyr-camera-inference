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
#include <zephyr/drivers/gpio.h>
#include <string.h>

#if (defined(EI_CLASSIFIER_SENSOR) && (EI_CLASSIFIER_SENSOR != EI_CLASSIFIER_SENSOR_CAMERA))
#error "This inference project is only for camera sensor"
#endif

// LED definitions - conditional based on board availability
#define LED0_NODE DT_ALIAS(led0)  // Red LED (required on all boards)

#if DT_NODE_EXISTS(DT_ALIAS(led1))
#define LED1_NODE DT_ALIAS(led1)  // Green LED (optional)
#define HAS_GREEN_LED 1
#else
#define HAS_GREEN_LED 0
#endif

static const struct gpio_dt_spec red_led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
#if HAS_GREEN_LED
static const struct gpio_dt_spec green_led = GPIO_DT_SPEC_GET(LED1_NODE, gpios);
#endif

static uint8_t snapshot_buf[EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT * 3] __attribute__((aligned(32)));
// __attribute__((aligned(32), section(".ext_ram.bss")));

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
    ei_printf("Starting impulse...\n");
    ei_start_impulse();
    ei_printf("Impulse started, entering state machine loop\n");
    
    state = INFERENCE_STATE_SAMPLING;

    while(INFERENCE_STATE_STOP != state) {
        switch(state){
            case INFERENCE_STATE_SAMPLING:
                ei_printf("Taking photo...\n");
                // Blink red once before capture
                gpio_pin_set_dt(&red_led, 1);
                k_sleep(K_MSEC(300));
                gpio_pin_set_dt(&red_led, 0);
                
                // capture image from camera
                if (ei_camera_capture(snapshot_buf, EI_CLASSIFIER_INPUT_WIDTH, EI_CLASSIFIER_INPUT_HEIGHT, &out_size) != 0) {
                    ei_printf("ERR: Failed to capture image from camera\n");
                    state = INFERENCE_STATE_STOP;
                    break;
                }
                
                // Blink red twice after capture succeeds
                for (int i = 0; i < 2; i++) {
                    gpio_pin_set_dt(&red_led, 1);
                    k_sleep(K_MSEC(150));
                    gpio_pin_set_dt(&red_led, 0);
                    k_sleep(K_MSEC(150));
                }
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
                // Quick heartbeat to show it's still looping
#if HAS_GREEN_LED
                gpio_pin_set_dt(&green_led, 1);
                k_sleep(K_MSEC(50));
                gpio_pin_set_dt(&green_led, 0);
#else
                gpio_pin_set_dt(&red_led, 1);
                k_sleep(K_MSEC(50));
                gpio_pin_set_dt(&red_led, 0);
#endif
                
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
    ei_printf("Running inference...\n");
    EI_IMPULSE_ERROR res = run_classifier(&features_signal, &result, false);

    if (res != 0) {
        ei_printf("ERR: Failed to run classifier\n");
        ei_printf("ERR: %d\n", res);
        ret = false;
    }
    else {
        display_results(&ei_default_impulse, &result);
        
        // LED feedback for FOMO detections
        // Turn off LEDs first
        gpio_pin_set_dt(&red_led, 0);
#if HAS_GREEN_LED
        gpio_pin_set_dt(&green_led, 0);
#endif
        
        // Check bounding boxes for detections with confidence > 0.6 (60% threshold)
        ei_printf("Inference complete! Found %d detections\n", result.bounding_boxes_count);
        bool high_confidence_found = false;
        for (size_t ix = 0; ix < result.bounding_boxes_count; ix++) {
            ei_impulse_result_bounding_box_t bb = result.bounding_boxes[ix];
            ei_printf("  Detection %d: %s (%.0f%% confidence)\n", ix + 1, bb.label, bb.value * 100.0f);
            if (bb.value > 0.6f) {
                ei_printf("  *** HIGH CONFIDENCE DETECTION: %s (%.0f%%) ***\n", bb.label, bb.value * 100.0f);
                high_confidence_found = true;
                
                // lamp = red LED, coffee = green LED (or red if no green)
                if (strcmp(bb.label, "lamp") == 0) {
                    // lamp - blink red 3 times
                    for (int i = 0; i < 3; i++) {
                        gpio_pin_set_dt(&red_led, 1);
                        k_sleep(K_MSEC(150));
                        gpio_pin_set_dt(&red_led, 0);
                        k_sleep(K_MSEC(150));
                    }
                }
#if HAS_GREEN_LED
                else if (strcmp(bb.label, "coffee") == 0) {
                    // coffee - blink green 3 times
                    for (int i = 0; i < 3; i++) {
                        gpio_pin_set_dt(&green_led, 1);
                        k_sleep(K_MSEC(150));
                        gpio_pin_set_dt(&green_led, 0);
                        k_sleep(K_MSEC(150));
                    }
                }
#else
                else if (strcmp(bb.label, "coffee") == 0) {
                    // coffee - blink red 2 times (different pattern for single LED)
                    for (int i = 0; i < 2; i++) {
                        gpio_pin_set_dt(&red_led, 1);
                        k_sleep(K_MSEC(100));
                        gpio_pin_set_dt(&red_led, 0);
                        k_sleep(K_MSEC(100));
                    }
                }
#endif
                break; // Only respond to first high-confidence detection
            }
        }
        
        if (!high_confidence_found && result.bounding_boxes_count > 0) {
            ei_printf("No detections above 60%% threshold\n");
        }
        else if (result.bounding_boxes_count == 0) {
            ei_printf("No objects detected in frame\n");
        }
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

    // Initialize LEDs
#if HAS_GREEN_LED
    if (!gpio_is_ready_dt(&red_led) || !gpio_is_ready_dt(&green_led)) {
        ei_printf("ERR: LEDs not ready\n");
    }
    gpio_pin_configure_dt(&red_led, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&green_led, GPIO_OUTPUT_INACTIVE);
    
    // Flash both LEDs to indicate startup
    gpio_pin_set_dt(&red_led, 1);
    gpio_pin_set_dt(&green_led, 1);
    k_sleep(K_MSEC(500));
    gpio_pin_set_dt(&red_led, 0);
    gpio_pin_set_dt(&green_led, 0);
#else
    if (!gpio_is_ready_dt(&red_led)) {
        ei_printf("ERR: LED not ready\n");
    }
    gpio_pin_configure_dt(&red_led, GPIO_OUTPUT_INACTIVE);
    
    // Flash LED to indicate startup
    gpio_pin_set_dt(&red_led, 1);
    k_sleep(K_MSEC(500));
    gpio_pin_set_dt(&red_led, 0);
#endif

    ei_printf("Inferencing settings:\n");
    ei_printf("\tClassifier interval: %.2f ms.\n", (float)EI_CLASSIFIER_INTERVAL_MS);
    ei_printf("\tInput frame size: %d\n", EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE);
    ei_printf("\tNumber of output classes: %d\n", sizeof(ei_classifier_inferencing_categories) / sizeof(ei_classifier_inferencing_categories[0]));

    // let's start, we will continuously run inference
    ei_printf("Calling run_classifier_init...\n");
    // Blink LED 6 times before classifier init
#if HAS_GREEN_LED
    for (int i = 0; i < 6; i++) {
        gpio_pin_set_dt(&green_led, 1);
        k_sleep(K_MSEC(100));
        gpio_pin_set_dt(&green_led, 0);
        k_sleep(K_MSEC(100));
    }
#else
    for (int i = 0; i < 6; i++) {
        gpio_pin_set_dt(&red_led, 1);
        k_sleep(K_MSEC(100));
        gpio_pin_set_dt(&red_led, 0);
        k_sleep(K_MSEC(100));
    }
#endif
    run_classifier_init();
    ei_printf("run_classifier_init done\n");

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
    ei_printf(" ---- ei_camera_get_data offset %d length %d\n", offset, length);
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