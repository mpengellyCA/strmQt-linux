# StrmQt

**Your Emby library on the big screen — a native Linux client built for the couch.**

StrmQt is a media client for [Emby](https://emby.media) servers, written for KDE Plasma in
C++20 and Qt 6. It opens on your shelves rather than a menu, plays through mpv with
hardware decoding, and every inch of it can be driven with a game controller, your phone,
or the keyboard alone. A spare computer can also run it for a Fire TV or Android TV, with
[StrmLight](#on-a-tv-with-strmlight) on the TV and the TV's own remote in your hand.

[**Download**](https://github.com/mpengellyCA/strmQt-linux/releases) ·
[First run](#first-run) · [Controls](#controls) · [How it is built](ARCHITECTURE.md)

> **Early software, honestly labelled.** It is broadly functional against a live Emby 4.9
> server and has been through a lot of review, but not through months of daily use. See
> [Where it stands](#where-it-stands) before you rely on it.

---

## What you get

### A ten-foot interface that was designed as one

Home opens on rails of what you are part-way through and what is next. A library is a grid
you can re-shape — posters, wide art, or a list — with sorting, an A–Z jump for the times
you know exactly what you are looking for, and a **Filters** sheet that narrows films and
shows by what the server knows about them: genre, year, studio or network, age rating and
score, and for films the resolution, HDR format, video codec and audio layout. Series drill down to seasons and
put the cursor on the next unwatched episode, so arriving and pressing play does the
obvious thing. Nothing here is a desktop control scaled up.

### Playback that behaves like a player

libmpv with hardware decoding and HDR-aware tone mapping. Chapters, audio and subtitle
track pickers, multiple versions of the same title, a play queue with shuffle and repeat,
and auto-advance through a series. Leave the player and the film carries on in a
picture-in-picture frame; leave a record and it carries on in a docked bar with the artwork
still on it.

- Upscaling of video below the window's resolution: FSRCNNX and FSR1 for live
  action (ArtCNN for SD sources such as DVDs, which also cleans up compression
  noise), Anime4K for animation, chosen automatically per item and stepped
  down on GPUs that cannot keep up; overridable live from the player's Quality tab (mpv engine).

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

All of them go through one input layer, so they behave the same. An arrow always goes to
the nearest thing in that direction — every control a mouse can click, a pad can walk to,
the player's on-screen buttons included — a menu or a sheet keeps the keys until it is
closed, and focus is never left on nothing. Leave the mouse alone for five seconds and the
pointer hides and stops highlighting what it rests on, in the whole app and not only over
a video; move it and it is back.

### Your phone is already the remote

A full MPRIS2 interface, so KDE Connect and the Plasma media applet control it with no
setup. Another Emby client — the phone app, Emby Web — can also drive this one as a
playback target.

There is also a built-in Web Remote: switch it on in Settings (or accept the one-time
offer) and open the address it shows in your phone's browser. It is served over HTTPS on
your local network only, and a phone has to pair first — tap Pair, and StrmQt shows a
four-digit code over whatever is on screen for two minutes; type it on the phone — so
other devices on the network cannot take over playback. A phone stays paired until you
remove it in Settings. The remote's pairing has nothing to do with your Emby sign-in: it
drives the sign-in screen and the *Who's watching?* picker too, and signing in, switching
profile or signing out never disconnects it. Anything a key or a controller button can
do, the phone can ask for too, and its Remote screen switches between the full player and
the mini player.

### On a TV, with StrmLight

A computer with no screen of its own — a mini PC by the TV — can run StrmQt for a Fire TV
or Android TV to watch. **Settings → StrmLight → Streaming host** sets it up once: a
hidden desktop running StrmQt full screen, and a Sunshine of its own streaming it. It runs
as two services of your user with its own settings, certificate and ports, so a Sunshine
you already use for games is left alone, and it has a sound system of its own — a virtual
speaker and no sound card — so only StrmQt's sound reaches the TV. StrmQt never installs
or changes Sunshine; set-up says what is missing (KWin, Sunshine, PipeWire, WirePlumber)
if it cannot go ahead.

On the TV, [**StrmLight**](https://github.com/mpengellyCA/strmLight-android) — a Moonlight
made for StrmQt — finds the host on the network by itself and shows a four-digit code.
Type it once, under **Settings → StrmLight → Pair a StrmLight** or on the phone remote,
and both the picture and the remote are paired. From then on, opening StrmLight goes
straight to StrmQt, and the TV's own remote drives it: arrows, OK, Back, play and pause go
to StrmQt directly rather than as keys through the stream. Settings lists every paired
phone and StrmLight, each with Remove.

**Settings → StrmLight → Picture and sound** sets the host screen's size and scale, and
what StrmLight streams: frame rate, quality (bitrate), codec, and stereo, 5.1 or 7.1
sound. StrmLight reads these before each stream, so its own settings never need touching.
HDR is not offered yet, because KWin's headless screen cannot be HDR.

Only StrmLight can watch the streaming host. A plain Moonlight cannot pair with it, and
StrmLight in turn streams only from the Sunshine its StrmQt vouches for, never from a
plain Sunshine.

Several StrmLights can share one streaming host, and more than one can watch at once,
all seeing the same screen: the same film in two rooms, or music through the house. Each
StrmLight pairs with one StrmQt: pairing a TV with another StrmQt makes it forget the one
before. The host's Sunshine keeps
out of the desktop's system tray.

The streaming host is not in the Flatpak, which cannot run systemd user services or KWin
outside its sandbox; it says so instead of offering it.

### YouTube and YouTube Music, in the same room

Two more destinations in the menu, each driven with the same arrows, OK and Back as the
rest of the app — no mouse needed, no second remote.

**YouTube** is YouTube's own TV interface, the one a console or a smart TV shows. A video
takes the whole window; press Menu, or touch the left edge with the pointer, and the app's
menu slides out over it without stopping it. Leave the page and the video carries on in a
picture-in-picture frame with the same docked bar a film gets — scrub, ±10 seconds,
volume, stop, back. Trailers on a film's page play here instead of opening a browser.

**YouTube Music** is the full music site, drawn large and given a D-pad: an arrow moves a
cursor to the nearest song, album or button in that direction and OK plays it. What it
plays carries on under other pages with its cover, previous and next on the docked bar.

Only one thing is ever heard: starting a film, a YouTube video or a YouTube Music track
quiets the other two.

Signing in: YouTube signs in on its own screen with a code, as on a TV. YouTube Music
cannot be signed in to inside an app — Google refuses it — so **Settings → YouTube** copies
a sign-in from Firefox instead: press **Look in Firefox** and it lists each Firefox
profile and container on your computer, and whether it is signed in to YouTube; you choose
which one to use. Nothing of Firefox's is opened before you press that button. StrmQt
copies only the `youtube.com` and `google.com` cookies of the one you pick — which is the
Google session of every account signed in there — and never writes to Firefox's files.

The AppImage does not have these two yet; every other package does.

Don't want them? **Settings → YouTube** switches each off separately, and off is
gone: out of the menu, nothing playing, and trailers open in your browser as before.
There is no ad blocking and none is planned.

### Your credentials stay yours

Sign-in tokens go to the system keyring — KWallet on Plasma 6 or 5, and the Secret
Service (GNOME Keyring, KeePassXC) elsewhere — scoped per profile, and there is no
compiled-in server address. Where no keyring is available the app says so plainly and
falls back to an owner-only vault file rather than quietly writing a plaintext config.
Settings → Server has a **Credentials** row naming the store in use, and the Flatpak can
reach every one of them.

---

## Install

Builds for every release are on the
[**Releases page**](https://github.com/mpengellyCA/strmQt-linux/releases).

Each package is built inside, and for, one distro release, against that release's own
Qt. Install it from its local path so the package manager pulls in the dependencies.

| Distro | Format | Command |
|---|---|---|
| Ubuntu 24.04 LTS | `.deb` | `sudo apt install ./strmqt_0.9.0-1.ubuntu24.04_amd64.deb` |
| Ubuntu 26.04 LTS | `.deb` | `sudo apt install ./strmqt_0.9.0-1.ubuntu26.04_amd64.deb` |
| Debian 12 | `.deb` | `sudo apt install ./strmqt_0.9.0-1.deb12_amd64.deb` |
| Debian 13 | `.deb` | `sudo apt install ./strmqt_0.9.0-1.deb13_amd64.deb` |
| Fedora 43 | `.rpm` | `sudo dnf install ./strmqt-0.9.0-1.fc43.x86_64.rpm` |
| Fedora 44 | `.rpm` | `sudo dnf install ./strmqt-0.9.0-1.fc44.x86_64.rpm` |
| Arch and derivatives | Arch package | `sudo pacman -U ./strmqt-0.9.0-1-x86_64.pkg.tar.zst` |
| everyone | Flatpak | `flatpak install ./ca.mikesdev.StrmQt.flatpak` |
| glibc ≥ 2.39: Ubuntu 24.04+, Debian 13+, Fedora 40+ — not Debian 12 | AppImage | `chmod +x StrmQt-0.9.0-x86_64.AppImage` and run it |

A keyring is recommended, not required: the `.deb` recommends
`kwalletmanager | gnome-keyring | keepassxc`, the `.rpm` recommends
`(kf6-kwallet or gnome-keyring or keepassxc)`, and the Arch package lists `kwallet` and
`gnome-keyring` as optional dependencies. Without one, tokens go to the vault file.

### On Qt older than 6.8

Ubuntu 24.04 and Debian 12 ship Qt 6.4, and StrmQt runs on it with every page and every
feature. Nothing is missing; a few things are drawn differently:

- Icons, shadows and round masks are drawn by Qt's older effects module, and look the
  same or nearly so.
- The blurred backdrop behind Home and a person's page has a slightly different
  character.
- The Crate music headings are narrower, because Qt 6.4 cannot widen the display font.

[ARCHITECTURE.md](ARCHITECTURE.md#supported-qt-versions-and-compatibility-shims) has the
details.

### On Fedora

- **The `.rpm` is tied to Fedora's exact Qt version.** StrmQt uses a piece of Qt's
  private interface and carries QML compiled ahead of time for one Qt, so the package
  requires `qt6-qtdeclarative` at exactly the version it was built against. When Fedora
  updates Qt within a release, `dnf upgrade` holds that update back, or asks to remove
  StrmQt, until the next StrmQt release ships an `.rpm` rebuilt for the new Qt. The
  Flatpak does not have this problem.
- **HEVC and AAC need RPM Fusion's `ffmpeg`** in place of Fedora's `ffmpeg-free`. See
  [RPM Fusion's configuration page](https://rpmfusion.org/Configuration), then
  `sudo dnf swap ffmpeg-free ffmpeg --allowerasing`.

### The AppImage

An AppImage bundles libraries but never glibc itself — the dynamic loader, the NSS modules
and the graphics drivers all have to agree with the host's C library — so every AppImage
has a glibc floor. This one is built on Ubuntu 24.04, which puts that floor at glibc 2.39,
and CI runs it on bare Ubuntu 24.04, Debian 13 and Fedora 43 systems before a release.
It uses the host's own C++ runtime. Debian 12 (glibc 2.36) is below the floor; its users
take the `.deb` or the Flatpak.

### Building a `.deb` from source

On Ubuntu 24.04 and Debian 12, which do not package SDL3, the source package's build
downloads one pinned, hash-checked SDL3 3.4.16 tarball for the gamepad support, so an
offline build (an `sbuild` with no network) of those two will not work. Installing the
binary `.deb` is unaffected.

### Target platform

**Plasma 6 on Wayland** remains the primary target. Plasma 5.27 and GNOME are supported
through the keyring backends above, and a gamepad works on every listed distro.

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
| Previous / next item | Shift+`P` · Shift+`N` | — |
| Previous / next chapter | `[` · `]` | — |
| Playback statistics | Ctrl+`I` | — |
| Previous / next chapter, else item | Media Previous · Media Next | — |
| Fast-forward 2× → 32× | hold Media Next | — |

Left and Right seek while nothing on screen is selected. Once a control is, the arrows
move between controls instead — Up from the bar reaches the on-screen Back button, so you
can return to the library without stopping — and Select presses the one you are on.

**With a remote.** OK is Select and Back is Back, anywhere — Back closes a menu or a
panel as well as leaving a page. ⏭ and ⏮ act while anything plays, a record under the
library included: with chapters they step by chapter first, then through the queue.
Held, ⏭ fast-forwards, doubling every second from 2× to 32×, and letting go returns to
the speed you were playing at. The number keys work while browsing only, never in the
player, and a focused A–Z strip keeps them for jumping to `#`. If Plasma takes the media
keys for itself they arrive as MPRIS Next/Previous: a skip still works, the hold does not.

A button that does nothing can be identified: run with
`QT_LOGGING_RULES="strmqt.input.keys.debug=true"` and every key the window receives is
logged with its Qt key, scan code and keysym (keys that type a character are logged
without them, so a password never reaches the log). For the Flatpak, pass it with
`flatpak run --env=QT_LOGGING_RULES="strmqt.input.keys.debug=true" ca.mikesdev.StrmQt`.

## Where it stands

Version **0.9.0**, a pre-release: StrmQt on a TV. The streaming host, StrmLight's
one-code pairing and discovery, the TV remote and the host's separate sound arrive here,
along with code pairing for the phone remote, an on-screen keyboard, a featured title on
Home, logos, and gapless music with mpv. The host's file generation, pairing and
discovery are covered by tests, and the whole path — set-up, discovery, one code, the
stream, the TV's remote and the separate sound — has been used on one desktop with one
Fire TV. It has not yet run on a dedicated mini PC, or at boot with nobody logged in.

0.8.5 added YouTube and YouTube Music as destinations in the menu, with background play,
a docked bar and their own off switches. Both sites can change under StrmQt without
notice.

0.8.0 was the input release: keyboard, mouse, controller, remote
and phone go through one input layer, so an arrow always has somewhere to go and no
control is out of reach; libraries of films and shows can be filtered by their metadata;
an idle pointer hides everywhere; and video below the window's resolution is upscaled.
The input layer and the filters are covered by tests but have had very little time
in the hand on a real controller, remote or phone.
The core of it has been exercised against a live Emby 4.9 server: browsing, search,
playback of video and audio, live updates over a WebSocket, playlists, favourites,
resume and watch state reported back, remote control, and MPRIS2.

The build is clean under `-Werror`, `ctest` passes 92/92 on Qt 6.4.2, 6.8.2, 6.10 and
6.11 in CI containers, the reviewed qmllint warning baseline matches, and a
page-construction self-test builds all 17 screens on every release.

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
- Gapless audio is mpv only, and chapter thumbnails are not implemented.
- Music Home's *Forgotten favourites* shelf is not yet ordered by least recently
  played: Emby keeps no play date on an album to order it by.
- On a transcoded stream a held ⏭ fast-forwards no faster than the engine plays (4× in
  mpv), because every seek ahead would restart the server's transcode.

The full list, and the reasoning behind each, is in
[ARCHITECTURE.md](ARCHITECTURE.md#10-known-limitations).

## Building it yourself

<details>
<summary>Requirements and build steps</summary>

Required: **Qt 6.4+** (6.8+ for the full visual tier; Core, Gui, Quick, QuickControls2,
Network, DBus, OpenGL, Test, WebSockets), **libmpv**, **CMake 3.25+** and **Ninja**.

Optional, each degrading gracefully when absent: **libvlc** (fallback engine,
`-DSTRMQT_WITH_VLC=OFF`), **SDL3** (gamepad, `-DSTRMQT_WITH_SDL3=OFF`), a keyring —
**KWallet** or a **Secret Service** provider (credential storage), **kscreen** (HDR
probing via `kscreen-doctor`).

- `-DSTRMQT_QML_TIER=auto|full|compat` picks the QML tier; `auto` chooses `full` on
  Qt 6.8+ and `compat` below it.
- `-DSTRMQT_BUNDLE_SDL3=ON` fetches and statically links a pinned SDL3 3.4.16, for
  distros that do not package SDL3.

The compat tier needs **Qt5Compat.GraphicalEffects** at runtime.

```bash
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
./build/dev/strmqt
```

To check a change on another distro's Qt, run the same build, tests and self-test CI
runs inside that distro's container (podman):

```bash
scripts/ci/local.sh ubuntu-24.04 /tmp/wX -- /src/scripts/ci/check.sh /build $(scripts/ci/deps.sh --cmake-args ubuntu-24.04)
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
