# Music Crate, Phase 5: Out of the Sleeve Player

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the audio half of the player with the Crate "Out of the sleeve" design:
- a record that slides out of its sleeve and spins while music plays;
- a side panel with Up next, Album and Lyrics tabs;
- a docked bar with a 72 px cover, a 2 px amber playhead and a record slice.

All of it is fed by a new `NowPlayingMusicController`, so QML never reshapes queue or ticket data. `NowPlayingPanel.qml` is deleted.

**Architecture:** see the index, `docs/superpowers/plans/2026-09-16-music-crate.md`. **Read its Global Constraints, Shared vocabulary and "Phase 2–5 contract" before any task.** Every task in this file implicitly includes them. Phases 1–4 have landed before this phase starts:
- `MusicController`, `MusicPage.qml`, `AlbumPage.qml` and `ArtistPage.qml` are gone.
- `src/ui/music/Music.cmake` exists.
- The Crate controls (`CrateKicker`, `CrateHeading`, …) are registered in module `StrmQt`.

**Spec:** `docs/superpowers/specs/2026-09-16-music-crate-design.md`:
- §7 (docked bar and full player), for the design;
- §8 (input), for keys and navigation;
- §9 V4 (lyrics) and §2 (tokens), for the lyrics branch and the visual tokens.

Mockup: `.superpowers/brainstorm/1687594-1789613149/content/player.html`, option B.

## Contract notes

These are additive to the index's contract or recorded deviations from it. Nothing listed in the index is renamed or re-typed, except the one QML property in item 3.

1. **`NowPlayingMusicController` needs the player and `ItemActions`.**
   - The contract constructor `(MusicRepository*, MusicPlayback*, QObject*)` is kept. The controller gains `void bind(PlayerController *player, ItemActions *actions)`, which Application calls right after construction.
   - The reason: `MusicPlayback` exposes neither object, and the controller reads playback state, the ticket and favourite changes.
