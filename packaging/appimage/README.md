# StrmQt AppImage

The release AppImage is built in an `ubuntu:24.04` container
(`scripts/ci/deps.sh appimage`):

| Piece | Source |
|---|---|
| Qt 6.11.3 (`linux_gcc_64`, plus `qtwebsockets`) | aqtinstall 3.3.0, installed at `/opt/qt/6.11.3/gcc_64`. `Deploy.cmake`'s `INCLUDE_PLUGINS` needs Qt ≥ 6.10, and the base download already carries QtSvg and QtWayland. |
| libmpv 0.37 and its codec closure (ffmpeg 6.1) | Ubuntu 24.04's `libmpv-dev`, walked by `build-appimage.sh` |
| SDL3 3.4.16 | bundled static (`-DSTRMQT_BUNDLE_SDL3=ON`) |
| appimagetool 1.9.1 | pinned by sha256 |

Local build, the same one release CI runs (the source copy is there because
`build-appimage.sh` writes next to its source, and `/src` is read-only):

```bash
df -h /tmp
scripts/ci/local.sh appimage /tmp/w17a-appimage -- bash -c '
  export TMPDIR=/var/tmp
  /src/scripts/ci/copy-tree.sh /src /var/tmp/src-copy
  cd /var/tmp/src-copy &&
  STRMQT_APPIMAGE_BUILD_ROOT=/build/out STRMQT_APPIMAGE_CMAKE_ARGS="$(/src/scripts/ci/deps.sh --cmake-args appimage)" \
  APPIMAGE_EXTRACT_AND_RUN=1 packaging/appimage/build-appimage.sh'
# -> /tmp/w17a-appimage/out/dist/StrmQt-<version>-x86_64.AppImage   (~180 MB)
```

On Arch, `./packaging/appimage/build-appimage.sh` still works (pkg-config and a
Qt ≥ 6.10 are enough) and writes `build/dist/`, but that AppImage inherits
Arch's glibc floor and is for that machine class only.

## Where it runs: glibc 2.39 and up

glibc symbol versioning is forward-only: a binary importing `GLIBC_2.x` will
not load against an older glibc. So the newest `GLIBC_` symbol anything in the
bundle imports is the oldest glibc the AppImage starts on. `build-appimage.sh`
checks it on every build and fails above **2.39**, Ubuntu 24.04's glibc. The
aqt Qt is built on an older base and does not raise it. The 0.7.5 build logs
`newest glibc symbol needed: GLIBC_2.38`.

That covers **Ubuntu 24.04+, Debian 13+ and Fedora 40+**. **Debian 12**
(glibc 2.36) is not covered: its users take the `.deb` or the Flatpak.

Release CI proves the floor rather than trusting it: the finished AppImage runs
its self-test in bare `debian:trixie` and `fedora:43` containers that carry only
the table below (`scripts/ci/appimage-host.sh`).

## What the host must provide

The bundle deliberately ships **no** library that talks to a kernel device, the
display server, the audio server, or the font database. Those must be the
host's, because the other half of the conversation is the host's — a bundled
`libGL` cannot load the host's DRI driver, and a bundled `fontconfig` reads the
host font cache with the wrong version stamp and returns zero fonts.

This is every soname a bundled object needs that the bundle does not carry,
measured from the `DT_NEEDED` entries of the 0.7.5 AppDir. Most of it is
required at startup, not just by a feature: `libmpv` is linked, and Ubuntu's
libmpv and ffmpeg link SDL2, JACK, PipeWire, Vulkan, OpenCL, cairo and pango
directly. `scripts/ci/appimage-host.sh` installs exactly these packages.

