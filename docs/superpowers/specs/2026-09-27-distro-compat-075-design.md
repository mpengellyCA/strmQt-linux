# StrmQt 0.7.5: distro compatibility

Date: 2026-09-27. Status: approved design, pending implementation plan.
Branch: `compat-075` (from `main` at v0.7.0).

Evidence base: `.superpowers/compat-0.7.5-study.md` (the "study"). Every version,
package name and API date below comes from it or from a package query run on
2026-09-27 in the named container. Where this spec departs from the study's
recommendation, §13 says so and why.

---

## 1. Why

StrmQt builds only against Qt 6.8 or newer, and its only portable artifact is the
Flatpak. The Arch package is Arch-only, and the AppImage imports `GLIBC_2.43`,
so in practice it is Arch-only too. That leaves out the largest install base
there is: Ubuntu 24.04 LTS and everything built on it (Mint 22, Pop!_OS 24.04,
Zorin, elementary 8), plus Debian 12. Both ship Qt **6.4.2**.

0.7.5 makes StrmQt build and run on each target distro's **own** Qt, from 6.4.2 up.
It ships a native package per distro release and an AppImage with a glibc 2.39 floor.

The study measured the porting job and found it small and bounded:
- 3 C++ APIs newer than 6.4;
- 3 QML features newer than 6.4;
- 2 CMake features newer than 6.4.

Only MultiEffect (7 sites) needs real work. The rest is packaging and CI.

### Out of scope

- Windows (the next release).
- The *Forgotten favourites* ordering (ARCHITECTURE.md §10).
- Bundling Qt into a native package, or a PPA or backport of a newer Qt.
- Distros that ship Qt 6.5–6.7. No supported target does. They get the compat tier (§3), untested.
- Debian-archive-quality packaging (source packages, sbuild, no network during build). The `.deb`/`.rpm` files are our own release artifacts (§7.2).
- Portal-based secrets (`org.freedesktop.portal.Secret`) and portal-based idle inhibition. §11 lists these as follow-ups.
- A Copr or OBS repository for Fedora (§7.3 says what a Qt rebase costs instead).

---

## 2. Supported matrix

| Distro | Qt | QML tier | SDL3 | Wallet reached through | Artifact |
|---|---|---|---|---|---|
| Ubuntu 24.04 LTS (noble) and derivatives | 6.4.2 | compat | bundled 3.4.16 (static) | kwalletd5 (Kubuntu, Plasma 5.27) / Secret Service (GNOME) | `strmqt_0.7.5-1~ubuntu24.04_amd64.deb` |
| Ubuntu 26.04 LTS (resolute) | 6.10.2 | full | system 3.4.2 | kwalletd6 / Secret Service | `strmqt_0.7.5-1~ubuntu26.04_amd64.deb` |
| Debian 12 (bookworm) | 6.4.2 | compat | bundled 3.4.16 (static) | kwalletd5 / Secret Service | `strmqt_0.7.5-1~deb12_amd64.deb` |
| Debian 13 (trixie) | 6.8.2 | full | system 3.2.10 | kwalletd6 / Secret Service | `strmqt_0.7.5-1~deb13_amd64.deb` |
| Fedora 43 | 6.10.3 | full | system 3.4.16 | kwalletd6 / Secret Service | `strmqt-0.7.5-1.fc43.x86_64.rpm` |
| Fedora 44 | 6.11.x | full | system | kwalletd6 / Secret Service | `strmqt-0.7.5-1.fc44.x86_64.rpm` |
| Arch and derivatives | 6.11 | full | system | kwalletd6 / Secret Service | `strmqt-0.7.5-1-x86_64.pkg.tar.zst` (unchanged) |
| Any x86_64 with glibc ≥ 2.39 | bundled 6.11.3 | full | bundled (static) | as the host provides | `StrmQt-0.7.5-x86_64.AppImage` |
| Any with Flatpak | KDE runtime 6.11 | full | runtime | as the host provides | `ca.mikesdev.StrmQt.flatpak` (unchanged) |

Other versions measured on 2026-09-27 (they bound the build-dependency lists in §7):

| | Ubuntu 24.04 | Debian 12 | Debian 13 | Ubuntu 26.04 | Fedora 43 |
|---|---|---|---|---|---|
| CMake | 3.28.3 | **3.25.1** | 3.31.6 | 4.2.3 | 3.31.11 |
| GCC | 13 | 12.2 | 14.2 | 15.2 | 15.2 |
| libmpv | 0.37.0 | **0.35.1** | 0.40.0 | 0.41.0 | 0.40.0 |
| libvlc | 3.0.20 | 3.0.23 | 3.0.24 | 3.0.23 | 3.0.23 |
| SVG image plugin in | `libqt6svg6` ✔ | `libqt6svg6` | `qt6-svg-plugins` | `qt6-svg-plugins` | `qt6-qtsvg` |
| QtQuick.Effects | absent | absent | `qml6-module-qtquick-effects` | same | in `qt6-qtdeclarative` |
| Qt5Compat effects | `qml6-module-qt5compat-graphicaleffects` 6.4.2 | same, 6.4.2 | (not needed) | (not needed) | (not needed) |

The floor is **CMake 3.25**, **GCC 12** and **glibc 2.36** (native), or **glibc 2.39** (AppImage).

---

## 3. Two tiers, and what degrades

**The full tier** is Qt 6.8 and newer. It is today's app, unchanged.

**The compat tier** is Qt 6.4–6.7. It keeps every feature and every page. It changes how a few things *look*:

