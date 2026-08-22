# transparent: local, GPU-accelerated background removal for Linux

## Motivation

Existing local background-removal tools are bad: broken/no GPU acceleration on Linux (clearly an afterthought), don't fit the OS they're running on, and nag with subscription prompts. Web tools are worse. This project started as a local-first, offline background-removal alternative built to work well on Linux first, without the bloat or licensing games. It's growing into a broader image-editing toolkit: background removal, upscaling, bokeh, colorization, and other edits that usually cost a subscription or are done poorly by existing local tools, all built on the same pipeline-step architecture and the same no-cloud, no-subscription, no-telemetry rules.

## Project intent

- Personal tool first, with a firm intent to open-source it once it works.
- Free. No subscription, no cloud calls, no telemetry.
- License: **your code under GPLv3** (see [LICENSE](LICENSE)).
  - **Originally planned as LGPLv3**, "to match Qt's own license." Revisited: LGPL exists specifically so *other people's proprietary software* can link against your code without inheriting your license. That's why Qt itself is LGPL, since Qt is a library meant to be embedded in other people's apps. `transparent` is an application, not a library anyone else is going to link into their own proprietary codebase, so LGPL's actual reason to exist doesn't apply here. GPLv3 gives the thing that was actually wanted instead: anyone who distributes a modified version has to share the source back, under the same terms. GPLv3 over GPLv2 specifically because it's the modern default (explicit patent grant) and nothing in this stack needs GPLv2 compatibility.
  - This is compatible with dynamically-linked LGPLv3 Qt and MIT-licensed vision.cpp/BiRefNet-lite without issue: GPL projects commonly dynamically link LGPL libraries.
  - Bundled third-party components keep their own licenses, made explicit in-repo and in an About screen:
    - Qt: LGPLv3, **dynamically linked** (avoids any static-linking obligations)
    - vision.cpp (and ggml): MIT
    - BiRefNet-lite: MIT

## Platform rollout (sequential, not parallel)

1. **Linux: build this first, completely, before touching anything else.** Get the engine, UI, and packaging solid here.
2. **Windows: after Linux fully works.** Don't start Windows-specific work until the Linux MVP is done and validated on real hardware (AMD RX 9070 XT, RDNA4).
3. **macOS: last priority, dependent on getting help from a Mac-owning friend to test.** Not planned for near-term work.

## Architecture decisions

### Inference engine: vision.cpp + Vulkan backend (supersedes ncnn)

- **Originally ncnn** (Tencent, BSD-3-Clause), for the reasons below. This section is kept for context, not because ncnn is still the choice:
  - The one inference engine that gives genuine vendor-neutral GPU acceleration (NVIDIA/AMD/Intel) across OSes through a single Vulkan backend, without per-vendor branching (no separate CUDA/DirectML/ROCm/CoreML paths needed). Same approach Upscayl and chaiNNer rely on.
  - Tiny footprint, pure C++, trivial to statically link, plain C API for FFI.
  - ONNX Runtime was rejected as the primary engine: no mature Vulkan execution provider (long-open, unresolved GitHub issues), still true as of this rewrite and not reopened.
  - **What actually broke this choice**: ncnn's PyTorch/ONNX conversion tooling (`pnnx`, `onnx2ncnn`) can't translate transformer ops (Swin backbone) or modulated deformable convolution (DCNv2), both of which BiRefNet-lite needs. That's what forced the MVP to ship IS-Net (a lower-quality CNN stand-in) instead of the intended BiRefNet-lite. See git history for the full account, including the hand-written ncnn custom layer that was needed and later removed.
