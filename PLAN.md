# transparent — local GPU-accelerated background removal (and later, upscaling)

## Motivation

Existing local background-removal tools are bad: broken/no GPU acceleration on Linux (clearly an afterthought), don't fit the OS they're running on, and nag with subscription prompts. Web tools are worse. This project is a local-first, offline alternative — background removal now, upscaling and eventually video/GIF support later — that actually works well on Linux first, without the bloat or licensing games.

## Project intent

- Personal tool first, with a firm intent to open-source it once it works.
- Free. No subscription, no cloud calls, no telemetry.
- License: **your code under GPLv3** (see [LICENSE](LICENSE)).
  - **Originally planned as LGPLv3**, "to match Qt's own license." Revisited: LGPL exists specifically so *other people's proprietary software* can link against your code without inheriting your license — that's why Qt itself is LGPL, since Qt is a library meant to be embedded in other people's apps. `transparent` is an application, not a library anyone else is going to link into their own proprietary codebase, so LGPL's actual reason to exist doesn't apply here. GPLv3 gives the thing that was actually wanted instead: anyone who distributes a modified version has to share the source back, under the same terms. GPLv3 over GPLv2 specifically because it's the modern default (explicit patent grant) and nothing in this stack needs GPLv2 compatibility.
  - This is compatible with dynamically-linked LGPLv3 Qt and MIT-licensed vision.cpp/BiRefNet-lite without issue — GPL projects commonly dynamically link LGPL libraries.
  - Bundled third-party components keep their own licenses, made explicit in-repo and in an About screen:
    - Qt — LGPLv3, **dynamically linked** (avoids any static-linking obligations)
    - vision.cpp (and ggml) — MIT
    - BiRefNet-lite — MIT

## Platform rollout (sequential, not parallel)

1. **Linux — build this first, completely, before touching anything else.** Get the engine, UI, and packaging solid here.
2. **Windows — after Linux fully works.** Don't start Windows-specific work until the Linux MVP is done and validated on real hardware (AMD RX 9070 XT, RDNA4).
3. **macOS — last priority, dependent on getting help from a Mac-owning friend to test.** Not planned for near-term work.

## Architecture decisions

### Inference engine: vision.cpp + Vulkan backend (supersedes ncnn)

- **Originally ncnn** (Tencent, BSD-3-Clause), for the reasons below — this section is kept for context, not because ncnn is still the choice:
  - The one inference engine that gives genuine vendor-neutral GPU acceleration (NVIDIA/AMD/Intel) across OSes through a single Vulkan backend, without per-vendor branching (no separate CUDA/DirectML/ROCm/CoreML paths needed). Same approach Upscayl and chaiNNer rely on.
  - Tiny footprint, pure C++, trivial to statically link, plain C API for FFI.
  - ONNX Runtime was rejected as the primary engine: no mature Vulkan execution provider (long-open, unresolved GitHub issues) — still true as of this rewrite, not reopened.
  - **What actually broke this choice**: ncnn's PyTorch/ONNX conversion tooling (`pnnx`, `onnx2ncnn`) can't translate transformer ops (Swin backbone) or modulated deformable convolution (DCNv2) — both of which BiRefNet-lite needs. That's what forced the MVP to ship IS-Net (a lower-quality CNN stand-in) instead of the intended BiRefNet-lite. See git history for the full account, including the hand-written ncnn custom layer that was needed and later removed.
