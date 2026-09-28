#!/usr/bin/env bash
# scripts/ci/package-deb.sh TARGET OUT_DIR — build StrmQt's .deb for TARGET in
# the current container (whose Build-Depends deps.sh installed). The build
# runs in a scratch copy of /src with packaging/debian copied to debian/.
set -euo pipefail
target=${1:?usage: package-deb.sh TARGET OUT_DIR}; out=${2:?usage: package-deb.sh TARGET OUT_DIR}
case "$target" in
    ubuntu-24.04) suffix=ubuntu24.04 ;; ubuntu-26.04) suffix=ubuntu26.04 ;;
    debian-12) suffix=deb12 ;; debian-13) suffix=deb13 ;;
    *) echo "package-deb.sh: not a Debian-family target: $target" >&2; exit 2 ;;
esac
src=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
"$src/scripts/ci/copy-tree.sh" "$src" "$work/strmqt"
cp -r "$work/strmqt/packaging/debian" "$work/strmqt/debian"
cd "$work/strmqt"
# 0.7.5-1 -> 0.7.5-1~<suffix>: one upload per release, ordered below a real Debian upload.
version=$(dpkg-parsechangelog -S Version)
sed -i "1s/($version)/($version~$suffix)/" debian/changelog
dpkg-buildpackage -b -us -uc
mkdir -p "$out"
cp ../strmqt_*.deb "$out"/
lintian --fail-on error --info "$out"/strmqt_*~"$suffix"_amd64.deb
ls -l "$out"
