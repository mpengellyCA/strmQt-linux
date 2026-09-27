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
