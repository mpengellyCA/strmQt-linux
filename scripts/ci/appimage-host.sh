#!/usr/bin/env bash
# scripts/ci/appimage-host.sh TARGET — make a bare container into the minimal
# host packaging/appimage/README.md says the AppImage needs, so the release
# tests that table instead of trusting it (spec 2026-09-27 §7.4).
#
# The lists follow the rows of the README's "What the host must provide"
# table, in order; glibc is in every base image. Package names were looked up
# per soname (apt-file in debian:trixie, dnf repoquery --whatprovides in
# fedora:43). Ubuntu 24.04 uses the same names as Debian 13.
set -euo pipefail
case "${1:?usage: appimage-host.sh TARGET}" in
    debian-13|ubuntu-24.04)
        export DEBIAN_FRONTEND=noninteractive
        apt-get update
        apt-get install -y --no-install-recommends \
            libgl1 libegl1 libglx-mesa0 libegl-mesa0 libgl1-mesa-dri libgbm1 libdrm2 \
            libva2 libva-drm2 libva-wayland2 libva-x11-2 libvdpau1 libvulkan1 ocl-icd-libopencl1 \
            libwayland-client0 libwayland-cursor0 libwayland-egl1 libxkbcommon0 libxkbcommon-x11-0 \
            libx11-6 libx11-xcb1 libxext6 libxrandr2 libxss1 libxv1 libxpresent1 \
            libxcb1 libxcb-cursor0 libxcb-glx0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 \
            libxcb-randr0 libxcb-render0 libxcb-render-util0 libxcb-shape0 libxcb-shm0 \
            libxcb-sync1 libxcb-util1 libxcb-xfixes0 libxcb-xkb1 \
            libfontconfig1 libfreetype6 libharfbuzz0b libfribidi0 \
            libglib2.0-0t64 libcairo2 libcairo-gobject2 libpango-1.0-0 libpangocairo-1.0-0 \
            libgdk-pixbuf-2.0-0 \
            libasound2t64 libpulse0 libpipewire-0.3-0t64 libjack-jackd2-0 libsdl2-2.0-0 \
            libssl3t64 libgnutls30t64 libnettle8t64 libgcrypt20 libgpg-error0 libgssapi-krb5-2 \
            libdbus-1-3 libudev1 \
            zlib1g libzstd1 liblzma5 liblz4-1 libbrotli1 ;;
    fedora-43)
        dnf install -y --setopt=install_weak_deps=False \
            libglvnd-glx libglvnd-egl mesa-libGL mesa-libEGL mesa-dri-drivers mesa-libgbm libdrm \
            libva libvdpau vulkan-loader OpenCL-ICD-Loader \
            libwayland-client libwayland-cursor libwayland-egl libxkbcommon libxkbcommon-x11 \
            libX11 libX11-xcb libXext libXrandr libXScrnSaver libXv libXpresent \
            libxcb xcb-util-cursor xcb-util-wm xcb-util-image xcb-util-keysyms \
            xcb-util-renderutil xcb-util \
            fontconfig freetype harfbuzz fribidi \
            glib2 cairo cairo-gobject pango \
            gdk-pixbuf2 \
            alsa-lib pulseaudio-libs pipewire-libs pipewire-jack-audio-connection-kit-libs sdl2-compat \
            openssl-libs gnutls nettle libgcrypt libgpg-error krb5-libs \
            dbus-libs systemd-libs \
            zlib-ng-compat libzstd xz-libs lz4-libs libbrotli ;;
    *) echo "appimage-host.sh: no host list for $1" >&2; exit 2 ;;
esac
