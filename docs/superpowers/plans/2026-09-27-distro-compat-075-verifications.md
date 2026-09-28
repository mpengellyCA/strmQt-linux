# 0.7.5 distro compatibility: measurements

Every entry is a command that was run and what it printed, not an expectation.
Tasks append here; the final gate (Task 21) checks the record is complete.

## Task 1: baselines (before any porting)

| Target | Qt | Result |
|---|---|---|
| host (Arch) | 6.11.2 | gate H green: no build warnings; ctest `100% tests passed out of 74`; qmllint `baseline matches (1336 warnings)`; self-test 15/15, `selftest.sh: OK` |
| debian-13 | 6.8.2 | build clean; ctest `99% tests passed, 1 tests failed out of 74`, `tst_navigation_history` fails; self-test 15/15, `selftest.sh: OK` |
| ubuntu-24.04 | 6.4.2 | configure fails: `Could not find a configuration file for package "Qt6" that is compatible with requested version "6.8"` (`Qt6Config.cmake, version: 6.4.2`) |

## Task 2: tst_navigation_history on Qt 6.8.2

Before: 18 passed, 11 failed ("Type StrmGrid unavailable" against a previous
test's deleted temp dir). Cause confirmed by keeping every temp dir alive (all
pass). After staging the module once per run: 29 passed on 6.8.2 and 6.11.x.

## Task 3: first 6.4 compile

debian-12 container (`cmake version 3.25.1`, Qt 6.4.2): configure completes
(`-- Configuring done`, `-- Generating done`). The plan's build command stops at
the first failure, which is one the plan did not predict:

```
/src/src/server/emby/EmbyWebSocket.cpp:195:36: error: 'errorOccurred' is not a member of 'QWebSocket'
FAILED: src/CMakeFiles/strmqt_core.dir/server/emby/EmbyWebSocket.cpp.o
```

`QWebSocket::errorOccurred` is Qt 6.5 (6.4 has `error(QAbstractSocket::SocketError)`).
New information for Task 4. Because every other object waits on `strmqt_core`
(`ninja -k 0` builds nothing further), each remaining non-generated object was
compiled on its own (`ninja -t commands -s <obj> | bash`). The rest of
`strmqt_core` compiles; the other errors are exactly the predicted ones:

```
/src/src/app/ImageLimits.h:5:10: fatal error: QtTypes: No such file or directory
/src/src/app/music/MusicRepository.cpp:26:22: error: 'makeReadyValueFuture' is not a member of 'QtFuture'; did you mean 'makeReadyFuture'?
/src/src/input/InputMap.cpp:1066:40: error: 'qReturnArg' was not declared in this scope; did you mean 'QReturnArgument'?
```

`<QtTypes>` is a fatal error, so translation units including `ImageLimits.h`
could hide further errors until it is fixed.

Host gate H (Qt 6.11): no build warnings; ctest `100% tests passed out of 74`;
qmllint `baseline matches (1336 warnings)`; self-test 15/15, `selftest.sh: OK`;
`strings -el /tmp/w03a/strmqt | grep -m1 'qt/qml/StrmQt'` prints `/qt/qml/StrmQt/ui/Main.qml`.

## Task 4: the C++ on Qt 6.4

Host (Qt 6.11.2), before the fix, the new untyped-handler test fails as predicted:

```
QMetaObject::invokeMethod: No such method QObject_QML_0::invokeAction(QString,bool)
Candidates are:
    invokeAction(QVariant,QVariant)
FAIL!  : InputMapTest::triggerReachesAnUntypedQmlHandler() 'm_map->trigger(QStringLiteral("app.fullscreen"), true)' returned FALSE.
```

Four C++ sites ported: `InputMap::trigger` (signature-shaped call, `qReturnArg`
6.5+ / `QArgument` below), `MusicRepository.cpp` `ready()` (`makeReadyFuture`
below 6.6), `EmbyWebSocket.cpp` (`errorOccurred` 6.5+ /
`qOverload<QAbstractSocket::SocketError>(&QWebSocket::error)` below), and
`ImageLimits.h` (`<QtTypes>` -> `<QtGlobal>`). Fixing `<QtTypes>` unmasked no
further C++ errors.

ubuntu-24.04 (Qt 6.4.2, GCC 13.2), plan's Step 6 command: no `error:` or
`FAILED` lines, `[build exit 0]`. A clean rebuild logs 0 `warning:` lines and
links `/build/strmqt`. No qmlcachegen failure either: the QML effect and font
features sites (Tasks 5, 6) do not fail the 6.4 build; they are runtime
matters. debian-12 (Qt 6.4.2, GCC 12): same, `[build exit 0]`, 0
`error:|FAILED|warning:` lines.

