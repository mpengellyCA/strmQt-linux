# StrmQt AppImage

    ./packaging/appimage/build-appimage.sh
    # -> build/dist/StrmQt-0.3.0-x86_64.AppImage   (~99 MB)

## Read this before you hand the file to anyone

**This AppImage is not portable.** It is a convenience artifact for the machine
class it was built on, not a run-anywhere binary. If you need portable
distribution, use the Flatpak (`packaging/flatpak/`). That is what it is for.

The reason is not our code and cannot be fixed by our packaging:

```
$ objdump -T AppDir/usr/lib/libavcodec.so.62 | grep -o 'GLIBC_2\.[0-9]*' | sort -uV | tail -1
GLIBC_2.43
```

The bundled `libavcodec` — which we must bundle, because the whole point is to
carry the codec stack — imports symbols versioned `GLIBC_2.43`. glibc symbol
versioning is forward-only: a binary importing `GLIBC_2.43` will not load
against an older glibc, full stop. That puts the floor at roughly February 2026
**regardless of what StrmQt itself is compiled against**. In practice: current
Arch and its derivatives. Debian stable, Ubuntu LTS, RHEL and friends will
refuse to start it.

The usual AppImage answer is to build on the oldest distro you intend to
support. That would mean building ffmpeg, libplacebo, libmpv and Qt 6.11 from
source on an ancient base — a distinct, much larger job than this script. Until
someone does that, treat this as "an Arch build you can copy to another Arch
box without installing dependencies".

## What the host must provide

The bundle deliberately ships **no** library that talks to a kernel device, the
display server, the audio server, or the font database. Those must be the
host's, because the other half of the conversation is the host's — a bundled
`libGL` cannot load the host's DRI driver, and a bundled `fontconfig` reads the
host font cache with the wrong version stamp and returns zero fonts.

So the host needs:

| Component | Sonames | Consequence if absent |
|---|---|---|
| Mesa / GL | `libGL`, `libGLX`, `libEGL`, `libGLdispatch`, `libgbm`, `libdrm` | no GL context; app aborts at startup |
| Video accel | `libva`, `libva-drm`, `libva-wayland`, `libva-x11`, `libvdpau`, `libvulkan` | software decode only, or mpv init failure |
| Display | `libwayland-client/cursor/egl`, `libxkbcommon`, `libX11`, `libxcb-*` | no QPA platform; app aborts |
| Fonts | `libfontconfig`, `libfreetype`, `libharfbuzz`, `libfribidi` | no text rendered |
| Audio | `libasound`, `libpulse`, `libpipewire`, `libjack` | no audio output |
| Input | `libSDL3` (gamepad), `libSDL2` (mpv) | gamepad support disabled / mpv load failure |
| System | glibc ≥ 2.43, `libdbus-1`, `libsystemd` | MPRIS and secrets unavailable |
| TLS | `libssl`, `libcrypto`, `libgnutls` | HTTPS to the Emby server fails |

On a normal Arch desktop all of these are already present. `libSDL2` is worth
calling out — it comes from Arch's `sdl2-compat`, which is not in `base`.

## What is bundled

Qt 6.11 (Core/Gui/Quick/QuickControls2/Qml/Network/DBus/OpenGL/WaylandClient),
the Qt QPA plugins `qwayland`, `qxcb`, `qoffscreen`, `qminimal` plus the Wayland
shell/decoration/graphics integrations, the imageformats Emby actually needs
(jpeg, png, gif, webp, svg, ico), `libmpv` and its codec closure (ffmpeg,
libplacebo, libass, libzimg, dav1d, x264/x265, …), and `libstdc++`/`libgcc_s`.
139 shared objects, ~233 MB uncompressed, ~99 MB after zstd squashfs.

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
APPDIR=build/appimage/AppDir

# Bundled Qt/mpv resolve into the AppDir; GL/fontconfig/libva resolve to the host.
ldd $APPDIR/usr/bin/strmqt | grep -E 'libQt6Core|libmpv|libGL\.so|libfontconfig|libva\.so'

# No bundled object may be missing an $ORIGIN RPATH.
find $APPDIR/usr/lib -type f -name '*.so*' -exec sh -c 'patchelf --print-rpath "$1" | grep -q "\$ORIGIN" || echo "BAD $1"' _ {} \;

# No absolute DT_NEEDED may survive.
find $APPDIR/usr -type f -name '*.so*' -exec patchelf --print-needed {} \; | grep '^/'

# Headless smoke test (this is what qoffscreen is bundled for).
QT_FORCE_STDERR_LOGGING=1 QT_QPA_PLATFORM=offscreen \
  ./build/dist/StrmQt-0.3.0-x86_64.AppImage
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