- **Now vision.cpp** (Acly, ggml-based, MIT): same vendor-neutral single-Vulkan-backend story as ncnn (no ADR reversal on that front), but its ggml core implements Swin-Transformer and a native `conv_2d_deform` op, so BiRefNet-lite runs without any conversion workarounds or hand-written custom layers. Vendored via CMake FetchContent (pinned to a tagged release), since no vcpkg port exists.
- **CPU fallback**: `visp::backend_init()` automatically falls back to ggml's CPU backend if Vulkan isn't available, same failure-avoidance goal as before.
- **No image tiling for segmentation, and none needed.** BiRefNet-lite's GGUF (`birefnet.image_size = 1024`) always downsamples input to a fixed 1024×1024 before inference, then upsamples the resulting mask back to the original resolution (confirmed in vision.cpp's `birefnet.cpp`). VRAM cost is constant regardless of input size, so there's no capacity risk to hedge against. Tiling wouldn't help quality either: segmentation needs whole-image context to identify the subject, so a tile of someone's forearm alone can't self-identify as foreground the way ESRGAN's independently-processed upscaling tiles can. The actual mechanism vision.cpp uses for large images is a bounded auto-downscale against a hardcoded 4GB single-tensor cap (`backend_device::max_alloc()`), not tiling.
- **Higher-resolution background removal, if wanted later: `BiRefNet-dynamic`, not tiling.** `BiRefNet-dynamic-F16.gguf` is published at the same huggingface.co/Acly account this project already trusts, and scales its internal resolution with input size instead of the fixed 1024×1024 above. An allocator bug that broke it on large images ([krita-vision-tools#54](https://github.com/Acly/krita-vision-tools/issues/54): an RTX 5090 failed above ~2432×1664) was fixed upstream in September 2025 and is present in the vision.cpp commit this repo vendors, so no engineering blocker remains. Scoped under "Model swappability" in Deferred, not the current MVP default: BiRefNet-lite stays default until swappability is built. Boundary-refinement add-ons (CascadePSP, PyMatting, both MIT-licensed) were considered and skipped: added complexity for marginal gain over what BiRefNet-dynamic's own full-resolution foreground estimation already does.
- **Depth-of-field / bokeh.** First cut: mask-only, blurring everything outside `BackgroundRemovalStep`'s existing mask with a feathered edge, no new model. True depth-graduated blur (sharp near the subject, progressively blurred by distance) is technically available: vision.cpp implements Depth-Anything V2 (CLI command `depthany`), and Acly publishes GGUF weights for Small, Base, and Large. Only **Small** (Apache-2.0, DINOv2-ViT-S, 50.6MB) is license-clean; Base and Large are CC-BY-NC-4.0 and excluded under the same non-commercial policy that already ruled out RMBG and MODNet. Depth-graduated blur also needs from-scratch implementation work, since nothing like it exists in this repo yet: edge haloing where sharp foreground meets blurred background, hair and other fine detail reading as a blob since both the depth map and mask edges are low-frequency, and Depth-Anything V2's *relative* (not metric) depth means the in-focus plane has to be derived heuristically rather than read directly. Deferred until the mask-only version proves visibly insufficient.

### Video/GIF scope: GIF only for now, no FFmpeg

- Deferred item 4 ("Video / GIF support") bundles two different asks with very different dependency weight: animated GIF and real video (mp4 etc.). Splitting them was a deliberate choice, not an oversight — three options were on the table: (a) GIF only, using Qt's own GIF decoder plus a small new encoder dependency; (b) full video + GIF via FFmpeg; (c) scaffold the frame-loop plumbing with no real codec yet, deferring the dependency choice entirely. (a) was picked.
- **Why not FFmpeg (option b) for this pass**: pulling in libav* is a much larger commitment than this project's other dependencies — a new CMake `FetchContent` target, a licensing review (FFmpeg ships LGPL and GPL build variants, and some codecs are patent-encumbered), and a meaningfully larger AppImage — for a feature whose GIF half doesn't need it at all. Worth a real ADR-style writeup of its own if/when true video support gets prioritized, not a rider on the GIF work.
- **Why GIF encoding uses giflib (vcpkg, MIT), not FFmpeg or a vendored header**: Qt's bundled GIF plugin turned out to be read-only (confirmed empirically: `"gif"` is absent from `QImageWriter::supportedImageFormats()`), so writing an animated GIF back out needs something else. giflib is already in vcpkg's port set (`vulkan`/`vulkan-headers` were already pulled that way, per `vcpkg.json`), so adding it kept the same dependency mechanism instead of introducing a second one (FetchContent, like vision.cpp) or vendoring a single-header library directly into the tree. giflib only reads/writes the GIF container, though — no quantizer of its own — so `GifIO` (`src/core/GifIO.h`) also owns a small median-cut color quantizer for building the output palette.
- **Inherent GIF limitations, not bugs in this implementation**: a 256-color-max palette (one shared across all frames of a given GIF, rebuilt via median-cut) and on/off-only transparency (no smooth alpha — `BackgroundRemovalStep`'s soft mask edge gets thresholded at 128). Both are true of the GIF format itself, not something a better encoder would avoid.

### Background removal model: BiRefNet-lite

- MIT-licensed, current strong quality for salient object segmentation/matting.
- Other models considered:
  - RMBG-1.4/2.0 (BRIA): rejected for now, CC BY-NC 4.0 and non-commercial only, would block open-sourcing without a paid license.
  - MODNet: rejected, CC BY-NC-SA 4.0 and non-commercial, and portrait-specific rather than general-purpose.
  - IS-Net/U2Net: a viable fallback (Apache-2.0, smaller/faster) but visibly lower quality than BiRefNet, so not the MVP choice.
  - BEN2-base: MIT, a close alternative to BiRefNet-lite, worth a look if BiRefNet-lite underperforms in practice.

### Language & UI: C++ + Qt Widgets

- C++ links directly against vision.cpp's native API: no FFI/bridging layer, smallest binary, fastest startup, no bundled runtime to ship.
- Qt Widgets (not QML/Qt Quick): simpler and lighter than a declarative/GPU-composited UI, and sufficient for a deliberately minimal interface. Qt also re-skins itself reasonably per-OS, which directly addresses the "doesn't fit in with anything" complaint about the current tools.
- **Fallback plan**: if the C++ build/engineering effort proves too fragile or slow going, fall back to Python + PySide6 (same Qt look-and-feel) at the cost of a bundled interpreter and larger install size. Note vision.cpp doesn't have an official Python wheel the way ncnn did, so this fallback would need its own bindings story.
- C# considered and ruled out for this stack: no official bindings for ncnn or vision.cpp, and Qt isn't a natural fit in the C# ecosystem (would mean hand-written P/Invoke or dropping Qt for Avalonia).

### Internal architecture: pipeline-step abstraction from day one

- Even though MVP only does one operation (background removal), build it as a swappable "pipeline step" (image in → image out) rather than a hardcoded one-off path.
- This is what makes batch processing ("loop the step over files"), upscaling (v2, "add another step"), and video/GIF (later, "loop the step over frames") additive later instead of a rewrite.
- **One level down, the same idea again: `SegmentationModel`.** `BackgroundRemovalStep` (a pipeline step) doesn't talk to ncnn or vision.cpp directly: it calls a `SegmentationModel` interface (`isReady()` / `computeMask(QImage) -> QImage`) and knows nothing else about how the mask was produced. `NcnnSegmentationModel` was the first adapter; `VisionCppSegmentationModel` replaced it. This is why the ncnn → vision.cpp switch touched zero lines of `BackgroundRemovalStep`: the seam paid off immediately, not just hypothetically. Worth keeping this same shape when upscaling (Real-ESRGAN) is added: an `UpscaleModel`-style seam behind that pipeline step too, rather than binding it directly to vision.cpp's API.

## v1 (MVP) scope: Linux only

- Single image, drag-and-drop input.
- Background removal via BiRefNet-lite running on vision.cpp/Vulkan (CPU fallback if no Vulkan).
- Minimal UI: open image → see transparent result → export. No library, no history, no batch queue yet.
- Output: transparent PNG.
- Packaging: **AppImage**, portable, no install/root required, avoids the sandboxing/GPU-passthrough configuration overhead Flatpak would add. Distro-specific packages (deb/rpm) can follow later once the app is stable; not a Linux MVP requirement.

## Deferred (post-Linux-MVP, roughly in order)

1. ~~Batch / folder processing~~ **Done.** Drop a folder instead of a file: `BatchRunner` (`src/core/BatchRunner.h`) finds the supported images directly inside it (non-recursive), runs the same `Pipeline` over each, and saves each result as a PNG into a separately-chosen output folder. Progress and per-file failures surface in the status label; a source image that fails to load or save is skipped without stopping the rest of the batch.
2. ~~Upscaling~~ **Done.** `UpscaleStep`/`VisionCppUpscaleModel` (`src/core/UpscaleStep.h`, `src/core/VisionCppUpscaleModel.h`) mirror the `SegmentationModel` seam behind a second pipeline step, running the `ESRGAN-4x-foolhardy_Remacri` checkpoint (BSD-3-Clause, huggingface.co/Acly/Real-ESRGAN-GGUF) — picked over the repo's "plus"/pixel-shuffle variants, which vision.cpp's `esrgan_load_model` doesn't support. Simple mode runs it (or background removal) one at a time via a dropdown; Advanced mode can combine both, in either order, since `UpscaleStep` rebuilds the alpha channel after the RGB-only upscale so a prior background-removal result's transparency survives regardless of order.
3. ~~Depth-of-field / bokeh~~ **Mask-only first cut done.** `BokehStep` (`src/core/BokehStep.h`) reuses the same `SegmentationModel` background-removal already depends on — no separate depth model — and box-blurs (three-pass, separable) everything the mask calls background, blending back to the sharp original by mask weight so the existing soft mask edge is what feathers the transition. Full depth-graduated blur (Depth-Anything V2) is still a later refinement, not scoped yet.
4. ~~Video / GIF support~~ **GIF done; video still deferred.** `GifIO` (`src/core/GifIO.h`) reads an animated GIF's frames via Qt's own (read-only) GIF plugin, runs each frame through the same `Pipeline` MainWindow already uses for single images, and re-encodes the result as a new looping animated GIF. GIF encoding uses [giflib](http://giflib.sourceforge.net/) (MIT, pulled via vcpkg like Vulkan already is) rather than FetchContent, since giflib only reads/writes the container format — no quantizer of its own — so `GifIO` also owns a small median-cut color quantizer for building the output palette. `BatchRunner` picked up the same behavior for a folder containing GIFs (writes `<basename>.gif` instead of `<basename>.png` when a source file is an animated GIF); `MainWindow` gained a frame-cycling preview so a processed GIF can be watched, not just its first frame, before export. GIF's own limitations are inherent, not bugs: 256-color-max palette (shared across all frames, resampled via median-cut) and on/off-only transparency (alpha thresholded at 128, so `BackgroundRemovalStep`'s smooth mask edge loses its softness in GIF output). True video (mp4 etc.) needs a real decode/encode dependency (FFmpeg or similar) with its own licensing review and is intentionally out of scope here — see "Video/GIF scope" under Architecture decisions for why.
5. Model swappability (letting users pick BiRefNet-lite vs BiRefNet-dynamic vs BEN2-base vs others) if BiRefNet-lite turns out not to be the right default for everyone. See "Higher-resolution background removal" under Architecture decisions for the BiRefNet-dynamic option this unlocks.
6. Linux distro-native packages (deb/rpm) alongside the AppImage
7. Windows port (portable single .exe to start; installer only if a real need shows up: auto-update, Start Menu integration, etc.)
8. macOS port, contingent on tester access

**Contingent, not on the numbered list above**: colorizing black-and-white photos. Candidate model: DDColor (Apache-2.0, ConvNeXt encoder plus transformer dual-decoder), tentative and not locked in. No GGUF weights exist for any permissively-licensed colorization model, so this needs a from-scratch PyTorch-to-GGUF conversion, comparable to what BiRefNet needed, though likely less custom-kernel work since vision.cpp already implements the ops a ConvNeXt/transformer model would need. Gated behind a feasibility spike proving the conversion works before it gets a firm slot in the list above. If it clears that spike, it's placed after distro packages, before the Windows port.

## Key prior art referenced

These predate the vision.cpp switch and describe *ncnn's* track record, not this project's current runtime. Kept because the underlying point (single-Vulkan-backend beats per-vendor branching) is still exactly the argument for vision.cpp too:

- **Upscayl**: Electron + ncnn/Vulkan sidecar, proved the cross-vendor Vulkan-via-ncnn approach works in production, packaged as AppImage/deb/rpm/dmg/exe.
- **chaiNNer**: routes AMD/Intel through ncnn and NVIDIA through PyTorch/CUDA, independent confirmation that ncnn was a standard "vendor-neutral fallback" answer at the time.
- **rembg**: a cautionary example. ONNX-Runtime-based, GPU support requires users to separately install matching CUDA/cuDNN or ROCm builds themselves, exactly the fragile, per-vendor setup story this project avoids, first with ncnn/Vulkan and now vision.cpp/Vulkan.
