# transparent: local, GPU-accelerated background removal for Linux

## Motivation

Existing local background-removal tools are bad. They have broken or missing GPU acceleration on Linux, they fit the OS poorly, and they nag for subscriptions. Web tools are worse. This started as a local-first, offline alternative built to work well on Linux first. It has grown into a broader image-editing toolkit (background removal, upscaling, bokeh, and more) on one pipeline-step architecture and the same no-cloud, no-subscription, no-telemetry rules.

## Project intent

- Personal tool first, with intent to open-source once it works.
- Free. No subscription, no cloud calls, no telemetry.
- License: GPLv3 for this repo's own code (see [LICENSE](LICENSE)).
  - Originally planned as LGPLv3 to match Qt's own license. LGPL exists so proprietary software can link against your code without inheriting your license, which is why Qt itself is LGPL. `transparent` is an application, not a library, so that reason does not apply. GPLv3 requires anyone distributing a modified version to share the source under the same terms, which is what was actually wanted. It was chosen over GPLv2 for the explicit patent grant. Nothing here needs GPLv2 compatibility.
  - Compatible with dynamically-linked LGPLv3 Qt and MIT-licensed vision.cpp/BiRefNet-lite. GPL projects commonly dynamically link LGPL libraries.
  - Bundled components keep their own licenses (Qt LGPLv3 dynamically linked, vision.cpp/ggml MIT, BiRefNet-lite MIT). They are listed in-repo and in the About screen.

## Platform rollout (sequential, not parallel)

1. Linux: build this first, completely, before touching anything else.
2. Windows: after Linux fully works and is validated on real hardware (AMD RX 9070 XT, RDNA4).
3. macOS: last priority, dependent on getting help from a Mac-owning friend to test.

## Architecture decisions

### Inference engine (vision.cpp + Vulkan)

vision.cpp (Acly, ggml-based, MIT) runs everything through a single Vulkan backend. That gives vendor-neutral GPU acceleration across NVIDIA/AMD/Intel with no per-vendor branching, the same approach Upscayl and chaiNNer use. ggml's CPU backend is the automatic fallback when no Vulkan device is available.

ncnn was the original choice for the same vendor-neutral reasoning, but its conversion tooling (`pnnx`, `onnx2ncnn`) cannot translate the transformer and deformable-conv ops BiRefNet-lite needs. vision.cpp implements both natively. The full history of the ncnn attempt is in git log. ONNX Runtime was also considered and rejected because it has no mature Vulkan execution provider.

**No image tiling for segmentation.** BiRefNet-lite's GGUF always downsamples input to 1024x1024 before inference and upsamples the mask back, so VRAM cost is constant regardless of input size. vision.cpp auto-downscales against a 4GB single-tensor cap for very large images. Tiling would not help quality either, since segmentation needs whole-image context to identify the subject.