2. **Added `NowPlayingMusicController` members**, beyond the contract:
   - `trackId`, `trackItem` (the queue's current map, for menus and playlists)
   - `sourceKicker` ("Playing from · Sunburned Almanac" / "Playing from Album X"). `contextLabel` is already translated as "from %1", so the display string is formed in C++.
   - `albumSummary` ("12 tracks · 48 min"), `timeText` ("1:52 / 4:57"), `lyricsTimed`, `currentLyricRow`
   - `Q_INVOKABLE toggleFavourite()`
   - a typed `TrackListModel *trackModel() const`, for Application's relay registration
   - `sourceLabel` follows the contract exactly: the queue's `sourceLabel`, else its `contextLabel`.
3. **`RecordStage.state` is named `recordState`.** `state` is `QQuickItem`'s own state-machine property; shadowing it breaks `State`/`Transition` use and qmllint flags it. The API is otherwise the contract's, with these additions:
   - `live` (the page is current)
   - `holdIn` (the shared sleeve is in the air)
   - `sleeveSize`, `sleeveRect(target)`, `sleeveRadius`
   - read-only `changing`, `turning`, `settling`, `slide`, `phase`, `shownCover`, for tests
4. **Lyrics source.** `MusicRepository` gains `lyrics(itemId, mediaSourceId, streamIndex)`, `EmbyMusicMapper` gains `parseLyrics`, and `MusicTypes.h` gains `LyricLine { timeMs, text }`.
   - The request is Emby's subtitle JSON for a lyrics sidecar attached to the track, `/Videos/{id}/{mediaSourceId}/Subtitles/{index}/Stream.js`.
   - The parser also accepts the `{"Lyrics":[{Start,Text}]}` shape.
   - Everything sits behind `caps::kLyricsAvailable`. When it is false, no request is ever made and the tab never shows.
   - Task 2 Step 1 swaps the path if V4 recorded a different one.
5. **New file `src/ui/music/MusicNowPlaying.qml`.** This is the audio full-player view: wash, stage zone, panel, back button, ⋯ menu and playlist picker. `PlayerPage` swaps `NowPlayingPanel` for it, which keeps the page's input routing untouched.
6. **Setting location.** The spec says *Settings → Interface → Animate record*, but `SettingsPage.qml` has no Interface section. The toggle goes in **Appearance**, next to Density.
7. **Music-only `MediaItemModel` roles are all still used**, so none are removed:
   - `artists`, `artistIds`, `albumArtist`, `album` and `albumId` are read by `PlayQueue`, `ItemActions`, MPRIS, the web remote, `TrackTable` and `NowPlayingInfo`.
   - `TrackListModel` must serve every media role.
   - Task 7 runs the check and records the result in its commit message.
8. **qmllint.** The new QML reads context properties (`PlayerCtl`, `NowPlayingMusicCtl`, `Prefs`, `Actions`, `MusicPlay`, `PlaylistCtl`, `Input`), and qmllint reports those as `[unqualified]`. Those cannot be qualified.
   - The Phase 5 gate (Task 9) accepts exactly those fingerprints for `src/ui/music/`, `MiniPlayer.qml` and `PlayerPage.qml`, plus the removal of `NowPlayingPanel.qml`'s fingerprints.
   - Every other new warning is fixed.
   - Between Tasks 5 and 8, `check-qmllint-baseline.sh` exits 1 ("qmllint warning set changed") while it lists those additions. That is expected. A per-task "lint" check means reading the diff it prints; it does not mean a zero exit. The exception is the "unresolvable QML types" message, which is always a failure.
   - This overlaps Phase 6 Task 3, which then sees no music `[unqualified]` additions to settle.
9. **The docked audio bar's playhead is pointer-seekable but not a keyboard stop.**
   - The 2 px line replaces the 16 px `StrmSlider` for audio only; video keeps the slider.
   - Keyboard seeking stays on the full player's scrubber and the `player.seek*` actions.

## Waves

| Wave | Tasks | Notes |
|---|---|---|
| 5a | 1 ‖ 2 ‖ 5 | Setting; lyrics parser and repository call; `RecordStage` and its QML test. They touch disjoint files, except that 5 adds a test target (three agents) |
| 5b | 3 ‖ 6 | Controller and its integration test (needs 2); `MusicPlayerPanel` (reads the controller only at runtime) (two agents) |
| 5c | 4 | Application and `main.cpp` wiring (needs 1 and 3) |
| 5d | 7 ‖ 8 | `MusicNowPlaying`, the `PlayerPage` swap and the `NowPlayingPanel` removal (needs 5, 6); the docked bar (needs 4) (two agents) |
| 5e | 9 | Phase gate and manual visual check |

Agents in one wave may all append to `src/CMakeLists.txt`, `tests/CMakeLists.txt` and `src/ui/music/Music.cmake`. They only add lines, so the orchestrator's merge at the gate is mechanical.

**Commits in parallel waves:** an agent in a multi-agent wave does **not** run its commit step. It reports the files it touched. At the gate the orchestrator:
1. builds and tests the integrated tree;
2. commits each task in task order with that task's message;
3. runs `rm -rf /tmp/w5*` in the same step as the last commit (AGENTS.md).

A single-agent wave (5c, 5e, or serial execution) commits its own step.

Build commands (parallel agents substitute `/tmp/w<wave><agent>` as the index explains):

```bash
cmake --preset dev && cmake --build --preset dev
ctest --preset dev -R <test_name> --output-on-failure
```

---

### Task 1: `Settings::animateRecord`

**Files:**
- Modify: `src/core/Settings.h`, `src/core/Settings.cpp`
- Modify: `src/ui/pages/SettingsPage.qml` (Appearance panel)
- Test: `tests/unit/tst_settings_prefs.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces: `Q_PROPERTY(bool animateRecord READ animateRecord WRITE setAnimateRecord NOTIFY animateRecordChanged)` on `Settings`, exposed to QML as `Prefs.animateRecord`. The default is `true`, and it is stored under `appearance/animateRecord`.

- [ ] **Step 1: Write the failing test**

In `tests/unit/tst_settings_prefs.cpp`, add the slot declaration after `void reducedMotionDefaultsAndPersists();`:

```cpp
    void animateRecordDefaultsOnAndPersists();
```

Add the definition after `SettingsPrefsTest::reducedMotionDefaultsAndPersists()`:

```cpp
void SettingsPrefsTest::animateRecordDefaultsOnAndPersists()
{
    QTemporaryDir dir;
    const QString ini = dir.filePath(QStringLiteral("record.ini"));
    Settings settings(ini);
    // Crate spec §7.2: the record spins by default; off gives a static record.
    QVERIFY(settings.animateRecord());
    QSignalSpy spy(&settings, &Settings::animateRecordChanged);
    settings.setAnimateRecord(false);
    settings.setAnimateRecord(false);
    QCOMPARE(spy.count(), 1);
    QVERIFY(!Settings(ini).animateRecord());
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build --preset dev --target tst_settings_prefs`
Expected: a compile error, `'class strmqt::Settings' has no member named 'animateRecord'`.

- [ ] **Step 3: Implement the setting**

In `src/core/Settings.h`, after the `reducedMotion` `Q_PROPERTY`:

```cpp
    Q_PROPERTY(bool animateRecord READ animateRecord WRITE setAnimateRecord NOTIFY
                   animateRecordChanged)
```

After `void setReducedMotion(bool reduced);`:

```cpp
    // The spinning record on the music player (Crate spec §7.2). Off draws the
    // record still and slid out; Theme.reducedMotion also stills it.
    bool animateRecord() const;
    void setAnimateRecord(bool animate);
```

After `void reducedMotionChanged();`:

```cpp
    void animateRecordChanged();
```

In `src/core/Settings.cpp`, after `kReducedMotionKey`:

```cpp
const auto kAnimateRecordKey = QStringLiteral("appearance/animateRecord");
```

After `Settings::setReducedMotion`:

```cpp
bool Settings::animateRecord() const
{
    return m_store.value(kAnimateRecordKey, true).toBool();
}

void Settings::setAnimateRecord(bool animate)
{
    if (animate == animateRecord())
        return;
    m_store.setValue(kAnimateRecordKey, animate);
    emit animateRecordChanged();
}
```

- [ ] **Step 4: Add the toggle**

In `src/ui/pages/SettingsPage.qml`, inside the Appearance `StrmPanel`, directly after the `SettingsSections.SettingRow` whose `label` is `qsTr("Density")`:

```qml
                    SettingsSections.SettingRow {
                        width: parent.width
                        label: qsTr("Animate record")
                        hint: page.prefsAvailable
                              ? qsTr("The record spins while music plays. Off shows it still, out of its sleeve.")
                              : page.unavailableHint

                        StrmSwitch {
                            enabled: page.prefsAvailable
                            checked: page.prefsAvailable && Prefs.animateRecord
                            // Owner-controlled, like every switch on this page.
                            onToggled: Prefs.animateRecord = !Prefs.animateRecord
                        }
                    }
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `cmake --build --preset dev --target tst_settings_prefs && ctest --preset dev -R tst_settings_prefs --output-on-failure`
Expected: PASS (9 tests).

- [ ] **Step 6: Commit**

```bash
git add src/core/Settings.h src/core/Settings.cpp src/ui/pages/SettingsPage.qml tests/unit/tst_settings_prefs.cpp
git commit -m "feat(settings): add the animate record preference

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---
### Task 2: Lyrics DTO, parser and `MusicRepository::lyrics`

**Files:**
- Modify: `src/server/dto/music/MusicTypes.h` (`LyricLine`)
- Modify: `src/server/emby/EmbyMusicMapper.h`, `src/server/emby/EmbyMusicMapper.cpp` (`parseLyrics`)
- Modify: `src/app/music/MusicRepository.h`, `src/app/music/MusicRepository.cpp` (`lyrics`)
- Test: `tests/unit/tst_music_mapper.cpp`, `tests/integration/tst_music_repository.cpp`

**Interfaces:**
- Consumes: `EmbyClient::getJson` (Phase 1 Task 1); the V4 row of `docs/superpowers/plans/2026-09-16-music-crate-verifications.md`.
- Produces:
  - `struct music::LyricLine { qint64 timeMs = -1; QString text; }`. `timeMs` is −1 when the lyrics are untimed.
  - `QList<music::LyricLine> emby::parseLyrics(const QJsonDocument &doc)`
  - `QFuture<Result<QList<LyricLine>>> MusicRepository::lyrics(const QString &itemId, const QString &mediaSourceId, int streamIndex)`. It is not cached; the controller asks once per track and source.

Rules:
- Accept `{"TrackEvents":[{"StartPositionTicks":n,"Text":s}]}` (Emby's subtitle JSON) and `{"Lyrics":[{"Start":n,"Text":s}]}`. Both times are ticks (10 000 per ms).
- Trim the text. Keep blank lines, since they are stanza breaks, but a document with no non-blank line returns an empty list.
- If no line has a time above 0, every line gets `timeMs = -1` (untimed).
- The repository does **not** check `caps::kLyricsAvailable`. The controller gates, so the call stays testable.

- [ ] **Step 1: Read V4**

Open `docs/superpowers/plans/2026-09-16-music-crate-verifications.md`, row V4.
- If it recorded a working path other than the subtitle stream (for example `/Audio/{id}/Lyrics`), change `kLyricsStreamPath` in Step 5 to that path. Drop the `mediaSourceId`/`streamIndex` arguments from its `arg()` calls, but keep the method's signature so Task 3 is unchanged.
- If the recorded shape has other key names, add them to `parseLyrics` next to the two shapes below, with a matching test row.
- If V4 is false, implement everything as written: the controller never calls it.

- [ ] **Step 2: Write the failing tests**

In `tests/unit/tst_music_mapper.cpp`, add the slot `void parsesLyricsInBothShapes();` and add `#include <QJsonDocument>` if it is not already included:

```cpp
void MusicMapperTest::parsesLyricsInBothShapes()
{
    const auto timed = emby::parseLyrics(QJsonDocument::fromJson(
        R"({"TrackEvents":[{"StartPositionTicks":0,"Text":"First light"},
                           {"StartPositionTicks":125000000,"Text":"  Second  "},
                           {"StartPositionTicks":180000000,"Text":""}]})"));
    QCOMPARE(timed.size(), 3);
    QCOMPARE(timed[0].timeMs, 0);
    QCOMPARE(timed[1].timeMs, 12500);
    QCOMPARE(timed[1].text, QStringLiteral("Second"));
    QCOMPARE(timed[2].text, QString());

    const auto plain = emby::parseLyrics(QJsonDocument::fromJson(
        R"({"Lyrics":[{"Text":"One"},{"Start":0,"Text":"Two"}]})"));
    QCOMPARE(plain.size(), 2);
    QCOMPARE(plain[0].timeMs, -1);
    QCOMPARE(plain[1].timeMs, -1);
    QCOMPARE(plain[1].text, QStringLiteral("Two"));

    QVERIFY(emby::parseLyrics(QJsonDocument::fromJson("[]")).isEmpty());
    QVERIFY(emby::parseLyrics(QJsonDocument::fromJson(R"({"Lyrics":[{"Text":"  "}]})")).isEmpty());
}
```

In `tests/integration/tst_music_repository.cpp`, add the slot `void lyricsReadTheSidecarStream();`:

```cpp
void MusicRepositoryTest::lyricsReadTheSidecarStream()
{
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Videos/l1/ms1/Subtitles/2/Stream.js"), 200,
                     R"({"TrackEvents":[{"StartPositionTicks":10000000,"Text":"Hello"}]})");
    auto future = m_repo->lyrics(QStringLiteral("l1"), QStringLiteral("ms1"), 2);
    QTRY_VERIFY(future.isFinished());
    const auto result = future.result();
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.value.size(), 1);
    QCOMPARE(result.value[0].timeMs, 1000);
    QCOMPARE(result.value[0].text, QStringLiteral("Hello"));

    auto missing = m_repo->lyrics(QStringLiteral("l2"), QStringLiteral("ms1"), 2);
    QTRY_VERIFY(missing.isFinished());
    QVERIFY(!missing.result().ok());
}
```

- [ ] **Step 3: Run the tests to verify they fail**

Run: `cmake --build --preset dev --target tst_music_mapper tst_music_repository`
Expected: compile errors, `'parseLyrics' is not a member of 'strmqt::emby'` and `'class strmqt::music::MusicRepository' has no member named 'lyrics'`.

- [ ] **Step 4: Add the DTO and the parser**

In `src/server/dto/music/MusicTypes.h`, after `struct Track { … };`:

```cpp
// One line of lyrics (Crate spec §7.2). timeMs is -1 for untimed lyrics.
struct LyricLine
{
    qint64 timeMs = -1;
    QString text;
};
```

In `src/server/emby/EmbyMusicMapper.h`, add `#include <QJsonDocument>` and, before the closing namespace:

```cpp
// A lyrics sidecar as Emby serves it: its subtitle JSON ({"TrackEvents":[…]})
// or the {"Lyrics":[{Start,Text}]} shape. Untimed when no line has a time.
QList<music::LyricLine> parseLyrics(const QJsonDocument &doc);
```

In `src/server/emby/EmbyMusicMapper.cpp`, before the closing namespace:

```cpp
QList<music::LyricLine> parseLyrics(const QJsonDocument &doc)
{
    constexpr qint64 kTicksPerMs = 10'000;
    const QJsonObject root = doc.object();
    QJsonArray entries = root.value(QStringLiteral("TrackEvents")).toArray();
    QString timeKey = QStringLiteral("StartPositionTicks");
    if (entries.isEmpty()) {
        entries = root.value(QStringLiteral("Lyrics")).toArray();
        timeKey = QStringLiteral("Start");
    }

    QList<music::LyricLine> lines;
    bool timed = false;
    bool anyText = false;
    for (const QJsonValue &value : std::as_const(entries)) {
        const QJsonObject entry = value.toObject();
        music::LyricLine line;
        const QJsonValue time = entry.value(timeKey);
        if (time.isDouble())
            line.timeMs = static_cast<qint64>(time.toDouble()) / kTicksPerMs;
        line.text = entry.value(QStringLiteral("Text")).toString().trimmed();
        timed = timed || line.timeMs > 0;
        anyText = anyText || !line.text.isEmpty();
        lines.append(line);
    }
    if (!anyText)
        return {};
    if (!timed) {
        for (music::LyricLine &line : lines)
            line.timeMs = -1;
    }
    return lines;
}
```

If `EmbyMusicMapper.cpp` does not already include them, add `#include <QJsonArray>` and `#include <QJsonObject>`.

- [ ] **Step 5: Add the repository call**

In `src/app/music/MusicRepository.h`, after `artistProfile(…)`:

```cpp
    // The lyrics sidecar attached to a track (Crate spec §9 V4). Not cached:
    // NowPlayingMusicController asks once per track and source.
    QFuture<Result<QList<LyricLine>>> lyrics(const QString &itemId, const QString &mediaSourceId,
                                             int streamIndex);
```

In `src/app/music/MusicRepository.cpp`, in the anonymous namespace after `kAlbum`:

```cpp
// V4 (verifications file). Emby serves an external .lrc/.txt next to a track as
// a subtitle stream; its JSON rendition carries the lines and their ticks.
const QString kLyricsStreamPath = QStringLiteral("/Videos/%1/%2/Subtitles/%3/Stream.js");
```

After `MusicRepository::artistProfile`:

```cpp
QFuture<Result<QList<LyricLine>>> MusicRepository::lyrics(const QString &itemId,
                                                         const QString &mediaSourceId,
                                                         int streamIndex)
{
    const QString path = kLyricsStreamPath.arg(itemId, mediaSourceId).arg(streamIndex);
    return m_client->getJson(path, {}).then(this, [](Result<QJsonDocument> result) {
        if (!result.ok())
            return Result<QList<LyricLine>>::failure(result.error);
        return Result<QList<LyricLine>>::success(emby::parseLyrics(result.value));
    });
}
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `cmake --build --preset dev --target tst_music_mapper tst_music_repository && ctest --preset dev -R "tst_music_mapper|tst_music_repository" --output-on-failure`
Expected: PASS for both. `parsesLyricsInBothShapes` and `lyricsReadTheSidecarStream` are listed.

- [ ] **Step 7: Commit**

```bash
git add src/server/dto/music/MusicTypes.h src/server/emby/EmbyMusicMapper.h src/server/emby/EmbyMusicMapper.cpp \
        src/app/music/MusicRepository.h src/app/music/MusicRepository.cpp \
        tests/unit/tst_music_mapper.cpp tests/integration/tst_music_repository.cpp
git commit -m "feat(music): read lyrics sidecars into lyric lines

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: `NowPlayingMusicController`

**Files:**
- Create: `src/app/controllers/music/NowPlayingMusicController.h`, `src/app/controllers/music/NowPlayingMusicController.cpp`
- Modify: `src/CMakeLists.txt` (`strmqt_app`)
- Test: `tests/integration/tst_now_playing_music.cpp`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes:
  - `MusicRepository::albumTracks`, `MusicRepository::lyrics` (Task 2), `MusicPlayback::playTracks` / `toMaps` (Phase 1)
  - `TrackListModel` (`setItems`, `items`, `clear`, `indexOfNavigationIdentity`)
  - `MusicFormat` (`formatDuration`, `formatRuntime`, `formatTrackCount`, `coverUrl`)
  - `emby::deriveAudioFormat`, `emby::caps::kLyricsAvailable`
  - `PlayerController` (`queue`, `active`, `busy`, `buffering`, `paused`, `positionMs`, `durationMs`, `streamMethod`, `currentSource`, `audioStreams`, `subtitleStreams`, and their signals)
  - `PlayQueue` (`current`, `currentItem`, `sourceLabel`, `contextLabel`, `currentChanged`, `currentItemChanged`, `queueChanged`, `sourceLabelChanged`)
  - `ItemActions::setFavorite` / `favoriteChanged`
- Produces: `class strmqt::music::NowPlayingMusicController : QObject`, with every contract member plus the additions in Contract notes 1–2, and `void setLyricsEnabledForTests(bool)`.

Rules:
- **The current item** is `queue->current()`. `active` is true only when that item's type is `Audio`.
  - `artist` is the item's `artists` joined with ", ", with `artistId = artistIds[0]`; with no artists it falls back to `albumArtist`.
  - `coverUrl` is `coverUrl(item.coverSource())`, falling back to the queue map's `posterUrl`.
- **`albumChanging()`** is emitted only when the *track* moved, the previous and new `albumId` are both non-empty, and they differ.
  - It fires **after** the members change and **before** `trackChanged()`. The QML handler then reads the new `coverUrl` and starts the sequence before any binding sees the new cover.
- **`recordState`:**
  - `"stopped"` unless `active && player->active()`
  - otherwise `"buffering"` while `busy || buffering`
  - then `"paused"` while paused
  - else `"playing"`
- **`readout`:** `deriveAudioFormat(...)` on the ticket's first audio stream, then ` · `, then `DIRECT PLAY` / `DIRECT STREAM` / `TRANSCODE`. Empty parts are dropped.
- **`albumTracks`:**
  - Reloaded only when `albumId` differs from the loaded album; the repository's 10-minute cache makes that free after `playAlbum`.
  - A superseded reply is dropped by generation.
  - `currentAlbumRow` is `indexOfNavigationIdentity("i:" + trackId)`.
- **Lyrics:**
  - Only when lyrics are enabled (`caps::kLyricsAvailable`, overridable in tests).
  - Requested on `sourceIndexChanged`, which the player forces for every new ticket, from the first subtitle stream whose codec is `lrc`, `txt` or `text`.
  - Keyed by `trackId|sourceId|index`, and cleared when the track moves.
  - `currentLyricRow` is the last line whose `timeMs ≤ positionMs` (−1 before the first line or when untimed).

- [ ] **Step 1: Write the failing test**

`tests/integration/tst_now_playing_music.cpp`:

```cpp
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>

#include "FakePlayerBackend.h"
#include "MockEmbyServer.h"
#include "app/ItemActions.h"
#include "app/PlayQueue.h"
#include "app/controllers/PlayerController.h"
#include "app/controllers/music/NowPlayingMusicController.h"
#include "app/music/MusicPlayback.h"
#include "app/music/MusicRepository.h"
#include "app/music/models/TrackListModel.h"
#include "core/Settings.h"
#include "server/emby/EmbyClient.h"

using namespace strmqt;
using namespace strmqt::music;

namespace {

const auto kUserId = QStringLiteral("a1b2c3d4e5f60718293a4b5c6d7e8f90");
const auto kToken = QStringLiteral("not-a-real-token-fixture-only");

QString fixturePath(const QString &name)
{
    return QStringLiteral(STRMQT_FIXTURES_DIR "/") + name;
}

QString itemsPath() { return QStringLiteral("/Users/%1/Items").arg(kUserId); }

QJsonObject trackJson(const QString &id, const QString &albumId, int number)
{
    return {{"Id", id}, {"Name", "Track " + id}, {"Type", "Audio"}, {"AlbumId", albumId},
            {"Album", "Album " + albumId}, {"AlbumPrimaryImageTag", "atag"},
            {"AlbumArtist", "Hollow Coves"},
            {"AlbumArtists", QJsonArray{QJsonObject{{"Id", "ar1"}, {"Name", "Hollow Coves"}}}},
            {"ArtistItems", QJsonArray{QJsonObject{{"Id", "ar1"}, {"Name", "Hollow Coves"}}}},
            {"Artists", QJsonArray{"Hollow Coves"}},
            {"ParentIndexNumber", 1}, {"IndexNumber", number},
            {"RunTimeTicks", 4 * 60 * 10'000'000LL}};
}

QByteArray page(const QJsonArray &items)
{
    return QJsonDocument(QJsonObject{{"Items", items}, {"TotalRecordCount", items.size()}})
        .toJson(QJsonDocument::Compact);
}

Track track(const QString &id, const QString &albumId, int number)
{
    Track t;
    t.id = id;
    t.title = QStringLiteral("Track ") + id;
    t.displayTitle = t.title;
    t.albumId = albumId;
    t.albumTitle = QStringLiteral("Album ") + albumId;
    t.discNumber = 1;
    t.trackNumber = number;
    t.runtimeMs = 4 * 60 * 1000;
    t.artists = {{QStringLiteral("ar1"), QStringLiteral("Hollow Coves")}};
    t.albumArtists = t.artists;
    t.coverRef = {albumId, QStringLiteral("Primary"), QStringLiteral("atag")};
    return t;
}

// The audio ticket, optionally with an .lrc sidecar as a second, external
// subtitle stream (Crate spec §9 V4).
QByteArray playbackInfo(bool withLyrics)
{
    QFile file(fixturePath(QStringLiteral("playback_info_audio.json")));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (withLyrics) {
        QJsonArray sources = root.value("MediaSources").toArray();
        QJsonObject source = sources.at(0).toObject();
        QJsonArray streams = source.value("MediaStreams").toArray();
        streams.append(QJsonObject{{"Codec", "lrc"}, {"Type", "Subtitle"}, {"Index", 1},
                                   {"IsExternal", true}, {"Language", "eng"}});
        source.insert("MediaStreams", streams);
        sources.replace(0, source);
        root.insert("MediaSources", sources);
    }
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

} // namespace

class NowPlayingMusicTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void followsTheCurrentTrack();
    void sourceLabelFallsBackToContext();
    void recordStateFollowsThePlayer();
    void albumChangingOnlyWhenTheAlbumChanges();
    void albumTracksComeFromTheRepositoryCache();
    void readoutFromTicketAndMethod();
    void timeTextFormatsBothClocks();
    void favouriteToggles();
    void lyricsBehindCaps();

private:
    PlayQueue *queue() const { return m_player->queue(); }
    int requestsTo(const QString &method, const QString &path, const QString &queryPart = QString()) const;

    MockEmbyServer *m_mock = nullptr;
    emby::EmbyClient *m_client = nullptr;
    FakePlayerBackend *m_backend = nullptr;
    QTemporaryDir *m_dir = nullptr;
    Settings *m_settings = nullptr;
    PlayerController *m_player = nullptr;
    ItemActions *m_actions = nullptr;
    MusicRepository *m_repo = nullptr;
    MusicPlayback *m_playback = nullptr;
    NowPlayingMusicController *m_controller = nullptr;
};

using Q = QList<QPair<QString, QString>>;

void NowPlayingMusicTest::init()
{
    m_mock = new MockEmbyServer(this);
    QVERIFY(m_mock->start());
    const QStringList ids{"a1", "a2", "a3", "b1", "b2"};
    for (const QString &id : ids)
        m_mock->addRoute("POST", QStringLiteral("/Items/%1/PlaybackInfo").arg(id), 200, playbackInfo(false));
    m_mock->addRoute("POST", "/Items/l1/PlaybackInfo", 200, playbackInfo(true));
    m_mock->addRoute("GET", "/Videos/l1/ms301004/Subtitles/1/Stream.js", 200,
                     R"({"TrackEvents":[{"StartPositionTicks":10000000,"Text":"Salt on the window"},
                                        {"StartPositionTicks":50000000,"Text":"Tide in the hall"}]})");
    m_mock->addRoute("POST", "/Sessions/Playing", 204, {});
    m_mock->addRoute("POST", "/Sessions/Playing/Progress", 204, {});
    m_mock->addRoute("POST", "/Sessions/Playing/Stopped", 204, {});
    m_mock->addRoute("POST", QStringLiteral("/Users/%1/FavoriteItems/a1").arg(kUserId), 200,
                     R"({"IsFavorite":true})");
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"ParentId", "alA"}}, 200,
                          page({trackJson("a1", "alA", 1), trackJson("a2", "alA", 2), trackJson("a3", "alA", 3)}));
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"ParentId", "alB"}}, 200,
                          page({trackJson("b1", "alB", 1), trackJson("b2", "alB", 2)}));
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"ParentId", "alL"}}, 200, page({trackJson("l1", "alL", 1)}));

    m_client = new emby::EmbyClient(this);
    m_client->setBaseUrl(m_mock->baseUrl());
    m_client->setDeviceId(QStringLiteral("test-device"));
    m_client->setSession(kToken, kUserId);

    m_backend = new FakePlayerBackend(this);
    m_dir = new QTemporaryDir;
    QVERIFY(m_dir->isValid());
    m_settings = new Settings(m_dir->filePath(QStringLiteral("settings.ini")), this);
    m_player = new PlayerController(m_client, m_backend, m_settings, this);
    m_actions = new ItemActions(m_client, m_player, this);
    m_repo = new MusicRepository(m_client, this);
    m_playback = new MusicPlayback(m_repo, m_actions, this);
    m_controller = new NowPlayingMusicController(m_repo, m_playback, this);
    m_controller->bind(m_player, m_actions);
    m_controller->setLyricsEnabledForTests(false);
}

void NowPlayingMusicTest::cleanup()
{
    delete m_controller;
    delete m_playback;
    delete m_repo;
    delete m_actions;
    delete m_player;
    delete m_settings;
    delete m_dir;
    delete m_backend;
    delete m_client;
    delete m_mock;
    m_controller = nullptr;
    m_playback = nullptr;
    m_repo = nullptr;
    m_actions = nullptr;
    m_player = nullptr;
    m_settings = nullptr;
    m_dir = nullptr;
    m_backend = nullptr;
    m_client = nullptr;
    m_mock = nullptr;
}

int NowPlayingMusicTest::requestsTo(const QString &method, const QString &path, const QString &queryPart) const
{
    int count = 0;
    for (const auto &request : m_mock->requests()) {
        if (request.method == method && request.path == path
            && (queryPart.isEmpty() || request.query.contains(queryPart)))
            ++count;
    }
    return count;
}

void NowPlayingMusicTest::followsTheCurrentTrack()
{
    QVERIFY(!m_controller->active());
    QCOMPARE(m_controller->recordState(), QStringLiteral("stopped"));

    m_playback->playAlbum(QStringLiteral("alA"), QStringLiteral("Sunburned Almanac"), 1);
    QTRY_COMPARE(m_controller->trackId(), QStringLiteral("a2"));
    QVERIFY(m_controller->active());
    QCOMPARE(m_controller->title(), QStringLiteral("Track a2"));
    QCOMPARE(m_controller->album(), QStringLiteral("Album alA"));
    QCOMPARE(m_controller->albumId(), QStringLiteral("alA"));
    QCOMPARE(m_controller->artist(), QStringLiteral("Hollow Coves"));
    QCOMPARE(m_controller->artistId(), QStringLiteral("ar1"));
    QVERIFY2(m_controller->coverUrl().contains(QStringLiteral("alA")), qPrintable(m_controller->coverUrl()));
    QCOMPARE(m_controller->trackItem().value(QStringLiteral("itemId")).toString(), QStringLiteral("a2"));
    QCOMPARE(m_controller->sourceLabel(), QStringLiteral("Sunburned Almanac"));
    QCOMPARE(m_controller->sourceKicker(), QStringLiteral("Playing from · Sunburned Almanac"));

    QSignalSpy changed(m_controller, &NowPlayingMusicController::trackChanged);
    m_player->playNext();
    QTRY_COMPARE(m_controller->trackId(), QStringLiteral("a3"));
    QVERIFY(changed.count() >= 1);
    QCOMPARE(m_controller->title(), QStringLiteral("Track a3"));
}

void NowPlayingMusicTest::sourceLabelFallsBackToContext()
{
    m_player->playQueue(MusicPlayback::toMaps({track("b1", "alB", 1), track("b2", "alB", 2)}), 0);
    QTRY_COMPARE(m_controller->trackId(), QStringLiteral("b1"));
    QVERIFY(queue()->sourceLabel().isEmpty());
    QCOMPARE(m_controller->sourceLabel(), queue()->contextLabel());
    QCOMPARE(m_controller->sourceLabel(), QStringLiteral("from Album alB"));
    QCOMPARE(m_controller->sourceKicker(), QStringLiteral("Playing from Album alB"));
}

void NowPlayingMusicTest::recordStateFollowsThePlayer()
{
    QSignalSpy states(m_controller, &NowPlayingMusicController::recordStateChanged);
    m_player->playQueue(MusicPlayback::toMaps({track("a1", "alA", 1)}), 0);
    QTRY_COMPARE(m_controller->recordState(), QStringLiteral("buffering"));
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);

    m_backend->simulateState(PlayerBackend::State::Playing);
    QTRY_COMPARE(m_controller->recordState(), QStringLiteral("playing"));

    m_player->setPaused(true);
    QTRY_COMPARE(m_controller->recordState(), QStringLiteral("paused"));

    m_backend->simulateBuffering(true);
    QTRY_COMPARE(m_controller->recordState(), QStringLiteral("buffering"));
    m_backend->simulateBuffering(false);
    QTRY_COMPARE(m_controller->recordState(), QStringLiteral("paused"));

    m_player->stop();
    QTRY_COMPARE(m_controller->recordState(), QStringLiteral("stopped"));
    QVERIFY(states.count() >= 5);
}

void NowPlayingMusicTest::albumChangingOnlyWhenTheAlbumChanges()
{
    QSignalSpy changing(m_controller, &NowPlayingMusicController::albumChanging);
    m_player->playQueue(MusicPlayback::toMaps({track("a1", "alA", 1), track("a2", "alA", 2),
                                               track("b1", "alB", 1)}), 0);
    QTRY_COMPARE(m_controller->trackId(), QStringLiteral("a1"));
    QCOMPARE(changing.count(), 0); // nothing was playing: no album to leave

    m_player->playNext();
    QTRY_COMPARE(m_controller->trackId(), QStringLiteral("a2"));
    QCOMPARE(changing.count(), 0); // same album keeps spinning

    // The cover is already the new album's when the signal fires.
    QString coverAtSignal;
    connect(m_controller, &NowPlayingMusicController::albumChanging, this,
            [&] { coverAtSignal = m_controller->coverUrl(); });
    m_player->playNext();
    QTRY_COMPARE(m_controller->trackId(), QStringLiteral("b1"));
    QCOMPARE(changing.count(), 1);
    QVERIFY2(coverAtSignal.contains(QStringLiteral("alB")), qPrintable(coverAtSignal));

    m_player->playPrevious();
    QTRY_COMPARE(m_controller->trackId(), QStringLiteral("a2"));
    QCOMPARE(changing.count(), 2);
}

void NowPlayingMusicTest::albumTracksComeFromTheRepositoryCache()
{
    m_playback->playAlbum(QStringLiteral("alA"), QStringLiteral("Sunburned Almanac"), 2);
    QTRY_COMPARE(m_controller->trackId(), QStringLiteral("a3"));
    QTRY_COMPARE(m_controller->trackModel()->rowCount(), 3);
    QTRY_COMPARE(m_controller->currentAlbumRow(), 2);
    QCOMPARE(m_controller->albumSummary(), QStringLiteral("3 tracks · 12 min"));
    QCOMPARE(m_controller->albumTracks(), static_cast<QObject *>(m_controller->trackModel()));
    // playAlbum filled the cache; the controller did not ask again.
    QCOMPARE(requestsTo(QStringLiteral("GET"), itemsPath(), QStringLiteral("ParentId=alA")), 1);

    m_controller->playAlbumFrom(0);
    QTRY_COMPARE(m_controller->trackId(), QStringLiteral("a1"));
    QTRY_COMPARE(m_controller->currentAlbumRow(), 0);
    QCOMPARE(queue()->sourceLabel(), QStringLiteral("Album alA"));
    QCOMPARE(queue()->rowCount(), 3);

    m_controller->playAlbumFrom(7); // out of range: ignored
    QCOMPARE(m_controller->trackId(), QStringLiteral("a1"));
}

void NowPlayingMusicTest::readoutFromTicketAndMethod()
{
    QCOMPARE(m_controller->readout(), QString());
    m_player->playQueue(MusicPlayback::toMaps({track("a1", "alA", 1)}), 0);
    QTRY_COMPARE(m_controller->readout(), QStringLiteral("FLAC 24/96 · DIRECT PLAY"));
}

void NowPlayingMusicTest::timeTextFormatsBothClocks()
{
    m_player->playQueue(MusicPlayback::toMaps({track("a1", "alA", 1)}), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateState(PlayerBackend::State::Playing);
    QTRY_COMPARE(m_controller->timeText(), QStringLiteral("0:00 / --:--"));
    m_backend->simulateDuration(297'000);
    m_backend->simulatePosition(112'000);
    QTRY_COMPARE(m_controller->timeText(), QStringLiteral("1:52 / 4:57"));
}

void NowPlayingMusicTest::favouriteToggles()
{
    m_player->playQueue(MusicPlayback::toMaps({track("a1", "alA", 1)}), 0);
    QTRY_COMPARE(m_controller->trackId(), QStringLiteral("a1"));
    QVERIFY(!m_controller->favourite());

    QSignalSpy spy(m_controller, &NowPlayingMusicController::favouriteChanged);
    m_controller->toggleFavourite();
    QVERIFY(m_controller->favourite()); // optimistic
    QVERIFY(spy.count() >= 1);
    QTRY_COMPARE(requestsTo(QStringLiteral("POST"), QStringLiteral("/Users/%1/FavoriteItems/a1").arg(kUserId)), 1);
}

void NowPlayingMusicTest::lyricsBehindCaps()
{
    const QString lyricsPath = QStringLiteral("/Videos/l1/ms301004/Subtitles/1/Stream.js");

    // Caps say no: the ticket has a sidecar, but nothing is asked for.
    m_player->playQueue(MusicPlayback::toMaps({track("l1", "alL", 1)}), 0);
    QTRY_COMPARE(m_controller->readout(), QStringLiteral("FLAC 24/96 · DIRECT PLAY"));
    QTest::qWait(50);
    QCOMPARE(requestsTo(QStringLiteral("GET"), lyricsPath), 0);
    QVERIFY(!m_controller->lyricsAvailable());
    QVERIFY(m_controller->lyrics().isEmpty());

    // Caps say yes: the current ticket's sidecar is read.
    QSignalSpy lyricsSpy(m_controller, &NowPlayingMusicController::lyricsChanged);
    m_controller->setLyricsEnabledForTests(true);
    QTRY_VERIFY(m_controller->lyricsAvailable());
    QCOMPARE(requestsTo(QStringLiteral("GET"), lyricsPath), 1);
    QVERIFY(m_controller->lyricsTimed());
    const QVariantList lines = m_controller->lyrics();
    QCOMPARE(lines.size(), 2);
    QCOMPARE(lines.at(1).toMap().value(QStringLiteral("timeMs")).toLongLong(), 5000);
    QCOMPARE(lines.at(1).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("Tide in the hall"));
    QCOMPARE(m_controller->currentLyricRow(), -1);

    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulatePosition(6000);
    QTRY_COMPARE(m_controller->currentLyricRow(), 1);
    m_backend->simulatePosition(1200);
    QTRY_COMPARE(m_controller->currentLyricRow(), 0);

    // A track without a sidecar clears them.
    m_player->playQueue(MusicPlayback::toMaps({track("a1", "alA", 1)}), 0);
    QTRY_COMPARE(m_controller->trackId(), QStringLiteral("a1"));
    QVERIFY(!m_controller->lyricsAvailable());
    QCOMPARE(m_controller->currentLyricRow(), -1);
}

QTEST_GUILESS_MAIN(NowPlayingMusicTest)
#include "tst_now_playing_music.moc"
```

Add to `tests/CMakeLists.txt`, after `tst_music_playback`:

```cmake
strmqt_add_test(tst_now_playing_music
    integration/tst_now_playing_music.cpp
    mocks/MockEmbyServer.h mocks/MockEmbyServer.cpp
    mocks/FakePlayerBackend.h
)
target_include_directories(tst_now_playing_music PRIVATE mocks)
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --preset dev && cmake --build --preset dev --target tst_now_playing_music`
Expected: a compile error, `app/controllers/music/NowPlayingMusicController.h: No such file or directory`.

- [ ] **Step 3: Write the header**

`src/app/controllers/music/NowPlayingMusicController.h`:

```cpp
#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "server/dto/music/MusicTypes.h"

namespace strmqt {
class ItemActions;
class PlayerController;
} // namespace strmqt

namespace strmqt::music {

class MusicPlayback;
class MusicRepository;
class TrackListModel;

// What the music player shows (Crate spec §7). Reads the queue, the playback
// ticket and the player's state, and hands QML finished display values: the
// "Out of the sleeve" stage, the docked audio bar and the player's side panel
// never reshape queue maps or stream lists themselves.
class NowPlayingMusicController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY trackChanged)
    Q_PROPERTY(QString trackId READ trackId NOTIFY trackChanged)
    Q_PROPERTY(QVariantMap trackItem READ trackItem NOTIFY trackChanged)
    Q_PROPERTY(QString title READ title NOTIFY trackChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY trackChanged)
    Q_PROPERTY(QString artistId READ artistId NOTIFY trackChanged)
    Q_PROPERTY(QString album READ album NOTIFY trackChanged)
    Q_PROPERTY(QString albumId READ albumId NOTIFY trackChanged)
    Q_PROPERTY(QString coverUrl READ coverUrl NOTIFY trackChanged)
    Q_PROPERTY(bool favourite READ favourite NOTIFY favouriteChanged)
    Q_PROPERTY(QString readout READ readout NOTIFY readoutChanged)
    Q_PROPERTY(QString sourceLabel READ sourceLabel NOTIFY sourceLabelChanged)
    Q_PROPERTY(QString sourceKicker READ sourceKicker NOTIFY sourceLabelChanged)
    Q_PROPERTY(QString recordState READ recordState NOTIFY recordStateChanged)
    Q_PROPERTY(QObject *albumTracks READ albumTracks CONSTANT)
    Q_PROPERTY(int currentAlbumRow READ currentAlbumRow NOTIFY currentAlbumRowChanged)
    Q_PROPERTY(QString albumSummary READ albumSummary NOTIFY albumSummaryChanged)
    Q_PROPERTY(QString timeText READ timeText NOTIFY timeTextChanged)
    Q_PROPERTY(bool lyricsAvailable READ lyricsAvailable NOTIFY lyricsChanged)
    Q_PROPERTY(QVariantList lyrics READ lyrics NOTIFY lyricsChanged)
    Q_PROPERTY(bool lyricsTimed READ lyricsTimed NOTIFY lyricsChanged)
    Q_PROPERTY(int currentLyricRow READ currentLyricRow NOTIFY currentLyricRowChanged)

public:
    NowPlayingMusicController(MusicRepository *repository, MusicPlayback *playback,
                              QObject *parent = nullptr);

    // MusicPlayback exposes neither of these, and this controller reads both.
    // Called once, by Application, right after construction.
    void bind(PlayerController *player, ItemActions *actions);

    bool active() const { return m_active; }
    QString trackId() const { return m_trackId; }
    QVariantMap trackItem() const { return m_trackItem; }
    QString title() const { return m_title; }
    QString artist() const { return m_artist; }
    QString artistId() const { return m_artistId; }
    QString album() const { return m_album; }
    QString albumId() const { return m_albumId; }
    QString coverUrl() const { return m_coverUrl; }
    bool favourite() const;
    QString readout() const { return m_readout; }
    QString sourceLabel() const { return m_sourceLabel; }
    QString sourceKicker() const { return m_sourceKicker; }
    QString recordState() const { return m_recordState; }
    QObject *albumTracks() const;
    TrackListModel *trackModel() const { return m_tracks; }
    int currentAlbumRow() const { return m_currentAlbumRow; }
    QString albumSummary() const { return m_albumSummary; }
    QString timeText() const { return m_timeText; }
    bool lyricsAvailable() const { return m_lyricsEnabled && !m_lyrics.isEmpty(); }
    QVariantList lyrics() const { return m_lyricsVariant; }
    bool lyricsTimed() const { return !m_lyrics.isEmpty() && m_lyrics.constFirst().timeMs >= 0; }
    int currentLyricRow() const { return m_currentLyricRow; }

    // Plays the current album from `row` of albumTracks, labelled with the album.
    Q_INVOKABLE void playAlbumFrom(int row);
    Q_INVOKABLE void toggleFavourite();

    // caps::kLyricsAvailable is constexpr; tests drive both branches.
    void setLyricsEnabledForTests(bool enabled);

signals:
    // A different album's track is about to be shown. Emitted after coverUrl
    // already holds the new cover and before trackChanged(), so RecordStage can
    // start its slide-in before any binding swaps the sleeve.
    void albumChanging();
    void trackChanged();
    void favouriteChanged();
    void readoutChanged();
    void sourceLabelChanged();
    void recordStateChanged();
    void currentAlbumRowChanged();
    void albumSummaryChanged();
    void timeTextChanged();
    void lyricsChanged();
    void currentLyricRowChanged();

private:
    void refreshTrack();
    void refreshSourceLabel();
    void refreshRecordState();
    void refreshReadout();
    void refreshTime();
    void refreshCurrentAlbumRow();
    void refreshCurrentLyricRow();
    void loadAlbumTracks(const QString &albumId);
    void setAlbumSummary(const QString &summary);
    void loadLyrics();
    void clearLyrics();

    MusicRepository *m_repository = nullptr;
    MusicPlayback *m_playback = nullptr;
    TrackListModel *m_tracks = nullptr;
    QPointer<PlayerController> m_player;
    QPointer<ItemActions> m_actions;

    bool m_active = false;
    QString m_trackId;
    QVariantMap m_trackItem;
    QString m_title;
    QString m_artist;
    QString m_artistId;
    QString m_album;
    QString m_albumId;
    QString m_coverUrl;
    bool m_itemFavourite = false;
    QHash<QString, bool> m_favouriteOverrides;
    QString m_readout;
    QString m_sourceLabel;
    QString m_sourceKicker;
    QString m_recordState = QStringLiteral("stopped");
    QString m_timeText;

    QString m_tracksAlbumId;
    quint64 m_albumGeneration = 0;
    int m_currentAlbumRow = -1;
    QString m_albumSummary;

    bool m_lyricsEnabled;
    QString m_lyricsKey;
    quint64 m_lyricsGeneration = 0;
    QList<LyricLine> m_lyrics;
    QVariantList m_lyricsVariant;
    int m_currentLyricRow = -1;
};

} // namespace strmqt::music
```

- [ ] **Step 4: Write the implementation**

`src/app/controllers/music/NowPlayingMusicController.cpp`:

```cpp
#include "app/controllers/music/NowPlayingMusicController.h"

#include <algorithm>
#include <iterator>

#include "app/ItemActions.h"
#include "app/PlayQueue.h"
#include "app/controllers/PlayerController.h"
#include "app/music/MusicFormat.h"
#include "app/music/MusicPlayback.h"
#include "app/music/MusicRepository.h"
#include "app/music/models/TrackListModel.h"
#include "core/Log.h"
#include "server/emby/EmbyMusicMapper.h"
#include "server/emby/MusicServerCapabilities.h"

namespace strmqt::music {

namespace {

bool isLyricsCodec(const QString &codec)
{
    const QString lower = codec.toLower();
    return lower == QLatin1String("lrc") || lower == QLatin1String("txt")
           || lower == QLatin1String("text");
}

} // namespace

NowPlayingMusicController::NowPlayingMusicController(MusicRepository *repository,
                                                     MusicPlayback *playback, QObject *parent)
    : QObject(parent),
      m_repository(repository),
      m_playback(playback),
      m_tracks(new TrackListModel(this)),
      m_lyricsEnabled(emby::caps::kLyricsAvailable)
{
}

void NowPlayingMusicController::bind(PlayerController *player, ItemActions *actions)
{
    Q_ASSERT(player && actions && !m_player);
    m_player = player;
    m_actions = actions;

    PlayQueue *queue = player->queue();
    connect(queue, &PlayQueue::currentChanged, this, &NowPlayingMusicController::refreshTrack);
    connect(queue, &PlayQueue::currentItemChanged, this, &NowPlayingMusicController::refreshTrack);
    connect(queue, &PlayQueue::queueChanged, this, &NowPlayingMusicController::refreshTrack);
    connect(queue, &PlayQueue::sourceLabelChanged, this, &NowPlayingMusicController::refreshSourceLabel);

    connect(player, &PlayerController::activeChanged, this, &NowPlayingMusicController::refreshRecordState);
    connect(player, &PlayerController::pausedChanged, this, &NowPlayingMusicController::refreshRecordState);
    connect(player, &PlayerController::busyChanged, this, &NowPlayingMusicController::refreshRecordState);
    connect(player, &PlayerController::bufferingChanged, this, &NowPlayingMusicController::refreshRecordState);
    connect(player, &PlayerController::activeChanged, this, &NowPlayingMusicController::refreshTime);
    // Forced for every new ticket, so it also marks "this track's streams are in".
    connect(player, &PlayerController::sourceIndexChanged, this, [this] {
        refreshReadout();
        loadLyrics();
    });
    connect(player, &PlayerController::streamMethodChanged, this, &NowPlayingMusicController::refreshReadout);
    connect(player, &PlayerController::positionChanged, this, [this] {
        refreshTime();
        refreshCurrentLyricRow();
    });
    connect(player, &PlayerController::durationChanged, this, &NowPlayingMusicController::refreshTime);

    connect(actions, &ItemActions::favoriteChanged, this, [this](const QString &itemId, bool favourite) {
        m_favouriteOverrides.insert(itemId, favourite);
        if (itemId == m_trackId)
            emit favouriteChanged();
    });

    refreshTrack();
}

bool NowPlayingMusicController::favourite() const
{
    return m_active && m_favouriteOverrides.value(m_trackId, m_itemFavourite);
}

QObject *NowPlayingMusicController::albumTracks() const
{
    return m_tracks;
}

void NowPlayingMusicController::refreshTrack()
{
    const PlayQueue *queue = m_player ? m_player->queue() : nullptr;
    const MediaItem item = queue ? queue->current() : MediaItem{};
    const bool active = !item.id.isEmpty() && item.type == QLatin1String("Audio");

    const QString previousTrackId = m_trackId;
    const QString previousAlbumId = m_albumId;
    const bool wasActive = m_active;
    const QVariantMap previousItem = m_trackItem;

    m_active = active;
    m_trackId = active ? item.id : QString();
    m_trackItem = active ? queue->currentItem() : QVariantMap{};
    m_title = active ? item.name : QString();
    if (active && !item.artists.isEmpty()) {
        m_artist = item.artists.join(QStringLiteral(", "));
        m_artistId = item.artistIds.value(0);
    } else {
        m_artist = active ? item.albumArtist : QString();
        m_artistId.clear();
    }
    m_album = active ? item.album : QString();
    m_albumId = active ? item.albumId : QString();
    QString cover = active ? music::coverUrl(item.coverSource()) : QString();
    if (active && cover.isEmpty())
        cover = m_trackItem.value(QStringLiteral("posterUrl")).toString();
    m_coverUrl = cover;
    // A newer value from the server (a live patch on the queue item) replaces
    // whatever this session last toggled.
    if (active && item.favorite != m_itemFavourite && m_trackId == previousTrackId)
        m_favouriteOverrides.remove(m_trackId);
    m_itemFavourite = active && item.favorite;

    const bool trackMoved = m_trackId != previousTrackId || m_active != wasActive;
    if (trackMoved) {
        if (!previousAlbumId.isEmpty() && !m_albumId.isEmpty() && previousAlbumId != m_albumId)
            emit albumChanging();
        clearLyrics();
    }
    if (trackMoved || m_trackItem != previousItem) {
        emit trackChanged();
        emit favouriteChanged();
    }

    if (m_albumId != m_tracksAlbumId)
        loadAlbumTracks(m_albumId);
    else
        refreshCurrentAlbumRow();
    refreshSourceLabel();
    refreshRecordState();
    refreshReadout();
    refreshTime();
    if (trackMoved)
        loadLyrics();
}

void NowPlayingMusicController::refreshSourceLabel()
{
    const PlayQueue *queue = m_player ? m_player->queue() : nullptr;
    QString label;
    QString kicker;
    if (queue && m_active) {
        if (!queue->sourceLabel().isEmpty()) {
            label = queue->sourceLabel();
            kicker = tr("Playing from · %1").arg(label);
        } else if (!queue->contextLabel().isEmpty()) {
            // contextLabel is already the translated "from %1".
            label = queue->contextLabel();
            kicker = tr("Playing %1").arg(label);
        }
    }
    if (label == m_sourceLabel && kicker == m_sourceKicker)
        return;
    m_sourceLabel = label;
    m_sourceKicker = kicker;
    emit sourceLabelChanged();
}

void NowPlayingMusicController::refreshRecordState()
{
    QString state = QStringLiteral("stopped");
    if (m_active && m_player && m_player->active()) {
        if (m_player->busy() || m_player->buffering())
            state = QStringLiteral("buffering");
        else if (m_player->paused())
            state = QStringLiteral("paused");
        else
            state = QStringLiteral("playing");
    }
    if (state == m_recordState)
        return;
    m_recordState = state;
    emit recordStateChanged();
}

void NowPlayingMusicController::refreshReadout()
{
    QStringList parts;
    if (m_active && m_player) {
        const QVariantList streams = m_player->audioStreams();
        if (!streams.isEmpty()) {
            const QVariantMap stream = streams.constFirst().toMap();
            const AudioFormat format = emby::deriveAudioFormat(
                stream.value(QStringLiteral("codec")).toString(),
                stream.value(QStringLiteral("bitDepth")).toInt(),
                stream.value(QStringLiteral("sampleRate")).toInt(),
                static_cast<int>(stream.value(QStringLiteral("bitRate")).toLongLong()),
                stream.value(QStringLiteral("channels")).toInt());
            if (!format.badge.isEmpty())
                parts.append(format.badge);
        }
        const QString method = m_player->streamMethod();
        if (method == QLatin1String("DirectPlay"))
            parts.append(tr("DIRECT PLAY"));
        else if (method == QLatin1String("DirectStream"))
            parts.append(tr("DIRECT STREAM"));
        else if (method == QLatin1String("Transcode"))
            parts.append(tr("TRANSCODE"));
    }
    const QString readout = parts.join(QStringLiteral(" · "));
    if (readout == m_readout)
        return;
    m_readout = readout;
    emit readoutChanged();
}

void NowPlayingMusicController::refreshTime()
{
    QString text;
    if (m_active && m_player && m_player->active()) {
        const QString elapsed = formatDuration(m_player->positionMs());
        const QString total = formatDuration(m_player->durationMs());
        text = (elapsed.isEmpty() ? QStringLiteral("0:00") : elapsed) + QStringLiteral(" / ")
               + (total.isEmpty() ? QStringLiteral("--:--") : total);
    }
    if (text == m_timeText)
        return;
    m_timeText = text;
    emit timeTextChanged();
}

void NowPlayingMusicController::refreshCurrentAlbumRow()
{
    const int row = m_trackId.isEmpty()
                        ? -1
                        : m_tracks->indexOfNavigationIdentity(QStringLiteral("i:") + m_trackId);
    if (row == m_currentAlbumRow)
        return;
    m_currentAlbumRow = row;
    emit currentAlbumRowChanged();
}

void NowPlayingMusicController::setAlbumSummary(const QString &summary)
{
    if (summary == m_albumSummary)
        return;
    m_albumSummary = summary;
    emit albumSummaryChanged();
}

void NowPlayingMusicController::loadAlbumTracks(const QString &albumId)
{
    m_tracksAlbumId = albumId;
    const quint64 generation = ++m_albumGeneration;
    if (albumId.isEmpty()) {
        m_tracks->clear();
        setAlbumSummary(QString());
        refreshCurrentAlbumRow();
        return;
    }
    m_repository->albumTracks(albumId).then(this, [this, generation](Result<QList<Track>> result) {
        if (generation != m_albumGeneration)
            return;
        if (!result.ok()) {
            qCWarning(logApp) << "Now playing: album tracks unavailable:" << result.error;
            m_tracks->clear();
            setAlbumSummary(QString());
            refreshCurrentAlbumRow();
            m_tracksAlbumId.clear(); // the next track of this album tries again
            return;
        }
        qint64 runtimeMs = 0;
        for (const Track &track : std::as_const(result.value))
            runtimeMs += track.runtimeMs;
        const QString count = formatTrackCount(static_cast<int>(result.value.size()));
        const QString runtime = formatRuntime(runtimeMs);
        m_tracks->setItems(result.value);
        setAlbumSummary(runtime.isEmpty() ? count : count + QStringLiteral(" · ") + runtime);
        refreshCurrentAlbumRow();
    });
}

void NowPlayingMusicController::playAlbumFrom(int row)
{
    if (!m_playback || row < 0 || row >= m_tracks->rowCount())
        return;
    m_playback->playTracks(m_tracks->items(), row, m_album);
}

void NowPlayingMusicController::toggleFavourite()
{
    if (m_actions && m_active)
        m_actions->setFavorite(m_trackId, !favourite());
}

void NowPlayingMusicController::setLyricsEnabledForTests(bool enabled)
{
    m_lyricsEnabled = enabled;
    clearLyrics();
    loadLyrics();
}

void NowPlayingMusicController::clearLyrics()
{
    ++m_lyricsGeneration;
    m_lyricsKey.clear();
    if (!m_lyrics.isEmpty()) {
        m_lyrics.clear();
        m_lyricsVariant.clear();
        emit lyricsChanged();
    }
    refreshCurrentLyricRow();
}

void NowPlayingMusicController::loadLyrics()
{
    if (!m_lyricsEnabled || !m_active || !m_player)
        return;
    const QString sourceId = m_player->currentSource().value(QStringLiteral("id")).toString();
    int streamIndex = -1;
    const QVariantList subtitles = m_player->subtitleStreams();
    for (const QVariant &value : subtitles) {
        const QVariantMap stream = value.toMap();
        if (isLyricsCodec(stream.value(QStringLiteral("codec")).toString())) {
            streamIndex = stream.value(QStringLiteral("index")).toInt();
            break;
        }
    }
    if (sourceId.isEmpty() || streamIndex < 0)
        return;
    const QString key = m_trackId + QLatin1Char('|') + sourceId + QLatin1Char('|')
                        + QString::number(streamIndex);
    if (key == m_lyricsKey)
        return;
    m_lyricsKey = key;
    const quint64 generation = ++m_lyricsGeneration;
    m_repository->lyrics(m_trackId, sourceId, streamIndex)
        .then(this, [this, generation](Result<QList<LyricLine>> result) {
            if (generation != m_lyricsGeneration)
                return;
            if (!result.ok()) {
                qCWarning(logApp) << "Now playing: lyrics unavailable:" << result.error;
                return;
            }
            m_lyrics = result.value;
            m_lyricsVariant.clear();
            for (const LyricLine &line : std::as_const(m_lyrics))
                m_lyricsVariant.append(QVariantMap{{QStringLiteral("timeMs"), line.timeMs},
                                                   {QStringLiteral("text"), line.text}});
            emit lyricsChanged();
            refreshCurrentLyricRow();
        });
}

void NowPlayingMusicController::refreshCurrentLyricRow()
{
    int row = -1;
    if (m_player && lyricsTimed()) {
        const qint64 position = m_player->positionMs();
        const auto next = std::upper_bound(m_lyrics.cbegin(), m_lyrics.cend(), position,
                                           [](qint64 at, const LyricLine &line) { return at < line.timeMs; });
        row = static_cast<int>(std::distance(m_lyrics.cbegin(), next)) - 1;
    }
    if (row == m_currentLyricRow)
        return;
    m_currentLyricRow = row;
    emit currentLyricRowChanged();
}

} // namespace strmqt::music
```

Add to `strmqt_app` in `src/CMakeLists.txt`:

```cmake
    app/controllers/music/NowPlayingMusicController.h app/controllers/music/NowPlayingMusicController.cpp
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `cmake --build --preset dev --target tst_now_playing_music && ctest --preset dev -R tst_now_playing_music --output-on-failure`
Expected: PASS (11 tests: 9 slots plus `initTestCase`/`cleanupTestCase`).

If `readoutFromTicketAndMethod` shows a different badge, check `deriveAudioFormat("flac", 24, 96000, 0, 2)` in `tst_music_mapper` first. The fixture and that function define the string, and the test must not be edited to match a broken mapper.

- [ ] **Step 6: Commit**

```bash
git add src/app/controllers/music/NowPlayingMusicController.h src/app/controllers/music/NowPlayingMusicController.cpp \
        src/CMakeLists.txt tests/integration/tst_now_playing_music.cpp tests/CMakeLists.txt
git commit -m "feat(music): add the now playing music controller

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: Wire `NowPlayingMusicCtl`

**Files:**
- Modify: `src/app/Application.h`, `src/app/Application.cpp`
- Modify: `src/app/main.cpp`

**Interfaces:**
- Consumes: `NowPlayingMusicController` (Task 3); `m_musicRepository`, `m_musicPlayback`, `m_musicRelay` (Phase 1); `m_player`, `m_actions`.
- Produces:
  - `music::NowPlayingMusicController *Application::nowPlayingMusic() const`
  - the QML context property `NowPlayingMusicCtl`
  - `trackModel()` registered with `musicRelay()->addModel(...)`, so favourites patch the Album tab in place

- [ ] **Step 1: Declare**

In `src/app/Application.h`, add `class NowPlayingMusicController;` to the `namespace music { … }` forward declarations that Phase 1 added. After `music::MusicPlayback *musicPlayback() const { return m_musicPlayback; }`:

```cpp
    music::NowPlayingMusicController *nowPlayingMusic() const { return m_nowPlayingMusic; }
```

After `music::MusicPlayback *m_musicPlayback = nullptr;`:

```cpp
    music::NowPlayingMusicController *m_nowPlayingMusic = nullptr;
```

- [ ] **Step 2: Construct and bind**

In `src/app/Application.cpp`, add `#include "controllers/music/NowPlayingMusicController.h"`. Directly after `m_musicPlayback = new music::MusicPlayback(m_musicRepository, m_actions, this);`:

```cpp
    // What the music player and the docked audio bar show (Crate spec §7).
    // bind() hands it the player and the favourite source MusicPlayback keeps
    // to itself; its album track list takes user-data patches like any page's.
    m_nowPlayingMusic = new music::NowPlayingMusicController(m_musicRepository, m_musicPlayback, this);
    m_nowPlayingMusic->bind(m_player, m_actions);
    m_musicRelay->addModel(m_nowPlayingMusic->trackModel());
```

- [ ] **Step 3: Expose it**

In `src/app/main.cpp`, add `#include "controllers/music/NowPlayingMusicController.h"`. After the `MusicPlay` context property line:

```cpp
    // The music player's display values: record state, readout, album, lyrics.
    engine.rootContext()->setContextProperty(QStringLiteral("NowPlayingMusicCtl"), app.nowPlayingMusic());
```

- [ ] **Step 4: Build and self-test**

```bash
cmake --build --preset dev
STRMQT_SELFTEST=1 QT_QPA_PLATFORM=offscreen QT_ASSUME_STDERR_HAS_CONSOLE=1 ./build/dev/strmqt; echo "selftest exit $?"
ctest --preset dev -R "tst_now_playing_music|tst_music_playback|tst_settings_prefs" --output-on-failure
```

Expected: the build has no warnings, `selftest exit 0`, and all three tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/app/Application.h src/app/Application.cpp src/app/main.cpp
git commit -m "feat(music): expose the now playing music controller to QML

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 5: `RecordStage`

**Files:**
- Create: `src/ui/music/RecordStage.qml`
- Modify: `src/ui/music/Music.cmake`
- Test: `tests/unit/tst_record_stage.cpp`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Theme` (`crateSleeveRadius`, `crateSleeveElevation`, `animSlow`, `animNormalMs`, `easeStandard`, `reducedMotion`, `ground`, `hairline`, `surfaceRaisedColor`, `shadowColor`, `scale`); `StrmImage`.
- Produces: `RecordStage` (module `StrmQt`):
  - In: `coverUrl`, `recordState` (`"playing" | "paused" | "buffering" | "stopped"`), `animate` (bound by callers to `Prefs.animateRecord`), `live`, `holdIn`, `sleeveSize` (default `Theme.scale(420)`)
  - Read-only: `spinning`, `motion`, `slide` (0 = in the sleeve, 0.45 = out), `phase` (`""` | `"in"`), `shownCover`, `changing`, `turning`, `settling`, `sleeveRadius`
  - `function changeAlbum(newCoverUrl: string): void`, `function sleeveRect(target: Item): rect`
  - `implicitWidth = round(sleeveSize × 1.45)`, `implicitHeight = sleeveSize`

Behaviour (spec §7.2 table):
- **Out of the sleeve:** `slide` is 0.45 unless stopped, `holdIn`, or `phase === "in"`. It animates over `Theme.animSlow` only while `motion` (`animate && !Theme.reducedMotion`).
- **Spin:** only while `motion && live && the window is shown && playing && !holdIn && phase === ""`. A `RotationAnimator` turns once every 1.8 s from the current angle. Stopping writes the angle back, so resuming never jumps.
- **Pause:** the spin stops and a 600 ms `OutCubic` settle adds 40° (decelerate to rest). Buffering stops it with no settle.
- **Different album:** `changeAlbum(url)` runs a sequence:
  1. slide in (`phase "in"`, `animSlow`);
  2. the sleeve cross-fades to `url`;
  3. hold for `animNormalMs`;
  4. slide out, and resync to `coverUrl`.

  With no motion, when stopped, or during `holdIn`, the cover swaps at once.
- **Sleeve flight:** during `holdIn` the sleeve, record and shadow are invisible and the record is in. The flight lands on `sleeveRect`, and when `holdIn` clears the record slides out.
- **One rotating layer:** the disc, grooves and circle-cropped label are one `layer.enabled` item.

- [ ] **Step 1: Write the failing test**

`tests/unit/tst_record_stage.cpp`:

```cpp
#include <QDir>
#include <QFile>
#include <QImage>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>
#include <QTest>

namespace {

const char *kProbe = R"QML(
import QtQuick
import StrmQt

Item {
    id: root
    width: 520
    height: 320

    function setReducedMotion(on) { Theme.reducedMotion = on; }
    function change(url) { stage.changeAlbum(url); }
    function sleeveWidth() { return stage.sleeveRect(root).width; }

    RecordStage {
        id: stage
        objectName: "stage"
        sleeveSize: 300
    }
}
)QML";

bool copySource(const QString &from, const QString &modulePath, const QString &name)
{
    return QFile::copy(QStringLiteral(STRMQT_SOURCE_DIR) + from, modulePath + QLatin1Char('/') + name);
}

} // namespace

class TestRecordStage : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void spinsOnlyWhilePlayingAndLive();
    void animateOffOrReducedMotionStillsTheRecord();
    void albumChangeSlidesInSwapsAndSlidesOut();
    void changeWhileStoppedOrInFlightSwapsAtOnce();
    void sleeveRectMapsTheSleeve();

private:
    QVariant call(const char *function, const QVariant &argument = QVariant());

    QTemporaryDir m_dir;
    QString m_coverA;
    QString m_coverB;
    QQuickView *m_view = nullptr;
    QObject *m_root = nullptr;
    QObject *m_stage = nullptr;
};