| Surface | Full tier (6.8+) | Compat tier (6.4–6.7) | What a user sees on compat |
|---|---|---|---|
| Icon tint (`StrmIcon`, every glyph) | `MultiEffect` colorization | `Qt5Compat.GraphicalEffects` `ColorOverlay` | Identical for the white-glyph icon set (ColorOverlay keeps the source's alpha, so antialiasing survives). The colour animation is kept. |
| Panel shadow (`StrmPanel`), sleeve shadow (`RecordStage`) | `MultiEffect` shadow | `DropShadow` | A close approximation. Blur radius maps as `blur × 32 px`, MultiEffect's default `blurMax`. |
| Round masks (`CratePortrait`, record label) | `MultiEffect` mask | `OpacityMask` | The edge takes the mask rectangle's own antialiasing instead of `maskSpreadAtMin`. It is marginally harder. |
| Backdrop wash (`HomePage`, `PersonPage`) | `MultiEffect` blur 48 + saturation −0.55 | `FastBlur` radius 48, layered through `Desaturate` 0.55 | A slightly different blur character. The same clamp and opacity apply. |
| Tabular figures (8 sites) | `font.features: {"tnum": 1}` | not set | **Nothing.** Every site is IBM Plex Mono, whose digits are already fixed-width. The feature only guards a hypothetical proportional mono face. |
| Crate display width (`CrateHeading`) | Archivo `wdth` 120 via `font.variableAxes` | default width (100) | Crate headings, strip labels and hero titles are about 17% narrower. Weight still comes from `font.weight`. |
| QML type annotations | enforced and coerced (6.7+) | ignored by the interpreter | Nothing, as far as the tests and the self-test can see. This is a risk, not a feature (§11). |

Two other things degrade on some compat-tier distros, independently of the tier:
- **Debian 12's libmpv 0.35 ignores `target-colorspace-hint=auto`.** Its return value is already ignored, so HDR content is tone-mapped, as everywhere else today.
- **On Fedora, codecs follow the distro:** ffmpeg-free lacks HEVC and AAC decode until RPM Fusion's ffmpeg is installed (§10, README).

**Tier selection is one build decision, not a set of runtime checks.** CMake cache variable `STRMQT_QML_TIER`:
- `auto` (the default): `full` when the Qt found is ≥ 6.8, otherwise `compat`.
- `full`: requires Qt ≥ 6.7, the newest API it uses (`font.variableAxes`). Configure fails with that reason on older Qt.
- `compat`: allowed on any Qt. On a 6.11 dev machine it is how the fallbacks get built, tested and looked at without a container.

The resolved tier is:
- printed at configure time;
- written to `<build>/strmqt-qml-tier.txt`, which the Debian rules read (§7.2);
- compiled into the binary as `STRMQT_QML_TIER`, which the log reports once at startup.

---

## 4. The Qt floor

### 4.1 Build system

| Change | Where | Why |
|---|---|---|
| `cmake_minimum_required(VERSION 3.25)` | `CMakeLists.txt:1` | Debian 12 ships 3.25.1. Nothing in the tree needs 3.26–3.28. The Debian 12 configure run is the proof, not a reading. |
| `find_package(Qt6 6.4 REQUIRED …)` | `CMakeLists.txt:16` | The floor. |
| `qt_standard_project_setup(REQUIRES 6.8)` only when Qt ≥ 6.8, else plain `qt_standard_project_setup()` | `CMakeLists.txt:18` | `REQUIRES` and the QTP policies are 6.5+. Guarding at 6.8 keeps the full tier's policy set byte-identical to today. |
| `RESOURCE_PREFIX /qt/qml` on `qt_add_qml_module` | `src/CMakeLists.txt` | This is QTP0001's NEW default, so nothing changes at 6.8+. Before 6.5 the default is `/`. Pinning the prefix gives one resource layout on every Qt: `qrc:/qt/qml/StrmQt/…`. |
| On Qt < 6.5, `engine.addImportPath("qrc:/qt/qml")` and `engine.load(QUrl("qrc:/qt/qml/StrmQt/ui/Main.qml"))` in place of `loadFromModule` | `src/app/main.cpp:101` | `loadFromModule` is 6.5+, and 6.4's default import path does not contain `qrc:/qt/qml`. |

QTP0004 (a qmldir per subdirectory, 6.8) does not exist on 6.4. That is harmless: every QML file in a subdirectory already imports `StrmQt` explicitly.

### 4.2 C++ shims

Each is guarded by `QT_VERSION_CHECK` at the one site that needs it. No compatibility header: three sites do not justify one.

1. **`QQmlApplicationEngine::loadFromModule`** (6.5), `main.cpp`: as §4.1.
2. **Variadic `QMetaObject::invokeMethod` with `qReturnArg`** (6.5), `InputMap::trigger`. It is replaced by a helper that finds `invokeAction` on the handler's meta-object and **shapes the call from the signature it reports**:
   - `(QString, bool) → bool` for a C++ handler, or a typed QML function on a Qt that types it;
   - `(QVariant, QVariant) → QVariant` for an untyped or pre-typing QML function;
   - either mix.

   It uses `qReturnArg` on 6.5+ and `Q_RETURN_ARG`/`Q_ARG` below. The study's risk 3 (how 6.4 exposes a QML-declared `invokeAction(actionId: string, autoRepeat: bool): bool`) stops being a guess, because the helper no longer depends on the answer. `tst_input_map` gains an untyped-QML-handler case, so both shapes are tested on every Qt.
3. **`QtFuture::makeReadyValueFuture`** (6.6), `MusicRepository.cpp:26`: on older Qt, `QtFuture::makeReadyFuture(std::move(result))`. It exists since 6.0 and is deprecated only from 6.6, so the guard keeps it out of the full-tier build and its `-Werror`.

Unverified at design time: that `#include <QtTypes>` (`src/app/ImageLimits.h:5`) exists on 6.4. The Ubuntu 24.04 build answers it. The fallback is `<QtGlobal>`.

### 4.3 QML shims

**Mechanism: directory selection by CMake, with the type name taken from the file's basename.**

```
src/ui/shims/
  Shims.cmake            ← included from src/CMakeLists.txt
  full/                  ← Qt ≥ 6.8: MultiEffect, font.features, font.variableAxes
    StrmTint.qml  StrmShadow.qml  StrmMask.qml  StrmBackdropBlur.qml
    TabularText.qml  TabularMetrics.qml  CrateDisplayText.qml
  compat/                ← Qt 6.4–6.7: Qt5Compat.GraphicalEffects, and no font tweaks
    (the same seven file names)
```

`Shims.cmake` adds exactly one directory's files to the `StrmQt` module with `qt_target_qml_sources`:

```cmake
set(_strmqt_shims StrmTint StrmShadow StrmMask StrmBackdropBlur
                  TabularText TabularMetrics CrateDisplayText)
list(TRANSFORM _strmqt_shims PREPEND "ui/shims/${STRMQT_QML_TIER}/")
list(TRANSFORM _strmqt_shims APPEND ".qml")
qt_target_qml_sources(strmqt QML_FILES ${_strmqt_shims})
```

`qt_target_qml_sources` names the type after the file (`NAME_WE`, `Qt6QmlMacros.cmake`), so both tiers register `StrmTint`, `StrmShadow`, and so on. Callers write `StrmTint { … }` after the `import StrmQt` they already have. They never import `QtQuick.Effects` or `Qt5Compat.GraphicalEffects` themselves.
- There is no conditional import, no `Loader` by URL, and no runtime version check.
- The module never contains both variants, so neither qmlcachegen nor qmllint ever sees an import that the Qt at hand cannot resolve.

`QT_RESOURCE_ALIAS` (the study's suggestion) was considered and dropped:
- the type name already comes from the basename;
- an alias only makes the two tiers' resource paths identical;
- that buys nothing, since no code loads a shim by path, and it adds a 6.4 behaviour to verify.

**Contracts.** Each shim pair exposes the same properties. That is the whole API.

| Shim | Properties | Full | Compat | Sites |
|---|---|---|---|---|
| `StrmTint` | `source`, `color` | `MultiEffect { colorization: 1; colorizationColor: color }` | `ColorOverlay` | `StrmIcon` |
| `StrmShadow` | `source`, `elevation` (`{blur, y, opacity}`, a `Theme.elevationN`), `shadowColor` | `MultiEffect`, `autoPaddingEnabled` + `shadowEnabled` | `DropShadow`, `transparentBorder`, radius `blur × 32`, `color` × opacity | `StrmPanel` (`layer.effect`), `RecordStage` (sibling) |
| `StrmMask` | `source`, `maskSource` | `MultiEffect { maskEnabled; maskThresholdMin: 0.5; maskSpreadAtMin: 1.0 }` | `OpacityMask` | `CratePortrait`, `RecordStage` label |
| `StrmBackdropBlur` | `source` | `MultiEffect { blurEnabled; blur: 1; blurMax: 48; saturation: -0.55; autoPaddingEnabled: false }` | `FastBlur { radius: 48; layer.enabled: true; layer.effect: Desaturate { desaturation: 0.55 } }` | `HomePage` wash, `PersonPage` wash |
| `TabularText` | everything `Text` has | `Text { font.features: ({ "tnum": 1 }) }` | `Text {}` | 7 `Text` sites (§3) |
| `TabularMetrics` | everything `TextMetrics` has | `TextMetrics { font.features: ({ "tnum": 1 }) }` | `TextMetrics {}` | `MiniPlayer` `timeMetrics` |
| `CrateDisplayText` | everything `Text` has | `Text { font.variableAxes: Theme.crateDisplayAxes }` | `Text {}` | `CrateHeading` (its root) |

Every effect shim can serve as a `layer.effect` root, because both variants' root types declare `source`: MultiEffect does, and so do all the Qt5Compat effects.

**Tests that stage the module by copying files** (`tst_navigation_history`, `tst_card_component`, `tst_qml_accessibility`, `tst_focus_clip`, `tst_music_player_panel`, `tst_record_stage`, `tst_crate_controls`) stage the shims too, from the tier the build selected:
- one helper, `tests/mocks/QmlShimStaging.h`, `stageShims(modulePath)`, returns the qmldir lines;
- the directory comes from a compile definition `STRMQT_SHIMS_DIR`, set for every test by `strmqt_add_test`.

A pixel-measuring test that depends on the Archivo width axis (`tst_focus_clip`'s genre cell) keeps its full-tier assertion. It skips *only that assertion* in the compat tier, with a `QSKIP` reason naming this spec. It does not loosen the full-tier check.

### 4.4 What only a 6.4 run can answer

The study's §2.6 lists behaviour measured only on 6.11:
- focus-chain clearing;
- the XF86OK mapping;
- KeyNavigation targets held as `QPointer`;
- Tab skipping invisible items;
- the ShortcutOverride and event-point modifier facts;
- pixel-measured tiles.

None of it can be settled by reading. **The defence is the full ctest suite and the page self-test in an Ubuntu 24.04 container, from the first porting task onwards** (§8.1), plus two targeted checks:
- **KeyNavigation pointers:** read `QQuickKeyNavigationAttachedPrivate` in the 6.4.2 private headers (`qt6-declarative-private-dev` on noble). If its targets are raw pointers, `MusicAlbumPage`'s reverse wiring (`:596-605`) gets an explicit clear on the shelf's destruction.
- **Remote OK (XF86OK):** `tst_remote_ok_key` runs on 6.4. The real remote is checked by hand only if someone has one on a 24.04 box, and the README says so.

---

## 5. SDL3

SDL3 is the only gamepad path, and Ubuntu 24.04 and Debian 12 do not package it (study §1.4, Launchpad Q#820942).

**New option `STRMQT_BUNDLE_SDL3` (default OFF).**
- When ON, `cmake/StrmQtSdl3.cmake` fetches SDL3 with `FetchContent`, pinned by version and hash:
  - `https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-3.4.16.tar.gz`
  - SHA-256 `7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68` (computed 2026-09-27)
  - built static, with only the subsystems `SDL_INIT_GAMEPAD` needs, and linked as `SDL3::SDL3-static`.
- When OFF (the default), today's `pkg_check_modules(SDL3 … sdl3)` path is unchanged. A distro packager never gets a network fetch they did not ask for.
- `FETCHCONTENT_SOURCE_DIR_SDL3` supports an offline build from an unpacked tarball.

Who turns it on:
- the Ubuntu 24.04 and Debian 12 package builds;
- the Ubuntu 24.04 CI job;
- the AppImage, which previously relied on the host's `libSDL3`, a library most hosts do not have.

Everyone else links the distro's SDL3.

**Why 3.4.16:**
- it is the newest stable SDL3 release as of 2026-09-27;
- it is exactly what Fedora 43 ships;
- it is in the same ABI-stable 3.x series as Debian 13's 3.2.10 and Ubuntu 26.04's 3.4.2.

`GamepadManager` uses only SDL 3.0-era gamepad calls (`SDL_OpenGamepad`, the gamepad events, one hint). It compiles against any of them.

**Stated reason, recorded in the commit that adds it (AGENTS.md):**

> SDL3 is the only gamepad path, and Ubuntu 24.04 LTS and Debian 12 do not package it. It is bundled statically — pinned tarball and hash, gamepad subsystems only, about 2 MB — only in builds that set `STRMQT_BUNDLE_SDL3=ON`: those two distros' packages and the AppImage. Every other build links the distro's SDL3.

SDL3 is zlib-licensed. A bundled build installs its `LICENSE.txt` as `share/doc/strmqt/SDL3-LICENSE.txt`, and the Debian copyright file carries the stanza.

---

## 6. Secrets

### 6.1 The gap

`SecretsStore` talks only to `org.kde.kwalletd6`. Two groups never reach it and fall back to the vault file, a real downgrade for exactly this release's audience (study §1.3):
- Kubuntu 24.04 and Debian 12 KDE (Plasma 5.27, whose daemon is `kwalletd5`);
- every GNOME, Cinnamon, XFCE or other desktop, which runs gnome-keyring or KeePassXC.

### 6.2 Backends and probe order

There are three backends, all over QtDBus. There is no new library.

| # | Backend | Service / path / interface | Serves |
|---|---|---|---|
| 1 | KWallet 6 | `org.kde.kwalletd6` · `/modules/kwalletd6` · `org.kde.KWallet` | Plasma 6 (today's backend, unchanged on the wire) |
| 2 | KWallet 5 | `org.kde.kwalletd5` · `/modules/kwalletd5` · `org.kde.KWallet` | Plasma 5.27: Kubuntu 24.04, Debian 12 KDE |
| 3 | Secret Service | `org.freedesktop.secrets` · `/org/freedesktop/secrets` · `org.freedesktop.Secret.Service` | gnome-keyring, KeePassXC, and any other implementation |
| — | vault file | `<AppDataLocation>/secrets.ini`, 0600, warned | last resort, as today |

**KWallet comes first even where kwalletd6 also serves Secret Service.** Existing users' tokens live in KWallet's `StrmQt` folder. Asking the Secret Service first would not find them, and every Plasma 6 user would be signed out by the upgrade.

**Probing uses two calls to `org.freedesktop.DBus`, then a pure function:**
- the calls are `ListNames` (who is running) and `ListActivatableNames` (who could be started);
- the function is `chooseSecretBackends(owned, activatable, desktop)`, which returns the ordered candidate list.

The rules:
- **A KWallet generation is a candidate if its name is owned, or if it is activatable and `XDG_CURRENT_DESKTOP` contains `KDE`.** Without the desktop rule, a GNOME machine that has kwalletd installed by some KDE application would activate it. The user would get a "create a new wallet" dialog for a wallet they do not use.
- **The Secret Service is a candidate if its name is owned or activatable.** Its implementations are the desktop's own keyring, so activating one is what the user expects.
- **With no candidates, the vault is used**, with today's warning.

**Two kinds of failure, handled differently:**
- **Unavailable** moves to the next candidate. This covers:
  - a D-Bus error of `ServiceUnknown`, `NoReply`, `UnknownObject`, `UnknownMethod`, `NameHasNoOwner` or `Timeout`;
  - a Secret Service with no `default` collection (`ReadAlias` answers `/`).
- **Refused** goes straight to the vault, as today, and does not ask a second keyring. This covers:
  - KWallet's `open` answering −1;
  - a Secret Service unlock prompt the user dismissed.

  The user said no. Asking them again in a different dialog would be the bug.

Everything after "which backend" is unchanged, because the backends plug into `SecretsStore`'s existing state machine:
- the operation queue, identity retirement, vault migration and scrubbing;
- the write-failure demotion and the empty-read vault consult.

`StorageMode::Wallet` now means "a keyring". `SecretsStore` gains a `backendName` property ("KWallet", "KWallet 5", "Secret Service", "vault file") for the log and for Settings → About. The login-screen and toast strings stop naming KWallet and say "system keyring (KWallet or Secret Service)".

### 6.3 Secret Service protocol subset

All calls are asynchronous and go through the same transport seam (§6.5).

| Step | Call | Notes |
|---|---|---|
| prepare | `Service.OpenSession("plain", <"">)` → `(v, o session)` | The `plain` algorithm puts the secret on the session bus in the clear. That is the same exposure KWallet's `writePassword(…, QString)` already has; the bus is per-user. A DH-AES session is a follow-up (§11). |
| prepare | `Service.ReadAlias("default")` → `o collection` | `/` is *unavailable*. |
| open | `Properties.Get("org.freedesktop.Secret.Collection", "Locked")` on the collection | If unlocked: done. |
| open | `Service.Unlock([collection])` → `(ao unlocked, o prompt)` | A prompt of `/` means done. Otherwise `Prompt.Prompt("")`, then wait for `Prompt.Completed(b dismissed, v)`. Dismissed is *refused*. |
| write | `Collection.CreateItem({Label, Attributes}, (session, "", utf8 value, "text/plain; charset=utf8"), true)` → `(o item, o prompt)` | `replace = true` makes a rewrite idempotent. A non-`/` prompt runs as above. |
| read | `Service.SearchItems(attributes)` → `(ao unlocked, ao locked)` | If both are empty: success with an empty value, so `SecretsStore` consults the vault, as it does for KWallet. If only locked items match: `Unlock` them, then continue. Then `Item.GetSecret(session)` → `(oayays)`. |
| remove | `SearchItems`, then `Item.Delete()` on each match | No matches is success. |

**Per-account scoping** is carried by the key, exactly as for KWallet: `emby/<sha256(server+user)>/accessToken` (`Settings.cpp:123-135`). The item attributes are:
- `xdg:schema = ca.mikesdev.StrmQt.Secret`;
- `strmqt-key = <that key>`.

The label is `StrmQt access token`. The server address and user name therefore never appear in the keyring, only their hash.

### 6.4 Sandbox and packaging

The Flatpak gains two permissions, each with the manifest's usual justification comment:
- `--talk-name=org.kde.kwalletd5`;
- `--talk-name=org.freedesktop.secrets`.

The PKGBUILD's optdepends names `gnome-keyring` beside `kwallet`. The `.deb` Recommends and the `.rpm` Recommends list both keyrings as alternatives.

### 6.5 Testing: an injectable transport, no bus

- `secrets::DBusTransport` has two methods:
  - `call(message, context, done)`;
  - `connectSignal(service, path, interface, name, context, handler)`.
- `SessionBusTransport` implements it with `QDBusConnection::sessionBus()`.
- `tests/mocks/FakeDBusTransport.h` records every message and lets a test answer it, or raise a signal, later: the same "hold, then complete" style as `FakeSecretsStore`.

`tst_secret_backends` covers:
- the probe table;
- the KWallet 5 wire format;
- each Secret Service step, including the prompt path and the no-default-collection path;
- per-key attribute scoping;
- unavailable falling through to the next backend, versus refused going to the vault;
- a `SecretsStore` end to end over the fake transport.

`tst_secrets_store` and `FakeSecretsStore` are untouched. Its seam (`requestNetworkWallet` → `completeNetworkWallet` …) is still the state machine's, and the production implementations of those virtuals now delegate to the chosen backend.

---

## 7. Packaging

### 7.1 Why one build per distro release

- The binary imports one `Qt_6_PRIVATE_API` symbol (`QQmlPrivate::compositeMetaType`), and qmlcachegen's ahead-of-time code is valid only for the Qt version it was built against.
- Package names differ per release: the SVG plugin, QtQuick.Effects versus Qt5Compat, SDL3.

So a `.deb` or `.rpm` is built in, and only for, one distro release's own container. CPack is not used.

### 7.2 `.deb`: `packaging/debian/`

`dpkg-buildpackage` wants `debian/` at the source root. The build script copies `packaging/debian` there in a scratch copy of the tree. The repository root stays as it is: `packaging/arch`, `packaging/flatpak`, and now `packaging/debian` and `packaging/rpm`.

**The files:**
- `control`: `debhelper-compat (= 13)`, one binary package `strmqt`.
- `rules`: `dh $@ --buildsystem=cmake+ninja`.
- `changelog`: a single `0.7.5-1` entry. The build script rewrites it to `0.7.5-1~<suffix>`: `ubuntu24.04`, `ubuntu26.04`, `deb12` or `deb13`.
- `copyright`: DEP-5. It covers the app, the bundled fonts (OFL) and the SDL3 stanza.
- `source/format`: `3.0 (quilt)`.

**Build-Depends** are the union needed on all four releases. They use `|` alternatives only where a package does not exist on every release, and a `<!nocheck>` profile for test-only runtime modules.
- `debian/rules` passes `-DSTRMQT_BUNDLE_SDL3=ON` when `pkg-config --exists sdl3` fails, and only then.
- `qt6-tools-dev` is deliberately absent: the build does not use Qt Tools, and on bookworm it is a mismatched `6.4.2~rc1`.

**Runtime `Depends`:**

| Kind | Where it comes from |
|---|---|
| Libraries | `${shlibs:Depends}`. `dh_shlibdeps` handles them, including a static SDL3 correctly contributing nothing. |
| Qt ABI | `${strmqt:QtAbi}`, computed in `rules`. The package owning `libQt6Qml.so.6` is pinned to its *upstream* version range: `(>= 6.4.2), (<< 6.4.3~)`. The private-ABI coupling is honoured, and a distro security rebuild of the same upstream Qt still installs. |
| QML effect module | `${strmqt:QmlEffects}`. `rules` reads `strmqt-qml-tier.txt`: `qml6-module-qtquick-effects` for full, `qml6-module-qt5compat-graphicaleffects` for compat. |
| SVG plugin | `${strmqt:SvgPlugin}`. The package that owns `imageformats/libqsvg.so` in the build environment (`dpkg -S`). The build **fails** if none does: that is the trap from §2.5 of the study, caught at build time instead of at the first blank icon. |
| Fixed | `qml6-module-qtquick`, `qml6-module-qtquick-controls`, `qml6-module-qtquick-templates`, `qml6-module-qtquick-window`, `qml6-module-qtqml-workerscript`, `qml6-module-qtqml`, `qt6-qpa-plugins`, `qt6-wayland`, `vlc-plugin-base`, `vlc-plugin-video-output`, `hicolor-icon-theme` |

**Recommends:** `kwalletmanager | gnome-keyring | keepassxc`, and `kscreen`.

**Network.** The build fetches exactly one thing: the pinned, hash-checked SDL3 tarball, and only on noble and bookworm. These are release artifacts built in our own CI, not Debian-archive uploads.

`lintian` runs on each `.deb`. Its errors fail the job; its warnings are reviewed once and either fixed or overridden, each with a comment.

### 7.3 `.rpm`: `packaging/rpm/strmqt.spec`

- `BuildRequires` are the study's §4.2 list, verified on Fedora 43: `gcc-c++ cmake ninja-build pkgconf qt6-qtbase-devel qt6-qtbase-private-devel qt6-qtdeclarative-devel qt6-qtwebsockets-devel qt6-qtsvg-devel mpv-devel vlc-devel SDL3-devel openssl-devel`, plus the test runtime (`vlc-plugins-base vlc-plugin-ffmpeg ffmpeg-free qt6-qtsvg`).
- `Requires` covers what autoreq cannot see:
  - `qt6-qtdeclarative%{?_isa} = %{_qt6_version}` (the private ABI);
  - `qt6-qtsvg`, `qt6-qtwayland`, `vlc-plugins-base`, `vlc-plugin-ffmpeg`.
- `Recommends: (kwallet or gnome-keyring or keepassxc)`.
- `%check` runs ctest; `rpmlint` runs on the result.

**Fedora rebases Qt minor versions inside a stable release** (43: 6.9 → 6.10; 44: 6.10 → 6.11). With the exact `Requires`, dnf holds that Qt update back, or asks to remove StrmQt, until a rebuilt `.rpm` exists. The README says this plainly, and the answer for 0.7.5 is "install the matching `.rpm` from the next release, or use the Flatpak". A Copr project that rebuilds automatically is the follow-up, not part of this release.

### 7.4 AppImage

The AppImage moves from `archlinux:base-devel` to **`ubuntu:24.04`**, which gives a glibc 2.39 floor:

| Piece | Source | Pin |
|---|---|---|
| Qt | `aqtinstall`, not the distro. The AppImage bundles Qt by nature, and `Deploy.cmake`'s `INCLUDE_PLUGINS` needs Qt ≥ 6.10. | aqtinstall **3.3.0**, Qt **6.11.3** `linux_gcc_64`, `-m qtwebsockets` |
| libmpv + codec closure | Ubuntu 24.04's `libmpv-dev` **0.37.0** and ffmpeg **6.1.1**, walked by the existing BFS in `build-appimage.sh` | distro |
| SDL3 | `STRMQT_BUNDLE_SDL3=ON` (static) | 3.4.16 |
| appimagetool | unchanged | 1.9.1, sha256 `ed4ce84f…384eb0` |

`build-appimage.sh` changes where it assumed Arch:
- the multiarch lib directory (`/usr/lib/x86_64-linux-gnu`) in the mpv walk;
- the plugin and QML directories, read from the deployed `qt.conf` instead of the hard-coded `lib/qt6/plugins` that only a distro Qt produces;
- the portability banner.

The forbidden-soname assertion and the positive assertions stay as they are.

The glibc floor is checked, not assumed. The build fails if any bundled object imports a `GLIBC_` symbol newer than 2.39. That includes the aqt Qt, whose official Linux binaries are built on an older base.

Release CI runs the finished AppImage's self-test in **bare `debian:trixie` and `fedora:43` containers**. They carry only the host libraries that `packaging/appimage/README.md`'s "host must provide" table lists, which makes that table tested rather than written.

### 7.5 Unchanged

- **Flatpak:** only the two `--talk-name` lines of §6.4.
- **Arch package:** only `_tag` and one optdepends line.

---

## 8. CI and release

### 8.1 One set of scripts, run by CI and by `podman`

`scripts/ci/` is the single source of truth for "build StrmQt on distro X":

| Script | Job |
|---|---|
| `deps.sh TARGET` | Installs build and test dependencies for a target (`ubuntu-24.04`, `ubuntu-26.04`, `debian-12`, `debian-13`, `fedora-43`, `fedora-44`, `appimage`). It also prints the target's base image (`--image`) and extra CMake flags (`--cmake-args`). Once the packaging tasks land, the Debian and Fedora targets install from `packaging/debian/control` (`mk-build-deps`) and the spec (`dnf builddep`), so the package lists exist once. |
| `Containerfile` | `FROM $BASE`, then runs `deps.sh $TARGET`. Local images are tagged `localhost/strmqt-ci:<target>`. |
| `check.sh BUILD_DIR` | Configure (RelWithDebInfo, Ninja), build, `ctest`, the page self-test, and install sanity. It fails on a self-test log that mentions `Unsupported image format`, `is not installed` or `Cannot assign to non-existent property`: each of those is a runtime dependency or tier miss the exit code alone would hide. |
| `local.sh TARGET BUILD_DIR -- CMD…` | Builds the image if missing, then runs `CMD` with the checkout read-only at `/src`, the host's `BUILD_DIR` at `/build`, and `TMPDIR=/build/tmp` (AGENTS.md). |
| `package-deb.sh`, `package-rpm.sh`, `install-check.sh` | Build a package from a scratch copy of `/src`; then install it into a clean container of the same release and run the self-test there. |

**The container is the first thing built, so that every porting task is verified on 6.4 as it lands.** The Ubuntu 24.04 container check is the first task of the plan. The `ci.yml` job follows once it passes: a red job on `main` teaches nothing.

### 8.2 `ci.yml`

`ci.yml` keeps the Arch job. That job alone runs the qmllint baseline: the baseline is 6.11's warning set, and older qmllint versions report different warnings.

It gains two jobs:

| Job | Container | Proves |
|---|---|---|
| `floor` | `ubuntu:24.04` | Qt 6.4.2, compat tier, bundled SDL3: build, full ctest, self-test. **This is the job that enforces the floor.** |
| `debian-13` | `debian:trixie` | Qt 6.8.2, full tier on the oldest full-tier Qt: build, full ctest, self-test. |

Both run `scripts/ci/deps.sh` and `scripts/ci/check.sh`, the same commands a developer runs locally through `local.sh`.

### 8.3 `packages.yml` and `release.yml`

**New reusable workflow `packages.yml`.** It runs on `workflow_call` and `workflow_dispatch`, and contains:
- a `deb` matrix: ubuntu:24.04, ubuntu:26.04, debian:bookworm, debian:trixie;
- an `rpm` matrix: fedora:43, fedora:44;
- an `appimage` job;
- per matrix entry, an install-check job in a **fresh** container of the same image, which installs the artifact (`apt-get install ./…deb` / `dnf install ./…rpm`, so the dependency list is exercised) and runs the self-test;
- two AppImage portability jobs (bare trixie, bare fedora:43).

`workflow_dispatch` is what makes "build the packages from `main` before tagging" a one-liner.

**`release.yml`:**
- calls `packages.yml` in place of its own AppImage job;
- keeps the Arch and Flatpak jobs;
- extends **Version consistency** to `packaging/debian/changelog` (`0.7.5-1`) and the spec's `Version:` (`0.7.5`), beside `PKGBUILD _tag` and `project(VERSION)`;
- extends the publish glob with `*.deb` and `*.rpm`.

### 8.4 Release procedure (lesson from v0.7.0)

1. `chore(release): 0.7.5` bumps the version in:
   - `CMakeLists.txt`;
   - the PKGBUILD's `_tag`;
   - the AppStream `<release>`;
   - `debian/changelog`;
   - the spec's `Version:` and `%changelog`.
2. `main` is fast-forwarded to the branch and pushed (with the user's go-ahead: AGENTS.md).
3. **Wait for `ci.yml` to go green on that `main` commit, and run `packages.yml` on it and see it green, before any tag exists.**
4. Tag `v0.7.5` on that commit and push the tag. `release.yml` drafts the release with every artifact attached.
5. Publish the draft as a **pre-release** with written notes: `gh release edit v0.7.5 --draft=false --prerelease --notes-file …`. `release.yml` marks only `-rc`/`-beta` tags as pre-releases, so for this plain tag the flag is set by hand, deliberately.

---

## 9. Folded-in fixes

1. **`tst_navigation_history` leaks state on Qt 6.8.2.** Run as a whole, 11 of 29 functions fail with `Type StrmGrid unavailable / …/StrmQt/StrmGrid.qml: No such file` in a *previous* test's deleted `QTemporaryDir`. Each passes alone.
   - **The cause:** each test stages the `StrmQt` module into its own temporary directory. On 6.8 the type loader keeps module-URI state that points at the first staged directory. 6.11 does not.
   - **The fix:** stage the module **once per test run** (`initTestCase`, kept alive until `cleanupTestCase`), so every engine resolves `StrmQt` to the same, still-existing files. Per-test files (`Probe.qml`, `BoundedNavigationStack.qml`) stay per-test.
   - It is verified in the Debian 13 container, 29/29 in one run.
2. **MPRIS `CanGoPrevious` is false for the first item of a chapterless video**, yet `skipBack` restarts it once it is more than 5 s in. `PlayerController::canSkipBack` gates the restart branch on `m_isAudio`; `skipBack`/`playPrevious` restart any item. Align the property with the behaviour: drop `m_isAudio` from that branch. `tst_player_skip` gains the video case of `musicCanSkipBackOnceARestartWouldHappen`.
3. **The web remote's Next/Previous** (`WebRemoteServer::handleApiPlayback`, the `next`/`previous` actions) call `playNext`/`playPrevious`, so they ignore chapters. Every other surface (remote keys, pad, MPRIS) calls `skipForward`/`skipBack`, and so will these. The item action "Play next" (`m_actions->playNext(map)`, the up-next queue insert) is a different verb and stays. The ARCHITECTURE.md §3 "Skip and fast-forward" paragraph loses its "the web remote still…" exception. `tst_web_remote_server` gains a test with a real `PlayerController` over `FakePlayerBackend`.

---

## 10. Documentation

**README:**
- an **Install** section per format and distro (`.deb`, `.rpm`, AppImage, Flatpak, Arch), with the supported-distro table of §2;
- the compat-tier degradation table of §3, in user terms;
- the Fedora Qt-rebase note and the Fedora codec note;
- **Building** requirements: Qt 6.4+, CMake 3.25+, `STRMQT_QML_TIER`, `STRMQT_BUNDLE_SDL3`;
- "Where it stands" for 0.7.5.

**ARCHITECTURE.md:**
- a new section **"Supported Qt versions and compatibility shims"**: the tiers, how CMake selects the shim directory, the three C++ guards, why KWallet is probed first, and where the behaviour risks were checked;
- §3 "Skip and fast-forward" updated (§9.3);
- §7 the container checks and the suite count;
- §8 the three secret backends and the probe order;
- §9 the native packages and the new AppImage base;
- §10 the AppImage floor line.

**`packaging/appimage/README.md`:** the new base, the floor, and the tested host table.

---

## 11. Risks

1. **Behaviour on 6.4 that no audit can see** (study risk 1): focus and Tab rules, 6.4's qmlcachegen with `ComponentBehavior: Bound` and inline components, and pre-6.7 type annotations.
   - **Mitigation:** the full suite and the self-test run in the 24.04 container from the first task, and `ci.yml` enforces them. Plan Task 7 is a time-boxed sweep whose outcome is recorded.
   - What the tests cannot reach (feel, focus under a real compositor) is a manual pass on a 24.04 VM before release.
2. **Visual parity of the compat effects**, above all `StrmIcon`, which every glyph passes through. **Mitigation:** the compat tier builds on the 6.11 dev machine (`-DSTRMQT_QML_TIER=compat`), so the user compares both tiers side by side before the shim tasks close.
3. **Fedora Qt rebases** break the exact-version `Requires` until a rebuild exists (§7.3). This is accepted and documented. Copr is the follow-up.
4. **Secret Service implementations vary.** KeePassXC may have no `default` alias, and prompts differ.
   - **Mitigation:** a missing default collection is *unavailable* (the next candidate, then the vault), never an error loop.
   - A manual check against gnome-keyring (a GNOME VM or container session) is a release-gate item.
5. **AppImage host coverage.** Ubuntu's libmpv 0.37 closure differs from Arch's.
   - **Mitigation:** the forbidden-soname assertion, the glibc-symbol check, and the self-test in bare trixie and fedora:43.
6. **The `plain` Secret Service session** sends the token over the session bus unencrypted. This is no worse than KWallet's D-Bus API today, and it is recorded as a follow-up (a DH-AES session using the OpenSSL the app already links).
7. **GNOME idle inhibition** (study §1.3): `PowerInhibit` uses `org.freedesktop.ScreenSaver` / `PowerManagement`, which GNOME implements only partly. Checked manually; if it fails, a README note and a follow-up, not a 0.7.5 blocker.

---

## 12. Acceptance criteria

0.7.5 is done when all of the following hold:

1. On the dev host (Qt 6.11):
   - `cmake --preset dev && cmake --build --preset dev && ctest --preset dev` passes;
   - the qmllint baseline shows no new warnings;
   - the self-test constructs 15/15 pages;
   - all of that holds in both `STRMQT_QML_TIER=full` and `=compat` (the qmllint baseline in full only).
2. `scripts/ci/local.sh … check.sh` passes, with every test and the 15/15 self-test, in `ubuntu-24.04` (Qt 6.4.2), `debian-12` (6.4.2), `debian-13` (6.8.2), `ubuntu-26.04` (6.10.2) and `fedora-43` (6.10.3).
3. A `.deb` builds for each of the four Debian-family targets and an `.rpm` for fedora-43 and fedora-44; each installs into a fresh container of its release with only the package manager resolving dependencies, and the installed `strmqt` passes the self-test with no `Unsupported image format` or `is not installed` in its log.
4. The AppImage builds on ubuntu:24.04:
   - no bundled object needs `GLIBC_` newer than 2.39;
   - it passes the self-test in bare `debian:trixie` and `fedora:43`.
5. `tst_navigation_history` passes 29/29 in one run on Qt 6.8.2.
6. `tst_secret_backends` passes, and a manual check on a real session shows:
   - a token stored in KWallet 6 (Plasma 6) is still read after the upgrade;
   - a GNOME session stores the token in gnome-keyring (`secret-tool search strmqt-key …` finds it) and shows no vault warning.
7. MPRIS `CanGoPrevious` is true 5 s into the first item of a chapterless video. The web remote's Next steps by chapter in a chaptered film.
8. `ci.yml` has green `floor` and `debian-13` jobs on `main`, and `packages.yml` is green on the same commit, before `v0.7.5` is tagged.
9. The release carries 4 `.deb`s, 2 `.rpm`s, the Arch package, the AppImage, the Flatpak and the provenance file, and is published as a pre-release with notes.
10. README and ARCHITECTURE.md describe what was shipped: every distro in the README table has a matching artifact on the release.

---

## 13. Decisions this spec makes (beyond the study)

| Decision | Study said | Chosen, and why |
|---|---|---|
| Tier boundary | per-feature (6.5 / 6.6 / 6.7) | **One switch at 6.8.** It matches the user's two tiers. No target distro ships 6.5–6.7, so per-feature thresholds would create configurations nobody tests. |
| Shim selection | `QT_RESOURCE_ALIAS` | **Directory selection.** The type name comes from the basename, so an alias adds nothing, and it adds a 6.4 behaviour to verify. |
| `qReturnArg` replacement | `Q_ARG` macros, hoping the QML signature matches | **Signature-shaped call.** It removes study risk 3 instead of testing for it. |
| SDL3 on 24.04 / Debian 12 | leave optional (keyboard only) | **Bundle statically** (the user's decision). Default OFF, so distro builds never fetch. |
| AppImage mpv | build mpv, libplacebo and ffmpeg from source | **Ubuntu 24.04's libmpv 0.37.** The 24.04 `.deb` has to work with 0.37 anyway, and a from-source media stack is a second build system to maintain. Revisit if 0.37 shows a defect the Flatpak's 0.41 does not. |
| `debian/` location | `packaging/debian/` | Kept there, and copied into place by the build script. The root stays uncluttered, consistent with the other packaging directories. |
| Package workflow | jobs inside `release.yml` | **A reusable `packages.yml`**, so packages can be built from `main` before tagging (§8.4 step 3). |
| Secret probe | "also probe kwalletd5" + "add Secret Service" | **Both**, ordered KWallet 6 → 5 → Secret Service, with the desktop rule for activatable KWallet and the unavailable/refused split. |
