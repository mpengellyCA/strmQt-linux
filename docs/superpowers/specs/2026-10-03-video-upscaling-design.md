# Video upscaling — design

Status: draft for review, 2026-10-03. Sub-project A of the video-processing work.

## 1. Intent

**What the user asked for:** FSR1 and other upscalers for content below the native
resolution of the monitor or video window, optimised for quality. It should live in
the player, next to the transcoding settings, and take effect immediately.

**Agreed during brainstorming:**

- **Audience:** everyone who installs StrmQt. Quality first on strong GPUs, stepping
  down automatically on laptops and integrated graphics, so playback never stutters.
- **Content:** libraries mix live action and animation. Both get upscalers chosen for
  them, picked per item from Emby's genre tags.
- **Control:** automatic by default. A picker in the OSD's Quality tab overrides it
  for the current video, and the change shows on the frame at once. The override
  resets when the next item starts.
- **Engine:** mpv only, through mpv user shaders (approach 1). VLC shows the feature
  as unavailable; a VLC path can be added later behind the same engine calls.

**Out of scope here:**
- **B. viewing-condition looks** (bright room, dark room, sunset);
- **C. Cat Vision / Dog Vision**;
- **VLC support.**

B and C are separate sub-projects with their own specs, and build on the engine calls
defined here.

**Success looks like:**
1. A 480p, 720p or 1080p film on a 1440p or 4K screen is upscaled by the best chain the
   GPU sustains, with no dropped frames caused by the upscaler.
2. An anime episode gets an animation upscaler without the user doing anything.
3. Changing the picker mid-film changes the picture within a frame or two.
4. A weak GPU steps down once, then remembers, so later films start smooth.

## 2. Findings from the feasibility spike

The spike was a throwaway libmpv probe using StrmQt's render path: the OpenGL render API,
with Qt's default surface format. It ran on an RX 9070 XT (radeonsi, Mesa 26.2.3,
libmpv 2.5.0 / mpv 0.41) and on Mesa's software renderer, offscreen. Nothing from it is
kept. What it found:

1. **Renderer.** The OpenGL render API runs mpv's older `vo_gpu` renderer
   (`video/out/opengl/libmpv_gl.c`), not `gpu-next`. The public render API offers only
   OpenGL and software backends, so `gpu-next` is unreachable from an embedded player
   today.
2. **ArtCNN does not work on this path.** On `vo_gpu` it fails to compile: it needs the
   `GL_EXT_shader_explicit_arithmetic_types_float16` extension, and it uses a LUMA hook
   writing 4 components where the maximum is 1. It is excluded until libmpv exposes
   `gpu-next`. Anime4K's CNN upscalers take its place for animation.
3. **mpv's own pass timings are unavailable.** Reading `vo-passes` returns
   "property unavailable" on the render API, even mid-playback. The governor therefore
   cannot use mpv's numbers.
4. **StrmQt can time mpv itself, but only with timestamp queries.** Wrapping
   `mpv_render_context_render()` in a `GL_TIME_ELAPSED` query breaks rendering. mpv
   already uses that query type internally, nesting is illegal, and every frame logged
   `after creating texture: OpenGL error INVALID_OPERATION` while the shaders silently
   did not run. A pair of `glQueryCounter(GL_TIMESTAMP)` calls around the render call
   works and gives believable numbers (table below).
5. **Target-time blocking spoils the measurement.** By default the render call blocks
   until the frame's display time (`MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME`), and the
   timestamp gap then includes that wait (about 40 ms at 24 fps). StrmQt uses the
   default today. mpv's header warns that turning it off "without doing anything in
   addition" knocks A/V sync slightly off, so StrmQt must do that wait itself (§4.4).
6. **HDR is fine.** On a 10-bit PQ BT.2020 clip, every working shader kept the frame's
   mean colour within 0.2 of 255 levels of the unshaded frame. The peak differences are
   edge sharpening, as expected. No shader needs to be marked SDR-only today; the
   mechanism stays, for future additions.
7. **The CI compile test is viable.** Mesa's software renderer, run offscreen with
   `QT_QPA_PLATFORM=offscreen`, compiled FSR1, NIS, FSRCNNX 16 and Anime4K CNN VL with
   no errors. It reported ArtCNN's 7 compile failures, so it catches the defect class
   it exists for.

**Measured GPU cost per frame**, on the RX 9070 XT at 2560×1440 output (average ms;
from a 720p source unless noted):