void TestRecordStage::initTestCase()
{
    QVERIFY(m_dir.isValid());
    const QString modulePath = m_dir.filePath(QStringLiteral("StrmQt"));
    QVERIFY(QDir().mkpath(modulePath));
    QVERIFY(copySource(QStringLiteral("/src/ui/Theme.qml"), modulePath, QStringLiteral("Theme.qml")));
    QVERIFY(copySource(QStringLiteral("/src/ui/controls/StrmImage.qml"), modulePath, QStringLiteral("StrmImage.qml")));
    QVERIFY(copySource(QStringLiteral("/src/ui/music/RecordStage.qml"), modulePath, QStringLiteral("RecordStage.qml")));

    QFile qmldir(modulePath + QStringLiteral("/qmldir"));
    QVERIFY(qmldir.open(QIODevice::WriteOnly));
    qmldir.write("module StrmQt\n"
                 "singleton Theme 1.0 Theme.qml\n"
                 "StrmImage 1.0 StrmImage.qml\n"
                 "RecordStage 1.0 RecordStage.qml\n");
    qmldir.close();

    QFile probe(m_dir.filePath(QStringLiteral("Probe.qml")));
    QVERIFY(probe.open(QIODevice::WriteOnly));
    probe.write(kProbe);
    probe.close();

    // Two real covers, so StrmImage reaches Ready instead of logging a dead URL.
    QImage a(8, 8, QImage::Format_RGB32);
    a.fill(Qt::darkRed);
    QVERIFY(a.save(m_dir.filePath(QStringLiteral("a.png"))));
    QImage b(8, 8, QImage::Format_RGB32);
    b.fill(Qt::darkBlue);
    QVERIFY(b.save(m_dir.filePath(QStringLiteral("b.png"))));
    m_coverA = QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("a.png"))).toString();
    m_coverB = QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("b.png"))).toString();
}

