# transparent: local, GPU-accelerated background removal for Linux

## Motivation

Existing local background-removal tools are bad: broken or missing GPU acceleration on Linux, a poor fit for the OS, and constant subscription nagging. Web tools are worse. This started as a local-first, offline alternative built to work well on Linux first. It's grown into a broader image-editing toolkit (background removal, upscaling, bokeh, and more), all on the same pipeline-step architecture and the same no-cloud, no-subscription, no-telemetry rules.

## Project intent

- Personal tool first, with intent to open-source once it works.
- Free. No subscription, no cloud calls, no telemetry.
- License: your code under GPLv3 (see [LICENSE](LICENSE)).
  - Originally planned as LGPLv3, to match Qt's own license. LGPL exists so other people's proprietary software can link against your code without inheriting your license, that's why Qt itself is LGPL. `transparent` is an application, not a library other codebases link into, so that reason doesn't apply here. GPLv3 does what was actually wanted: anyone distributing a modified version has to share the source back under the same terms. GPLv3 over GPLv2 for the explicit patent grant; nothing here needs GPLv2 compatibility.
  - Compatible with dynamically-linked LGPLv3 Qt and MIT-licensed vision.cpp/BiRefNet-lite: GPL projects commonly dynamically link LGPL libraries.
  - Bundled components keep their own licenses (Qt: LGPLv3, dynamically linked; vision.cpp/ggml: MIT; BiRefNet-lite: MIT), listed in-repo and in the About screen.

## Platform rollout (sequential, not parallel)

1. Linux: build this first, completely, before touching anything else.
2. Windows: after Linux fully works and is validated on real hardware (AMD RX 9070 XT, RDNA4).
3. macOS: last priority, dependent on getting help from a Mac-owning friend to test.

## Architecture decisions

### Inference engine: vision.cpp + Vulkan backend

vision.cpp (Acly, ggml-based, MIT) runs everything through a single Vulkan backend: vendor-neutral GPU acceleration across NVIDIA/AMD/Intel with no per-vendor branching, the same approach Upscayl and chaiNNer use. ggml's CPU backend is the automatic fallback when no Vulkan device is available.

ncnn was the original choice for the same vendor-neutral reasoning, but its conversion tooling (`pnnx`, `onnx2ncnn`) can't translate the transformer and deformable-conv ops BiRefNet-lite needs. vision.cpp implements both natively, so BiRefNet-lite runs without conversion workarounds. Full history of the ncnn attempt is in git log, not repeated here. ONNX Runtime was also considered and rejected: no mature Vulkan execution provider.

**No image tiling for segmentation.** BiRefNet-lite's GGUF always downsamples input to 1024x1024 before inference, then upsamples the mask back, so VRAM cost is constant regardless of input size (vision.cpp instead auto-downscales against a 4GB single-tensor cap for very large images). Tiling wouldn't help quality either: segmentation needs whole-image context to identify the subject.

