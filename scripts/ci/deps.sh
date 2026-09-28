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
case "$target" in
    # No SDL3 package on these releases (spec §5): bundle the pinned static one.
    ubuntu-24.04|debian-12) cmake_args="-DSTRMQT_BUNDLE_SDL3=ON" ;;
esac

case "$mode" in
    image) echo "$image"; exit 0 ;;
    cmake-args) echo "$cmake_args"; exit 0 ;;
esac

# Debian family: packaging/debian/control's Build-Depends is the single list
# (installed below with mk-build-deps). qt6-tools-dev is deliberately absent:
# nothing here uses Qt Tools, and bookworm's is a mismatched 6.4.2~rc1.

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
        apt-get install -y --no-install-recommends devscripts equivs lintian git ca-certificates
        control=$(cd "$(dirname "$0")/../.." && pwd)/packaging/debian/control
        work=$(mktemp -d)
        (cd "$work" && mk-build-deps --install --remove \
            --tool 'apt-get -y --no-install-recommends' "$control")
        rm -rf "$work"
        ;;
    fedora-*)
        dnf install -y --setopt=install_weak_deps=False "${dnf_pkgs[@]}"
        ;;
esac
