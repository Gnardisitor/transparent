# transparent

Local, GPU-accelerated image editing. Background removal, denoising, upscaling, and bokeh blur, all in one app. Linux first, with a working native Windows build. No cloud calls, no subscriptions, no telemetry.

## Capabilities

Everything below works end to end and is covered by CI.

- Vulkan GPU acceleration with CPU fallback
- Background removal via BiRefNet-lite
- Denoising via SCUNet
- 4x upscaling via Real-ESRGAN
- Bokeh blur with an adjustable strength slider
- A zoomable preview with a before/after comparison
- Model management in the settings tab, including user-imported models from disk
- Simple mode for one operation at a time
- Advanced mode for running several in any order
- Single images, batch folders, and animated GIF in and out

Inference runs via [vision.cpp](https://github.com/Acly/vision.cpp), built from [this fork](https://forge.db-serve.com/dbajan/vision.cpp) which adds the SCUNet architecture. GIF encoding uses [giflib](http://giflib.sourceforge.net/) plus a small built-in color quantizer.

## Building

Prerequisites:

- Git
- CMake 3.28+
- Ninja
- C++20 compiler
- Qt6 base

vcpkg is vendored as a git submodule and supplies the other necessary libraries.

### Linux

1. Install the system dependencies:

Debian/Ubuntu (Debian 13 and Ubuntu 24.04 or newer):

```bash
sudo apt install build-essential cmake ninja-build git qt6-base-dev shaderc
```

Fedora:

```bash
sudo dnf install gcc-c++ cmake ninja-build git qt6-qtbase-devel glslc
```

Arch:

```bash
sudo pacman -S --needed base-devel cmake ninja git qt6-base shaderc
```

2. Clone the repository:

```bash
git clone https://forge.db-serve.com/dbajan/transparent.git
cd transparent
git submodule update --init
```

3. Configure and build:

```bash
cmake --preset default
cmake --build build
```

4. Run the tests:

```bash
ctest --test-dir build
```

5. Run the app:

```bash
./build/transparent
```

6. Package into an AppImage:

```bash
./packaging/build-appimage.sh
```

### Windows

The native MSVC route works and is exercised with every change.

1. Install system dependencies

```powershell
winget install --id Microsoft.VisualStudio.2022.BuildTools --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.Windows11SDK.26100"
winget install -e --id Git.Git
winget install -e --id Kitware.CMake
winget install -e --id Ninja-build.Ninja
winget install -e --id KhronosGroup.VulkanSDK
uvx --from aqtinstall aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O C:\Qt
```

2. Enable long paths on Git. The vcpkg and vision.cpp source trees exceed the default `MAX_PATH` limit.

```powershell
git config --global core.longpaths true
sudo reg add "HKLM\SYSTEM\CurrentControlSet\Control\FileSystem" /v LongPathsEnabled /t REG_DWORD /d 1
```

3. Clone the repository:

```powershell
git clone https://forge.db-serve.com/dbajan/transparent.git C:\Documents\Dev\transparent
cd C:\Documents\Dev\transparent
git submodule update --init
```

4. Build the app using `cl.exe`.

```powershell
set PATH=C:\Qt\6.8.3\msvc2022_64\bin;%PATH%
cmake --preset windows
cmake --build build
```

5. Run the tests:

```powershell
ctest --test-dir build
```

6. Run the app:

```powershell
.\build\transparent.exe
```

7. Make a portable version:

```powershell
mkdir portable\bin
copy build\transparent.exe portable\bin\
C:\Qt\6.8.3\msvc2022_64\bin\windeployqt.exe --release --compiler-runtime portable\bin\transparent.exe
copy build\vulkan-1.dll portable\bin\
xcopy /E /I build\models portable\share\transparent\models\
```

## Models

Three defaults are downloaded and checksum-verified at configure time, and copied into the app's data directory on first launch, so a fresh install works offline:

| Category | Model | License | Source |
|---|---|---|---|
| Background removal | [BiRefNet-lite](https://github.com/zhengpeng7/birefnet) | MIT | [Acly/BiRefNet-GGUF](https://huggingface.co/Acly/BiRefNet-GGUF) |
| Denoise | [SCUNet](https://github.com/cszn/SCUNet) color real GAN | Apache-2.0 | [transparent-models](https://forge.db-serve.com/dbajan/transparent-models) |
| Upscale | Real-ESRGAN `foolhardy_Remacri` | BSD-3-Clause | [Acly/Real-ESRGAN-GGUF](https://huggingface.co/Acly/Real-ESRGAN-GGUF) |

More models are available to available to install in the settings tab. Custom converted models can be added using the `Add model from disk` button or from being added into the models folder. Only `birefnet`, `scunet`, and `esrgan` as currently supported.

To convert your own checkpoints for a supported architecture, use [scripts/convert.py](https://forge.db-serve.com/dbajan/vision.cpp/blob/main/scripts/convert.py) in the vision.cpp fork. It supports the same architectures and produces F16 GGUF files, for example:

```bash
uv run python scripts/convert.py esrgan 4x-UltraSharp.pth -q f16 -o models/
```

## License

This project's own code is [GPLv3](LICENSE). Bundled third-party components (Qt, vision.cpp/ggml, BiRefNet-lite, SCUNet, Real-ESRGAN, giflib) keep their own licenses.
