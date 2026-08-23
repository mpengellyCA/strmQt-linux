# StrmQt

A native Emby client for KDE Plasma, written in C++20 against Qt 6 / Qt Quick.

- **10-foot UI** — a fully custom Qt Quick interface built for keyboard, gamepad and remote
  control, not for a mouse pointer. No stock controls.
- **libmpv playback** — raw libmpv C API, hardware decoding, HDR-aware tone mapping.
- **MPRIS2** — the app exposes a full `org.mpris.MediaPlayer2` interface, so KDE Connect
  turns a phone into a remote (play/pause, seek, stop, volume).
- **KWallet credentials** — the Emby access token never touches the repo or a config file
  when kwalletd6 is available.
- **Modular seams** — `MediaServerBackend` and `PlayerBackend` are interfaces. Emby and
  libmpv are the first implementations; Jellyfin/Plex/UPnP and other engines are meant to
  drop in beside them.

Target platform is Plasma 6 on Wayland (developed on CachyOS/Arch, Qt 6.11, Radeon RX 9070).
It is written to stay portable to other Plasma/Wayland and X11 systems, but that is untested.

## Status

Version **0.2.0** — early but broadly functional. Everything below has been exercised
against a live Emby 4.9 server:

- Browse libraries, collections, series, people and music; search across all of them
- Play video and audio with hardware decode, a full OSD, chapters, track and version
  pickers, a play queue with shuffle, and auto-advance through a series
- Live updates over a WebSocket, so watching something elsewhere is reflected here
- Playlists, favourites, resume, and watch state reported back to the server
- Remote control from another Emby client, and MPRIS2 for KDE Connect

The build is clean under `-Werror`, `ctest` passes 24/24, `strmqt_qmllint` reports no
type errors, and `STRMQT_SELFTEST=1` constructs all 13 pages.

**Caveats worth knowing before you try it.** The interface has been verified by tests,
a page-construction self-test and live-server probes rather than by sustained daily use,
so expect rough edges. Hardware decode is verified on one machine (Radeon RX 9070, Wayland);
other GPUs are untested. See [ARCHITECTURE.md](ARCHITECTURE.md) for how it is built and
where the known limits are.

## Build from source

```bash
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
./build/dev/strmqt
```

The `dev` preset is a Ninja Debug build with warnings as errors. For an optimised build use
the `release` preset (RelWithDebInfo, no `-Werror`):

```bash
cmake --preset release && cmake --build --preset release
```

Binaries land directly in `build/<preset>/`.

### strmqt-cli

The same build produces `strmqt-cli`, a headless Emby probe that links only the core
library (no QtGui). It is useful for checking a server, seeding a session, or debugging the
REST layer without the UI:

```bash
strmqt-cli status
strmqt-cli login --user NAME     # password from the terminal or $STRMQT_PASSWORD
strmqt-cli libraries
strmqt-cli resume
strmqt-cli latest [libraryId]
strmqt-cli nextup
strmqt-cli logout
```

## Dependencies

Required:

| Dependency | Version | Notes |
|------------|---------|-------|
| Qt 6       | 6.8+    | Core, Gui, Quick, QuickControls2, Network, DBus, OpenGL, Test |
| libmpv     | —       | primary playback engine, found via pkg-config |
| CMake      | 3.28+   | |
| Ninja      | —       | generator used by both presets |

Optional — each degrades gracefully when absent:

| Dependency | Provides | Disable with |
|------------|----------|--------------|
| libvlc  | fallback playback engine | `-DSTRMQT_WITH_VLC=OFF` |
| SDL3    | gamepad input | `-DSTRMQT_WITH_SDL3=OFF` |
| kwallet | secure credential storage (via `org.kde.kwalletd6` on D-Bus) | — |
| kscreen | HDR display probing via `kscreen-doctor` | — |

Without kwallet the token falls back to a plaintext INI file and the app logs a warning.
Without kscreen the HDR probe degrades silently and playback continues with tone mapping.
The app builds and runs keyboard-only without SDL3, and with mpv alone without libvlc.

## Packaging

Three artifacts live under `packaging/`:

- `arch/PKGBUILD` — native Arch package. Builds against the system Qt and mpv;
  `STRMQT_APPIMAGE_DEPLOY` stays off so no private Qt copy is shipped.
- `flatpak/ca.mikesdev.StrmQt.yml` — Flatpak manifest on `org.kde.Platform`, with mpv
  (and libvlc) built as manifest modules.
- `appimage/build-appimage.sh` — AppImage recipe; this is the one configuration that
  bundles the Qt/QML runtime (`-DSTRMQT_APPIMAGE_DEPLOY=ON`).

A `.desktop` entry and AppStream metainfo are installed from `packaging/desktop/` and
`packaging/appstream/`. The app id `ca.mikesdev.StrmQt` is shared by the desktop file,
AppStream component, icon name, and Wayland `app_id` so KWin associates the window with the
desktop entry.

## Configuration

Server URL and username are set in **Settings** (`F2` in the app). A default server URL is
compiled in, so a fresh install only needs credentials.

- Access token: KWallet folder **`StrmQt`**, key **`emby/accessToken`**.
- Other settings (server URL, user id, device id, playback engine, tone-mapping curve):
  QSettings, under the `StrmQt` organisation.
- Logs go to journald whenever the app has no tty:

  ```bash
  journalctl --user -t strmqt -f
  ```

Default controls: arrows to navigate, Enter/Space to select, Esc to go back, `/` for search,
`F2` for settings, `F11` for fullscreen, `A`/`C` to cycle audio and subtitle tracks during
playback, `I` for the OSD, `S` to stop.

## Known issues

- **The libVLC fallback engine renders a black video plane.** Its vmem display callback
  never fires — format negotiation logs `1920x800 → 802` and then goes silent. mpv is the
  default engine and is fully working; this only affects `playback/engine=vlc`. The cause is
  narrowed to the forced RV32 chroma with no fallback, compounded by `--quiet` suppressing
  the libvlc error that would confirm it.
- With `engine=vlc`, audio/subtitle track cycling and decoder-info reporting are no-ops —
  `VlcPlayer` does not override those methods.
- **Zero-copy VAAPI is not reached.** `MpvVideoItem` does not pass
  `MPV_RENDER_PARAM_WL_DISPLAY` to mpv, so VAAPI cannot obtain a `VADisplay`. Hardware
  decode still works through `vulkan-copy` (verified on an RX 9070), so the cost is one
  extra copy rather than a lost feature.
- **True HDR passthrough is deferred.** The embedded GL path composites into an SDR FBO.
  HDR content is tone-mapped instead, correctly and verified against HDR10 HEVC; the default
  curve is `hable` because vo_gpu behind the render API rejects `bt.2446a`.
- Gamepad (SDL3) and KDE Connect phone control work and their init paths are verified live,
  but sustained end-to-end use on physical hardware has not been exercised.
- **No gapless audio advance.** mpv's gapless path wants a playlist handed to the engine
  rather than per-item loads.
- MPRIS Next/Previous, chapter thumbnails, picture-in-picture and drag-to-reorder are not
  implemented; each needs a verb or an identifier the current interfaces do not carry.
- The AppImage's glibc floor is set by the bundled FFmpeg, not by this code: it runs on
  current Arch and little else.

## License

GPL-3.0-or-later. See [COPYING](COPYING) for the full license text.