| Component | Sonames | Debian / Ubuntu | Fedora |
|---|---|---|---|
| GL / Mesa | `libGL`, `libEGL`, `libgbm`, `libdrm` (+ Mesa's drivers) | `libgl1 libegl1 libglx-mesa0 libegl-mesa0 libgl1-mesa-dri libgbm1 libdrm2` | `libglvnd-glx libglvnd-egl mesa-libGL mesa-libEGL mesa-dri-drivers mesa-libgbm libdrm` |
| Video accel / compute | `libva`, `libva-drm`, `libva-wayland`, `libva-x11`, `libvdpau`, `libvulkan`, `libOpenCL` | `libva2 libva-drm2 libva-wayland2 libva-x11-2 libvdpau1 libvulkan1 ocl-icd-libopencl1` | `libva libvdpau vulkan-loader OpenCL-ICD-Loader` |
| Wayland / keyboard | `libwayland-client/cursor/egl`, `libxkbcommon`, `libxkbcommon-x11` | `libwayland-client0 libwayland-cursor0 libwayland-egl1 libxkbcommon0 libxkbcommon-x11-0` | `libwayland-client libwayland-cursor libwayland-egl libxkbcommon libxkbcommon-x11` |
| X11 | `libX11`, `libX11-xcb`, `libXext`, `libXrandr`, `libXss`, `libXv`, `libXpresent` | `libx11-6 libx11-xcb1 libxext6 libxrandr2 libxss1 libxv1 libxpresent1` | `libX11 libX11-xcb libXext libXrandr libXScrnSaver libXv libXpresent` |
| xcb (Qt's xcb plugin) | `libxcb`, `-cursor`, `-glx`, `-icccm`, `-image`, `-keysyms`, `-randr`, `-render`, `-render-util`, `-shape`, `-shm`, `-sync`, `-util`, `-xfixes`, `-xkb` | `libxcb1 libxcb-cursor0 libxcb-glx0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-randr0 libxcb-render0 libxcb-render-util0 libxcb-shape0 libxcb-shm0 libxcb-sync1 libxcb-util1 libxcb-xfixes0 libxcb-xkb1` | `libxcb xcb-util-cursor xcb-util-wm xcb-util-image xcb-util-keysyms xcb-util-renderutil xcb-util` |
| Fonts | `libfontconfig`, `libfreetype`, `libharfbuzz`, `libfribidi` | `libfontconfig1 libfreetype6 libharfbuzz0b libfribidi0` | `fontconfig freetype harfbuzz fribidi` |
| GLib / cairo / pango | `libglib-2.0`, `libgobject`, `libgio`, `libgthread`, `libcairo`, `libcairo-gobject`, `libpango-1.0`, `libpangocairo` | `libglib2.0-0t64 libcairo2 libcairo-gobject2 libpango-1.0-0 libpangocairo-1.0-0` | `glib2 cairo cairo-gobject pango` |
| Pixbuf (ffmpeg's librsvg) | `libgdk_pixbuf-2.0` | `libgdk-pixbuf-2.0-0` | `gdk-pixbuf2` |
| Audio | `libasound`, `libpulse`, `libpipewire-0.3`, `libjack`, `libSDL2` (mpv, ffmpeg) | `libasound2t64 libpulse0 libpipewire-0.3-0t64 libjack-jackd2-0 libsdl2-2.0-0` | `alsa-lib pulseaudio-libs pipewire-libs pipewire-jack-audio-connection-kit-libs sdl2-compat` |
| TLS / crypto | `libssl`, `libcrypto`, `libgnutls`, `libnettle`, `libgcrypt`, `libgpg-error`, `libgssapi_krb5` | `libssl3t64 libgnutls30t64 libnettle8t64 libgcrypt20 libgpg-error0 libgssapi-krb5-2` | `openssl-libs gnutls nettle libgcrypt libgpg-error krb5-libs` |
| System | glibc ≥ 2.39, `libdbus-1`, `libudev` | `libdbus-1-3 libudev1` | `dbus-libs systemd-libs` |
| Compression | `libz`, `libzstd`, `liblzma`, `liblz4`, `libbrotli*` | `zlib1g libzstd1 liblzma5 liblz4-1 libbrotli1` | `zlib-ng-compat libzstd xz-libs lz4-libs libbrotli` |

A desktop install has all of these. SDL3 is not on the list: the AppImage links
it statically. `libbz2` is not either: Debian's soname is `libbz2.so.1.0` and
Fedora ships only `libbz2.so.1`, so the AppImage carries its own.

## What is bundled

Qt 6.11.3 (Core/Gui/Quick/QuickControls2/Qml/Network/DBus/OpenGL/WebSockets/
WaylandClient/XcbQpa and ICU), the Qt QPA plugins `qwayland`, `qxcb`,
`qoffscreen`, `qminimal` plus the Wayland shell/decoration/graphics
integrations, the imageformats plugins in aqt's base download (jpeg, gif, svg,
ico; PNG is built into QtGui), `libmpv` and its codec closure (ffmpeg,
libplacebo, libass, dav1d, x264/x265, …), and `libstdc++`/`libgcc_s`. About 186
shared objects, ~550 MB uncompressed (RelWithDebInfo), ~180 MB after zstd
squashfs.

Explicitly **not** bundled: libVLC (the AppImage is mpv-only — see
`build-appimage.sh`), Qt translations, the 27 KImageFormats plugins, GTK/Plasma
platform themes, and QML tooling plugins.

## Files here

| File | Role |
|---|---|
| `Deploy.cmake` | Included by `src/CMakeLists.txt` only under `-DSTRMQT_APPIMAGE_DEPLOY=ON`. Drives `qt_generate_deploy_qml_app_script` and owns the exclusion policy. |
| `AppRun` | AppImage entry point. Sets `QSG_RHI_BACKEND`, `XDG_DATA_DIRS`, clears `QT_QPA_PLATFORMTHEME`, offers `--strmqt-cli`. |
| `build-appimage.sh` | Configure → build → install → bundle mpv → patchelf → assert → pack. |

Both `Deploy.cmake` and `build-appimage.sh` carry long comments explaining
*why* each rule exists. Read them before changing a list; several of the rules
look like over-caution and are not.

## Non-obvious things that will bite you

1. **Qt's "don't bundle system libraries" guard does nothing on a distro Qt.**
   It only ignores link directories *outside* `QT6_INSTALL_PREFIX`, which is
   `/usr` here — so `/usr/lib` is inside it and nothing is ignored. Left at
   defaults, Qt copies libc, libGL and fontconfig into the AppDir. The
   `PRE_EXCLUDE_REGEXES` list in `Deploy.cmake` is the only thing preventing
   that.

2. **Qt ships only the xcb QPA plugin unless you ask for Wayland by name.**
   `Gui.json` declares `"platforms": ["xcb"]` and Qt strips `platforms` from
   bulk plugin selection. Without `INCLUDE_PLUGINS qwayland` the AppImage runs
   silently under XWayland — no error, just blurry scaling and no native
   fullscreen. `build-appimage.sh` asserts on this.

3. **`LD_LIBRARY_PATH` must never be set in `AppRun`.** It is inherited by
   children (we spawn host `kscreen-doctor`) and it outranks the `DT_RUNPATH`
   that Mesa and the VA-API drivers depend on. Bundled libraries are found via
   `$ORIGIN` RPATH instead, which is per-object and not inherited.

4. **Arch's `libmpv.so.2` has an absolute `DT_NEEDED`** (`/usr/lib/libmujs.so`).
   CMake's `file(GET_RUNTIME_DEPENDENCIES)` refuses to scan such an object and
   aborts the whole install, so `Deploy.cmake` excludes `^libmpv` and
   `build-appimage.sh` bundles mpv's closure itself with a denylist-pruned BFS,
   then rewrites the absolute entry to a bare soname with patchelf.

5. **`-DQT_DEPLOY_USE_PATCHELF=ON` is mandatory.** Qt's default RPATH fixup uses
   `file(RPATH_SET)`, which can only rewrite an existing `DT_RPATH`/`DT_RUNPATH`
   and cannot create one. Several Qt Wayland plugins ship with none, so the
   install dies partway through. `Deploy.cmake` fails fast with an explanation
   if the flag is missing.

6. **Denylist patterns need `-` and `_` in their character classes.**
   `libX[A-Za-z0-9]*\.so` matches `libX11.so.6` but *not* `libX11-xcb.so.1`,
   which is how the X11↔xcb bridge leaked into an early build. Symmetrically,
   an unanchored `^libva` swallows `libvapoursynth-script.so.0`, which is a real
   `DT_NEEDED` of libmpv and must be bundled.

## Verifying a build

```bash
APPDIR=/tmp/w17a-appimage/out/appimage/AppDir     # build/appimage/AppDir on Arch

# Bundled Qt/mpv resolve into the AppDir; GL/fontconfig/libva resolve to the host.
ldd $APPDIR/usr/bin/strmqt | grep -E 'libQt6Core|libmpv|libGL\.so|libfontconfig|libva\.so'

# No bundled object may be missing an $ORIGIN RPATH (aqt puts plugins and QML in usr/plugins, usr/qml).
find $APPDIR/usr/lib $APPDIR/usr/plugins $APPDIR/usr/qml -type f -name '*.so*' -exec sh -c 'patchelf --print-rpath "$1" | grep -q "\$ORIGIN" || echo "BAD $1"' _ {} \;

# No absolute DT_NEEDED may survive.
find $APPDIR/usr -type f -name '*.so*' -exec patchelf --print-needed {} \; | grep '^/'

# Headless page-construction self-test (this is what qoffscreen is bundled for).
APPIMAGE_EXTRACT_AND_RUN=1 scripts/ci/selftest.sh /tmp/w17a-appimage/out/dist/StrmQt-*-x86_64.AppImage
```

`build-appimage.sh` runs the forbidden-soname assertion and the
`libqwayland` / `wayland-shell-integration` / `libmpv` presence assertions on
every build. **If the forbidden-soname assertion fires, fix the list in
`Deploy.cmake` and rebuild — do not delete the offending file from the AppDir.**
By the time the file exists, every dependant was already resolved against it;
deleting it leaves dangling `DT_NEEDED` entries that fail at `dlopen` time, in a
code path nobody tests, months later.

## VLC-disabled runtime check

The AppImage configures with `-DSTRMQT_WITH_VLC=OFF`; `PlayerPage` loads the
VLC video plane conditionally, so the mpv-only QML module remains complete.
Release CI runs `STRMQT_SELFTEST=1` from inside the finished AppImage and fails
the job if any of the 13 pages cannot be constructed.

## appimagetool AppStream warning

appimagetool warns that `usr/share/metainfo/ca.mikesdev.StrmQt.appdata.xml` is
missing. We install `ca.mikesdev.StrmQt.metainfo.xml`, which is the current
AppStream filename; `.appdata.xml` is the pre-2019 spelling. Shipping both would
register a duplicate component id. The warning is safe to ignore.