void TestRecordStage::init()
{
    m_view = new QQuickView;
    m_view->engine()->addImportPath(m_dir.path());
    m_view->setSource(QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("Probe.qml"))));
    QVERIFY2(m_view->status() == QQuickView::Ready,
             qPrintable(m_view->errors().isEmpty() ? QStringLiteral("no root object")
                                                   : m_view->errors().first().toString()));
    m_view->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_view));
    m_root = m_view->rootObject();
    m_stage = m_root->findChild<QQuickItem *>(QStringLiteral("stage"));
    QVERIFY(m_stage);
    m_stage->setProperty("coverUrl", m_coverA);
}

void TestRecordStage::cleanup()
{
    delete m_view;
    m_view = nullptr;
    m_root = nullptr;
    m_stage = nullptr;
}

QVariant TestRecordStage::call(const char *function, const QVariant &argument)
{
    QVariant result;
    if (argument.isValid())
        QMetaObject::invokeMethod(m_root, function, Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, argument));
    else
        QMetaObject::invokeMethod(m_root, function, Q_RETURN_ARG(QVariant, result));
    return result;
}

void TestRecordStage::spinsOnlyWhilePlayingAndLive()
{
    QCOMPARE(m_stage->property("recordState").toString(), QStringLiteral("stopped"));
    QVERIFY(!m_stage->property("turning").toBool());
    QCOMPARE(m_stage->property("slide").toReal(), 0.0);
    QCOMPARE(m_stage->property("shownCover").toString(), m_coverA);

    m_stage->setProperty("recordState", QStringLiteral("playing"));
    QVERIFY(m_stage->property("spinning").toBool());
    QTRY_VERIFY(m_stage->property("turning").toBool());
    QTRY_COMPARE(m_stage->property("slide").toReal(), 0.45);

    // Leaving the page stops the animator; it is not merely hidden.
    m_stage->setProperty("live", false);
    QVERIFY(!m_stage->property("spinning").toBool());
    QTRY_VERIFY(!m_stage->property("turning").toBool());
    m_stage->setProperty("live", true);
    QTRY_VERIFY(m_stage->property("turning").toBool());

    m_stage->setProperty("recordState", QStringLiteral("paused"));
    QTRY_VERIFY(!m_stage->property("turning").toBool());
    QVERIFY(m_stage->property("settling").toBool());
    QTRY_VERIFY(!m_stage->property("settling").toBool());
    QCOMPARE(m_stage->property("slide").toReal(), 0.45); // stays out

    m_stage->setProperty("recordState", QStringLiteral("buffering"));
    QVERIFY(!m_stage->property("turning").toBool());
    QVERIFY(!m_stage->property("settling").toBool());
    QCOMPARE(m_stage->property("slide").toReal(), 0.45); // holds

    m_stage->setProperty("recordState", QStringLiteral("stopped"));
    QTRY_COMPARE(m_stage->property("slide").toReal(), 0.0);
}

void TestRecordStage::animateOffOrReducedMotionStillsTheRecord()
{
    m_stage->setProperty("animate", false);
    m_stage->setProperty("recordState", QStringLiteral("playing"));
    QVERIFY(!m_stage->property("spinning").toBool());
    QVERIFY(!m_stage->property("turning").toBool());
    QCOMPARE(m_stage->property("slide").toReal(), 0.45); // out at once, static

    m_stage->setProperty("animate", true);
    QTRY_VERIFY(m_stage->property("turning").toBool());

    call("setReducedMotion", true);
    QVERIFY(!m_stage->property("motion").toBool());
    QTRY_VERIFY(!m_stage->property("turning").toBool());
    QVERIFY(!m_stage->property("settling").toBool());
    m_stage->setProperty("recordState", QStringLiteral("stopped"));
    QCOMPARE(m_stage->property("slide").toReal(), 0.0);
    call("setReducedMotion", false);
}

void TestRecordStage::albumChangeSlidesInSwapsAndSlidesOut()
{
    m_stage->setProperty("recordState", QStringLiteral("playing"));
    QTRY_COMPARE(m_stage->property("slide").toReal(), 0.45);

    // NowPlayingMusicController emits albumChanging before the cover binding moves.
    call("change", m_coverB);
    m_stage->setProperty("coverUrl", m_coverB);
    QVERIFY(m_stage->property("changing").toBool());
    QCOMPARE(m_stage->property("phase").toString(), QStringLiteral("in"));
    QCOMPARE(m_stage->property("shownCover").toString(), m_coverA); // not before it is in
    QVERIFY(!m_stage->property("spinning").toBool());

    QTRY_COMPARE(m_stage->property("slide").toReal(), 0.0);
    QTRY_COMPARE(m_stage->property("shownCover").toString(), m_coverB);
    QTRY_COMPARE(m_stage->property("phase").toString(), QString());
    QTRY_VERIFY(!m_stage->property("changing").toBool());
    QTRY_COMPARE(m_stage->property("slide").toReal(), 0.45);
    QTRY_VERIFY(m_stage->property("turning").toBool());
}

void TestRecordStage::changeWhileStoppedOrInFlightSwapsAtOnce()
{
    call("change", m_coverB);
    QVERIFY(!m_stage->property("changing").toBool());
    QCOMPARE(m_stage->property("shownCover").toString(), m_coverB);

    m_stage->setProperty("recordState", QStringLiteral("playing"));
    m_stage->setProperty("holdIn", true);
    QCOMPARE(m_stage->property("slide").toReal(), 0.0);
    QVERIFY(!m_stage->property("spinning").toBool());
    call("change", m_coverA);
    QVERIFY(!m_stage->property("changing").toBool());
    QCOMPARE(m_stage->property("shownCover").toString(), m_coverA);

    // The flight has landed: out it comes.
    m_stage->setProperty("holdIn", false);
    QTRY_COMPARE(m_stage->property("slide").toReal(), 0.45);
}

void TestRecordStage::sleeveRectMapsTheSleeve()
{
    QCOMPARE(call("sleeveWidth").toReal(), 300.0);
    QCOMPARE(m_stage->property("implicitWidth").toReal(), 435.0);
    QCOMPARE(m_stage->property("implicitHeight").toReal(), 300.0);
}

