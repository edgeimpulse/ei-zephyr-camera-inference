/*
 * OV2640 driver with I2C initialization timing fix for ESP32-S3
 * 
 * This is a patched version of the Zephyr OV2640 driver that adds a small delay
 * after checking if I2C is ready, to allow the ESP32 I2C peripheral to fully stabilize
 * before attempting camera communication.
 * 
 * Original driver: zephyr/drivers/video/ov2640.c
 * Issue: ESP32 I2C needs extra time after device_is_ready() returns true
 * Fix: Add 100ms delay before I2C configuration
 */

#include <zephyr/kernel.h>

/* Include the original driver and override the init function */
#define ov2640_init_0 ov2640_init_0_original
#include "../../../../zephyr/drivers/video/ov2640.c"
#undef ov2640_init_0

/* Patched initialization function with I2C stabilization delay */
static int ov2640_init_0(const struct device *dev)
{
	const struct ov2640_config *cfg = dev->config;

	if (!device_is_ready(cfg->i2c.bus)) {
		LOG_ERR("Bus device is not ready");
		return -ENODEV;
	}

	/* ESP32-S3 I2C timing fix: Wait for I2C to fully stabilize */
	k_msleep(100);

#if DT_INST_NODE_HAS_PROP(0, reset_gpios)
	if (!gpio_is_ready_dt(&cfg->reset_gpio)) {
		LOG_ERR("%s: device %s is not ready", dev->name,
				cfg->reset_gpio.port->name);
		return -ENODEV;
	}
#endif

	uint32_t i2c_cfg = I2C_MODE_CONTROLLER |
					I2C_SPEED_SET(I2C_SPEED_STANDARD);

	if (i2c_configure(cfg->i2c.bus, i2c_cfg)) {
		LOG_ERR("Failed to configure ov2640 i2c interface.");
	}

	return ov2640_init(dev);
}
