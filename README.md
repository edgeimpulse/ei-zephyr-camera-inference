# Edge Impulse Camera Inference on Zephyr

This repository demonstrates how to run image-based Edge Impulse models on Zephyr using the **Edge Impulse Zephyr Module**.  
Drop in your model > build > flash > get real-time camera inference.

## Initialize This Repo

```bash
west init https://github.com/edgeimpulse/ei-zephyr-camera-inference.git
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
- <img width="792" height="200" alt="image" src="https://github.com/user-attachments/assets/c4c31e24-9e9a-4798-aa46-abc73b99f5ef" />


## Build

Choose your board:

**Espressif ESP32-S3-EYE:**
```bash
west build --pristine -b esp32s3_eye/esp32s3/procpu
```

**Seeed Studio XIAO ESP32S3 Sense:**
```bash
west build --pristine -b xiao_esp32s3/esp32s3/procpu/sense
```

> **Note for XIAO ESP32S3 Sense:** The camera resolution is set to 160x120 in the board config file ([boards/xiao_esp32s3_procpu_sense.conf](boards/xiao_esp32s3_procpu_sense.conf)) and will be automatically downsampled to match your model's input size. Adjust `CONFIG_VIDEO_FRAME_WIDTH` and `CONFIG_VIDEO_FRAME_HEIGHT` if needed, but ensure they match one of the OV2640's supported resolutions.

## Flash

```bash
west flash
```

Or specify runner:

```bash
west flash --runner jlink
west flash --runner nrfjprog
west flash --runner openocd
```



## Resources
- [Edge Impulse SDK for Zephyr](https://github.com/edgeimpulse/edge-impulse-sdk-zephyr)


Clear BSD License - see `LICENSE` file  
Copyright (c) 2025 EdgeImpulse Inc.
