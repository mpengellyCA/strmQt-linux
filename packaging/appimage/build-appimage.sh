#!/usr/bin/env bash
# build-appimage.sh — build StrmQt as a single-file AppImage.
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Usage:  ./packaging/appimage/build-appimage.sh
# Output: build/dist/StrmQt-<version>-x86_64.AppImage
#
# Read packaging/appimage/README.md before shipping this to anyone: the bundled
# ffmpeg imports GLIBC_2.43, so this artifact is NOT portable across distros.
# It is a convenience build for current Arch. Flatpak is the portable path.

set -euo pipefail

APP_NAME="StrmQt"
APP_ID="ca.mikesdev.StrmQt"
APP_ARCH="x86_64"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
# Read from the project definition rather than restated here. Hardcoding it
# shipped a "StrmQt-0.2.0" AppImage built from 0.3.0-rc1 sources — the one
# artifact of the three whose version is in its filename and nowhere else.
# Must come after SRC_DIR: under `set -u` an early read would abort the script.
APP_VERSION="$(sed -n 's/^project(StrmQt VERSION \([0-9.]*\).*/\1/p' \
    "${SRC_DIR}/CMakeLists.txt")"
[ -n "${APP_VERSION}" ] || { printf 'ERROR: cannot read project version from %s\n' \
    "${SRC_DIR}/CMakeLists.txt" >&2; exit 1; }
BUILD_DIR="${SRC_DIR}/build/appimage"
APPDIR="${BUILD_DIR}/AppDir"
DIST_DIR="${SRC_DIR}/build/dist"
OUTPUT="${DIST_DIR}/${APP_NAME}-${APP_VERSION}-${APP_ARCH}.AppImage"

