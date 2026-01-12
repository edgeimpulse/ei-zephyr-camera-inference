
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

#include <zephyr/kernel.h>
#include "sensors/ei_camera.h"
#include "sensors/camera_diag.h"
#include "inference/inferencing.h"
#include "edge-impulse-sdk/porting/ei_classifier_porting.h"
#include <stdio.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

// LED for debugging
#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec debug_led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

void blink_pattern(int count) {
    for (int i = 0; i < count; i++) {
        gpio_pin_set_dt(&debug_led, 1);
        k_sleep(K_MSEC(200));
        gpio_pin_set_dt(&debug_led, 0);
        k_sleep(K_MSEC(200));
    }
    k_sleep(K_MSEC(500));
}

int main(void)
{
    gpio_pin_configure_dt(&debug_led, GPIO_OUTPUT_INACTIVE);
    
    // 1 blink = main started
    blink_pattern(1);
    
    // This is needed so that output of printf is output immediately without buffering
    setvbuf(stdout, NULL, _IONBF, 0);
    
    LOG_INF("=== Edge Impulse Camera Inference ===");
    printf("\n\n=== Edge Impulse Camera Inference ===\n");
    
    // 2 blinks = after printf
    blink_pattern(2);
    
    // Wait for USB CDC to be ready
    k_sleep(K_MSEC(2000));
    
    // Give camera extra time to power up before diagnostics
    printf("Waiting for camera power stabilization (3 seconds)...\n");
    k_sleep(K_MSEC(3000));
    
    // Run hardware diagnostics first
    camera_hardware_diagnostics();
    
    // Reinitialize the sensor (boot-time init failed due to I2C timing)
    int ret = camera_reinit_sensor();
    if (ret != 0) {
        printf("WARNING: Sensor reinit failed (ret=%d), continuing anyway...\n", ret);
    }
    
    // Extra delay after reinit to ensure sensor is ready
    printf("Waiting for sensor to stabilize after reinit (2 seconds)...\n");
    k_sleep(K_MSEC(2000));
    
    printf("Starting camera initialization...\n");

    // 3 blinks = before camera init
    blink_pattern(3);
    
    ei_camera_init(160, 120);

    // 4 blinks = camera initialized
    blink_pattern(4);
    
    printf("Starting inference state machine...\n");
    ei_printf("Starting inference state machine...\n");
    
    // 5 blinks = before inference start
    blink_pattern(5);
    
    ei_inference_sm(); // run state machine

    return 0;
}
