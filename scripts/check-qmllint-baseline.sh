#!/usr/bin/env bash

set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 || ( $# -eq 2 && $2 != "--update" ) ]]; then
    echo "usage: $0 BUILD_DIR [--update]" >&2
    exit 2
fi

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
build_dir=$1
baseline="$repo_root/config/qmllint-baseline.txt"
log=$(mktemp)
normalized=$(mktemp)
trap 'rm -f "$log" "$normalized"' EXIT

# qmllint currently returns success for warnings. Preserve its full output in
# the job log, then compare normalized warning records independently.
cmake --build "$build_dir" --target strmqt_qmllint 2>&1 | tee "$log"

# Keep the warning text and the source statement that triggered it, but remove
# absolute checkout paths and line/column numbers. Sorting preserves duplicate
# warnings while making the result insensitive to qmllint traversal order.
awk -v root="$repo_root/" '
    /^Warning: / {
        header = $0
        sub("^Warning: " root, "", header)
        sub("^Warning: ", "", header)
        sub(/:[0-9]+:[0-9]+:/, ":", header)
        source = ""
        if ((getline source) > 0)
            sub(/^[[:space:]]+/, "", source)
        print header "\t" source
    }
' "$log" | LC_ALL=C sort > "$normalized"

if [[ ${2:-} == "--update" ]]; then
    mkdir -p "$(dirname -- "$baseline")"
    cp "$normalized" "$baseline"
    echo "Updated $baseline ($(wc -l < "$baseline") warnings)."
    exit 0
fi

if [[ ! -f $baseline ]]; then
    echo "qmllint baseline is missing; review warnings and run:" >&2
    echo "  $0 $build_dir --update" >&2
    exit 1
fi

if ! diff -u "$baseline" "$normalized"; then
    echo "qmllint warning set changed." >&2
    echo "Fix new warnings, or review the complete diff and update the baseline intentionally:" >&2
    echo "  $0 $build_dir --update" >&2
    exit 1
fi

echo "qmllint warning baseline matches ($(wc -l < "$normalized") warnings)."