Tests on 6.4.2 as the plan runs them: `tst_music_repository` `Totals: 31
passed, 0 failed`; `tst_emby_websocket` `Totals: 13 passed, 0 failed` (both
images). `tst_input_map` `Totals: 28 passed, 2 failed` on both images:
`triggerReachesAQmlHandler` and `triggerReachesAnUntypedQmlHandler` fail at
`component.create()` with `module "QtQml.Models" is not installed`. `import
QtQml` pulls `QtQml.Models` on 6.4, and `deps.sh` does not install
`qml6-module-qtqml-models`. With that package installed in a throwaway
container (`apt-get install qml6-module-qtqml-models`, image unchanged),
ubuntu-24.04 gives `tst_input_map` `Totals: 30 passed, 0 failed`, including
`triggerReachesAQmlHandler`, `triggerReachesAnUntypedQmlHandler`,
`triggerAsksTheNewestLiveHandler` and `triggerSkipsGoneAndMalformedHandlers`.
That closes the study's risk 3, pending the package being added to `deps.sh`.

Signatures the Qt 6.4.2 meta-object reports for a QML `invokeAction`:

```
bool invokeAction(QString,bool)          # function invokeAction(actionId: string, autoRepeat: bool): bool
QVariant invokeAction(QVariant,QVariant) # function invokeAction(actionId, autoRepeat)
```

Host gate H (NN=04, Qt 6.11.2): no build warnings; ctest `100% tests passed out
of 74`; qmllint `baseline matches (1336 warnings)`; `selftest.sh: OK`.

### Task 4 follow-up: QtQml.Models in the apt images

I compared the imports in src/ and tests/ (`QtQml`, `QtQuick`, `QtQuick.Controls.Basic`, `QtQuick.Effects`, `QtQuick.Window`)
with the installed modules. For each apt image, every module that an installed module's qmldir `import`/`depends` line names,
but that is not itself installed:

| Image | Missing |
|---|---|
| ubuntu-24.04, debian-12 (6.4.2) | `QtQml.Models`, plus the Controls platform styles for Windows/iOS/macOS (optional, non-Linux) |
| debian-13 (6.8.2), ubuntu-26.04 (6.10.2) | only the Controls platform styles (with FluentWinUI3); `QtQml.Models` is already pulled in |

`qml6-module-qtqml-models` exists on all four (6.4.2+dfsg-4build3, 6.4.2+dfsg-1, 6.8.2+dfsg-7, 6.10.2+dfsg-3), so it
goes in `apt_common`. `QtQuick.Effects` is still missing on 6.4, where it does not exist; Task 5 handles that.

C(ubuntu-24.04) and C(debian-12) on the rebuilt images (`...-356a6f99c6ad`): `tst_input_map` `Passed`. ctest
`89% tests passed, 8 tests failed out of 74` (ubuntu-24.04) and `91% tests passed, 7 tests failed out of 74` (debian-12).
Failures:

- `module "QtQuick.Effects" is not installed` (Task 5): tst_qml_accessibility, tst_navigation_history, tst_record_stage,
  tst_focus_clip, tst_card_component (then SIGSEGV at address 0x8 in `menuKeyAsksForTheCurrentCard`, after `m_root` failed
  to load). tst_music_player_panel also hits it.
- `Cannot assign to non-existent property "features"` / `"variableAxes"` (Task 6): tst_music_player_panel (CrateKicker.qml:14),
  tst_crate_controls (CrateHeading.qml:16).
- ubuntu-24.04 only, not QML: tst_mpv_video_item `bundledScriptsAreNotLoaded` `'luaThreads.isEmpty()' returned FALSE. (lua/console)`.
  Ubuntu 24.04's libmpv starts its console script thread anyway. For Task 7.

Self-test (run on its own, because check.sh stops at ctest): on both images the Qt < 6.5 load path reaches
`qrc:/qt/qml/StrmQt/ui/Main.qml` and fails at `Main.qml:1018:17: Type StrmIcon unavailable` / `StrmIcon.qml:2:1:
module "QtQuick.Effects" is not installed`, with `selftest.sh: /build/strmqt exited 1` (Task 5).

### Task 5: QML tier and effect shims

Host gate H (NN=05, Qt 6.11.2, `STRMQT_QML_TIER` auto): configure logs `StrmQt QML tier: full (Qt 6.11.2)`; no build
warnings; ctest `100% tests passed out of 74`; `selftest.sh: OK`, log line `QML tier: full on Qt 6.11.2`.
qmllint: the only change is StrmPanel's three `panel._shadow.*` `[unqualified]` entries collapsing into one on the
replacing line (`elevation: panel._shadow`), baseline updated under ruling R1 to 1334 warnings. No warning names a
file under `src/ui/shims/`.

