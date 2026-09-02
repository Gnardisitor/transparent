# transparent: local, GPU-accelerated image editing

## Motivation

Existing local background-removal tools are bad. They have broken or missing GPU acceleration on Linux, they fit the OS poorly, and they nag for subscriptions. Web tools are worse. This started as a local-first, offline alternative built to work well on Linux first. It has grown into a broader image-editing toolkit (background removal, upscaling, bokeh, and more) on one pipeline-step architecture and the same no-cloud, no-subscription, no-telemetry rules.

## Project intent

- Personal tool first, with intent to open-source once it works.
- Free. No subscription, no cloud calls, no telemetry.
- License: GPLv3 for this repo's own code (see [LICENSE](LICENSE)).
  - Originally planned as LGPLv3 to match Qt's own license. LGPL exists so proprietary software can link against your code without inheriting your license, which is why Qt itself is LGPL. `transparent` is an application, not a library, so that reason does not apply. GPLv3 requires anyone distributing a modified version to share the source under the same terms, which is what was actually wanted. It was chosen over GPLv2 for the explicit patent grant. Nothing here needs GPLv2 compatibility.
  - Compatible with dynamically-linked LGPLv3 Qt and MIT-licensed vision.cpp/BiRefNet-lite. GPL projects commonly dynamically link LGPL libraries.
  - Bundled components keep their own licenses (Qt LGPLv3 dynamically linked, vision.cpp/ggml MIT, BiRefNet-lite and SCUNet MIT/Apache-2.0, Real-ESRGAN BSD-3-Clause, giflib MIT). They are listed in the README.

## Platform rollout (sequential, not parallel)

1. Linux, done.
2. Windows, done natively (MSVC). The MinGW cross-build from the Linux runner remains an option for CI packaging.
3. macOS: last priority, dependent on getting help from a Mac-owning friend to test.

## Architecture decisions

### Inference engine (vision.cpp + Vulkan)

vision.cpp (Acly, ggml-based, MIT) runs everything through a single Vulkan backend. That gives vendor-neutral GPU acceleration across NVIDIA/AMD/Intel with no per-vendor branching, the same approach Upscayl and chaiNNer use. ggml's CPU backend is the automatic fallback when no Vulkan device is available.

ncnn was the original choice for the same vendor-neutral reasoning, but its conversion tooling (`pnnx`, `onnx2ncnn`) cannot translate the transformer and deformable-conv ops BiRefNet-lite needs. vision.cpp implements both natively. The full history of the ncnn attempt is in git log. ONNX Runtime was also considered and rejected because it has no mature Vulkan execution provider.

**No image tiling for segmentation.** BiRefNet-lite's GGUF always downsamples input to 1024x1024 before inference and upsamples the mask back, so VRAM cost is constant regardless of input size. vision.cpp auto-downscales against a 4GB single-tensor cap for very large images. Tiling would not help quality either, since segmentation needs whole-image context to identify the subject.

