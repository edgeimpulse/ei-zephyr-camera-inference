# Edge Impulse Camera Inference on Zephyr

Run image-based Edge Impulse models (classification, object detection, visual anomaly
detection) on any Zephyr board with a camera, using the
**[Edge Impulse Zephyr Module](https://docs.edgeimpulse.com/hardware/deployments/run-zephyr-module)**.
Drop in your model > build > flash > get real-time inference on the camera stream.

This is the image counterpart of
[ei-zephyr-imu-inference](https://github.com/edgeimpulse/ei-zephyr-imu-inference).

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

The impulse must be an **image** impulse. The build stops with a clear error if
`model-parameters/model_metadata.h` says otherwise.

## Supported targets

The application never names a board. It uses the devicetree `zephyr,camera` chosen
node and the Zephyr video API, so anything with a camera driver works — boards with
a camera on-module, or any board plus a camera shield.

Boards with an on-module camera:

| Board | `-b` argument | Camera |
| --- | --- | --- |
| Arduino Nicla Vision | `arduino_nicla_vision/stm32h747xx/m7` | GC2145 |
| ESP32-S3-EYE | `esp32s3_eye/esp32s3/procpu` | OV2640 |
| Seeed XIAO ESP32S3 Sense | `xiao_esp32s3/esp32s3/procpu/sense` | OV2640 |

Any board plus a camera shield works too, for example:

```bash
west build -p -b nucleo_h743zi --shield st_stm32f4dis_cam .
```

Run `ls zephyr/boards/shields | grep -i cam` for the full list of camera shields, and
`ls zephyr/drivers/video` for the supported sensors.

Board-specific settings live in `boards/<board>.conf` (and `.overlay`), never in the
sources. Adding a board is usually just a `.conf` with the right video buffer pool
size — see below.

## Build

```bash
west build -p -b arduino_nicla_vision/stm32h747xx/m7 .
```

ESP32-S3 targets need the MCUboot bootloader, so build them with sysbuild:

```bash
west build --sysbuild -p -b esp32s3_eye/esp32s3/procpu .
```

## Flash

```bash
west flash
```

## How it works

```
camera (video API) -> RGB888 -> crop + rescale -> signal_t -> run_classifier -> display_results
```

- `src/camera/ei_camera.cpp` — the board-agnostic part. Picks a pixel format the app
  can convert (RGB565, RGB565X, RGB24, YUYV or GREY) and the smallest advertised
  resolution that still covers the model input, converts each frame to packed RGB888,
  then crops to the model aspect ratio and scales down with the SDK's
  `crop_and_interpolate_rgb888()`. Pixels reach the impulse through a `signal_t`
  callback as `0x00RRGGBB` floats, which is what the Edge Impulse image DSP block
  expects — grayscale models are handled inside the block, so this code always feeds RGB.
- `src/inference/inferencing.cpp` — the capture/classify state machine.
- `src/main.cpp` — init and go.

## Configuration

| Kconfig | Default | Purpose |
| --- | --- | --- |
| `CONFIG_EI_CAMERA_CAPTURE_WIDTH` / `_HEIGHT` | 0 | Capture resolution. 0 picks the smallest one the sensor offers that covers the model input. |
| `CONFIG_EI_CAMERA_NUM_BUFS` | 2 | Video buffers. Must be >= the driver's `min_vbuf_count`. |
| `CONFIG_EI_CAMERA_CAPTURE_TIMEOUT_MS` | 2000 | How long to wait for a frame. -1 waits forever. |
| `CONFIG_EI_CAMERA_INFERENCE_DELAY_MS` | 0 | Throttle between inferences. |
| `CONFIG_EI_CAMERA_HFLIP` / `_VFLIP` | n | Applied by the sensor when it supports the control. |

### Memory

Two separate pools have to be sized for the capture resolution:

- `CONFIG_VIDEO_BUFFER_POOL_HEAP_SIZE` holds the raw frames:
  `CONFIG_EI_CAMERA_NUM_BUFS x width x height x bytes_per_pixel`.
- `CONFIG_HEAP_MEM_POOL_SIZE` holds the RGB888 working frame (`width x height x 3`)
  plus whatever the impulse arena needs.

Both are model and resolution dependent. If the app prints
`out of video buffers` or `out of memory for the ... RGB888 frame`, raise the
matching one in `boards/<board>.conf`.
