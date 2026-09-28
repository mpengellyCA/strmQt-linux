#!/usr/bin/env bash
# scripts/ci/deps.sh — what each target distro needs to build, test and
# self-test StrmQt. The single list for CI (ci.yml, packages.yml) and for local
# container checks (local.sh). Package names verified 2026-09-27 in each
# release's container (spec 2026-09-27 §2).
#
#   deps.sh TARGET               install build + test dependencies (as root, in a TARGET container)
#   deps.sh --image TARGET       print TARGET's container image
#   deps.sh --cmake-args TARGET  print TARGET's extra CMake arguments
set -euo pipefail

mode=install
case "${1:-}" in
    --image|--cmake-args) mode=${1#--}; shift ;;
esac
target=${1:?usage: deps.sh [--image|--cmake-args] TARGET}

case "$target" in
    ubuntu-24.04) image=docker.io/library/ubuntu:24.04 ;;
    ubuntu-26.04) image=docker.io/library/ubuntu:26.04 ;;
    debian-12)    image=docker.io/library/debian:bookworm ;;
    debian-13)    image=docker.io/library/debian:trixie ;;
    fedora-43)    image=registry.fedoraproject.org/fedora:43 ;;
    fedora-44)    image=registry.fedoraproject.org/fedora:44 ;;
    appimage)     image=docker.io/library/ubuntu:24.04 ;;
    *) echo "deps.sh: unknown target '$target'" >&2; exit 2 ;;
esac

cmake_args=""
case "$target" in
    # No SDL3 package on these releases (spec §5): bundle the pinned static one.
    ubuntu-24.04|debian-12) cmake_args="-DSTRMQT_BUNDLE_SDL3=ON" ;;
    appimage) cmake_args="-DSTRMQT_BUNDLE_SDL3=ON -DCMAKE_PREFIX_PATH=/opt/qt/6.11.3/gcc_64" ;;
esac

case "$mode" in
    image) echo "$image"; exit 0 ;;
    cmake-args) echo "$cmake_args"; exit 0 ;;
esac

# Debian family: packaging/debian/control's Build-Depends is the single list
# (installed below with mk-build-deps). qt6-tools-dev is deliberately absent:
# nothing here uses Qt Tools, and bookworm's is a mismatched 6.4.2~rc1.

# Fedora: packaging/rpm/strmqt.spec's BuildRequires is the single list (dnf builddep below).

case "$target" in
    appimage)
        export DEBIAN_FRONTEND=noninteractive
        apt-get update
        # Build tools, libmpv 0.37 + its codec closure (walked by build-appimage.sh),
        # libvlc, SDL3's udev, OpenGL/xkb headers aqt's Qt links against, and Python for aqt.
        apt-get install -y --no-install-recommends \
            build-essential cmake ninja-build pkg-config git ca-certificates file patchelf \
            python3-venv libmpv-dev libvlc-dev libssl-dev libudev-dev libgl-dev libegl-dev \
            libxkbcommon-dev libfontconfig-dev libfreetype-dev libdbus-1-dev \
            vlc-plugin-base ffmpeg libgl1-mesa-dri curl binutils
        python3 -m venv /opt/aqt
        /opt/aqt/bin/pip install --no-cache-dir aqtinstall==3.3.0
        /opt/aqt/bin/aqt install-qt linux desktop 6.11.3 linux_gcc_64 -m qtwebsockets -O /opt/qt
        curl -fsSL -o /usr/local/bin/appimagetool \
            https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage
        echo "ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0  /usr/local/bin/appimagetool" | sha256sum -c -
        chmod +x /usr/local/bin/appimagetool
        # The type2 runtime is the first ELF every user runs; appimagetool would
        # otherwise fetch the latest one, unverified, on every pack.
        mkdir -p /usr/local/share/appimage
        curl -fsSL -o /usr/local/share/appimage/runtime-x86_64 \
            https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64
        echo "2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d  /usr/local/share/appimage/runtime-x86_64" | sha256sum -c -
        ;;
    ubuntu-*|debian-*)
        export DEBIAN_FRONTEND=noninteractive
        apt-get update
        apt-get install -y --no-install-recommends devscripts equivs lintian git ca-certificates
        control=$(cd "$(dirname "$0")/../.." && pwd)/packaging/debian/control
        work=$(mktemp -d)
        (cd "$work" && mk-build-deps --install --remove \
            --tool 'apt-get -y --no-install-recommends' "$control")
        rm -rf "$work"
        ;;
    fedora-*)
        dnf install -y --setopt=install_weak_deps=False dnf-plugins-core rpm-build rpmlint git-core
        dnf builddep -y --setopt=install_weak_deps=False \
            "$(cd "$(dirname "$0")/../.." && pwd)/packaging/rpm/strmqt.spec"
        ;;
esac
