# StrmQt 0.7.5 Distro Compatibility: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make StrmQt build and run on each target distro's own Qt, from **6.4.2** up.
- Full features on Qt 6.8+; a documented compat tier on 6.4–6.7.
- A native `.deb` for Ubuntu 24.04 and 26.04 and for Debian 12 and 13, and an `.rpm` for Fedora 43 and 44.
- An AppImage with a glibc 2.39 floor.
- Keyring support beyond kwalletd6.
- Two parked fixes (MPRIS `CanGoPrevious`, the web remote's Next/Previous) and a test-harness fix for Qt 6.8.
- Release it as the 0.7.5 pre-release.

**Architecture:**
- Three C++ sites get `QT_VERSION_CHECK` guards.
- Seven QML **shims**, each in a `full/` and a `compat/` variant under `src/ui/shims/`. CMake adds exactly one directory to the `StrmQt` module, so callers write `StrmTint {}` and never import conditionally.
- SDL3 3.4.16 is fetched and linked statically only when `STRMQT_BUNDLE_SDL3=ON`.
- `SecretsStore` keeps its state machine. Its production transport now delegates to a chosen backend: KWallet 6 → KWallet 5 → Secret Service, over an injectable `DBusTransport`. The vault file is the last resort.
- One set of `scripts/ci/` scripts builds and tests every distro, both in GitHub Actions containers and locally through `podman`.

**Spec:** `docs/superpowers/specs/2026-09-27-distro-compat-075-design.md`. Read it before any task: every task argues from it. The evidence behind the spec is `.superpowers/compat-0.7.5-study.md`.

**Tech stack:**
- C++20, Qt 6.4.2–6.11 (Core, DBus, Gui, Network, OpenGL, Quick, QuickControls2, Test, WebSockets), Qt5Compat.GraphicalEffects (compat tier, runtime only);
- libmpv, libvlc, SDL3, OpenSSL;
- CMake ≥ 3.25 with Ninja; debhelper 13, rpmbuild;
- aqtinstall 3.3.0, appimagetool 1.9.1;
- podman locally, GitHub Actions containers in CI.

---

## Global Constraints

- **AGENTS.md is binding.**
  - No new dependency without a stated reason in its commit. SDL3 (Task 8) is the only one, and its reason is fixed in spec §5.
  - No secrets in the repo. TLS errors stay fatal.
  - Change only what the task needs. No drive-by refactors or reformatting.
- **Never push, tag, amend, rebase or force.** Task 22 is the only task that pushes or tags, and only after the user says so in this session.
- **Layers:** `src/core`, `src/server` and `src/platform` never include QtGui. The secret backends live in `strmqt_core` (QtCore + QtDBus only).
- **Qt versions:**
  - Code must compile warning-free on Qt 6.11 (dev preset, `-Werror`) and compile on 6.4.2 (container builds, warnings allowed).
  - A version guard is `#if QT_VERSION >= QT_VERSION_CHECK(6, x, 0)` with a comment naming the API and its "since" version.
  - **No QML file may import `QtQuick.Effects` or `Qt5Compat.GraphicalEffects` except a shim under `src/ui/shims/`.**
- **Build directories (AGENTS.md "Agent-fleet builds"):**
  - Task *N* builds only under `/tmp/wNNa` (host, full tier), `/tmp/wNNb` (host, compat tier) and `/tmp/wNNa-<target>` (a container, mounted as `/build`). For example, Task 5 uses `/tmp/w05a`, `/tmp/w05b` and `/tmp/w05a-ubuntu-24.04`.
  - `TMPDIR` is set inside each: `/tmp/wNNa/tmp`, or `/build/tmp` in a container, as `local.sh` does.
  - Run `df -h /tmp` before starting a build.
  - **The implementer never deletes a build directory.** The orchestrator runs `rm -rf /tmp/wNN*` at the task's gate, after review, in the same step as confirming the commit.
  - Container images are cached as `localhost/strmqt-ci:<target>-<hash>` in podman's storage, not in `/tmp`.
- **Conventions:**
  - Commits are conventional (`feat(scope):`, `fix(scope):`, `test(scope):`, `build(scope):`, `ci:`, `docs:`, `chore(release):`), one per task unless a task says otherwise.
  - Stage explicit paths only. Never `git add -A`: the untracked `docs/strmqt-backport.md` and `skills-lock.json` belong to the user.
  - Every commit message ends with these two lines:

    ```
    Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
    Claude-Session: https://claude.ai/code/session_013JJsS9enEqUZ9WD9HD99Vw
    ```
- **Fixtures:** use the existing test identities: user `a1b2c3d4e5f60718293a4b5c6d7e8f90`, token `not-a-real-token-fixture-only`. No network in unit tests. The only network access in the whole plan is `apt`/`dnf`/`pip`/`podman pull` and the pinned SDL3, aqt Qt and appimagetool downloads.

### The gates every task refers to

**H: host gate, full tier.** Replace `NN` with the task number.

```bash
df -h /tmp
mkdir -p /tmp/wNNa/tmp
export TMPDIR=/tmp/wNNa/tmp
cmake -S . -B /tmp/wNNa -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSTRMQT_WERROR=ON
cmake --build /tmp/wNNa
ctest --test-dir /tmp/wNNa --output-on-failure
bash scripts/check-qmllint-baseline.sh /tmp/wNNa
scripts/ci/selftest.sh /tmp/wNNa/strmqt
```

Expected:
- the build has no warnings;
- ctest reports `100% tests passed`;
- the qmllint script prints no *new* warning fingerprints;
- `selftest.sh: OK`.

These are the `dev` preset's cache variables, spelled out so the tree cannot pick up `build/dev`'s cache.

**HC: host gate, compat tier** (from Task 5 on). The same as H, with three differences: directory `/tmp/wNNb`, add `-DSTRMQT_QML_TIER=compat`, and **no qmllint step**, since the baseline is the full tier's.

Expected:
- the configure log says `StrmQt QML tier: compat (Qt 6.11.x)`;
- ctest passes;
- `selftest.sh: OK`.

**C(target): container gate.**

```bash
df -h /tmp
scripts/ci/local.sh <target> /tmp/wNNa-<target> -- \
    /src/scripts/ci/check.sh /build $(scripts/ci/deps.sh --cmake-args <target>)
```

Expected: the last line is `check.sh: OK (<tier>)`. `<tier>` is `compat` on `ubuntu-24.04` and `debian-12`, and `full` elsewhere.

The first run of a target builds its image. That needs network access and takes a few minutes; later runs reuse it.

---

## Shared vocabulary

A task implementer sees only their own task. These names are fixed across tasks. Do not rename them.

| Name | Kind | Introduced |
|---|---|---|
| `scripts/ci/deps.sh TARGET` / `--image TARGET` / `--cmake-args TARGET` | script | Task 1 |
| targets `ubuntu-24.04` `ubuntu-26.04` `debian-12` `debian-13` `fedora-43` `fedora-44` `appimage` | script argument | Task 1 (`appimage` Task 17) |
| `scripts/ci/local.sh TARGET BUILD_DIR -- CMD…` | script | Task 1 |
| `scripts/ci/check.sh BUILD_DIR [CMAKE_ARGS…]` | script | Task 1 |
| `scripts/ci/selftest.sh BINARY [LOG]` | script | Task 1 |
| `docs/superpowers/plans/2026-09-27-distro-compat-075-verifications.md` | measurement record | Task 1 |
| `STRMQT_QML_TIER` (`auto`/`full`/`compat`), `STRMQT_QML_TIER_RESOLVED` | CMake cache var / variable | Task 5 |
| `<build>/strmqt-qml-tier.txt` | file (`full` or `compat`) | Task 5 |
| `STRMQT_SHIMS_DIR` | test compile definition | Task 5 |
| `StrmTint`, `StrmShadow`, `StrmMask`, `StrmBackdropBlur` | QML shims | Task 5 |
| `TabularText`, `TabularMetrics`, `CrateDisplayText` | QML shims | Task 6 |
| `tests/mocks/QmlShimStaging.h` → `strmqt::test::stageShims(modulePath)` | test helper | Task 5 |
| `STRMQT_BUNDLE_SDL3`, `STRMQT_SDL3_TARGET` | CMake option / variable | Task 8 |
| `strmqt::secrets::DBusTransport`, `SessionBusTransport`, `isUnavailableError` | C++ | Task 12 |
| `strmqt::secrets::SecretBackend`, `Outcome`, `Reply`, `BackendKind`, `chooseSecretBackends`, `makeSecretBackend` | C++ | Task 12 |
| `strmqt::secrets::KWalletBackend(int generation, …)` | C++ | Task 12 |
| `strmqt::secrets::SecretServiceBackend` | C++ | Task 13 |
| `SecretsStore::backendName()` / `SessionController::secretBackend` | C++ property | Task 12 / Task 14 |
| `tests/mocks/FakeDBusTransport.h` → `strmqt::test::FakeDBusTransport` | test helper | Task 12 |
| `packaging/debian/`, `packaging/rpm/strmqt.spec` | packaging | Tasks 15, 16 |
| `scripts/ci/copy-tree.sh`, `package-deb.sh`, `package-rpm.sh`, `install-check.sh`, `appimage-host.sh` | scripts | Tasks 15, 15, 16, 15, 17 |
| `.github/workflows/packages.yml` | reusable workflow | Task 18 |

---

## Waves

This plan is executed **serially**, with one implementer per task (superpowers:subagent-driven-development). Each task is its own wave. The table groups them only to show what each group depends on.

| Group | Tasks | Depends on |
|---|---|---|
| A: see the floor | 1, 2 | nothing. Task 1 is the Ubuntu 24.04 container, so everything after it is checked on Qt 6.4 |
| B: port | 3, 4, 5, 6, 7 | A. Each is checked in `C(ubuntu-24.04)` as it lands; Task 7 closes whatever remains |
| C: enforce | 8, 9 | B. SDL3 before the CI job, so the job builds the shipped configuration |
| D: parked fixes | 10, 11 | only the tree. Independent of B, but ordered here so they too are checked on 6.4 |
| E: keyrings | 12, 13, 14 | in order |
| F: packages | 15, 16, 17, 18 | B–E. 18 wires 15–17 into CI |
| G: docs and release | 19, 20, 21, 22 | everything. 22 needs the user |

---

## Execution notes for the orchestrator

- **Gate per task:**
  1. Review the task.
  2. Confirm that its commit exists (`git log -1 --stat`).
  3. Run `rm -rf /tmp/wNN*` (AGENTS.md: the orchestrator deletes, in the same step).
- **Space:** before any task that runs a container build, check `df -h /tmp`. A container build directory is about 1 GB; a task never holds more than about five at once.
- **When a verification's output differs from the plan's "Expected":**
  - the implementer stops and reports; they do not improvise;
  - Task 7 is the one task whose job *is* to absorb the unexpected, within its time box.
- **Manual checks** (Tasks 5, 6, 14 and 21) need the user. Ask, wait, and record the answer in the task's commit message or the verifications file.
- **Network:** Tasks 1, 8, 15–17 and 21 need it (package managers, pinned downloads). If it is unavailable, stop and tell the user. Do not work around a missing download.

---

### Task 1: Per-distro container recipes and a local check runner

**Files:**
- Create: `scripts/ci/deps.sh`, `scripts/ci/Containerfile`, `scripts/ci/local.sh`, `scripts/ci/check.sh`, `scripts/ci/selftest.sh` (all executable)
- Create: `docs/superpowers/plans/2026-09-27-distro-compat-075-verifications.md`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `deps.sh`, `local.sh`, `check.sh` and `selftest.sh`, with the signatures in Shared vocabulary;
  - the baseline measurements on Qt 6.4.2 and 6.8.2.

`ci.yml` is **not** touched here. The CI job lands in Task 9, once it can pass.

- [ ] **Step 1: Write `scripts/ci/deps.sh`**

```bash
#!/usr/bin/env bash
# scripts/ci/deps.sh — what each target distro needs to build, test and
# self-test StrmQt. The single list for CI (ci.yml, packages.yml) and for local
# container checks (local.sh). Package names verified 2026-09-27 in each
# release's container (spec 2026-09-27 §2).
#
#   deps.sh TARGET               install build + test dependencies (as root, in a TARGET container)
#   deps.sh --image TARGET       print TARGET's container image
#   deps.sh --cmake-args TARGET  print TARGET's extra CMake arguments
set -euo pipefail

mode=install
case "${1:-}" in
    --image|--cmake-args) mode=${1#--}; shift ;;
esac
target=${1:?usage: deps.sh [--image|--cmake-args] TARGET}

case "$target" in
    ubuntu-24.04) image=docker.io/library/ubuntu:24.04 ;;
    ubuntu-26.04) image=docker.io/library/ubuntu:26.04 ;;
    debian-12)    image=docker.io/library/debian:bookworm ;;
    debian-13)    image=docker.io/library/debian:trixie ;;
    fedora-43)    image=registry.fedoraproject.org/fedora:43 ;;
    fedora-44)    image=registry.fedoraproject.org/fedora:44 ;;
    *) echo "deps.sh: unknown target '$target'" >&2; exit 2 ;;
esac

cmake_args=""

case "$mode" in
    image) echo "$image"; exit 0 ;;
    cmake-args) echo "$cmake_args"; exit 0 ;;
esac

# Debian family. qt6-tools-dev is deliberately absent: nothing here uses Qt
# Tools, and bookworm's is a mismatched 6.4.2~rc1.
apt_common=(
    build-essential cmake ninja-build pkg-config git ca-certificates file
    qt6-base-dev qt6-base-dev-tools qt6-declarative-dev qt6-declarative-dev-tools
    qt6-websockets-dev qt6-svg-dev qt6-wayland qt6-qpa-plugins
    qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-templates
    qml6-module-qtquick-window qml6-module-qtqml-workerscript qml6-module-qtqml
    libmpv-dev libvlc-dev vlc-plugin-base vlc-plugin-video-output libssl-dev
    ffmpeg libgl1-mesa-dri
)
case "$target" in
    # Qt 6.4.2: no QtQuick.Effects (compat tier), no SDL3 package, and the SVG
    # image plugin ships inside libqt6svg6 (pulled by qt6-svg-dev).
    ubuntu-24.04|debian-12)
        apt_extra=(qml6-module-qt5compat-graphicaleffects libudev-dev) ;;
    # Qt 6.8+: full tier; the SVG plugin is its own package (study §2.5).
    ubuntu-26.04|debian-13)
        apt_extra=(qml6-module-qtquick-effects qt6-svg-plugins libsdl3-dev) ;;
esac

dnf_pkgs=(
    gcc-c++ cmake ninja-build pkgconf git-core file
    qt6-qtbase-devel qt6-qtbase-private-devel qt6-qtdeclarative-devel
    qt6-qtwebsockets-devel qt6-qtsvg-devel qt6-qtsvg qt6-qtwayland
    mpv-devel vlc-devel vlc-plugins-base vlc-plugin-ffmpeg SDL3-devel openssl-devel
    ffmpeg-free mesa-dri-drivers
)

case "$target" in
    ubuntu-*|debian-*)
        export DEBIAN_FRONTEND=noninteractive
        apt-get update
        apt-get install -y --no-install-recommends "${apt_common[@]}" "${apt_extra[@]}"
        ;;
    fedora-*)
        dnf install -y --setopt=install_weak_deps=False "${dnf_pkgs[@]}"
        ;;
esac
```

- [ ] **Step 2: Write `scripts/ci/Containerfile` and `scripts/ci/local.sh`**

`scripts/ci/Containerfile`:

```dockerfile
# One image per target: the distro plus exactly what deps.sh installs. The
# build context is a scratch directory local.sh assembles, laid out like the
# repository, so deps.sh finds its files relative to itself.
ARG BASE
FROM ${BASE}
ARG TARGET
COPY . /opt/strmqt-ci/
RUN /opt/strmqt-ci/scripts/ci/deps.sh "${TARGET}"
```

`scripts/ci/local.sh`:

```bash
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
```

- [ ] **Step 3: Write `scripts/ci/selftest.sh` and `scripts/ci/check.sh`**

`scripts/ci/selftest.sh`:

```bash
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
STRMQT_SELFTEST=1 QT_QPA_PLATFORM=offscreen QT_ASSUME_STDERR_HAS_CONSOLE=1 \
    "$bin" >"$log" 2>&1 || status=$?
cat "$log"

if [ "$status" -ne 0 ]; then
    echo "selftest.sh: $bin exited $status" >&2
    exit 1
fi
if grep -E 'Unsupported image format|is not installed|Cannot assign to non-existent property|is not a type' "$log" >&2; then
    echo "selftest.sh: the log shows a missing runtime dependency or a QML tier mismatch" >&2
    exit 1
fi
if ! grep -Eq 'selftest: ([0-9]+)/\1 pages constructed' "$log"; then
    echo "selftest.sh: not every page was constructed" >&2
    exit 1
fi
echo "selftest.sh: OK"
```

`scripts/ci/check.sh`:

```bash
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
```

`strmqt-qml-tier.txt` does not exist until Task 5, so before that the last line reads `check.sh: OK (tier n/a)`.

- [ ] **Step 4: Make them executable and syntax-check**

Run:

```bash
chmod +x scripts/ci/deps.sh scripts/ci/local.sh scripts/ci/check.sh scripts/ci/selftest.sh
for f in scripts/ci/*.sh; do bash -n "$f" && echo "ok $f"; done
scripts/ci/deps.sh --image ubuntu-24.04
scripts/ci/deps.sh --cmake-args debian-13; echo "[exit $?]"
scripts/ci/deps.sh --image nonsense; echo "[exit $?]"
```

Expected:
- four `ok` lines;
- `docker.io/library/ubuntu:24.04`;
- an empty line followed by `[exit 0]`;
- `deps.sh: unknown target 'nonsense'` followed by `[exit 2]`.

- [ ] **Step 5: Host sanity: `selftest.sh` against today's tree**

Run gate **H** with `NN=01`.

Expected: all green. This proves `selftest.sh`'s log checks accept a known-good build. A false positive here (for example, `is not a type` matching a benign line) must be fixed in the regex, not ignored.

- [ ] **Step 6: Baseline on Debian 13 (Qt 6.8.2)**

Run:

```bash
df -h /tmp
scripts/ci/local.sh debian-13 /tmp/w01a-debian-13 -- /src/scripts/ci/check.sh /build 2>&1 | tail -40
```

Expected, because `tst_navigation_history` fails on Qt 6.8 (spec §9.1):
- the build succeeds;
- ctest ends with `99% tests passed, 1 tests failed out of 74`, and the failed test is `tst_navigation_history`;
- the script stops there.

Then run the self-test by hand:

```bash
scripts/ci/local.sh debian-13 /tmp/w01a-debian-13 -- /src/scripts/ci/selftest.sh /build/strmqt | tail -3
```

Expected: `selftest: 15/15 pages constructed` and `selftest.sh: OK`.

- [ ] **Step 7: Baseline on Ubuntu 24.04 (Qt 6.4.2)**

Run:

```bash
scripts/ci/local.sh ubuntu-24.04 /tmp/w01a-ubuntu-24.04 -- /src/scripts/ci/check.sh /build 2>&1 | tail -15
```

Expected: configure fails with `Could not find a configuration file for package "Qt6" that is compatible with requested version "6.8"`, listing `…/Qt6Config.cmake, version: 6.4.2`. This is the floor Tasks 3–7 lower.

- [ ] **Step 8: Start the verifications record**

Create `docs/superpowers/plans/2026-09-27-distro-compat-075-verifications.md`:

```markdown
# 0.7.5 distro compatibility: measurements

Every entry is a command that was run and what it printed, not an expectation.
Tasks append here; the final gate (Task 21) checks the record is complete.

## Task 1: baselines (before any porting)

| Target | Qt | Result |
|---|---|---|
| host (Arch) | 6.11.x | gate H green |
| debian-13 | 6.8.2 | build clean; ctest 73/74, `tst_navigation_history` fails; self-test 15/15 |
| ubuntu-24.04 | 6.4.2 | configure fails: Qt 6.8 required |
```

Fill in the real Qt patch version from Step 5's configure output and anything else that differed from "Expected".

- [ ] **Step 9: Commit**

```bash
git add scripts/ci/deps.sh scripts/ci/Containerfile scripts/ci/local.sh scripts/ci/check.sh \
        scripts/ci/selftest.sh docs/superpowers/plans/2026-09-27-distro-compat-075-verifications.md
git commit -m "build(ci): add per-distro container recipes and a local check runner

scripts/ci/deps.sh is the one list of what each target distro needs;
local.sh runs any command in that distro's container with the checkout
read-only, check.sh builds, tests, self-tests and install-checks, and
selftest.sh judges the self-test's log as well as its exit code.

Baselines: Debian 13 (Qt 6.8.2) builds with tst_navigation_history
failing; Ubuntu 24.04 (Qt 6.4.2) does not configure (Qt 6.8 required).

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_013JJsS9enEqUZ9WD9HD99Vw"
```

---

### Task 2: `tst_navigation_history` stages its QML module once per run

**Files:**
- Modify: `tests/unit/tst_navigation_history.cpp` (`createProbe`, around lines 877–945)
- Modify: the verifications file

**Interfaces:**
- Consumes: Task 1's `local.sh` and the `debian-13` image.
- Produces: a test that passes 29/29 in one run on Qt 6.8.2 and on 6.11.

Use superpowers:systematic-debugging. The spec's hypothesis (§9.1) comes from the study's observation. It has not been proven.

- [ ] **Step 1: Reproduce on 6.8.2**

```bash
scripts/ci/local.sh debian-13 /tmp/w02a-debian-13 -- bash -c '
  cmake -S /src -B /build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSTRMQT_WERROR=OFF >/dev/null &&
  cmake --build /build --target tst_navigation_history >/dev/null &&
  QT_QPA_PLATFORM=offscreen /build/tst_navigation_history 2>&1 | tail -30'
```

Expected:
- `Totals: 18 passed, 11 failed`;
- the failures quote `Type StrmGrid unavailable` with a path inside a **different** test's temporary directory.

Then run one of the failing functions alone, for example `/build/tst_navigation_history restoresForwardFocusAndReplacesBranches`, and expect it to pass. If either expectation does not hold, stop and report what you saw.

- [ ] **Step 2: Confirm the cause before fixing it**

- Keep every per-test `QTemporaryDir` alive until the end of the run: temporarily move them into a static `std::vector<std::unique_ptr<QTemporaryDir>>`.
- Re-run Step 1. If the failures disappear, the cause is confirmed: the 6.8 type loader resolves `StrmQt` to the first staged path.
- Revert this probe. It is a diagnostic, not the fix.

- [ ] **Step 3: Stage the module once**

In `tests/unit/tst_navigation_history.cpp`, split `createProbe`:
- the module staging moves into a function-local static that runs once;
- the per-test `Probe.qml` and `BoundedNavigationStack.qml` stay in the test's own `dir`;
- the view's import path points at the shared module root.

```cpp
// The StrmQt module is staged ONCE per run, in a directory that outlives every
// test. Qt 6.8's type loader remembers where it first resolved a module URI
// across engines, so a per-test copy in a per-test QTemporaryDir left later
// tests resolving StrmGrid against a deleted directory: "Type StrmGrid
// unavailable", 11 of 29 on 6.8.2, each passing alone (spec 2026-09-27 §9.1).
// Qt 6.11 does not keep that state; staging once is right on both.
QString stagedModuleRoot()
{
    static QTemporaryDir root;
    static const bool staged = [] {
        if (!root.isValid())
            return false;
        const QString modulePath = root.filePath(QStringLiteral("StrmQt"));
        if (!QDir().mkpath(modulePath))
            return false;
        // ... the existing moduleFiles list, copy loop and qmldir write, unchanged,
        //     with `dir` replaced by `root` ...
        return true;
    }();
    return staged ? root.path() : QString();
}
```

In `createProbe`, delete the moved code. Before `view.setSource(...)`, add:

```cpp
    const QString moduleRoot = stagedModuleRoot();
    if (moduleRoot.isEmpty())
        return nullptr;
    view.engine()->addImportPath(moduleRoot);
```

This replaces `view.engine()->addImportPath(dir.path());`. `BoundedNavigationStack.qml` and `Probe.qml` are still written into `dir`, next to each other, because the probe finds the helper through its own directory.

- [ ] **Step 4: Verify on 6.8.2 and 6.11**

Re-run Step 1's command.

Expected: `Totals: 29 passed, 0 failed`.

Then run gate **H** (`NN=02`).

Expected: all green. Run `tst_navigation_history` alone as well, and expect 29 passed.

- [ ] **Step 5: Record and commit**

Append to the verifications file:

```markdown
## Task 2: tst_navigation_history on Qt 6.8.2

Before: 18 passed, 11 failed ("Type StrmGrid unavailable" against a previous
test's deleted temp dir). Cause confirmed by keeping every temp dir alive (all
pass). After staging the module once per run: 29 passed on 6.8.2 and 6.11.x.
```

```bash
git add tests/unit/tst_navigation_history.cpp docs/superpowers/plans/2026-09-27-distro-compat-075-verifications.md
git commit -m "test(ui): stage the navigation-history QML module once per run

On Qt 6.8.2, 11 of 29 functions failed in a full run with \"Type StrmGrid
unavailable\" pointing into an earlier test's deleted QTemporaryDir; each
passed alone. The 6.8 type loader keeps where it first resolved the StrmQt
URI across engines. The module is now staged once, in a directory that
outlives the run; per-test files stay per-test. 29/29 on 6.8.2 and 6.11.

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_013JJsS9enEqUZ9WD9HD99Vw"
```

---

### Task 3: Lower the Qt floor to 6.4 and CMake to 3.25

**Files:**
- Modify: `CMakeLists.txt` (lines 1, 16–18)
- Modify: `src/CMakeLists.txt` (`qt_add_qml_module`)
- Modify: `src/app/main.cpp` (line 101)

**Interfaces:**
- Consumes: nothing new.
- Produces:
  - configure succeeds on Qt ≥ 6.4 and CMake ≥ 3.25;
  - the module's resources are at `qrc:/qt/qml/StrmQt/…` on every Qt;
  - `Main.qml` loads on Qt < 6.5.

- [ ] **Step 1: CMake floor**

In `CMakeLists.txt`:
- line 1: change `cmake_minimum_required(VERSION 3.28)` to `cmake_minimum_required(VERSION 3.25)`.
- Replace lines 16–18 with:

```cmake
# Qt 6.4.2 is the floor: Ubuntu 24.04 LTS and Debian 12 ship exactly that, and
# no supported distro ships 6.5-6.7 (spec 2026-09-27 §2). Qt 6.8+ is the full
# tier; older Qt builds the compat QML shims (src/ui/shims, spec §3).
find_package(Qt6 6.4 REQUIRED COMPONENTS
    Core DBus Gui Network OpenGL Quick QuickControls2 Test WebSockets)
# REQUIRES and the QTP policies are Qt 6.5+. Guarding at 6.8 keeps the full
# tier's policy set exactly what it was.
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.8)
    qt_standard_project_setup(REQUIRES 6.8)
else()
    qt_standard_project_setup()
endif()
```

- [ ] **Step 2: Pin the resource prefix**

In `src/CMakeLists.txt`, add `RESOURCE_PREFIX /qt/qml` to `qt_add_qml_module(strmqt …)`, directly after `VERSION 1.0`:

```cmake
qt_add_qml_module(strmqt
    URI StrmQt
    VERSION 1.0
    # QTP0001's default from Qt 6.5 on, spelled out so Qt 6.4 (whose default is
    # "/") puts the module in the same place: qrc:/qt/qml/StrmQt/... (spec §4.1).
    RESOURCE_PREFIX /qt/qml
    QML_FILES
```

- [ ] **Step 3: Load `Main.qml` on Qt < 6.5**

In `src/app/main.cpp`, replace `engine.loadFromModule("StrmQt", "Main");` with:

```cpp
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    engine.loadFromModule("StrmQt", "Main");
#else
    // loadFromModule is Qt 6.5, and 6.4's default import path lacks
    // qrc:/qt/qml. RESOURCE_PREFIX pins the module there on every Qt
    // (src/CMakeLists.txt), so one URL serves (spec 2026-09-27 §4.1).
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/StrmQt/ui/Main.qml")));
#endif
```

Add `#include <QUrl>` if `main.cpp` does not already include it.

- [ ] **Step 4: Host gate**

Run gate **H** (`NN=03`).

Expected: all green. The resource layout on 6.11 is unchanged. `strings -el /tmp/w03a/strmqt | grep -m1 'qt/qml/StrmQt'` finds the path.

- [ ] **Step 5: Debian 12 configure (CMake 3.25.1, Qt 6.4.2)**

```bash
scripts/ci/local.sh debian-12 /tmp/w03a-debian-12 -- bash -c '
  cmake --version | head -1
  cmake -S /src -B /build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSTRMQT_WERROR=OFF 2>&1 | tail -5
  cmake --build /build 2>&1 | grep -E "error:|FAILED" | head -20'
```

Expected:
- `cmake version 3.25.1`;
- configure completes (`-- Generating done`). Any CMake error here means a feature newer than 3.25 is in use: name it and stop;
- the build fails only in `src/input/InputMap.cpp` (`qReturnArg`) and `src/app/music/MusicRepository.cpp` (`makeReadyValueFuture`), plus possibly `src/app/ImageLimits.h` (`<QtTypes>`). Task 4 fixes these. A failure anywhere else is new information: record it in the verifications file for Task 4.

- [ ] **Step 6: Record and commit**

Append the Step 5 error list to the verifications file under `## Task 3: first 6.4 compile`.

```bash
git add CMakeLists.txt src/CMakeLists.txt src/app/main.cpp \
        docs/superpowers/plans/2026-09-27-distro-compat-075-verifications.md
git commit -m "build: lower the Qt floor to 6.4 and CMake to 3.25

Ubuntu 24.04 LTS and Debian 12 ship Qt 6.4.2 and Debian 12 ships CMake
3.25.1. qt_standard_project_setup(REQUIRES) stays for Qt 6.8+ so the full
tier's policies do not change; RESOURCE_PREFIX /qt/qml pins the module's
resource path on every Qt; before 6.5 Main.qml is loaded by URL with
qrc:/qt/qml added to the import path (no loadFromModule).

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_013JJsS9enEqUZ9WD9HD99Vw"
```

---

### Task 4: Build the C++ on Qt 6.4

**Files:**
- Modify: `src/input/InputMap.cpp` (`InputMap::trigger`, around lines 1052–1076)
- Modify: `src/app/music/MusicRepository.cpp` (`ready()`, line 26)
- Modify, only if Task 3's record says so: `src/app/ImageLimits.h` (line 5)
- Test: `tests/unit/tst_input_map.cpp`

**Interfaces:**
- Consumes: Task 3.
- Produces:
  - `InputMap::trigger` works against C++ handlers and against typed or untyped QML handlers on every Qt;
  - all C++ compiles on 6.4.2.

- [ ] **Step 1: Write the failing test**

In `tests/unit/tst_input_map.cpp`, declare `void triggerReachesAnUntypedQmlHandler();` after `void triggerReachesAQmlHandler();`, and define it after `InputMapTest::triggerReachesAQmlHandler()`:

```cpp
// A QML handler without type annotations presents (QVariant, QVariant) ->
// QVariant to the meta-object on every Qt, and before Qt 6.7 an annotated one
// may too. trigger() must shape the call from what the meta-object reports
// (spec 2026-09-27 §4.2), not assume (QString, bool) -> bool.
void InputMapTest::triggerReachesAnUntypedQmlHandler()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQml
        QtObject {
            property string last: ""
            function invokeAction(actionId, autoRepeat) {
                if (actionId !== "app.fullscreen")
                    return false
                last = actionId
                return autoRepeat === true
            }
        })", QUrl());
    std::unique_ptr<QObject> handler(component.create());
    QVERIFY2(handler, qPrintable(component.errorString()));
    m_map->registerHandler(handler.get());

    QVERIFY(m_map->trigger(QStringLiteral("app.fullscreen"), true));
    QCOMPARE(handler->property("last").toString(), QStringLiteral("app.fullscreen"));
    // The handler's own answer comes back, not merely "the call succeeded".
    QVERIFY(!m_map->trigger(QStringLiteral("app.fullscreen"), false));
    QVERIFY(!m_map->trigger(QStringLiteral("app.settings")));
}
```

- [ ] **Step 2: Watch it fail on 6.11**

```bash
mkdir -p /tmp/w04a/tmp && export TMPDIR=/tmp/w04a/tmp
cmake -S . -B /tmp/w04a -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSTRMQT_WERROR=ON >/dev/null
cmake --build /tmp/w04a --target tst_input_map && /tmp/w04a/tst_input_map triggerReachesAnUntypedQmlHandler
```

Expected: `FAIL!` at the first `QVERIFY(m_map->trigger(…))`, with the warning `has no invokeAction(QString, bool)`. The typed `qReturnArg` call cannot reach an untyped function.

If it passes instead, this Qt converts the arguments. Keep the test anyway: Step 3 is still needed for 6.4. Note the result in the commit message.

- [ ] **Step 3: Shape the call from the signature**

In `src/input/InputMap.cpp`, add to the anonymous namespace (create one above `InputMap::trigger` if the file has none there):

```cpp
// One typed invoke, spelled for the Qt at hand: the variadic qReturnArg form
// is Qt 6.5+, and QArgument/QReturnArgument (what Q_ARG expands to) are what
// 6.4 has.
template<class R, class A, class B>
bool invokeShaped(const QMetaMethod &method, QObject *target, R &result, const A &a, const B &b)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return method.invoke(target, Qt::DirectConnection, qReturnArg(result), a, b);
#else
    return method.invoke(target, Qt::DirectConnection,
                         QReturnArgument<R>(QMetaType::fromType<R>().name(), result),
                         QArgument<A>(QMetaType::fromType<A>().name(), a),
                         QArgument<B>(QMetaType::fromType<B>().name(), b));
#endif
}

// Asks one handler to run an action. A handler is C++ (Q_INVOKABLE bool
// invokeAction(const QString &, bool)) or QML (`function invokeAction(actionId:
// string, autoRepeat: bool): bool`). A QML function reaches the meta-object as
// (QString, bool) -> bool, as (QVariant, QVariant) -> QVariant, or as a mix,
// depending on its annotations and on the Qt version. So the call is shaped by
// what the meta-object reports rather than assumed (spec 2026-09-27 §4.2).
// False when there is no two-argument invokeAction or the call fails.
bool callInvokeAction(QObject *handler, const QString &actionId, bool autoRepeat, bool *handled)
{
    const QMetaObject *meta = handler->metaObject();
    for (int i = meta->methodCount() - 1; i >= 0; --i) {
        const QMetaMethod method = meta->method(i);
        if (method.name() != QByteArrayLiteral("invokeAction") || method.parameterCount() != 2)
            continue;
        const bool typedId = method.parameterMetaType(0) == QMetaType::fromType<QString>();
        const bool typedRepeat = method.parameterMetaType(1) == QMetaType::fromType<bool>();
        const bool typedResult = method.returnMetaType() == QMetaType::fromType<bool>();
        const QVariant idArg = actionId;
        const QVariant repeatArg = autoRepeat;

        auto withResult = [&](auto &result) {
            auto withId = [&](const auto &id) {
                return typedRepeat ? invokeShaped(method, handler, result, id, autoRepeat)
                                   : invokeShaped(method, handler, result, id, repeatArg);
            };
            return typedId ? withId(actionId) : withId(idArg);
        };

        bool boolResult = false;
        QVariant variantResult;
        if (!(typedResult ? withResult(boolResult) : withResult(variantResult)))
            return false;
        *handled = typedResult ? boolResult : variantResult.toBool();
        return true;
    }
    return false;
}
```

In `InputMap::trigger`, replace the `QMetaObject::invokeMethod(handler, "invokeAction", Qt::DirectConnection, qReturnArg(handled), actionId, autoRepeat)` call with:

```cpp
        bool handled = false;
        if (!callInvokeAction(handler, actionId, autoRepeat, &handled)) {
            qCWarning(logApp) << "input map: handler" << handler
                              << "has no invokeAction(QString, bool)";
            continue;
        }
```

Keep the warning text exactly as it is: `triggerSkipsGoneAndMalformedHandlers` matches it. Add `#include <QMetaMethod>` if the file does not already include it.

- [ ] **Step 4: `makeReadyValueFuture` (Qt 6.6)**

In `src/app/music/MusicRepository.cpp`, replace the body of `ready()`:

```cpp
template<class T>
QFuture<Result<T>> ready(Result<T> result)
{
    // makeReadyValueFuture is Qt 6.6; makeReadyFuture (6.0) is deprecated from
    // 6.6 on, so each Qt gets the spelling it does not warn about (spec §4.2).
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    return QtFuture::makeReadyValueFuture(std::move(result));
#else
    return QtFuture::makeReadyFuture(std::move(result));
#endif
}
```

- [ ] **Step 5: Host gate**

Run gate **H** (`NN=04`).

Expected: all green, including `triggerReachesAQmlHandler`, `triggerReachesAnUntypedQmlHandler`, `triggerAsksTheNewestLiveHandler` and `triggerSkipsGoneAndMalformedHandlers`.

- [ ] **Step 6: Compile on 6.4.2**

```bash
scripts/ci/local.sh ubuntu-24.04 /tmp/w04a-ubuntu-24.04 -- bash -c '
  cmake -S /src -B /build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSTRMQT_WERROR=OFF >/dev/null &&
  cmake --build /build 2>&1 | grep -E "error:|FAILED" | head -20; echo "[build exit ${PIPESTATUS[0]}]"'
```

Expected: no C++ `error:` lines.
- If `src/app/ImageLimits.h` fails on `<QtTypes>`, replace that include with `<QtGlobal>` (spec §4.2) and re-run.
- A QML compile failure (qmlcachegen on `QtQuick.Effects` or `font.features`) is expected, and Tasks 5 and 6 fix it. Record which files fail.

Then run the C++ tests that do not load app QML:

```bash
scripts/ci/local.sh ubuntu-24.04 /tmp/w04a-ubuntu-24.04 -- bash -c '
  cmake --build /build --target tst_input_map tst_music_repository tst_secrets_store 2>&1 | tail -2 &&
  QT_QPA_PLATFORM=offscreen /build/tst_input_map | tail -3 &&
  /build/tst_music_repository | tail -3'
```

Expected: `Totals: … 0 failed` for both. **`triggerReachesAQmlHandler` passing on 6.4 closes the study's risk 3.** Record it in the verifications file, with the signature the 6.4 meta-object reported if you printed it while debugging.

- [ ] **Step 7: Commit**

```bash
git add src/input/InputMap.cpp src/app/music/MusicRepository.cpp tests/unit/tst_input_map.cpp \
        docs/superpowers/plans/2026-09-27-distro-compat-075-verifications.md
# plus src/app/ImageLimits.h if Step 6 changed it
git commit -m "fix(compat): build the C++ on Qt 6.4

InputMap::trigger no longer assumes invokeAction is (QString, bool) -> bool:
it reads the handler's signature from the meta-object and shapes the call,
so typed and untyped QML handlers work on every Qt, with qReturnArg on 6.5+
and QArgument/QReturnArgument below. MusicRepository uses makeReadyFuture
before Qt 6.6. A test covers the untyped QML handler.

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_013JJsS9enEqUZ9WD9HD99Vw"
```

---
### Task 5: QML tier mechanism and the four effect shims

**Files:**
- Create: `cmake/StrmQtQmlTier.cmake`, `src/ui/shims/Shims.cmake`
- Create: `src/ui/shims/full/{StrmTint,StrmShadow,StrmMask,StrmBackdropBlur}.qml`
- Create: `src/ui/shims/compat/{StrmTint,StrmShadow,StrmMask,StrmBackdropBlur}.qml`
- Create: `tests/mocks/QmlShimStaging.h`
- Modify: `CMakeLists.txt` (include the tier module after `find_package(Qt6 …)`)
- Modify: `src/CMakeLists.txt`:
  - include `Shims.cmake` before `Controls.cmake`;
  - `target_compile_definitions(strmqt PRIVATE STRMQT_QML_TIER="${STRMQT_QML_TIER_RESOLVED}")`.
- Modify: `src/app/main.cpp` (one startup log line)
- Modify, replacing `MultiEffect` and dropping `import QtQuick.Effects`:
  - `src/ui/controls/StrmIcon.qml`;
  - `src/ui/controls/StrmPanel.qml`;
  - `src/ui/music/RecordStage.qml` (two sites);
  - `src/ui/music/CratePortrait.qml`;
  - `src/ui/pages/HomePage.qml`;
  - `src/ui/pages/PersonPage.qml`.
- Modify: `tests/CMakeLists.txt` (`strmqt_add_test` gains `STRMQT_SHIMS_DIR`)
- Modify: every test that stages a copied `StrmQt` module containing a file that now uses a shim. At least `tst_navigation_history`, `tst_card_component`, `tst_qml_accessibility`, `tst_focus_clip`, `tst_music_player_panel`, `tst_record_stage` and `tst_crate_controls`; find the rest with the grep in Step 6.

**Interfaces:**
- Consumes: Tasks 3–4.
- Produces:
  - `STRMQT_QML_TIER` and `STRMQT_QML_TIER_RESOLVED`;
  - `strmqt-qml-tier.txt`;
  - the four effect shims with the property contracts of spec §4.3;
  - `strmqt::test::stageShims(const QString &modulePath) -> QByteArray` (qmldir lines, or empty on failure).

- [ ] **Step 1: `cmake/StrmQtQmlTier.cmake`**

```cmake
# QML tier selection (spec 2026-09-27 §3). One build decision: which directory
# of src/ui/shims joins the StrmQt module. Never a runtime check.
set(STRMQT_QML_TIER "auto" CACHE STRING
    "QML tier: auto (full on Qt >= 6.8), full (needs Qt >= 6.7), or compat")
set_property(CACHE STRMQT_QML_TIER PROPERTY STRINGS auto full compat)

if(STRMQT_QML_TIER STREQUAL "auto")
    if(Qt6_VERSION VERSION_GREATER_EQUAL 6.8)
        set(STRMQT_QML_TIER_RESOLVED full)
    else()
        set(STRMQT_QML_TIER_RESOLVED compat)
    endif()
elseif(STRMQT_QML_TIER STREQUAL "full")
    if(Qt6_VERSION VERSION_LESS 6.7)
        message(FATAL_ERROR "STRMQT_QML_TIER=full needs Qt >= 6.7 (font.variableAxes); "
                            "found ${Qt6_VERSION}. Use auto or compat.")
    endif()
    set(STRMQT_QML_TIER_RESOLVED full)
elseif(STRMQT_QML_TIER STREQUAL "compat")
    set(STRMQT_QML_TIER_RESOLVED compat)
else()
    message(FATAL_ERROR "STRMQT_QML_TIER must be auto, full or compat (got '${STRMQT_QML_TIER}')")
endif()

message(STATUS "StrmQt QML tier: ${STRMQT_QML_TIER_RESOLVED} (Qt ${Qt6_VERSION})")
file(WRITE "${CMAKE_BINARY_DIR}/strmqt-qml-tier.txt" "${STRMQT_QML_TIER_RESOLVED}\n")
```

In `CMakeLists.txt`, directly after the `qt_standard_project_setup` block from Task 3, add:

```cmake
include(${CMAKE_SOURCE_DIR}/cmake/StrmQtQmlTier.cmake)
```

- [ ] **Step 2: `src/ui/shims/Shims.cmake`**

```cmake
# Tier shims (spec 2026-09-27 §4.3). Exactly one directory joins the module;
# the type name is the file's basename, so both tiers register the same types
# and callers never import an effects module themselves. Task 6 adds the three
# text shims to this list.
set(_strmqt_shims StrmTint StrmShadow StrmMask StrmBackdropBlur)
list(TRANSFORM _strmqt_shims PREPEND "ui/shims/${STRMQT_QML_TIER_RESOLVED}/")
list(TRANSFORM _strmqt_shims APPEND ".qml")
qt_target_qml_sources(strmqt QML_FILES ${_strmqt_shims})
```

In `src/CMakeLists.txt`, before the `Controls.cmake` include, add:

```cmake
include(${CMAKE_CURRENT_SOURCE_DIR}/ui/shims/Shims.cmake)
target_compile_definitions(strmqt PRIVATE STRMQT_QML_TIER="${STRMQT_QML_TIER_RESOLVED}")
```

If `main.cpp` belongs to a different target than `strmqt` (check with `grep -n main.cpp src/CMakeLists.txt`), put the definition on that target instead.

- [ ] **Step 3: The full-tier effect shims**

`src/ui/shims/full/StrmTint.qml`:

```qml
import QtQuick
import QtQuick.Effects

// Tier shim (spec 2026-09-27 §4.3): recolours a white glyph to `color`.
// Full tier: MultiEffect colorization, exactly what StrmIcon did before.
MultiEffect {
    property color color: "white"

    colorization: 1.0
    colorizationColor: color
}
```

`src/ui/shims/full/StrmShadow.qml`:

```qml
import QtQuick
import QtQuick.Effects

// Tier shim (spec §4.3): a drop shadow described by a Theme.elevationN
// ({blur, y, opacity}). Usable as a sibling or as a layer.effect root.
MultiEffect {
    property var elevation: ({ blur: 0, y: 0, opacity: 0 })
    // `shadowColor` is MultiEffect's own property, so it is the contract name.

    autoPaddingEnabled: true
    shadowEnabled: true
    shadowBlur: elevation.blur
    shadowVerticalOffset: elevation.y
    shadowOpacity: elevation.opacity
}
```

`src/ui/shims/full/StrmMask.qml`:

```qml
import QtQuick
import QtQuick.Effects

// Tier shim (spec §4.3): shows `source` only where `maskSource` is opaque.
MultiEffect {
    maskEnabled: true
    maskThresholdMin: 0.5
    maskSpreadAtMin: 1.0
}
```

`src/ui/shims/full/StrmBackdropBlur.qml`:

```qml
import QtQuick
import QtQuick.Effects

// Tier shim (spec §4.3): the backdrop wash, a heavy blur with the colour
// pulled down. Used as a layer.effect root.
MultiEffect {
    autoPaddingEnabled: false
    blurEnabled: true
    blur: 1.0
    blurMax: 48
    saturation: -0.55
}
```

- [ ] **Step 4: The compat-tier effect shims**

`src/ui/shims/compat/StrmTint.qml`:

```qml
import QtQuick
import Qt5Compat.GraphicalEffects

// Tier shim (spec 2026-09-27 §4.3), compat tier (Qt < 6.8): ColorOverlay keeps
// the source's alpha, so a white antialiased glyph comes out as `color` with
// its antialiasing intact, which is what MultiEffect colorization gives.
ColorOverlay {
    // ColorOverlay's own property is `color`, so the contract is met as-is.
}
```

`src/ui/shims/compat/StrmShadow.qml`:

```qml
import QtQuick
import Qt5Compat.GraphicalEffects

// Tier shim (spec §4.3), compat tier. MultiEffect's shadowBlur is a 0..1
// fraction of blurMax (default 32 px); DropShadow wants a radius in pixels.
DropShadow {
    property var elevation: ({ blur: 0, y: 0, opacity: 0 })
    property color shadowColor: "black"

    transparentBorder: true
    horizontalOffset: 0
    verticalOffset: elevation.y
    radius: elevation.blur * 32
    samples: Math.min(64, 2 * Math.ceil(radius) + 1)
    color: Qt.rgba(shadowColor.r, shadowColor.g, shadowColor.b, shadowColor.a * elevation.opacity)
}
```

`src/ui/shims/compat/StrmMask.qml`:

```qml
import QtQuick
import Qt5Compat.GraphicalEffects

// Tier shim (spec §4.3), compat tier. The edge is the mask item's own
// antialiasing rather than MultiEffect's spread: marginally harder.
OpacityMask {
}
```

`src/ui/shims/compat/StrmBackdropBlur.qml`:

```qml
import QtQuick
import Qt5Compat.GraphicalEffects

// Tier shim (spec §4.3), compat tier: FastBlur at MultiEffect's blurMax, then
// Desaturate by the same amount MultiEffect's saturation removes.
FastBlur {
    radius: 48
    transparentBorder: false
    layer.enabled: true
    layer.effect: Desaturate {
        desaturation: 0.55
    }
}
```

**Checks for these four files:**
- **Do not add a `source` property.** Every root type already declares it.
- If qmllint or the 6.4 build shows that one of these root types lacks a property the contract names, stop and report which one.

- [ ] **Step 5: Replace the seven sites**

Each change swaps the type and keeps every binding the shim does not absorb (`anchors`, `visible`, the `Behavior`). Each file drops `import QtQuick.Effects`.

1. **`StrmIcon.qml`:** `MultiEffect { … colorization: 1.0; colorizationColor: icon.color … }` becomes `StrmTint { anchors.fill: parent; source: glyph; color: icon.color; visible: …; Behavior on color { ColorAnimation { … } } }`. The `Behavior` moves from `colorizationColor` to `color`.
   - Update the header comment: the tint is now "a StrmTint (MultiEffect colorization on the full tier, ColorOverlay on the compat tier)".
   - The offscreen note stays: it holds for both.
2. **`StrmPanel.qml`:** `layer.effect: StrmShadow { elevation: panel._shadow; shadowColor: Theme.shadowColor }`.
3. **`RecordStage.qml:188`:** `StrmShadow { anchors.fill: parent; source: shadowCaster; elevation: Theme.crateSleeveElevation; shadowColor: Theme.shadowColor }`.
4. **`RecordStage.qml:294`:** `StrmMask { anchors.fill: parent; source: labelArt; maskSource: labelMask }`.
5. **`CratePortrait.qml:86`:** `StrmMask { anchors.fill: parent; source: avatar; maskSource: circle }`.
6. **`HomePage.qml:283`:** `layer.effect: StrmBackdropBlur {}`. Update the comment at `:232` from "a blurred MultiEffect layer" to "a blurred layer".
7. **`PersonPage.qml:227`:** `layer.effect: StrmBackdropBlur {}`.

Then:

```bash
grep -rn "QtQuick.Effects\|MultiEffect\|Qt5Compat" src/ui --include=*.qml | grep -v '^src/ui/shims/'
```

Expected: no output. Comments that mention MultiEffect must go too, or be reworded to name the shim.

- [ ] **Step 6: Tests stage the shims**

In `tests/CMakeLists.txt`, inside `strmqt_add_test`, add after the existing `target_compile_definitions` line:

```cmake
    # Tests that stage a copied StrmQt module copy the tier's shims too
    # (tests/mocks/QmlShimStaging.h, spec 2026-09-27 §4.3).
    target_compile_definitions(${name} PRIVATE
        STRMQT_SHIMS_DIR="${CMAKE_SOURCE_DIR}/src/ui/shims/${STRMQT_QML_TIER_RESOLVED}")
```

Create `tests/mocks/QmlShimStaging.h`:

```cpp
#pragma once

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QString>
#include <QStringList>

namespace strmqt::test {

// Copies the build's tier shims (src/ui/shims/<tier>, spec 2026-09-27 §4.3)
// into a staged StrmQt module and returns their qmldir lines, so a test that
// stages StrmIcon, StrmPanel and friends by copying files resolves StrmTint and
// the rest exactly as the app does. Returns an empty array on failure.
inline QByteArray stageShims(const QString &modulePath)
{
    const QDir shims(QStringLiteral(STRMQT_SHIMS_DIR));
    const QStringList files = shims.entryList({QStringLiteral("*.qml")}, QDir::Files, QDir::Name);
    if (files.isEmpty())
        return {};
    QByteArray lines;
    for (const QString &file : files) {
        const QString target = modulePath + QLatin1Char('/') + file;
        QFile::remove(target);
        if (!QFile::copy(shims.filePath(file), target))
            return {};
        lines += file.chopped(4).toUtf8() + " 1.0 " + file.toUtf8() + '\n';
    }
    return lines;
}

} // namespace strmqt::test
```

Find every staging test that copies a file which now uses a shim:

```bash
grep -ln 'qmldir' tests/unit/*.cpp | xargs grep -lE 'StrmIcon|StrmPanel|RecordStage|CratePortrait|HomePage|PersonPage|StrmButton|StrmIconButton|StrmChip|StrmCard|StrmMenu|MiniPlayer|Music'
```

In each file it lists:
- include `"QmlShimStaging.h"`;
- after the staged files are copied and before the qmldir is written, append `stageShims(modulePath)` to the qmldir text;
- fail the staging (return `nullptr`, or `QVERIFY`, whichever the file already does on a copy failure) if it returns empty.

The mocks directory is added per target today (`target_include_directories(tst_player_skip PRIVATE mocks)`), so also add `target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/mocks)` to `strmqt_add_test`. That puts the header on every test's include path; the existing per-target lines stay, since they are harmless duplicates and removing them is out of scope.

- [ ] **Step 7: Log the tier at startup**

In `src/app/main.cpp`, next to the existing startup log line (the one that logs the version), add:

```cpp
    qCInfo(logApp) << "QML tier:" << STRMQT_QML_TIER << "on Qt" << qVersion();
```

Use whichever logging category `main.cpp` already uses.

- [ ] **Step 8: Host gates, both tiers**

Run gates **H** (`NN=05`) and **HC**.

Expected:
- both are green;
- the H configure log says `StrmQt QML tier: full`, and HC's says `compat`;
- HC's self-test log contains `QML tier: compat`;
- the qmllint baseline (H only) shows no new fingerprints. If qmllint reports the shims' unknown `elevation.blur` accesses as new warnings, type the property as `var` (already done). Do not update the baseline file to hide real warnings. If the new files add warnings, stop and report them.

- [ ] **Step 9: 6.4.2 container**

Run gate **C(ubuntu-24.04)** (`/tmp/w05a-ubuntu-24.04`).

Expected, the first time: QML compile errors remain only for `font.features` and `font.variableAxes` (Task 6).

If `qmlcachegen` fails the build there, check that the ubuntu-24.04 build gets past compilation with Task 6's files stubbed:
- temporarily comment out the `font.features` and `font.variableAxes` lines;
- run the container check;
- **restore them**;
- record the result in the verifications file.

Task 6 makes this container check green; this task only has to show that no error mentions an effect.

- [ ] **Step 10: Visual check (user)**

Ask the user to run both tiers side by side on the host:

```bash
/tmp/w05a/strmqt   # full
/tmp/w05b/strmqt   # compat
```

Ask them to compare:
- icons, especially the hover colour animation on a button;
- a settings panel's shadow;
- the album page's record label and a crate portrait;
- the home and person backdrop wash.

Record their answer ("matches" / "differs: …") in the verifications file. Any "differs" that is more than the spec §3 table allows goes back into the shim before commit.

- [ ] **Step 11: Commit**

```bash
git add cmake/StrmQtQmlTier.cmake src/ui/shims CMakeLists.txt src/CMakeLists.txt src/app/main.cpp \
        src/ui/controls/StrmIcon.qml src/ui/controls/StrmPanel.qml src/ui/music/RecordStage.qml \
        src/ui/music/CratePortrait.qml src/ui/pages/HomePage.qml src/ui/pages/PersonPage.qml \
        tests/CMakeLists.txt tests/mocks/QmlShimStaging.h tests/unit/<each staging test changed> \
        docs/superpowers/plans/2026-09-27-distro-compat-075-verifications.md
git commit -m "feat(ui): select effect shims per Qt tier (MultiEffect or Qt5Compat)

QtQuick.Effects is Qt 6.5+. StrmTint, StrmShadow, StrmMask and
StrmBackdropBlur each exist twice under src/ui/shims (full: MultiEffect;
compat: Qt5Compat.GraphicalEffects), and CMake adds exactly one tier's
directory to the StrmQt module (STRMQT_QML_TIER=auto|full|compat, auto =
full on Qt >= 6.8). No caller imports an effects module any more. Tests
that stage the module copy the build's shims.

Visual check, both tiers on Qt 6.11: <the user's answer>.

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_013JJsS9enEqUZ9WD9HD99Vw"
```

---

### Task 6: Tabular figures and the Archivo width as tier shims

**Files:**
- Create: `src/ui/shims/{full,compat}/{TabularText,TabularMetrics,CrateDisplayText}.qml`
- Modify: `src/ui/shims/Shims.cmake` (add the three names)
- Modify, replacing `font.features` and `font.variableAxes`:
  - `src/ui/music/CrateKicker.qml`;
  - `src/ui/music/CrateBadge.qml`;
  - `src/ui/shell/MiniPlayer.qml` (two sites);
  - `src/ui/music/MusicNowPlaying.qml`;
  - `src/ui/music/MusicPlayerPanel.qml`;
  - `src/ui/pages/MusicHomePage.qml`;
  - `src/ui/music/CrateHeading.qml`.
- Modify: `tests/unit/tst_focus_clip.cpp` (compat-only `QSKIP` of the Archivo-width assertion, if Step 5 shows it needs one)

**Interfaces:**
- Consumes: Task 5's mechanism and `stageShims`.
- Produces: `TabularText`, `TabularMetrics` and `CrateDisplayText` (spec §4.3). The whole app compiles on Qt 6.4.2.

- [ ] **Step 1: Write the six files**

`src/ui/shims/full/TabularText.qml`:

```qml
import QtQuick

// Tier shim (spec 2026-09-27 §4.3): Text with tabular figures, so counters and
// times do not jitter. font.features is Qt 6.6+.
Text {
    font.features: ({ "tnum": 1 })
}
```

`src/ui/shims/compat/TabularText.qml`:

```qml
import QtQuick

// Tier shim (spec §4.3), compat tier: no font.features before Qt 6.6. Every
// user sets IBM Plex Mono, whose figures are fixed-width anyway (spec §3).
Text {
}
```

`TabularMetrics.qml` is the same pair with `TextMetrics` as the root type.

`src/ui/shims/full/CrateDisplayText.qml`:

```qml
import QtQuick
import StrmQt

// Tier shim (spec §4.3): Text in Archivo at the crate display width.
// font.variableAxes is Qt 6.7+.
Text {
    font.variableAxes: Theme.crateDisplayAxes
}
```

`src/ui/shims/compat/CrateDisplayText.qml`:

```qml
import QtQuick

// Tier shim (spec §4.3), compat tier: no font.variableAxes before Qt 6.7, so
// Archivo renders at its default width (about 17% narrower); the weight still
// comes from font.weight (spec §3).
Text {
}
```

Add `TabularText TabularMetrics CrateDisplayText` to `_strmqt_shims` in `Shims.cmake`.

- [ ] **Step 2: Replace the sites**

For each site:
- change the element's type (`Text` → `TabularText`, `TextMetrics` → `TabularMetrics`);
- delete its `font.features: …` line;
- keep every other binding.

| File | Site |
|---|---|
| `src/ui/music/CrateKicker.qml` | the root `Text` |
| `src/ui/music/CrateBadge.qml` | the inner `Text` (around line 36) |
| `src/ui/shell/MiniPlayer.qml` | `timeMetrics` (around line 315, `TextMetrics`) and the `Text` around 944–956 |
| `src/ui/music/MusicNowPlaying.qml` | the `Text` around 428–437 |
| `src/ui/music/MusicPlayerPanel.qml` | the `Text` around 435–441 |
| `src/ui/pages/MusicHomePage.qml` | the `Text` around 690–696 |

For `src/ui/music/CrateHeading.qml`:
- the root `Text` becomes `CrateDisplayText`;
- delete `font.variableAxes: Theme.crateDisplayAxes`;
- add to the header comment: "The width axis is applied by CrateDisplayText (a tier shim), since font.variableAxes is Qt 6.7+."

Check that nothing is left:

```bash
grep -rn "font.features\|variableAxes" src/ui --include=*.qml | grep -v '^src/ui/shims/'
```

Expected: no output.

A root element that becomes a shim type keeps its `id` and its properties. If a file's root `Text` also declares `property` members, that is still fine: a shim is an ordinary QML type.

- [ ] **Step 3: Host gates, both tiers**

Run gates **H** (`NN=06`) and **HC**.

Expected: both green, with **H**'s qmllint showing no new fingerprints.

`tst_crate_tokens`, `tst_crate_controls` and `tst_focus_clip` stage `CrateHeading`, so they must now stage the shims. If one fails with `CrateDisplayText is not a type`, it was missed in Task 5 Step 6: add `stageShims` there.

- [ ] **Step 4: 6.4.2 container: the floor builds**

Run gates **C(ubuntu-24.04)** and **C(debian-12)**.

Expected:
- the build completes;
- the self-test shows `selftest: 15/15 pages constructed` and `QML tier: compat on Qt 6.4.2`;
- ctest may still show failures. **Record them all in the verifications file** under `## Task 6: first full 6.4 run`, with test name and first failing message. They are Task 7's input.
- The only failures this task must fix are those caused by the shims themselves (a missing staging, a wrong property).

- [ ] **Step 5: `tst_focus_clip` on compat**

If HC's `tst_focus_clip` fails **only** on the genre-cell overrun assertion that depends on the Archivo width (commit `2aac8bb`), guard that one assertion:

```cpp
    if (QStringLiteral(STRMQT_QML_TIER_NAME) == QLatin1String("compat"))
        QSKIP("Compat tier: Archivo renders at its default width without font.variableAxes "
              "(spec 2026-09-27 §4.3), so this overrun is not reproduced there.");
```

To provide the name, add `target_compile_definitions(tst_focus_clip PRIVATE STRMQT_QML_TIER_NAME="${STRMQT_QML_TIER_RESOLVED}")` in `tests/CMakeLists.txt`.

The `QSKIP` goes immediately before the width-dependent `QVERIFY`. If other functions in the same test then do not run, move that assertion into its own test function first.

If `tst_focus_clip` passes on HC, change nothing.

- [ ] **Step 6: Visual check (user)**

Ask the user to compare the Music home page (crate headings, section strip, badges) and the mini-player time readout in `/tmp/w06a/strmqt` and `/tmp/w06b/strmqt`.

Expected: on compat, the headings are narrower and nothing else differs. Record the answer.

- [ ] **Step 7: Commit**

```bash
git add src/ui/shims src/ui/music/CrateKicker.qml src/ui/music/CrateBadge.qml src/ui/shell/MiniPlayer.qml \
        src/ui/music/MusicNowPlaying.qml src/ui/music/MusicPlayerPanel.qml src/ui/pages/MusicHomePage.qml \
        src/ui/music/CrateHeading.qml docs/superpowers/plans/2026-09-27-distro-compat-075-verifications.md
# plus tests/unit/tst_focus_clip.cpp and tests/CMakeLists.txt if Step 5 changed them
git commit -m "feat(ui): tabular figures and Archivo width as tier shims

font.features (Qt 6.6) and font.variableAxes (Qt 6.7) move into
TabularText, TabularMetrics and CrateDisplayText. On the compat tier
they are plain Text/TextMetrics: every tabular site is IBM Plex Mono,
whose figures are fixed-width already, and crate headings render at
Archivo's default width. With this the whole app compiles on Qt 6.4.2.

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_013JJsS9enEqUZ9WD9HD99Vw"
```

---

### Task 7: Qt 6.4 behaviour sweep

**Time box:** one working session. The goal is a green `C(ubuntu-24.04)` and `C(debian-12)`, with every deviation either fixed or recorded.

**Files:**
- Modify: whatever each failure needs, as the smallest change that is right on every Qt, each behind a `QT_VERSION_CHECK` where the behaviour genuinely differs.
- Modify: the verifications file.

**Interfaces:**
- Consumes: Task 6's failure list.
- Produces: green container checks on both 6.4.2 distros, and a record of what 6.4 did differently.

Use superpowers:systematic-debugging for every failure. **A test is never loosened to pass on 6.4.** If the 6.4 behaviour is a real Qt difference the app can tolerate, the test gains a version-gated expectation with a comment naming the Qt change; otherwise the app is fixed.

- [ ] **Step 1: KeyNavigation targets (spec §4.4)**

```bash
scripts/ci/local.sh ubuntu-24.04 /tmp/w07a-ubuntu-24.04 -- bash -c '
  apt-get install -y --no-install-recommends qt6-declarative-private-dev >/dev/null 2>&1
  f=$(find /usr/include -name qquickitem_p.h | head -1); echo "$f"
  grep -n -A12 "class QQuickKeyNavigationAttachedPrivate" "$f"'
```

(`apt-get` works here because the container runs as root and is discarded; the image is unchanged.)

Expected: the member declarations `left`, `right`, `up`, `down`, `tab` and `backtab`. Record whether they are `QPointer<QQuickItem>` or `QQuickItem *`.

If they are raw pointers:
- in `src/ui/pages/MusicAlbumPage.qml` (the reverse `KeyNavigation` wiring around `:596-605`), clear the targets that point into the shelf when the shelf is destroyed (`Component.onDestruction`) and set them to `null`;
- add a comment naming Qt 6.4's raw-pointer `KeyNavigation`.

- [ ] **Step 2: Work the failure list**

For each failure recorded in Task 6, run the test alone in the container:

```bash
scripts/ci/local.sh ubuntu-24.04 /tmp/w07a-ubuntu-24.04 -- bash -c \
  'QT_QPA_PLATFORM=offscreen /build/<test> <function> 2>&1 | tail -40'
```

The `/build` tree is Task 6's `check.sh` build; rebuild the test first with `cmake --build /build --target <test>`.

Classify each failure in the verifications file as one of:
- **app bug on 6.4**: fixed in the app;
- **Qt difference, tolerated**: a version-gated test expectation, with the reason;
- **test harness**: fixed in the test.

Expected families (spec §4.4):
- focus-chain clearing;
- Tab skipping invisible items;
- XF86OK mapping (`tst_remote_ok_key`);
- ShortcutOverride and modifiers;
- pixel measurements;
- untyped-annotation coercion (a QML function receiving a string where 6.7+ would coerce).

- [ ] **Step 3: Page walk under the self-test**

The self-test constructs every page, but constructing is not using. In the container, run the self-test log through the warnings filter:

```bash
scripts/ci/local.sh ubuntu-24.04 /tmp/w07a-ubuntu-24.04 -- bash -c \
  'grep -E "qml:|TypeError|ReferenceError|Unable to assign|Binding loop" /build/selftest.log | sort | uniq -c | sort -rn | head -40'
```

Compare with the same filter over the host's `/tmp/w07a/selftest.log`: run gate **H** first, since `selftest.sh` writes `$build/selftest.log` only when given the path, so pass it.

Every warning that appears **only** on 6.4 is investigated and either fixed or recorded with its reason.

- [ ] **Step 4: Both floors green, and the host too**

Run gates **C(ubuntu-24.04)**, **C(debian-12)**, **H** and **HC**.

Expected: all four end `OK`. If the time box runs out first, stop and report the remaining failures with their classification. Tasks 8+ depend on a green floor, so the orchestrator decides with the user whether to extend the box or ship a documented exception.

- [ ] **Step 5: Commit**

Commit each distinct fix separately as it lands, as `fix(compat): <what> on Qt 6.4` with the cause in the body, staging only the files that fix touched. Finish with the record:

```bash
git add docs/superpowers/plans/2026-09-27-distro-compat-075-verifications.md
git commit -m "docs(compat): record the Qt 6.4 behaviour sweep

<one line per finding: test, cause, resolution>

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_013JJsS9enEqUZ9WD9HD99Vw"
```

---

### Task 8: Bundle SDL3 3.4.16 statically where the distro has none

**Files:**
- Create: `cmake/StrmQtSdl3.cmake`
- Modify: `src/CMakeLists.txt` (the SDL3 block: `option(STRMQT_WITH_SDL3 …)`, `pkg_check_modules(SDL3 …)`, and the `if(SDL3_FOUND)` sites)
- Modify: `scripts/ci/deps.sh` (`--cmake-args` for `ubuntu-24.04` and `debian-12`)
- Modify: `packaging/appimage/build-appimage.sh` (only the `FORBIDDEN` comment for `libSDL`, if it claims SDL comes from the host)

**Interfaces:**
- Consumes: nothing new.
- Produces:
  - `STRMQT_BUNDLE_SDL3` (default OFF);
  - `STRMQT_SDL3_TARGET`, the target to link: `SDL3::SDL3-static` when bundled, `PkgConfig::SDL3` otherwise;
  - `STRMQT_HAVE_SDL3`, true when either path provides SDL3.

- [ ] **Step 1: `cmake/StrmQtSdl3.cmake`**

```cmake
# SDL3 for the gamepad path (spec 2026-09-27 §5). The distro's SDL3 by default;
# with STRMQT_BUNDLE_SDL3=ON (Ubuntu 24.04 and Debian 12 packages, the
# AppImage) a pinned 3.4.16 tarball, built static with the gamepad subsystems
# only. A distro build never fetches anything it did not ask for.
option(STRMQT_BUNDLE_SDL3 "Fetch and statically link SDL3 3.4.16 (for distros without SDL3)" OFF)

set(STRMQT_HAVE_SDL3 OFF)
if(NOT STRMQT_WITH_SDL3)
    return()
endif()

if(STRMQT_BUNDLE_SDL3)
    include(FetchContent)
    # The gamepad path needs JOYSTICK, HAPTIC, HIDAPI, SENSOR and EVENTS; the
    # rest is off so the static archive stays near 2 MB and pulls no X11,
    # Wayland, audio or GPU dependency into StrmQt's link.
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    foreach(_sub AUDIO VIDEO RENDER GPU CAMERA DIALOG TRAY)
        set(SDL_${_sub} OFF CACHE BOOL "" FORCE)
    endforeach()
    FetchContent_Declare(SDL3
        URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-3.4.16.tar.gz
        URL_HASH SHA256=7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68)
    # No EXCLUDE_FROM_ALL argument: it needs CMake 3.28 and the floor is 3.25.
    # SDL_INSTALL=OFF keeps it out of the install tree instead.
    FetchContent_MakeAvailable(SDL3)
    set(STRMQT_SDL3_TARGET SDL3::SDL3-static)
    set(STRMQT_HAVE_SDL3 ON)
    install(FILES ${sdl3_SOURCE_DIR}/LICENSE.txt
            DESTINATION ${CMAKE_INSTALL_DOCDIR} RENAME SDL3-LICENSE.txt)
    message(STATUS "SDL3: bundled 3.4.16 (static)")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL3 IMPORTED_TARGET sdl3)
    if(SDL3_FOUND)
        set(STRMQT_SDL3_TARGET PkgConfig::SDL3)
        set(STRMQT_HAVE_SDL3 ON)
        message(STATUS "SDL3: system ${SDL3_VERSION}")
    else()
        message(STATUS "SDL3: not found; gamepad support disabled "
                       "(set STRMQT_BUNDLE_SDL3=ON to bundle it)")
    endif()
endif()
```

`CMAKE_INSTALL_DOCDIR` needs `include(GNUInstallDirs)`; check whether the top-level `CMakeLists.txt` already includes it, and include it in this module if not.

- [ ] **Step 2: Use it**

In `src/CMakeLists.txt`:
- keep `option(STRMQT_WITH_SDL3 …)`;
- replace the `pkg_check_modules(SDL3 IMPORTED_TARGET sdl3)` line with `include(${CMAKE_SOURCE_DIR}/cmake/StrmQtSdl3.cmake)`;
- change every `if(SDL3_FOUND)` to `if(STRMQT_HAVE_SDL3)`;
- change every `PkgConfig::SDL3` to `${STRMQT_SDL3_TARGET}`.

Then check that nothing is left:

```bash
grep -n "SDL3" src/CMakeLists.txt tests/CMakeLists.txt
```

Expected: no `SDL3_FOUND` or `PkgConfig::SDL3` anywhere.

The `return()` in the module returns from the include only, which is why it is a separate file.

- [ ] **Step 3: deps.sh**

In `scripts/ci/deps.sh`, before the `case "$mode"` that prints, set:

```bash
case "$target" in
    # No SDL3 package on these releases (spec §5): bundle the pinned static one.
    ubuntu-24.04|debian-12) cmake_args="-DSTRMQT_BUNDLE_SDL3=ON" ;;
esac
```

`libudev-dev` is already in their `apt_extra`. SDL's HIDAPI and joystick backends use it for hot-plug.

- [ ] **Step 4: Host, default OFF unchanged**

Run gate **H** (`NN=08`).

Expected:
- green;
- the configure log says `SDL3: system 3.x`;
- `ldd /tmp/w08a/strmqt | grep -i sdl` shows the system `libSDL3.so.0`.

- [ ] **Step 5: Host, bundled**

```bash
mkdir -p /tmp/w08b/tmp && export TMPDIR=/tmp/w08b/tmp
cmake -S . -B /tmp/w08b -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSTRMQT_WERROR=ON -DSTRMQT_BUNDLE_SDL3=ON 2>&1 | grep -E "SDL3|Error"
cmake --build /tmp/w08b && ctest --test-dir /tmp/w08b --output-on-failure | tail -3
ldd /tmp/w08b/strmqt | grep -ci sdl
STRMQT_SELFTEST=1 QT_QPA_PLATFORM=offscreen /tmp/w08b/strmqt 2>&1 | grep -E "SDL3 gamepad support active|selftest:"
```

Expected:
- `SDL3: bundled 3.4.16 (static)`;
- `100% tests passed`;
- `0` from `ldd` (no shared SDL);
- the line `SDL3 gamepad support active` and `selftest: 15/15 pages constructed`.

If `-Werror` trips on SDL's own sources, the warnings flags are leaking into the subproject. Scope the project's warning flags to StrmQt's own targets rather than turning `STRMQT_WERROR` off. Check how `STRMQT_WERROR` is applied first (`grep -rn WERROR CMakeLists.txt cmake src/CMakeLists.txt`).

- [ ] **Step 6: Containers**

Run gates **C(ubuntu-24.04)** and **C(debian-12)**. `deps.sh --cmake-args` now passes the bundle flag.

Expected:
- `check.sh: OK (compat)`;
- `grep "SDL3 gamepad support active" /tmp/w08a-ubuntu-24.04/selftest.log` finds the line.

Then run **C(debian-13)**. Expected: `SDL3: system 3.2.10`, and `OK (full)`.

- [ ] **Step 7: Commit, with the stated reason**

```bash
git add cmake/StrmQtSdl3.cmake src/CMakeLists.txt scripts/ci/deps.sh
# plus packaging/appimage/build-appimage.sh if its comment changed
git commit -m "build(input): bundle SDL3 3.4.16 statically where the distro has none

Dependency reason (AGENTS.md): SDL3 is the only gamepad path, and Ubuntu
24.04 LTS and Debian 12 do not package it. It is bundled statically —
pinned tarball and hash, gamepad subsystems only, about 2 MB — only in
builds that set STRMQT_BUNDLE_SDL3=ON: those two distros' packages and the
AppImage. Every other build links the distro's SDL3.

3.4.16 is the newest stable release (2026-09-27) and what Fedora 43 ships;
GamepadManager uses only SDL 3.0-era calls. SDL3 is zlib-licensed; its
LICENSE.txt is installed as share/doc/strmqt/SDL3-LICENSE.txt.

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_013JJsS9enEqUZ9WD9HD99Vw"
```

---

### Task 9: CI enforces the floor (`ubuntu:24.04`) and the oldest full tier (`debian:trixie`)

**Files:**
- Modify: `.github/workflows/ci.yml`

**Interfaces:**
- Consumes: `deps.sh` and `check.sh`.
- Produces: the CI jobs `floor` and `debian-13`.

- [ ] **Step 1: Add the jobs**

Append to `jobs:` in `.github/workflows/ci.yml`. Keep the existing Arch job unchanged, and match its `actions/checkout` pin exactly (`grep -n "actions/checkout" .github/workflows/ci.yml`):

```yaml
  # The Qt floor (spec 2026-09-27 §8.2): Ubuntu 24.04's Qt 6.4.2, the compat
  # QML tier and the bundled SDL3. The same commands a developer runs through
  # scripts/ci/local.sh. qmllint stays on the Arch job: its baseline is 6.11's.
  floor:
    runs-on: ubuntu-latest
    container: ubuntu:24.04
    steps:
      - uses: actions/checkout@<same pin as the arch job>
      - name: Dependencies
        run: scripts/ci/deps.sh ubuntu-24.04
      - name: Build, test, self-test
        run: scripts/ci/check.sh "$RUNNER_TEMP/build" $(scripts/ci/deps.sh --cmake-args ubuntu-24.04)
      - name: Self-test log
        if: always()
        uses: actions/upload-artifact@<same pin as used elsewhere in the repo's workflows, or v4>
        with:
          name: selftest-floor
          path: ${{ runner.temp }}/build/selftest.log
          if-no-files-found: ignore

  # The oldest full-tier Qt the release supports: Debian 13's 6.8.2.
  debian-13:
    runs-on: ubuntu-latest
    container: debian:trixie
    steps:
      - uses: actions/checkout@<same pin>
      - name: Dependencies
        run: scripts/ci/deps.sh debian-13
      - name: Build, test, self-test
        run: scripts/ci/check.sh "$RUNNER_TEMP/build" $(scripts/ci/deps.sh --cmake-args debian-13)
```

`actions/checkout` in a bare Ubuntu container works without git: it falls back to the REST API. `deps.sh` installs git anyway for later steps.

- [ ] **Step 2: Lint**

```bash
podman run --rm --security-opt label=disable -v "$PWD:/repo:ro" -w /repo docker.io/rhysd/actionlint:1.7.12 -color=never
```

Expected: no output, exit 0. `shellcheck` findings on the `run:` lines are fixed, not suppressed.

- [ ] **Step 3: Rehearse both jobs locally**

The jobs run exactly `deps.sh` and `check.sh`, so the local check *is* the rehearsal. Run gates **C(ubuntu-24.04)** and **C(debian-13)** (`NN=09`).

Expected: `check.sh: OK (compat)` and `check.sh: OK (full)`.

- [ ] **Step 4: Commit**

```bash
git add .github/workflows/ci.yml
git commit -m "ci: build and test on Ubuntu 24.04 (Qt 6.4) and Debian 13 (Qt 6.8)

floor enforces the Qt 6.4.2 floor with the compat QML tier and bundled
SDL3; debian-13 covers the oldest full-tier Qt. Both run scripts/ci's
deps.sh and check.sh, the commands used locally through podman; both
were green locally before this commit.

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_013JJsS9enEqUZ9WD9HD99Vw"
```

---

### Task 10: MPRIS `CanGoPrevious` matches what `skipBack` does

**Files:**
- Modify: `src/playback/PlayerController.cpp` (`canSkipBack`, around line 1107)
- Modify: `src/playback/PlayerController.h` (the `canSkipBack` comment: "For music, also true…")
- Test: `tests/integration/tst_player_skip.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces: `canSkipBack()` is true for any active item past the restart threshold.

- [ ] **Step 1: Write the failing test**

In `tests/integration/tst_player_skip.cpp`, next to `musicCanSkipBackOnceARestartWouldHappen`, add a test modelled on it. Reuse its helpers: `itemMap`, `detailsWithChapters`, `start(items, index, expectedChapters)`, and `FakePlayerBackend`'s `simulateState`, `simulateDuration` and `simulatePosition`. Read that test first and copy its exact structure, changing only the media type to a chapterless video:

```cpp
// spec 2026-09-27 §9.2: skipBack restarts any item once it is past the
// threshold, so MPRIS CanGoPrevious must say so for a chapterless video too.
void PlayerSkipTest::chapterlessVideoCanSkipBackOnceARestartWouldHappen()
{
    // One chapterless video, first in its queue: no chapter to step to, no previous item.
    start({itemMap(QStringLiteral("v1"), QStringLiteral("Movie"))}, 0, /*expectedChapters=*/0);
    // ... drive the fake backend to Playing with a duration, exactly as the music test does ...
    QVERIFY(!m_controller->canSkipBack());       // at the start: skipBack would do nothing

    // ... simulatePosition past the restart threshold, as the music test does ...
    QVERIFY(m_controller->canSkipBack());        // a restart would now happen

    m_controller->skipBack();
    QCOMPARE(m_backend->seeks.constLast(), 0);   // and skipBack does restart it
}
```

The `// ...` lines are the music test's own statements, copied. If `seeks` stores a different type (for example `qint64` milliseconds), compare with that.

Run:

```bash
mkdir -p /tmp/w10a/tmp && export TMPDIR=/tmp/w10a/tmp
cmake -S . -B /tmp/w10a -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSTRMQT_WERROR=ON >/dev/null
cmake --build /tmp/w10a --target tst_player_skip && /tmp/w10a/tst_player_skip chapterlessVideoCanSkipBackOnceARestartWouldHappen
```

Expected: `FAIL!` at the second `QVERIFY(m_controller->canSkipBack())`.

- [ ] **Step 2: Fix**

```cpp
bool PlayerController::canSkipBack() const
{
    // Mirrors skipBack(): a chapter to step to, an item to go back to, or a
    // restart of the current item once it is past the threshold. The restart
    // applies to every item, not only music (spec 2026-09-27 §9.2).
    return skipsByChapter() || hasPrevious() || (m_active && m_pastRestartThreshold);
}
```

Update the header comment: drop "For music, also…" and say "also true once the current item is past the restart threshold, since skipBack would restart it". If `m_isAudio` is now unused, the compiler will say so under `-Werror`; it is used elsewhere, so it should not be.

Check that the MPRIS `CanGoPrevious` change notification still fires when `m_pastRestartThreshold` flips for video. Find where the property's change signal is emitted:

```bash
grep -n "pastRestartThreshold\|canSkipBackChanged" src/playback/PlayerController.cpp
```

If that emission is also gated on `m_isAudio`, drop the gate there too, and extend the test with a `QSignalSpy` on `canSkipBackChanged` (or whatever the signal is called).

- [ ] **Step 3: Gate and commit**

Run gate **H** (`NN=10`), expecting green, then **C(ubuntu-24.04)**, expecting `OK (compat)`.

```bash
git add src/playback/PlayerController.cpp src/playback/PlayerController.h tests/integration/tst_player_skip.cpp
git commit -m "fix(playback): CanGoPrevious is true once any item would restart

skipBack restarts the current item past the threshold whatever it is,
but canSkipBack only said so for audio, so MPRIS reported CanGoPrevious
false five seconds into the first item of a chapterless video.

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_013JJsS9enEqUZ9WD9HD99Vw"
```

---

### Task 11: The web remote's Next/Previous skip like every other surface

**Files:**
- Modify: `src/remote/WebRemoteServer.cpp` (`handleApiPlayback`, the `next`/`previous` actions around line 1794)
- Modify: `ARCHITECTURE.md` (§3 "Skip and fast-forward": drop the web-remote exception)
- Test: `tests/unit/tst_web_remote_server.cpp`, and `tests/CMakeLists.txt` if the target needs `mocks/FakePlayerBackend.h` added

**Interfaces:**
- Consumes: nothing new.
- Produces: `POST /api/playback` `{"action":"next"}` calls `skipForward()`, and `previous` calls `skipBack()`. The "Play next" item action (`m_actions->playNext(map)`, around line 1746) is unchanged.

- [ ] **Step 1: Write the failing test**

Read `tst_web_remote_server.cpp` first:
- its fixture builds `WebRemoteServer(Settings*, PlayerController*, ItemActions*, HomeController*, SessionController*, emby::EmbyClient*, QObject*)`;
- it listens on port 18337 through `m_settings`;
- it sends with its `request()` helper over HTTPS.

If the fixture passes `nullptr` for the controller, build a real `PlayerController` over `FakePlayerBackend` for this test. Take the construction from `tests/integration/tst_player_skip.cpp`, which already does it; the target already has `mocks` on its include path.

```cpp
// spec 2026-09-27 §9.3: the web remote's Next/Previous are the same verbs as
// the remote keys, the pad and MPRIS: skipForward/skipBack, which step by
// chapter when the item has chapters.
void WebRemoteServerTest::nextAndPreviousStepByChapter()
{
    // A playing film with three chapters (0 s, 600 s, 1200 s), positioned at 700 s,
    // set up the way tst_player_skip's chapter tests do.
    // ...

    QCOMPARE(request("POST", "/api/playback", R"({"action":"next"})").status, 200);
    QCOMPARE(backend->seeks.constLast(), 1200 * 1000);   // next chapter, not the next item
    QCOMPARE(backend->loadedUrls.size(), 1);             // no new item was loaded

    QCOMPARE(request("POST", "/api/playback", R"({"action":"previous"})").status, 200);
    // back to the start of the current chapter, or the previous chapter, per skipBack's rule
    // (read PlayerController::skipBack and assert what it does for this position)
}
```

Use the units and helper names the files actually use. The assertions that matter are:
- **no second `loadedUrls` entry after `next`**;
- **a chapter seek**.

Run it on the host (`/tmp/w11a`) and expect it to fail: `playNext` loads the next item, or does nothing with a single-item queue, so there is no chapter seek.

- [ ] **Step 2: Fix**

In `WebRemoteServer.cpp`, the `next` and `previous` branches call `m_player->skipForward()` and `m_player->skipBack()`, each with the comment `// The same verb as the remote keys, pad and MPRIS (ARCHITECTURE.md §3).` Do not touch `m_actions->playNext(map)`.

In `ARCHITECTURE.md` §3 "Skip and fast-forward", delete the sentence that says the web remote still steps by queue entry (added in `2c7c6ac`). If the paragraph lists the surfaces that call `skipForward`/`skipBack`, add the web remote to the list.

- [ ] **Step 3: Gate and commit**

Run gate **H** (`NN=11`), expecting green.

```bash
git add src/remote/WebRemoteServer.cpp ARCHITECTURE.md tests/unit/tst_web_remote_server.cpp
# plus tests/CMakeLists.txt if changed
git commit -m "fix(remote): web remote Next/Previous skip by chapter like every other surface

The web remote called playNext/playPrevious, so in a chaptered film it
jumped to the next queue item while the remote keys, pad and MPRIS
stepped chapters. It now calls skipForward/skipBack. The \"Play next\"
item action (the up-next insert) is a different verb and is unchanged.

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_013JJsS9enEqUZ9WD9HD99Vw"
```

---
### Task 12: Secret backends, part 1: transport seam, KWallet 5 and 6, probe order

**Files:**
- Create: `src/platform/secrets/DBusTransport.h`, `src/platform/secrets/DBusTransport.cpp`
- Create: `src/platform/secrets/SecretBackend.h`, `src/platform/secrets/SecretBackend.cpp`, holding `chooseSecretBackends` and `makeSecretBackend`
- Create: `src/platform/secrets/KWalletBackend.h`, `src/platform/secrets/KWalletBackend.cpp`
- Modify: `src/platform/SecretsStore.h` and `.cpp`:
  - production virtuals delegate to the chosen backend;
  - a transport constructor;
  - `backendName`.
- Modify: `src/CMakeLists.txt`: add the new sources next to `platform/SecretsStore.h platform/SecretsStore.cpp`, in the same target.
- Create: `tests/mocks/FakeDBusTransport.h`, `tests/unit/tst_secret_backends.cpp`
- Modify: `tests/CMakeLists.txt` (`strmqt_add_test(tst_secret_backends unit/tst_secret_backends.cpp)`, plus `Qt6::DBus` if the core target does not export it)
- **Untouched:** `tests/unit/tst_secrets_store.cpp` and `tests/mocks/FakeSecretsStore.h`. Their seam is the state machine's, and it keeps its signatures.

**Interfaces:**
- Consumes: nothing new.
- Produces:

```cpp
namespace strmqt::secrets {

// The one way the secret backends reach D-Bus (spec 2026-09-27 §6.5).
class DBusTransport
{
public:
    using Done = std::function<void(const QDBusMessage &reply)>;
    using SignalHandler = std::function<void(const QDBusMessage &signal)>;
    virtual ~DBusTransport() = default;
    virtual bool connected() const = 0;
    // `done` runs once, only while `context` is alive.
    virtual void call(const QDBusMessage &message, QObject *context, Done done) = 0;
    // `handler` runs for each matching signal while `context` is alive.
    virtual bool connectSignal(const QString &service, const QString &path,
                               const QString &interface, const QString &name,
                               QObject *context, SignalHandler handler) = 0;
};

class SessionBusTransport final : public DBusTransport { /* QDBusConnection::sessionBus() */ };

// ServiceUnknown, NoReply, UnknownObject, UnknownMethod, NameHasNoOwner, Timeout (spec §6.2).
bool isUnavailableError(const QDBusMessage &reply);

enum class Outcome { Ok, Unavailable, Refused, Failed };
struct Reply
{
    Outcome outcome = Outcome::Failed;
    QString value;   // read(): the secret; empty = not stored
    QString error;
};
using Callback = std::function<void(const Reply &)>;

class SecretBackend
{
public:
    virtual ~SecretBackend() = default;
    virtual QString name() const = 0;             // "KWallet", "KWallet 5", "Secret Service"
    virtual void prepare(Callback done) = 0;      // find the wallet / collection
    virtual void open(Callback done) = 0;         // open / unlock it (may prompt)
    virtual void write(const QString &key, const QString &value, Callback done) = 0;
    virtual void read(const QString &key, Callback done) = 0;
    virtual void remove(const QString &key, Callback done) = 0;
};

enum class BackendKind { KWallet6, KWallet5, SecretService };
QList<BackendKind> chooseSecretBackends(const QStringList &owned, const QStringList &activatable,
                                        const QString &currentDesktop);
std::unique_ptr<SecretBackend> makeSecretBackend(BackendKind kind, DBusTransport &transport,
                                                 QObject *context);

class KWalletBackend final : public SecretBackend
{
public:
    KWalletBackend(int generation, DBusTransport &transport, QObject *context);  // 5 or 6
    // ...
};

} // namespace strmqt::secrets
```

`SecretsStore` gains:
- `explicit SecretsStore(std::shared_ptr<secrets::DBusTransport> transport, QObject *parent = nullptr);`. The default constructor delegates to it with a `SessionBusTransport`.
- `Q_PROPERTY(QString backendName READ backendName NOTIFY storageModeChanged)`:
  - the active backend's `name()` in Wallet mode;
  - `"vault file"` in PlaintextFallback mode;
  - `""` when Unknown;
  - `"keyring"` in Wallet mode with no backend object, which only happens under `FakeSecretsStore`.

The files live under `src/platform/secrets/`, use QtCore and QtDBus only (no QtGui, per the layer rule), and use the `strmqt::secrets` namespace.

- [ ] **Step 1: Write the fake transport**

`tests/mocks/FakeDBusTransport.h`:

```cpp
#pragma once

#include "platform/secrets/DBusTransport.h"

#include <QDBusMessage>
#include <QList>
#include <QPointer>

namespace strmqt::test {

// Records every D-Bus call a backend makes and lets the test answer it later,
// in the "hold, then complete" style of FakeSecretsStore. No bus is involved:
// replies are built with QDBusMessage::createReply from plain QVariants, so
// backends must accept both QDBusArgument and plain values (fromDBus).
class FakeDBusTransport final : public secrets::DBusTransport
{
public:
    struct Call
    {
        QDBusMessage message;
        QPointer<QObject> context;
        Done done;
        bool answered = false;
    };
    struct Subscription
    {
        QString service, path, interface, name;
        QPointer<QObject> context;
        SignalHandler handler;
    };

    bool busConnected = true;
    QList<Call> calls;
    QList<Subscription> subscriptions;

    bool connected() const override { return busConnected; }
    void call(const QDBusMessage &message, QObject *context, Done done) override
    {
        calls.append({message, context, std::move(done)});
    }
    bool connectSignal(const QString &service, const QString &path, const QString &interface,
                       const QString &name, QObject *context, SignalHandler handler) override
    {
        subscriptions.append({service, path, interface, name, context, std::move(handler)});
        return true;
    }

    // The oldest unanswered call; tests assert its member before answering.
    int pending() const
    {
        for (int i = 0; i < calls.size(); ++i)
            if (!calls[i].answered)
                return i;
        return -1;
    }
    const QDBusMessage &next() const { return calls[pending()].message; }

    void reply(const QVariantList &arguments)
    {
        Call &c = calls[pending()];
        c.answered = true;
        if (c.context)
            c.done(c.message.createReply(arguments));
    }
    void replyError(const QString &name, const QString &text = QStringLiteral("fake"))
    {
        Call &c = calls[pending()];
        c.answered = true;
        if (c.context)
            c.done(c.message.createErrorReply(name, text));
    }
    void emitSignal(const QString &path, const QString &interface, const QString &name,
                    const QVariantList &arguments)
    {
        QDBusMessage signal = QDBusMessage::createSignal(path, interface, name);
        signal.setArguments(arguments);
        for (const Subscription &s : std::as_const(subscriptions))
            if (s.context && s.path == path && s.interface == interface && s.name == name)
                s.handler(signal);
    }
};

} // namespace strmqt::test
```

- [ ] **Step 2: Write the failing tests**

`tests/unit/tst_secret_backends.cpp` is a `QObject` test class with `QTEST_GUILESS_MAIN`. It covers the following. Task 13 adds the Secret Service cases.

1. **`chooseSecretBackends` table** (data-driven, `_data`), with columns owned, activatable, desktop and expected:
   - `{kwalletd6}`, `{}`, `GNOME` → `[KWallet6]`: an owned name counts on any desktop.
   - `{}`, `{kwalletd6}`, `KDE` → `[KWallet6]`.
   - `{}`, `{kwalletd6}`, `GNOME` → `[]`: activatable KWallet only on KDE.
   - `{}`, `{kwalletd5}`, `ubuntu:KDE` → `[KWallet5]`: the desktop is a colon list.
   - `{secrets}`, `{}`, `GNOME` → `[SecretService]`.
   - `{}`, `{secrets}`, `XFCE` → `[SecretService]`.
   - `{kwalletd6, kwalletd5, secrets}`, `{}`, `KDE` → `[KWallet6, KWallet5, SecretService]`: the order is fixed.
   - `{}`, `{}`, `KDE` → `[]`.

   The full service names are `org.kde.kwalletd6`, `org.kde.kwalletd5` and `org.freedesktop.secrets`.
2. **`isUnavailableError`**:
   - true for each of the six names in the interface comment, in their `org.freedesktop.DBus.Error.` form;
   - false for `org.freedesktop.DBus.Error.AccessDenied` and for a non-error reply.
3. **KWallet 5 wire format** (`kwallet5SpeaksTheKWalletProtocolAtItsOwnPath`):
   - `KWalletBackend(5, fake, &ctx)`. `prepare` sends `networkWallet` to service `org.kde.kwalletd5`, path `/modules/kwalletd5`, interface `org.kde.KWallet`. Reply `"kdewallet"` → `Ok`.
   - `open` sends `open("kdewallet", qlonglong 0, appId)`. Reply int `7` → `Ok`.
   - `write` sends `writePassword(7, "StrmQt", key, value, appId)`. Reply int `0` → `Ok`.
   - `read` → `readPassword(7, "StrmQt", key, appId)`. Reply `"tok"` → `Ok`, with value `"tok"`.
   - `remove` → `removeEntry(7, "StrmQt", key, appId)`. Reply `0` → `Ok`.
4. **KWallet 6** (`kwallet6KeepsTodaysWireFormat`): the same, at `org.kde.kwalletd6` and `/modules/kwalletd6`. This pins that existing Plasma 6 users still reach their `StrmQt` folder.
5. **Refused versus unavailable** (`kwalletOpenMinusOneIsRefused`, `kwalletServiceUnknownIsUnavailable`):
   - `open` answering `-1` → `Refused`;
   - `networkWallet` answering the error `org.freedesktop.DBus.Error.ServiceUnknown` → `Unavailable`;
   - any other error → `Failed`.
6. **`SecretsStore` end to end over the fake transport:**
   - `storeFallsThroughAnUnavailableBackend`:
     - `ListNames` answers `[org.kde.kwalletd6, org.kde.kwalletd5]`, and `ListActivatableNames` answers `[]`;
     - XDG desktop `KDE`, set with `qputenv` in the test and restored after;
     - kwalletd6's `networkWallet` answers `ServiceUnknown`;
     - kwalletd5 answers the whole protocol;
     - then `writeSecret` completes `Ok`, `storageMode() == Wallet` and `backendName() == "KWallet 5"`.
   - `storeGoesToTheVaultWhenAKeyringRefuses`:
     - kwalletd6 `open` answers `-1`;
     - there is **no** call to kwalletd5 or the Secret Service afterwards (assert the fake's `calls`);
     - the mode is `PlaintextFallback` and `backendName() == "vault file"`.
   - `storeUsesTheVaultWithNoCandidates`: both lists are empty → the vault, and the only calls made were the two `org.freedesktop.DBus` probes.
   - `storeUsesTheVaultWithoutASessionBus`: `busConnected = false` → the vault, with no calls.

Each store test uses `QStandardPaths::setTestModeEnabled(true)` and `setLegacyFilePathForTests(<QTemporaryDir path>/secrets.ini)`, so the vault is never the user's. Futures are awaited the way `tst_secrets_store.cpp` awaits them; copy its helper.

Add the target in `tests/CMakeLists.txt`:

```cmake
strmqt_add_test(tst_secret_backends unit/tst_secret_backends.cpp)
target_include_directories(tst_secret_backends PRIVATE mocks)
```

Run the build: it fails to compile because the headers do not exist. That is the red step.

- [ ] **Step 3: Implement the transport**

`src/platform/secrets/DBusTransport.cpp`. `SessionBusTransport::call` is `QDBusConnection::sessionBus().asyncCall(message)` with a `QDBusPendingCallWatcher` parented to `context`:
- `done(watcher->reply())` runs from `finished`, then `watcher->deleteLater()`;
- lift the reentrancy handling from `SecretsStore.cpp`'s `watchCall` unchanged, with its comment.

`connectSignal` uses a small private `QObject` receiver, parented to `context`, that holds the handler and exposes a slot `void deliver(const QDBusMessage &)`. It is connected with:

```cpp
QDBusConnection::sessionBus().connect(service, path, interface, name, receiver, SLOT(deliver(QDBusMessage)))
```

Since `service` for a Prompt signal is the unique name of the sender, pass an empty service there. A string-based `SLOT` is required by this `QDBusConnection::connect` overload.

`isUnavailableError` compares `reply.errorName()` against the six `org.freedesktop.DBus.Error.*` names.

- [ ] **Step 4: Implement `KWalletBackend` and the chooser**

`KWalletBackend(int generation, …)`:
- service `org.kde.kwalletd<generation>`, path `/modules/kwalletd<generation>`;
- interface `org.kde.KWallet`, folder `StrmQt`;
- `appId()` exactly as `SecretsStore.cpp` computes it today. Move the helper into `DBusTransport.h` as `secrets::applicationId()`, and have `SecretsStore.cpp` use it;
- `name()` is `"KWallet"` for 6 and `"KWallet 5"` for 5.

The backend keeps the wallet name from `prepare` and the handle from `open`. The reply checks are the ones `SecretsStore.cpp`'s `request*` methods do today: argument count, meta type, and write/remove answering `0`. Move them; do not rewrite them.

Error mapping:

| Reply | Outcome |
|---|---|
| an unavailable error | `Unavailable` |
| another error or a malformed reply | `Failed` |
| `open` → handle `< 0` | `Refused` |

`chooseSecretBackends`:
- split `currentDesktop` on `:`, and treat it as KDE if any part equals `KDE` (case-insensitive);
- then apply spec §6.2's rules in the order KWallet6, KWallet5, SecretService.

`makeSecretBackend` builds `KWalletBackend(6|5)`. For `SecretService` it returns `nullptr` until Task 13, with a `// Task 13` comment that Task 13 deletes. `SecretsStore` skips a null backend as unavailable.

- [ ] **Step 5: Delegate from `SecretsStore`**

In `SecretsStore.h`, add the members:

```cpp
    std::shared_ptr<secrets::DBusTransport> m_transport;
    QList<secrets::BackendKind> m_candidates;
    bool m_probed = false;
    std::unique_ptr<secrets::SecretBackend> m_backend;
```

Add the constructor and `backendName()` from Interfaces, and the forward declarations needed to keep the header light.

In `SecretsStore.cpp`:
- `walletTransportAvailable()` returns `m_transport && m_transport->connected()`.
- `requestNetworkWallet()`:
  1. If not yet probed, call `org.freedesktop.DBus.ListNames` and then `ListActivatableNames` (at `/org/freedesktop/DBus`, interface `org.freedesktop.DBus`) through the transport.
  2. Compute `m_candidates = chooseSecretBackends(owned, activatable, qEnvironmentVariable("XDG_CURRENT_DESKTOP"))`.
  3. Log the list once: `secrets: keyring candidates …`. Names only; never a key or a value.
  4. Call `tryNextBackend()`. A failed probe call counts as an empty list.
- `tryNextBackend()`:
  1. Pop candidates until `makeSecretBackend` yields one.
  2. Call its `prepare`.
     - `Ok`: `completeNetworkWallet(true, m_backend->name())`, which is non-empty, so the state machine proceeds.
     - `Unavailable`: log it, then `QMetaObject::invokeMethod(this, &SecretsStore::tryNextBackend, Qt::QueuedConnection)`. Queued, so the backend whose callback is running is never destroyed on its own stack. `tryNextBackend` resets `m_backend` at its start.
     - `Refused` or `Failed`: `completeNetworkWallet(false, {}, reply.error)`.
  3. With no candidates left, call `completeNetworkWallet(false, {}, QStringLiteral("no keyring reachable"))`.
- `requestOpenWallet(name)` calls `m_backend->open`.
  - `Ok`: `completeOpenWallet(true, 0)`.
  - `Refused` or `Failed`: `completeOpenWallet(false, -1, error)`, which goes to the vault. There is no second keyring (spec §6.2).
  - `Unavailable` (the daemon vanished between prepare and open): the next candidate is prepared and then opened, and only its final result completes the open. Implement this as a small `openNextBackend()` continuation. Do not re-enter the state machine.
- `requestWritePassword`, `requestReadPassword` and `requestRemoveEntry` call the backend and complete with `outcome == Ok`, the value, and the error.
- The warning in `startWalletInitialization` says `"session bus not reachable; secrets fall back to the vault file"` instead of naming kwalletd6.
- `setStorageMode` already emits `storageModeChanged`, which is also `backendName`'s notify.

Every backend callback captures `QPointer<SecretsStore>`, or relies on the transport's context guard, where the context is `this`.

`m_walletHandle` stays as the state machine's "open" marker (it is set to `0`). Do not remove it: `FakeSecretsStore` paths depend on the state machine as it is.

- [ ] **Step 6: Green**

```bash
mkdir -p /tmp/w12a/tmp && export TMPDIR=/tmp/w12a/tmp
cmake -S . -B /tmp/w12a -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSTRMQT_WERROR=ON >/dev/null
cmake --build /tmp/w12a --target tst_secret_backends tst_secrets_store
/tmp/w12a/tst_secret_backends | tail -3; /tmp/w12a/tst_secrets_store | tail -3
```

Expected: both show `0 failed`. `tst_secrets_store` is **unchanged** and still passes: that is the proof the state machine is intact.

Then run gate **H** (`NN=12`), expecting green with **75** test executables (`100% tests passed … out of 75`), and **C(ubuntu-24.04)**, expecting `OK (compat)`.

- [ ] **Step 7: Manual check on this machine (Plasma 6)**

Run `/tmp/w12a/strmqt` with the user's existing profile, then:

```bash
journalctl --user -t strmqt --since -2min 2>/dev/null | grep -i secrets || true
```

If the app logs to stderr instead, read its terminal output.

Expected:
- `secrets: keyring candidates KWallet6 …`;
- no vault warning;
- the user is still signed in, because the token was read from KWallet 6's `StrmQt` folder.

Ask the user to confirm that they did not have to sign in again. Record the answer.

- [ ] **Step 8: Commit**

```bash
git add src/platform/secrets src/platform/SecretsStore.h src/platform/SecretsStore.cpp src/CMakeLists.txt \
        tests/mocks/FakeDBusTransport.h tests/unit/tst_secret_backends.cpp tests/CMakeLists.txt
git commit -m "feat(platform): keyring backends behind a D-Bus transport seam, with KWallet 5

SecretsStore's state machine is unchanged; its production transport now
probes the session bus (ListNames, ListActivatableNames) and delegates to
the first reachable backend: KWallet 6, then KWallet 5 (Plasma 5.27:
Kubuntu 24.04, Debian 12 KDE). An activatable KWallet counts only on a
KDE desktop. An unavailable keyring falls through to the next; a refused
one goes to the vault file, as before. No new library: QtDBus only.

tst_secret_backends drives the backends and the store over a fake
transport; tst_secrets_store is untouched and passes. Checked on Plasma
6: the existing KWallet 6 token is still read.

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_013JJsS9enEqUZ9WD9HD99Vw"
```

---

### Task 13: Secret backends, part 2: the freedesktop Secret Service

**Files:**
- Create: `src/platform/secrets/SecretServiceBackend.h`, `src/platform/secrets/SecretServiceBackend.cpp`
- Modify: `src/platform/secrets/SecretBackend.cpp` (`makeSecretBackend` builds it)
- Modify: `src/CMakeLists.txt` (the two new sources)
- Test: `tests/unit/tst_secret_backends.cpp`

**Interfaces:**
- Consumes: Task 12.
- Produces: `SecretServiceBackend(DBusTransport &, QObject *context)`, whose `name()` is `"Secret Service"`, implementing spec §6.3 exactly.

- [ ] **Step 1: Marshalling types**

In `SecretServiceBackend.h`:

```cpp
namespace strmqt::secrets {

// org.freedesktop.Secret.Secret: (oayays) — session, parameters, value, content type.
struct DBusSecret
{
    QDBusObjectPath session;
    QByteArray parameters;
    QByteArray value;
    QString contentType;
};
QDBusArgument &operator<<(QDBusArgument &arg, const DBusSecret &secret);
const QDBusArgument &operator>>(const QDBusArgument &arg, DBusSecret &secret);

using StringMap = QMap<QString, QString>;

// Reads a D-Bus reply argument whether it arrived from the bus (a QDBusArgument)
// or from a test's plain QVariant (FakeDBusTransport builds replies directly).
template<class T>
T fromDBus(const QVariant &v)
{
    if (v.canConvert<QDBusArgument>())
        return qdbus_cast<T>(v.value<QDBusArgument>());
    return v.value<T>();
}

} // namespace strmqt::secrets

Q_DECLARE_METATYPE(strmqt::secrets::DBusSecret)
Q_DECLARE_METATYPE(strmqt::secrets::StringMap)
```

Register both with `qDBusRegisterMetaType` once, in the backend constructor, guarded by a function-local static. `QDBusVariant` values, such as the `Locked` property and the `OpenSession` output, go through `fromDBus<QDBusVariant>(…).variant()` when they arrive wrapped.

- [ ] **Step 2: Write the failing tests**

Add to `tst_secret_backends.cpp`. Each test drives a `SecretServiceBackend` over `FakeDBusTransport` and asserts every message's service, path, interface, member and arguments before answering it.

1. `secretServicePreparesAPlainSessionAndTheDefaultCollection`:
   - `OpenSession("plain", QDBusVariant(""))` goes to `org.freedesktop.secrets`, `/org/freedesktop/secrets`, `org.freedesktop.Secret.Service`. Reply `(QDBusVariant(""), QDBusObjectPath("/org/freedesktop/secrets/session/s1"))`.
   - Then `ReadAlias("default")`. Reply `QDBusObjectPath("/org/freedesktop/secrets/collection/login")` → `Ok`.
2. `secretServiceWithoutADefaultCollectionIsUnavailable`: `ReadAlias` answers `QDBusObjectPath("/")` → `Unavailable`.
3. `secretServiceOpenOfAnUnlockedCollectionNeedsNoPrompt`: `Properties.Get("org.freedesktop.Secret.Collection", "Locked")` on the collection path answers `QDBusVariant(false)` → `Ok`, and no `Unlock` is sent.
4. `secretServiceUnlockRunsThePrompt`:
   - `Locked` is `true`, so `Unlock([collection])` is sent;
   - it answers `([], "/org/freedesktop/secrets/prompt/p1")`;
   - the backend subscribes to `org.freedesktop.Secret.Prompt.Completed` on that path, then calls `Prompt.Prompt("")`, which the fake answers with an empty reply;
   - the fake emits `Completed(false, QDBusVariant(…))` → `Ok`.
5. `secretServiceDismissedPromptIsRefused`: the same, with `Completed(true, …)` → `Refused`.
6. `secretServiceWriteStoresOneItemPerKey`:
   - `CreateItem` goes to the collection path, interface `org.freedesktop.Secret.Collection`;
   - its arguments are:
     - a property map whose `org.freedesktop.Secret.Item.Label` is `"StrmQt access token"`;
     - `org.freedesktop.Secret.Item.Attributes` is `{"xdg:schema": "ca.mikesdev.StrmQt.Secret", "strmqt-key": key}`;
     - a `DBusSecret{session, "", value.toUtf8(), "text/plain; charset=utf8"}`;
     - `replace = true`.
   - Reply `(item path, QDBusObjectPath("/"))` → `Ok`.
   - Assert that the attributes contain the key and **nothing else**: no server URL and no user name.
7. `secretServiceReadFindsTheItemAndGetsItsSecret`:
   - `SearchItems({xdg:schema, strmqt-key})` answers `([item], [])`;
   - `Item.GetSecret(session)` on the item answers a `DBusSecret` with value `"tok"` → `Ok`, value `"tok"`.
8. `secretServiceReadOfAMissingKeyIsAnEmptySuccess`: `SearchItems` answers `([], [])` → `Ok` with an empty value, which is what makes `SecretsStore` consult the vault.
9. `secretServiceReadUnlocksLockedItems`: `SearchItems` answers `([], [item])`, then `Unlock([item])` answers `([item], "/")`, then `GetSecret` → `Ok`.
10. `secretServiceRemoveDeletesEveryMatch`: `SearchItems` answers two items, and `Item.Delete()` is sent to each, each answering `QDBusObjectPath("/")` → `Ok`. With no matches, `Ok` with no `Delete`.
11. `storeReachesTheSecretServiceOnGnome`, over `SecretsStore`:
    - `ListNames` answers `[org.freedesktop.secrets]`, with desktop `GNOME`;
    - the whole flow completes a `writeSecret`;
    - `backendName() == "Secret Service"`.
12. `storeFallsFromKWalletToTheSecretService`: kwalletd6 is owned but answers `ServiceUnknown` → the Secret Service serves.

Build and run `tst_secret_backends`. Expected: the new functions fail, because `makeSecretBackend` returns null for `SecretService`.

- [ ] **Step 3: Implement**

`SecretServiceBackend`:
- keeps `m_session` and `m_collection` (`QDBusObjectPath`) from `prepare`;
- has a private `runPrompt(const QDBusObjectPath &prompt, Callback done)`:
  - `/` → `done(Ok)`;
  - otherwise `connectSignal("", prompt.path(), "org.freedesktop.Secret.Prompt", "Completed", context, …)`, then `call(Prompt.Prompt(""))`;
  - `Completed`'s first argument `true` → `Refused`;
- implements every step of spec §6.3 in its table's order;
- maps errors with `isUnavailableError`, exactly as KWallet does.

The attribute map is built in one helper, `attributesFor(key)`, which the write, read and remove paths all use. Remove the `// Task 13` stub in `makeSecretBackend`.

The log names the backend and the step, never the key's value or the secret.

- [ ] **Step 4: Green and gates**

Expected:
- `tst_secret_backends`: `0 failed`;
- gate **H** (`NN=13`): green;
- **C(debian-12)**: `OK (compat)`. Debian 12's QtDBus 6.4 marshals `DBusSecret` and `StringMap` without complaint in the tests.

- [ ] **Step 5: Manual check against gnome-keyring (user)**

Needs a session with gnome-keyring. If this machine has none, it is deferred to Task 21's checklist; say so in the commit.

Otherwise:
1. Run the Task 13 build in a GNOME session, or with `XDG_CURRENT_DESKTOP=GNOME` and gnome-keyring-daemon running with a `login` collection.
2. Sign in.
3. Run `secret-tool search --all xdg:schema ca.mikesdev.StrmQt.Secret`.

Expected: one item labelled `StrmQt access token`, whose attribute `strmqt-key` is `emby/<64 hex>/accessToken`.

- [ ] **Step 6: Commit**

```bash
git add src/platform/secrets/SecretServiceBackend.h src/platform/secrets/SecretServiceBackend.cpp \
        src/platform/secrets/SecretBackend.cpp src/CMakeLists.txt tests/unit/tst_secret_backends.cpp
git commit -m "feat(platform): store tokens in the freedesktop Secret Service

Third keyring backend, after KWallet 6 and 5: org.freedesktop.secrets
(gnome-keyring, KeePassXC) over QtDBus, with the plain session. One item
per key, attributes xdg:schema=ca.mikesdev.StrmQt.Secret and strmqt-key
(the hashed per-account key), so the server and user never appear in the
keyring. No default collection is 'unavailable' (next backend, then the
vault); a dismissed unlock prompt is 'refused' (the vault, no second
dialog).

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_013JJsS9enEqUZ9WD9HD99Vw"
```

---

### Task 14: The keyring in the UI, the Flatpak and the Arch package

**Files:**
- Modify: `src/app/controllers/SessionController.h` and `.cpp` (`secretBackend` property)
- Modify: `src/ui/Main.qml` (the toast around line 1619)
- Modify: `src/ui/pages/LoginPage.qml` (around lines 368 and 381)
- Modify: `src/ui/pages/SettingsPage.qml` (a "Credentials" row in the Server panel)
- Modify: `packaging/flatpak/ca.mikesdev.StrmQt.yml` (finish-args)
- Modify: `packaging/arch/PKGBUILD` (optdepends)
- Test: `tests/unit/tst_session_controller.cpp`, or the test that already covers `secretStorage`. Find it with `grep -ln secretStorage tests/unit/*.cpp`.

**Interfaces:**
- Consumes: `SecretsStore::backendName`.
- Produces: `Q_PROPERTY(QString secretBackend READ secretBackend NOTIFY secretStorageChanged)` on `SessionController`.

- [ ] **Step 1: Test first**

In the test that covers `secretStorage`, add an assertion that `secretBackend` follows the store:
- `"vault file"` after the vault engages;
- the fake's `"keyring"` in wallet mode;
- a `secretStorageChanged` spy fires once for each.

Run it and expect a compile failure: the property does not exist yet.

- [ ] **Step 2: Implement**

`SessionController::secretBackend()` returns `m_secrets->backendName()`. It reuses `secretStorageChanged` as its notify, which is already connected to the store's `storageModeChanged` (`SessionController.cpp:61`).

- [ ] **Step 3: Strings**

The words change. The logic does not.

- **`Main.qml` toast:** `qsTr("No system keyring (KWallet or Secret Service) accepted your sign-in — it will be stored in a vault file with lower security.")`
- **`LoginPage.qml` banner:** `qsTr("No system keyring (KWallet or Secret Service) is available, so your sign-in will be stored in a vault file with lower security — anyone who can read your home folder could take it.")`
- **`LoginPage.qml` footer:** `qsTr("StrmQt %1 · passwords are never stored · access tokens use your system keyring, or a vault file when none is available")`
- **`SettingsPage.qml`**, in the Server panel after "Signed in as":

```qml
                    SettingsSections.InfoRow {
                        width: parent.width
                        label: qsTr("Credentials")
                        // "KWallet", "KWallet 5", "Secret Service" or "vault file"
                        // (SecretsStore::backendName); empty until first used.
                        value: Session.secretBackend
                        visible: Session.secretBackend.length > 0
                    }
```

Then check that no user-facing KWallet-only claim is left:

```bash
grep -rn "KWallet" src/ui --include=*.qml
```

Expected: only the three new strings, which say "KWallet or Secret Service".

- [ ] **Step 4: Flatpak and PKGBUILD**

In `packaging/flatpak/ca.mikesdev.StrmQt.yml`, replace the KWallet comment and `--talk-name` line with:

```yaml
  # The system keyring is the SecretsStore backend (ARCHITECTURE.md §8). Load-
  # bearing for security: without these names no keyring is reachable inside
  # the sandbox and SecretsStore falls back to the vault file (app data dir,
  # 0600) — the login screen warns while so. Probed in this order: KWallet 6
  # (Plasma 6), KWallet 5 (Plasma 5.27), the freedesktop Secret Service
  # (gnome-keyring, KeePassXC).
  - --talk-name=org.kde.kwalletd6
  - --talk-name=org.kde.kwalletd5
  - --talk-name=org.freedesktop.secrets
```

In `packaging/arch/PKGBUILD` optdepends, after the `kwallet` line, add:

```bash
  'gnome-keyring: persist the Emby access token outside KDE (Secret Service)'
```

- [ ] **Step 5: Gates and a look**

Run gates **H** (`NN=14`) and **HC**, expecting both green.

Ask the user to open Settings → Server in `/tmp/w14a/strmqt`. Expected: `Credentials  KWallet`. Record the answer.

- [ ] **Step 6: Commit**

```bash
git add src/app/controllers/SessionController.h src/app/controllers/SessionController.cpp src/ui/Main.qml \
        src/ui/pages/LoginPage.qml src/ui/pages/SettingsPage.qml packaging/flatpak/ca.mikesdev.StrmQt.yml \
        packaging/arch/PKGBUILD tests/unit/<the session test>
git commit -m "feat(ui): name the keyring in use, and reach every keyring from the Flatpak

The login banner, toast and footer say 'system keyring (KWallet or
Secret Service)' instead of KWallet; Settings > Server shows which one
holds the token. The Flatpak may talk to kwalletd5 and
org.freedesktop.secrets; the Arch package lists gnome-keyring as an
optional alternative to kwallet.

Checked: Settings > Server shows 'Credentials: KWallet' on Plasma 6.

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_013JJsS9enEqUZ9WD9HD99Vw"
```

---
### Task 15: Native `.deb` for Ubuntu 24.04 and 26.04 and Debian 12 and 13

**Files:**
- Create: `packaging/debian/control`, `packaging/debian/rules` (executable), `packaging/debian/changelog`, `packaging/debian/copyright`, `packaging/debian/source/format`
- Create: `scripts/ci/copy-tree.sh`, `scripts/ci/package-deb.sh`, `scripts/ci/install-check.sh` (executable)
- Modify: `scripts/ci/deps.sh` (the Debian family installs Build-Depends from `packaging/debian/control` with `mk-build-deps`)

**Interfaces:**
- Consumes: `local.sh`, `selftest.sh`, `strmqt-qml-tier.txt`, `STRMQT_BUNDLE_SDL3`.
- Produces:
  - `package-deb.sh TARGET OUT_DIR`, which writes `strmqt_0.7.5-1~<suffix>_amd64.deb` to `OUT_DIR`;
  - `install-check.sh PACKAGE_FILE`, which installs the package into the current, clean container and runs the self-test.

Suffixes: `ubuntu-24.04` → `ubuntu24.04`, `ubuntu-26.04` → `ubuntu26.04`, `debian-12` → `deb12`, `debian-13` → `deb13`.

- [ ] **Step 1: `packaging/debian/control`**

```
Source: strmqt
Section: video
Priority: optional
Maintainer: Mike Pengelly <mike@leadrix.io>
Build-Depends: debhelper-compat (= 13),
               cmake (>= 3.25), ninja-build, pkgconf | pkg-config, file,
               qt6-base-dev, qt6-base-dev-tools, qt6-declarative-dev, qt6-declarative-dev-tools,
               qt6-websockets-dev, qt6-svg-dev,
               libmpv-dev, libvlc-dev, libssl-dev,
               libsdl3-dev | libudev-dev,
               qml6-module-qtquick-effects | qml6-module-qt5compat-graphicaleffects,
               qt6-svg-plugins | libqt6svg6,
               qml6-module-qtquick <!nocheck>, qml6-module-qtquick-controls <!nocheck>,
               qml6-module-qtquick-templates <!nocheck>, qml6-module-qtquick-window <!nocheck>,
               qml6-module-qtqml-workerscript <!nocheck>, qml6-module-qtqml <!nocheck>,
               qt6-qpa-plugins <!nocheck>, qt6-wayland <!nocheck>,
               vlc-plugin-base <!nocheck>, vlc-plugin-video-output <!nocheck>,
               ffmpeg <!nocheck>, libgl1-mesa-dri <!nocheck>,
               ca-certificates, git
Standards-Version: 4.7.0
Homepage: https://github.com/mpengellyCA/strmQt-linux
Rules-Requires-Root: no

Package: strmqt
Architecture: amd64
Depends: ${shlibs:Depends}, ${misc:Depends},
         ${strmqt:QtAbi}, ${strmqt:QmlEffects}, ${strmqt:SvgPlugin},
         qml6-module-qtquick, qml6-module-qtquick-controls, qml6-module-qtquick-templates,
         qml6-module-qtquick-window, qml6-module-qtqml-workerscript, qml6-module-qtqml,
         qt6-qpa-plugins, qt6-wayland, vlc-plugin-base, vlc-plugin-video-output,
         hicolor-icon-theme
Recommends: kwalletmanager | gnome-keyring | keepassxc, kscreen
Description: couch-first Emby client for the Linux desktop
 StrmQt is a native Qt 6 / QML client for Emby media servers, built to be
 driven from the sofa: keyboard, gamepad, TV remote, KDE Connect or the
 built-in web remote. Playback uses libmpv, with libvlc as a fallback.
```

**Alternatives are ordered newest-release-first.** `mk-build-deps` resolves to the first alternative that exists, so trixie and 26.04 get SDL3, QtQuick.Effects and the separate SVG plugin package, while noble and bookworm fall to the second.

On noble and bookworm `libudev-dev` is the alternative that gets installed. It is what the bundled SDL3's HIDAPI backend wants; on trixie and 26.04 `libsdl3-dev` pulls it in anyway.

`git` and `ca-certificates` are for FetchContent's HTTPS download on the bundling releases. Use the `Description` wording from `packaging/appstream/ca.mikesdev.StrmQt.metainfo.xml`'s `<summary>` and first paragraph if they differ from the draft above.

- [ ] **Step 2: `packaging/debian/rules`**

```make
#!/usr/bin/make -f
# StrmQt Debian packaging (spec 2026-09-27 §7.2). One binary package, built in
# and for exactly one release's container.

export DEB_BUILD_MAINT_OPTIONS = hardening=+all
include /usr/share/dpkg/architecture.mk
BUILDDIR := obj-$(DEB_HOST_GNU_TYPE)

# No SDL3 package (Ubuntu 24.04, Debian 12): bundle the pinned static 3.4.16.
BUNDLE_SDL3 := $(shell pkg-config --exists sdl3 && echo OFF || echo ON)

%:
	dh $@ --buildsystem=cmake+ninja --builddirectory=$(BUILDDIR)

override_dh_auto_configure:
	dh_auto_configure -- -DSTRMQT_WERROR=OFF -DSTRMQT_BUNDLE_SDL3=$(BUNDLE_SDL3)

override_dh_auto_test:
ifeq (,$(filter nocheck,$(DEB_BUILD_OPTIONS)))
	cd $(BUILDDIR) && QT_QPA_PLATFORM=offscreen ctest --output-on-failure
endif

# Runtime dependencies no ELF NEEDED entry can express (spec §7.2):
#  - QtAbi: the binary imports a Qt_6_PRIVATE_API symbol, so pin the upstream
#    Qt version it was built against (a same-version distro rebuild still fits);
#  - QmlEffects: the QML tier's effect module (strmqt-qml-tier.txt);
#  - SvgPlugin: whichever package ships imageformats/libqsvg.so here. None is
#    a build failure, not a package with blank icons.
override_dh_gencontrol:
	set -e; \
	qml=$$(dpkg -S "$$(find /usr/lib -name 'libQt6Qml.so.6' | head -1)" | cut -d: -f1); \
	qtver=$$(dpkg-query -W -f='$${Version}' "$$qml" | sed -E 's/^[0-9]+://; s/[-+~].*$$//'); \
	next=$$(echo "$$qtver" | awk -F. '{ printf "%d.%d.%d", $$1, $$2, $$3 + 1 }'); \
	tier=$$(cat $(BUILDDIR)/strmqt-qml-tier.txt); \
	case "$$tier" in \
	  full) effects=qml6-module-qtquick-effects ;; \
	  compat) effects=qml6-module-qt5compat-graphicaleffects ;; \
	  *) echo "rules: unknown QML tier '$$tier'" >&2; exit 1 ;; \
	esac; \
	svgso=$$(find /usr/lib -path '*/qt6/plugins/imageformats/libqsvg.so' | head -1); \
	[ -n "$$svgso" ] || { echo "rules: no Qt SVG image plugin installed" >&2; exit 1; }; \
	svg=$$(dpkg -S "$$svgso" | cut -d: -f1); \
	echo "strmqt:QtAbi=$$qml (>= $$qtver), $$qml (<< $$next~)" >> debian/strmqt.substvars; \
	echo "strmqt:QmlEffects=$$effects" >> debian/strmqt.substvars; \
	echo "strmqt:SvgPlugin=$$svg" >> debian/strmqt.substvars
	dh_gencontrol
```

Recipe lines are tab-indented; the heredoc above shows them with a tab.

Check `find /usr/lib -name libQt6Qml.so.6` inside a container first. If it returns a symlink, `dpkg -S` still resolves it: the multiarch path is owned by `libqt6qml6`. The awk increment produces the next patch version, `6.4.3`, and with `~` the dependency is `(<< 6.4.3~)`.

- [ ] **Step 3: `changelog`, `copyright`, `source/format`**

`packaging/debian/changelog`:

```
strmqt (0.7.5-1) UNRELEASED; urgency=medium

  * Distro compatibility release: native packages for Ubuntu 24.04 and 26.04,
    Debian 12 and 13; Qt 6.4.2 or newer; KWallet 5 and Secret Service keyrings.

 -- Mike Pengelly <mike@leadrix.io>  Sun, 27 Sep 2026 12:00:00 +0000
```

`UNRELEASED` becomes `unstable` in Task 22.

`packaging/debian/source/format`: `3.0 (quilt)`.

`packaging/debian/copyright` (DEP-5):

```
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: StrmQt
Source: https://github.com/mpengellyCA/strmQt-linux

Files: *
Copyright: 2026 Mike Pengelly
License: GPL-3.0-or-later

Files: assets/fonts/*
Copyright: The Archivo Project Authors; IBM Corp.; The Public Sans Project Authors
License: OFL-1.1
Comment: Embedded in the executable as Qt resources. Full texts:
 assets/fonts/OFL-Archivo.txt, OFL-IBMPlexMono.txt, OFL-PublicSans.txt.

Comment: SDL3
 The Ubuntu 24.04 and Debian 12 packages statically link SDL3 3.4.16
 (https://libsdl.org), Copyright (C) 1997-2026 Sam Lantinga, under the zlib
 license; its text is installed as /usr/share/doc/strmqt/SDL3-LICENSE.txt.

License: GPL-3.0-or-later
 On Debian systems, the full text is in /usr/share/common-licenses/GPL-3.

License: OFL-1.1
 SIL Open Font License, Version 1.1. See the OFL-*.txt files named above.
```

Take the font copyright holders from the first lines of each `OFL-*.txt`, not from this draft. Lintian may want the SDL3 note as a `Files:` stanza. If it complains, keep the note as a stand-alone paragraph comment or add a `Files: debian/*` placeholder; lintian's own message decides which.

- [ ] **Step 4: `copy-tree.sh`, `package-deb.sh`, `install-check.sh`**

`scripts/ci/copy-tree.sh`:

```bash
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
```

`scripts/ci/package-deb.sh`:

```bash
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
```

`scripts/ci/install-check.sh`:

```bash
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
```

`--no-install-recommends` and `install_weak_deps=False` are deliberate: the package must work with its hard dependencies alone.

Make all three executable.

- [ ] **Step 5: `deps.sh` reads Build-Depends**

The Debian branch of `deps.sh`:
- keeps a short bootstrap list: `devscripts equivs lintian git ca-certificates`;
- then runs `mk-build-deps --install --remove --tool 'apt-get -y --no-install-recommends' <file>/packaging/debian/control` when that file exists;
- falls back to the Task 1 arrays when it does not, so older checkouts still work.

`deps.sh` finds the file relative to itself (`$(dirname "$0")/../../packaging/debian/control`). `local.sh` already copies it into the image context, so the image rebuilds whenever `control` changes.

Delete the Debian arrays only if the fallback is truly unneeded. Since the scripts ship with the tree, it is: **remove `apt_common` and `apt_extra`**, and keep a one-line comment pointing at `packaging/debian/control` as the single list.

- [ ] **Step 6: Build and install-check all four**

For each `T` in `ubuntu-24.04 debian-12 debian-13 ubuntu-26.04`:

```bash
df -h /tmp
scripts/ci/local.sh $T /tmp/w15a-$T -- /src/scripts/ci/package-deb.sh $T /build/out
podman run --rm --security-opt label=disable -v /tmp/w15a-$T/out:/pkgs:ro -v "$PWD:/src:ro" \
    $(scripts/ci/deps.sh --image $T) /src/scripts/ci/install-check.sh /pkgs/strmqt_0.7.5-1~*_amd64.deb
```

The second command uses the **bare** distro image, not the CI image, so nothing from the build environment helps the install.

Expected for each target:
- lintian has no errors;
- the install pulls in the tier's effects module (`qml6-module-qt5compat-graphicaleffects` on 24.04 and 12; `qml6-module-qtquick-effects` on 13 and 26.04) and the SVG plugin package (`libqt6svg6` / `qt6-svg-plugins`);
- `selftest.sh: OK`.

Also print the resolved substvars:

```bash
dpkg-deb -f /tmp/w15a-$T/out/strmqt_*.deb Depends | tr ',' '\n' | grep -E 'qt6qml|effects|svg'
```

Record each target's `Depends` excerpt and lintian warnings in the verifications file.

**Review every lintian warning.** Fix it, or add `packaging/debian/strmqt.lintian-overrides` with a comment per override. A likely one is `no-manual-page` for `strmqt` and `strmqt-cli`: override it with the reason "GUI app; strmqt-cli --help documents itself".

- [ ] **Step 7: Container checks still green**

Run **C(debian-13)**. `deps.sh` now installs from `control`, so the image is rebuilt.

Expected: `OK (full)`. This proves that the move to `mk-build-deps` lost nothing.

- [ ] **Step 8: Commit**

```bash
git add packaging/debian scripts/ci/copy-tree.sh scripts/ci/package-deb.sh scripts/ci/install-check.sh \
        scripts/ci/deps.sh docs/superpowers/plans/2026-09-27-distro-compat-075-verifications.md
git commit -m "build(deb): native packages for Ubuntu 24.04/26.04 and Debian 12/13

packaging/debian is copied to debian/ in a scratch copy of the tree and
built per release (0.7.5-1~ubuntu24.04, ~ubuntu26.04, ~deb12, ~deb13).
debian/rules computes three dependencies no NEEDED entry can express:
the upstream Qt version (the binary uses one private Qt symbol), the QML
tier's effect module, and whichever package ships the SVG image plugin —
failing the build if none does. deps.sh now installs Build-Depends from
the control file, so the list exists once. Each package installs into a
bare container of its release and passes the self-test.

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_013JJsS9enEqUZ9WD9HD99Vw"
```

---

### Task 16: Native `.rpm` for Fedora 43 and 44

**Files:**
- Create: `packaging/rpm/strmqt.spec`, `scripts/ci/package-rpm.sh` (executable)
- Modify: `scripts/ci/deps.sh` (the Fedora branch runs `dnf builddep` from the spec)

**Interfaces:**
- Consumes: `copy-tree.sh`, `install-check.sh`.
- Produces: `package-rpm.sh TARGET OUT_DIR`, which writes `strmqt-0.7.5-1.fc<N>.x86_64.rpm`.

- [ ] **Step 1: `packaging/rpm/strmqt.spec`**

```spec
# StrmQt RPM packaging (spec 2026-09-27 §7.3). Built in, and for, one Fedora
# release's container by scripts/ci/package-rpm.sh; not a Fedora-review spec.
Name:           strmqt
Version:        0.7.5
Release:        1%{?dist}
Summary:        Couch-first Emby client for the Linux desktop
License:        GPL-3.0-or-later AND OFL-1.1
URL:            https://github.com/mpengellyCA/strmQt-linux
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc-c++ cmake ninja-build pkgconf git-core
BuildRequires:  qt6-rpm-macros
BuildRequires:  qt6-qtbase-devel qt6-qtbase-private-devel qt6-qtdeclarative-devel
BuildRequires:  qt6-qtwebsockets-devel qt6-qtsvg-devel
BuildRequires:  mpv-devel vlc-devel SDL3-devel openssl-devel
# %%check runs the full suite and the page self-test:
BuildRequires:  qt6-qtsvg qt6-qtwayland vlc-plugins-base vlc-plugin-ffmpeg ffmpeg-free mesa-dri-drivers

# The binary imports a Qt_6_PRIVATE_API symbol and carries qmlcachegen code
# for exactly this Qt: pin it. Fedora rebases Qt inside a release, so this
# holds the Qt update back until a rebuilt StrmQt exists (README).
Requires:       qt6-qtdeclarative%{?_isa} = %{_qt6_version}
Requires:       qt6-qtsvg%{?_isa} qt6-qtwayland%{?_isa}
Requires:       vlc-plugins-base vlc-plugin-ffmpeg
Recommends:     (kwallet or gnome-keyring or keepassxc)

%description
StrmQt is a native Qt 6 / QML client for Emby media servers, built to be
driven from the sofa: keyboard, gamepad, TV remote, KDE Connect or the
built-in web remote. Playback uses libmpv, with libvlc as a fallback.

%prep
%autosetup -n %{name}-%{version}

%build
%cmake -G Ninja -DSTRMQT_WERROR=OFF
%cmake_build

%install
%cmake_install

%check
export QT_QPA_PLATFORM=offscreen
%ctest

%files
%license assets/fonts/OFL-*.txt
%doc README.md
%{_bindir}/strmqt
%{_bindir}/strmqt-cli
%{_datadir}/applications/ca.mikesdev.StrmQt.desktop
%{_metainfodir}/ca.mikesdev.StrmQt.metainfo.xml
%{_datadir}/icons/hicolor/*/apps/ca.mikesdev.StrmQt.*

%changelog
* Sun Sep 27 2026 Mike Pengelly <mike@leadrix.io> - 0.7.5-1
- Distro compatibility release: native Fedora 43/44 package.
```

`%ctest` and `%cmake` come from `cmake-rpm-macros`, which `cmake` pulls in. Check `rpm --eval '%{_qt6_version}'` inside `fedora-43` before relying on it; `qt6-rpm-macros` defines it. If it expands empty, derive the version in `package-rpm.sh` (`rpm -q --qf '%{VERSION}' qt6-qtdeclarative`) and pass it as `--define "_qt6_version …"`.

- [ ] **Step 2: `scripts/ci/package-rpm.sh`**

```bash
#!/usr/bin/env bash
# scripts/ci/package-rpm.sh TARGET OUT_DIR — build StrmQt's .rpm for a Fedora
# TARGET in the current container, from a clean copy of /src.
set -euo pipefail
target=${1:?usage: package-rpm.sh TARGET OUT_DIR}; out=${2:?usage: package-rpm.sh TARGET OUT_DIR}
case "$target" in fedora-*) ;; *) echo "package-rpm.sh: not a Fedora target: $target" >&2; exit 2 ;; esac
src=$(cd "$(dirname "$0")/../.." && pwd)
top=$(mktemp -d)
version=$(rpmspec -q --qf '%{VERSION}\n' --srpm "$src/packaging/rpm/strmqt.spec")
mkdir -p "$top"/{SOURCES,SPECS}
"$src/scripts/ci/copy-tree.sh" "$src" "$top/strmqt-$version"
tar -C "$top" -czf "$top/SOURCES/strmqt-$version.tar.gz" "strmqt-$version"
cp "$src/packaging/rpm/strmqt.spec" "$top/SPECS/"
rpmbuild --define "_topdir $top" -bb "$top/SPECS/strmqt.spec"
mkdir -p "$out"
find "$top/RPMS" -name 'strmqt-[0-9]*.rpm' -exec cp {} "$out"/ \;
rpmlint "$out"/strmqt-"$version"-*.rpm
ls -l "$out"
```

- [ ] **Step 3: `deps.sh` reads the spec**

The Fedora branch:
- bootstraps `dnf-plugins-core rpm-build rpmlint git-core`;
- then runs `dnf builddep -y --setopt=install_weak_deps=False <…>/packaging/rpm/strmqt.spec`;
- removes `dnf_pkgs`, leaving a one-line comment pointing at the spec.

- [ ] **Step 4: Build and install-check both**

For `T` in `fedora-43 fedora-44`:

```bash
scripts/ci/local.sh $T /tmp/w16a-$T -- /src/scripts/ci/package-rpm.sh $T /build/out
podman run --rm --security-opt label=disable -v /tmp/w16a-$T/out:/pkgs:ro -v "$PWD:/src:ro" \
    $(scripts/ci/deps.sh --image $T) /src/scripts/ci/install-check.sh /pkgs/strmqt-0.7.5-1.fc*.x86_64.rpm
rpm -qp --requires /tmp/w16a-$T/out/strmqt-0.7.5-1.fc*.x86_64.rpm | grep -E 'qtdeclarative|svg'
```

Expected:
- `rpmlint` shows `0 errors`;
- `selftest.sh: OK`;
- the `Requires` include `qt6-qtdeclarative(x86-64) = 6.10.3` on fc43 and fc44's own version.

Record both in the verifications file. rpmlint warnings are reviewed like lintian's. Overrides go in `packaging/rpm/strmqt.rpmlintrc`, passed with `rpmlint -r`, each with a comment.

- [ ] **Step 5: `C(fedora-43)` still green**

Run **C(fedora-43)**, expecting `OK (full)`.

- [ ] **Step 6: Commit**

```bash
git add packaging/rpm scripts/ci/package-rpm.sh scripts/ci/deps.sh \
        docs/superpowers/plans/2026-09-27-distro-compat-075-verifications.md
git commit -m "build(rpm): native packages for Fedora 43 and 44

packaging/rpm/strmqt.spec, built per release by scripts/ci/package-rpm.sh
from a clean copy of the tree. Requires pins qt6-qtdeclarative to the
exact Qt it was built against (private symbol, qmlcachegen); the README
explains what that means when Fedora rebases Qt. deps.sh installs
BuildRequires with dnf builddep, so the list exists once. Each package
installs into a bare container of its release and passes the self-test.

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_013JJsS9enEqUZ9WD9HD99Vw"
```

---

### Task 17: The AppImage on Ubuntu 24.04 (glibc 2.39 floor)

**Files:**
- Modify: `scripts/ci/deps.sh` (a new `appimage` target)
- Modify: `packaging/appimage/build-appimage.sh`:
  - build-root and CMake-args overrides;
  - multiarch paths;
  - plugin and QML directories derived from `qt.conf`;
  - a glibc check;
  - the banner.
- Create: `scripts/ci/appimage-host.sh` (executable)
- Modify: `packaging/appimage/README.md`

**Interfaces:**
- Consumes: `STRMQT_BUNDLE_SDL3`, `selftest.sh`.
- Produces:
  - `deps.sh appimage`: ubuntu:24.04 plus aqt Qt 6.11.3 at `/opt/qt/6.11.3/gcc_64`, and appimagetool 1.9.1 at `/usr/local/bin/appimagetool`;
  - `build-appimage.sh`, honouring `STRMQT_APPIMAGE_BUILD_ROOT` (default: `${SRC_DIR}/build`, as today) and `STRMQT_APPIMAGE_CMAKE_ARGS`;
  - `appimage-host.sh TARGET`: installs a "host must provide" set in a bare container.

- [ ] **Step 1: `deps.sh appimage`**

Add `appimage) image=docker.io/library/ubuntu:24.04 ;;` and `cmake_args="-DSTRMQT_BUNDLE_SDL3=ON -DCMAKE_PREFIX_PATH=/opt/qt/6.11.3/gcc_64"` for it, then an install branch:

```bash
    appimage)
        export DEBIAN_FRONTEND=noninteractive
        apt-get update
        # Build tools, libmpv 0.37 + its codec closure (walked by build-appimage.sh),
        # libvlc, SDL3's udev, OpenGL/xkb headers aqt's Qt links against, and Python for aqt.
        apt-get install -y --no-install-recommends \
            build-essential cmake ninja-build pkg-config git ca-certificates file patchelf \
            python3-venv libmpv-dev libvlc-dev libssl-dev libudev-dev libgl-dev libegl-dev \
            libxkbcommon-dev libfontconfig-dev libfreetype-dev libdbus-1-dev \
            vlc-plugin-base ffmpeg libgl1-mesa-dri curl binutils
        python3 -m venv /opt/aqt
        /opt/aqt/bin/pip install --no-cache-dir aqtinstall==3.3.0
        /opt/aqt/bin/aqt install-qt linux desktop 6.11.3 linux_gcc_64 -m qtwebsockets -O /opt/qt
        curl -fsSL -o /usr/local/bin/appimagetool \
            https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage
        echo "ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0  /usr/local/bin/appimagetool" | sha256sum -c -
        chmod +x /usr/local/bin/appimagetool
        ;;
```

Qt 6.11's Linux arch name in aqt is `linux_gcc_64`, and the install directory is `gcc_64`. Confirm both with `/opt/aqt/bin/aqt list-qt linux desktop --arch 6.11.3` on the first run. If the name differs, fix it here and in the spec's §7.4 pin, in the same commit.

QtSvg, QtWayland (the plugin) and Qt5Compat are not needed for the full tier. Confirm with the Step 4 self-test: `Unsupported image format` means `-m qtimageformats` or the SVG module is missing. QtSvg is part of aqt's base `qtbase` download for 6.11, so this is a check, not an expected edit.

- [ ] **Step 2: `build-appimage.sh`**

These changes are confined to the places the script assumed Arch:

1. `BUILD_ROOT="${STRMQT_APPIMAGE_BUILD_ROOT:-${SRC_DIR}/build}"`. Then `BUILD_DIR="${BUILD_ROOT}/appimage"` and `DIST_DIR="${BUILD_ROOT}/dist"`. The `cmake -S … -B …` line appends `${STRMQT_APPIMAGE_CMAKE_ARGS:-}`, unquoted, so it can split into several flags.
2. **The mpv root and the BFS fallback** (lines ~145–146 and ~182):
   - try `$(pkg-config --variable=libdir mpv)` first, as today;
   - the absolute-NEEDED fallback looks in `/usr/lib`, `/usr/lib/x86_64-linux-gnu` and `/lib/x86_64-linux-gnu`, in that order.
3. **The plugin and QML directories:** read them from the `qt.conf` that `Deploy.cmake` writes into the AppDir (`Plugins=` and `QmlImports=` / `Qml2Imports=`), relative to `usr/bin`, instead of `PLUGINS_DIR=usr/lib/qt6/plugins`. Fail with a clear message if `qt.conf` or a key is missing.
4. **The glibc floor**, after the bundle is assembled and before `appimagetool`:

```bash
# glibc floor (spec 2026-09-27 §7.4): nothing bundled may need newer than 2.39.
newest=$(find "${APPDIR}" -type f \( -name '*.so*' -o -perm -u+x \) -print0 |
    xargs -0 objdump -T 2>/dev/null | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail -1)
log "  newest glibc symbol needed: ${newest}"
if [[ "$(printf '%s\n' "${newest#GLIBC_}" 2.39 | sort -V | tail -1)" != "2.39" ]]; then
    die "a bundled object needs ${newest}, above the 2.39 floor"
fi
```

   Use the script's existing `log`/`die` helpers, or whatever it names them.
5. The banner at the top says it builds on Ubuntu 24.04 with aqt Qt for a glibc 2.39 floor (Arch still works when `pkg-config` and a Qt ≥ 6.10 are present). The pacman hints in the tool check gain apt equivalents.

The `FORBIDDEN` list and the positive assertions stay as they are. `libSDL` stays forbidden: the static SDL3 means no `libSDL3.so` should appear, which the assertion now *proves*.

- [ ] **Step 3: `scripts/ci/appimage-host.sh`**

```bash
#!/usr/bin/env bash
# scripts/ci/appimage-host.sh TARGET — make a bare container into the minimal
# host packaging/appimage/README.md says the AppImage needs, so the release
# tests that table instead of trusting it (spec 2026-09-27 §7.4).
set -euo pipefail
case "${1:?usage: appimage-host.sh TARGET}" in
    debian-13)
        apt-get update
        apt-get install -y --no-install-recommends <exactly the README table's Debian names> ;;
    fedora-43)
        dnf install -y --setopt=install_weak_deps=False <exactly the README table's Fedora names> ;;
    *) echo "appimage-host.sh: no host list for $1" >&2; exit 2 ;;
esac
```

Fill the two lists from `packaging/appimage/README.md`'s "host must provide" table: glibc, libstdc++, libGL/EGL (mesa), fontconfig, freetype, libxkbcommon, the X11/Wayland client libraries, and D-Bus. Look up the per-distro package names in each container with `apt-file` or `dnf provides` for each soname in the table. **If the README's table turns out wrong, fix the table.** That is the point of this step.

- [ ] **Step 4: Build and test the AppImage**

```bash
df -h /tmp
scripts/ci/local.sh appimage /tmp/w17a-appimage -- bash -c '
  rm -rf /build/src-copy && /src/scripts/ci/copy-tree.sh /src /build/src-copy
  cd /build/src-copy &&
  STRMQT_APPIMAGE_BUILD_ROOT=/build/out STRMQT_APPIMAGE_CMAKE_ARGS="$(/src/scripts/ci/deps.sh --cmake-args appimage)" \
  APPIMAGE_EXTRACT_AND_RUN=1 packaging/appimage/build-appimage.sh 2>&1 | tail -25'
```

The copy is there because `build-appimage.sh` writes next to its source, and `/src` is read-only.

Expected:
- `newest glibc symbol needed: GLIBC_2.3x` (≤ 2.39);
- the forbidden and positive assertions pass;
- `/tmp/w17a-appimage/out/dist/StrmQt-0.7.5-x86_64.AppImage` exists. Until Task 22 bumps the version it is named `-0.7.0-`, which is fine.

Then check it on the bare hosts:

```bash
for T in debian-13 fedora-43; do
  podman run --rm --security-opt label=disable -v /tmp/w17a-appimage/out/dist:/dist:ro -v "$PWD:/src:ro" \
    -e APPIMAGE_EXTRACT_AND_RUN=1 $(scripts/ci/deps.sh --image $T) bash -c \
    "/src/scripts/ci/appimage-host.sh $T >/dev/null && cp /dist/StrmQt-*.AppImage /tmp/a.AppImage && /src/scripts/ci/selftest.sh /tmp/a.AppImage"
done
```

Expected: `selftest.sh: OK` twice, and the log shows `SDL3 gamepad support active`. `/dist` is read-only and extract-and-run writes next to the file, hence the copy.

Record the glibc line and both results in the verifications file.

- [ ] **Step 5: `packaging/appimage/README.md`**

Update:
- the build base (ubuntu:24.04, aqt Qt 6.11.3, libmpv 0.37);
- the floor: glibc 2.39, so it runs on Ubuntu 24.04+, Debian 13+ and Fedora 40+. Debian 12 (glibc 2.36) is **not** covered: its users take the `.deb` or the Flatpak;
- the host table, as corrected in Step 3;
- the local build command from Step 4.

- [ ] **Step 6: Commit**

```bash
git add scripts/ci/deps.sh scripts/ci/appimage-host.sh packaging/appimage/build-appimage.sh \
        packaging/appimage/README.md docs/superpowers/plans/2026-09-27-distro-compat-075-verifications.md
git commit -m "build(appimage): build on Ubuntu 24.04 with aqt Qt 6.11.3 for a glibc 2.39 floor

The AppImage was built on Arch and so needed a current glibc. It now
builds in ubuntu:24.04 with Qt 6.11.3 from aqtinstall 3.3.0 (Deploy.cmake
needs Qt >= 6.10), Ubuntu's libmpv 0.37 closure, and the bundled static
SDL3. The build fails if any bundled object needs a glibc symbol newer
than 2.39. Verified by the self-test in bare debian:trixie and fedora:43
containers with only the README's host table installed.

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_013JJsS9enEqUZ9WD9HD99Vw"
```

---

### Task 18: `packages.yml`, and `release.yml` ships the packages

**Files:**
- Create: `.github/workflows/packages.yml`
- Modify: `.github/workflows/release.yml`:
  - the `appimage` job is replaced by a `packages` call;
  - version consistency covers the new version files;
  - the publish glob covers `.deb` and `.rpm`.

**Interfaces:**
- Consumes: `package-deb.sh`, `package-rpm.sh`, `install-check.sh`, `deps.sh appimage`, `appimage-host.sh`.
- Produces: a reusable workflow with `workflow_call` (input `ref`) and `workflow_dispatch`, whose artifacts are named `deb-<target>`, `rpm-<target>` and `appimage`.

- [ ] **Step 1: `packages.yml`**

```yaml
name: Packages

# Builds every native package and the AppImage, then installs each into a
# fresh container of its release and runs the self-test (spec 2026-09-27 §8.3).
# release.yml calls it for a tag; workflow_dispatch runs it on main before
# tagging (spec §8.4, step 3).
on:
  workflow_call:
    inputs:
      ref:
        description: 'Commit to build'
        type: string
        required: true
  workflow_dispatch:

permissions:
  contents: read

jobs:
  deb:
    name: .deb (${{ matrix.target }})
    runs-on: ubuntu-latest
    strategy:
      fail-fast: false
      matrix:
        include:
          - { target: ubuntu-24.04, image: 'ubuntu:24.04' }
          - { target: ubuntu-26.04, image: 'ubuntu:26.04' }
          - { target: debian-12,    image: 'debian:bookworm' }
          - { target: debian-13,    image: 'debian:trixie' }
    container: ${{ matrix.image }}
    steps:
      - uses: actions/checkout@v4
        with: { ref: '${{ inputs.ref || github.sha }}' }
      - run: scripts/ci/deps.sh ${{ matrix.target }}
      - run: scripts/ci/package-deb.sh ${{ matrix.target }} "$RUNNER_TEMP/out"
      - uses: actions/upload-artifact@v4
        with: { name: 'deb-${{ matrix.target }}', path: '${{ runner.temp }}/out/*.deb' }

  deb-install:
    name: .deb install check (${{ matrix.target }})
    needs: deb
    runs-on: ubuntu-latest
    strategy:
      fail-fast: false
      matrix:
        include: <the same four entries>
    container: ${{ matrix.image }}
    steps:
      - uses: actions/checkout@v4
        with: { ref: '${{ inputs.ref || github.sha }}', sparse-checkout: scripts/ci }
      - uses: actions/download-artifact@v4
        with: { name: 'deb-${{ matrix.target }}', path: pkgs }
      - run: scripts/ci/install-check.sh pkgs/*.deb

  rpm:        # as deb, targets fedora-43 / fedora-44, images registry.fedoraproject.org/fedora:43 / :44,
              # package-rpm.sh, artifact rpm-<target> (*.rpm)
  rpm-install: # as deb-install, for rpm

  appimage:
    name: AppImage
    runs-on: ubuntu-latest
    container: ubuntu:24.04
    steps:
      - uses: actions/checkout@v4
        with: { ref: '${{ inputs.ref || github.sha }}' }
      - run: scripts/ci/deps.sh appimage
      - name: Build
        env:
          APPIMAGE_EXTRACT_AND_RUN: '1'
        run: |
          STRMQT_APPIMAGE_BUILD_ROOT="$RUNNER_TEMP/out" \
          STRMQT_APPIMAGE_CMAKE_ARGS="$(scripts/ci/deps.sh --cmake-args appimage)" \
            packaging/appimage/build-appimage.sh
      - uses: actions/upload-artifact@v4
        with: { name: appimage, path: '${{ runner.temp }}/out/dist/*.AppImage' }

  appimage-hosts:
    name: AppImage on ${{ matrix.target }}
    needs: appimage
    runs-on: ubuntu-latest
    strategy:
      fail-fast: false
      matrix:
        include:
          - { target: debian-13, image: 'debian:trixie' }
          - { target: fedora-43, image: 'registry.fedoraproject.org/fedora:43' }
    container: ${{ matrix.image }}
    steps:
      - uses: actions/checkout@v4
        with: { ref: '${{ inputs.ref || github.sha }}', sparse-checkout: scripts/ci }
      - uses: actions/download-artifact@v4
        with: { name: appimage, path: dist }
      - run: scripts/ci/appimage-host.sh ${{ matrix.target }}
      - run: chmod +x dist/*.AppImage && APPIMAGE_EXTRACT_AND_RUN=1 scripts/ci/selftest.sh dist/*.AppImage
```

Write out the `<…>` and `#` shorthand entries in full: the file must be complete YAML. Use the checkout and artifact action versions the repo already uses (`v4`).

The `sparse-checkout` of `scripts/ci` needs git in the container. If the bare images lack it, drop `sparse-checkout`: `actions/checkout` then falls back to downloading the whole tree through the REST API, which is also fine.

- [ ] **Step 2: `release.yml`**

1. Delete the `appimage` job. Add:

```yaml
  packages:
    name: Packages
    needs: resolve
    uses: ./.github/workflows/packages.yml
    with:
      ref: ${{ needs.resolve.outputs.sha }}
```

2. `release.needs` becomes `[resolve, arch, packages, flatpak]`.
3. In the Arch job's **Version consistency** step, next to the PKGBUILD `_tag` and `project(VERSION)` checks, add:
   - `packaging/debian/changelog`'s first-line version, compared with `"${VERSION}-1"`, using `sed -n '1s/^strmqt (\([^)]*\)).*/\1/p'`;
   - the changelog's distribution must not be `UNRELEASED`;
   - `packaging/rpm/strmqt.spec`'s `Version:`, compared with `${VERSION}`.

   Use the step's existing variable names and failure style.
4. The publish `find` adds `-o -name '*.deb' -o -name '*.rpm'`.

The Flatpak job and the pre-release logic are unchanged. The publish step still creates a **draft**; Task 22 publishes it.

- [ ] **Step 3: Lint**

```bash
podman run --rm --security-opt label=disable -v "$PWD:/repo:ro" -w /repo docker.io/rhysd/actionlint:1.7.12 -color=never
```

Expected: no output.

- [ ] **Step 4: Rehearse**

Every job runs a script that Tasks 15–17 already ran locally in the same images, so a local rehearsal re-runs those. **Skip it unless a script changed in this task.** The first real run is Task 22's `gh workflow run packages.yml --ref main`, before any tag exists.

Simulate the version-consistency step's commands on the current tree:
- the changelog is still `UNRELEASED` and the version is 0.7.0;
- so they **should fail** now, on the version mismatch and the `UNRELEASED` check;
- paste the failure into the commit message as proof that the check bites.

- [ ] **Step 5: Commit**

```bash
git add .github/workflows/packages.yml .github/workflows/release.yml
git commit -m "ci: build, install-check and release the native packages and the AppImage

packages.yml (reusable, and dispatchable on main before tagging) builds
four .debs, two .rpms and the AppImage, installs each package into a
fresh container of its release and runs the self-test there, and runs
the AppImage's self-test on bare Debian 13 and Fedora 43. release.yml
calls it instead of its own Arch-based AppImage job, checks the Debian
changelog and RPM spec versions against the tag, and attaches .deb and
.rpm files to the release.

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_013JJsS9enEqUZ9WD9HD99Vw"
```

---
### Task 19: ARCHITECTURE.md describes what was built

**Files:**
- Modify: `ARCHITECTURE.md`:
  - a new subsection "Supported Qt versions and compatibility shims" in §1 (Shape of the program), after the layer description;
  - §4, one pointer line;
  - §7, §8, §9 and §10.

**Interfaces:** none. This is documentation of Tasks 3–18. Every claim must be checkable against the tree.

- [ ] **Step 1: The new subsection**

It goes at the end of §1 and should run to about 40–60 lines, in the file's voice: what the design is, then why. It covers:
- **The two tiers.** Qt 6.8+ is full and 6.4–6.7 is compat. Say why the switch is single (no distro ships 6.5–6.7), and what compat loses, as a four-row condensation of spec §3's table.
- **How a tier is chosen:**
  - `STRMQT_QML_TIER` and `cmake/StrmQtQmlTier.cmake`;
  - `src/ui/shims/{full,compat}`: the type name is the basename, and one directory joins the module;
  - the rule that no QML outside `src/ui/shims` imports an effects module;
  - `strmqt-qml-tier.txt` and who reads it (`debian/rules`);
  - the startup log line.
- **The C++ guards:**
  - `main.cpp`'s `loadFromModule` fallback and the pinned `RESOURCE_PREFIX`;
  - `InputMap`'s signature-shaped `invokeAction` call, and why it is shaped rather than `Q_ARG`'d;
  - `MusicRepository`'s ready future.

  Name the files.
- **Build floor:** CMake 3.25 (Debian 12) and why `EXCLUDE_FROM_ALL` is not used in FetchContent.
- **The private-API coupling** (`QQmlPrivate::compositeMetaType`), which is why each package is built per distro release and pins its Qt.
- **Where the 6.4 behaviour risks were checked:** the containers, the `floor` CI job, and Task 7's record, cited by its path in `docs/superpowers/plans/…-verifications.md`.

- [ ] **Step 2: The other sections**

- **§4 (User interface):** one sentence where effects or `StrmIcon` are described, pointing at the new subsection. Replace any remaining "MultiEffect" claim with "the StrmTint shim".
- **§7 (Testing):**
  - the suite count is **75**;
  - `scripts/ci` and the container checks (`local.sh … check.sh`): which distros, and that `ci.yml` runs `floor` and `debian-13`;
  - `packages.yml`'s install checks;
  - `tst_secret_backends` and its fake transport;
  - the navigation-history staging lesson, in one sentence: stage a QML module once per run.
- **§8 (Configuration and secrets):**
  - the three backends;
  - the probe order and the KDE rule for activatable KWallet;
  - unavailable versus refused;
  - the Secret Service attributes (only the hashed key);
  - why KWallet is first (existing tokens);
  - the `plain` session and its follow-up.
- **§9 (Packaging):**
  - `.deb` and `.rpm`, with where they live and how they are built;
  - the three computed Debian dependencies;
  - the Fedora exact-Qt pin and its consequence;
  - the AppImage's new base and its glibc 2.39 check;
  - SDL3 bundling, and who turns it on.
- **§10 (Known limitations):**
  - the AppImage floor line becomes glibc 2.39 (Debian 12 not covered);
  - compat-tier visuals;
  - the Fedora Qt rebase;
  - the `plain` Secret Service session;
  - GNOME idle inhibition if Task 21 finds it lacking.

  Remove every limitation that this release fixed.

- [ ] **Step 3: Check the claims**

For each file path or symbol the new text names, run:

```bash
grep -o '`[^`]*`' ARCHITECTURE.md | sort -u | grep -E '\.(cpp|h|qml|cmake|sh|yml|spec)`|/' | tr -d '`' | while read -r p; do [ -e "$p" ] || echo "missing: $p"; done
```

Expected: no `missing:` lines for paths added in this task. Pre-existing lines are not this task's business.

- [ ] **Step 4: Commit**

```bash
git add ARCHITECTURE.md
git commit -m "docs: supported Qt versions, keyring backends and native packages in ARCHITECTURE

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_013JJsS9enEqUZ9WD9HD99Vw"
```

---

### Task 20: README: install per distro, and what degrades

**Files:**
- Modify: `README.md`:
  - "Your credentials stay yours";
  - "Install";
  - "Where it stands";
  - "Building it yourself".

**Interfaces:** none. The README's supported table must name exactly the artifacts `packages.yml` builds.

- [ ] **Step 1: "Your credentials stay yours"**

Tokens go to the system keyring: KWallet on Plasma 6 or 5, and the Secret Service (GNOME Keyring, KeePassXC) elsewhere. The rest of the paragraph stays.

- [ ] **Step 2: "Install"**

Rewrite the section:

1. **The supported-distro table** (spec §2), with columns distro, format, and command. Use `apt install ./strmqt_0.7.5-1~ubuntu24.04_amd64.deb` and `sudo dnf install ./strmqt-0.7.5-1.fc43.x86_64.rpm`: a local-path install, so the package manager resolves dependencies. Rows:
   - Ubuntu 24.04 LTS, Ubuntu 26.04;
   - Debian 12, Debian 13;
   - Fedora 43, Fedora 44;
   - Arch (`pacman -U`);
   - the Flatpak (everyone);
   - the AppImage (glibc ≥ 2.39: Ubuntu 24.04+, Debian 13+, Fedora 40+ — not Debian 12).
2. **"On Qt older than 6.8"**: spec §3's degradation table, in user terms:
   - icons, shadows and masks are drawn by Qt's older effects module and look the same or nearly;
   - the backdrop blur has a slightly different character;
   - crate headings are narrower;
   - it applies to Ubuntu 24.04 and Debian 12;
   - nothing is missing.
3. **Fedora notes:**
   - the `.rpm` is tied to Fedora's exact Qt version. When Fedora updates Qt within a release, dnf holds the update back or asks to remove StrmQt until the next StrmQt release ships a matching `.rpm`. The Flatpak does not have this problem.
   - HEVC and AAC need RPM Fusion's `ffmpeg` (replacing `ffmpeg-free`). Link RPM Fusion's configuration page.
4. **The AppImage paragraph:**
   - the glibc story is now the good news: built on Ubuntu 24.04, floor 2.39, verified on Debian 13 and Fedora 43;
   - drop the `GLIBC_2.44` error block and the "lowering that floor" paragraph, which are no longer true;
   - keep one sentence on why an AppImage always has a glibc floor.
5. **Target platform:** Plasma 6 on Wayland remains the primary target. Plasma 5.27 and GNOME are supported through the keyring backends, and a gamepad works on every listed distro.

- [ ] **Step 3: "Where it stands" and "Building it yourself"**

"Where it stands":
- Version **0.7.5**, a pre-release: the distro compatibility release, in one short paragraph (Qt 6.4+, native packages, keyrings, the two skip fixes);
- the test count is `ctest passes 75/75`, plus "on Qt 6.4.2, 6.8.2, 6.10 and 6.11 in CI containers";
- remove bullets that no longer hold; keep the rest.

"Building it yourself":
- Qt **6.4+** (6.8+ for the full visual tier), CMake **3.25+**;
- `STRMQT_QML_TIER` (auto/full/compat) and `STRMQT_BUNDLE_SDL3` (for distros without SDL3), one line each;
- Qt5Compat.GraphicalEffects needed at runtime on the compat tier;
- **the container check:**

```bash
scripts/ci/local.sh ubuntu-24.04 /tmp/wX -- /src/scripts/ci/check.sh /build $(scripts/ci/deps.sh --cmake-args ubuntu-24.04)
```

- [ ] **Step 4: Check the table against CI**

```bash
grep -oE 'strmqt_0\.7\.5-1~[a-z0-9.]+_amd64\.deb|strmqt-0\.7\.5-1\.fc[0-9]+\.x86_64\.rpm' README.md | sort -u
```

Expected: exactly the six artifact names:
- `~ubuntu24.04`, `~ubuntu26.04`, `~deb12`, `~deb13`;
- `fc43`, `fc44`.

- [ ] **Step 5: Commit**

```bash
git add README.md
git commit -m "docs: install per distro, supported table, and what degrades before Qt 6.8

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_013JJsS9enEqUZ9WD9HD99Vw"
```

---

### Task 21: Final gate

**Files:**
- Modify: the verifications file (the final table and the manual checklist)
- Modify: any file needed to fix a regression this gate finds, as its own `fix(…)` commit

**Interfaces:** none. This task proves spec §12 items 1–7 and 10 on the branch. Items 8 and 9 are Task 22.

- [ ] **Step 1: Space, then every automated check**

```bash
df -h /tmp          # need ~9 GB free; ask the orchestrator to clear /tmp/w* first if not
```

Run, in this order, **one at a time**, recording each result line:

1. Gate **H** (`NN=21`) and **HC**.
2. **C(…)** for `ubuntu-24.04`, `debian-12`, `debian-13`, `ubuntu-26.04`, `fedora-43` and `fedora-44`.
3. Task 15 Step 6 for all four Debian targets.
4. Task 16 Step 4 for both Fedora targets.
5. Task 17 Step 4, the AppImage and its two hosts.
6. actionlint (Task 9 Step 2).

Between container groups, keep an eye on space: `du -sh /tmp/w21a-* | sort -h`.

**Do not delete build directories yourself.** If space runs short, stop and ask the orchestrator to delete finished ones.

- [ ] **Step 2: Hygiene greps**

```bash
grep -rn "QtQuick.Effects\|Qt5Compat" src/ui --include=*.qml | grep -v '^src/ui/shims/'     # expect nothing
grep -rn "font.features\|variableAxes" src/ui --include=*.qml | grep -v '^src/ui/shims/'    # expect nothing
grep -rn "kwalletd6" src --include=*.cpp | grep -v 'secrets/'                                # expect nothing
grep -rnE "ignoreSslErrors|QSslSocket::VerifyNone" src | grep -v '^src/.*//'                   # expect no new hit vs main
git diff main --stat | tail -1
git status --short                                                                            # only the user's two untracked files
```

For the TLS line, compare against `main`: run `git grep -nE "ignoreSslErrors|VerifyNone" main -- src`. Any hit that exists on the branch and not on `main` is a stop.

- [ ] **Step 3: Manual checklist (user)**

Ask the user for each item, and record the answer and date. An item they cannot do now is recorded as **deferred**, with the reason. The release notes in Task 22 mention any deferral.

1. **Plasma 6, KWallet 6:** the existing token survives the upgrade. `/tmp/w21a/strmqt` starts signed in, and Settings → Server shows `Credentials: KWallet`.
2. **GNOME keyring:** in a GNOME session (VM or container desktop), sign-in stores the token. `secret-tool search --all xdg:schema ca.mikesdev.StrmQt.Secret` finds one item, and there is no vault warning.
3. **Compat visuals:** side by side, `/tmp/w21a/strmqt` and `/tmp/w21b/strmqt` (HC) match the spec §3 table.
4. **An Ubuntu 24.04 VM** with the `~ubuntu24.04` `.deb` installed: sign in, browse, play a video and a track, use the keyboard and (if available) a gamepad. Focus behaves as it does on Arch.
5. **GNOME idle inhibition:** during playback on GNOME the screen does not blank (`gnome-session-inhibit --list` shows StrmQt). If it does not work, add a README and ARCHITECTURE §10 limitation line as a `docs:` commit. It is not a blocker (spec §11 risk 7).
6. **Web remote:** in a chaptered film, the phone's Next steps a chapter. **MPRIS:** `playerctl -p strmqt previous` five seconds into a chapterless video restarts it, and KDE's media applet shows Previous enabled.

- [ ] **Step 4: Record and commit**

Append a `## Task 21: final gate` table to the verifications file. It has one row per check in Steps 1–3, with the command, the result line and the date. Then:

```bash
git add docs/superpowers/plans/2026-09-27-distro-compat-075-verifications.md
git commit -m "docs(compat): record the 0.7.5 final gate

<one line per group: host, containers, packages, AppImage, manual>

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_013JJsS9enEqUZ9WD9HD99Vw"
```

---

### Task 22: Release 0.7.5

**Files:**
- Modify: `CMakeLists.txt` (`project(StrmQt VERSION 0.7.5 …)`)
- Modify: `packaging/arch/PKGBUILD` (`_tag=v0.7.5`, `pkgrel=1`)
- Modify: `packaging/appstream/ca.mikesdev.StrmQt.metainfo.xml` (a new `<release version="0.7.5" date="…" type="development">` entry above 0.7.0's, in the same style)
- Modify: `packaging/debian/changelog` (`UNRELEASED` → `unstable`, with the release date)
- Modify: `packaging/rpm/strmqt.spec` (`Version: 0.7.5` is already set; check the `%changelog` date)

**Interfaces:** none.

**This task pushes and tags.** AGENTS.md: never push unless the user explicitly asks. Steps 3 onwards run only after the user says, in this session, to release.

- [ ] **Step 1: Bump**

Edit the five files.

The AppStream entry describes 0.7.5 in two to four `<li>`s. Write them for users:
- runs on Ubuntu 24.04+, Debian 12+ and Fedora 43+;
- native packages;
- sign-in kept in GNOME Keyring or KWallet 5;
- web remote Next/Previous step chapters, and MPRIS Previous is enabled for video.

Then check that the files agree:

```bash
grep -n "project(StrmQt VERSION" CMakeLists.txt
grep -n "^_tag=" packaging/arch/PKGBUILD
head -1 packaging/debian/changelog
grep -n "^Version:" packaging/rpm/strmqt.spec
grep -n 'release version="0.7.5"' packaging/appstream/ca.mikesdev.StrmQt.metainfo.xml
appstreamcli validate --no-net packaging/appstream/ca.mikesdev.StrmQt.metainfo.xml || true
```

Expected:
- `0.7.5`, `v0.7.5`, `strmqt (0.7.5-1) unstable; …`, `Version: 0.7.5`, and the release line;
- `appstreamcli` either absent on this host or reporting no errors. If it is absent, run it in the `debian-13` container with `apt-get install -y appstream`.

Run gate **H** (`NN=22`). Expected: green, with the startup log showing `0.7.5`.

- [ ] **Step 2: Commit**

```bash
git add CMakeLists.txt packaging/arch/PKGBUILD packaging/appstream/ca.mikesdev.StrmQt.metainfo.xml \
        packaging/debian/changelog packaging/rpm/strmqt.spec
git commit -m "chore(release): 0.7.5

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_013JJsS9enEqUZ9WD9HD99Vw"
```

**STOP.** Report to the user:
- the branch head;
- the verifications file's final table;
- any deferred manual item.

Ask: "Release 0.7.5 — fast-forward main to compat-075, push, wait for CI, then tag?" Continue only on an explicit yes.

- [ ] **Step 3: `main`, pushed, and green before any tag** (spec §8.4; the lesson of v0.7.0)

```bash
git checkout main
git merge --ff-only compat-075
git push origin main
gh run list --workflow ci.yml --branch main --limit 1
gh run watch <that run id> --exit-status
```

Expected: `ci.yml` completes green, with the Arch, `floor` and `debian-13` jobs all succeeding.

If `--ff-only` fails, `main` has moved. Stop and ask the user; do not merge or rebase on your own.

```bash
gh workflow run packages.yml --ref main
sleep 5; gh run list --workflow packages.yml --branch main --limit 1
gh run watch <that run id> --exit-status
```

Expected: every `deb`, `deb-install`, `rpm`, `rpm-install`, `appimage` and `appimage-hosts` job is green.

**A red job stops the release.** Fix it on a branch as its own commit, then fast-forward and push again with the user's say-so. **No tag exists until both workflows are green on the same `main` commit.**

- [ ] **Step 4: Tag**

```bash
git tag -a v0.7.5 -m "StrmQt 0.7.5"
git push origin v0.7.5
gh run list --workflow release.yml --limit 1
gh run watch <that run id> --exit-status
gh release view v0.7.5 --json isDraft,assets --jq '.isDraft, (.assets[].name)'
```

Expected: `true`, followed by exactly these artifacts:
- 4 `.deb`s and 2 `.rpm`s;
- `strmqt-0.7.5-1-x86_64.pkg.tar.zst`;
- `StrmQt-0.7.5-x86_64.AppImage`;
- `ca.mikesdev.StrmQt.flatpak`;
- `release-provenance.txt`.

That is spec §12 item 9.

- [ ] **Step 5: Publish as a pre-release with notes**

Write the notes to `$TMPDIR/notes-0.7.5.md`, not into the repo. Cover:
- **Install**: which file for which distro, the README table in short;
- **What's new**: Qt 6.4+, the keyrings, the two skip fixes;
- **On Ubuntu 24.04 and Debian 12**: the compat-tier visuals;
- **Fedora**: the Qt-rebase note and the codec note;
- **Known limitations**: link ARCHITECTURE §10, and list any item Task 21 deferred.

Then:

```bash
gh release edit v0.7.5 --draft=false --prerelease --notes-file "$TMPDIR/notes-0.7.5.md"
gh release view v0.7.5 --json isDraft,isPrerelease,url
```

Expected: `"isDraft":false`, `"isPrerelease":true`, and the URL, which goes to the user.

---

## Self-review against the spec

| Spec section / user decision | Task(s) | How it is proven |
|---|---|---|
| §2 supported matrix | 1 (images), 15, 16, 17, 20 | Each distro has a container check and a package install-check; the README table is grepped against artifact names (Task 20 Step 4) |
| §3 tiers and degradation; decision 1 (6.4 floor, two tiers, documented degradation) | 3–7, 19, 20 | Host full and compat gates, `C(ubuntu-24.04)`/`C(debian-12)` = compat, `C(debian-13)` = full; user visual checks (Tasks 5, 6, 21); the README and ARCHITECTURE tables |
| §4.1 build system (CMake 3.25, `find_package` 6.4, guarded `qt_standard_project_setup`, `RESOURCE_PREFIX`, `loadFromModule`) | 3 | Configures on Debian 12's CMake 3.25.1 and Qt 6.4.2 |
| §4.2 C++ shims (`qReturnArg`, `makeReadyValueFuture`, QtTypes) | 4 | A new untyped-handler test; the 6.4 compile |
| §4.3 QML shims, the seven types, staging tests, `tst_focus_clip` QSKIP | 5, 6 | Hygiene greps (no effects import outside shims); both host tiers; 6.4 build |
| §4.4 behaviour checks (KeyNavigation pointers, XF86OK, focus) | 7 | Private-header read; the full suite on 6.4.2 ×2; a recorded sweep |
| §5 SDL3; decision 2 (bundled where missing, pinned, reason stated) | 8, 15 (`rules`), 17 | `ldd` shows no shared SDL; "SDL3 gamepad support active" on 24.04, Debian 12 and the AppImage; the reason is in the commit body |
| §6 secrets; decision 3 (KWallet 5 + Secret Service over QtDBus, probe order, vault last, tests with an injectable transport) | 12, 13, 14 | `tst_secret_backends` (probe table, wire formats, unavailable versus refused, end to end); `tst_secrets_store` untouched and green; manual KWallet 6 and GNOME checks (Task 21) |
| §6.4 Flatpak talk-names, PKGBUILD optdepends | 14 | Diff review |
| §7.2 `.deb` (four releases, computed Depends, lintian) | 15 | Built and installed into bare containers with the self-test |
| §7.3 `.rpm` (Fedora 43/44, exact Qt pin, rpmlint) | 16 | Same |
| §7.4 AppImage on ubuntu:24.04 with aqt Qt, glibc ≤ 2.39, host tests; decision 4 | 17 | The glibc check in the script; the self-test on bare trixie and fedora:43 |
| §7.5 Flatpak and Arch keep working | 14 (manifest), 22 (release.yml's unchanged jobs) | `release.yml`'s Arch and Flatpak jobs green in Task 22 |
| §8.1 scripts/ci; "Ubuntu 24.04 container early" | **1** | The first task; every later task verifies with it |
| §8.2 `ci.yml` `floor` (24.04) and `debian-13` jobs | 9 | Rehearsed locally with the same scripts; green on `main` in Task 22 Step 3 |
| §8.3 `packages.yml`, `release.yml` attaches packages and checks versions | 18 | actionlint; a real run on `main` before tagging (Task 22) |
| §8.4 release procedure (CI green on `main` before tagging, pre-release with notes); decision 6 | 22 | Explicit STOP for the user; `gh run watch --exit-status` on both workflows before `git tag` |
| §9.1 `tst_navigation_history` on 6.8.2; decision 5 | 2 | 29/29 in one run in `debian-13` |
| §9.2 MPRIS `CanGoPrevious`; decision 7 | 10 | A new failing-then-passing test |
| §9.3 web remote Next/Previous; ARCHITECTURE Skip paragraph; decision 7 | 11 | A new failing-then-passing test; the doc edit in the same commit |
| §10 docs (README, ARCHITECTURE, AppImage README); decision 6 | 17, 19, 20 | A path-existence check; the artifact-name grep |
| §11 risks 1–7 | 7 (1), 5/6/21 (2), 16/20 (3), 13/21 (4), 17 (5), 19 (6), 21 (7) | As listed in each task |
| §12 acceptance 1–10 | 21 (1–7, 10), 22 (8, 9) | The final gate's table and the release's asset list |
| Out of scope: Windows, *Forgotten favourites* ordering | none | Not touched; the README limitation stays |

**Gaps checked for and closed while writing:**
- Every task that runs a build names its `/tmp/wNN*` directory, and none deletes one.
- Every commit stages explicit paths.
- The only push and tag live in Task 22, behind a STOP.
- The test count moves from 74 to 75 in Task 12 (`tst_secret_backends`) and is quoted as 75 in Tasks 19 and 20. **A task that adds another test executable must update both.**
- `deps.sh`'s package lists are replaced by the control file and the spec (Tasks 15 and 16) only after those exist. Until then the Task 1 arrays serve.
