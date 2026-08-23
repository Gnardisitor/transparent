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
7. Windows port (portable single .exe to start; installer only if a real need shows up).
8. macOS port, contingent on tester access.

**Contingent, not on the list above**: colorizing black-and-white photos. Candidate model is DDColor (Apache-2.0), tentative. No GGUF weights exist for any permissively-licensed colorization model, so this needs a from-scratch PyTorch-to-GGUF conversion, comparable to BiRefNet's, gated behind a feasibility spike before it gets a firm slot, after distro packages and before the Windows port if it clears that spike.