| Chain | SDR 360p | SDR 720p | HDR 720p |
|---|---|---|---|
| mpv default scaler | 0.31 | — | — |
| `ewa_lanczossharp` | 0.73 | 1.01 | 1.26 |
| FSR1 (EASU + RCAS) | 0.51 | 0.78 | 1.13 |
| NIS | 0.60 | 0.75 | 1.08 |
| FSRCNNX 8 | 0.86 | 1.51 | 1.95 |
| FSRCNNX 16 | 1.47 | 3.33 | 3.86 |
| Anime4K CNN x2 M | 0.66 | 0.92 | 1.30 |
| Anime4K CNN x2 VL | 1.27 | 2.18 | 2.59 |

Even the heaviest chain uses under 20% of a 24 fps frame budget on this GPU. The governor
exists for laptops and integrated graphics, which these numbers do not represent.

## 3. Shader set and tiers

**Bundled shaders**, installed to `share/strmqt/shaders/`. mpv takes file paths, so they
are files on disk, not Qt resources. Each shader's licence text is installed next to it.

| Shader | Source | Licence |
|---|---|---|
| `FSR.glsl` (AMD FidelityFX FSR1: EASU + RCAS) | agyild's mpv port | MIT |
| `NVScaler.glsl` (NVIDIA Image Scaling) | agyild's mpv port | MIT |
| `FSRCNNX_x2_16-0-4-1.glsl`, `FSRCNNX_x2_8-0-4-1.glsl` | igv/FSRCNN-TensorFlow release 1.1 | LGPL-3.0-or-later |
| `Anime4K_Upscale_CNN_x2_VL.glsl`, `Anime4K_Upscale_CNN_x2_M.glsl` | bloc97/Anime4K v4.0.1 | MIT |

All of these are compatible with StrmQt's GPL-3.0-or-later.
- Each file is vendored at a recorded upstream revision, with a SHA-256 in
  `shaders/SOURCES`. This follows the repo's rule that dependencies are pinned, with the
  reason given in the commit that adds them.
- The Debian `copyright` file and the `.rpm`'s `%license` list gain matching entries.

**Ladders.** Auto walks a ladder from its current ceiling downwards. The governor lowers
the ceiling one rung at a time.

| Rung | Live action | Animation |
|---|---|---|
| 4 | FSRCNNX 16 | Anime4K CNN x2 VL |
| 3 | FSRCNNX 8 | Anime4K CNN x2 M |
| 2 | FSR1 | FSR1 |
| 1 | `ewa_lanczossharp` (no shader) | `ewa_lanczossharp` |
| 0 | mpv's default scaler (off) | the same |

> **Amendment 2026-10-04.** On SD sources (display height under 720 lines), live-action
> rung 4 is ArtCNN C4F16 DS, vendored with a `vo_gpu` compatibility patch
> (`shaders/ArtCNN_C4F16_DS.vo_gpu.patch`). HD and unknown-size sources keep FSRCNNX 16;
> rung 3 and below are unchanged, and the picker offers ArtCNN DS for any source. A
> broken ArtCNN DS steps down to rung 3, like any other rung. `MpvPlayer` also reports
> user shaders that mpv fails to parse (e.g. `Unrecognized command '…'!`), not only
> compile failures.

**Neural rungs.**
- Rungs 4 and 3 are 2× doublers. They are only chosen when the scale factor is at
  least 1.5.
- Below 1.5×, Auto starts at rung 2. A doubler at 1080p → 1200p costs GPU time for no
  visible gain.
- When a neural rung runs, `scale` and `cscale` are set to `ewa_lanczossharp`, which
  covers the remaining factor (for example 360p → 1440p is 2× neural, then 2× EWA)
  and the chroma.

**Manual-only.** NIS appears in the picker for both content types but is never chosen
by Auto. FSR1 already fills that rung, and comparing the two side by side is what an
override is for.

**When upscaling applies.** A chain runs only when the source is smaller than the
output in either dimension, after aspect fitting. At or above output size, the policy
returns rung 0, and mpv's downscaler is left alone.

## 4. Architecture

### 4.1 Engine calls (`src/playback/PlayerBackend.h`)

Optional calls with harmless defaults, as the interface already does for speed, delays
and subtitle style:

```cpp
virtual bool supportsVideoShaders() const { return false; }
virtual void setVideoProcessing(const VideoProcessing &chain) { Q_UNUSED(chain); }
```

