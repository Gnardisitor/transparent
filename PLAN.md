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
    want it anyway can bring their own weights via the custom-model import (below); this project
    never redistributes the weights.
  - MODNet: rejected, CC BY-NC-SA 4.0, non-commercial and portrait-specific.
  - IS-Net/U2Net: a viable fallback (Apache-2.0, smaller/faster) but visibly lower quality.
  - BEN2-base: MIT, a close alternative worth a look if BiRefNet-lite underperforms in practice.

### Model management (done)

Both `SegmentationModel` (BiRefNet-lite / BiRefNet-dynamic / BiRefNet full) and `UpscaleModel` (foolhardy_Remacri / NMKD-Superscale-SP) are swappable, using license-clean checkpoints published at the same huggingface.co/Acly account the build already downloads from.

Adjustable upscale *amount* (as opposed to model choice) was considered and rejected. Every compatible Real-ESRGAN checkpoint is fixed at 4x, and the only different-shaped file (`RealESRGAN-x4plus_anime-6B`) is a "plus" variant `esrgan_load_model` does not support.

### Custom models (done)

Goal: let the user add models that are deliberately not in the curated catalog — the motivating
case is RMBG-1.4, which cannot be pre-hosted due to its license. Designed to stay minimal:
the audience is advanced users doing manual setup once.

Facts the design rests on (verified in the fork and upstream):

- vision.cpp recognizes exactly six GGUF architectures: `birefnet`, `scunet`, `esrgan`, `migan`,
  `depthanything`, `mobile-sam`. Transparent's seams are single-architecture per category:
  Segmentation = birefnet only, Denoise = scunet only, Upscale = esrgan only.
- RMBG-1.4 is IS-Net/U2-Net-family — **not** loadable today, and Acly's ecosystem does not
  support it either (krita-vision-tools = SAM/BiRefNet/MI-GAN, pinning the same vision.cpp base
  commit this fork started from). Supporting it means a new architecture in the fork: converter
  support, arch code, parity tests — the SCUNet recipe, deferred as its own future project.
- A GGUF's `general.architecture` is readable cheaply through visp's `model_file` metadata, so
  imports/scans can validate architecture without loading weights.

Decisions:

- **Folder-as-state: the models directory is scanned for `.gguf` files.** Recognized
  architectures auto-register under their category (`birefnet` → Segmentation, `scunet` →
  Denoise, `esrgan` → Upscale). No import dialog, no naming step, **no persisted custom-model
  list** — the folder is the state, so files deleted behind the app's back simply disappear on
  the next scan (stale QSettings entries were the alternative; rejected).
- Display name = filename; custom rows show "user-provided (license not verified)" instead of a
  license, since nothing about an imported file can be verified.
- **One "Add model from disk…" button at the bottom of Settings**: file dialog → validate
  architecture → copy into the models directory → row appears in the right section (the
  architecture routes it; the button needs no category input). A convenience over
  hand-copying into AppData; the scan works without it.
- **Unrecognized `.gguf` files render as grayed rows** with a tooltip naming the architecture
  (e.g. "Unsupported architecture: rmbg") — answers "why doesn't my file show up?" without any
  extra flow. Covers arches vision.cpp knows but transparent has no seam for (`migan`,
  `depthanything`, `mobile-sam`) the same way.
- **Removal is file-manager deletion only** this round; no delete UI.
- **Licensing rule: transparent-models never hosts non-commercial weights.** License-restricted
  models (RMBG-1.4 etc.) enter only via user import; the project's repos and download flow stay
  permissively licensed (matching the GPLv3 + permissive-models position already in PLAN).