Host gate HC (`-DSTRMQT_QML_TIER=compat`, Qt 6.11.2 with qt6-5compat): configure logs `StrmQt QML tier: compat
(Qt 6.11.2)`; no build warnings; ctest `100% tests passed out of 74`; `selftest.sh: OK`, log line `QML tier: compat
on Qt 6.11.2`. The self-test logs of the two tiers carry the same four warnings (all `not authenticated`).

C(ubuntu-24.04) (Qt 6.4.2): configure logs `StrmQt QML tier: compat (Qt 6.4.2)`; qmlcachegen compiles the four
compat shims; build exit 0. ctest `95% tests passed, 4 tests failed out of 74`. tst_card_component (no more
SIGSEGV), tst_record_stage, tst_qml_accessibility and tst_navigation_history now pass. The failures left:

- `Cannot assign to non-existent property "variableAxes"` / `"features"` (Task 6): tst_focus_clip and
  tst_crate_controls (CrateHeading.qml:16), tst_music_player_panel (CrateKicker.qml:14).
- tst_mpv_video_item `bundledScriptsAreNotLoaded` (lua/console), as before (Task 7).

No line of the log mentions an effects module. Self-test run on its own: `QML tier: compat on Qt 6.4.2`, then
`MiniPlayer.qml:315:14: Cannot assign to non-existent property "features"` and exit 1 (Task 6). StrmIcon no longer
stops it.

Visual check (full vs compat, both on Qt 6.11): pending the user.

## Task 6: first full 6.4 run

Host gate H (NN=06, Qt 6.11.2, tier auto): configure logs `StrmQt QML tier: full (Qt 6.11.2)`; build exit 0 with
`STRMQT_WERROR=ON`; ctest `100% tests passed out of 74`; qmllint `baseline matches (1334 warnings)`, unchanged, and
no warning names a file under `src/ui/shims/`; `selftest.sh: OK`, `QML tier: full on Qt 6.11.2`, `15/15 pages
constructed`.

Host gate HC (`-DSTRMQT_QML_TIER=compat`, Qt 6.11.2): configure logs `StrmQt QML tier: compat (Qt 6.11.2)`; build
exit 0; ctest `100% tests passed out of 74` (tst_focus_clip passes, so Step 5's QSKIP is not needed);
`selftest.sh: OK`, `QML tier: compat on Qt 6.11.2`. Both tiers' self-test logs carry the same warnings (all
`not authenticated`). Running the H and HC ctests at the same time collides on fixed ports (tst_web_remote_*,
tst_image_cache); run serially, both are green.

C(ubuntu-24.04) (Qt 6.4.2): configure logs `StrmQt QML tier: compat (Qt 6.4.2)`; qmlcachegen compiles the three
new compat shims; build exit 0. ctest `99% tests passed, 1 tests failed out of 74`. No font.features/variableAxes
failure is left. The one failure (Task 7):

- tst_mpv_video_item `bundledScriptsAreNotLoaded`: `'luaThreads.isEmpty()' returned FALSE. (lua/console)`.

Self-test run on its own (`selftest.sh /build/strmqt /build/selftest.log`): `QML tier: compat on Qt 6.4.2`,
`selftest: 15/15 pages constructed`, `selftest.sh: OK`. The log carries warnings the host tiers do not (Task 7):

- `ui/shell/LoadingState.qml: Cannot instantiate bound component outside its creation context` (9 times), and once
  `ui/pages/MusicBrowsePage.qml` with the same message; both files use `pragma ComponentBehavior: Bound`.
- `QQmlComponent: Component is not ready` (7 times, right after the LoadingState warnings).
- `ReferenceError: Overlay is not defined` at `ui/pages/PlaylistPage.qml:1152` and `:1215`, and
  `ui/pages/MusicPlaylistPage.qml:737` and `:809` (`parent: Overlay.overlay`).
- `tls: cannot open certificate: "/root/.local/share/StrmQt/strmqt/webremote/cert.pem"` (fresh container home).

C(debian-12) was not run in this task (the dispatch named only ubuntu-24.04).

Visual check (full vs compat on Qt 6.11: Music home crate headings, section strip, badges, mini-player time
readout): pending the user.

## Task 7: Qt 6.4 behaviour sweep

