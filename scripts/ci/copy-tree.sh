#!/usr/bin/env bash
# scripts/ci/copy-tree.sh SRC DEST — copy the source tree as git sees it
# (tracked plus untracked-but-not-ignored files that exist), so a package
# build never sees build directories, local packages or editor state.
set -euo pipefail
src=${1:?usage: copy-tree.sh SRC DEST}; dest=${2:?usage: copy-tree.sh SRC DEST}
mkdir -p "$dest"
git -c safe.directory='*' -C "$src" ls-files -z --cached --others --exclude-standard |
    while IFS= read -r -d '' f; do [ -e "$src/$f" ] && printf '%s\0' "$f"; done |
    tar -C "$src" --null -T - -cf - | tar -C "$dest" -xf -
