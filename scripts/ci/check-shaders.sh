#!/usr/bin/env bash
# scripts/ci/check-shaders.sh ROOT — assert that the upscaling shaders and
# their licence texts are installed under ROOT/share/strmqt/shaders (spec
# 2026-10-03 §6). ROOT is an install prefix: /usr, a DESTDIR stage's usr, or
# an AppDir's usr. The list is deliberately independent of CMakeLists.txt's:
# a file dropped from both must still fail here.
set -euo pipefail
root=${1:?usage: check-shaders.sh ROOT}
dir="$root/share/strmqt/shaders"
missing=0
for f in FSR.glsl FSR.LICENSE NVScaler.glsl NVScaler.LICENSE \
         FSRCNNX_x2_16-0-4-1.glsl FSRCNNX_x2_8-0-4-1.glsl FSRCNNX.LICENSE \
         Anime4K_Upscale_CNN_x2_VL.glsl Anime4K_Upscale_CNN_x2_M.glsl Anime4K.LICENSE; do
    if [ ! -s "$dir/$f" ]; then
        echo "check-shaders.sh: missing $dir/$f" >&2
        missing=1
    fi
done
[ "$missing" -eq 0 ] || exit 1
echo "check-shaders.sh: OK ($dir)"
