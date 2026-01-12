/*
 * Camera hardware diagnostics for OV2640
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <stdio.h>

#define OV2640_I2C_ADDR 0x30

/* OV2640 Register addresses */
#define OV2640_REG_BANK_SEL     0xFF
#define OV2640_REG_MIDH         0x1C  /* Manufacturer ID high byte */
#define OV2640_REG_MIDL         0x1D  /* Manufacturer ID low byte */
#define OV2640_REG_PID          0x0A  /* Product ID high byte */
#define OV2640_REG_VER          0x0B  /* Product ID low byte */
#define OV2640_REG_RESET        0x04  /* DSP reset register */
#define OV2640_REG_CTRL0        0xC2  /* Control 0 */

#define OV2640_BANK_SENSOR      0x01
#define OV2640_BANK_DSP         0x00

static const struct device *i2c_dev = NULL;

static int ov2640_read_reg(uint8_t reg, uint8_t *val)
{
    if (!i2c_dev) {
        return -ENODEV;
    }
    
    return i2c_write_read(i2c_dev, OV2640_I2C_ADDR, &reg, 1, val, 1);
}

static int ov2640_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    
    if (!i2c_dev) {
        return -ENODEV;
    }
    
    return i2c_write(i2c_dev, buf, 2, OV2640_I2C_ADDR);
}

void camera_hardware_diagnostics(void)
{
#if DT_NODE_EXISTS(DT_NODELABEL(i2c1))
    int ret;
    uint8_t val;
    uint16_t manufacturer_id, product_id;
    
    printf("\n=== Camera Hardware Diagnostics ===\n");
    
    /* Get I2C device */
    i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c1));
    if (!device_is_ready(i2c_dev)) {
        printf("ERROR: I2C device not ready\n");
        return;
    }
    printf("✓ I2C device ready\n");
    
    /* Test I2C bus scan */
    printf("\nScanning I2C bus...\n");
    int devices_found = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        struct i2c_msg msgs[1];
        uint8_t dummy = 0;
        
        msgs[0].buf = &dummy;
        msgs[0].len = 0;
        msgs[0].flags = I2C_MSG_WRITE | I2C_MSG_STOP;
        
        ret = i2c_transfer(i2c_dev, &msgs[0], 1, addr);
        if (ret == 0) {
            printf("  Found device at 0x%02X", addr);
            if (addr == 0x30 || addr == 0x21) {
                printf(" (possible OV2640)");
            }
            printf("\n");
            devices_found++;
        }
    }
    if (devices_found == 0) {
        printf("  ✗ NO DEVICES FOUND - Camera not connected or not powered!\n");
    }
    
    /* Read OV2640 identification registers */
    printf("\nReading OV2640 ID registers...\n");
    
    /* Switch to sensor bank */
    ret = ov2640_write_reg(OV2640_REG_BANK_SEL, OV2640_BANK_SENSOR);
    if (ret < 0) {
        printf("ERROR: Failed to switch to sensor bank (ret=%d)\n", ret);
        return;
    }
    k_msleep(10);
    
    /* Read Manufacturer ID */
    ret = ov2640_read_reg(OV2640_REG_MIDH, &val);
    if (ret < 0) {
        printf("ERROR: Failed to read MIDH (ret=%d)\n", ret);
        return;
    }
    manufacturer_id = val << 8;
    
    ret = ov2640_read_reg(OV2640_REG_MIDL, &val);
    if (ret < 0) {
        printf("ERROR: Failed to read MIDL (ret=%d)\n", ret);
        return;
    }
    manufacturer_id |= val;
    
    printf("  Manufacturer ID: 0x%04X (expected: 0x7FA2)\n", manufacturer_id);
    
    /* Read Product ID */
    ret = ov2640_read_reg(OV2640_REG_PID, &val);
    if (ret < 0) {
        printf("ERROR: Failed to read PID (ret=%d)\n", ret);
        return;
    }
    product_id = val << 8;
    
    ret = ov2640_read_reg(OV2640_REG_VER, &val);
    if (ret < 0) {
        printf("ERROR: Failed to read VER (ret=%d)\n", ret);
        return;
    }
    product_id |= val;
    
    printf("  Product ID: 0x%04X (expected: 0x2642)\n", product_id);
    
    /* Check DSP bank registers */
    printf("\nChecking DSP bank...\n");
    ret = ov2640_write_reg(OV2640_REG_BANK_SEL, OV2640_BANK_DSP);
    if (ret < 0) {
        printf("ERROR: Failed to switch to DSP bank (ret=%d)\n", ret);
        return;
    }
    k_msleep(10);
    
    ret = ov2640_read_reg(OV2640_REG_RESET, &val);
    if (ret < 0) {
        printf("ERROR: Failed to read RESET register (ret=%d)\n", ret);
    } else {
        printf("  RESET register: 0x%02X\n", val);
        printf("    DVP enable: %s\n", (val & 0x04) ? "DISABLED" : "ENABLED");
    }
    
    ret = ov2640_read_reg(OV2640_REG_CTRL0, &val);
    if (ret < 0) {
        printf("ERROR: Failed to read CTRL0 register (ret=%d)\n", ret);
    } else {
        printf("  CTRL0 register: 0x%02X\n", val);
    }
    
    /* Verify camera is actually connected and working */
    if (manufacturer_id == 0x7FA2 && product_id == 0x2642) {
        printf("\n✓ Camera hardware verified - OV2640 detected\n");
    } else if (manufacturer_id == 0xFFFF || manufacturer_id == 0x0000) {
        printf("\n✗ Camera NOT detected - I2C bus issue or camera not connected\n");
    } else {
        printf("\n✗ Wrong camera detected - check hardware\n");
    }
    
    printf("=== End Diagnostics ===\n\n");