QTEST_MAIN(TestRecordStage)
#include "tst_record_stage.moc"
```

Add to `tests/CMakeLists.txt`, after `tst_navigation_history`:

```cmake
strmqt_add_test(tst_record_stage unit/tst_record_stage.cpp)
target_link_libraries(tst_record_stage PRIVATE Qt6::Gui Qt6::Quick)
target_compile_definitions(tst_record_stage PRIVATE STRMQT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
set_tests_properties(tst_record_stage PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --preset dev && cmake --build --preset dev --target tst_record_stage && ctest --preset dev -R tst_record_stage --output-on-failure`
Expected: FAIL in `initTestCase`, because `QFile::copy` of `/src/ui/music/RecordStage.qml` returns false.

- [ ] **Step 3: Write `RecordStage.qml`**

`src/ui/music/RecordStage.qml`:

```qml
import QtQuick
import QtQuick.Effects
import QtQuick.Window
import StrmQt

// RecordStage: the "Out of the sleeve" stage (Crate spec §7.2).
//
// A square sleeve with a record behind it. While music plays the record sits
// 45% out of the sleeve and turns at 33⅓ rpm; a pause lets it run down; a new
// album slides the record in, cross-fades the sleeve and slides it out again;
// a stopped session puts it away. Everything here is presentation: the state
// comes from NowPlayingMusicCtl.recordState and the album change from its
// albumChanging() signal, which the owner forwards to changeAlbum().
//
// One rotating layer. The disc, its grooves and the circle-cropped label are a
// single layer-enabled item, so a turn re-composites one texture instead of
// re-rendering the cover and the mask every frame. The animator runs only while
// it can be seen: the page is current (`live`), the window is shown, and motion
// is allowed; otherwise it is stopped, not hidden.
Item {
    id: stage

    property string coverUrl: ""
    // "playing" | "paused" | "buffering" | "stopped". Not `state`: that is
    // Item's own state-machine property.
    property string recordState: "stopped"
    // Settings → Appearance → Animate record. Off: a still record, slid out.
    property bool animate: true
    // The page holding this is the current page.
    property bool live: true
    // The shared sleeve is in the air (Main.qml's SleeveFlight). The stage keeps
    // its geometry for the flight to land on but draws nothing, and the record
    // stays in until the flight lands.
    property bool holdIn: false
    property real sleeveSize: Theme.scale(420)

    readonly property bool motion: stage.animate && !Theme.reducedMotion
    readonly property bool windowShown: stage.Window.visibility !== Window.Hidden
                                        && stage.Window.visibility !== Window.Minimized
    readonly property bool spinning: stage.motion && stage.live && stage.windowShown
                                     && stage.recordState === "playing"
                                     && !stage.holdIn && internal.phase === ""
    readonly property alias phase: internal.phase
    readonly property alias shownCover: internal.shownCover
    readonly property alias slide: disc.slide
    readonly property bool changing: changeSequence.running
    readonly property bool turning: spin.running
    readonly property bool settling: settle.running
    readonly property real sleeveRadius: Theme.crateSleeveRadius

    // The transition's large endpoint, in `target`'s coordinates. Empty before
    // layout, so Main.qml's poll waits one more frame.
    function sleeveRect(target: Item): rect {
        if (sleeve.width <= 0 || sleeve.height <= 0)
            return Qt.rect(0, 0, 0, 0);
        const corner = sleeve.mapToItem(target, 0, 0);
        return Qt.rect(corner.x, corner.y, sleeve.width, sleeve.height);
    }

    // A different album is about to play. Called before coverUrl moves.
    function changeAlbum(newCoverUrl: string): void {
        if (!stage.motion || stage.recordState === "stopped" || stage.holdIn) {
            changeSequence.stop();
            internal.phase = "";
            stage.swapCover(newCoverUrl);
            return;
        }
        internal.pendingCover = newCoverUrl;
        changeSequence.restart();
    }

    function swapCover(url: string): void {
        if (url === internal.shownCover)
            return;
        internal.previousCover = internal.shownCover;
        internal.shownCover = url;
    }

    function syncSpin(): void {
        if (stage.spinning) {
            settle.stop();
            const from = disc.rotation % 360;
            disc.rotation = from;
            spin.from = from;
            spin.to = from + 360;
            spin.restart();
            return;
        }
        if (!spin.running)
            return;
        spin.stop(); // an animator writes the angle it reached back on stop
        if (stage.recordState === "paused" && stage.motion) {
            settle.from = disc.rotation;
            settle.to = disc.rotation + 40;
            settle.restart();
        }
    }

    implicitWidth: Math.round(stage.sleeveSize * 1.45)
    implicitHeight: stage.sleeveSize

    Accessible.role: Accessible.Graphic
    Accessible.name: qsTr("Record")

    onSpinningChanged: stage.syncSpin()
    onMotionChanged: {
        if (!stage.motion)
            settle.stop();
    }
    onCoverUrlChanged: {
        if (!changeSequence.running)
            stage.swapCover(stage.coverUrl);
    }
    Component.onCompleted: {
        internal.shownCover = stage.coverUrl;
        stage.syncSpin();
    }

    QtObject {
        id: internal

        property string phase: ""
        property string shownCover: ""
        property string previousCover: ""
        property string pendingCover: ""
    }

    SequentialAnimation {
        id: changeSequence

        ScriptAction { script: internal.phase = "in" }
        PauseAnimation { duration: Theme.animSlow }
        ScriptAction { script: stage.swapCover(internal.pendingCover) }
        PauseAnimation { duration: Theme.animNormalMs }
        ScriptAction {
            script: {
                internal.phase = "";
                stage.swapCover(stage.coverUrl);
            }
        }
    }

    RotationAnimator {
        id: spin

        target: disc
        duration: 1800 // 33⅓ rpm
        loops: Animation.Infinite
    }

    RotationAnimator {
        id: settle

        target: disc
        duration: 600
        easing.type: Theme.easeStandard
    }

    // ── Shadow ──────────────────────────────────────────────────────────────
    // A flat caster behind the sleeve, for the reasons SleeveFlight.qml gives:
    // an effect on the sleeve itself would pad and inset its texture.
    Item {
        x: sleeve.x
        y: sleeve.y
        width: sleeve.width
        height: sleeve.height
        visible: !stage.holdIn

        Rectangle {
            id: shadowCaster

            anchors.fill: parent
            radius: stage.sleeveRadius
            color: Theme.shadowColor
            layer.enabled: true
        }

        MultiEffect {
            anchors.fill: parent
            source: shadowCaster
            autoPaddingEnabled: true
            shadowEnabled: true
            shadowColor: Theme.shadowColor
            shadowBlur: Theme.crateSleeveElevation.blur
            shadowVerticalOffset: Theme.crateSleeveElevation.y
            shadowOpacity: Theme.crateSleeveElevation.opacity
        }
    }

    // ── The record ──────────────────────────────────────────────────────────
    Item {
        id: disc

        readonly property real diameter: Math.round(stage.sleeveSize * 0.95)
        readonly property real slideTarget: (stage.recordState === "stopped" || stage.holdIn
                                             || internal.phase === "in") ? 0 : 0.45
        property real slide: disc.slideTarget

        x: Math.round((stage.sleeveSize - disc.diameter) / 2 + disc.slide * stage.sleeveSize)
        y: Math.round((stage.sleeveSize - disc.diameter) / 2)
        width: disc.diameter
        height: disc.diameter
        visible: !stage.holdIn
        layer.enabled: true
        layer.smooth: true

        Behavior on slide {
            enabled: stage.motion
            NumberAnimation {
                duration: Theme.animSlow
                easing.type: Theme.easeStandard
            }
        }

        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: Theme.ground
            border.width: 1
            border.color: Theme.hairline
        }

        // Grooves: a few hairline rings between the rim and the label.
        Repeater {
            model: 4

            Rectangle {
                required property int index

                readonly property real size: disc.diameter * (0.9 - index * 0.12)

                anchors.centerIn: parent
                width: size
                height: size
                radius: size / 2
                color: "transparent"
                border.width: 1
                border.color: Theme.hairline
                opacity: 0.6
            }
        }

        // The label: the cover, circle-cropped.
        Item {
            id: label

            readonly property real size: Math.round(disc.diameter * 0.36)

            anchors.centerIn: parent
            width: label.size
            height: label.size

            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: Theme.surfaceRaisedColor
            }

            Item {
                id: labelArt

                anchors.fill: parent
                visible: false
                layer.enabled: true

                StrmImage {
                    anchors.fill: parent
                    source: internal.shownCover
                }
            }

            Rectangle {
                id: labelMask

                anchors.fill: parent
                radius: width / 2
                visible: false
                layer.enabled: true
                layer.smooth: true
            }

            MultiEffect {
                anchors.fill: parent
                source: labelArt
                maskEnabled: true
                maskSource: labelMask
                maskThresholdMin: 0.5
                maskSpreadAtMin: 1.0
            }

            // Spindle hole.
            Rectangle {
                anchors.centerIn: parent
                width: Math.max(4, Math.round(label.size * 0.06))
                height: width
                radius: width / 2
                color: Theme.ground
            }
        }
    }

    // ── The sleeve ──────────────────────────────────────────────────────────
    Rectangle {
        id: sleeve

        width: stage.sleeveSize
        height: stage.sleeveSize
        radius: stage.sleeveRadius
        clip: true
        color: Theme.surfaceRaisedColor
        // Geometry stays for the flight to land on; the square itself is in the air.
        opacity: stage.holdIn ? 0 : 1

        // The outgoing cover under the incoming one: StrmImage starts a new
        // source at opacity 0 and fades in once it is Ready, which is the cross-fade.
        StrmImage {
            anchors.fill: parent
            source: internal.previousCover
            fadeDuration: 0
        }

        StrmImage {
            anchors.fill: parent
            source: internal.shownCover
            fadeDuration: Theme.animSlow
        }
    }
}
```

Add `ui/music/RecordStage.qml` to the `QML_FILES` list in `src/ui/music/Music.cmake`.

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build --preset dev --target tst_record_stage && ctest --preset dev -R tst_record_stage --output-on-failure`
Expected: PASS (7 tests: 5 slots plus `initTestCase`/`cleanupTestCase`), with no `QML` warnings in the output.

- [ ] **Step 5: Build the app and lint the file**

```bash
cmake --build --preset dev
bash scripts/check-qmllint-baseline.sh build/dev
```

Expected: the build is clean. The lint script lists no new fingerprint for `RecordStage.qml`, since it reads only `Theme`, its own ids and `StrmImage`. Fix anything listed.

- [ ] **Step 6: Commit**

```bash
git add src/ui/music/RecordStage.qml src/ui/music/Music.cmake tests/unit/tst_record_stage.cpp tests/CMakeLists.txt
git commit -m "feat(music): add the record stage

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 6: `MusicPlayerPanel`

**Files:**
- Create: `src/ui/music/MusicPlayerPanel.qml`
- Modify: `src/ui/music/Music.cmake`

**Interfaces:**
- Consumes:
  - `NowPlayingMusicCtl`: `sourceKicker`, `album`, `albumSummary`, `coverUrl`, `albumTracks`, `currentAlbumRow`, `playAlbumFrom`, `lyricsAvailable`, `lyrics`, `lyricsTimed`, `currentLyricRow`. These are runtime bindings, so the panel builds before Task 4 lands.
  - `PlayerCtl.queue` (`count`, `currentIndex`, `jumpTo`, `removeAt`, `moveItem`)
  - `TrackTable`, `TrackRow`, `StrmIconButton`, `StrmImage`, `FocusRing`, `CrateKicker`
- Produces: `MusicPlayerPanel` (module `StrmQt`):
  - `signal leftRequested()`
  - `property string currentTab` (`"upNext" | "album" | "lyrics"`), `readonly property var tabKeys`
  - `function focusTabs(): void`, `function focusContent(): void`

Input (spec §7.2 "two focus zones joined by Left/Right", §8):
- **Tab strip:** Left/Right change tab. Left on the first tab emits `leftRequested`. Down or Return enters the tab's content. Up is not handled, so it reaches `PlayerPage`, which takes the keyboard back.
- **Up next:**
  - Return or click jumps. Delete or the row ✕ removes.
  - Ctrl+Up/Ctrl+Down moves the highlighted entry and keeps it highlighted.
  - Up on the first row goes to the strip.
- **Album:** Return or click plays the album from that row; Up on the first row goes to the strip.
- **Lyrics:** Up/Down scroll, and Up at the top goes to the strip. Timed lyrics keep the current line in the middle band.
- **Hover is never focus.** The ring draws only on `activeFocus`.

- [ ] **Step 1: Write the panel**

`src/ui/music/MusicPlayerPanel.qml`:

```qml
// Bound: the delegates reach this file's ids (panel, the lists).
pragma ComponentBehavior: Bound

import QtQuick
import StrmQt

// MusicPlayerPanel: the full player's side panel (Crate spec §7.2).
//
// Three tabs over what NowPlayingMusicCtl and the queue already hold:
//   Up next  the queue, "Playing from · <source>", jump / remove / reorder
//   Album    the current album's tracklist (cached), playing row marked
//   Lyrics   only when the server provides them; timed lines follow the playhead
//
// Its own tab strip rather than StrmTabBar: StrmTabBar consumes Left/Right for
// itself, and here Left off the first tab has to leave the panel for the stage.
FocusScope {
    id: panel

    // Left off the first tab: the stage takes the keyboard.
    signal leftRequested

    readonly property var queue: {
        const q = PlayerCtl.queue;
        return (q !== undefined && q !== null) ? q : null;
    }
    readonly property int queueCount: panel.queue !== null ? panel.queue.count : 0
    readonly property bool lyricsAvailable: NowPlayingMusicCtl.lyricsAvailable === true
    readonly property var tabKeys: panel.lyricsAvailable ? ["upNext", "album", "lyrics"]
                                                         : ["upNext", "album"]
    property string currentTab: "upNext"

    function tabLabel(key: string): string {
        if (key === "album")
            return qsTr("Album");
        if (key === "lyrics")
            return qsTr("Lyrics");
        return qsTr("Up next");
    }

    function stepTab(step: int): bool {
        const next = panel.tabKeys.indexOf(panel.currentTab) + step;
        if (next < 0 || next >= panel.tabKeys.length)
            return false;
        panel.currentTab = panel.tabKeys[next];
        return true;
    }

    function focusTabs(): void {
        tabStrip.forceActiveFocus(Qt.TabFocusReason);
    }

    function focusContent(): void {
        if (panel.currentTab === "album")
            albumList.forceActiveFocus(Qt.TabFocusReason);
        else if (panel.currentTab === "lyrics")
            lyricList.forceActiveFocus(Qt.TabFocusReason);
        else
            queueList.forceActiveFocus(Qt.TabFocusReason);
    }

    function jump(index: int): void {
        if (panel.queue !== null && index >= 0 && index < panel.queueCount)
            panel.queue.jumpTo(index);
    }

    function remove(index: int): void {
        if (panel.queue !== null && index >= 0 && index < panel.queueCount)
            panel.queue.removeAt(index);
    }

    function move(from: int, to: int): bool {
        if (panel.queue === null || from < 0 || to < 0 || from >= panel.queueCount || to >= panel.queueCount)
            return false;
        panel.queue.moveItem(from, to);
        return true;
    }

    // The Lyrics tab goes when a track without lyrics comes on.
    onTabKeysChanged: {
        if (panel.tabKeys.indexOf(panel.currentTab) < 0)
            panel.currentTab = "upNext";
    }

    // ── Surface ─────────────────────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        color: Theme.surfaceColor
        opacity: 0.82
    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: Theme.hairline
    }

    // Presses and the wheel on the panel's empty space must not reach the
    // page's picture area below, where a click pauses and the wheel is volume.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: wheel => wheel.accepted = true
    }

    // ── Tab strip ───────────────────────────────────────────────────────────
    Item {
        id: tabStrip

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: Theme.spacingLoose
        anchors.rightMargin: Theme.spacingLoose
        anchors.topMargin: Theme.spacingLoose
        height: Theme.controlHeight
        focus: true
        activeFocusOnTab: false

        Accessible.role: Accessible.PageTabList
        Accessible.name: qsTr("Player panel")

        Keys.onLeftPressed: event => {
            if (!panel.stepTab(-1))
                panel.leftRequested();
            event.accepted = true;
        }
        Keys.onRightPressed: event => {
            panel.stepTab(1);
            event.accepted = true;
        }
        Keys.onDownPressed: event => {
            panel.focusContent();
            event.accepted = true;
        }
        Keys.onReturnPressed: event => {
            panel.focusContent();
            event.accepted = true;
        }
        Keys.onEnterPressed: event => {
            panel.focusContent();
            event.accepted = true;
        }

        Row {
            id: tabRow

            height: parent.height
            spacing: Theme.spacingLoose

            Repeater {
                model: panel.tabKeys

                delegate: Item {
                    id: tab

                    required property string modelData
                    readonly property bool selected: tab.modelData === panel.currentTab

                    width: tabText.implicitWidth
                    height: tabRow.height

                    Accessible.role: Accessible.PageTab
                    Accessible.name: tabText.text

                    Text {
                        id: tabText

                        anchors.verticalCenter: parent.verticalCenter
                        text: panel.tabLabel(tab.modelData)
                        color: (tab.selected || tabHover.hovered) ? Theme.textPrimaryColor
                                                                   : Theme.textSecondaryColor
                        font.family: Theme.fontDisplay
                        font.pixelSize: Theme.crateKickerSize
                        font.letterSpacing: Theme.crateKickerSize * Theme.crateKickerTracking
                        font.weight: Font.DemiBold
                        font.capitalization: Font.AllUppercase
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 3
                        color: Theme.accentColor
                        visible: tab.selected
                    }

                    HoverHandler {
                        id: tabHover
                        cursorShape: Qt.PointingHandCursor
                    }

                    TapHandler {
                        gesturePolicy: TapHandler.ReleaseWithinBounds
                        onTapped: {
                            Input.noteInput("mouse");
                            panel.currentTab = tab.modelData;
                        }
                    }

                    FocusRing {
                        active: tabStrip.activeFocus && tab.selected
                        radius: Theme.radiusChip
                        inset: -Theme.scale(4)
                    }
                }
            }
        }
    }

    // ── Tabs ────────────────────────────────────────────────────────────────
    Item {
        id: body

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: tabStrip.bottom
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.spacingValue
        anchors.rightMargin: Theme.spacingValue
        anchors.topMargin: Theme.spacingValue
        anchors.bottomMargin: Theme.spacingValue

        // Up next
        Item {
            anchors.fill: parent
            visible: panel.currentTab === "upNext"

            CrateKicker {
                id: sourceKicker

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: Theme.spacingTight
                text: NowPlayingMusicCtl.sourceKicker
                visible: text.length > 0
            }

            TrackTable {
                id: queueList

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.topMargin: sourceKicker.visible ? sourceKicker.height + Theme.spacingValue : 0
                clip: true
                spacing: Theme.scale(2)
                model: panel.queue
                rowHeight: Theme.scale(58)
                jumpRole: "label"

                onActivated: index => panel.jump(index)

                Keys.onDeletePressed: event => {
                    if (!event.isAutoRepeat)
                        panel.remove(queueList.currentIndex);
                    event.accepted = true;
                }
                Keys.onUpPressed: event => {
                    if (event.modifiers & Qt.ControlModifier) {
                        const from = queueList.currentIndex;
                        if (panel.move(from, from - 1))
                            queueList.currentIndex = from - 1;
                        event.accepted = true;
                    } else if (queueList.currentIndex <= 0) {
                        panel.focusTabs();
                        event.accepted = true;
                    } else {
                        event.accepted = false;
                    }
                }
                Keys.onDownPressed: event => {
                    if (event.modifiers & Qt.ControlModifier) {
                        const from = queueList.currentIndex;
                        if (panel.move(from, from + 1))
                            queueList.currentIndex = from + 1;
                        event.accepted = true;
                    } else {
                        event.accepted = false;
                    }
                }

                onVisibleChanged: {
                    if (queueList.visible && panel.queue !== null)
                        queueList.currentIndex = Math.max(0, panel.queue.currentIndex);
                }

                Text {
                    anchors.centerIn: parent
                    width: parent.width - Theme.spacingValue
                    visible: queueList.count === 0
                    text: qsTr("Nothing queued after this.")
                    color: Theme.textTertiary
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                }

                // The queue's row, as QueuePanel draws it.
                delegate: TrackRow {
                    id: queueRow

                    required property int index
                    required property var model

                    width: queueList.width
                    rowHeight: Theme.scale(58)
                    surfaceBottomMargin: 0
                    showNumber: false
                    showPlayingMarker: true
                    hoverPlayGlyph: false
                    showCover: true
                    coverSize: Theme.scale(40)
                    coverUrl: queueRow.model.posterUrl !== undefined ? String(queueRow.model.posterUrl) : ""
                    title: queueRow.model.label !== undefined ? String(queueRow.model.label) : ""
                    secondary: queueRow.model.subtitle !== undefined ? String(queueRow.model.subtitle) : ""
                    playing: queueRow.model.isCurrent === true
                    current: queueRow.ListView.isCurrentItem && queueList.activeFocus
                    verbsRevealed: queueRow.hovered || queueRow.current

                    onActivated: {
                        queueList.currentIndex = queueRow.index;
                        queueList.forceActiveFocus(Qt.MouseFocusReason);
                        panel.jump(queueRow.index);
                    }

                    StrmIconButton {
                        iconName: "close"
                        round: true
                        size: Theme.scale(28)
                        tooltip: qsTr("Remove from queue")
                        activeFocusOnTab: false
                        onClicked: panel.remove(queueRow.index)
                    }
                }
            }
        }

        // Album
        Item {
            anchors.fill: parent
            visible: panel.currentTab === "album"

            Item {
                id: albumHeader

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: Theme.spacingTight
                height: Theme.scale(52)

                Rectangle {
                    id: albumCover

                    width: parent.height
                    height: parent.height
                    radius: Theme.crateSleeveRadius
                    clip: true
                    color: Theme.surfaceRaisedColor

                    StrmImage {
                        anchors.fill: parent
                        source: NowPlayingMusicCtl.coverUrl
                    }
                }

                Column {
                    anchors.left: albumCover.right
                    anchors.leftMargin: Theme.spacingValue
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.scale(2)

                    Text {
                        width: parent.width
                        text: NowPlayingMusicCtl.album
                        color: Theme.textPrimaryColor
                        font.family: Theme.fontDisplay
                        font.pixelSize: Theme.fontBodySize
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }

                    Text {
                        width: parent.width
                        text: NowPlayingMusicCtl.albumSummary
                        color: Theme.textTertiary
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontCaption
                        font.features: ({ "tnum": 1 })
                        elide: Text.ElideRight
                    }
                }
            }

            TrackTable {
                id: albumList

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: albumHeader.bottom
                anchors.bottom: parent.bottom
                anchors.topMargin: Theme.spacingValue
                clip: true
                spacing: Theme.scale(2)
                model: NowPlayingMusicCtl.albumTracks
                rowHeight: Theme.scale(44)
                jumpRole: "displayTitle"

                onActivated: index => NowPlayingMusicCtl.playAlbumFrom(index)

                Keys.onUpPressed: event => {
                    if (albumList.currentIndex <= 0) {
                        panel.focusTabs();
                        event.accepted = true;
                    } else {
                        event.accepted = false;
                    }
                }

                onVisibleChanged: {
                    if (albumList.visible && NowPlayingMusicCtl.currentAlbumRow >= 0)
                        albumList.currentIndex = NowPlayingMusicCtl.currentAlbumRow;
                }

                // Follow the playing track, but never move a highlight the
                // keyboard is holding.
                Connections {
                    target: NowPlayingMusicCtl

                    function onCurrentAlbumRowChanged() {
                        if (!albumList.activeFocus && NowPlayingMusicCtl.currentAlbumRow >= 0)
                            albumList.currentIndex = NowPlayingMusicCtl.currentAlbumRow;
                    }
                }

                delegate: TrackRow {
                    id: albumRow

                    required property int index
                    required property var model

                    width: albumList.width
                    rowHeight: Theme.scale(44)
                    surfaceBottomMargin: 0
                    showNumber: true
                    showPlayingMarker: true
                    number: Number(albumRow.model.trackNumber) > 0 ? Number(albumRow.model.trackNumber) : -1
                    title: albumRow.model.displayTitle !== undefined ? String(albumRow.model.displayTitle) : ""
                    // Credit only where it differs from the record's own artist.
                    secondary: albumRow.model.differsFromAlbumArtist === true
                               ? String(albumRow.model.artistText) : ""
                    durationText: albumRow.model.durationText !== undefined ? String(albumRow.model.durationText) : ""
                    favorite: albumRow.model.favourite === true
                    playing: albumRow.index === NowPlayingMusicCtl.currentAlbumRow
                    current: albumRow.ListView.isCurrentItem && albumList.activeFocus

                    onActivated: {
                        albumList.currentIndex = albumRow.index;
                        albumList.forceActiveFocus(Qt.MouseFocusReason);
                        NowPlayingMusicCtl.playAlbumFrom(albumRow.index);
                    }
                }
            }
        }

        // Lyrics
        Item {
            anchors.fill: parent
            visible: panel.currentTab === "lyrics"

            ListView {
                id: lyricList

                readonly property bool timed: NowPlayingMusicCtl.lyricsTimed === true
                readonly property int step: Theme.scale(48)

                anchors.fill: parent
                anchors.leftMargin: Theme.spacingTight
                anchors.rightMargin: Theme.spacingTight
                clip: true
                activeFocusOnTab: false
                boundsBehavior: Flickable.StopAtBounds
                spacing: Theme.spacingTight
                model: NowPlayingMusicCtl.lyrics
                currentIndex: lyricList.timed ? NowPlayingMusicCtl.currentLyricRow : -1
                highlightRangeMode: lyricList.timed ? ListView.ApplyRange : ListView.NoHighlightRange
                preferredHighlightBegin: Math.round(lyricList.height * 0.4)
                preferredHighlightEnd: Math.round(lyricList.height * 0.6)
                highlightMoveDuration: Theme.animSlow

                Keys.onUpPressed: event => {
                    if (lyricList.atYBeginning)
                        panel.focusTabs();
                    else
                        lyricList.contentY = Math.max(lyricList.originY, lyricList.contentY - lyricList.step);
                    event.accepted = true;
                }
                Keys.onDownPressed: event => {
                    if (!lyricList.atYEnd)
                        lyricList.contentY = Math.min(lyricList.originY + lyricList.contentHeight - lyricList.height,
                                                      lyricList.contentY + lyricList.step);
                    event.accepted = true;
                }

                delegate: Text {
                    id: lyricLine

                    required property int index
                    required property var modelData
                    readonly property bool isCurrent: lyricList.timed
                                                      && lyricLine.index === NowPlayingMusicCtl.currentLyricRow

                    width: lyricList.width
                    text: lyricLine.modelData.text !== undefined ? String(lyricLine.modelData.text) : ""
                    wrapMode: Text.WordWrap
                    color: !lyricList.timed ? Theme.textSecondaryColor
                         : lyricLine.isCurrent ? Theme.textPrimaryColor : Theme.textTertiary
                    font.family: Theme.fontDisplay
                    font.pixelSize: lyricList.timed ? Theme.fontTitle : Theme.fontBodySize
                    font.weight: lyricLine.isCurrent ? Font.DemiBold : Font.Normal

                    Behavior on color {
                        ColorAnimation {
                            duration: Theme.animFastMs
                            easing.type: Theme.easeStandard
                        }
                    }
                }
            }

            FocusRing {
                active: lyricList.activeFocus
                radius: Theme.radiusChip
            }
        }
    }
}
```

Add `ui/music/MusicPlayerPanel.qml` to the `QML_FILES` list in `src/ui/music/Music.cmake`.

- [ ] **Step 2: Build and lint**

```bash
cmake --build --preset dev
bash scripts/check-qmllint-baseline.sh build/dev
```

Expected:
- The build is clean, since `qmlcachegen` compiles the file.
- The only new lint fingerprints for `MusicPlayerPanel.qml` are `Unqualified access [unqualified]` on `PlayerCtl`, `NowPlayingMusicCtl` and `Input` (Contract note 8).
- Fix any other warning, such as a missing property or a type mismatch, before continuing.

- [ ] **Step 3: Commit**

```bash
git add src/ui/music/MusicPlayerPanel.qml src/ui/music/Music.cmake
git commit -m "feat(music): add the player side panel with up next, album and lyrics

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 7: `MusicNowPlaying`, the `PlayerPage` swap, and removing `NowPlayingPanel`

