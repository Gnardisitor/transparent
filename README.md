# transparent

Local, GPU-accelerated background removal. Linux-first, with a working native Windows build. No cloud calls, no subscriptions, no telemetry.

## Status

Linux MVP in progress. Background removal, 4x upscaling, and a mask-only bokeh blur all work end to end. Drag an image in for one processed PNG out. Drag a folder in and pick an output folder to batch every supported image in it. Drag an animated GIF in to run every frame through the pipeline and export a new GIF. Simple mode runs one operation at a time. Advanced mode runs several in any order. Inference runs via [vision.cpp](https://github.com/Acly/vision.cpp) (BiRefNet-lite and Real-ESRGAN) with Vulkan GPU acceleration and a CPU fallback. Bokeh reuses BiRefNet-lite's own mask, no separate model. GIF decoding uses Qt's own plugin. Encoding uses [giflib](http://giflib.sourceforge.net/) (MIT) plus a small built-in color quantizer. See [PLAN.md](PLAN.md) for full scope, architecture decisions, and the roadmap (true video support, full depth-graduated blur, colorization, CI and packaging for Windows, macOS port).

## Why

Existing local background-removal tools are bad on Linux. They have broken or absent GPU acceleration, they fit the desktop poorly, and they nag for subscriptions. This started as a local-first, offline alternative built to work well on Linux first. It is growing into a broader toolkit for image edits that usually cost a subscription or are done poorly by existing local tools. See [PLAN.md](PLAN.md) for the rest of the reasoning.

## Building

Prerequisites (all platforms):

- CMake 3.28+, Ninja
- A C++20 compiler
- Qt6 base package (Widgets, Network, Concurrent, and Test all ship in qtbase)
- git (this repo uses a submodule)

vcpkg is vendored as a git submodule and supplies the Vulkan headers/loader and giflib. CMake's `FetchContent` fetches [vision.cpp](https://github.com/Acly/vision.cpp) and its ggml backend from source, pinned to a tagged release. The `default` preset covers Linux and macOS; a separate `windows` preset picks the right vcpkg triplet. The first configure takes a while. It bootstraps vcpkg, compiles vision.cpp/ggml including Vulkan shader compilation, and downloads checksum-verified model weights. Later builds are incremental.

Everything is linked statically except Qt and the Vulkan loader. vision.cpp's headers mark its API with `__declspec(dllimport)` on MSVC unless `VISP_STATIC_DEFINE` is defined, which upstream does not support, so on Windows a small local patch ([cmake/patch-visioncpp-static.cmake](cmake/patch-visioncpp-static.cmake), wired in via `FetchContent`'s `PATCH_COMMAND`) adds that convention, and a top-level `add_compile_definitions(VISP_STATIC_DEFINE)` applies it to both sides of the link. Without it, linking fails with `LNK2019` on `__imp_...` symbols. The `windows` preset also uses vcpkg's `x64-windows-static-md` triplet, which builds giflib (and everything else vcpkg provides) as static libraries while keeping the dynamic C runtime that the prebuilt Qt binaries require; the plain `x64-windows-static` triplet would force `/MT` and clash with Qt's `/MD`.

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
./build/transparent
```

### Windows

The native MSVC route works and is exercised with every change. PLAN.md still plans a MinGW-w64 cross-build from the Linux CI runner for packaging; that work has not started.

1. **Visual Studio 2022** with the *Desktop development with C++* workload. CMake and Ninja ship with VS.

2. **Git for Windows**, with long paths enabled. The vcpkg and vision.cpp source trees exceed the default `MAX_PATH` limit.

   ```bat
   git config --global core.longpaths true
   reg add "HKLM\SYSTEM\CurrentControlSet\Control\FileSystem" /v LongPathsEnabled /t REG_DWORD /d 1
   ```

   Clone somewhere short, e.g. `C:\Dev\transparent`:

   ```bat
   git clone https://forge.db-serve.com/dbajan/transparent.git C:\Dev\transparent
   cd C:\Dev\transparent
   git submodule update --init
   ```

3. **Qt 6.8+ for MSVC 2022 64-bit**, from the Qt online installer (pick the `MSVC 2022 64-bit` component, not the MinGW one) or with aqtinstall:

   ```bat
   uvx --from aqtinstall aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O C:\Qt
   ```

4. **Vulkan SDK** from [LunarG](https://vulkan.lunarg.com/sdk/home). Its `glslc` compiles ggml's Vulkan shaders. vcpkg already supplies the loader and headers.

5. Open an **"x64 Native Tools Command Prompt for VS 2022"** so `cl.exe` and Ninja are found. Put Qt's bin directory on `PATH` and build:

   ```bat
   set PATH=C:\Qt\6.8.3\msvc2022_64\bin;%PATH%
   cmake --preset windows
   cmake --build build
   ```

   Adjust the Qt path to the version you installed. The `windows` preset sets the `x64-windows-static-md` vcpkg triplet, so giflib is linked statically and only the Vulkan loader ends up as a runtime DLL. vcpkg copies `vulkan-1.dll` next to the executables automatically.

6. Run the tests, then the app:

   ```bat
   ctest --test-dir build
   build\transparent.exe
   ```

   The exe needs the Qt DLLs on `PATH` to start, which step 5's `set PATH` provides. For a self-contained folder:

   ```bat
   mkdir portable\bin
   copy build\transparent.exe portable\bin\
   C:\Qt\6.8.3\msvc2022_64\bin\windeployqt.exe --release --compiler-runtime portable\bin\transparent.exe
   copy build\vulkan-1.dll portable\bin\
   xcopy /E /I build\models portable\share\transparent\models\
   ```

   `windeployqt` pulls in the Qt DLLs, the MSVC runtime, and the platform and image-format plugins; the explicit copy adds the Vulkan loader. giflib is static, so there is no `gif.dll` to ship. The app finds models in `..\share\transparent\models` relative to the exe. There is no installer yet (see PLAN.md).

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