**Higher-resolution background removal, if ever needed.** `BiRefNet-dynamic` scales its internal resolution with input size instead of the fixed 1024x1024. Its large-image allocator bug ([krita-vision-tools#54](https://github.com/Acly/krita-vision-tools/issues/54)) was fixed upstream before this repo's vendored vision.cpp commit. Model management (below) unlocks swapping to it.

**Depth-of-field / bokeh.** The first cut is mask-only, blurring everything outside `BackgroundRemovalStep`'s mask with a feathered edge. No separate depth model. True depth-graduated blur is possible via vision.cpp's Depth-Anything V2, but only the Small checkpoint (Apache-2.0, 50.6MB) is license-clean. Base and Large are CC-BY-NC-4.0, the same restriction that ruled out RMBG and MODNet. It would also need real implementation work (edge haloing, fine detail reading as a blob, deriving a focus plane from relative depth). Deferred until mask-only proves visibly insufficient.

**Bokeh strength control. Done.** A 0-100% slider in the Settings dialog maps to a blur radius scaled to the image's shorter side (5% at 100%), so the effect looks consistent across resolutions. Adjusting it live-previews by re-blending the mask `BokehStep` cached from the last real `process()` call, off the GUI thread via `QtConcurrent`. The blur is cheap CPU work, not model inference. Live preview only updates the screen when Bokeh is the last active step (`MainWindow::isBokehTheActiveOutputStep()`). The slider still updates and persists otherwise. Bokeh always uses whichever segmentation model Background Removal is set to, and swapping that model invalidates the cache. Both steps take the subject mask from a shared `PipelineRun` (`src/core/PipelineRun.h`), so it is inferred once per image even when both are enabled; a size change in between (the upscaler) forces a recompute.

### Video/GIF scope (GIF only for now, no FFmpeg)

Animated GIF and real video are different asks. GIF uses Qt's decoder plus a small encoder dependency. Video would need FFmpeg, a much bigger commitment (new build dependency, LGPL/GPL licensing review, patent-encumbered codecs, larger AppImage). It gets its own writeup when actually prioritized.

GIF encoding uses giflib (vcpkg, MIT). Qt's bundled GIF plugin turned out to be read-only (confirmed via `QImageWriter::supportedImageFormats()`). giflib only reads/writes the container, so `GifIO` (`src/core/GifIO.h`) also owns a small median-cut color quantizer for building the output palette.

Inherent GIF limitations (not bugs) are a 256-color-max palette shared across all frames (rebuilt via median-cut) and on/off-only transparency, where `BackgroundRemovalStep`'s soft mask edge gets thresholded at 128. GIFs are also streamed, not materialized: `GifIO::Reader`/`Writer` (`src/core/GifIO.h`) handle one frame at a time, so peak memory is one source frame plus one processed frame no matter the animation length. The streamed palette is fixed by the first processed frame's colors.

### Background removal model (BiRefNet-lite)

- MIT-licensed, strong quality for salient object segmentation/matting.
- Other models considered:
  - RMBG-1.4/2.0 (BRIA): rejected, `bria-rmbg-1.4` license (source-available, non-commercial,
    redistribution-restricted). IS-Net-family architecture, unsupported by vision.cpp. Users who
    want it anyway can bring their own weights via the custom-model import (see Custom models); this project
    never redistributes the weights.
  - MODNet: rejected, CC BY-NC-SA 4.0, non-commercial and portrait-specific.
  - IS-Net/U2Net: a viable fallback (Apache-2.0, smaller/faster) but visibly lower quality.
  - BEN2-base: MIT, a close alternative worth a look if BiRefNet-lite underperforms in practice.

### Model management (done)

Both `SegmentationModel` (BiRefNet-lite / BiRefNet-dynamic / BiRefNet full) and `UpscaleModel` (foolhardy_Remacri / NMKD-Superscale-SP) are swappable, using license-clean checkpoints published at the same huggingface.co/Acly account the build already downloads from.

Adjustable upscale *amount* (as opposed to model choice) was considered and rejected. Every compatible Real-ESRGAN checkpoint is fixed at 4x, and the only different-shaped file (`RealESRGAN-x4plus_anime-6B`) is a "plus" variant `esrgan_load_model` does not support.

Design:

- Curated registry with a fixed list per category (name, filename, license, size, download URL, SHA256). Entries are marked Installed or Not Installed by filename match.
- On-demand download for everything beyond the defaults. Each entry has its own download button, runs off the GUI thread, and is verified against its known SHA256 before being marked Installed. A mismatch deletes the file and shows an error. Manually placing a file in the models directory works the same way.
- Models live in `QStandardPaths::AppDataLocation`, off the build tree. The build-tree path is wiped by clean rebuilds and is no place for manual placement.
- First run needs no network access. The three defaults (BiRefNet-lite, Remacri, and SCUNet real GAN) are fetched at CMake configure time, and the app copies them into AppData on first launch if missing.
- Selecting a model applies live, no restart. The steps take their model through a swappable `shared_ptr`, and loading a new model runs asynchronously with a spinner using the same `QtConcurrent`/`QFutureWatcher` pattern `MainWindow` already uses.
- It lives in a Settings dialog opened from a menu-bar action next to Help, not in the Simple/Advanced mode selector. Model choice and bokeh strength are set-occasionally preferences, and nesting them in Advanced mode would lock Simple-mode users out of picking BiRefNet-dynamic for a large photo.
- Persisted via `QSettings`.

### Custom models (done)

Users can add models that are not in the curated catalog. The motivating case is RMBG-1.4, which this project cannot host due to its license. Built to stay minimal, since the audience is advanced users setting things up once.

Facts the design rests on:

- vision.cpp recognizes six GGUF architectures: `birefnet`, `scunet`, `esrgan`, `migan`, `depthanything`, `mobile-sam`. The seams here are single-architecture per category: Segmentation = birefnet, Denoise = scunet, Upscale = esrgan.
- RMBG-1.4 is IS-Net/U2-Net family, so it does not load today, and Acly's ecosystem does not support it either. Loading it means a new architecture in the fork (converter support, arch code, parity tests), deferred as its own future project.
- A GGUF's `general.architecture` is readable cheaply from metadata alone, so files are classified without loading weights.

Decisions:

- Folder-as-state. The models directory is scanned for `.gguf` files, and recognized architectures register under their category (`birefnet` to Segmentation, `scunet` to Denoise, `esrgan` to Upscale). No import dialog, no naming step, no persisted custom-model list. The folder is the state, so a deleted file disappears on the next scan.
- Display name is the filename. Custom rows read "user-provided (license not verified)".
- One "Add model from disk…" button at the bottom of Settings. File dialog, architecture validation, copy into the models directory, and the row appears in the right section on its own. The architecture routes the file, so the button needs no category input.
- Unrecognized `.gguf` files render as grayed rows with a tooltip naming the architecture. This also covers arches vision.cpp knows but this app has no seam for (`migan`, `depthanything`, `mobile-sam`).
- Removal is file-manager deletion only this round, no delete UI.
- Licensing rule: transparent-models never hosts non-commercial weights. Restricted models enter only via user import.

The scan reads GGUF headers only, so it is cheap regardless of file sizes. Settings rescans on open and after an import, and re-applies the persisted active selection afterwards.

### Denoising (SCUNet)

**Model: SCUNet (Swin-Conv-UNet), checkpoints `scunet_color_real_gan` and `scunet_color_real_psnr`, both Apache-2.0.** Blind real-world color denoising, ~18M params. The GAN variant is the default (sharper, PSNR smoother). Rejected alternatives: SwinIR denoise (non-blind, fixed sigma, ~12x the FLOPs), NAFNet (weak color real-denoise coverage), Restormer (heavy, no permissive GGUF ecosystem), realesr-general-x4v3 (an upsampler, and visp's `esrgan` arch is RRDB-only). The fixed-sigma gaussian SCUNet variants are niche and skipped.

**Fork strategy: a private fork of vision.cpp, not upstream PRs.** Upstream has no denoising architecture, and visp's public headers don't expose the conv/attention/UNet primitives, so implementing SCUNet in `transparent` would mean reimplementing visp internals against raw ggml. The fork lives at [forge.db-serve.com/dbajan/vision.cpp](https://forge.db-serve.com/dbajan/vision.cpp), is based on upstream `main`, and tracks its `main` branch. Set a `GIT_TAG` when a fork tag exists. The SCUNet port is in `src/visp/arch/scunet.*`, converter support in `scripts/convert.py`, and per-layer parity tests in `tests/test_scunet.py` (10 cases, all passing, CLI output within 1 LSB of the PyTorch reference on CPU and Vulkan).

**ggml update policy:** the submodule pins Acly's vision patch series (`vision-20260331`: conv2d_deform, cwhn im2col, f16 repeat) which upstream llama.cpp lacks. Don't bump unprompted. Bump when Acly publishes a newer `vision-*` tag, or when profiling shows a ggml-bound bottleneck, and re-run the parity suite afterwards.

**Hosting:** the Forgejo LFS repo [transparent-models](https://forge.db-serve.com/dbajan/transparent-models) holds `scunet-color-real-gan-F16.gguf` and `scunet-color-real-psnr-F16.gguf` (F16, ~36MB each, SHA256s in `ModelCatalog.cpp`). The GAN checkpoint is also a CMake-time default. The PSNR variant is download-on-demand.

**App integration:** a standalone `DenoiseStep` mirroring `UpscaleStep`, with `ModelCategory::Denoise`, a `DenoiseModel` seam, and a `VisionCppDenoiseModel` adapter. RGB-only, preserving an existing alpha channel. Pipeline order is BackgroundRemoval, Bokeh, Denoise, Upscale. Bokeh sits right after background removal so its blur runs at source resolution. Denoising runs before upscaling because upscaling amplifies noise. Large images run full-resolution up to 2.25MP (~1500x1500), then 512px tiles with 32px overlap, feather-blended inside `scunet_compute`. Tile sizes are 64-aligned, which SCUNet requires and which gives edge tiles replicate padding for free.

**Performance (512x512, RX 9070 XT / Vulkan):** tuned from ~464ms to ~268ms. The current graph runs direct convolutions with no im2col materialization, folds layer-norm affine transforms into the following linear, and uses non-flash attention. Remaining costs are generic elementwise ops. Making them f16 would need new backend shaders, which is not worth it. Details in git history and the tests.

### Language and UI (C++ and Qt Widgets)

- C++ links directly against vision.cpp's native API. No FFI layer, smallest binary, fastest startup.
- Qt Widgets, not QML/Qt Quick. Simpler and lighter, sufficient for a deliberately minimal interface.

### Preview zoom & before/after compare (done)

A zoomable preview with a before/after wipe, built as the `PreviewCanvas` widget (`src/ui/PreviewCanvas.h`). Covered by `tests/test_preview_canvas.cpp` (geometry and render checks) and a `MainWindow` integration test. Decisions, which double as the user-facing behavior:

- "Before" is the original image as loaded, "after" is the final pipeline result. Intermediate per-step results are not captured.
- The wipe slider is the compare presentation, on by default for stills. A button under the close button toggles it, `W` toggles it from the keyboard, and the enabled state persists in QSettings. There is no A/B hold-key flip; one compare mechanism.
- Pan and zoom are shared between both images, so they stay aligned at any zoom.
- Cursor-anchored wheel zoom, left-drag pan, double-click toggles fit and 100%, `+`/`-` zoom, `F` fits, `1` is 100%. A small overlay strip at the top-left shows the zoom percentage with fit and 1:1 buttons. The strip paints a dark scrim with white text so it stays readable over any image.
- Rendering is smooth while shrinking and nearest-neighbor at 200% magnification and up. Smooth upscaling hides the pixel differences zoom exists to show. Max zoom is 16x.
- View state survives re-renders (bokeh slider ticks, model swaps, mode switches). A new image load resets to fit, wipe on, divider at 50%.
- The divider is a clean vertical line with a grip and a hover cursor, spanning the image area only.
- Animated GIFs get zoom but not compare (full-resolution per-frame data is streamed and discarded). The wipe button renders disabled with a tooltip. Batch runs show no compare UI.
- While a still is processing, both wipe halves show the source until the result arrives.

### Internal architecture (pipeline steps from day one)

The MVP is built as swappable pipeline steps (image in, image out) rather than a hardcoded path. That made batch processing, upscaling, and GIF support additive later instead of a rewrite.

`BackgroundRemovalStep` never talks to vision.cpp directly, only to a `SegmentationModel` interface (`isReady()` / `computeMask()`). `NcnnSegmentationModel` was the first adapter and `VisionCppSegmentationModel` replaced it without touching `BackgroundRemovalStep`. `UpscaleModel` mirrors the same seam for Real-ESRGAN.

### Windows port

Shipped: a native MSVC build, exercised by CI on every change. The `windows` CMake preset uses vcpkg's `x64-windows-static-md` triplet (static giflib, dynamic CRT to match the prebuilt Qt binaries), the Vulkan SDK provides `glslc` for the shader build, and a portable folder comes from `windeployqt` plus a manual copy of the Vulkan loader. No installer yet.

If Windows packaging should ever run on the Linux CI runner instead of a Windows machine, the decided approach is cross-compilation with mingw-w64 and vcpkg's community `x64-mingw-dynamic` triplet, Qt via `aqtinstall`, and a Linux `glslc` (it compiles target-agnostic SPIR-V as a host step). Open questions from the earlier research: whether vision.cpp's CMake cooperates with `CMAKE_CROSSCOMPILING`, whether that triplet builds giflib and the Vulkan loader, and how to package without a native `windeployqt` (run it under Wine, or hardcode the Qt DLL list). The goal is a portable zipped `.exe`. A purpose-built CI image (Debian plus mingw-w64, aqtinstall, and glslc, with ccache and vcpkg-cache volumes allow-listed in the runner config) would come first. None of this work has started.

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
7. ~~Windows port~~ **Native MSVC build done**, including a portable folder via `windeployqt`. Remaining: an installer only if a real need shows up, and optionally the MinGW cross-build so the Linux runner can package Windows binaries.
8. macOS port, contingent on tester access.

Also done since the list above was written: SCUNet denoising, the zoom and before/after wipe preview, and custom user-imported models. See the sections above.

**Contingent, not on the list above**: colorizing black-and-white photos. Candidate model is DDColor (Apache-2.0), tentative. No GGUF weights exist for any permissively-licensed colorization model, so this needs a from-scratch PyTorch-to-GGUF conversion and a feasibility spike before it gets a firm slot.

## CI/CD with Forgejo Actions

The remote (`forge.db-serve.com`) is a self-hosted Forgejo instance, so GitHub Actions does not apply. Forgejo Actions is GitHub-Actions-compatible, with workflow YAML in `.forgejo/workflows/`, matching what this account's other repos already run: `runs-on: homelab` against a self-hosted runner, `actions/checkout` plus apt for dependencies, `secrets.TOKEN` for anything hitting the Forgejo API.

- `.forgejo/workflows/ci.yml`: build + `ctest` on every push and PR. Installs Qt6/Ninja via apt and caches `ccache` and vcpkg's download cache across runs.
- `.forgejo/workflows/release.yml`: on a published release, runs `packaging/build-appimage.sh` and uploads the AppImage plus a SHA256SUMS file as release assets via `actions/forgejo-release`.

**`glslc` comes from LunarG's Vulkan SDK apt repo, not Ubuntu's own.** The first CI run failed at link time with `undefined reference to repeat_f16_data`/`repeat_f16_len` because ggml's Vulkan shader generator silently dropped an fp16 shader variant instead of failing loudly. The runner's `glslc` (Ubuntu noble's `universe` package, `2023.8-1build1`) was too old. Both workflows now add `packages.lunarg.com`'s apt repo and install `vulkan-sdk` from there, the same fix llama.cpp's official Vulkan Dockerfile uses. LunarG stopped updating their Ubuntu packages after May 2025 in favor of the Linux tarball, so if a future ggml shader needs something newer, the tarball is the fallback.