Implementation notes: the scan reads only GGUF headers via `gguf_init_from_file` with
metadata-only params (never tensor data), so it is cheap even with multi-GB files present;
Settings triggers a rescan on open (`showEvent`), at construction, and after any import;
`ModelManager::isInstalled`/`pathFor` are already filename-based, so selection persistence
(`models/<category>Model` keys) works for custom files unchanged. Conflict handling for the
Add button: if the chosen file's name collides with an existing model, refuse and ask the user
to rename the file. As built: `ModelManager::scanModels()`/`importModel()` +
`ModelCatalog::categoryForArchitecture`/`isRecognizedArchitecture`/`architectureForCategory`
(classification tests in test_model_manager/test_model_catalog); SettingsPage renders scanned
rows per category (no download button), an "Other files in the models folder" group for
unusable files, and re-applies the persisted active selection after each rescan so a custom
active model keeps its radio check.

Design:

- Curated registry, not free-form file browsing. The selection screen knows a fixed list per category (name, filename, license, size, download URL, SHA256), scans the models directory, and marks each entry Installed or Not Installed by filename match. Browsing for an arbitrary `.gguf` is deferred, since vision.cpp's loaders are architecture-specific and would fail on an incompatible file anyway.
- On-demand download for everything beyond the defaults. Each entry has its own download button, runs off the GUI thread, and is verified against its known SHA256 before being marked Installed. A mismatch deletes the file and shows an error. Manually placing a file in the models directory works the same way.
- Models live in `QStandardPaths::AppDataLocation`, off the build tree. The build-tree path is wiped by clean rebuilds and is no place for manual placement.
- First run still needs no network access. The three defaults (BiRefNet-lite, Remacri, and SCUNet real GAN) are fetched at CMake configure time, and the app copies them into AppData on first launch if missing.
- Selecting a model applies live, no restart. The steps take their model through a swappable `shared_ptr`, and loading a new model runs asynchronously with a spinner using the same `QtConcurrent`/`QFutureWatcher` pattern `MainWindow` already uses.
- It lives in a Settings dialog opened from a menu-bar action next to Help, not in the Simple/Advanced mode selector. Model choice and bokeh strength are set-occasionally-then-forget preferences, and nesting them in Advanced mode would lock Simple-mode users out of picking BiRefNet-dynamic for a large photo.
- Persisted via `QSettings`.

### Denoising (SCUNet)

Decided in full in a grilling session; recorded here so it survives across sessions. Nothing is committed or pushed until explicitly asked.

**Model: SCUNet (Swin-Conv-UNet), checkpoints `scunet_color_real_gan` and `scunet_color_real_psnr`, both Apache-2.0.** Blind real-world color denoising (trained on SIDD), ~18M params, 67 GFLOPs. GAN variant is the default (sharper; PSNR is smoother). The alternatives were checked and rejected: SwinIR denoise (non-blind, fixed-σ checkpoints, ~12× the FLOPs), NAFNet (weak color real-denoise coverage), Restormer (heavy, no permissive GGUF ecosystem), realesr-general-x4v3 (denoise+SR combo; visp's `esrgan` arch is RRDB-only and it's an upsampler, not a standalone denoiser). The fixed-σ gaussian SCUNet variants are skipped: not blind, niche. SCUNet's Swin v1 blocks can build on the backbone visp already implements for BiRefNet.

**Fork strategy: a private fork of vision.cpp, not upstream PRs.** vision.cpp v0.3.1 has no denoising architecture (families: sam, birefnet, depth_anything, migan, esrgan), and upstream `main` is only ~8 build/packaging commits past it — no denoising either. visp's *public* headers don't expose the conv/attention/UNet primitives, so implementing SCUNet inside `transparent` would mean reimplementing visp internals against raw ggml. The fork is the clean route, and no upstream PR is wanted by design: the fork exists for this project's needs and its maintenance burden stays here, not in a diverging patch series against Acly's repo.

Fork specifics (implemented in the `vision.cpp` working copy):

