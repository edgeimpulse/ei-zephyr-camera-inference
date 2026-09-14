# Place your Edge Impulse model here

This folder is a Zephyr module holding the exported impulse. It is not committed —
regenerate it from Edge Impulse Studio:

**Deployment** > **Zephyr library** > **Build**, then

```bash
unzip -o ~/Downloads/your-model.zip -d model/
```

which gives you:

```
model/
  CMakeLists.txt
  edge-impulse-sdk/
  model-parameters/
  tflite-model/
  zephyr/module.yml
```

The impulse must be an **image** impulse, otherwise the build fails with
"The model in model/ is not an image model".

> [!NOTE]
> If the build fails on `zephyr/module.yml`, check its `name:` field — Studio
> sometimes emits a project name containing a space, which Zephyr's module
> handling rejects. Replace the space with an underscore.