**Files:**
- Create: `src/ui/music/MusicNowPlaying.qml`
- Modify: `src/ui/music/Music.cmake`, `src/ui/pages/PlayerPage.qml`, `src/ui/player/Player.cmake`, `src/ui/shell/SleeveFlight.qml` (comment only)
- Delete: `src/ui/player/NowPlayingPanel.qml`
- Test: `tests/unit/tst_music_player_sources.cpp`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes:
  - `RecordStage` (Task 5), `MusicPlayerPanel` (Task 6), `NowPlayingMusicCtl` (Tasks 3–4), `Prefs.animateRecord` (Task 1)
  - `CrateKicker`, `CrateHeading` (Phase 2)
  - `MusicPlay.radio` (Phase 1)
  - `Actions.openAlbum`/`openArtist`, `PlayerCtl`, `PlaylistCtl`, `PlaylistPicker`, `StrmMenu`, `StrmSlider`, `StrmIconButton`, `StrmToastHost`, `CoverWash`
- Produces: `MusicNowPlaying` (module `StrmQt`):
  - `signal leaveRequested()`
  - `property bool sleeveInFlight`, `property bool live`
  - `function sleeveRect(target: Item): rect`, `readonly property real sleeveRadius`
  - `function focusTransport(): void`, `function focusScrubber(): void`

  `PlayerPage` keeps its whole public surface (`sleeveRect`, `sleeveRadius`, `sleeveInFlight`, `minimizeRequested`), so `Main.qml`'s flight code is untouched.

Layout (spec §7.2):
- **Ground:** opaque `Theme.ground` with the cover wash (clamp unchanged).
- **Stage zone (left):**
  - source kicker
  - `RecordStage`, with `sleeveSize = min(scale(420), the height left, width / 1.45)`
  - `CrateHeading` title at `crateHeroAlbum`
  - artist · album links (pointer; the ⋯ menu carries both for the keyboard)
  - transport ⇄ ⏮ ⏯ ⏭ ↻ ♡ ＋ ⋯
  - scrubber with the mono `timeText`
  - readout, which pulses "BUFFERING"
- **Panel (right):** 440 px from `scale(1100)` wide, else 40%.
- **Back (top left):** pointer-only; the keyboard leaves through `player.back` as before.
- **Keys:**
  - The page's Down calls `focusTransport()`.
  - In the transport row, Left/Right walk the buttons, Down goes to the scrubber, and Right on ⋯ enters the panel.
  - The scrubber's Up returns to ⏯.
  - The panel's `leftRequested` returns to ⋯.
  - Up from the transport row is not handled here, so `PlayerPage` takes the keyboard back as it did before.

- [ ] **Step 1: Write the failing source-contract test**

`tests/unit/tst_music_player_sources.cpp`:

```cpp
#include <QFile>
#include <QTest>

namespace {

QByteArray sourceFor(const QString &relative)
{
    QFile file(QStringLiteral(STRMQT_SOURCE_DIR "/") + relative);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

} // namespace

// The wiring between the player page, the Crate view and the controller.
// Behaviour is covered by tst_record_stage and tst_now_playing_music; these
// rows pin the bindings that connect them, which no C++ test can reach.
class MusicPlayerSourcesTest : public QObject
{
    Q_OBJECT

private slots:
    void playerPageUsesTheCrateView();
    void nowPlayingViewDrivesTheRecord();
    void nowPlayingPanelIsGone();
};

void MusicPlayerSourcesTest::playerPageUsesTheCrateView()
{
    const QByteArray page = sourceFor(QStringLiteral("src/ui/pages/PlayerPage.qml"));
    QVERIFY(!page.isEmpty());
    QVERIFY(page.contains("MusicNowPlaying {"));
    QVERIFY(page.contains("live: page.visible && page.audioMode"));
    QVERIFY(page.contains("return nowPlaying.sleeveRect(target);"));
    QVERIFY(page.contains("readonly property real sleeveRadius: nowPlaying.sleeveRadius"));
    QVERIFY(page.contains("nowPlaying.focusTransport();"));
    QVERIFY(!page.contains("NowPlayingPanel"));
}

void MusicPlayerSourcesTest::nowPlayingViewDrivesTheRecord()
{
    const QByteArray view = sourceFor(QStringLiteral("src/ui/music/MusicNowPlaying.qml"));
    QVERIFY(!view.isEmpty());
    QVERIFY(view.contains("recordState: NowPlayingMusicCtl.recordState"));
    QVERIFY(view.contains("coverUrl: NowPlayingMusicCtl.coverUrl"));
    QVERIFY(view.contains("holdIn: view.sleeveInFlight"));
    QVERIFY(view.contains("function onAlbumChanging()"));
    QVERIFY(view.contains("record.changeAlbum(NowPlayingMusicCtl.coverUrl)"));
    QVERIFY(view.contains("Prefs.animateRecord"));
    QVERIFY(view.contains("MusicPlayerPanel {"));
    QVERIFY(view.contains("MusicPlay.radio(NowPlayingMusicCtl.trackId, NowPlayingMusicCtl.title)"));
    // QML states intent; it does not format durations or badges itself.
    QVERIFY(view.contains("text: NowPlayingMusicCtl.timeText"));
    QVERIFY(!view.contains("formatTime("));
}

void MusicPlayerSourcesTest::nowPlayingPanelIsGone()
{
    QVERIFY(!QFile::exists(QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/player/NowPlayingPanel.qml")));
    QVERIFY(!sourceFor(QStringLiteral("src/ui/player/Player.cmake")).contains("NowPlayingPanel"));
    QVERIFY(!sourceFor(QStringLiteral("src/ui/shell/SleeveFlight.qml")).contains("NowPlayingPanel"));
}

QTEST_GUILESS_MAIN(MusicPlayerSourcesTest)
#include "tst_music_player_sources.moc"
```

Add to `tests/CMakeLists.txt`, after `tst_record_stage`:

```cmake
strmqt_add_test(tst_music_player_sources unit/tst_music_player_sources.cpp)
target_compile_definitions(tst_music_player_sources PRIVATE STRMQT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --preset dev && cmake --build --preset dev --target tst_music_player_sources && ctest --preset dev -R tst_music_player_sources --output-on-failure`
Expected: FAIL at `playerPageUsesTheCrateView`, on `page.contains("MusicNowPlaying {")`.

- [ ] **Step 3: Write `MusicNowPlaying.qml`**

`src/ui/music/MusicNowPlaying.qml`:

```qml
pragma ComponentBehavior: Bound

import QtQuick
import StrmQt

// MusicNowPlaying: the full player for a record (Crate spec §7.2, "Out of the
// sleeve"). PlayerPage shows it in audio mode in place of the video surface.
//
// Stage on the left (the record, what is playing, transport, scrubber, readout),
// MusicPlayerPanel on the right. Every value on screen comes finished from
// NowPlayingMusicCtl or PlayerCtl; this file lays them out and states intent.
//
// Pointer presses it does not claim fall through to PlayerPage's picture area,
// which is how click-to-pause and the volume wheel keep working here.
FocusScope {
    id: view

    // Back to wherever the player was opened from, playback continuing.
    signal leaveRequested

    // The shared sleeve is in the air (Main.qml's SleeveFlight).
    property bool sleeveInFlight: false
    // The player page is current and in audio mode: the record may turn.
    property bool live: true

    readonly property bool buffering: NowPlayingMusicCtl.recordState === "buffering"
    readonly property var queue: {
        const q = PlayerCtl.queue;
        return (q !== undefined && q !== null) ? q : null;
    }
    readonly property bool shuffled: view.queue !== null && view.queue.shuffled === true
    readonly property int repeatMode: view.queue !== null ? Number(view.queue.repeatMode) : 0
    readonly property bool animateRecord: typeof Prefs !== "undefined" ? Prefs.animateRecord : true

    // The transition's large endpoint, in `target`'s coordinates.
    function sleeveRect(target: Item): rect {
        return record.sleeveRect(target);
    }

    readonly property real sleeveRadius: record.sleeveRadius

    function focusTransport(): void {
        playPause.forceActiveFocus(Qt.TabFocusReason);
    }

    function focusScrubber(): void {
        scrubber.forceActiveFocus(Qt.TabFocusReason);
    }

    function runMenu(key: string): void {
        switch (key) {
        case "album":
            Actions.openAlbum(NowPlayingMusicCtl.albumId, NowPlayingMusicCtl.album);
            break;
        case "artist":
            Actions.openArtist(NowPlayingMusicCtl.artistId, NowPlayingMusicCtl.artist);
            break;
        case "radio":
            MusicPlay.radio(NowPlayingMusicCtl.trackId, NowPlayingMusicCtl.title);
            break;
        case "stop":
            PlayerCtl.stop(); // Main pops the page on stopped()
            break;
        default:
            break;
        }
    }

    // A credit on the stage. Pointer-only: the ⋯ menu is the keyboard's route.
    component CreditLink: Text {
        id: link

        property bool linked: true
        signal activated

        color: (linkHover.hovered && link.linked) ? Theme.textPrimaryColor : Theme.textSecondaryColor
        font.family: Theme.fontBody
        font.pixelSize: Theme.fontBodySize
        font.underline: linkHover.hovered && link.linked
        elide: Text.ElideRight

        HoverHandler {
            id: linkHover
            enabled: link.linked
            cursorShape: Qt.PointingHandCursor
        }

        TapHandler {
            enabled: link.linked
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: {
                Input.noteInput("mouse");
                link.activated();
            }
        }
    }

    // ── Ground ──────────────────────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        color: Theme.ground
    }

    CoverWash {
        anchors.fill: parent
        source: NowPlayingMusicCtl.coverUrl
    }

    Connections {
        target: NowPlayingMusicCtl

        // Emitted with coverUrl already moved and before any binding sees it.
        function onAlbumChanging() {
            record.changeAlbum(NowPlayingMusicCtl.coverUrl);
        }
    }

    // ── Stage ───────────────────────────────────────────────────────────────
    Item {
        id: stageZone

        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: panel.left

        Column {
            id: stageColumn

            // Everything in the column except the record, so the record takes
            // what height is left and never pushes the transport off screen.
            readonly property real reserved: kicker.height + titleText.height + credits.height
                                             + transport.height + scrubberRow.height + readout.height
                                             + 6 * stageColumn.spacing + 2 * Theme.spacingLoose

            anchors.centerIn: parent
            width: Math.max(0, Math.min(stageZone.width - 2 * Theme.pageMarginValue, Theme.scale(640)))
            spacing: Theme.spacingValue

            CrateKicker {
                id: kicker

                width: parent.width
                text: NowPlayingMusicCtl.sourceKicker
            }

            RecordStage {
                id: record

                sleeveSize: Math.max(Theme.scale(160),
                                     Math.min(Theme.scale(420),
                                              stageZone.height - stageColumn.reserved,
                                              stageColumn.width / 1.45))
                coverUrl: NowPlayingMusicCtl.coverUrl
                recordState: NowPlayingMusicCtl.recordState
                animate: view.animateRecord
                live: view.live
                holdIn: view.sleeveInFlight
            }

            CrateHeading {
                id: titleText

                width: parent.width
                text: NowPlayingMusicCtl.title
                pixelSize: Theme.crateHeroAlbum
            }

            Row {
                id: credits

                width: parent.width
                spacing: Theme.spacingTight

                CreditLink {
                    id: artistLink

                    text: NowPlayingMusicCtl.artist
                    linked: NowPlayingMusicCtl.artistId.length > 0
                    width: Math.min(implicitWidth, Math.round((credits.width - creditDot.width) / 2))
                    onActivated: Actions.openArtist(NowPlayingMusicCtl.artistId, NowPlayingMusicCtl.artist)
                }

                Text {
                    id: creditDot

                    visible: artistLink.text.length > 0 && albumLink.text.length > 0
                    text: "·"
                    color: Theme.textTertiary
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontBodySize
                }

                CreditLink {
                    id: albumLink

                    text: NowPlayingMusicCtl.album
                    linked: NowPlayingMusicCtl.albumId.length > 0
                    width: Math.min(implicitWidth, credits.width - artistLink.width - creditDot.width
                                                   - 2 * credits.spacing)
                    onActivated: Actions.openAlbum(NowPlayingMusicCtl.albumId, NowPlayingMusicCtl.album)
                }
            }

            // ⇄ ⏮ ⏯ ⏭ ↻ ♡ ＋ ⋯
            Row {
                id: transport

                spacing: Theme.spacingTight

                StrmIconButton {
                    id: shuffleButton

                    anchors.verticalCenter: parent.verticalCenter
                    size: Theme.scale(40)
                    activeFocusOnTab: false
                    iconName: "shuffle"
                    tooltip: view.shuffled ? qsTr("Shuffle on") : qsTr("Shuffle off")
                    checked: view.shuffled
                    enabled: view.queue !== null
                    onClicked: view.queue.shuffled = !view.queue.shuffled

                    KeyNavigation.right: prevButton
                    KeyNavigation.down: scrubber
                }

                StrmIconButton {
                    id: prevButton

                    anchors.verticalCenter: parent.verticalCenter
                    size: Theme.scale(40)
                    activeFocusOnTab: false
                    iconName: "skip-previous"
                    tooltip: qsTr("Previous")
                    enabled: PlayerCtl.hasPrevious === true
                    onClicked: PlayerCtl.playPrevious()

                    KeyNavigation.left: shuffleButton
                    KeyNavigation.right: playPause
                    KeyNavigation.down: scrubber
                }

                StrmIconButton {
                    id: playPause

                    anchors.verticalCenter: parent.verticalCenter
                    size: Theme.scale(56)
                    round: true
                    focus: true
                    activeFocusOnTab: false
                    iconName: PlayerCtl.paused === true ? "play" : "pause"
                    tooltip: PlayerCtl.paused === true ? qsTr("Play") : qsTr("Pause")
                    onClicked: PlayerCtl.togglePause()

                    KeyNavigation.left: prevButton
                    KeyNavigation.right: nextButton
                    KeyNavigation.down: scrubber
                }

                StrmIconButton {
                    id: nextButton

                    anchors.verticalCenter: parent.verticalCenter
                    size: Theme.scale(40)
                    activeFocusOnTab: false
                    iconName: "skip-next"
                    tooltip: qsTr("Next")
                    enabled: PlayerCtl.hasNext === true
                    onClicked: PlayerCtl.playNext()

                    KeyNavigation.left: playPause
                    KeyNavigation.right: repeatButton
                    KeyNavigation.down: scrubber
                }

                StrmIconButton {
                    id: repeatButton

                    anchors.verticalCenter: parent.verticalCenter
                    size: Theme.scale(40)
                    activeFocusOnTab: false
                    iconName: view.repeatMode === 2 ? "repeat-one" : "repeat"
                    tooltip: view.repeatMode === 0 ? qsTr("Repeat off")
                           : view.repeatMode === 1 ? qsTr("Repeat all")
                           : qsTr("Repeat one")
                    checked: view.repeatMode !== 0
                    enabled: view.queue !== null
                    onClicked: view.queue.cycleRepeatMode()

                    KeyNavigation.left: nextButton
                    KeyNavigation.right: favouriteButton
                    KeyNavigation.down: scrubber
                }

                StrmIconButton {
                    id: favouriteButton

                    anchors.verticalCenter: parent.verticalCenter
                    size: Theme.scale(40)
                    activeFocusOnTab: false
                    iconName: NowPlayingMusicCtl.favourite ? "heart-filled" : "heart"
                    tooltip: NowPlayingMusicCtl.favourite ? qsTr("Remove from favourites")
                                                          : qsTr("Add to favourites")
                    checked: NowPlayingMusicCtl.favourite
                    enabled: NowPlayingMusicCtl.trackId.length > 0
                    onClicked: NowPlayingMusicCtl.toggleFavourite()

                    KeyNavigation.left: repeatButton
                    KeyNavigation.right: addButton
                    KeyNavigation.down: scrubber
                }

                StrmIconButton {
                    id: addButton

                    anchors.verticalCenter: parent.verticalCenter
                    size: Theme.scale(40)
                    activeFocusOnTab: false
                    iconName: "plus"
                    tooltip: qsTr("Add to playlist")
                    enabled: NowPlayingMusicCtl.trackId.length > 0
                    onClicked: playlistPicker.show(NowPlayingMusicCtl.title, [NowPlayingMusicCtl.trackId])

                    KeyNavigation.left: favouriteButton
                    KeyNavigation.right: moreButton
                    KeyNavigation.down: scrubber
                }

                StrmIconButton {
                    id: moreButton

                    anchors.verticalCenter: parent.verticalCenter
                    size: Theme.scale(40)
                    activeFocusOnTab: false
                    iconName: "more-horizontal"
                    tooltip: qsTr("More")
                    onClicked: {
                        const corner = moreButton.mapToItem(null, 0, moreButton.height);
                        moreMenu.popupAt(corner.x, corner.y);
                    }

                    KeyNavigation.left: addButton
                    KeyNavigation.down: scrubber
                    Keys.onRightPressed: event => {
                        panel.focusTabs();
                        event.accepted = true;
                    }
                }
            }

            Item {
                id: scrubberRow

                width: parent.width
                height: Theme.controlHeight

                StrmSlider {
                    id: scrubber

                    anchors.left: parent.left
                    anchors.right: timeLabel.left
                    anchors.rightMargin: Theme.spacingValue
                    anchors.verticalCenter: parent.verticalCenter
                    activeFocusOnTab: false
                    enabled: PlayerCtl.durationMs > 0
                    from: 0
                    to: Math.max(1, PlayerCtl.durationMs)
                    value: PlayerCtl.positionMs
                    buffered: PlayerCtl.bufferedEndMs
                    stepSize: 10000
                    armToScrub: true
                    accessibleName: qsTr("Playback position")
                    accessibleDescription: NowPlayingMusicCtl.timeText

                    onCommitted: v => PlayerCtl.seekTo(Math.round(v))

                    KeyNavigation.up: playPause
                }

                Text {
                    id: timeLabel

                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: NowPlayingMusicCtl.timeText
                    color: Theme.textTertiary
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontCaption
                    font.features: ({ "tnum": 1 })
                }
            }

            Text {
                id: readout

                width: parent.width
                text: view.buffering ? qsTr("Buffering") : NowPlayingMusicCtl.readout
                color: Theme.textTertiary
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontCaption
                font.capitalization: Font.AllUppercase
                font.letterSpacing: Theme.fontCaption * Theme.crateKickerTracking
                elide: Text.ElideRight

                SequentialAnimation on opacity {
                    running: view.buffering && !Theme.reducedMotion
                    loops: Animation.Infinite
                    alwaysRunToEnd: true

                    NumberAnimation {
                        to: 0.35
                        duration: Theme.animAmbient / 2
                        easing.type: Easing.InOutSine
                    }
                    NumberAnimation {
                        to: 1.0
                        duration: Theme.animAmbient / 2
                        easing.type: Easing.InOutSine
                    }
                }
            }
        }
    }

    // ── Panel ───────────────────────────────────────────────────────────────
    MusicPlayerPanel {
        id: panel

        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: view.width >= Theme.scale(1100) ? Theme.scale(440) : Math.round(view.width * 0.4)

        onLeftRequested: moreButton.forceActiveFocus(Qt.BacktabFocusReason)
    }

    // ── Back ────────────────────────────────────────────────────────────────
    StrmIconButton {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: Theme.spacingLoose
        size: Theme.scale(40)
        round: true
        activeFocusOnTab: false
        iconName: "arrow-left"
        tooltip: qsTr("Back")
        onClicked: view.leaveRequested()
    }

    // ── Menus and pickers ───────────────────────────────────────────────────
    StrmMenu {
        id: moreMenu

        actions: [
            { "key": "album", "text": qsTr("Go to album"), "iconName": "lib-music",
              "enabled": NowPlayingMusicCtl.albumId.length > 0 },
            { "key": "artist", "text": qsTr("Go to artist"), "iconName": "user",
              "enabled": NowPlayingMusicCtl.artistId.length > 0 },
            { "key": "radio", "text": qsTr("Radio"), "iconName": "playlist",
              "enabled": NowPlayingMusicCtl.trackId.length > 0 },
            { "separator": true },
            { "key": "stop", "text": qsTr("Stop"), "iconName": "stop" }
        ]

        onTriggered: index => view.runMenu(String(moreMenu.actions[index].key))
    }

    PlaylistPicker {
        id: playlistPicker

        z: 800
        mediaType: "Audio"
        onDismissed: addButton.forceActiveFocus(Qt.OtherFocusReason)
    }

    // `pending` tells this view's result apart from a playlist edited elsewhere.
    Connections {
        target: PlaylistCtl

        function onActionSucceeded(message) {
            if (!playlistPicker.pending)
                return;
            playlistPicker.pending = false;
            toasts.show(message, "success");
        }
        function onActionFailed(message) {
            if (!playlistPicker.pending)
                return;
            playlistPicker.pending = false;
            toasts.show(message, "error");
        }
    }

    StrmToastHost {
        id: toasts

        anchors.fill: parent
        z: 900
    }
}
```

