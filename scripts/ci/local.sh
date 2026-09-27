#!/usr/bin/env bash
# scripts/ci/local.sh TARGET BUILD_DIR [--] CMD... — run CMD in a TARGET
# container, with this checkout read-only at /src and the host's BUILD_DIR at
# /build (TMPDIR=/build/tmp). BUILD_DIR must be an agent build directory,
# /tmp/w<wave><agent>[-<target>] (AGENTS.md), which the orchestrator deletes.
set -euo pipefail

target=${1:?usage: local.sh TARGET BUILD_DIR [--] CMD...}
build=${2:?usage: local.sh TARGET BUILD_DIR [--] CMD...}
shift 2
[ "${1:-}" = "--" ] && shift
[ $# -gt 0 ] || { echo "local.sh: no command given" >&2; exit 2; }

repo=$(cd "$(dirname "$0")/../.." && pwd)
base=$("$repo/scripts/ci/deps.sh" --image "$target")

# The image depends only on the files copied into it, so its tag is their
# hash: editing deps.sh (or, later, the package files it reads) builds a new
# image instead of silently reusing a stale one.
context=$(mktemp -d)
trap 'rm -rf "$context"' EXIT
for f in scripts/ci/deps.sh packaging/debian/control packaging/rpm/strmqt.spec; do
    [ -f "$repo/$f" ] || continue
    mkdir -p "$context/$(dirname "$f")"
    cp "$repo/$f" "$context/$f"
done
hash=$(cd "$context" && find . -type f -print0 | sort -z | xargs -0 sha256sum | sha256sum | cut -c1-12)
image="localhost/strmqt-ci:${target}-${hash}"

if ! podman image exists "$image"; then
    podman build -t "$image" --build-arg "BASE=$base" --build-arg "TARGET=$target" \
        -f "$repo/scripts/ci/Containerfile" "$context"
fi

mkdir -p "$build/tmp"
podman run --rm --security-opt label=disable \
    -v "$repo:/src:ro" -v "$build:/build" \
    -e TMPDIR=/build/tmp -w /build \
    "$image" "$@"
