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
    *) echo "deps.sh: unknown target '$target'" >&2; exit 2 ;;
esac

cmake_args=""

case "$mode" in
    image) echo "$image"; exit 0 ;;
    cmake-args) echo "$cmake_args"; exit 0 ;;
esac

# Debian family. qt6-tools-dev is deliberately absent: nothing here uses Qt
# Tools, and bookworm's is a mismatched 6.4.2~rc1.
apt_common=(
    build-essential cmake ninja-build pkg-config git ca-certificates file
    qt6-base-dev qt6-base-dev-tools qt6-declarative-dev qt6-declarative-dev-tools
    qt6-websockets-dev qt6-svg-dev qt6-wayland qt6-qpa-plugins
    qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-templates
    qml6-module-qtquick-window qml6-module-qtqml-workerscript qml6-module-qtqml
    libmpv-dev libvlc-dev vlc-plugin-base vlc-plugin-video-output libssl-dev
    ffmpeg libgl1-mesa-dri
)
case "$target" in
    # Qt 6.4.2: no QtQuick.Effects (compat tier), no SDL3 package, and the SVG
    # image plugin ships inside libqt6svg6 (pulled by qt6-svg-dev).
    ubuntu-24.04|debian-12)
        apt_extra=(qml6-module-qt5compat-graphicaleffects libudev-dev) ;;
    # Qt 6.8+: full tier; the SVG plugin is its own package (study §2.5).
    ubuntu-26.04|debian-13)
        apt_extra=(qml6-module-qtquick-effects qt6-svg-plugins libsdl3-dev) ;;
esac

dnf_pkgs=(
    gcc-c++ cmake ninja-build pkgconf git-core file
    qt6-qtbase-devel qt6-qtbase-private-devel qt6-qtdeclarative-devel
    qt6-qtwebsockets-devel qt6-qtsvg-devel qt6-qtsvg qt6-qtwayland
    mpv-devel vlc-devel vlc-plugins-base vlc-plugin-ffmpeg SDL3-devel openssl-devel
    ffmpeg-free mesa-dri-drivers
)

case "$target" in
    ubuntu-*|debian-*)
        export DEBIAN_FRONTEND=noninteractive
        apt-get update
        apt-get install -y --no-install-recommends "${apt_common[@]}" "${apt_extra[@]}"
        ;;
    fedora-*)
        dnf install -y --setopt=install_weak_deps=False "${dnf_pkgs[@]}"
        ;;
esac
