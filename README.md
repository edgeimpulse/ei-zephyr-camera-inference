# Edge Impulse Camera Inference on Zephyr

This repository demonstrates how to run image-based Edge Impulse models on Zephyr using the **Edge Impulse Zephyr Module**.  
Drop in your model > build > flash > get real-time camera inference.

## Initialize This Repo

```bash
west init -m https://github.com/edgeimpulse/ei-zephyr-camera-inference.git
cd ei-zephyr-camera-inference
west update
```

This fetches:
- Zephyr RTOS
- Edge Impulse Zephyr SDK module
- All module dependencies

## Update the Model

In Edge Impulse Studio go to:  
**Deployment** > **Zephyr library** > **Build**

Download the generated `.zip`

Extract into the `model/` folder:

```bash
unzip -o ~/Downloads/your-model.zip -d model/
```

Your `model/` directory should contain:
- `CMakeLists.txt`
- `edge-impulse-sdk/`
- `model-parameters/`
- `tflite-model/`

## Supported boards

The project has been tested with the following boards:
- [Espressif ESP32-S3-EYE](https://docs.zephyrproject.org/latest/boards/espressif/esp32s3_eye/doc/index.html)
- [Seeed Studio XIAO ESP32S3 Sense](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/) with OV2640 camera
- Arduino Nicla Vision (STM32H747)
- EK-RA8D1
- FRDM-MCXN236
- FRDM-MCXN947
- i.MX RT1060/1064/1170 EVK
- STM32H7B3I-DK
- STM32MP135F-DK
- STM32N6570-DK

## Build

### ESP32-S3-EYE (Recommended)

The ESP32-S3-EYE is pre-configured with **PSRAM support** for camera operations:

```bash
west build --pristine -b esp32s3_eye/esp32s3/procpu
```

**PSRAM Configuration (already configured):**
- External SPIRAM enabled (Octal mode)
- Video buffer allocated in PSRAM via `.ext_ram.bss` section
- 230KB video buffer pool
- DMA optimized for camera transfers

The board configuration (`boards/esp32s3_eye_procpu.conf`) automatically:
- Enables SPIRAM: `CONFIG_ESP_SPIRAM=y`
- Sets octal mode: `CONFIG_SPIRAM_MODE_OCT=y`
- Allocates video buffers to external RAM: `CONFIG_VIDEO_BUFFER_POOL_ZEPHYR_REGION_NAME=".ext_ram.bss"`
- Configures 240x240 RGB pixel format

**To move the heap to PSRAM** (if you need more main RAM for inference):
Add to `boards/esp32s3_eye_procpu.conf`:
```
CONFIG_ESP32_WIFI_NET_ALLOC_SPIRAM=y
CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY=y
```

### Seeed Studio XIAO ESP32S3 Sense

```bash
west build --pristine -b xiao_esp32s3/esp32s3/procpu/sense
```

> **Note for XIAO ESP32S3 Sense:** The camera resolution is set to 160x120 in the board config file ([boards/xiao_esp32s3_procpu_sense.conf](boards/xiao_esp32s3_procpu_sense.conf)) and will be automatically downsampled to match your model's input size. Adjust `CONFIG_VIDEO_FRAME_WIDTH` and `CONFIG_VIDEO_FRAME_HEIGHT` if needed, but ensure they match one of the OV2640's supported resolutions.

### Other Boards

For other supported boards:

```bash
# Arduino Nicla Vision
west build --pristine -b arduino_nicla_vision/stm32h747xx/m7

# FRDM-MCXN236
west build --pristine -b frdm_mcxn236

# i.MX RT1064 EVK
west build --pristine -b mimxrt1064_evk
```

### Arduino Nicla Vision

The Arduino Nicla Vision uses the **GC2145** camera sensor with the STM32H747 dual-core processor (only M7 core is used for camera/inference).

**Build:**
```bash
west build --pristine -b arduino_nicla_vision/stm32h747xx/m7
```

**Flash:**
The board uses DFU (Device Firmware Update) mode for flashing:
1. Double-tap the reset button to enter DFU mode (green LED will light up)
2. Flash with: `west flash` or manually:
   ```bash
   dfu-util -d ,2341:035f -a 0 -D build/zephyr/zephyr.bin -s 0x08040000
   ```
3. Quick tap reset to run the application

**LED Feedback (when serial console unavailable):**
- **Green flash at startup** = Application started
- **Brief green flash every few seconds** = Heartbeat (inference loop running)
- **Red LED blinks 3x** = "lamp" object detected (>40% confidence)
- **Green LED blinks 3x** = "coffee" object detected (>40% confidence)
- Adjust detection threshold in [src/inference/inferencing.cpp](src/inference/inferencing.cpp#L146)

**Memory Configuration:**
- Video buffer: 80KB heap
- Main heap: 81920 bytes
- RAM usage: ~42% (219KB/512KB)
- Flash usage: ~29% (231KB/768KB)

> **Note:** USB CDC serial console is configured but may not output reliably. Use LED feedback for visual confirmation that FOMO inference is working.
west build --pristine -b mimxrt1064_evk
```

## Flash

### ESP32-S3-EYE

```bash
west flash
```

The ESP32-S3-EYE uses the built-in USB-JTAG interface. Make sure:
- Connect USB-C cable to the board
- Hold BOOT button while connecting if flash fails
- Use `--esp-device /dev/tty.usbmodem*` if auto-detection fails

### Seeed Studio XIAO ESP32S3 Sense

```bash
west flash
```

The XIAO ESP32S3 Sense uses USB-C for power and flashing. If flashing fails, the board will auto-reset after flash completes.

### Other Boards

Specify the appropriate runner:

```bash
# J-Link
west flash --runner jlink

# nRF Command Line Tools
west flash --runner nrfjprog

# OpenOCD
west flash --runner openocd

# PyOCD
west flash --runner pyocd
```

## Monitor Output

```bash
# ESP32-S3-EYE or XIAO ESP32S3 Sense (USB CDC)
west espressif monitor

# Or use screen
screen /dev/tty.usbmodem* 115200

# Other boards (adjust port)
screen /dev/tty.usbserial* 115200
```



## Resources
- [Edge Impulse SDK for Zephyr](https://github.com/edgeimpulse/edge-impulse-sdk-zephyr)


Clear BSD License - see `LICENSE` file  
Copyright (c) 2025 EdgeImpulse Inc.
