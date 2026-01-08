# Edge Impulse Camera Inference on Zephyr

This repository demonstrates how to run image-based Edge Impulse models on Zephyr using the **Edge Impulse Zephyr Module**.  
Drop in your model > build > flash > get real-time camera inference.

## Initialize This Repo

```bash
west init https://github.com/edgeimpulse/ei-zephyr-imu-inference.git
cd ei-zephyr-imu-inference
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

## Build

Choose your board:

For example: Espressif ESP32-S3-EYE
```bash
west build --pristine -b esp32s3_eye/esp32s3/procpu
```

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
