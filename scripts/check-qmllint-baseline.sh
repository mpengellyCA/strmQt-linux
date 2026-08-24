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

# Independent of the baseline, and checked before it: these categories mean a
# QML type will not resolve at runtime, which is how a broken page ships from a
# clean build. Comparing to a baseline alone would let one be accepted by an
# --update, so the fatal set is never baselineable.
if grep -E 'is not a type|was not found|unavailable|incompatible-type' "$log"; then
    echo "qmllint found unresolvable QML types (never baselineable)." >&2
    exit 1
fi

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
    added=$(comm -13 "$baseline" "$normalized" | wc -l)
    removed=$(comm -23 "$baseline" "$normalized" | wc -l)
    baseline_count=$(wc -l < "$baseline")
    echo "qmllint warning set changed: +$added / -$removed against $baseline_count baselined." >&2
    if (( added + removed >= baseline_count )); then
        echo "The entire set moved, which usually means the qmllint version changed" >&2
        echo "rather than the QML. Confirm the Qt version before re-baselining." >&2
    fi
    echo "Fix new warnings, or review the complete diff and update the baseline intentionally:" >&2
    echo "  $0 $build_dir --update" >&2
    exit 1
fi

echo "qmllint warning baseline matches ($(wc -l < "$normalized") warnings)."