`VideoProcessing` is a small value type in `src/playback/upscale/`:
- the shader file paths, in order;
- the `scale` and `cscale` filter names, empty meaning mpv's default.

B and C will extend it later. One call carries the whole chain, so a change is applied
atomically.

GPU timing reaches the controller through a signal on the video item (§4.4), not through
`PlayerBackend`. The timing is a property of the render thread, not of the engine's
control interface.

**`MpvPlayer`:**
- `supportsVideoShaders()` returns true.
- `setVideoProcessing()` sets the `glsl-shaders` list, `scale` and `cscale` as runtime
  properties. These apply on the next rendered frame, with no reload, which is what
  makes the picker immediate.

**`VlcPlayer`** keeps the defaults.

### 4.2 Decision logic (`src/playback/upscale/`, no QtGui)

- **`UpscaleCatalog`** — the table in §3 as data. Each entry has:
  - id, display name, content type (live action, animation or either);
  - rung, or manual-only;
  - whether it is a 2× doubler;
  - shader file names and licence file;
  - an SDR-only flag (all false today).

  At startup it resolves file names against the installed shader directory. An entry
  whose files are missing is dropped, with one warning naming the files.
- **`UpscalePolicy`** — a pure function:

  ```cpp
  struct UpscaleInput {
      QSize source, output;     // output = video rect in device pixels
      ContentType content;      // LiveAction, Animation, Unknown → LiveAction ladder
      int ceiling;              // 0..4, from the governor
      bool hdr;
      bool softwareRenderer;
      QString override;         // "" = Auto, "off", or a catalog id
  };
  struct UpscaleDecision { VideoProcessing chain; QString activeId; QString reason; };
  ```

  It applies §3:
  - software renderer → off;
  - an override, when set → that chain, validated against the catalog;
  - otherwise, no upscale needed → rung 0;
  - otherwise, the ladder for the content type, capped by the ceiling, with the 1.5×
    doubler rule applied and SDR-only entries skipped for HDR.

  `reason` is the human-readable status line.
- **`UpscaleGovernor`** — consumes GPU-time summaries and dropped-frame counts, and owns
  the ceiling. Rules:
  - **Budget:** 50% of the video frame interval (about 20.8 ms at 24 fps, 8.3 ms at
    60 fps). This leaves headroom for Qt's scene graph and the OSD.
  - **Over budget:** the 95th-percentile GPU time over budget for 3 s in a row lowers
    the ceiling by one rung.
  - **Dropped frames:** a rise in mpv's `frame-drop-count` or `vo-delayed-frame-count`
    while a shader chain runs lowers it at once. A grace period of 2 s after a load,
    seek or chain change ignores the transient.
  - **Never up mid-video.** The ceiling never rises during a video. Each new video
    starts from the saved ceiling.
  - **Saved per GPU.** The ceiling is saved in `Settings`, keyed by the `GL_RENDERER`
    string. An unseen GPU starts at rung 4 (quality first).
  - **Re-test.** `resetCeiling()` backs Settings → Playback → "Re-test upscaling on
    this GPU".
  - **Overrides.** Under a manual override, the governor still records timings for the
    stats overlay, but does not change the chain. The user asked for that chain.

### 4.3 Wiring (`src/app/controllers/PlayerController`)

- **Owns** the catalog, the governor and the current override.
- **Re-runs the policy on:**
  - load and video reconfigure (source size, HDR);
  - a video-rect change (window resize, fullscreen), debounced to 250 ms;
  - an override change;
  - content type becoming known;
  - a governor ceiling change.

  It pushes the result with `setVideoProcessing()` only when the chain differs.
- **Content type** comes from the `itemDetails()` reply already fetched at playback
  start (`fetchChapters`). If any genre is "Anime" or "Animation", case-insensitive,
  the item is animation. If an episode's own genres are empty, one extra
  `itemDetails(seriesId)` request reads the series' genres. Until a reply arrives, the
  live-action ladder applies; the update is applied when it does.