C(debian-12) on the tree as Task 6 left it (`d90c250`), first run: `100% tests passed, 0 tests failed out of
74`, `selftest.sh: OK`, `check.sh: OK (compat)`. tst_mpv_video_item passes there (libmpv 0.35). Its self-test log
carries the same 6.4-only warnings as ubuntu-24.04 (9x LoadingState and 1x MusicBrowsePage "Cannot instantiate
bound component outside its creation context", 7x "Component is not ready", the four `Overlay` ReferenceErrors,
the TLS warning). No debian-12-only finding.

Step 1, KeyNavigation (ubuntu-24.04, `qt6-declarative-private-dev` in a throwaway container):
`/usr/include/x86_64-linux-gnu/qt6/QtQuick/6.4.2/QtQuick/private/qquickitem_p.h:731-736` declares `left`, `right`,
`up`, `down`, `tab`, `backtab` as `QPointer<QQuickItem>`, as 6.11 does. No change to `MusicAlbumPage.qml`.

Findings and resolutions:

| Where | Cause | Class | Resolution |
|---|---|---|---|
| tst_mpv_video_item `bundledScriptsAreNotLoaded` (ubuntu-24.04: `lua/console`) | libmpv 0.37 knows the console switch only as `load-osd-console` (renamed `load-console` in mpv 0.40, `options.c`), so the console's Lua VM ran in the app | app bug on 6.4 hosts | `8c7787d`: `load-osd-console` is the fallback when `load-console` is rejected. ubuntu-24.04 `Totals: 14 passed, 0 failed, 2 skipped` |
| Self-test: LoadingState (9x), MusicBrowsePage (1x) "Cannot instantiate bound component outside its creation context" | Qt 6.4's `QQuickLoader` creates its item in `new QQmlContext(creationContext)`; the object creator refuses a `ComponentBehavior: Bound` component whose parent context is not its creation context. 6.5 passes the creation context for bound components (`qquickloader.cpp`, 6.4.2 vs 6.5.0) | app bug on 6.4 (feature loss) | `3025ac1`: `BoundLoader` tier shim (full: `Loader`; compat: `createObject()` host with Loader's sizing and focus scope) at all six Loaders that load bound components. Nothing loaded on 6.4 before: Music browse sections, every custom rail/grid card (Music home shelves, artist, album, browse), the player OSD's track/chapter/queue/settings panels and scrub preview, loading skeletons |
| Self-test: 7x "QQmlComponent: Component is not ready" (album page) | Same Qt 6.4 pattern in `QQuickItemViewPrivate::createComponentItem` (header/footer); the backtrace (`QT_MESSAGE_PATTERN` `%{backtrace}`) ends in `QQuickItemView::componentComplete`. The album page's `footer:` is its only bound header/footer | app bug on 6.4 (More-by shelf missing) | `3025ac1`: `BoundViewSlot` tier shim (full: the component; compat: an unbound host that creates it); `MusicAlbumPage` footer goes through it |
| Self-test: `ReferenceError: Overlay is not defined` PlaylistPage.qml:1152/:1215, MusicPlaylistPage.qml:737/:809 | `QtQuick.Controls.Basic` registers `Overlay` only from 6.5 (6.11's `plugins.qmltypes` lists `QtQuick.Controls.Basic/Overlay`); on 6.4 it is in `QtQuick.Templates` only. The rename/delete sheets stayed inside the page | app bug on 6.4 (degraded) | `b08fd7b`: `import QtQuick.Templates as T`, `T.Overlay.overlay` |
| Self-test: `tls: cannot open certificate: .../webremote/cert.pem` | `WebRemoteController::updateFingerprint` reads the certificate at start-up; a fresh home has none until the web remote is first enabled (`TlsCertificateGenerator::ensureCertificate`). Any Qt, any fresh profile | benign, not 6.4 | none |
| Qt 6.4.2 `qmlcachegen` segfaults on `const x = item; x.width = loader.width` in a QML function | qmlcachegen bug, met while writing the compat `BoundLoader` | tooling | written through the property instead; commented in the shim |

Red/green on ubuntu-24.04: the bound card probe (`tst_card_component`, now `pragma ComponentBehavior: Bound` like
its real callers) failed 5 functions with the stock Loader in StrmRail/StrmGrid (the `3025ac1` message says 6;
it is 5: railLoads, currentFollows, cardSignals, hoverIsNotFocus, gridLoads) and passes with BoundLoader; the new
`viewCreatesAFooterFromABoundFile` fails with the full-tier BoundViewSlot staged and passes with the compat one.
`selftest.sh` now fails on "Cannot instantiate bound component" and "Component is not ready".

Compat shims on Qt 6.11 (instead of HC, see below): a bound probe run with `qml` over the compat `BoundLoader` and
`BoundViewSlot` prints exactly what the same probe prints with Qt's `Loader` (explicitly sized loader resizes its
item, 200x100 then 150; natural loader follows its item, 70x40 then 33; inactive null then created; source switch
reloads, `loaded` twice; footer created, height 25).