**Higher-resolution background removal, if ever needed**: `BiRefNet-dynamic` scales its internal resolution with input size instead of the fixed 1024x1024 above. Its large-image allocator bug ([krita-vision-tools#54](https://github.com/Acly/krita-vision-tools/issues/54)) was fixed upstream before this repo's vendored vision.cpp commit. See "Model management" below for the planned model-swap feature this unlocks.

**Depth-of-field / bokeh.** First cut: mask-only, blurring everything outside `BackgroundRemovalStep`'s mask with a feathered edge, no separate depth model. True depth-graduated blur is possible via vision.cpp's Depth-Anything V2, but only the Small checkpoint (Apache-2.0, 50.6MB) is license-clean (Base/Large are CC-BY-NC-4.0, same restriction that ruled out RMBG/MODNet), and it needs real implementation work: edge haloing, fine detail reading as a blob, and deriving the focus plane from Depth-Anything's relative (not metric) depth. Deferred until mask-only proves visibly insufficient.

**Bokeh strength control. Done.** A 0-100% slider in the Settings dialog, mapped to a blur radius scaled to the image's own shorter side (5% of it at 100%) rather than a fixed pixel count, so the effect looks consistent across resolutions. Adjusting it live-previews by reusing the mask `BokehStep` cached from the last real `process()` call and re-blending off the GUI thread via `QtConcurrent`; the blur itself is cheap CPU work, not model inference, so this stays fast without rerunning BiRefNet-lite per tick. `BokehStep::reblendCached(strengthPercent)` takes the in-flight slider value as an explicit argument rather than reading its own stored `strengthPercent_` back on the worker thread, so a fast drag can't race a GUI-thread write against a worker-thread read of the same member. Live preview only actually updates the screen when Bokeh is the last step in whatever's currently active (`MainWindow::isBokehTheActiveOutputStep()`) — otherwise the slider still updates and persists, it just doesn't have a valid single-step output to show until the next real run. Bokeh always uses whichever segmentation model Background Removal is set to, no independent model choice of its own; swapping that model invalidates the cache.

### Video/GIF scope: GIF only for now, no FFmpeg

Animated GIF and real video (mp4 etc.) are different asks. GIF uses Qt's own decoder plus a small new encoder dependency; video would need FFmpeg, a much bigger commitment (a new build dependency, LGPL/GPL licensing review, patent-encumbered codecs, a larger AppImage). Worth its own writeup when video is actually prioritized, not a rider on the GIF work.

GIF encoding uses giflib (vcpkg, MIT) rather than FFmpeg or a vendored header: Qt's bundled GIF plugin turned out to be read-only (confirmed via `QImageWriter::supportedImageFormats()`), and giflib was already available the same way vulkan/vulkan-headers are pulled in (`vcpkg.json`). giflib only reads/writes the container, so `GifIO` (`src/core/GifIO.h`) also owns a small median-cut color quantizer for building the output palette.

Inherent GIF limitations, not bugs: a 256-color-max palette (shared across all frames, rebuilt via median-cut) and on/off-only transparency (`BackgroundRemovalStep`'s soft mask edge gets thresholded at 128).

### Background removal model: BiRefNet-lite

- MIT-licensed, strong quality for salient object segmentation/matting.
- Other models considered:
  - RMBG-1.4/2.0 (BRIA): rejected, CC BY-NC 4.0, non-commercial only.
  - MODNet: rejected, CC BY-NC-SA 4.0, non-commercial and portrait-specific.
  - IS-Net/U2Net: a viable fallback (Apache-2.0, smaller/faster) but visibly lower quality.
  - BEN2-base: MIT, a close alternative worth a look if BiRefNet-lite underperforms in practice.

### Model management. Done: selection screen, on-demand downloads, AppData, live swap

Supersedes the old one-line "model swappability" deferred item. Both `SegmentationModel` (BiRefNet-lite / BiRefNet-dynamic / BiRefNet full) and `UpscaleModel` (foolhardy_Remacri / NMKD-Superscale-SP) become swappable, using license-clean checkpoints already published at the same huggingface.co/Acly account this project already downloads from at build time.

Adjustable upscale *amount* (as opposed to model choice) was considered and rejected: every compatible Real-ESRGAN checkpoint there is fixed at 4x (the only different-shaped file, `RealESRGAN-x4plus_anime-6B`, is a "plus" variant `esrgan_load_model` doesn't support), so there's nothing to expose without resampling the model's own output.

Design:

- Curated registry, not free-form file browsing, for now. The selection screen knows a fixed list per category (name, filename, license, size, download URL, SHA256), scans the models directory, and marks each entry Installed/Not Installed by filename match. Browsing for an arbitrary `.gguf` is deferred, not dropped: vision.cpp's loaders are architecture-specific and would just fail on an incompatible file, and the curated list already covers what's worth offering.
- On-demand download, not bundle-everything-at-build-time. Each entry gets its own download button, runs off the GUI thread, and is verified against its known SHA256 before being marked Installed (same discipline `models/CMakeLists.txt` already applies). A mismatch deletes the file and shows an error. Manually placing a file in the models directory works the same way, since Installed-detection is just filename matching.
- Models directory moves to `QStandardPaths::AppDataLocation`, off the build tree. `TRANSPARENT_MODELS_DIR` is currently `${CMAKE_BINARY_DIR}/models`, wiped by a clean rebuild and not a stable place to point users at for manual placement. (`main.cpp` already has an unused `#include <QStandardPaths>`, likely an earlier, unfinished step toward this.)
- First run still needs no network access. The two current defaults keep being fetched at CMake configure time as today; on first launch the app copies them into the AppData directory if missing, a local file copy, not a download.
- Selecting a model applies live, no restart. `BackgroundRemovalStep`, `BokehStep`, and `UpscaleStep` currently take their model as an immutable `shared_ptr` fixed at construction, this needs a way to swap it post-construction. Loading a newly selected model runs asynchronously with a spinner, the same `QtConcurrent`/`QFutureWatcher` pattern `MainWindow` already uses for processing.
- Lives in a Settings dialog, opened from a menu-bar action next to Help rather than a third item in the Simple/Advanced mode selector (`SettingsPage`, hosted in a `QDialog` `MainWindow` owns) — both model pickers and the bokeh-strength slider live there. Model choice and bokeh strength are set-occasionally-then-forget preferences, not per-run choices; nesting them inside Advanced mode would lock Simple-mode users out of picking BiRefNet-dynamic for a large photo, and a menu-bar action keeps the dialog reachable regardless of which mode is active without spending a mode-selector slot on it or sharing the main window's image-drop status label.
- Persisted via `QSettings`. Nothing in the app persists any state today; every launch resets to hardcoded defaults.

### Language & UI: C++ + Qt Widgets

- C++ links directly against vision.cpp's native API: no FFI layer, smallest binary, fastest startup, no bundled runtime.
- Qt Widgets, not QML/Qt Quick: simpler and lighter, sufficient for a deliberately minimal interface, and re-skins reasonably per-OS.

### Internal architecture: pipeline-step abstraction from day one

Even though the MVP only did one operation (background removal), it's built as a swappable pipeline step (image in, image out) rather than a hardcoded path. This is what made batch processing, upscaling, and GIF support additive later instead of a rewrite.

One level down, the same idea again: `SegmentationModel`. `BackgroundRemovalStep` never talks to ncnn or vision.cpp directly, only to a `SegmentationModel` interface (`isReady()` / `computeMask()`). `NcnnSegmentationModel` was the first adapter, `VisionCppSegmentationModel` replaced it, and the switch touched zero lines of `BackgroundRemovalStep`. `UpscaleModel` mirrors the same seam for Real-ESRGAN.

### Windows port: cross-compilation via MinGW-w64 from the existing Linux CI runner

Researched and decided ahead of actually starting the Windows port (still gated behind Linux distro packages, see the deferred list below); full citations and the superseded native-runner analysis are in [docs/research/windows-port-and-ci.md](docs/research/windows-port-and-ci.md), not repeated here.

**Decision: cross-compile Windows binaries from the same Linux box that already hosts Forgejo and its Actions runner, using mingw-w64, rather than standing up a native Windows CI runner.** There's no second machine to dedicate as a Windows runner, and the alternative — Forgejo's official `act_runner` is Linux-only, with Windows support existing only as an unofficial, alpha-quality community build ("should not be considered secure enough to deploy in production") — is worse than cross-compiling from infrastructure that already exists and is already trusted. This was the opposite of the first conclusion this research reached; revisited once the actual hardware constraint (no spare Windows machine) was clear, and the individual cross-compile risks turned out to be smaller than first assessed (see below).

**Toolchain**:

- **mingw-w64** (`gcc-mingw-w64-x86-64`/`g++-mingw-w64-x86-64`), targeting `x86_64-w64-mingw32`, for the app's own code and for cross-compiling vision.cpp/ggml.
- **vcpkg's community `x64-mingw-dynamic` triplet**, chainloaded through a custom CMake toolchain file (`VCPKG_CHAINLOAD_TOOLCHAIN_FILE`), for the small vcpkg deps this repo already has: `giflib`, `vulkan`/`vulkan-headers`. Not covered by vcpkg's own CI, but these are small, portable C libraries with a long independent history of building under MinGW — low realistic risk despite the formal disclaimer.
- **Qt6, via `aqtinstall` rather than building from source.** `aqtinstall` (`pip install aqtinstall`) fetches Qt's own official prebuilt Windows MinGW 64-bit binaries directly — the same archives the Qt Online Installer uses — regardless of what OS is doing the downloading. This sidesteps the single biggest from-source cross-compile risk entirely: no need to build all of Qt6 under an untested vcpkg triplet.
- **Vulkan shader compilation (`glslc`) is not actually cross-compile-sensitive.** It compiles GLSL to target-agnostic SPIR-V bytecode as a build-time host step, and ggml's own CMake already has first-party support for this exact situation: `ExternalProject_Add` builds its `vulkan-shaders-gen` tool for the host specifically when `CMAKE_CROSSCOMPILING` is set. A native Linux Vulkan SDK/`glslc` install on the build image covers this.

**Genuinely open unknowns, to resolve with a manual local spike before wiring up any CI**: whether vision.cpp's own `CMakeLists.txt` (not just bare ggml) cooperates with `CMAKE_CROSSCOMPILING` out of the box; whether vcpkg's mingw triplet builds `giflib`/the Vulkan loader without incident; and how to package the result, since `windeployqt.exe` is a Windows PE tool and won't run natively on the Linux build host (run it under Wine, or hardcode this app's small, fixed Qt DLL list — `Qt6Core`/`Gui`/`Widgets`/`Network`, `platforms/qwindows.dll`, plus the mingw runtime DLLs — as a CMake install step). None of these are exotic; expect a day or two of focused work with one or two fixable snags, not an open-ended research effort.

**Steps, in order**:

1. Do the cross-compile by hand on the homelab box first, outside any CI workflow, to actually resolve the unknowns above.
2. Once that spike succeeds, capture the working toolchain in a Dockerfile (below) and build a purpose-built CI image, rather than reinstalling the toolchain from scratch on every job run.
3. Push the image to Forgejo's own built-in container registry (no extra infra needed — it's already part of this project's Forgejo instance): `forgejo.yourdomain/you/ci-windows-cross:v1`.
4. Add a `.forgejo/workflows/windows-cross-build.yaml` job with `runs-on: docker` and `container: image: forgejo.yourdomain/you/ci-windows-cross:v1`, wired up with the persistent caching below.
5. Only then consider a Windows installer; PLAN.md's stated goal is a portable single `.exe` (windeployqt/manual-DLL-list output, zipped) to start, an installer (NSIS/WiX/Inno Setup) only if a real need shows up later.

**Dockerfile for the CI image**:

```dockerfile
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    git ca-certificates \
    cmake ninja-build \
    gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 \
    ccache \
    python3 python3-pip \
    glslc \
    && pip install --no-cache-dir --break-system-packages aqtinstall \
    && rm -rf /var/lib/apt/lists/*
```

(`glslc` here is the Linux-native shader compiler, used as a host tool during the build, per the "not cross-compile-sensitive" point above — a native Vulkan SDK/loader isn't needed for the *cross-compiled* Windows binary itself, only for this build-time step.)

**Persistent caching**, so CI doesn't recompile vision.cpp/ggml and vcpkg's deps from scratch on every run:

1. Create two named Docker volumes on the DinD daemon the runner talks to: `docker volume create ci-ccache` and `docker volume create ci-vcpkg-cache`. (Named volumes avoid the UID/permission mismatches raw bind-mounts commonly hit here; their persistence depends on the DinD container's own `/var/lib/docker` itself being durable, worth confirming.)
2. Allow-list both in the runner's `config.yaml`:
   ```yaml
   container:
     valid_volumes:
       - ci-ccache
       - ci-vcpkg-cache
   ```
   and restart the runner for it to take effect.
3. Reference them in the workflow job, with `CMAKE_C/CXX_COMPILER_LAUNCHER=ccache` (works transparently with the mingw cross-compiler — ccache just wraps whatever compiler command CMake invokes) and vcpkg's `files` binary-cache backend:
   ```yaml
   jobs:
     windows-cross-build:
       runs-on: docker
       container:
         image: forgejo.yourdomain/you/ci-windows-cross:v1
         volumes:
           - ci-ccache:/ccache
           - ci-vcpkg-cache:/vcpkg-cache
       env:
         CCACHE_DIR: /ccache
         CCACHE_MAXSIZE: 5G
         VCPKG_BINARY_SOURCES: "clear;files,/vcpkg-cache,readwrite"
       steps:
         - uses: actions/checkout@v4
           with:
             submodules: recursive
         - name: Configure
           run: |
             cmake --preset default \
               -DCMAKE_C_COMPILER_LAUNCHER=ccache \
               -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
         - name: Build
           run: cmake --build build
   ```
   `ccache` evicts on its own once `CCACHE_MAXSIZE` is hit; vcpkg's `files` cache backend doesn't auto-evict, but with only three small vcpkg deps that's not a practical concern yet.

## v1 (MVP) scope: Linux only

- Single image, drag-and-drop input.
- Background removal via BiRefNet-lite running on vision.cpp/Vulkan (CPU fallback if no Vulkan).
- Minimal UI: open image, see transparent result, export. No library, no history, no batch queue yet.
- Output: transparent PNG.
- Packaging: AppImage, portable, no install/root required, avoids the sandboxing/GPU-passthrough overhead Flatpak would add. Distro-specific packages (deb/rpm) can follow later.

## Deferred (post-Linux-MVP, roughly in order)

1. ~~Batch / folder processing~~ **Done.** Drop a folder instead of a file: `BatchRunner` (`src/core/BatchRunner.h`) finds supported images inside it (non-recursive), runs the same `Pipeline` over each, and saves results as PNGs into a separately-chosen output folder. Progress and per-file failures surface in the status label.
2. ~~Upscaling~~ **Done.** `UpscaleStep`/`VisionCppUpscaleModel` (`src/core/UpscaleStep.h`) mirror the `SegmentationModel` seam, running the `ESRGAN-4x-foolhardy_Remacri` checkpoint (BSD-3-Clause). Simple mode runs it or background removal one at a time; Advanced mode can combine both in either order, since `UpscaleStep` rebuilds the alpha channel after the RGB-only upscale. Adjustable upscale amount was considered and rejected, see "Model management" above.
3. ~~Depth-of-field / bokeh~~ **Mask-only, with an adjustable strength slider. Done.** `BokehStep` (`src/core/BokehStep.h`) reuses `BackgroundRemovalStep`'s `SegmentationModel`, no separate depth model, and box-blurs everything outside the mask, with the mask's own soft edge feathering the transition; strength is a live-previewed 0-100% slider in the Settings dialog. See "Bokeh strength control" above. Full depth-graduated blur (Depth-Anything V2) is still a later refinement, not scoped yet.
4. ~~Video / GIF support~~ **GIF done; video still deferred.** `GifIO` (`src/core/GifIO.h`) reads animated GIF frames via Qt's decoder, runs each through the same `Pipeline`, and re-encodes as a new GIF via giflib. `BatchRunner` and `MainWindow`'s frame-cycling preview both support this. True video needs FFmpeg or similar and is out of scope for now, see "Video/GIF scope" above.
5. ~~Model swappability~~ **Done.** See "Model management" above for the full design: a Settings dialog, per-model downloads into AppData, and live swap.
6. Linux distro-native packages (deb/rpm) alongside the AppImage.
7. Windows port (portable single .exe to start; installer only if a real need shows up). Build/CI approach researched and decided: MinGW-w64 cross-compilation from the existing Linux Forgejo runner, see "Windows port: cross-compilation via MinGW-w64" above.
8. macOS port, contingent on tester access.

**Contingent, not on the list above**: colorizing black-and-white photos. Candidate model is DDColor (Apache-2.0), tentative. No GGUF weights exist for any permissively-licensed colorization model, so this needs a from-scratch PyTorch-to-GGUF conversion, comparable to BiRefNet's, gated behind a feasibility spike before it gets a firm slot, after distro packages and before the Windows port if it clears that spike.
