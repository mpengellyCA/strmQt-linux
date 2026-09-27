# StrmQt

**Your Emby library on the big screen — a native Linux client built for the couch.**

StrmQt is a media client for [Emby](https://emby.media) servers, written for KDE Plasma in
C++20 and Qt 6. It opens on your shelves rather than a menu, plays through mpv with
hardware decoding, and every inch of it can be driven with a game controller, your phone,
or the keyboard alone.

[**Download**](https://github.com/mpengellyCA/strmQt-linux/releases) ·
[First run](#first-run) · [Controls](#controls) · [How it is built](ARCHITECTURE.md)

> **Early software, honestly labelled.** It is broadly functional against a live Emby 4.9
> server and has been through a lot of review, but not through months of daily use. See
> [Where it stands](#where-it-stands) before you rely on it.

---

## What you get

### A ten-foot interface that was designed as one

Home opens on rails of what you are part-way through and what is next. A library is a grid
you can re-shape — posters, wide art, or a list — with sorting, filters, and an A–Z jump
for the times you know exactly what you are looking for. Series drill down to seasons and
put the cursor on the next unwatched episode, so arriving and pressing play does the
obvious thing. Nothing here is a desktop control scaled up.

### Playback that behaves like a player

libmpv with hardware decoding and HDR-aware tone mapping. Chapters, audio and subtitle
track pickers, multiple versions of the same title, a play queue with shuffle and repeat,
and auto-advance through a series. Leave the player and the film carries on in a
picture-in-picture frame; leave a record and it carries on in a docked bar with the artwork
still on it.

### Music treated as music

A music library opens on its own home: pick up where you left off, stations, genre
bins, the artists you actually play, and a record pulled out at random. Browse it as
albums, artists, songs, genres or playlists with filter pills (genre, decade, format,
favourites, unplayed) and A–Z crate dividers. Albums show their liner notes, discs and
format; the full player pulls the record out of the sleeve, with lyrics beside it when
your files carry them and its own volume and mute. ReplayGain volume
normalisation, instant mixes from anything, and multi-select for batch favouriting
and queueing.

### It works with whatever is in your hand

Keyboard, mouse, an Xbox-style controller, or a remote. The controller is a first-class
citizen, not an afterthought: the shoulders change section, the triggers jump by letter
through a library too long to scroll, and holding **A** opens the same actions menu the
right mouse button does. A Bluetooth or TV remote needs no setup: OK selects, Back
goes back, Home goes Home, and the number pad opens your libraries. ⏮ and ⏭ step by
chapter and then by track, and holding ⏭ fast-forwards. Every binding is remappable in
Settings, and the on-screen shortcut sheet always shows the real one.

### Your phone is already the remote

A full MPRIS2 interface, so KDE Connect and the Plasma media applet control it with no
setup. Another Emby client — the phone app, Emby Web — can also drive this one as a
playback target.

### Your credentials stay yours

Sign-in tokens go to KWallet, scoped per profile, and there is no compiled-in server
address. Where KWallet is unavailable the app says so plainly and falls back to an
owner-only vault file rather than quietly writing a plaintext config.

---

## Install

Builds for every release are on the
[**Releases page**](https://github.com/mpengellyCA/strmQt-linux/releases).

**On Fedora, Ubuntu, Debian or anything that is not Arch — use the Flatpak.**

| Format | Who it is for | |
|---|---|---|
| **Flatpak** | everyone | `flatpak install ./ca.mikesdev.StrmQt.flatpak` |
| **Arch package** | Arch and derivatives | `sudo pacman -U ./strmqt-*.pkg.tar.zst` |
| **AppImage** | Arch, without installing anything | `chmod +x` and run — **needs glibc ≥ 2.44** |

The AppImage is a convenience build, not a run-anywhere binary, and the difference
matters enough to spell out. An AppImage bundles libraries but never glibc itself —
the C library has to be the host's, because the dynamic loader, the NSS modules and
the graphics drivers all have to agree with it. glibc is backward compatible and not
forward compatible, so a bundle built on a new distro will not start on an older one.
This one is built on current Arch, and the ffmpeg it has to carry imports
`GLIBC_2.44`, which puts the floor above Fedora 43 (2.42) and every current Ubuntu
and Debian. They get:

```
/lib64/libm.so.6: version `GLIBC_2.44' not found
```

Lowering that floor means building ffmpeg, libplacebo, libmpv and Qt from source on
an old base — a real packaging project, not a flag. The Flatpak already solves it by
carrying its own runtime, which is what it is for.

Target platform is **Plasma 6 on Wayland**. It is written to stay portable to other
Plasma/Wayland and X11 systems, but that is untested.

## First run

There is no server baked in. On first launch StrmQt asks for your own Emby server address
and signs you in; after that it remembers the profile and opens on a
*Who's watching?* picker.

Everything else lives in **Settings** (`F2`): the playback engine, tone-mapping curve,
appearance and density, live updates, and the full key-binding table.

## Controls

Defaults — all of them remappable, and `?` shows the current set at any time.

### Browsing

| | Keyboard | Controller |
|---|---|---|
| Move | Arrows | D-pad / left stick |
| Select | Enter · Space | **A** |
| Back | Esc · Backspace | **B** |
| Item actions | Menu | **hold A** |
| Menu rail | `M`, or Left at the edge of a page | **Menu**, or Left at the edge |
| Previous / next tab or library | Ctrl+Tab | **LB** / **RB** |
| Jump a letter | `[` `]` | **LT** / **RT** |
| Search | `/` | **Y** |
| Home | Home Page (a remote's Home) | — |
| Library 1–9 in the menu · Favorites | `1`–`9` · `0` | — |
| Command palette | Ctrl+K | — |
| Now-playing bar | `N` | **R3** |
| Full player ↔ mini player | `V` | **L3** |
| Settings · full screen | `F2` · `F11` | — |

### Playing

| | Keyboard | Controller |
|---|---|---|
| Play / pause | Space · `K` | **A** |
| Seek ±10 s | `J` · `L` | **LT** / **RT** |
| Seek ±60 s | PgDn · PgUp | **LB** / **RB** |
| Audio / subtitle track | `A` · `C` | **X** / **Y** |
| On-screen display | `I` | **Menu** |
| Volume | `+` · `-` | right stick |
| Leave, keep playing | Esc · Backspace | **View** |
| Stop | `S` | — |
| Previous / next chapter, else item | Media Previous · Media Next | — |
| Fast-forward 2× → 32× | hold Media Next | — |

**With a remote.** OK is Select and Back is Back, anywhere — Back closes a menu or a
panel as well as leaving a page. ⏭ and ⏮ act while anything plays, a record under the
library included: with chapters they step by chapter first, then through the queue.
Held, ⏭ fast-forwards, doubling every second from 2× to 32×, and letting go returns to
the speed you were playing at. The number keys work while browsing only, never in the
player, and a focused A–Z strip keeps them for jumping to `#`. If Plasma takes the media
keys for itself they arrive as MPRIS Next/Previous: a skip still works, the hold does not.

A button that does nothing can be identified: run with
`QT_LOGGING_RULES="strmqt.input.keys.debug=true"` and every key the window receives is
logged with its Qt key, scan code and keysym.

## Where it stands

Version **0.7.0**, a pre-release. It completes the Crate music redesign — Music Home,
Browse, the album, artist and playlist pages, and the full player with lyrics, volume
and mute — and adds support for Bluetooth and TV remotes. The core of it has been
exercised against a live Emby 4.9 server: browsing, search, playback of video and audio,
live updates over a WebSocket, playlists, favourites, resume and watch state reported
back, remote control, and MPRIS2.

The build is clean under `-Werror`, `ctest` passes 74/74, the reviewed qmllint warning
baseline matches, and a page-construction self-test builds all 15 screens on every release.

Worth knowing before you rely on it:

- The interface has been verified by tests, a self-test and live-server probes rather than
  by sustained daily use. Expect rough edges.
- Hardware decode is verified on one machine (Radeon RX 9070, Wayland). Other GPUs are
  untested.
- **mpv is the engine to use.** The libVLC fallback is incomplete — no track switching or
  decoder reporting — and its video output depends on VLC plugin packages that only the
  0.4.1 dependency list started declaring.
- Zero-copy VAAPI is not reached: hardware decode goes through `vulkan-copy`, which costs
  one extra copy rather than a feature.
- True HDR passthrough is deferred. HDR content is tone-mapped instead, correctly, and
  verified against HDR10 HEVC.
- No gapless audio advance yet, and chapter thumbnails are not implemented.
- Music Home's *Forgotten favourites* shelf is not yet ordered by least recently
  played: Emby keeps no play date on an album to order it by.
- On a transcoded stream a held ⏭ fast-forwards no faster than the engine plays (4× in
  mpv), because every seek ahead would restart the server's transcode.

The full list, and the reasoning behind each, is in
[ARCHITECTURE.md](ARCHITECTURE.md#10-known-limitations).

## Building it yourself

<details>
<summary>Requirements and build steps</summary>

Required: **Qt 6.8+** (Core, Gui, Quick, QuickControls2, Network, DBus, OpenGL, Test,
WebSockets), **libmpv**, **CMake 3.28+** and **Ninja**.

Optional, each degrading gracefully when absent: **libvlc** (fallback engine,
`-DSTRMQT_WITH_VLC=OFF`), **SDL3** (gamepad, `-DSTRMQT_WITH_SDL3=OFF`), **kwallet**
(credential storage), **kscreen** (HDR probing via `kscreen-doctor`).

```bash
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
./build/dev/strmqt
```

`dev` is a Ninja Debug build with warnings as errors; `release` is RelWithDebInfo without
them. Binaries land in `build/<preset>/`.

The same build produces **`strmqt-cli`**, a headless Emby probe that links only the core
library — useful for checking a server or debugging the REST layer with no UI:

```bash
strmqt-cli status | login --user NAME | libraries | resume | latest [id] | nextup | logout
```

Logs go to journald whenever the app has no tty:

```bash
journalctl --user -t strmqt -f
```

[ARCHITECTURE.md](ARCHITECTURE.md) explains how the program is put together and, more
usefully, why several parts of it look the way they do. [AGENTS.md](AGENTS.md) carries the
conventions any contributor — human or otherwise — is expected to follow.
[docs/BRANDING.md](docs/BRANDING.md) covers the brand mark, color tokens and typography.

</details>

## License

GPL-3.0-or-later. See [COPYING](COPYING) for the full text.
