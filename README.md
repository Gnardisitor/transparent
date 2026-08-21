# transparent

Local, GPU-accelerated background removal for Linux. No cloud calls, no subscriptions, no telemetry.

## Status

Linux MVP in progress. Background removal, 4x upscaling, and a mask-only bokeh/depth-of-field blur all work end to end, single image or a whole folder: drag an image in for one processed PNG out, or drag a folder in and pick an output folder to batch every supported image inside it. Simple mode runs one operation at a time (pick it from the dropdown next to the mode selector); Advanced mode lets you pick several, in whatever order you want, and run them together. Background removal and upscaling run via [vision.cpp](https://github.com/Acly/vision.cpp) (BiRefNet-lite and Real-ESRGAN respectively), with Vulkan GPU acceleration and a CPU fallback if no Vulkan device is available; bokeh reuses BiRefNet-lite's own mask to blur the background, no separate model. See [PLAN.md](PLAN.md) for full scope, architecture decisions, and the roadmap (full depth-graduated blur, colorization, Windows/macOS ports).

## Why

Existing local background-removal tools are bad on Linux: broken or absent GPU acceleration, poor fit with the desktop they run on, and constant subscription nagging. This started as a local-first, offline alternative built to work well on Linux first, and is growing into a broader toolkit for image edits that usually cost a subscription or are done poorly by existing local tools. See [PLAN.md](PLAN.md) for the rest of the reasoning.

## Building

Prerequisites:

- CMake 3.28+, Ninja
- A C++20 compiler
- Qt6 (Widgets, Test components)
- git (this repo uses a submodule)

vcpkg is vendored as a git submodule (used to pull Vulkan headers/loader); [vision.cpp](https://github.com/Acly/vision.cpp) and its ggml backend are fetched and built from source via CMake `FetchContent`, pinned to a tagged release.

Clone with submodules:

```bash
git clone --recurse-submodules <this-repo-url>
```

Already cloned without `--recurse-submodules`? Run:

```bash
git submodule update --init
```

Configure and build:

```bash
cmake --preset default
cmake --build build
```

The first configure/build takes a while: it bootstraps vcpkg, fetches and compiles vision.cpp/ggml from source (including Vulkan shader compilation), and downloads the model weights. Later builds are incremental.

Run the tests:

```bash
ctest --test-dir build
```

Run the app:

```bash
./build/src/transparent
```

## Models

Background removal uses [BiRefNet-lite](https://github.com/zhengpeng7/birefnet) (MIT), converted to GGUF by [Acly](https://huggingface.co/Acly/BiRefNet-GGUF) for vision.cpp. Upscaling uses the `foolhardy_Remacri` [Real-ESRGAN](https://github.com/xinntao/Real-ESRGAN) checkpoint (BSD-3-Clause), also converted to GGUF by [Acly](https://huggingface.co/Acly/Real-ESRGAN-GGUF). Weights aren't committed to this repo. [models/CMakeLists.txt](models/CMakeLists.txt) downloads and checksum-verifies them at configure time, the same way vision.cpp fetches its own default models.

## License

This project's own code is [GPLv3](LICENSE). See [PLAN.md](PLAN.md#project-intent) for why, over LGPLv3 or GPLv2. Bundled third-party components (Qt, vision.cpp/ggml, BiRefNet-lite, Real-ESRGAN) keep their own licenses; see PLAN.md for the full list.