#else
    printf("\n=== Camera Hardware Diagnostics ===\n");
    printf("I2C diagnostics not available for this board\n");
    printf("=== End Diagnostics ===\n\n");
#endif
}

/**
 * Structure for OV2640 register configuration
 */
struct ov2640_reg {
    uint8_t addr;
    uint8_t val;
};

/**
 * Default register configuration from Zephyr ov2640.c driver
 * These registers configure the camera for proper video output
 */
static const struct ov2640_reg default_regs[] = {
    { 0xFF, 0x00 },  /* BANK_SEL_DSP */
    { 0x2c, 0xff },
    { 0x2e, 0xdf },
    { 0xFF, 0x01 },  /* BANK_SEL_SENSOR */
    { 0x3c, 0x32 },
    { 0x11, 0x80 },  /* CLKRC - Set PCLK divider */
    { 0x09, 0x02 },  /* COM2 - Output drive x2 */
    { 0x04, 0x28 },  /* REG04 - HREF enable */
    { 0x13, 0xe0 },  /* COM8 - AGC/AEC enable */
    { 0x14, 0x48 },  /* COM9 - AGC gain 8x */
    { 0x15, 0x00 },  /* COM10 */
    { 0x2c, 0x0c },
    { 0x33, 0x78 },
    { 0x3a, 0x33 },
    { 0x3b, 0xfb },
    { 0x3e, 0x00 },
    { 0x43, 0x11 },
    { 0x16, 0x10 },
    { 0x39, 0x02 },
    { 0x35, 0x88 },
    { 0x22, 0x0a },
    { 0x37, 0x40 },
    { 0x23, 0x00 },
    { 0x34, 0xa0 },  /* ARCOM2 */
    { 0x06, 0x02 },
    { 0x06, 0x88 },
    { 0x07, 0xc0 },
    { 0x0d, 0xb7 },
    { 0x0e, 0x01 },
    { 0x4c, 0x00 },
    { 0x4a, 0x81 },
    { 0x21, 0x99 },
    { 0x24, 0x40 },  /* AEW */
    { 0x25, 0x38 },  /* AEB */
    { 0x26, 0x82 },  /* VV - AGC/AEC fast mode */
    { 0x48, 0x00 },  /* COM19 */
    { 0x49, 0x00 },  /* ZOOMS */
    { 0x5c, 0x00 },
    { 0x63, 0x00 },
    { 0x46, 0x00 },  /* FLL */
    { 0x47, 0x00 },  /* FLH */
    { 0x0c, 0x3c },  /* COM3 - banding filter */
    { 0x5d, 0x55 },  /* REG5D */
    { 0x5e, 0x7d },  /* REG5E */
    { 0x5f, 0x7d },  /* REG5F */
    { 0x60, 0x55 },  /* REG60 */
    { 0x61, 0x70 },  /* HISTO_LOW */
    { 0x62, 0x80 },  /* HISTO_HIGH */
    { 0x7c, 0x05 },
    { 0x20, 0x80 },
    { 0x28, 0x30 },
    { 0x6c, 0x00 },
    { 0x6d, 0x80 },
    { 0x6e, 0x00 },
    { 0x70, 0x02 },
    { 0x71, 0x94 },
    { 0x73, 0xc1 },
    { 0x3d, 0x34 },
    { 0x5a, 0x57 },
    { 0x4f, 0xbb },  /* BD50 */
    { 0x50, 0x9c },  /* BD60 */
    { 0xFF, 0x00 },  /* BANK_SEL_DSP */
    { 0xe5, 0x7f },
    { 0xf9, 0xc0 },  /* MC_BIST */
    { 0x41, 0x24 },
    { 0xe0, 0x14 },  /* RESET */
    { 0x76, 0xff },
    { 0x33, 0xa0 },
    { 0x42, 0x20 },
    { 0x43, 0x18 },
    { 0x4c, 0x00 },
    { 0x87, 0xd0 },  /* CTRL3 */
    { 0x88, 0x3f },
    { 0xd7, 0x03 },
    { 0xd9, 0x10 },
    { 0xd3, 0x82 },  /* R_DVP_SP */
    { 0xc8, 0x08 },
    { 0xc9, 0x80 },
    /* Color matrix and gamma correction */
    { 0x7c, 0x00 },  /* BPADDR */
    { 0x7d, 0x00 },  /* BPDATA */
    { 0x7c, 0x03 },
    { 0x7d, 0x48 },
    { 0x7d, 0x48 },
    { 0x7c, 0x08 },
    { 0x7d, 0x20 },
    { 0x7d, 0x10 },
    { 0x7d, 0x0e },
    /* Gamma curve */
    { 0x90, 0x00 },
    { 0x91, 0x0e },
    { 0x91, 0x1a },
    { 0x91, 0x31 },
    { 0x91, 0x5a },
    { 0x91, 0x69 },
    { 0x91, 0x75 },
    { 0x91, 0x7e },
    { 0x91, 0x88 },
    { 0x91, 0x8f },
    { 0x91, 0x96 },
    { 0x91, 0xa3 },
    { 0x91, 0xaf },
    { 0x91, 0xc4 },
    { 0x91, 0xd7 },
    { 0x91, 0xe8 },
    { 0x91, 0x20 },
    /* UV adjustment */
    { 0x92, 0x00 },
    { 0x93, 0x06 },
    { 0x93, 0xe3 },
    { 0x93, 0x03 },
    { 0x93, 0x03 },
    { 0x93, 0x00 },
    { 0x93, 0x02 },
    { 0x93, 0x00 },
    { 0x93, 0x00 },
    { 0x93, 0x00 },
    { 0x93, 0x00 },
    { 0x93, 0x00 },
    { 0x93, 0x00 },
    { 0x93, 0x00 },
    /* Lens correction */
    { 0x96, 0x00 },
    { 0x97, 0x08 },
    { 0x97, 0x19 },
    { 0x97, 0x02 },
    { 0x97, 0x0c },
    { 0x97, 0x24 },
    { 0x97, 0x30 },
    { 0x97, 0x28 },
    { 0x97, 0x26 },
    { 0x97, 0x02 },
    { 0x97, 0x98 },
    { 0x97, 0x80 },
    { 0x97, 0x00 },
    { 0x97, 0x00 },
    /* Color matrix coefficients */
    { 0xa4, 0x00 },
    { 0xa8, 0x00 },
    { 0xc5, 0x11 },
    { 0xc6, 0x51 },
    { 0xbf, 0x80 },
    { 0xc7, 0x10 },
    { 0xb6, 0x66 },
    { 0xb8, 0xA5 },
    { 0xb7, 0x64 },
    { 0xb9, 0x7C },
    { 0xb3, 0xaf },
    { 0xb4, 0x97 },
    { 0xb5, 0xFF },
    { 0xb0, 0xC5 },
    { 0xb1, 0x94 },
    { 0xb2, 0x0f },
    { 0xc4, 0x5c },
    /* AWB control */
    { 0xa6, 0x00 },
    { 0xa7, 0x20 },
    { 0xa7, 0xd8 },
    { 0xa7, 0x1b },
    { 0xa7, 0x31 },
    { 0xa7, 0x00 },
    { 0xa7, 0x18 },
    { 0xa7, 0x20 },
    { 0xa7, 0xd8 },
    { 0xa7, 0x19 },
    { 0xa7, 0x31 },
    { 0xa7, 0x00 },
    { 0xa7, 0x18 },
    { 0xa7, 0x20 },
    { 0xa7, 0xd8 },
    { 0xa7, 0x19 },
    { 0xa7, 0x31 },
    { 0xa7, 0x00 },
    { 0xa7, 0x18 },
    { 0x7f, 0x00 },
    { 0xe5, 0x1f },
    { 0xe1, 0x77 },
    { 0xdd, 0x7f },
    { 0xc2, 0x0e },  /* CTRL0 - YUV422, YUV_EN, RGB_EN */
    { 0x00, 0x00 }   /* End marker */
};

