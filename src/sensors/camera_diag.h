/*
 * Camera hardware diagnostics header
 */

#ifndef CAMERA_DIAG_H
#define CAMERA_DIAG_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Run camera hardware diagnostics
 * 
 * Tests I2C communication, reads camera ID registers,
 * and verifies camera is properly connected and responding.
 */
void camera_hardware_diagnostics(void);

/**
 * @brief Reinitialize the OV2640 sensor
 * 
 * Performs sensor initialization sequence directly via I2C.
 * Call this after a delay if boot-time initialization failed.
 * 
 * @return 0 on success, negative error code on failure
 */
int camera_reinit_sensor(void);

#ifdef __cplusplus
}
#endif

#endif /* CAMERA_DIAG_H */