**Higher-resolution background removal, if ever needed.** `BiRefNet-dynamic` scales its internal resolution with input size instead of the fixed 1024x1024. Its large-image allocator bug ([krita-vision-tools#54](https://github.com/Acly/krita-vision-tools/issues/54)) was fixed upstream before this repo's vendored vision.cpp commit. Model management (below) unlocks swapping to it.

**Depth-of-field / bokeh.** The first cut is mask-only, blurring everything outside `BackgroundRemovalStep`'s mask with a feathered edge. No separate depth model. True depth-graduated blur is possible via vision.cpp's Depth-Anything V2, but only the Small checkpoint (Apache-2.0, 50.6MB) is license-clean. Base and Large are CC-BY-NC-4.0, the same restriction that ruled out RMBG and MODNet. It would also need real implementation work (edge haloing, fine detail reading as a blob, deriving a focus plane from relative depth). Deferred until mask-only proves visibly insufficient.

**Bokeh strength control. Done.** A 0-100% slider in the Settings dialog maps to a blur radius scaled to the image's shorter side (5% at 100%), so the effect looks consistent across resolutions. Adjusting it live-previews by re-blending the mask `BokehStep` cached from the last real `process()` call, off the GUI thread via `QtConcurrent`. The blur is cheap CPU work, not model inference. Live preview only updates the screen when Bokeh is the last active step (`MainWindow::isBokehTheActiveOutputStep()`). The slider still updates and persists otherwise. Bokeh always uses whichever segmentation model Background Removal is set to, and swapping that model invalidates the cache.

### Video/GIF scope (GIF only for now, no FFmpeg)

Animated GIF and real video are different asks. GIF uses Qt's decoder plus a small encoder dependency. Video would need FFmpeg, a much bigger commitment (new build dependency, LGPL/GPL licensing review, patent-encumbered codecs, larger AppImage). It gets its own writeup when actually prioritized.

GIF encoding uses giflib (vcpkg, MIT). Qt's bundled GIF plugin turned out to be read-only (confirmed via `QImageWriter::supportedImageFormats()`). giflib only reads/writes the container, so `GifIO` (`src/core/GifIO.h`) also owns a small median-cut color quantizer for building the output palette.

Inherent GIF limitations (not bugs) are a 256-color-max palette shared across all frames (rebuilt via median-cut) and on/off-only transparency, where `BackgroundRemovalStep`'s soft mask edge gets thresholded at 128.

### Background removal model (BiRefNet-lite)

- MIT-licensed, strong quality for salient object segmentation/matting.
- Other models considered:
  - RMBG-1.4/2.0 (BRIA): rejected, CC BY-NC 4.0, non-commercial only.
  - MODNet: rejected, CC BY-NC-SA 4.0, non-commercial and portrait-specific.
  - IS-Net/U2Net: a viable fallback (Apache-2.0, smaller/faster) but visibly lower quality.
  - BEN2-base: MIT, a close alternative worth a look if BiRefNet-lite underperforms in practice.

### Model management (done)

Both `SegmentationModel` (BiRefNet-lite / BiRefNet-dynamic / BiRefNet full) and `UpscaleModel` (foolhardy_Remacri / NMKD-Superscale-SP) are swappable, using license-clean checkpoints published at the same huggingface.co/Acly account the build already downloads from.

Adjustable upscale *amount* (as opposed to model choice) was considered and rejected. Every compatible Real-ESRGAN checkpoint is fixed at 4x, and the only different-shaped file (`RealESRGAN-x4plus_anime-6B`) is a "plus" variant `esrgan_load_model` does not support.

Design:

- Curated registry, not free-form file browsing. The selection screen knows a fixed list per category (name, filename, license, size, download URL, SHA256), scans the models directory, and marks each entry Installed or Not Installed by filename match. Browsing for an arbitrary `.gguf` is deferred, since vision.cpp's loaders are architecture-specific and would fail on an incompatible file anyway.
- On-demand download, not bundled at build time. Each entry has its own download button, runs off the GUI thread, and is verified against its known SHA256 before being marked Installed. A mismatch deletes the file and shows an error. Manually placing a file in the models directory works the same way.
- Models live in `QStandardPaths::AppDataLocation`, off the build tree. The build-tree path is wiped by clean rebuilds and is no place for manual placement.
- First run still needs no network access. The two defaults are fetched at CMake configure time as before, and the app copies them into AppData on first launch if missing.
- Selecting a model applies live, no restart. The steps take their model through a swappable `shared_ptr`, and loading a new model runs asynchronously with a spinner using the same `QtConcurrent`/`QFutureWatcher` pattern `MainWindow` already uses.
- It lives in a Settings dialog opened from a menu-bar action next to Help, not in the Simple/Advanced mode selector. Model choice and bokeh strength are set-occasionally-then-forget preferences, and nesting them in Advanced mode would lock Simple-mode users out of picking BiRefNet-dynamic for a large photo.
- Persisted via `QSettings`.

### Language and UI (C++ and Qt Widgets)

- C++ links directly against vision.cpp's native API. No FFI layer, smallest binary, fastest startup.
- Qt Widgets, not QML/Qt Quick. Simpler and lighter, sufficient for a deliberately minimal interface.

### Internal architecture (pipeline steps from day one)

The MVP is built as swappable pipeline steps (image in, image out) rather than a hardcoded path. That made batch processing, upscaling, and GIF support additive later instead of a rewrite.

`BackgroundRemovalStep` never talks to vision.cpp directly, only to a `SegmentationModel` interface (`isReady()` / `computeMask()`). `NcnnSegmentationModel` was the first adapter and `VisionCppSegmentationModel` replaced it without touching `BackgroundRemovalStep`. `UpscaleModel` mirrors the same seam for Real-ESRGAN.

### Windows port (MinGW-w64 cross-compilation from the Linux CI runner)

Researched and decided ahead of starting the port. The work itself is still gated behind Linux distro packages (see deferred list).

**Decision. Cross-compile Windows binaries from the same Linux box that hosts Forgejo and its Actions runner, using mingw-w64, rather than standing up a native Windows CI runner.** There is no second machine for a native runner, and Forgejo's official `act_runner` is Linux-only. Its unofficial Windows build is alpha-quality and "should not be considered secure enough to deploy in production". Cross-compiling reuses infrastructure that already exists and is trusted.

**Toolchain**:

- **mingw-w64** (`gcc-mingw-w64-x86-64`/`g++-mingw-w64-x86-64`) targeting `x86_64-w64-mingw32`, for the app's code and for cross-compiling vision.cpp/ggml.
- **vcpkg's community `x64-mingw-dynamic` triplet** (chainloaded via `VCPKG_CHAINLOAD_TOOLCHAIN_FILE`) for `giflib` and `vulkan`/`vulkan-headers`. Not covered by vcpkg's own CI, but these are small portable C libraries with a long history of building under MinGW.
- **Qt6 via `aqtinstall`**, not from source. It fetches Qt's official prebuilt Windows MinGW 64-bit binaries on any host OS, which removes the single biggest from-source risk.
- **`glslc` is not cross-compile-sensitive.** It compiles GLSL to target-agnostic SPIR-V as a host build step, and ggml's CMake already builds its `vulkan-shaders-gen` tool for the host when `CMAKE_CROSSCOMPILING` is set. A native Linux `glslc` install covers this.

**Open unknowns, to resolve with a manual local spike before any CI**: whether vision.cpp's own `CMakeLists.txt` cooperates with `CMAKE_CROSSCOMPILING`, whether vcpkg's mingw triplet builds `giflib` and the Vulkan loader, and how to package the result (`windeployqt.exe` is a Windows PE tool and will not run natively on the Linux build host. Run it under Wine, or hardcode the app's fixed Qt DLL list as a CMake install step). Expect a day or two of focused work, not open-ended research.

**Steps, in order**:

1. Do the cross-compile by hand on the homelab box first, outside CI, to resolve the unknowns above.
2. Capture the working toolchain in a Dockerfile (below) and build a purpose-built CI image.
3. Push the image to Forgejo's built-in container registry as `forgejo.yourdomain/you/ci-windows-cross:v1`.
4. Add a `.forgejo/workflows/windows-cross-build.yaml` job with `runs-on: docker`, wired to the persistent caching below.
5. Only then consider an installer. The stated goal is a portable single `.exe` (windeployqt or manual DLL list, zipped) to start.

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

(`glslc` here is the Linux-native shader compiler, used as a host tool during the build. A native Vulkan SDK/loader is not needed for the cross-compiled Windows binary itself.)

**Persistent caching**, so CI does not recompile vision.cpp/ggml and vcpkg's deps from scratch every run:

1. Create two named Docker volumes on the DinD daemon: `ci-ccache` and `ci-vcpkg-cache`. Named volumes avoid the UID/permission mismatches raw bind-mounts commonly hit. Their persistence depends on the DinD container's own `/var/lib/docker` being durable, worth confirming.
2. Allow-list both in the runner's `config.yaml` and restart the runner:
   ```yaml
   container:
     valid_volumes:
       - ci-ccache
       - ci-vcpkg-cache
   ```
3. Reference them in the workflow job:
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
   `ccache` evicts on its own once `CCACHE_MAXSIZE` is hit. vcpkg's `files` backend does not auto-evict, but with only three small deps that is not a practical concern yet.

## v1 (MVP) scope (Linux only)

- Single image, drag-and-drop input.
- Background removal via BiRefNet-lite on vision.cpp/Vulkan, CPU fallback if no Vulkan.
- Minimal UI: open image, see transparent result, export. No library, history, or batch queue.
- Output: transparent PNG.
- Packaging: portable AppImage, no install or root required, avoiding Flatpak's sandboxing/GPU-passthrough overhead. **Done.** `packaging/build-appimage.sh` installs into an AppDir with `cmake --install` and runs linuxdeploy plus linuxdeploy-plugin-qt. Two upstream quirks needed workarounds: linuxdeploy's bundled `strip` predates DT_RELR relocations and aborts on libraries built with them (`NO_STRIP=1`), and on distros where KDE's kimageformats shares Qt's `plugins/imageformats` directory (e.g. Arch) several of its plugins have unresolvable dependencies, so `kimg_*.so` is excluded from the deploy step. Verified end to end on the dev machine.

## Deferred (post-Linux-MVP, roughly in order)

1. ~~Batch / folder processing~~ **Done.** Drop a folder instead of a file. `BatchRunner` (`src/core/BatchRunner.h`) finds supported images inside it (non-recursive), runs the same `Pipeline` over each, and saves PNGs to a chosen output folder. Progress and failures surface in the status label.
2. ~~Upscaling~~ **Done.** `UpscaleStep`/`VisionCppUpscaleModel` mirror the `SegmentationModel` seam, running the `ESRGAN-4x-foolhardy_Remacri` checkpoint (BSD-3-Clause). `UpscaleStep` rebuilds the alpha channel after the RGB-only upscale, so Simple mode runs it or background removal one at a time and Advanced mode can combine both.
3. ~~Depth-of-field / bokeh~~ **Mask-only with an adjustable strength slider. Done.** `BokehStep` (`src/core/BokehStep.h`) reuses `BackgroundRemovalStep`'s `SegmentationModel` and box-blurs everything outside the mask. Full depth-graduated blur (Depth-Anything V2) is a later refinement, not scoped yet.
4. ~~Video / GIF support~~ **GIF done, video still deferred.** `GifIO` (`src/core/GifIO.h`) reads animated GIF frames via Qt's decoder, runs each through the same `Pipeline`, and re-encodes via giflib. True video needs FFmpeg or similar.
5. ~~Model swappability~~ **Done.** See model management above.
6. Linux distro-native packages (deb/rpm) alongside the AppImage.
7. Windows port (portable single .exe first, installer only if a real need shows up). Approach decided: MinGW-w64 cross-compilation from the Linux Forgejo runner.
8. macOS port, contingent on tester access.

**Contingent, not on the list above**: colorizing black-and-white photos. Candidate model is DDColor (Apache-2.0), tentative. No GGUF weights exist for any permissively-licensed colorization model, so this needs a from-scratch PyTorch-to-GGUF conversion and a feasibility spike before it gets a firm slot.

## CI/CD with Forgejo Actions

The remote (`forge.db-serve.com`) is a self-hosted Forgejo instance, so GitHub Actions does not apply. Forgejo Actions is GitHub-Actions-compatible, with workflow YAML in `.forgejo/workflows/`, matching what this account's other repos already run: `runs-on: homelab` against a self-hosted runner, `actions/checkout` plus apt for dependencies, `secrets.TOKEN` for anything hitting the Forgejo API.

- `.forgejo/workflows/ci.yml`: build + `ctest` on every push and PR. Installs Qt6/Ninja via apt and caches `ccache` and vcpkg's download cache across runs.
- `.forgejo/workflows/release.yml`: on a published release, runs `packaging/build-appimage.sh` and uploads the AppImage plus a SHA256SUMS file as release assets via `actions/forgejo-release`.

**`glslc` comes from LunarG's Vulkan SDK apt repo, not Ubuntu's own.** The first CI run failed at link time with `undefined reference to repeat_f16_data`/`repeat_f16_len` because ggml's Vulkan shader generator silently dropped an fp16 shader variant instead of failing loudly. The runner's `glslc` (Ubuntu noble's `universe` package, `2023.8-1build1`) was too old. Both workflows now add `packages.lunarg.com`'s apt repo and install `vulkan-sdk` from there, the same fix llama.cpp's official Vulkan Dockerfile uses. LunarG stopped updating their Ubuntu packages after May 2025 in favor of the Linux tarball, so if a future ggml shader needs something newer, the tarball is the fallback.