/**
 * Full OV2640 initialization sequence
 * This writes all default registers needed for video output
 * Called if boot-time init failed due to I2C timing issues
 */
int camera_reinit_sensor(void)
{
#if DT_NODE_EXISTS(DT_NODELABEL(i2c1))
    int ret;
    int reg_count = 0;
    int fail_count = 0;
    
    if (!i2c_dev) {
        i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c1));
        if (!device_is_ready(i2c_dev)) {
            printf("ERROR: I2C device not ready for reinit\n");
            return -ENODEV;
        }
    }
    
    printf("\n=== Full OV2640 Initialization ===\n");
    printf("Writing all configuration registers...\n");
    
    /* Write all default registers */
    for (int i = 0; i < sizeof(default_regs) / sizeof(default_regs[0]); i++) {
        if (default_regs[i].addr == 0x00 && default_regs[i].val == 0x00) {
            break;  /* End marker */
        }
        
        ret = ov2640_write_reg(default_regs[i].addr, default_regs[i].val);
        if (ret < 0) {
            fail_count++;
            if (fail_count < 5) {  /* Only print first few errors */
                printf("  Warning: Failed to write reg 0x%02X = 0x%02X (ret=%d)\n",
                       default_regs[i].addr, default_regs[i].val, ret);
            }
        }
        reg_count++;
        
        /* Small delay after bank switches */
        if (default_regs[i].addr == 0xFF) {
            k_usleep(100);
        }
    }
    
    printf("Wrote %d registers (%d failures)\n", reg_count, fail_count);
    
    if (fail_count > 0) {
        printf("Warning: Some register writes failed, camera may not work\n");
    } else {
        printf("✓ All registers configured successfully\n");
    }
    
    printf("=== End Full Init ===\n\n");
    
    return (fail_count > reg_count / 2) ? -EIO : 0;  /* Fail if >50% errors */
#else
    return -ENOTSUP;
#endif
}