- **Working copy: `~/Documents/Dev/vision.cpp/`**, based on upstream `main` (`26a7529`) exactly as checked out — the extra commits are build hardening (static ggml option, SO version, symbol hiding) with an identical dependency graph (both v0.3.1 and main depend on Acly's llama.cpp submodule for ggml; only its `ggml/` subdirectory is built).
- **Cleanup: delete `.github/workflows/` only** (`ci.yml`, `pkg-check.yml` — they can't run on this infra). Keep cli (needed for debugging against reference outputs), tests (parity testing lives there), docs, and bindings; a minimal diff is the point of forking without PRs.
- **`VISP_STATIC_DEFINE` support is folded directly into the fork** (main still lacks it), and `transparent` drops its `cmake/patch-visioncpp-static.cmake` + `PATCH_COMMAND`.
- **SCUNet implementation follows `docs/model-implementation-guide.md`:** arch in `src/visp/arch/scunet.*`, loader + API + CLI entry (`vision-cli scunet`), converter support added to `scripts/convert.py` itself (reuses its Writer; no separate script), per-layer cosine parity against the PyTorch reference via the existing workbench harness (`tests/test_scunet.py`, 10 cases). Skipping parity testing is how bugs hide in ggml ports.
- **Implementation notes worth keeping:** the fork also fixes a latent `swin.h` mismatch (`window_reverse` declared with `int`, defined with `int64_t`); `scunet_compute` transfers weights to CWHN unconditionally and runs the whole graph CWHN, like BiRefNet's encoder; attention masks are only precomputed for stages that actually contain a second ("SW") block; `scunet_detect_params` accepts any `dim` divisible by `2*head_dim` (lets tiny test configs through) while rejecting garbage.
- **Verification:** all 10 `tests/test_scunet.py` parity cases pass; `vision-cli scunet` output for the real `scunet_color_real_gan` checkpoint matches the PyTorch reference on a 256×256 image to ≤1 LSB per channel on both CPU and the RX 9070 XT (Vulkan, ~156ms). Two failures pre-existing on the pristine fork (mobile-sam `test_predict_masks`, plus Vulkan-gated primitive tests before the Vulkan build was enabled) are unrelated to SCUNet.
- **ggml update policy:** the submodule pins Acly's vision patch series (`vision-20260331`) — conv2d_deform, cwhn im2col, f16 repeat — which upstream llama.cpp lacks, so updating means rebasing that series, not fast-forwarding. Don't bump unprompted: upstream gained only LLM-side ops since March 2026 (the one interesting perf item is `vulkan: tiled transpose for 0<->2 permuted CONT`, relevant to visp's CWHN permutes). Bump when Acly publishes a newer `vision-*` tag (then: submodule bump + re-run the parity suite), or if profiling shows ggml-bound bottlenecks. The CPU `conv_transpose_2d_p0` batch>1 inaccuracy exists in current upstream too; it can't affect the app (batch is always 1).
- No GGUF weights for any denoising model exist that C++ code can consume (`cstr/scunet-GGUF` on HF is weights-only with no public consumer; useful as a provenance cross-check at most). We convert the upstream cszn `.pth` checkpoints to **F16 GGUF** ourselves; the raw `.pth` checkpoints live in `vision.cpp/models/scunet/`.

**Hosting: `~/Documents/Dev/transparent-models/`** holds `scunet-color-real-gan-F16.gguf` and `scunet-color-real-psnr-F16.gguf` (F16, ~36MB each; SHA256s are recorded in `ModelCatalog.cpp`). This folder becomes the Forgejo LFS repo `forge.db-serve.com/dbajan/transparent-models` — one shared repo for all future models (colorization etc.). `transparent`'s `FetchContent_Declare` points at `https://forge.db-serve.com/dbajan/vision.cpp.git` tracking its `main` branch (tag-pinning deliberately deferred; when a fork tag is introduced, set `GIT_TAG` to it for reproducible CI builds).

**App integration (implemented): a standalone `DenoiseStep`**, mirroring `UpscaleStep`:

- New `ModelCategory::Denoise`, `DenoiseModel` seam, and `VisionCppDenoiseModel` adapter. RGB-only denoise that **preserves an existing alpha channel** (so it composes after BackgroundRemoval in Advanced mode).
- Pipeline order in `main.cpp`: BackgroundRemoval → Bokeh → **Denoise** → Upscale (bokeh directly after background removal keeps its mask-based blur at source resolution; denoise-then-upscale because upscaling amplifies noise). Advanced mode gets it via the reorderable step list automatically; Simple mode gets it in the dropdown; the Simple-mode default stays Background Removal; nothing is auto-enabled. Consequence of the order: bokeh's strength-slider live preview applies only when Bokeh is the last active step (Simple mode, or an Advanced order ending in Bokeh).
- `ModelCatalog`: two entries (GAN default, PSNR), forge `/media/branch/main/` raw-LFS URLs. The GAN model is also a CMake-time default (auto-provisioned like BiRefNet/Remacri, see "Model management"); the PSNR variant stays download-on-demand.
- **Large images: full-res inference up to 2.25MP (~1500×1500); above that, 512px tiles with 32px overlap**, feather-blended inside visp's `scunet_compute` (mirroring `esrgan_compute`; visp's own CLI tiles ESRGAN at 224/16 for the same VRAM reason, and the 64px-aligned tile sizes give edge tiles replicate padding for free, which SCUNet's /64 requirement needs anyway). Constants are revisited after testing on the RX 9070 XT — the real constraint is what a 12MP photo does to VRAM.

#### Denoise performance (profiled 2026-09-01, RX 9070 XT / Vulkan)

Measurements at 512x512 input, GPU: full model ~464ms; conv-only skeleton (all
attention blocks removed) ~50ms; two stage-1 attention blocks add ~62ms (~31ms per
block); two body-stage blocks are negligible (~45ms total). Conclusion: **window
attention is ~89% of runtime**, and the full-resolution stage-1 attention (8 blocks:
m_down1 + m_up1) is more than half of it by itself. Overall utilization is far below
hardware capability (~1-2% of the 9070 XT's fp16 throughput), so the headroom is real
but locked inside ggml's Vulkan flash-attention path (n_heads=1, head_dim=32, 64-token
windows, masked SW variants).

Done during this pass (keep):
- Converter robustness: config inference tolerates groups with zero blocks and
  checkpoints without any ConvTransBlock; `scunet_compute` skips attention constants
  when no attention blocks exist (unused constants can't get backend buffers).
- (A `ggml_conv_2d_cwhn` patch to the vendored ggml made during this pass was reverted
  in the second pass — see below; the submodule is back to Acly's `vision-20260331`
  commit, unmodified.)

Second pass (done, profile-driven):

The "attention is ~89% of runtime" conclusion above was wrong — the skeleton-vs-blocks
estimate was misleading. A real per-op GPU profile (`GGML_VK_PERF_LOGGER=1`, supported by
the vendored ggml) at 512x512 on the 464ms baseline showed: IM2COL 200ms, ADD 81ms,
CONT 44ms, NORM 31ms, all matmuls ~40ms, flash attention ~5ms. The dominant costs were
memory-bound elementwise ops and the conv im2col expansion, not attention math.

1. **Conv im2col elimination (the big win, 442 -> 289ms).** The copy audit found that
   *all* 61 convs ran the `ggml_conv_2d` im2col+mul_mat fallback: visp's CWHN
   presentation is `[C,W,H,N]`, which never satisfies ggml's channels-contiguous check
   (that wants `[W,H,C,N]` with `nb[2]==type_size`), so the previous session's
   `ggml_conv_2d_cwhn` branch was dead code on Vulkan (its "~2-4%" claim came
   from measurement noise). Worse, `ggml_conv_2d_cwhn` can never dispatch on Vulkan at
   all: `ggml_vk_conv_2d` asserts the input's innermost dim is densely packed
   (`nb10 == sizeof(float)`), which contradicts channels-dense memory for C>1. The fix
   in `nn.cpp conv_2d` keeps visp's `[C,W,H,N]` presentation and routes the
   contiguous-input case through one `cont(permute)` copy per side into
   `ggml_conv_2d_direct` (W-dense, so the direct Vulkan conv2d shader applies, no
   im2col tensor). Zero shader changes; CPU keeps its `conv_2d_direct_cwhn` branch.
   The dead branch and the vendored-ggml patch it needed were removed afterwards, so
   `depend/llama` is unmodified again; results identical (benchmarks and the ≤1 LSB
   output check re-verified after the revert).
2. **LayerNorm affine folding (289 -> 268ms).** `scunet::swin_block` no longer emits
   LN mul/add: `window_attention` and `mlp` take the pre-norm tokens, run only
   `ggml_norm` (statistics), and fold γ into the following linear's weight and
   β·W+b into its bias (`linear_folded_ln`, computed in f32 from the f16 weights at
   graph-build time, a few KB). The norm commutes with roll and window partition.
   Also: the attention-mask `ggml_repeat` for n_heads==1 is skipped (it was a
   same-shape full-tensor copy); `split_qkv` was split into `split_qkv` +
   `split_qkv_proj` so the folded path can reuse the QKV reshape logic.
3. **Stage-attention micro-benchmark (flash vs default).** Standalone harness timed
   `scunet::window_attention` at all four real stage shapes (dim 32/64/128/256,
   n_heads 1/2/4/8, 512px tile): non-flash was 10-22% faster at n_heads==1, a wash
   elsewhere; end-to-end flash on/off was within run-to-run noise after the conv fix
   (289 vs 294ms). Conclusion: no size-conditional flag; flash stays at its backend
   default. Attention is now ~5ms flash + ~40ms matmul per tile and no longer worth
   special-casing.
4. **F16 activations: investigated, blocked, not pursued.** Vulkan has `pipeline_norm_f32`
   only (no f16 norm), and the conv2d shader is f32-in/f32-out only; the residual
   elementwise costs (ADD ~75ms, CONT ~42ms, NORM ~32ms after the fixes) are all
   generic ggml-Vulkan shader throughput (a [32,512,512] add runs at ~0.27 TB/s on the
   9070 XT). Making these f16 means writing new backend shaders and a compute-dtype
   seam through `scunet_generate` — foundation work, explicitly not worth it here.
   Same for eliminating the two permute-copies per conv (needs a channels-dense
   conv2d shader variant): accepted as-is.

Re-benchmark (final, RX 9070 XT / Vulkan, scunet-color-real-gan): 512x512 442 ->
~268ms (1.65x); 1024x1024 full-res 916ms; 12MP tiled (4032x3024, 512px tiles) ~9.8s
(was ~22s, 2.2x). CPU fallback unaffected (512x512 ~1.7s). Correctness: parity suite
10/10 on CPU, C++ ctest green, optimized GPU/CPU output within 1 LSB per channel of
the pre-optimization output (which was itself ~0.5/255 vs the PyTorch reference).
All changes are op-graph level and hardware-neutral (no shader edits, no
vendor/hardware conditionals). Two suite failures are pre-existing and unrelated:
mobile-sam `test_predict_masks` (documented above) and `test_birefnet
::test_swin_transformer`, which is tolerance-flaky against its unseeded random inputs
(seeded runs show the numerics are bit-identical to the pristine fork; ~30% of seeds
exceed its atol=0.002 regardless of these changes).

Uncommitted at the time of writing (user commits/pushes): in the vision.cpp fork the
previous session's converter + compute fixes (convert.py, vision.cpp) plus this
session's conv-direct fix, LN folding, repeat skip and split_qkv refactor (nn.cpp,
nn.h, scunet.cpp, scunet.h); `depend/llama` is clean at Acly's `vision-20260331`
commit, no local patches. In transparent the step reorder (BackgroundRemoval -> Bokeh
-> Denoise -> Upscale) plus this PLAN section.

### Language and UI (C++ and Qt Widgets)

- C++ links directly against vision.cpp's native API. No FFI layer, smallest binary, fastest startup.
- Qt Widgets, not QML/Qt Quick. Simpler and lighter, sufficient for a deliberately minimal interface.

### Preview zoom & before/after compare (done)

Goal: inspect results at pixel level and see what the pipeline actually changed. Implemented
as designed: `PreviewCanvas` (`src/ui/PreviewCanvas.h/.cpp`) is the custom canvas widget that
absorbed `updatePreview()`'s checkerboard and HiDPI logic, with the wipe and view state on
top; `MainWindow` wires it to the pipeline results and hosts the compare controls. Geometry
and wipe rendering are covered by `tests/test_preview_canvas.cpp` (14 cases, including
grab-based render checks); `test_main_window.cpp` adds an integration test for the
load-image wiring. No new dependencies, no pipeline core changes; GIF compare remains out of
scope (below).

Decisions, recorded so they survive across sessions:

- **"Before" = the original image as loaded; "after" = the final pipeline result.**
  Intermediate per-step results are not captured anywhere and capturing them would mean
  touching the pipeline core; not wanted. Stills only — see the GIF decision below.
- **Wipe slider is the compare presentation, on by default.** A draggable vertical split line
  over one shared canvas: left of the line shows before, right shows after. A button under the
  existing ✕ (top-right overlay) toggles it off (after-only, i.e. today's view) and back on.
  The enabled state persists in QSettings like the bokeh strength; first-ever-launch default
  is enabled. There is deliberately no A/B hold-key flip — one compare mechanism, done well.
  `W` toggles the wipe via keyboard, matching the `F`/`1`/`+`/`-` scheme.
- **One shared view state.** Pan/zoom applies identically to both images (same dimensions for
  stills), so before and after stay perfectly aligned at any zoom — this is what makes
  differences visible.
- **Zoom interactions** (near-universal conventions, all cheap): cursor-anchored wheel zoom
  (what's under the mouse stays under the mouse), left-drag pan, double-click toggles
  fit ↔ 100%, `+`/`-` zoom in/out, `F` fit, `1` = 100%. A small semi-transparent overlay strip
  at the top-left of the preview (mirroring the ✕ at top-right) shows the zoom percentage with
  fit and 100% buttons.
- **Rendering: smooth while shrinking to fit, nearest-neighbor at ≥200% magnification.**
  Smooth upscaling blurs — which hides exactly the pixel differences zoom exists to show.
  Max zoom 16× (past visual resolution on any display; bounds repaint cost).
- **Stability across re-renders.** `resultImage_` is rewritten by bokeh slider ticks, model
  swaps, reprocess runs, and Simple/Advanced mode switches; zoom, pan, divider position, and
  wipe enabled state must persist through all of them (a reset per bokeh tick would make the
  live preview unusable). A **new image load** resets everything: fit, wipe on, divider at 50%.
- **Divider affordance:** clean vertical line with a small grip and a hover cursor change (⇔);
  wide grab area; dragging it takes priority over pan-drag. It spans the image area only, not
  the letterbox, and carries no "Before"/"After" text labels (self-explanatory, and unreadable
  over transparent regions).
- **Animated GIFs: compare disabled.** Full-resolution per-frame data is streamed and
  discarded (only downscaled preview frames survive), so frame-synced before/after would need
  new plumbing; zoom still works on preview frames. The wipe toggle button renders disabled
  with a tooltip ("Compare is not available for animations") — hidden controls would flicker
  as files are swapped.
- **Batch runs show no compare UI.**
- **While a still is processing** (result not ready yet), both wipe halves show the source
  with the spinner on top as today; the wipe becomes meaningful when the result arrives.
- **Implementation notes:** the canvas becomes a custom QWidget whose `paintEvent` absorbs
  `updatePreview()`'s checkerboard + DPR logic; the existing overlay reposition mechanism
  (✕, spinner) extends to the new strip and divider. Export, batch runner, and GIF encoding
  are untouched.

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