log()  { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m warn:\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31mERROR:\033[0m %s\n' "$*" >&2; exit 1; }

# ─── 1. Preflight ─────────────────────────────────────────────────────────────
# Fail here, loudly, rather than three minutes into a compile.
log "Preflight"

missing=()
for tool in cmake ninja patchelf appimagetool; do
    command -v "${tool}" >/dev/null 2>&1 || missing+=("${tool}")
done
if ((${#missing[@]})); then
    die "missing required tool(s): ${missing[*]}
  cmake/ninja  : pacman -S cmake ninja
  patchelf     : pacman -S patchelf
  appimagetool : https://github.com/AppImage/appimagetool/releases  (drop in ~/.local/bin, chmod +x)"
fi

command -v pkg-config >/dev/null 2>&1 || die "pkg-config not found"
pkg-config --exists mpv \
    || die "libmpv development files not found (pkg-config 'mpv'). Install mpv/libmpv."
log "  mpv $(pkg-config --modversion mpv), $(cmake --version | head -1), patchelf $(patchelf --version 2>&1 | head -1)"

# ─── 2. Configure + build ─────────────────────────────────────────────────────
# CMAKE_INSTALL_RPATH is set at configure time so OUR binaries are linked with
# $ORIGIN/../lib from the start. Qt's deployed libraries are NOT covered by this
# (Qt copies them verbatim) -- that is what patchelf pass 2 below is for.
#
# STRMQT_WITH_VLC=OFF: the AppImage is mpv-only. libVLC is a plugin-directory
# architecture (dlopen of ~500 modules under a compiled-in path) that does not
# survive relocation into an AppDir without a VLC_PLUGIN_PATH dance, and mpv is
# the primary engine anyway (PLAN §3.2).
#
# STRMQT_WERROR=OFF: a packaging build must not fail because a new Qt or GCC
# added a warning. Warnings-as-errors belongs in the dev preset.
#
# QT_DEPLOY_USE_PATCHELF=ON is NOT optional. Qt fixes up deployed plugin RPATHs
# via _qt_internal_set_rpath(), which by default calls CMake's internal
# file(RPATH_SET). That call can only REWRITE an existing DT_RPATH/DT_RUNPATH --
# it cannot create one. Several Qt Wayland plugins ship with no RPATH entry at
# all, so the install dies with:
#
#     file RPATH_SET could not write new RPATH: $ORIGIN/../../../
#       to .../wayland-shell-integration/liblayer-shell.so
#       No valid ELF RPATH or RUNPATH entry exists in the file
#
# Setting this makes Qt shell out to `patchelf --set-rpath` instead
# (Qt6CoreDeploySupport.cmake:124-136), which adds the entry when absent.
log "Configuring in ${BUILD_DIR}"
rm -rf "${BUILD_DIR}" "${OUTPUT}"
cmake -S "${SRC_DIR}" -B "${BUILD_DIR}" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_INSTALL_RPATH='$ORIGIN/../lib' \
    -DQT_DEPLOY_USE_PATCHELF=ON \
    -DSTRMQT_APPIMAGE_DEPLOY=ON \
    -DSTRMQT_WERROR=OFF \
    -DSTRMQT_WITH_VLC=OFF

log "Building"
cmake --build "${BUILD_DIR}"

log "Installing into AppDir (this also runs Qt's deploy step)"
rm -rf "${APPDIR}"
DESTDIR="${APPDIR}" cmake --install "${BUILD_DIR}"

[[ -x "${APPDIR}/usr/bin/strmqt" ]]     || die "strmqt did not install into the AppDir"
[[ -x "${APPDIR}/usr/bin/strmqt-cli" ]] || die "strmqt-cli did not install into the AppDir"

# ─── 3. The denylist ─────────────────────────────────────────────────────────
# Sonames that must resolve to the HOST at runtime. Kept in lockstep with
# _strmqt_pre_exclude_regexes in Deploy.cmake -- the rationale for each group
# lives there. Defined here (not at the assertion) because step 4 uses the same
# list to prune the mpv dependency walk, which makes the walk and the assertion
# consistent by construction rather than by vigilance. ERE, matched on basename.
#
# TRAP, hit for real on the first build: a character class before \.so MUST
# include '-' and '_'. `libX[A-Za-z0-9]*\.so` matches libX11.so.6 but NOT
# libX11-xcb.so.1, so the X11<->xcb bridge (needed by libQt6XcbQpa.so) sailed
# past both this assertion and the Deploy.cmake exclude list. Likewise `^libva`
# unanchored swallows libvapoursynth-script.so.0, which we DO want bundled.
# When adding a pattern here, add the identical one to Deploy.cmake.
FORBIDDEN='^(ld-linux|libc\.so|libm\.so|libmvec\.|libdl\.so|libpthread\.so|librt\.so|libutil\.so|libresolv\.|libanl\.|libnsl|libnss_)'
FORBIDDEN+='|^(libGL\.so|libGLX|libGLdispatch|libOpenGL|libGLESv|libEGL|libglapi|libgbm|libdrm|libLLVM)'
FORBIDDEN+='|^(libva\.so|libva-|libvdpau|libvulkan|libOpenCL|libnvidia|libcuda|libnvcuvid)'
FORBIDDEN+='|^(libwayland-|libdecor|libxkbcommon|libxkbfile|libxcb|libX[A-Za-z0-9_-]*\.so|libICE|libSM)'
FORBIDDEN+='|^(libasound|libpulse|libpipewire|libspa-|libjack|libSDL)'
FORBIDDEN+='|^(libfontconfig|libfreetype|libharfbuzz|libfribidi|libgraphite2|libexpat)'
FORBIDDEN+='|^(libglib-|libgobject-|libgio-|libgmodule-|libgthread-|libgdk_pixbuf|libgdk-|libgtk-|libcairo|libpango|libatk)'
FORBIDDEN+='|^(libssl|libcrypto|libgnutls|libnettle|libhogweed|libgmp\.so|libp11-kit|libtasn1|libgcrypt|libgpg-error)'
FORBIDDEN+='|^(libkrb5|libk5crypto|libgssapi_krb5|libkrb5support|libcom_err|libkeyutils|libsasl2|libldap|liblber)'
FORBIDDEN+='|^(libsystemd|libudev|libdbus-1|libcap\.so|libcap-|libselinux|libapparmor|libseccomp|libelf)'
FORBIDDEN+='|^(libz\.so|libzstd|liblzma|libbz2|liblz4|libbrotli)'

# ─── 4. Bundle libmpv and its codec closure by hand ──────────────────────────
# WHY THIS IS NOT DONE BY Qt's DEPLOY STEP
#
# Deploy.cmake excludes ^libmpv from file(GET_RUNTIME_DEPENDENCIES), not because
# we don't want mpv (we absolutely do) but because CMake cannot scan it. Arch's
# libmpv.so.2 records an absolute DT_NEEDED, [/usr/lib/libmujs.so], and CMake
# aborts the entire install with "file Paths to dependencies are not supported"
# rather than skipping the entry. So the media stack is bundled here.
#
# The walk is a BFS over DT_NEEDED that PRUNES at the denylist: when we decline
# to bundle, say, libglib-2.0.so.0, we also stop descending through it. That
# matters -- a flat `ldd` closure of libmpv lists 202 libraries, including whole
# subtrees (gdk-pixbuf -> glycin, cairo -> pixman, ...) that are only reachable
# through host-owned libraries we are not shipping. Pruning keeps those out.
log "Bundling libmpv and its dependency closure"

MPV_ROOT="$(pkg-config --variable=libdir mpv 2>/dev/null || echo /usr/lib)/libmpv.so.2"
[[ -f "${MPV_ROOT}" ]] || MPV_ROOT=/usr/lib/libmpv.so.2
[[ -f "${MPV_ROOT}" ]] || die "cannot locate libmpv.so.2 on the host"

# Resolve soname -> host path once, using the real loader. More trustworthy than
# reimplementing ld.so search order in bash.
declare -A RESOLVE=()
while read -r name _arrow path _addr; do
    [[ "${path}" == /* ]] || continue
    RESOLVE["${name}"]="${path}"
done < <(ldd "${MPV_ROOT}" 2>/dev/null | sed 's/^[[:space:]]*//')

declare -A SEEN=()
queue=("${MPV_ROOT}")
SEEN["$(basename "${MPV_ROOT}")"]=1
mpv_copied=0
mpv_pruned=0

# Root first: libmpv itself is never a "dependency" of anything we scanned.
cp -L "${MPV_ROOT}" "${APPDIR}/usr/lib/libmpv.so.2"
chmod u+w "${APPDIR}/usr/lib/libmpv.so.2"
mpv_copied=$((mpv_copied + 1))

while ((${#queue[@]})); do
    cur="${queue[0]}"; queue=("${queue[@]:1}")
    deps="$(patchelf --print-needed "${cur}" 2>/dev/null || true)"
    [[ -n "${deps}" ]] || continue
    while IFS= read -r dep; do
        [[ -n "${dep}" ]] || continue
        base="$(basename "${dep}")"          # tolerate absolute DT_NEEDED here
        [[ -n "${SEEN[${base}]:-}" ]] && continue
        SEEN["${base}"]=1
        if [[ "${base}" =~ ${FORBIDDEN} ]]; then
            mpv_pruned=$((mpv_pruned + 1))   # host-owned: do not copy, do not descend
            continue
        fi
        host="${RESOLVE[${base}]:-}"
        if [[ -z "${host}" && -f "/usr/lib/${base}" ]]; then
            host="/usr/lib/${base}"          # absolute-NEEDED targets ldd may not list
        fi
        [[ -n "${host}" && -f "${host}" ]] || { warn "unresolved mpv dependency: ${base}"; continue; }
        if [[ ! -e "${APPDIR}/usr/lib/${base}" ]]; then
            cp -L "${host}" "${APPDIR}/usr/lib/${base}"
            chmod u+w "${APPDIR}/usr/lib/${base}"
            mpv_copied=$((mpv_copied + 1))
        fi
        queue+=("${APPDIR}/usr/lib/${base}")
    done <<< "${deps}"
done
log "  ${mpv_copied} librar(y/ies) bundled, ${mpv_pruned} pruned as host-owned"

# ─── 5. patchelf pass 1: absolute DT_NEEDED -> bare soname ────────────────────
# Arch's libmpv.so.2 records:
#     0x0000000000000001 (NEEDED)  Shared library: [/usr/lib/libmujs.so]
# An *absolute* DT_NEEDED is opened by path directly: the loader skips DT_RPATH,
# DT_RUNPATH and LD_LIBRARY_PATH entirely. Inside the AppImage that resolves to
# the HOST's /usr/lib/libmujs.so, which may not exist -- and if it does, we get a
# host library mixed into our bundled mpv. Rewriting it to a bare soname puts it
# back under normal RPATH resolution so pass 2's $ORIGIN can find our copy.
log "patchelf pass 1: rewriting absolute DT_NEEDED entries"
abs_needed_fixed=0
while IFS= read -r -d '' obj; do
    # Skip non-ELF quickly; patchelf on a script is noisy garbage.
    head -c 4 "${obj}" | grep -q $'\x7fELF' || continue
    needed="$(patchelf --print-needed "${obj}" 2>/dev/null || true)"
    [[ -n "${needed}" ]] || continue
    while IFS= read -r dep; do
        [[ "${dep}" == /* ]] || continue
        base="$(basename "${dep}")"
        log "  ${obj#"${APPDIR}"/}: ${dep} -> ${base}"
        patchelf --replace-needed "${dep}" "${base}" "${obj}"
        abs_needed_fixed=$((abs_needed_fixed + 1))
        # The absolute NEEDED bypassed dependency scanning, so the target may not
        # have been copied in. Pull it in if it is missing.
        if [[ ! -e "${APPDIR}/usr/lib/${base}" && -e "${dep}" ]]; then
            log "    (also copying ${dep} -- it was skipped by dependency scanning)"
            cp -L "${dep}" "${APPDIR}/usr/lib/${base}"
        fi
    done <<< "${needed}"
done < <(find "${APPDIR}/usr" -type f \( -name '*.so' -o -name '*.so.*' -o -perm -u+x \) -print0)
log "  rewrote ${abs_needed_fixed} absolute DT_NEEDED entr(y/ies)"

# ─── 6. patchelf pass 2: $ORIGIN RPATH on everything bundled ──────────────────
# Qt deploys libraries with a plain file(INSTALL ... FOLLOW_SYMLINK_CHAIN)
# (Qt6CoreDeploySupport.cmake:566) and never touches their RPATH. A bundled
# libQt6Quick.so therefore still has whatever RPATH the distro build gave it --
# usually none -- so it resolves libQt6Gui.so through the default search path
# and silently binds to the HOST's Qt. Two Qt copies in one process is a fast
# crash at best.
#
# Qt DOES fix up plugin RPATHs (lines 592-602), so those are handled; we only
# top up anything that slipped through.
log "patchelf pass 2: setting \$ORIGIN RPATHs"

# 4a. Top-level bundled libraries: siblings of each other.
lib_count=0
while IFS= read -r -d '' lib; do
    patchelf --set-rpath '$ORIGIN' "${lib}"
    lib_count=$((lib_count + 1))
done < <(find "${APPDIR}/usr/lib" -maxdepth 1 -type f \( -name '*.so' -o -name '*.so.*' \) -print0)
log "  ${lib_count} top-level libraries -> \$ORIGIN"

# 4b. Our own binaries.
for exe in strmqt strmqt-cli; do
    patchelf --set-rpath '$ORIGIN/../lib' "${APPDIR}/usr/bin/${exe}"
done
log "  2 binaries -> \$ORIGIN/../lib"

# 4c. Defence in depth for plugin / QML .so files. Qt should already have set
#     these; we only touch ones that have no $ORIGIN, and we compute the correct
#     relative hop rather than assuming a depth. Idempotent by construction.
plugin_fixed=0
while IFS= read -r -d '' so; do
    cur="$(patchelf --print-rpath "${so}" 2>/dev/null || true)"
    [[ "${cur}" == *'$ORIGIN'* ]] && continue
    rel="$(realpath --relative-to="$(dirname "${so}")" "${APPDIR}/usr/lib")"
    patchelf --set-rpath "\$ORIGIN/${rel}" "${so}"
    plugin_fixed=$((plugin_fixed + 1))
done < <(find "${APPDIR}/usr/lib" -mindepth 2 -type f -name '*.so' -print0)
log "  ${plugin_fixed} plugin/QML object(s) needed an RPATH top-up"

# ─── 7. HARD ASSERTION: no host-owned library may be inside the AppDir ────────
# Denylists rot. Qt gains a dependency, ffmpeg gains a dependency, and a lib we
# never intended to ship appears in usr/lib. This check is the tripwire.
#
# If it fires: fix packaging/appimage/Deploy.cmake -- add the soname to
# _strmqt_pre_exclude_regexes and rebuild.
#
# DO NOT "fix" it by deleting the file from the AppDir. By the time it exists,
# every dependant was already resolved against it and had its own dependencies
# walked from it. Deleting it leaves dangling DT_NEEDED entries that fail at
# dlopen time, in a code path nobody tests, months later.
log "Asserting no host-owned libraries were bundled"

violations="$(find "${APPDIR}/usr/lib" -maxdepth 1 \( -name '*.so' -o -name '*.so.*' \) -printf '%f\n' \
    | grep -E "${FORBIDDEN}" | sort -u || true)"

if [[ -n "${violations}" ]]; then
    printf '\033[1;31mERROR:\033[0m host-owned libraries were bundled into the AppDir:\n' >&2
    printf '  %s\n' ${violations} >&2
    cat >&2 <<'EOF'

These libraries must come from the HOST at runtime. Bundling them produces an
AppImage that cannot create a GL context, cannot hardware-decode, renders no
text, or crashes on a mismatched libc -- depending on which one leaked.

FIX: add the soname to _strmqt_pre_exclude_regexes in
     packaging/appimage/Deploy.cmake, then re-run this script.

DO NOT delete the file from the AppDir. Its dependants were already resolved
against it during dependency scanning; removing it leaves dangling DT_NEEDED
entries that fail at dlopen time instead of at build time.
EOF
    exit 1
fi
log "  clean: no forbidden sonames in ${APPDIR}/usr/lib"

# ─── 8. Positive assertions ──────────────────────────────────────────────────
# The negative check above catches over-bundling. These catch under-bundling,
# which is worse because it fails silently: without libqwayland.so the app still
# starts -- under XWayland, blurry, with no native fullscreen. Nobody notices
# until a user reports "it looks fuzzy".
log "Asserting required components are present"

PLUGINS_DIR="${APPDIR}/usr/lib/qt6/plugins"

[[ -f "${PLUGINS_DIR}/platforms/libqwayland.so" ]] || die \
"libqwayland.so is MISSING from the AppDir.

Qt selects QPA plugins from /usr/lib/qt6/modules/Gui.json, whose qpa.platforms
array on this distro contains ONLY \"xcb\" -- and Qt6CoreDeploySupport.cmake:360
strips the 'platforms' type from bulk selection. Without an explicit
'INCLUDE_PLUGINS qwayland' in packaging/appimage/Deploy.cmake, the AppImage
ships the xcb plugin alone and runs silently under XWayland.

FIX: restore 'INCLUDE_PLUGINS qwayland' in packaging/appimage/Deploy.cmake."

[[ -d "${PLUGINS_DIR}/wayland-shell-integration" ]] || die \
"wayland-shell-integration/ is MISSING from the AppDir.

The Wayland QPA plugin dlopens a shell integration (xdg-shell) at runtime. With
libqwayland.so present but no shell integration, Qt aborts the Wayland platform
and falls back to xcb -- the same silent XWayland outcome the check above exists
to prevent.

FIX: ensure 'wayland-shell-integration' is not in _strmqt_exclude_plugin_types
in packaging/appimage/Deploy.cmake."

[[ -f "${APPDIR}/usr/lib/libmpv.so.2" ]] || die \
"libmpv.so.2 is MISSING from the AppDir. Playback would be impossible.
Check that PkgConfig::MPV is still linked into the strmqt target and that no
PRE_EXCLUDE_REGEXES entry in Deploy.cmake accidentally matches 'libmpv'."

log "  libqwayland.so, wayland-shell-integration/, libmpv.so.2 all present"

# ─── 9. AppDir metadata ──────────────────────────────────────────────────────
log "Populating AppDir metadata"

install -m 0644 "${APPDIR}/usr/share/applications/${APP_ID}.desktop" \
                "${APPDIR}/${APP_ID}.desktop"

# appimagetool wants a top-level icon matching the desktop entry's Icon= key,
# plus a .DirIcon for file managers. 256x256 is the sweet spot: large enough for
# a HiDPI launcher, small enough not to bloat the header.
ICON_SRC="${APPDIR}/usr/share/icons/hicolor/256x256/apps/${APP_ID}.png"
[[ -f "${ICON_SRC}" ]] || die "256x256 icon missing at ${ICON_SRC}"
install -m 0644 "${ICON_SRC}" "${APPDIR}/${APP_ID}.png"
cp "${ICON_SRC}" "${APPDIR}/.DirIcon"

install -m 0755 "${SCRIPT_DIR}/AppRun" "${APPDIR}/AppRun"

# GPL-3.0-or-later obliges us to convey the licence with the binary. An AppImage
# is a self-contained filesystem: it does not inherit Arch's shared
# /usr/share/licenses/common/GPL3, so the text has to travel with us.
install -Dm 0644 "${SRC_DIR}/COPYING" \
                 "${APPDIR}/usr/share/licenses/${APP_ID}/COPYING"

# ─── 10. Pack ─────────────────────────────────────────────────────────────────
# zstd: noticeably faster startup than the legacy gzip squashfs for a bundle
# this size, at a comparable ratio.
log "Packing ${OUTPUT}"
mkdir -p "${DIST_DIR}"
ARCH="${APP_ARCH}" VERSION="${APP_VERSION}" \
    appimagetool --comp zstd "${APPDIR}" "${OUTPUT}"

chmod +x "${OUTPUT}"
log "Done: ${OUTPUT} ($(du -h "${OUTPUT}" | cut -f1))"
printf '\n'
printf 'Portability note: the bundled ffmpeg imports GLIBC_2.43. This AppImage\n'
printf 'runs on current Arch and very little else. See packaging/appimage/README.md.\n'