Add `ui/music/MusicNowPlaying.qml` to the `QML_FILES` list in `src/ui/music/Music.cmake`.

- [ ] **Step 4: Swap it into `PlayerPage`**

In `src/ui/pages/PlayerPage.qml`, make these edits.

1. In the header comment, replace `swaps the video surface for NowPlayingPanel` with `swaps the video surface for MusicNowPlaying`.
2. Replace the property comment `Passed through to the\n    // now-playing panel, which owns the square this hides.` with `Passed through to\n    // MusicNowPlaying, whose record stage owns the square this hides.`
3. Replace:

```qml
    function sleeveRect(target: Item): rect {
        if (!page.audioMode)
            return Qt.rect(0, 0, 0, 0);
        return nowPlaying.heroArtRect(target);
    }

    readonly property real sleeveRadius: nowPlaying.heroArtRadius
```

with:

```qml
    function sleeveRect(target: Item): rect {
        if (!page.audioMode)
            return Qt.rect(0, 0, 0, 0);
        return nowPlaying.sleeveRect(target);
    }

    readonly property real sleeveRadius: nowPlaying.sleeveRadius
```

4. Replace:

```qml
    NowPlayingPanel {
        id: nowPlaying

        anchors.fill: parent
        visible: page.audioMode
        enabled: page.audioMode
        sleeveInFlight: page.sleeveInFlight
        onLeaveRequested: page.minimizeRequested()
    }
```

with:

```qml
    MusicNowPlaying {
        id: nowPlaying

        anchors.fill: parent
        visible: page.audioMode
        enabled: page.audioMode
        sleeveInFlight: page.sleeveInFlight
        // The record turns only while this page is the one on screen.
        live: page.visible && page.audioMode
        onLeaveRequested: page.minimizeRequested()
    }
```

5. In the OSD comment, replace `NowPlayingPanel is the permanent control surface instead, and it\n        // carries the scrubber, the transport, shuffle/repeat, volume and the\n        // queue that the OSD would have owned.` with `MusicNowPlaying is the permanent control surface instead, and it\n        // carries the scrubber, the transport, shuffle/repeat and the queue\n        // that the OSD would have owned.`
6. Replace:

```qml
    Keys.onDownPressed: event => {
        if (page.audioMode)
            nowPlaying.focusScrubber();
```

with:

```qml
    Keys.onDownPressed: event => {
        if (page.audioMode)
            nowPlaying.focusTransport(); // the transport sits above the scrubber
```

- [ ] **Step 5: Remove `NowPlayingPanel`**

```bash
git rm src/ui/player/NowPlayingPanel.qml
sed -i '/ui\/player\/NowPlayingPanel.qml/d' src/ui/player/Player.cmake
grep -rn "NowPlayingPanel" src tests docs --include=*.qml --include=*.cpp --include=*.h --include=*.cmake --include=CMakeLists.txt --include=*.md | grep -v "docs/superpowers/"
```

Expected: the only remaining hit is `src/ui/shell/SleeveFlight.qml`. In that file, replace `NowPlayingPanel's hero casts its shadow from a separate flat rectangle` with `RecordStage's sleeve casts its shadow from a separate flat rectangle`, then run the `grep` again. Expected: no output.

- [ ] **Step 6: Verify the music-only `MediaItemModel` roles (removal candidates)**

```bash
for role in artists artistIds albumArtist album albumId childCount; do
  printf '%-12s ' "$role"
  grep -rlE "\b${role}\b" src tests --include=*.qml --include=*.js --include=*.cpp --include=*.h \
    | grep -v "src/app/models/MediaItemModel" | wc -l
done
grep -nE "ArtistsRole|ArtistIdsRole|AlbumArtistRole|AlbumRole|AlbumIdRole|ChildCountRole" src/app/music/models/TrackListModel.cpp src/app/PlayQueue.cpp src/remote -r
```

Expected:
- Every role prints a count above 0.
- The second command shows `PlayQueue`, the web remote and/or `TrackListModel` reading them.
- `TrackListModel` serves every media role by contract, and `PlayQueue::itemAt`, MPRIS and the web remote read `artists`/`album`/`albumId`.

In that case nothing is removed; record it in the commit body. If a role prints 0, remove its enum value from `MediaItemModel.h`, its `roleNames` row and its `data()` case, rebuild, and run `ctest --preset dev --output-on-failure`.

- [ ] **Step 7: Run the tests and lint**

```bash
cmake --build --preset dev
ctest --preset dev -R "tst_music_player_sources|tst_record_stage|tst_navigation_history|tst_input_map" --output-on-failure
bash scripts/check-qmllint-baseline.sh build/dev
STRMQT_SELFTEST=1 QT_QPA_PLATFORM=offscreen QT_ASSUME_STDERR_HAS_CONSOLE=1 ./build/dev/strmqt; echo "selftest exit $?"
```

Expected:
- All four tests pass, and `selftest exit 0`.
- `NowPlayingPanel.qml`'s baseline fingerprints are listed as removed.
- The new fingerprints for `MusicNowPlaying.qml` and `PlayerPage.qml` are only `Unqualified access [unqualified]`, on `PlayerCtl`, `NowPlayingMusicCtl`, `Actions`, `MusicPlay`, `PlaylistCtl`, `Prefs` and `Input`. Fix anything else. Task 9 accepts these.

- [ ] **Step 8: Commit**

```bash
git add src/ui/music/MusicNowPlaying.qml src/ui/music/Music.cmake src/ui/pages/PlayerPage.qml \
        src/ui/player/Player.cmake src/ui/shell/SleeveFlight.qml \
        tests/unit/tst_music_player_sources.cpp tests/CMakeLists.txt
git commit -m "feat(music): play records out of the sleeve in the full player

Replaces NowPlayingPanel with MusicNowPlaying: the record stage, the
Crate title, transport and readout, and the side panel. The music-only
MediaItemModel roles all still have readers (PlayQueue, MPRIS, the web
remote, TrackListModel), so none are removed.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 8: The docked audio bar

**Files:**
- Modify: `src/ui/shell/MiniPlayer.qml`
- Test: `tests/unit/tst_mini_player_sources.cpp`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `NowPlayingMusicCtl.recordState`, `NowPlayingMusicCtl.timeText` (Task 4); the existing `mini.*` state (`positionMs`, `durationMs`, `seekable`, `openAlbum`, `albumId`, `sleeveInFlight`).
- Produces: no new public API. `dockedArtRect`, `artRadius`, `focusTransport`, `reservedHeight` and `expandRequested` keep their meaning. Audio-mode geometry changes: the bar is `scale(2) + scale(72)` tall and the square is 72 px.

Spec §7.1:
- **Layout:** a 72 px cover flush left, then the title (which links to the album), then "artist · album" links.
- **Controls:** ⇄ ⏮ ⏯ ⏭ ↻ · mono "elapsed / total" · ♡ · queue peek · stop · expand.
- **Playhead:** a 2 px amber line on the top edge (hairline track, amber fill, a 10 px pointer seek zone).
- **Record slice:** while playing, a slice of record shows past the cover's right edge (`scale(10)`, `animNormalMs`) and retracts on pause. It is hidden during the sleeve flight.
- **Video:** the bar is unchanged. It keeps the hairline edge, the 16 px `StrmSlider`, the inset art, the Stop in the transport row, and "elapsed / −remaining".

The `tst_navigation_history` rows for this file (`Actions.openArtist(mini.artistId, mini.artistText)`, `Actions.openAlbum(mini.albumId, mini.albumText)`) must keep passing; none of the edits below touch those lines.

- [ ] **Step 1: Write the failing source-contract test**

`tests/unit/tst_mini_player_sources.cpp`:

```cpp
#include <QFile>
#include <QTest>