- **Now vision.cpp** (Acly, ggml-based, MIT): same vendor-neutral single-Vulkan-backend story as ncnn (no ADR reversal on that front), but its ggml core implements Swin-Transformer and a native `conv_2d_deform` op, so BiRefNet-lite runs without any conversion workarounds or hand-written custom layers. Vendored via CMake FetchContent (pinned to a tagged release), since no vcpkg port exists.
- **CPU fallback**: `visp::backend_init()` automatically falls back to ggml's CPU backend if Vulkan isn't available, same failure-avoidance goal as before.
- **No image tiling in MVP.** Assume typical photo-sized inputs fit in VRAM in a single pass. Add tiling (à la Upscayl) later, only if it turns out to be needed — not before. (vision.cpp's ESRGAN path already tiles for upscaling; BiRefNet inference here does not.)

### Background removal model: BiRefNet-lite

- MIT-licensed, current strong quality for salient object segmentation/matting.
- Other models considered:
  - RMBG-1.4/2.0 (BRIA) — rejected for now: CC BY-NC 4.0, non-commercial only, would block open-sourcing without a paid license.
  - MODNet — rejected: CC BY-NC-SA 4.0, non-commercial, and portrait-specific rather than general-purpose.
  - IS-Net/U2Net — viable fallback (Apache-2.0, smaller/faster) but visibly lower quality than BiRefNet; not the MVP choice.
  - BEN2-base — MIT, a close alternative to BiRefNet-lite, worth a look if BiRefNet-lite underperforms in practice.

### Language & UI: C++ + Qt Widgets

- C++ links directly against vision.cpp's native API — no FFI/bridging layer, smallest binary, fastest startup, no bundled runtime to ship.
- Qt Widgets (not QML/Qt Quick): simpler and lighter than a declarative/GPU-composited UI, and sufficient for a deliberately minimal interface. Qt also re-skins itself reasonably per-OS, which directly addresses the "doesn't fit in with anything" complaint about the current tools.
- **Fallback plan**: if the C++ build/engineering effort proves too fragile or slow going, fall back to Python + PySide6 (same Qt look-and-feel) at the cost of a bundled interpreter and larger install size — note vision.cpp doesn't have an official Python wheel the way ncnn did, so this fallback would need its own bindings story.
- C# considered and ruled out for this stack: no official bindings for ncnn or vision.cpp, and Qt isn't a natural fit in the C# ecosystem (would mean hand-written P/Invoke or dropping Qt for Avalonia).

### Internal architecture: pipeline-step abstraction from day one

- Even though MVP only does one operation (background removal), build it as a swappable "pipeline step" (image in → image out) rather than a hardcoded one-off path.
- This is what makes batch processing ("loop the step over files"), upscaling (v2, "add another step"), and video/GIF (later, "loop the step over frames") additive later instead of a rewrite.
- **One level down, the same idea again: `SegmentationModel`.** `BackgroundRemovalStep` (a pipeline step) doesn't talk to ncnn or vision.cpp directly — it calls a `SegmentationModel` interface (`isReady()` / `computeMask(QImage) -> QImage`) and knows nothing else about how the mask was produced. `NcnnSegmentationModel` was the first adapter; `VisionCppSegmentationModel` replaced it. This is why the ncnn → vision.cpp switch touched zero lines of `BackgroundRemovalStep` — the seam paid for itself immediately, not just hypothetically. Worth keeping this same shape when upscaling (Real-ESRGAN) is added: an `UpscaleModel`-style seam behind that pipeline step too, rather than binding it directly to vision.cpp's API.

## v1 (MVP) scope — Linux only

- Single image, drag-and-drop input.
- Background removal via BiRefNet-lite running on vision.cpp/Vulkan (CPU fallback if no Vulkan).
- Minimal UI: open image → see transparent result → export. No library, no history, no batch queue yet.
- Output: transparent PNG.
- Packaging: **AppImage** — portable, no install/root required, avoids the sandboxing/GPU-passthrough configuration overhead Flatpak would add. Distro-specific packages (deb/rpm) can follow later once the app is stable; not a Linux MVP requirement.

## Deferred (post-Linux-MVP, roughly in order)

1. Windows port (portable single .exe to start; installer only if a real need shows up — auto-update, Start Menu integration, etc.)
2. Batch / folder processing (drag a folder instead of a file, run the same pipeline step per image)
3. Upscaling — Real-ESRGAN via vision.cpp (MIT; GGUF weights already published at huggingface.co/Acly/Real-ESRGAN-GGUF), same runtime already in place, added as a second pipeline step
4. Video / GIF support (run the background-removal and/or upscaling step per frame)
5. Linux distro-native packages (deb/rpm) alongside the AppImage
6. macOS port, contingent on tester access
7. Model swappability (letting users pick BiRefNet-lite vs BEN2-base vs others) if BiRefNet-lite turns out not to be the right default for everyone

## Key prior art referenced

These predate the vision.cpp switch and describe *ncnn's* track record, not this project's current runtime — kept because the underlying point (single-Vulkan-backend beats per-vendor branching) is still exactly the argument for vision.cpp too:

- **Upscayl** — Electron + ncnn/Vulkan sidecar, proved the cross-vendor Vulkan-via-ncnn approach works in production, packaged as AppImage/deb/rpm/dmg/exe.
- **chaiNNer** — routes AMD/Intel through ncnn and NVIDIA through PyTorch/CUDA, independent confirmation that ncnn was a standard "vendor-neutral fallback" answer at the time.
- **rembg** — cautionary example: ONNX-Runtime-based, GPU support requires users to separately install matching CUDA/cuDNN or ROCm builds themselves — exactly the fragile, per-vendor setup story this project avoids, first by using ncnn/Vulkan and now vision.cpp/Vulkan.
