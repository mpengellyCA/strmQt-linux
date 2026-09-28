#!/usr/bin/env bash
# scripts/ci/package-rpm.sh TARGET OUT_DIR — build StrmQt's .rpm for a Fedora
# TARGET in the current container, from a clean copy of /src.
set -euo pipefail
target=${1:?usage: package-rpm.sh TARGET OUT_DIR}; out=${2:?usage: package-rpm.sh TARGET OUT_DIR}
case "$target" in fedora-*) ;; *) echo "package-rpm.sh: not a Fedora target: $target" >&2; exit 2 ;; esac
src=$(cd "$(dirname "$0")/../.." && pwd)
top=$(mktemp -d)
# The rpmbuild tree (sources, BUILD, BUILDROOT) runs to several GB; the RPMs are copied out.
trap 'rm -rf "$top"' EXIT
version=$(rpmspec -q --qf '%{VERSION}\n' --srpm "$src/packaging/rpm/strmqt.spec")
mkdir -p "$top"/{SOURCES,SPECS}
"$src/scripts/ci/copy-tree.sh" "$src" "$top/strmqt-$version"
tar -C "$top" -czf "$top/SOURCES/strmqt-$version.tar.gz" "strmqt-$version"
cp "$src/packaging/rpm/strmqt.spec" "$top/SPECS/"
rpmbuild --define "_topdir $top" -bb "$top/SPECS/strmqt.spec"
mkdir -p "$out"
# A reused OUT_DIR must not hand rpmlint (or a caller's glob) a stale package.
rm -f "$out"/strmqt-*.rpm
find "$top/RPMS" -name 'strmqt-[0-9]*.rpm' -exec cp {} "$out"/ \;
rpmlint -r "$src/packaging/rpm/strmqt.rpmlintrc" "$out"/strmqt-"$version"-*.rpm
ls -l "$out"