namespace {

QByteArray sourceFor(const QString &relative)
{
    QFile file(QStringLiteral(STRMQT_SOURCE_DIR "/") + relative);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

} // namespace

// The docked audio bar (Crate spec §7.1). Pins the audio-only branches so the
// video bar provably keeps its own.
class MiniPlayerSourcesTest : public QObject
{
    Q_OBJECT

private slots:
    void audioBarIsTheCrateBar();
    void videoBarIsUnchanged();
};

void MiniPlayerSourcesTest::audioBarIsTheCrateBar()
{
    const QByteArray mini = sourceFor(QStringLiteral("src/ui/shell/MiniPlayer.qml"));
    QVERIFY(!mini.isEmpty());
    QVERIFY(mini.contains("readonly property int audioCoverSize: Theme.scale(72)"));
    QVERIFY(mini.contains("readonly property int playheadHeight: Theme.scale(2)"));
    QVERIFY(mini.contains("mini.isAudio ? mini.playheadHeight + mini.audioCoverSize"));
    QVERIFY(mini.contains("id: playhead"));
    QVERIFY(mini.contains("id: recordSlice"));
    QVERIFY(mini.contains("mini.isAudio && NowPlayingMusicCtl.recordState === \"playing\""));
    QVERIFY(mini.contains("id: audioStopButton"));
    QVERIFY(mini.contains("text: mini.isAudio ? NowPlayingMusicCtl.timeText : mini.timeText"));
    // Shuffle leads the transport row.
    QVERIFY(mini.indexOf("id: shuffleButton") < mini.indexOf("id: prevButton"));
    // The title opens the record in audio mode.
    QVERIFY(mini.contains("if (mini.isAudio && mini.albumId.length > 0)"));
}

void MiniPlayerSourcesTest::videoBarIsUnchanged()
{
    const QByteArray mini = sourceFor(QStringLiteral("src/ui/shell/MiniPlayer.qml"));
    QVERIFY(mini.contains("1 + mini.scrubberHeight + mini.contentHeight"));
    QVERIFY(mini.contains("id: scrubber"));
    QVERIFY(mini.contains("visible: !mini.isAudio"));
    QVERIFY(mini.contains("Actions.openArtist(mini.artistId, mini.artistText)"));
    QVERIFY(mini.contains("Actions.openAlbum(mini.albumId, mini.albumText)"));
}

QTEST_GUILESS_MAIN(MiniPlayerSourcesTest)
#include "tst_mini_player_sources.moc"
```

Add to `tests/CMakeLists.txt`, directly after `strmqt_add_test(tst_media_source unit/tst_media_source.cpp)`. That keeps it away from the lines Tasks 5 and 7 add in the same wave range:

```cmake
strmqt_add_test(tst_mini_player_sources unit/tst_mini_player_sources.cpp)
target_compile_definitions(tst_mini_player_sources PRIVATE STRMQT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --preset dev && cmake --build --preset dev --target tst_mini_player_sources && ctest --preset dev -R tst_mini_player_sources --output-on-failure`
Expected: FAIL at `audioBarIsTheCrateBar`, on `audioCoverSize`.

- [ ] **Step 3: Metrics**

In `src/ui/shell/MiniPlayer.qml`, replace:

```qml
    readonly property int scrubberHeight: Theme.scale(16)
    readonly property int contentHeight: Theme.scale(52)
    readonly property int barHeight: 1 + mini.scrubberHeight + mini.contentHeight
                                     + Theme.spacingTight
```

with:

```qml
    readonly property int scrubberHeight: Theme.scale(16)
    readonly property int contentHeight: Theme.scale(52)
    // The audio bar (Crate spec §7.1): a 2 px playhead over a 72 px cover and
    // nothing else, so the square reaches both the left and the bottom edge.
    readonly property int audioCoverSize: Theme.scale(72)
    readonly property int playheadHeight: Theme.scale(2)
    // How far the record slice shows past the cover while playing.
    readonly property int recordSliceOut: Theme.scale(10)
    readonly property int barHeight: mini.isAudio ? mini.playheadHeight + mini.audioCoverSize
                                                  : 1 + mini.scrubberHeight + mini.contentHeight
                                                    + Theme.spacingTight
    readonly property bool recordOut: mini.isAudio && NowPlayingMusicCtl.recordState === "playing"
```

- [ ] **Step 4: The time template**

Replace:

```qml
    readonly property string timeTemplate: {
        const shape = mini.formatTime(Math.max(mini.durationMs,
                                                NowPlayingInfo.positionSeconds * 1000))
                          .replace(/[0-9]/g, "0");
        return shape + "  /  " + (mini.seekable ? "−" + shape : mini.remainingText);
    }
```

with:

```qml
    readonly property string timeTemplate: {
        const shape = mini.formatTime(Math.max(mini.durationMs,
                                                NowPlayingInfo.positionSeconds * 1000))
                          .replace(/[0-9]/g, "0");
        // Audio draws NowPlayingMusicCtl.timeText, "elapsed / total", whose
        // right-hand side is "--:--" until the duration is known.
        if (mini.isAudio)
            return shape + " / " + (mini.durationMs > 0 ? shape : "--:--");
        return shape + "  /  " + (mini.seekable ? "−" + shape : mini.remainingText);
    }
```

- [ ] **Step 5: The top edge**

Replace the `edge` rectangle and the scrubber's first lines:

```qml
        Rectangle {
            id: edge

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 1
            color: Theme.hairline
        }
```

with:

```qml
        Rectangle {
            id: edge

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: mini.isAudio ? 0 : 1
            visible: !mini.isAudio
            color: Theme.hairline
        }
```

In the `StrmSlider { id: scrubber … }` block, replace:

```qml
            height: mini.scrubberHeight
            enabled: mini.seekable
```

with:

```qml
            // Video only; the audio bar's playhead is the 2 px line below.
            height: mini.isAudio ? 0 : mini.scrubberHeight
            visible: !mini.isAudio
            enabled: mini.seekable && !mini.isAudio
```

- [ ] **Step 6: The content block**

In `Item { id: content … }`, replace:

```qml
            anchors.top: scrubber.bottom
```

with:

```qml
            anchors.top: mini.isAudio ? parent.top : scrubber.bottom
            anchors.topMargin: mini.isAudio ? mini.playheadHeight : 0
```

and replace:

```qml
            height: mini.isAudio ? mini.barHeight - 1 - mini.scrubberHeight
                                 : mini.contentHeight
```

with:

```qml
            height: mini.isAudio ? mini.audioCoverSize : mini.contentHeight
```

- [ ] **Step 7: The record slice**

In `Item { id: identity … }`, insert this block directly **before** `Rectangle { id: artFrame`, so the slice draws under the cover:

```qml
                // A slice of record past the cover's right edge while music
                // plays (Crate spec §7.1); it retracts on pause. Hidden while the
                // sleeve is in the air, like the square it sits behind.
                Rectangle {
                    id: recordSlice

                    property real out: mini.recordOut ? mini.recordSliceOut : 0

                    visible: mini.isAudio && !mini.sleeveInFlight
                    width: Math.round(artFrame.height * 0.9)
                    height: recordSlice.width
                    radius: recordSlice.width / 2
                    x: artFrame.x + artFrame.width - recordSlice.width + recordSlice.out
                    anchors.verticalCenter: artFrame.verticalCenter
                    color: Theme.ground
                    border.width: 1
                    border.color: Theme.hairline

                    Behavior on out {
                        NumberAnimation {
                            duration: Theme.animNormalMs
                            easing.type: Theme.easeStandard
                        }
                    }

                    Rectangle {
                        anchors.centerIn: parent
                        width: Math.round(parent.width * 0.7)
                        height: width
                        radius: width / 2
                        color: "transparent"
                        border.width: 1
                        border.color: Theme.hairline
                        opacity: 0.6
                    }
                }
```

In the same block, replace the `artFrame` size lines:

```qml
                    height: mini.isAudio ? parent.height : Theme.scale(44)
```

with:

```qml
                    height: mini.isAudio ? mini.audioCoverSize : Theme.scale(44)
```

In `Column { id: labels … }`, replace:

```qml
                    anchors.leftMargin: Theme.spacingValue
```

with:

```qml
                    // Clear of the record slice at its widest, so the labels
                    // never move when it slides.
                    anchors.leftMargin: Theme.spacingValue + (mini.isAudio ? mini.recordSliceOut : 0)
```

- [ ] **Step 8: The title opens the album**

In `Item { id: titleLine … }`, replace:

```qml
                        Accessible.role: Accessible.Button
                        Accessible.name: qsTr("Open player: %1").arg(mini.trackTitle)
                        Accessible.onPressAction: mini.expandRequested()
```

with:

```qml
                        Accessible.role: Accessible.Button
                        Accessible.name: mini.isAudio && mini.albumId.length > 0
                                         ? qsTr("Open album: %1").arg(mini.albumText)
                                         : qsTr("Open player: %1").arg(mini.trackTitle)
                        Accessible.onPressAction: titleLine.activate()

                        // Audio: the title links to its record (spec §7.1); the
                        // cover and the expand button open the player.
                        function activate(): void {
                            if (mini.isAudio && mini.albumId.length > 0)
                                mini.openAlbum();
                            else
                                mini.expandRequested();
                        }
```

and, in the same item's `TapHandler`, replace:

```qml
                            onTapped: {
                                Input.noteInput("mouse");
                                mini.expandRequested();
                            }
```

with:

```qml
                            onTapped: {
                                Input.noteInput("mouse");
                                titleLine.activate();
                            }
```

(`artFrame` has the same three `Accessible` lines at 20 spaces and the same `TapHandler` body at 24/28 spaces. Both stay as they are: the cover still opens the player. Match on the `titleLine` copies, which sit at 24 spaces and at 28/32 spaces.)

In `MiniLink { id: albumLink … }`, replace `KeyNavigation.right: prevButton` with `KeyNavigation.right: shuffleButton`.

- [ ] **Step 9: The transport row**

In `Row { id: transport … }`:

1. Cut the whole `StrmIconButton { id: shuffleButton … }` block and paste it as the **first** child of the row, before `StrmIconButton { id: prevButton`. In it, replace:

```qml
                    KeyNavigation.left: nextButton
                    KeyNavigation.right: repeatButton
```

with:

```qml
                    KeyNavigation.left: albumLink
                    KeyNavigation.right: prevButton
```

2. In `prevButton`, replace `KeyNavigation.left: albumLink` with `KeyNavigation.left: shuffleButton`.
3. In `nextButton`, replace `KeyNavigation.right: shuffleButton` with `KeyNavigation.right: repeatButton`.
4. Delete the spacer and its comment:

```qml
                // Queue state is not transport, and the gap says so.
                Item {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: mini.isAudio && !mini.compactWidth
                    width: Theme.spacingValue
                    height: 1
                }
```

5. In `repeatButton`, replace `KeyNavigation.left: shuffleButton` with `KeyNavigation.left: nextButton`.
6. Replace the comment and first lines of `stopButton`:

```qml
                // Stop in both modes: a paused session still holds the bar,
                // and ending it should not mean opening the full player first.
                StrmIconButton {
                    id: stopButton

                    anchors.verticalCenter: parent.verticalCenter
                    size: Theme.scale(34)
```

with:

```qml
                // Stop in the transport for video; the audio bar keeps it on
                // the right, after the queue peek (spec §7.1). Either way a
                // paused session can be ended without opening the full player.
                StrmIconButton {
                    id: stopButton

                    anchors.verticalCenter: parent.verticalCenter
                    size: Theme.scale(34)
                    visible: !mini.isAudio
```

- [ ] **Step 10: The right cluster**

In `Row { id: rightCluster … }`, replace the time `Text`'s `text: mini.timeText` with:

```qml
                    text: mini.isAudio ? NowPlayingMusicCtl.timeText : mini.timeText
```

In `queueButton`, replace `KeyNavigation.right: expandButton` with `KeyNavigation.right: audioStopButton`. Insert this block between `queueButton` and `expandButton`:

```qml
                StrmIconButton {
                    id: audioStopButton

                    anchors.verticalCenter: parent.verticalCenter
                    size: Theme.scale(34)
                    visible: mini.isAudio
                    activeFocusOnTab: false
                    iconName: "stop"
                    tooltip: qsTr("Stop")

                    onClicked: PlayerCtl.stop()

                    KeyNavigation.left: queueButton
                    KeyNavigation.right: expandButton
                    KeyNavigation.up: scrubber
                }
```

In `expandButton`, replace `KeyNavigation.left: queueButton` with `KeyNavigation.left: audioStopButton`. KeyNavigation skips invisible items in the same direction, so video still walks expand → queue (hidden) → favourite (hidden) → stop.

- [ ] **Step 11: The playhead**

Directly after the closing brace of `Item { id: content … }`, still inside `Rectangle { id: bar … }`, add the block below. It is declared after the content so its 10 px seek zone sits above the cover's top edge.

```qml
        // ── Audio playhead ──────────────────────────────────────────────
        // A 2 px amber line on the top edge (Crate spec §7.1). Seekable with
        // the pointer through a 10 px zone; the keyboard seeks through the
        // player's own actions and the full player's scrubber.
        Item {
            id: playhead

            property real dragFraction: -1
            readonly property real fraction: playhead.dragFraction >= 0 ? playhead.dragFraction
                                           : mini.durationMs > 0 ? Math.min(1, mini.positionMs / mini.durationMs)
                                           : 0

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: mini.playheadHeight
            visible: mini.isAudio

            Accessible.role: Accessible.ProgressBar
            Accessible.name: qsTr("Playback position")
            Accessible.description: NowPlayingMusicCtl.timeText

            Rectangle {
                anchors.fill: parent
                color: Theme.hairline
            }

            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: Math.round(parent.width * playhead.fraction)
                color: Theme.accentColor
            }

            MouseArea {
                id: seekZone

                function fractionAt(x: real): real {
                    return Math.max(0, Math.min(1, x / Math.max(1, width)));
                }

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: Theme.scale(10)
                enabled: mini.isAudio && mini.seekable
                cursorShape: Qt.PointingHandCursor

                onPressed: mouse => {
                    Input.noteInput("mouse");
                    playhead.dragFraction = seekZone.fractionAt(mouse.x);
                }
                onPositionChanged: mouse => {
                    if (pressed)
                        playhead.dragFraction = seekZone.fractionAt(mouse.x);
                }
                onReleased: mouse => {
                    PlayerCtl.seekTo(Math.round(seekZone.fractionAt(mouse.x) * mini.durationMs));
                    playhead.dragFraction = -1;
                }
                onCanceled: playhead.dragFraction = -1
            }
        }
```

- [ ] **Step 12: Run the tests and lint**

```bash
cmake --build --preset dev
ctest --preset dev -R "tst_mini_player_sources|tst_navigation_history" --output-on-failure
bash scripts/check-qmllint-baseline.sh build/dev
STRMQT_SELFTEST=1 QT_QPA_PLATFORM=offscreen QT_ASSUME_STDERR_HAS_CONSOLE=1 ./build/dev/strmqt; echo "selftest exit $?"
```

Expected:
- Both tests pass, and `selftest exit 0`.
- The lint script exits non-zero and lists new fingerprints. For `MiniPlayer.qml` those must be only `Unqualified access [unqualified]` on `NowPlayingMusicCtl`; fix anything else. The baseline is refreshed once, in Task 9.

- [ ] **Step 13: Commit**

```bash
git add src/ui/shell/MiniPlayer.qml tests/unit/tst_mini_player_sources.cpp tests/CMakeLists.txt
git commit -m "feat(music): dock records in a Crate audio bar

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 9: Phase gate

**Files:**
- Modify: `config/qmllint-baseline.txt` (only through `--update`)

**Interfaces:** none. This task checks that Phase 5 is whole before Phase 6 starts.

- [ ] **Step 1: Clean build and the full suite**

```bash
rm -rf /tmp/w5*
cmake --preset dev
cmake --build --preset dev 2>&1 | tee build/dev/phase5-build.log | tail -n 5
grep -c "warning:" build/dev/phase5-build.log
ctest --preset dev --output-on-failure
STRMQT_SELFTEST=1 QT_QPA_PLATFORM=offscreen QT_ASSUME_STDERR_HAS_CONSOLE=1 ./build/dev/strmqt; echo "selftest exit $?"
rm -f build/dev/phase5-build.log
```

Expected:
- The build succeeds, and the `grep -c` prints `0`.
- ctest ends with `100% tests passed`. That includes `tst_settings`, `tst_music_mapper`, `tst_music_repository`, `tst_now_playing_music`, `tst_record_stage`, `tst_music_player_sources`, `tst_mini_player_sources`, `tst_navigation_history` and `tst_input_map`.
- `selftest exit 0`.

If a test fails, stop and fix it in the task that owns the file. Do not patch around it here.

- [ ] **Step 2: Review and accept the lint diff**

```bash
bash scripts/check-qmllint-baseline.sh build/dev 2>&1 | grep -E '^[+-][^+-]' | sort | uniq -c | sort -rn
```

Expected: every `+` line is `Unqualified access [unqualified]` in:
- `src/ui/music/MusicNowPlaying.qml`, `src/ui/music/MusicPlayerPanel.qml`
- `src/ui/pages/PlayerPage.qml`
- `src/ui/shell/MiniPlayer.qml`

Each names a context property from Contract note 8. Every `-` line is a `src/ui/player/NowPlayingPanel.qml` fingerprint, or a `PlayerPage.qml` line whose source statement changed.

If any other `+` line appears, fix its source and repeat this step. When the diff is only what is listed above:

```bash
bash scripts/check-qmllint-baseline.sh build/dev --update
bash scripts/check-qmllint-baseline.sh build/dev
```

Expected: `Updated …/config/qmllint-baseline.txt (N warnings).`, then `qmllint warning baseline matches (N warnings).` with exit 0.

- [ ] **Step 3: Manual visual check against spec §7**

Run `./build/dev/strmqt` against a real server. The session comes from SecretsStore; never paste a URL or token into a file or a command line that gets committed. Check each row, and fix any failure in the owning task before committing the gate.

| # | Check | Owner |
|---|---|---|
| 1 | Play an album track. The docked bar shows a 72 px cover flush left and bottom, a 2 px amber playhead that advances, and a record slice past the cover that retracts on pause. | 8 |
| 2 | Docked bar order: ⇄ ⏮ ⏯ ⏭ ↻, then "0:42 / 3:51", ♡, queue, stop, expand. Left/Right walks it without stops on hidden buttons. Clicking the playhead seeks. | 8 |
| 3 | The docked title opens the album; the cover and expand open the player. | 8 |
| 4 | Play a video. The bar is exactly as before: hairline, 16 px scrubber, inset art, stop in the transport, "elapsed / −remaining". | 8 |
| 5 | Expand. The sleeve flies to the stage sleeve, and the record slides out only after it lands. It spins at 1.8 s per turn. | 5, 7 |
| 6 | Pause: the spin settles over about 600 ms and the record stays out. Resume: it spins again. | 5 |
| 7 | Throttle the network until it buffers. The record holds and the readout pulses "BUFFERING". | 3, 7 |
| 8 | Next track on the same album keeps spinning. Next track on another album: slide in, cover crossfade, slide out. | 3, 5 |
| 9 | Stop, or let the queue end. The record slides fully in. | 5 |
| 10 | Collapse. The sleeve flies back to the docked cover. Minimise the window, or leave the player page: the spin stops (`RecordStage.spinning` false) and restarts on return. | 5, 7 |
| 11 | Settings → Appearance → Animate record off: a static, slid-out record with no spin and no slide animation. Turn it on again. | 1, 5 |
| 12 | Enable reduced motion in the OS or app. There are no rotations or slides, and state changes snap. | 5 |
| 13 | Readout reads e.g. "FLAC 24/96 · DIRECT PLAY", and changes to TRANSCODE when a transcode is forced. | 3 |
| 14 | Side panel: Up next shows "Playing from · …"; Delete removes a row and Ctrl+Up/Down moves one. Album lists the tracks with the playing row marked, and Return plays from that row. | 6 |
| 15 | Lyrics tab: present with timed lyrics that follow the playhead on a track that has them; hidden on a track without. If V4 recorded "no lyrics endpoint", the tab never appears. | 2, 3, 6 |
| 16 | Focus: Down from the page reaches the transport. Right from ⋯ reaches the tabs, and Left from the first tab returns. The ⋯ menu's Go to album / Go to artist pop the player and open the page. Radio and Stop work. Space, arrows and gamepad player bindings still work. | 6, 7 |
| 17 | ＋ opens the playlist picker, and adding shows a toast. ♡ toggles and the docked bar's ♡ follows. | 3, 7 |

- [ ] **Step 4: Commit the baseline**

```bash
git add config/qmllint-baseline.txt
git commit -m "chore(qml): accept the Crate player's context property lint

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
rm -rf /tmp/w5*
```

Expected: one commit. `git status --short` shows nothing from Phase 5.

---

## Self-review against spec §7 and §8

### §7.1 Docked bar (audio)

| Spec | Where |
|---|---|
| 72 px square cover flush left | Task 8, Steps 3, 6 and 7 (`audioCoverSize`, `leftMargin: 0`, `artFrame.height`) |
| Title links to album; "artist · album" links | Task 8, Step 8 (`titleLine.activate`). The existing `musicSubline` links are kept, and so are the `tst_navigation_history` strings. |
| ⇄ ⏮ ⏯ ⏭ ↻ · mono elapsed / total · ♡ · queue peek · stop · expand | Task 8, Steps 4, 9 and 10 (shuffle first, gap removed, `NowPlayingMusicCtl.timeText`, `audioStopButton` after queue) |
| 2 px amber playhead on the top edge | Task 8, Step 11 (`playhead`, `Theme.accentColor`); Contract note 9 |
| Record slice while playing, retracts on pause | Task 8, Step 7 (`recordSlice`, `mini.recordOut` from `NowPlayingMusicCtl.recordState`) |
| Video unchanged | Every Task 8 edit is gated on `mini.isAudio`; `videoBarIsUnchanged`; gate row 4 |

### §7.2 Full player (audio)

| Spec | Where |
|---|---|
| 420 px sleeve, record behind, circle-cropped cover label | Task 5 (`sleeveSize` default `Theme.scale(420)`, masked label); Task 7 sizes it down only when the height is short |
| Title in Crate display, artist and album links | Task 7 (`CrateHeading` at `crateHeroAlbum`, `CreditLink`) |
| Transport ⇄ ⏮ ⏯ ⏭ ↻ ♡ ＋ ⋯; ⋯ = album, artist, radio, stop | Task 7 (transport row, `StrmMenu` keys) |
| Scrubber with mono times | Task 7 (scrubber row with `NowPlayingMusicCtl.timeText`) |
| Readout "FLAC 24/96 · DIRECT PLAY" | Task 3 (`readout`, `readoutFromTicketAndMethod`); Task 7 |
| Playing: out ≈ 45%, 1.8 s per turn | Task 5 (`phase` "out", spin `RotationAnimator` 1800 ms) |
| Paused: settles over ≈ 600 ms, stays out | Task 5 (settle animation 600 ms); Task 3 `recordState` "paused" |
| Buffering: holds, readout pulses | Task 3 (`recordState` "buffering"); Task 7 (readout pulse) |
| Same album keeps spinning; different album slides in, crossfades, slides out | Task 3 (`albumChanging` only on an album change, `albumChangingOnlyWhenTheAlbumChanges`); Task 5 (`changeAlbum` sequence); Task 7 (Connections) |
| Stopped slides fully in | Task 3 (`recordState` "stopped"); Task 5 |
| Side panel 440 px, tabs | Task 6; Task 7 (`scale(440)` from `scale(1100)` wide, else 40%) |
| Up next with "Playing from · sourceLabel", reorder, remove, jump | Task 3 (`sourceKicker`); Task 6 |
| Album tab: cached `albumTracks`, playing row, play from row | Task 3 (`albumTracks`, `currentAlbumRow`, `playAlbumFrom`); Task 6 |
| Lyrics only if the server has them; timed follows, plain is static; hidden when none | Task 2 (behind V4); Task 3 (`lyricsBehindCaps`, `currentLyricRow`); Task 6 (tab visibility, ApplyRange only when timed) |
| Sleeve flight lands on the stage sleeve, record slides out after | Task 5 (`holdIn`, `sleeveRect`); Task 7 (PlayerPage passes `nowPlaying.sleeveRect`/`sleeveRadius`) |
| One rotating layer, running only while visible and page current, stopped not hidden | Task 5 (`spinning` = live && window shown && playing && animate && motion; the animator's `running` binds to it); Task 7 (`live: page.visible && page.audioMode`) |
| Settings → Interface → Animate record, default on | Task 1. It is placed under Appearance, since there is no Interface section (Contract note 6). |
| Wash keeps the existing clamp | Task 7 reuses `CoverWash` unchanged |
| Two focus zones joined by Left/Right; shortcuts and gamepad kept | Task 6 (`leftRequested`, `focusTabs`); Task 7 (Right on ⋯, Down → `focusTransport`); `tst_input_map` in Task 7 and the gate |

### §8 Navigation, input and errors (player-relevant parts)

| Spec | Where |
|---|---|
| Routes `album`/`artist` | Owned by Phases 3 and 4. Phase 5 only calls `Actions.openAlbum`/`openArtist`, which already pop the player (`pushPage`/`capturePageDeparture`). |
| Input contexts unchanged | No InputMap edits in Phase 5; `tst_input_map` runs in Task 7 and in the gate |
| Degrade by hiding the failed part | Task 3: a failed album-track or lyrics read leaves an empty model or no lyrics, so the tab or list hides and no error state appears on the player |
| Late replies dropped by generation counters | Task 3 (`m_albumGeneration`, `m_lyricsGeneration` checked in `.then`) |
| Session change clears the cache | Owned by Phase 1 `MusicRepository`; Task 3 reads through it |

### Issues found and fixed inline

1. **Lint wording (Tasks 5–8).** The per-task steps implied a clean lint exit, but the baseline script exits 1 on any addition. Contract note 8 now says a non-zero exit listing only the accepted `[unqualified]` additions is expected until Task 9, which refreshes the baseline once.
2. **Task 8 unqualified helper.** The playhead `MouseArea` called `fractionAt` unqualified, which qmllint would flag outside the accepted set. It now has `id: seekZone`, and the calls are qualified.
3. **Task 8 ambiguous match.** `artFrame` and `titleLine` share their `Accessible` lines and `TapHandler` body. The step now names the indents so the edit lands on `titleLine` only, and states that the cover keeps opening the player.
4. **Task 8 CMake anchor.** Task 8 used the same "after `tst_navigation_history`" anchor as Task 5, which would give adjacent hunks. It now anchors after `tst_media_source`.
5. **Gate log path.** The build log moved from `/tmp` to `build/dev/phase5-build.log`, keeping `/tmp` for the `/tmp/w5*` agent trees only.

No placeholders remain. Every commit ends with the `Co-Authored-By` line. The only ids used are the fixture ids.
