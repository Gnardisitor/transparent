# transparent

Local, GPU-accelerated background removal for Linux. No cloud calls, no subscriptions, no telemetry.

## Status

Linux MVP in progress. Background removal, 4x upscaling, and a mask-only bokeh blur all work end to end. Drag an image in for one processed PNG out. Drag a folder in and pick an output folder to batch every supported image in it. Drag an animated GIF in to run every frame through the pipeline and export a new GIF. Simple mode runs one operation at a time. Advanced mode runs several in any order. Inference runs via [vision.cpp](https://github.com/Acly/vision.cpp) (BiRefNet-lite and Real-ESRGAN) with Vulkan GPU acceleration and a CPU fallback. Bokeh reuses BiRefNet-lite's own mask, no separate model. GIF decoding uses Qt's own plugin. Encoding uses [giflib](http://giflib.sourceforge.net/) (MIT) plus a small built-in color quantizer. See [PLAN.md](PLAN.md) for full scope, architecture decisions, and the roadmap (true video support, full depth-graduated blur, colorization, Windows/macOS ports).

## Why

Existing local background-removal tools are bad on Linux. They have broken or absent GPU acceleration, they fit the desktop poorly, and they nag for subscriptions. This started as a local-first, offline alternative built to work well on Linux first. It is growing into a broader toolkit for image edits that usually cost a subscription or are done poorly by existing local tools. See [PLAN.md](PLAN.md) for the rest of the reasoning.

## Building

Prerequisites (all platforms):

- CMake 3.28+, Ninja
- A C++20 compiler
- Qt6 base package (Widgets, Network, Concurrent, and Test all ship in qtbase)
- git (this repo uses a submodule)

vcpkg is vendored as a git submodule and supplies the Vulkan headers/loader and giflib. CMake's `FetchContent` fetches [vision.cpp](https://github.com/Acly/vision.cpp) and its ggml backend from source, pinned to a tagged release. One `default` preset covers every platform. The first configure takes a while. It bootstraps vcpkg, compiles vision.cpp/ggml including Vulkan shader compilation, and downloads checksum-verified model weights. Later builds are incremental.

### Linux

Install the system dependencies. The vcpkg submodule supplies the Vulkan headers/loader and giflib, but ggml's Vulkan shaders need a system `glslc` (the `shaderc`/`glslc` package below).

Debian/Ubuntu:

```bash
sudo apt install build-essential cmake ninja-build git qt6-base-dev shaderc
```

CMake must be 3.28+. Debian 13 and Ubuntu 24.04 or newer qualify. On older releases install a newer CMake (`pip install cmake` or the Kitware apt repo).

Fedora:

```bash
sudo dnf install gcc-c++ cmake ninja-build git qt6-qtbase-devel glslc
```

Arch:

```bash
sudo pacman -S --needed base-devel cmake ninja git qt6-base shaderc
```

Clone and fetch the vendored vcpkg submodule:

```bash
git clone <this-repo-url>
cd transparent
git submodule update --init
```

Configure and build:

```bash
cmake --preset default
cmake --build build
```

Run the tests:

```bash
ctest --test-dir build
```

Run the app:

```bash
./build/src/transparent
```

### Windows (experimental)

Windows is not a supported, CI-tested target yet. PLAN.md plans a MinGW-w64 cross-build from the Linux CI runner, and that work has not started. The native MSVC route below follows the same CMake setup as Linux but has not been verified end to end, so expect a snag or two.

1. **Visual Studio 2022** with the *Desktop development with C++* workload. CMake and Ninja ship with VS.

2. **Git for Windows**, with long paths enabled. The vcpkg and vision.cpp source trees exceed the default `MAX_PATH` limit.

   ```bat
   git config --global core.longpaths true
   reg add "HKLM\SYSTEM\CurrentControlSet\Control\FileSystem" /v LongPathsEnabled /t REG_DWORD /d 1
   ```

   Clone somewhere short, e.g. `C:\Dev\transparent`:

   ```bat
   git clone <this-repo-url> C:\Dev\transparent
   cd C:\Dev\transparent
   git submodule update --init
   ```

3. **Qt 6.8+ for MSVC 2022 64-bit**, from the Qt online installer (pick the `MSVC 2022 64-bit` component, not the MinGW one) or with aqtinstall:

   ```bat
   pip install aqtinstall
   aqt install-qt windows desktop 6.8 win64_msvc2022_64
   ```

4. **Vulkan SDK** from [LunarG](https://vulkan.lunarg.com/sdk/home). Its `glslc` compiles ggml's Vulkan shaders. vcpkg already supplies the loader and headers.

5. Open an **"x64 Native Tools Command Prompt for VS 2022"** so `cl.exe` and Ninja are found. Put Qt's bin directory on `PATH` and build:

   ```bat
   set PATH=C:\Qt\6.8.3\msvc2022_64\bin;%PATH%
   cmake --preset default
   cmake --build build
   ```

   Adjust the Qt path to the version you installed.

6. Run the tests, then the app:

   ```bat
   ctest --test-dir build
   build\src\transparent.exe
   ```

   The exe needs the Qt DLLs on `PATH` to start, which step 5's `set PATH` provides. For a self-contained folder:

   ```bat
   mkdir portable\bin
   copy build\src\transparent.exe portable\bin\
   C:\Qt\6.8.3\msvc2022_64\bin\windeployqt.exe --release --compiler-runtime portable\bin\transparent.exe
   xcopy /E /I build\models portable\share\transparent\models\
   ```

   The app finds models in `..\share\transparent\models` relative to the exe. There is no installer yet (see PLAN.md).

## Packaging

Build a portable AppImage. The script needs a Qt6 `qmake`/`qmake6` on `PATH` and downloads linuxdeploy plus its Qt plugin into `packaging/tools/` on first run:

```bash
packaging/build-appimage.sh
```

The AppImage is written to `build/transparent-x86_64.AppImage`.

## Models

Background removal uses [BiRefNet-lite](https://github.com/zhengpeng7/birefnet) (MIT), converted to GGUF by [Acly](https://huggingface.co/Acly/BiRefNet-GGUF) for vision.cpp. Upscaling uses the `foolhardy_Remacri` [Real-ESRGAN](https://github.com/xinntao/Real-ESRGAN) checkpoint (BSD-3-Clause), converted to GGUF by [Acly](https://huggingface.co/Acly/Real-ESRGAN-GGUF) too. Weights are not committed to this repo. [models/CMakeLists.txt](models/CMakeLists.txt) downloads and checksum-verifies them at configure time.

## License

This project's own code is [GPLv3](LICENSE). See [PLAN.md](PLAN.md#project-intent) for why, over LGPLv3 or GPLv2. Bundled third-party components (Qt, vision.cpp/ggml, BiRefNet-lite, Real-ESRGAN, giflib) keep their own licenses. See PLAN.md for the full list.