- **Exposed to QML:**
  - `upscaleMode` (read/write: "" for Auto, "off", or a catalog id);
  - `upscaleChoices` (a list of { id, name } for this item's content type);
  - `upscaleActive`, `upscaleReason`;
  - `upscaleAvailable`, false on VLC or with a software renderer;
  - `upscaleGpuMs`.
- **The override resets** at each new item.

### 4.4 GPU timing (`src/playback/mpv/MpvVideoItem.cpp`, render thread)

**Per render call:**
1. Read `MPV_RENDER_PARAM_NEXT_FRAME_INFO` for the target time.
2. Issue `glQueryCounter(GL_TIMESTAMP)`.
3. Render with `MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME = 0`.
4. Issue the second timestamp.
5. Wait until the target time on the render thread. This is the same wait mpv performed
   internally, so A/V sync and pacing are unchanged (spike finding 5).

**Reading the results:**
- Query pairs come from a small ring (4 pairs), and are read back when
  `GL_QUERY_RESULT_AVAILABLE` says so. This never stalls the pipeline.
- Twice a second, the item emits a summary to the GUI thread (p95 and mean GPU ms,
  sample count) through a queued signal.

**When timing is unavailable** (no `GL_ARB_timer_query`, or GLES without
`EXT_disjoint_timer_query`):
- The summary reports "unavailable".
- The governor falls back to dropped-frame signals alone.
- Upscaling still works.

**Never use `GL_TIME_ELAPSED`** here (spike finding 4). A comment at the call site says
why.

### 4.5 UI (`src/ui/player`)

- **`PlaybackSettingsPanel`, Quality tab:**
  - a new Upscaling row, below the existing mode and bitrate rows;
  - its choices: Auto, Off, then `upscaleChoices`;
  - the status line under it shows `upscaleReason`, for example "FSRCNNX 16 ·
    720p → 1440p" or "Anime4K CNN VL · animation · 480p → 2160p";
  - when `upscaleAvailable` is false, the row is disabled and reads "Not available with
    VLC" or "Needs a GPU".

  Unlike the bitrate and mode rows, this one applies at once. The tab's existing
  "applies to the next stream" strip names only those two rows, and that stays true.
- **`StatsOverlay`:**
  - the active chain;
  - GPU ms per frame against the budget;
  - the ceiling, when it is below 4.
- **Settings → Playback:** the "Re-test upscaling on this GPU" button.
- **No new QML logic** beyond binding to controller properties (AGENTS.md: no business
  logic in QML).

## 5. Failure handling

| Situation | Behaviour |
|---|---|
| Software renderer (`llvmpipe`, `softpipe`, `SWR` in `GL_RENDERER`) | Policy returns off. The row reads "Needs a GPU". |
| A shader fails to compile | mpv logs `compile log (status=0)` or `Hook tried to …` at error level, and `MpvPlayer` forwards those as a `videoShaderFailed(path)` signal. The controller marks that catalog entry broken for the session and re-runs the policy (the ladder skips it). The status line says "FSRCNNX 16 failed to load — using FSRCNNX 8". |
| Missing shader files | The catalog drops the entry at startup, with one warning. The picker never offers it. |
| Over budget or dropped frames | The governor lowers the ceiling by one rung (§4.2). The status line shows "stepped down for smooth playback". |
| VLC engine | `supportsVideoShaders()` is false, so the row is disabled with a reason. Nothing is pushed. |
| Timer queries unsupported | The governor uses dropped frames only (§4.4). |
| HDR | No special case today; SDR-only entries are skipped (none exist). |

## 6. Testing

**Unit tests** (`tests/unit`; Qt Test; no GPU, no network):

- **`tst_upscale_policy`** — data-driven: source × output × content × ceiling × HDR ×
  software × override, checked against the expected chain, active id and reason. Rows
  include:
  - downscale and equal size → off;
  - 1.1× → rung 2, no doubler;
  - 2× live action → FSRCNNX 16;
  - 2× animation → Anime4K VL;
  - ceiling 3 → FSRCNNX 8;
  - ceiling 1 → EWA;
  - each override, including an unknown id → Auto;
  - software → off;
  - HDR with a fake SDR-only entry → skipped;
  - a broken entry → skipped.
- **`tst_upscale_catalog`** — against the source tree's `shaders/` directory:
  - every entry's files exist and have their licence file;
  - `SOURCES` lists every shader with a SHA-256 that matches the file;
  - both ladders have an entry at every rung 1–4;
  - missing files drop the entry.
- **`tst_upscale_governor`** — fed synthetic summaries and drop counts, with an injected
  clock:
  - 3 s over budget → exactly one step down;
  - 2.9 s over budget → none;
  - a drop outside the grace period → immediate step;
  - a drop inside it → ignored;
  - never up mid-video;
  - the ceiling persists per renderer string;
  - `resetCeiling()` clears it;
  - no stepping under a manual override.
- **`tst_player_controller` additions** — `FakePlayerBackend` records
  `setVideoProcessing()` calls:
  - load, video-rect change (after the debounce) and override each push a chain;
  - an identical decision pushes nothing;
  - the override resets on the next item;
  - a backend without shader support → `upscaleAvailable` false and no calls;
  - genres "Anime" → animation;
  - an episode without genres triggers the series request and switches ladder on reply;
  - `videoShaderFailed` → entry skipped and chain re-pushed.

**Integration test** — `tst_mpv_shader_compile` (`tests/integration`):
- **Setup:** a real libmpv render context on a `QOffscreenSurface` with Qt's default
  format.
- **Per shader chain:** render an ffmpeg-generated 360p clip for about 1 s at 1280×720,
  and fail if mpv logs a compile or hook error.
- **It also checks:**
  - a timestamp-query pair returns a non-zero GPU time;
  - mpv logs no `INVALID_OPERATION`, which guards §4.4.
- **No OpenGL context:** `QSKIP` with a message naming the missing capability. The CI
  containers have Mesa's software renderer, so it runs there; the spike measured about
  1 s per chain on llvmpipe.
- **Policy bypass:** the test bypasses `UpscalePolicy`'s software-renderer rule on
  purpose, by driving `MpvPlayer` directly.
- **Coverage:** this is what proves every bundled shader compiles on Qt 6.4 / mpv 0.37
  (Ubuntu 24.04 floor), Debian 13 and Arch.

**Existing gates:**
- H, HC and `C(target)` for all six targets green; the test count rises by four
  executables, and the README and ARCHITECTURE counts move with it;
- the qmllint baseline unchanged (no new warnings);
- the page self-test passes;
- package install checks verify that `share/strmqt/shaders/*.glsl` and the licence texts
  are installed in the `.deb`, `.rpm` and AppImage, and in the Flatpak manifest's
  install list.

**Manual, on the RX 9070 XT** (recorded in a verifications file):
1. 480p, 720p and 1080p live action, and an anime episode: Auto picks the expected
   chain; the status line and stats overlay agree.
2. Switch through every picker entry mid-play: each change appears within a frame or
   two; no reload; position unchanged.
3. Resize the window and toggle fullscreen: the chain follows, for example dropping the
   doubler near 1:1.
4. Debug setting `upscale/budgetScale=0.01`: watch the governor step down, rung by rung,
   with the status line saying so.
5. An HDR film: no visible colour or brightness change versus Off, apart from sharpness.
6. A/V sync and pacing unchanged versus 0.7.5, in a 24 fps film on the 120 Hz screen
   (§4.4's own wait replaces mpv's).

## 7. Docs

- **ARCHITECTURE §3 Playback** gains an "Upscaling" subsection: the ladder, the
  governor, why mpv timings are not used, and why `GL_TIME_ELAPSED` is forbidden.
- **ARCHITECTURE §10 Known limitations** gains: mpv only; no `gpu-next`-only shaders
  (ArtCNN) while libmpv's render API is OpenGL.
- **The README's playback section** gains a line.

## 8. Risks

1. **Replacing mpv's target-time wait** (§4.4) could change pacing or A/V sync. That is
   why manual check 6 exists. If pacing regresses, the fallback is to keep mpv's
   blocking, take the timestamps with `BLOCK_FOR_TARGET_TIME = 1`, and discard any
   sample whose CPU-side render duration exceeds the GPU budget. That is coarser, but
   harmless.
2. **Integrated GPUs are untested.** The governor's thresholds are reasoned, not
   measured, on weak hardware. The debug budget scale (manual check 4) exercises the
   mechanism. A real laptop check is welcome but not a release blocker.
3. **Upstream shader repositories are effectively frozen** (FSRCNNX 2021, Anime4K 2021,
   the agyild gists). Vendored copies are what we ship, so this is low risk.
4. **FSRCNNX is LGPL-3.0-or-later, not MIT.** Its shader headers say so (the upstream
   repository's LICENSE file is MIT, which does not cover the shaders). It is compatible with
   StrmQt's licence, but it must be listed in the Debian `copyright` file and the `.rpm`
   licence field like any other copyleft component.

## 9. Acceptance

1. All automated gates in §6 are green on all six container targets.
2. Manual checks 1–6 are passed and recorded.
3. The shaders and licences are present in every package format.
4. ARCHITECTURE and README are updated, and the counts are right.
