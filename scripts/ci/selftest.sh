#!/usr/bin/env bash
# scripts/ci/selftest.sh BINARY [LOG] — run StrmQt's page-construction self-test
# (ARCHITECTURE.md §7) and judge its LOG as well as its exit code. A missing QML
# module, a missing image plugin or a QML tier mismatch can each leave the
# exit code at 0; the log is where they show. An AppImage works as BINARY
# (pass APPIMAGE_EXTRACT_AND_RUN=1 in the environment).
set -euo pipefail

bin=${1:?usage: selftest.sh BINARY [LOG]}
log=${2:-$(mktemp)}

status=0
# The page lines are console.log, category qml at debug level. Fedora's Qt
# ships /usr/share/qt6/qtlogging.ini with *.debug=false; the environment's
# rules win over that file.
STRMQT_SELFTEST=1 QT_QPA_PLATFORM=offscreen QT_ASSUME_STDERR_HAS_CONSOLE=1 \
    QT_LOGGING_RULES="qml.debug=true" \
    "$bin" >"$log" 2>&1 || status=$?
cat "$log"

if [ "$status" -ne 0 ]; then
    echo "selftest.sh: $bin exited $status" >&2
    exit 1
fi
# "Cannot instantiate bound component" and "Component is not ready" are how Qt
# 6.4's Loader and ListView footer refuse a component declared under `pragma
# ComponentBehavior: Bound` (BoundLoader, BoundViewSlot): the page builds, but
# without that part.
if grep -E 'Unsupported image format|is not installed|Cannot assign to non-existent property|is not a type|Cannot instantiate bound component|Component is not ready' "$log" >&2; then
    echo "selftest.sh: the log shows a missing runtime dependency or a QML tier mismatch" >&2
    exit 1
fi
if ! grep -Eq 'selftest: ([0-9]+)/\1 pages constructed' "$log"; then
    echo "selftest.sh: not every page was constructed" >&2
    exit 1
fi
echo "selftest.sh: OK"
