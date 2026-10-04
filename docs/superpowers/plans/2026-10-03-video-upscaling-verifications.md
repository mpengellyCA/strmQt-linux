# Video upscaling — verifications

Manual checks from spec 2026-10-03 §6, on the RX 9070 XT (radeonsi), 2560×1440
120 Hz. Each line: date, build commit, what was seen. A failed check is
recorded as failed, with what happened.

| # | Check | Result |
|---|---|---|
| 1 | 480p, 720p, 1080p live action and an anime episode: Auto's chain; status line and stats overlay agree | |
| 2 | Every picker entry mid-play: visible within a frame or two; no reload; position unchanged | |
| 3 | Window resize and fullscreen toggle: chain follows (doubler drops near 1:1) | |
| 4 | `upscale/budgetScale=0.01` in settings.ini: governor steps down rung by rung; status line says so | |
| 5 | HDR film: no colour or brightness change against Off, apart from sharpness | |
| 6 | 24 fps film on the 120 Hz screen: A/V sync and pacing unchanged against 0.7.5 | |

Checks deferred to the user from Task 9 (the interface was only built and
started in a headless compositor, not looked at):

| # | Check | Result |
|---|---|---|
| 7 | A 720p film in a maximised window on the 1440p screen: OSD gear → Quality shows an "Upscaling" select reading "Auto"; the line under it reads `FSRCNNX 16 · 720p → 1440p` (overlaps check 1; check both) | |
| 8 | Choosing "FSR 1" changes the picture within a frame or two, with no reload and no jump in position; the line ends `· manual` (overlaps check 2) | |
| 9 | Ctrl+I shows the `Upscaling`, `GPU per frame` and (after a step-down only) `Upscale ceiling` rows | |
| 10 | Leaving the player for picture-in-picture, then returning: the log shows `upscaling: Off · 720p fills the window` and then the chain again | |
| 11 | Settings → Playback shows "Re-test upscaling", and its button is enabled on mpv | |
| 12 | Restart with the VLC engine (Settings → Playback → Engine): the Upscaling select is disabled and reads `Not available with VLC` | |
| 13 | Up/Down from the playback-mode chips reaches the Upscaling select and comes back | |
| 14 | libmpv 0.37 floor (Ubuntu 24.04): pacing and A/V sync. The runtime pick of the `target_time` clock (ns or µs) has been verified only on libmpv 2.5 | |

## Automated evidence so far

Task 6 measured pacing in a private headless compositor on the RX 9070, 1080p
23.976 fps clip in a 960×540 window, 12 s, three runs each, against HEAD
before the change (libmpv 2.5):

| build | swap interval p50 (ms) | drops | avsync abs max |
|---|---|---|---|
| HEAD | 41.94–41.95 | 0, 0, 0 | 0.0000 |
| with GPU timing and the moved wait | 41.94–41.96 | 0, 0, 0 | ≤ 0.0001 |

GPU time per 0.5 s window was mostly 0.24–1.1 ms p95. This does not cover the
120 Hz screen, HDR, or libmpv 0.37.

## Automated gates (§6)

Run on the Task 10 tree (branch `video-upscaling`, 80 tests).

| Gate | Result |
|---|---|
| H (full tier, Debug, `-Werror`) | build: 0 warnings; `100% tests passed out of 80`; `qmllint warning baseline matches (1340 warnings).`; `selftest.sh: OK` |
| HC (compat tier) | build: 0 warnings; `100% tests passed out of 80`; `selftest.sh: OK` |
| C(ubuntu-24.04) | `check-shaders.sh: OK (/build/stage/usr/share/strmqt/shaders)`; `check.sh: OK (compat)` |
| C(ubuntu-26.04) | `check-shaders.sh: OK (/build/stage/usr/share/strmqt/shaders)`; `check.sh: OK (full)` |
| C(debian-12) | `check-shaders.sh: OK (/build/stage/usr/share/strmqt/shaders)`; `check.sh: OK (compat)` at a3086fd. Earlier run failed to build (`mpv_get_time_ns` undeclared on libmpv 0.35); fixed in a3086fd by guarding it behind client API >= 2.2. |
| C(debian-13) | `check-shaders.sh: OK (/build/stage/usr/share/strmqt/shaders)`; `check.sh: OK (full)` |
| C(fedora-43) | `check-shaders.sh: OK (/build/stage/usr/share/strmqt/shaders)`; `check.sh: OK (full)` |
| C(fedora-44) | `check-shaders.sh: OK (/build/stage/usr/share/strmqt/shaders)`; `check.sh: OK (full)` |
| debian-13 .deb build and install-check | `check-shaders.sh: OK (/usr/share/strmqt/shaders)`; `selftest.sh: OK` |
| fedora-44 .rpm build and install-check | `check-shaders.sh: OK (/usr/share/strmqt/shaders)`; `selftest.sh: OK` |
