#!/usr/bin/env bash
# scripts/ci/install-check.sh PACKAGE — install a built .deb or .rpm into this
# clean container with the package manager resolving every dependency, then
# run the installed binary's self-test. This is what proves the Depends /
# Requires lists, not the build (spec 2026-09-27 §7).
set -euo pipefail
pkg=${1:?usage: install-check.sh PACKAGE}
case "$pkg" in
    *.deb) export DEBIAN_FRONTEND=noninteractive
           apt-get update
           apt-get install -y --no-install-recommends "$(realpath "$pkg")" ;;
    *.rpm) dnf install -y --setopt=install_weak_deps=False "$(realpath "$pkg")" ;;
    *) echo "install-check.sh: not a package: $pkg" >&2; exit 2 ;;
esac
"$(dirname "$0")/selftest.sh" /usr/bin/strmqt