The spec §4.4 families (focus-chain clearing, Tab skipping invisible items, XF86OK, ShortcutOverride, pixel
measurements, untyped-annotation coercion) produced no failure: ctest is green on 6.4.2 without a version-gated
expectation.

Gates after the fixes:

- C(ubuntu-24.04): `100% tests passed, 0 tests failed out of 74`, `selftest.sh: OK`; self-test warnings now only
  the host's four `not authenticated` plus the benign TLS line. check.sh then stopped at its install step:
  `"/build/stage/usr/bin/strmqt": No space left on device` (/tmp full, below). The same install check run inside
  the container on its own filesystem (`DESTDIR=/stage cmake --install /build` plus check.sh's `find`): no
  unexpected files, tier `compat`.
- H (NN=07): build 0 warnings (`STRMQT_WERROR=ON`), ctest `100% tests passed out of 74`, qmllint `baseline matches
  (1334 warnings)`, `selftest.sh: OK` (log at `/tmp/w07a/selftest.log`: 3x `home refresh error: "not
  authenticated"`, 1x `playlist: "not authenticated"`).
- HC: not run. C(debian-12) after the fixes: not completed, GCC `error writing to /build/tmp/cc*.s: No space left
  on device`. /tmp (16 GB tmpfs) was full: `/tmp/w06a`, `/tmp/w06b` (2.5 GB each, left alone), `/tmp/w07a`,
  `/tmp/w07a-ubuntu-24.04`, `/tmp/w07a-debian-12` (about 3 GB each) and 1.5 GB of editor `preamble-*.pch`.

For Task 19/20: two new tier shims, `BoundLoader` and `BoundViewSlot` (spec §4.3 list). No cosmetic 6.4
degradation is left open from this sweep.

## Task 15: native .deb packages

Built with `scripts/ci/local.sh $T /tmp/w15a-$T -- bash -c "TMPDIR=/var/tmp /src/scripts/ci/package-deb.sh $T /build/out"`
(the scratch tree in the container's own filesystem, so /tmp holds only the .deb), then install-checked in the
**bare** distro image, glob and `dpkg-deb` inside the container (ruling R10):
`podman run --rm --security-opt label=disable -v /tmp/w15a-$T/out:/pkgs:ro -v "$PWD:/src:ro" $(scripts/ci/deps.sh --image $T) bash -c '/src/scripts/ci/install-check.sh /pkgs/*.deb'`.

| Target | Package | Build | ctest | lintian | Install pulls | Self-test |
|---|---|---|---|---|---|---|
| ubuntu-24.04 | `strmqt_0.7.5-1~ubuntu24.04_amd64.deb` | compat, SDL3 bundled 3.4.16 | 75/75 | clean (2 overrides) | `qml6-module-qt5compat-graphicaleffects`, `libqt6svg6` | `QML tier: compat on Qt 6.4.2`, 15/15, `selftest.sh: OK` |
| debian-12 | `strmqt_0.7.5-1~deb12_amd64.deb` | compat, SDL3 bundled 3.4.16 | 75/75 | clean (2 overrides) | `qml6-module-qt5compat-graphicaleffects`, `libqt6svg6` | `QML tier: compat on Qt 6.4.2`, 15/15, `selftest.sh: OK` |
| debian-13 | `strmqt_0.7.5-1~deb13_amd64.deb` | full, system SDL3 3.2.10 | 75/75 | clean (2 overrides) | `qml6-module-qtquick-effects`, `qt6-svg-plugins`, `libsdl3-0` | `QML tier: full on Qt 6.8.2`, 15/15, `selftest.sh: OK` |
| ubuntu-26.04 | `strmqt_0.7.5-1~ubuntu26.04_amd64.deb` | full, system SDL3 3.4.2 | 75/75 | clean (2 overrides) | `qml6-module-qtquick-effects`, `qt6-svg-plugins`, `libsdl3-0` | `QML tier: full on Qt 6.10.2`, 15/15, `selftest.sh: OK` |

All four self-tests log `SDL3 gamepad support active`. Resolved `Depends` excerpts (`dpkg-deb -f … Depends`, in the container):

- ubuntu-24.04, debian-12: `libqt6qml6 (>= 6.4.2)`, `libqt6qml6 (<< 6.4.3~)`, `qml6-module-qt5compat-graphicaleffects`, `libqt6svg6`, `qml6-module-qtqml-models`
- debian-13: `libqt6qml6 (>= 6.8.2)`, `libsdl3-0 (>= 3.2.0)`, `libqt6qml6 (<< 6.8.3~)`, `qml6-module-qtquick-effects`, `qt6-svg-plugins`, `qml6-module-qtqml-models`
- ubuntu-26.04: `libqt6qml6 (>= 6.10.2)`, `libsdl3-0 (>= 3.2.0)`, `libqt6qml6 (<< 6.10.3~)`, `qml6-module-qtquick-effects`, `qt6-svg-plugins`, `qml6-module-qtqml-models`

Lintian warnings: only `no-manual-page` for `usr/bin/strmqt` and `usr/bin/strmqt-cli`, overridden in
`packaging/debian/strmqt.lintian-overrides` ("GUI app; strmqt-cli --help documents itself"). The DEP-5
copyright, with the SDL3 note as a stand-alone comment paragraph, drew no tag. `dpkg -L strmqt` on 24.04 lists
`/usr/share/doc/strmqt/SDL3-LICENSE.txt`, the path `debian/copyright` names (R4).

QML imports in `src/ui` (`QtQuick`, `QtQuick.Controls.Basic`, `QtQuick.Templates`, `QtQuick.Window`,
`QtQuick.Effects` / `Qt5Compat.GraphicalEffects`) map to the `qml6-module-*` Depends; `qml6-module-qtqml-models`
(Task 4 follow-up) was added to Depends and Build-Depends.

Found on the way:

- debhelper passes `-DFETCHCONTENT_FULLY_DISCONNECTED=ON`, so the first 24.04 build failed at configure
  (`Target "strmqt" links to: SDL3::SDL3-static but the target was not found`). `rules` passes
  `-DFETCHCONTENT_FULLY_DISCONNECTED=OFF` when it bundles SDL3.
- The first debian-13 image had neither `libsdl3-dev` nor `qt6-svg-plugins`: an or-group already satisfied by a
  transitively installed alternative installs nothing (`libsdl2-dev` pulls `libudev-dev`; `qt6-svg-dev` pulls
  `libqt6svg6`). The build bundled SDL3 and `rules` stopped with `rules: no Qt SVG image plugin installed`.
  Build-Depends now read `libsdl3-dev | libudev-dev (<< 256)` and `qt6-svg-plugins | libqt6svg6 (<< 6.5)`
  (systemd 255/252 and Qt 6.4 on noble/bookworm; 257+ and 6.8+ on trixie/26.04). All four packages above were
  built from that control file.

C(debian-13) on the image `deps.sh` now builds from `control` (mk-build-deps): `SDL3: system 3.2.10`,
`100% tests passed, 0 tests failed out of 75`, `selftest.sh: OK`, `check.sh: OK (full)`.

## Task 16: native .rpm packages

Built with `scripts/ci/local.sh $T /tmp/w16a-$T -- bash -c "TMPDIR=/var/tmp /src/scripts/ci/package-rpm.sh $T /build/out"`
(the rpmbuild tree in the container's own filesystem, removed by the script's EXIT trap), then install-checked in
the **bare** Fedora image, glob and `rpm -qp` inside the container (ruling R10):
`podman run --rm --security-opt label=disable -v /tmp/w16a-$T/out:/pkgs:ro -v "$PWD:/src:ro" $(scripts/ci/deps.sh --image $T) bash -c '/src/scripts/ci/install-check.sh /pkgs/*.rpm && rpm -qp --requires /pkgs/*.rpm | grep -E "qtdeclarative|svg|SDL3"'`.

| Target | Package | Build | %check ctest | rpmlint | Install pulls (Qt/SDL) | Self-test |
|---|---|---|---|---|---|---|
| fedora-43 | `strmqt-0.7.5-1.fc43.x86_64.rpm` | full, system SDL3 3.4.16 | 75/75 | 0 errors, 0 warnings, 8 filtered | `qt6-qtdeclarative 6.10.3`, `qt6-qtsvg`, `qt6-qtwayland`, `SDL3 3.4.16` | `QML tier: full on Qt 6.10.3`, 15/15, `selftest.sh: OK` |
| fedora-44 | `strmqt-0.7.5-1.fc44.x86_64.rpm` | full, system SDL3 3.4.16 | 75/75 | 0 errors, 0 warnings, 8 filtered | `qt6-qtdeclarative 6.11.2`, `qt6-qtsvg`, `qt6-qtwayland`, `SDL3 3.4.16` | `QML tier: full on Qt 6.11.2`, 15/15, `selftest.sh: OK` |

Both self-tests log `SDL3 gamepad support active`. `rpm -qp --requires` excerpts: fc43
`qt6-qtdeclarative(x86-64) = 6.10.3`, fc44 `qt6-qtdeclarative(x86-64) = 6.11.2`; both `qt6-qtsvg(x86-64)`,
`libSDL3.so.0(SDL3_0.0.0)(64bit)`. `%{_qt6_version}` (qt6-rpm-macros) expands to the installed Qt on both, so no
`--define` is needed. Fedora's `qt6-qtdeclarative` carries every QML module the app imports (QtQuick,
QtQuick.Controls.Basic, QtQuick.Templates, QtQuick.Window, QtQuick.Effects), so the pin doubles as the QML
Requires; `qt6-qtsvg` is the SVG image plugin.

rpmlint (8 filtered = Fedora's own 3 plus 5 in `packaging/rpm/strmqt.rpmlintrc`): `spelling-error` for `gamepad`,
`libmpv`, `libvlc` in `%description`, and `no-manual-page-for-binary` for `strmqt` / `strmqt-cli` (same reason as
the lintian override).

Found on the way:

- rpmlint `E: binary-or-shlib-defines-rpath /usr/bin/strmqt (RUNPATH: $ORIGIN:$ORIGIN/../lib64)`: Qt's
  `qt_standard_project_setup()` sets an `$ORIGIN` install RPATH. The spec's `%cmake` adds
  `-DCMAKE_SKIP_INSTALL_RPATH=ON`.
- The first install check stopped at `selftest.sh: not every page was constructed` with the app exiting 0: Fedora's
  qt6-qtbase ships `/usr/share/qt6/qtlogging.ini` with `*.debug=false`, which silences `console.log` (category
  `qml`, debug), so the per-page lines and the summary never reached the log. `selftest.sh` now runs the binary with
  `QT_LOGGING_RULES="qml.debug=true"`. This also affected `check.sh` (C(fedora-*)) before this task.

C(fedora-43) on the image `deps.sh` now builds with `dnf builddep` from the spec: `SDL3: system 3.4.16`,
`100% tests passed, 0 tests failed out of 75`, `selftest: 15/15 pages constructed`, `selftest.sh: OK`,
`check.sh: OK (full)`.

Task 16 fix round 1 (review):

- KWallet Recommends. `dnf repoquery` in the fedora-43 / fedora-44 CI images:
  - `--whatprovides kwallet` → `kwallet-0:4.12.3-28.fc43` / `kwallet-0:4.12.3-29.fc44` (KDE 4's kwallet, plus
    `kwalletmanager-0:15.04.3`), not the Plasma 6 daemon;
  - `kf6-kwallet kf5-kwallet` → `kf5-kwallet-0:5.116.0-4.fc43` / `-5.fc44`, `kf6-kwallet-0:6.30.0-1.fc43` / `.fc44`
    (plus the GA 6.18.0 / 6.25.0 and i686 builds);
  - `--whatprovides /usr/bin/ksecretd /usr/bin/kwalletd6` → `kf6-kwallet` only.
  Recommends is now `(kf6-kwallet or gnome-keyring or keepassxc)`.
- `%license COPYING assets/fonts/OFL-*.txt`. Rebuilt fc43 (`/tmp/w16f-fedora-43`): ctest 75/75, rpmlint
  `0 errors, 0 warnings, 8 filtered`. `rpm -qlp` lists `/usr/share/licenses/strmqt/COPYING` and the three OFL
  texts. `rpm -qp --recommends` gives `(kf6-kwallet or gnome-keyring or keepassxc)`. The bare fedora-43 install
  check logs `SDL3 gamepad support active`, `QML tier: full on Qt 6.10.3`, `selftest: 15/15 pages constructed`,
  `selftest.sh: OK`. fc44 was not rebuilt, because nothing release-specific changed.

## Task 17: the AppImage on Ubuntu 24.04

`deps.sh appimage` on `ubuntu:24.04`: `aqt list-qt linux desktop --arch 6.11.3` → `linux_gcc_64` (install dir
`gcc_64`), as pinned. `aqt list-qt linux desktop --archives 6.11.3 linux_gcc_64` → `icu qtbase qtdeclarative qtdoc
qtsvg qttools qttranslations qtwayland`: QtWayland and QtSvg are in the base download, so only `-m qtwebsockets` is
added (ruling R18). CMake: `StrmQt QML tier: full (Qt 6.11.3)`, `SDL3: bundled 3.4.16 (static)`.

Built with the brief's Step 4 command, but with the source copy and `TMPDIR` in the container's `/var/tmp`:

```
==>   Qt plugins in usr/plugins, QML in usr/qml
==>   121 librar(y/ies) bundled, 60 pruned as host-owned
==>   147 top-level libraries -> $ORIGIN
==>   0 plugin/QML object(s) needed an RPATH top-up
==>   clean: no forbidden sonames in /build/out/appimage/AppDir/usr/lib
==>   libqwayland.so, wayland-shell-integration/, libmpv.so.2, libqsvg.so all present
==>   newest glibc symbol needed: GLIBC_2.38
==> Done: /build/out/dist/StrmQt-0.7.0-x86_64.AppImage (178M)
```

Bare-host self-test (`appimage-host.sh T`, then `selftest.sh` on a copy of the AppImage with
`APPIMAGE_EXTRACT_AND_RUN=1`):

| Host | Result |
|---|---|
| `debian:trixie` (debian-13) | `SDL3 gamepad support active`, `QML tier: full on Qt 6.11.3`, `selftest: 15/15 pages constructed`, `selftest.sh: OK` |
| `fedora:43` | same four lines, `selftest.sh: OK` |
| `ubuntu:24.04` (Debian list) | same four lines, `selftest.sh: OK` |

Found on the way:

- The first build put Qt's libraries in `usr/lib/x86_64-linux-gnu`: GNUInstallDirs picks that `LIBDIR` for a `/usr`
  prefix on Debian/Ubuntu, and Qt's deploy step follows it (`qt.conf` said `Libraries = lib/x86_64-linux-gnu`).
  `strmqt`'s `$ORIGIN/../lib` RPATH then missed them (`ldd`: `libQt6Core.so.6 => not found`), and the
  forbidden-soname assertion, which checks `usr/lib` only, never saw them. `build-appimage.sh` now passes
  `-DCMAKE_INSTALL_LIBDIR=lib`.
- The deployed `qt.conf` has no `Plugins=` or `QmlImports=` key under aqt: `qt6_deploy_qt_conf` omits a key whose value
  is Qt's default (`plugins`, `qml`). `build-appimage.sh` reads an absent key as that default, as QLibraryInfo does,
  and fails if `qt.conf` or `Prefix=` is missing or a resolved directory does not exist.
- The host table was wrong. It is now every `DT_NEEDED` soname of the AppDir that the AppDir does not carry (73 beyond
  glibc), mapped to packages with `apt-file` (trixie) and `dnf repoquery --whatprovides` (fedora:43). Ubuntu's libmpv
  and ffmpeg link SDL2, JACK, PipeWire, PulseAudio, Vulkan, OpenCL, VDPAU, XScrnSaver, Xv, Xpresent, cairo, pango,
  gdk-pixbuf, gnutls and gcrypt directly, so all of them are needed at startup.
- `libbz2`: the first fedora-43 run stopped at `strmqt: error while loading shared libraries: libbz2.so.1.0`.
  Debian's soname is `libbz2.so.1.0`; Fedora's `bzip2-libs` ships only `libbz2.so.1`. libbz2 left the forbidden list
  (build-appimage.sh and Deploy.cmake together) and is bundled.
- The glibc check, exercised outside the build: an AppDir holding only a `/bin/sh` AppRun fails with "no bundled ELF
  object imports a GLIBC_ symbol"; one holding Arch's `libavutil.so.61` fails with "a bundled object needs
  GLIBC_2.44, above the 2.39 floor".

Task 17 fix round 1 (review), rebuilt in `/tmp/w17f-appimage`:

- The host now provides `libstdc++`/`libgcc_s`: they are in FORBIDDEN and in Deploy.cmake's pre-exclude list, and
  `find AppDir -name 'libstdc++*' -o -name 'libgcc_s*'` finds nothing. The build logs
  `newest GLIBC symbol needed: GLIBC_2.38` and `newest GLIBCXX symbol needed: GLIBCXX_3.4.32` (floor 3.4.33).
  Exercised outside the build: Arch's `libbotan-3.so.13` fails with "needs GLIBCXX_3.4.35, above the 3.4.33 floor";
  a static ELF (no dynamic section, `objdump -T` exits 1) is skipped instead of aborting.
- The type2 runtime is pinned: `20251108` `runtime-x86_64`, sha256 `2fca8b44…ec260d` (matches GitHub's asset
  digest), passed as `--runtime-file`. Re-packing the same AppDir in the appimage image with `--network none`
  succeeds, and the packed AppImage's first 944632 bytes equal the pinned runtime except the 16-byte `.digest_md5`
  section appimagetool fills in.
- Bare-host self-test: ubuntu:24.04, fedora:43 and debian:trixie each log `SDL3 gamepad support active`,
  `QML tier: full on Qt 6.11.3`, `selftest: 15/15 pages constructed`, `selftest.sh: OK`.
