# ei-zephyr-camera-inference

Capture frames from a camera with Zephyr's video subsystem and run an Edge
Impulse impulse on them.
Supported targets:

| Board | Camera | Capture format |
| --- | --- | --- |
| `esp32s3_eye/esp32s3/procpu` | OV2640 over LCD_CAM DVP | RGB565 176x144 (tested on hardware) |
| `arduino_nicla_vision/stm32h747xx/m7` | GC2145 over DCMI | RGB565 320x240 (tested on hardware) |

## Code structure

```
main.cpp            ei_camera_init()  -> configure the chosen camera, alloc buffers
  |
  +-> inferencing.cpp  ei_inference_sm()
        SAMPLING     -> ei_camera_capture()  dequeue a frame, crop + rescale it
        DATA_READY   -> ready to classify
        RUNNING      -> run_classifier() over ei_camera_get_data(), print results
```

A camera hands you a whole frame at once, so one capture fills the classifier
input instead of accumulating samples over
time into a circular buffer.

### The camera layer

`src/camera/ei_camera.cpp` uses the standard Zephyr video API
(`video_set_compose_format` / `video_enqueue` / `video_dequeue`), same as
`zephyr/samples/subsys/video/capture`. The one thing it does differently is the
conversion: instead of building a full resolution RGB888 copy of the frame and
then calling the SDK's `crop_and_interpolate_rgb888()`, it centre-crops,
bilinearly rescales and converts RGB565 to RGB888 in a single pass.

`ei_camera_get_data()` then hands the classifier one pixel per feature, packed
as `0x00RRGGBB` in a float, which is what Edge Impulse image models expect.

Supported capture formats are `RGB565`, `RGB565X` and `RGB24`; anything else is
rejected at init with a clear message.

### Using a different model

Export your impulse from Studio as a **Zephyr module** and either drop it in
`model/` inside this directory or point the build at it:

```
west build -b <board> ei-zephyr-camera-inference -- -DEI_MODEL_DIR=/path/to/export
```

Then update the `edge-impulse-sdk-zephyr` revision in `west.yml` to the version
in the new export's `check_version.cpp` and re-run `west update`.

## Configuration

All under `menuconfig` -> *Edge Impulse camera configuration*:

| Option | Default | Meaning |
| --- | --- | --- |
| `EI_CAMERA_WIDTH` / `EI_CAMERA_HEIGHT` | 160x120 | Sensor capture resolution, must be one the driver advertises |
| `EI_CAMERA_NUM_BUFS` | 2 | Video buffers in the pool |
| `EI_CAMERA_HFLIP` / `EI_CAMERA_VFLIP` | n | Mirror the frame |
| `EI_INFERENCE_INTERVAL_MS` | 200 | Sleep between inferences, 0 to free-run |
| `EI_INFERENCE_DEBUG` | n | Pass `debug=true` to `run_classifier()` |
| `EI_CAMERA_RGB565_BYTE_SWAP` | n | Camera delivers RGB565 high byte first |
| `EI_HEAP_SHARED_MULTI_HEAP` | n | Allocate EI memory from the shared multi heap |

Board overrides live in `boards/<board>.conf`.
