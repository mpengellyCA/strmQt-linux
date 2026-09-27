#!/usr/bin/env bash
# scripts/ci/check.sh BUILD_DIR [CMAKE_ARGS...] — configure, build, test,
# self-test and install-check the StrmQt tree this script lives in. It is CI's
# build job (ci.yml) and a developer's container check (local.sh) at once.
#
# Warnings are not errors here: on an old toolchain they come from Qt's own
# headers as often as from ours. The dev preset keeps -Werror on the current Qt.
set -euo pipefail

src=$(cd "$(dirname "$0")/../.." && pwd)
build=${1:?usage: check.sh BUILD_DIR [CMAKE_ARGS...]}
shift
mkdir -p "$build/tmp"
export TMPDIR="$build/tmp"

cmake -S "$src" -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=/usr \
    -DSTRMQT_WERROR=OFF "$@"
cmake --build "$build"
ctest --test-dir "$build" --output-on-failure

"$src/scripts/ci/selftest.sh" "$build/strmqt" "$build/selftest.log"

# The QML module is compiled into the executable. Loose QML, a qmldir or a
# static library in the install tree means the install rules drifted.
stage="$build/stage"
rm -rf "$stage"
DESTDIR="$stage" cmake --install "$build" >/dev/null
if find "$stage" \( -name '*.qml' -o -name qmldir -o -name '*.a' -o -path '*/include/*' \) | grep .; then
    echo "check.sh: unexpected files in the install tree" >&2
    exit 1
fi

tier=$(cat "$build/strmqt-qml-tier.txt" 2>/dev/null || echo "tier n/a")
echo "check.sh: OK ($tier)"
