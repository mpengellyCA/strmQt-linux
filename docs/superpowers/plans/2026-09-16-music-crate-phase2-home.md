# Music Crate, Phase 2: Music Home

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Opening a music library lands on Music Home: a resume (or pull-one-out) hero and eight Crate shelves that load independently. The phase adds `MusicLane`, `MusicHomeController`, custom cards in `StrmRail`/`StrmGrid`, the Phase 2 Crate controls, `MusicHomePage` and the `musicHome` route.

**Architecture:** see the index, `docs/superpowers/plans/2026-09-16-music-crate.md`. **Read its Global Constraints, Shared vocabulary and "Phase 2–5 contract" before any task.** Every task in this file implicitly includes them. Phase 1 (`2026-09-16-music-crate-phase1-data.md`) must be merged: this phase consumes `MusicRepository`, the music models, `MusicUserDataRelay`, `MusicPlayback`, the Crate tokens and the `Application` accessors.

**Spec:** `docs/superpowers/specs/2026-09-16-music-crate-design.md` §4 (Music Home), §2 (Crate visual language), §8 (input), and the Home mockup (`music-direction.html`, option A).

## Contract notes

The contract names in the index are unchanged. This phase **adds** the members below. A later phase may rely on them.

**`MusicLane`** (`src/app/controllers/music/MusicLane.h/.cpp`)
- `quint64 beginRefresh()`: a quiet refetch.
  - If the lane has loaded before and has no error, it keeps `loading` false, so content stays on screen.
  - A failure in quiet mode keeps the content and sets no `error`. It only logs `qCWarning(logApp)`.
  - Otherwise it behaves exactly like `begin()`.
- `void reset()`: bumps the generation and clears `loading`, `error` and `ready`. The model is left alone.
- `MusicModelBase *typedModel() const`.
- Initial state: `empty` is true, because there are no rows, no load and no error. A shelf is therefore hidden until the controller calls `begin()`.
- `fail(generation, "")` stores `tr("Couldn't load")`, so an error is never an empty string.
- The lane listens to the model's `countChanged`, so `empty` follows the rows.

**`MusicHomeController`** (`MusicHomeCtl`)
- Signal `shelfTextChanged()` is the NOTIFY for `addedThisWeekText` and `allGenresText`.
- `QList<MusicModelBase *> models() const` returns every model the controller owns, including the private one-row `heroAlbum`. `Application` registers each with the relay.
- `void resetSessionState()` clears the library id, the hero, all models and all lanes. `Application::teardownAuthenticatedSession` calls it.
- `static QStringList sectionKeys()` returns `home, albums, artists, songs, genres, playlists`.
- `Q_INVOKABLE void shuffleStation(int row)` and `Q_INVOKABLE void queueStation(int row)` back the station menu (Play / Shuffle / Add to queue).
- `hero.favourite` follows the hidden `heroAlbum` model, which the relay patches. The page toggles it with `Actions.setFavorite(hero.albumId, !hero.favourite)`.
- `heroLane` tracks the **hero request** (continue listening, then the pull-one-out fallback). Its model `heroRecent` is filled from the recent-albums result: the recent albums minus the hero album, first three. There is no second request.
  - The page shows the hero while `hero.mode !== ""`.
  - It shows the hero skeleton while `heroLane.loading && hero.mode === ""`, and the one-line error on `heroLane.error`.
- `genreLane` holds the 10 largest bins, followed by one synthetic **all-genres bin**:
  - `itemId` is `""`, `name` is `allGenresText` and `recordCount` is `0`.
  - Its `covers` are the first cover of each of the first three bins.
  - A tile whose `itemId` is empty is `isAllBin`.
- `open(libraryId)` with the library already open is `refreshStale()`.
- `refreshStale()` refetches only through the repository. Within the TTL every call is a cache hit, so no request is sent.
  - It covers hero, recent, new, genres, artists and forgotten, but never `pullLane` (random by design) or the stations.
  - When a result has the same ids in the same order, the model is **not** reset, so focus and scroll stay put.
  - Known limit: in that case a changed count or subtitle on an unchanged id waits for the next id change or the TTL. User data still patches in place through the relay.
- The stations are built once per `open()`, after both the pull pool (`randomAlbums(lib, 20)`, which also fills `pullLane`) and `topArtists` have settled, whether they succeeded or failed.
  - A later artist refresh that changes the top artist rebuilds them.
  - `reshuffle()` leaves the stations alone.
- **Phase 1 addition:** `MusicPlayback` gains `void shuffleStation(const QString &libraryId, const Station &station)` (label `"Station · " + label`, shuffled) and `void queueStation(const QString &libraryId, const Station &station)` (resolved and appended with `ItemActions::addAllToQueue`, with no intent reservation, because appending never replaces what plays).

**`StrmRail` / `StrmGrid`**
- `property Component cardComponent: null`, as the contract says.
  - The loaded item receives `model`, `index`, `current` and `hovered`, but only the properties it declares.
  - The rail listens to whichever of `activated()`, `playRequested()` and `menuRequested(x, y)` the item declares.
- Added `property int customCardWidth` and `property int customCardHeight`. They are the cell size while `cardComponent` is set, because a hidden `StrmCard` cannot measure a Crate control.
- `StrmRail` adds `property bool showHeading: true`. `CrateShelf` draws its own Crate heading and turns the rail's off.
- Every Crate control that can sit in a rail declares `property bool hovered` (its own `HoverHandler` by default), so the rail's hover index works unchanged.

**Routes** (`Main.qml`)
- `root.openMusicHome(libraryId, name)` is as in the contract.
- **Interim, removed in Phase 3:** `root.openMusicSection(libraryId, name, section, genreId)` opens the **existing** `music` route and `MusicPage`.
  - `"genres"` maps to the Albums tab, because `MusicPage` has no Genres tab.
  - A genre bin sets `MusicCtl.setGenreIds([genreId])`. Any other section clears the genre filter.
  - Phase 3 replaces the body with `openMusicBrowse`/`openMusicGenre`, keeping the page signals.
- `MusicHomePage` signals `sectionRequested(string key)` and `genreRequested(string genreId, string genreName)`. `Main.qml` wires them.
- `root.railKey` is `currentKey` with a leading `"musicHome:"` removed. `NavRail.current` and `cycleDestination` use it, so the rail highlights the music library while Home is on screen.

**Home page behaviour** (`MusicHomePage.qml`)
- S on Home plays the **Shuffle all** station, because Home has no single filtered view to shuffle. L favourites the hero album when the hero has focus, and otherwise the focused album or artist card.
- `CrateShelf.lane` is declared `property var lane`, because `MusicLane` is not a registered QML type. The name matches the contract.
- A library with no play history pays for small history requests on every `refreshStale()`. `MusicRepository` does not cache an empty history (Phase 1), so the `Limit 1` continue-listening query and the recent-albums history query go out each time Home becomes visible. Both are tiny, and the first play makes them cacheable.

**Lint baseline.** `MusicHomePage.qml` reads the context properties `MusicHomeCtl`, `MusicPlay`, `Actions` and `Session`. Like every existing page, that adds `[unqualified]` warnings. The phase gate (Task 9) re-baselines once, deliberately: first it checks that every new line is `[unqualified]` or `[unresolved-type]` on a context property. It never re-baselines the four fatal categories (`is not a type`, `was not found`, `unavailable`, `incompatible-type`).

## Waves

| Wave | Tasks | Notes |
|---|---|---|
| 2a | 1 ‖ 4 ‖ 5 | Independent: `MusicLane`, `cardComponent` in `StrmRail`/`StrmGrid`, the Crate atoms plus `Music.cmake` (three agents) |
| 2b | 2 ‖ 6 | `MusicHomeController` (needs 1) and `CrateShelf` (needs 4, 5) |
| 2c | 3 | Application and `main.cpp` wiring (needs 2) |
| 2d | 7 | `MusicHomePage` (needs 3, 6) |
| 2e | 8 → 9 | Routes and navigation tests, then the phase gate with the visual check |

Agents in one wave all append to `src/CMakeLists.txt`, `tests/CMakeLists.txt` or `src/ui/music/Music.cmake`. At the wave gate the orchestrator merges those hunks. They only add lines, so the merge is mechanical.

**Commits in parallel waves:** agents share one working tree, so an agent in a wave with more than one agent does **not** run its task's commit step. It reports the files it touched. At the gate, the orchestrator:
1. builds and tests the integrated tree;
2. runs each task's `git add … && git commit` in task order, with that task's message;
3. runs `rm -rf /tmp/w<wave>*` in the same step as the last commit (AGENTS.md).

A task run by a single agent (waves 2c, 2d, 2e, or any serial execution) commits its own step.

Build commands used in every task (the default dev tree; parallel agents substitute their `/tmp/w<wave><agent>` directory as the index explains):

```bash
cmake --preset dev && cmake --build --preset dev
ctest --preset dev -R <test_name> --output-on-failure
```

QML-only steps have no C++ unit test. They use this lint check instead, and say so where they do:

```bash
bash scripts/check-qmllint-baseline.sh build/dev
```

Before Task 9 re-baselines, the script reports new `[unqualified]` context-property lines on new pages as expected. At any stage, a line in a fatal category (`is not a type`, `was not found`, `unavailable`, `incompatible-type`) is a failure.

---
### Task 1: `MusicLane`

**Files:**
- Create: `src/app/controllers/music/MusicLane.h`, `src/app/controllers/music/MusicLane.cpp`
- Modify: `src/CMakeLists.txt` (`strmqt_app`)
- Test: `tests/unit/tst_music_lane.cpp`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `MusicModelBase` (Phase 1 Task 13), `AlbumGridModel` (test only), `logApp` (`core/Log.h`).
- Produces (namespace `strmqt::music`), `class MusicLane : QObject`:
  - `Q_PROPERTY(QObject *model READ model CONSTANT)`
  - `Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)`
  - `Q_PROPERTY(QString error READ error NOTIFY stateChanged)`
  - `Q_PROPERTY(bool empty READ empty NOTIFY stateChanged)`
  - `Q_PROPERTY(bool ready READ ready NOTIFY stateChanged)`
  - `MusicLane(MusicModelBase *model, QObject *parent = nullptr)`
  - `quint64 begin()`, `quint64 beginRefresh()`, `bool isCurrent(quint64) const`
  - `void succeed(quint64)`, `void fail(quint64, const QString &)`, `void reset()`
  - `MusicModelBase *typedModel() const`
  - `Q_INVOKABLE void retry()`, signals `stateChanged()`, `retryRequested()`

- [x] **Step 1: Write the failing test**

`tests/unit/tst_music_lane.cpp`:

```cpp
#include <QSignalSpy>
#include <QtTest>

#include "app/controllers/music/MusicLane.h"
#include "app/music/models/AlbumGridModel.h"

using namespace strmqt::music;

namespace {

QList<Album> albums(int count)
{
    QList<Album> list;
    for (int i = 0; i < count; ++i) {
        Album album;
        album.id = QStringLiteral("al%1").arg(i);
        album.title = QStringLiteral("Album %1").arg(i);
        list.append(album);
    }
    return list;
}

} // namespace

class MusicLaneTest : public QObject
{
    Q_OBJECT

private slots:
    void startsIdleAndEmpty();
    void beginSucceedFailFollowTheGeneration();
    void emptyTracksTheModel();
    void refreshKeepsContentOnFailure();
    void refreshOfAFailedLaneShowsLoading();
    void retryAsksTheOwner();
    void resetForgetsEverything();
};

void MusicLaneTest::startsIdleAndEmpty()
{
    AlbumGridModel model;
    MusicLane lane(&model);
    QCOMPARE(lane.model(), &model);
    QCOMPARE(lane.typedModel(), &model);
    QVERIFY(!lane.loading());
    QVERIFY(lane.error().isEmpty());
    QVERIFY(!lane.ready());
    QVERIFY(lane.empty());
}

void MusicLaneTest::beginSucceedFailFollowTheGeneration()
{
    AlbumGridModel model;
    MusicLane lane(&model);
    QSignalSpy changed(&lane, &MusicLane::stateChanged);

    const quint64 first = lane.begin();
    QVERIFY(lane.loading());
    QVERIFY(!lane.empty());
    QVERIFY(lane.isCurrent(first));
    QVERIFY(changed.count() >= 1);

    const quint64 second = lane.begin();
    QVERIFY(second > first);
    QVERIFY(!lane.isCurrent(first));

    lane.fail(first, QStringLiteral("stale")); // superseded: ignored
    QVERIFY(lane.loading());
    QVERIFY(lane.error().isEmpty());

    lane.fail(second, QString());
    QVERIFY(!lane.loading());
    QCOMPARE(lane.error(), QStringLiteral("Couldn't load"));
    QVERIFY(!lane.empty()); // an error is shown, not hidden
    QVERIFY(!lane.ready());

    const quint64 third = lane.begin();
    QVERIFY(lane.error().isEmpty());
    model.setItems(albums(2));
    lane.succeed(second); // superseded: ignored
    QVERIFY(lane.loading());
    lane.succeed(third);
    QVERIFY(!lane.loading());
    QVERIFY(lane.ready());
    QVERIFY(!lane.empty());
}

void MusicLaneTest::emptyTracksTheModel()
{
    AlbumGridModel model;
    MusicLane lane(&model);
    lane.succeed(lane.begin());
    QVERIFY(lane.ready());
    QVERIFY(lane.empty());

    QSignalSpy changed(&lane, &MusicLane::stateChanged);
    model.setItems(albums(1));
    QVERIFY(changed.count() >= 1);
    QVERIFY(!lane.empty());
    model.clear();
    QVERIFY(lane.empty());
}

void MusicLaneTest::refreshKeepsContentOnFailure()
{
    AlbumGridModel model;
    MusicLane lane(&model);
    model.setItems(albums(3));
    lane.succeed(lane.begin());

    const quint64 refresh = lane.beginRefresh();
    QVERIFY(!lane.loading()); // quiet: the shelf stays on screen
    QVERIFY(lane.isCurrent(refresh));
    lane.fail(refresh, QStringLiteral("HTTP 500"));
    QVERIFY(lane.error().isEmpty());
    QVERIFY(!lane.loading());
    QVERIFY(lane.ready());
    QCOMPARE(model.count(), 3);

    lane.succeed(lane.beginRefresh());
    QVERIFY(lane.ready());
}

void MusicLaneTest::refreshOfAFailedLaneShowsLoading()
{
    AlbumGridModel model;
    MusicLane lane(&model);
    lane.fail(lane.begin(), QStringLiteral("HTTP 500"));
    const quint64 refresh = lane.beginRefresh();
    QVERIFY(lane.loading());
    QVERIFY(lane.error().isEmpty());
    lane.fail(refresh, QStringLiteral("HTTP 502"));
    QCOMPARE(lane.error(), QStringLiteral("HTTP 502"));

    MusicLane fresh(&model);
    fresh.beginRefresh();
    QVERIFY(fresh.loading()); // never loaded: a refresh is a first load
}

void MusicLaneTest::retryAsksTheOwner()
{
    AlbumGridModel model;
    MusicLane lane(&model);
    QSignalSpy retry(&lane, &MusicLane::retryRequested);
    QMetaObject::invokeMethod(&lane, "retry");
    QCOMPARE(retry.count(), 1);
}

void MusicLaneTest::resetForgetsEverything()
{
    AlbumGridModel model;
    MusicLane lane(&model);
    const quint64 generation = lane.begin();
    lane.reset();
    QVERIFY(!lane.isCurrent(generation));
    QVERIFY(!lane.loading());
    QVERIFY(!lane.ready());
    QVERIFY(lane.error().isEmpty());
    lane.succeed(generation);
    QVERIFY(!lane.ready());
}

QTEST_GUILESS_MAIN(MusicLaneTest)
#include "tst_music_lane.moc"
```

Append to `tests/CMakeLists.txt`:

```cmake
strmqt_add_test(tst_music_lane unit/tst_music_lane.cpp)
```

- [x] **Step 2: Run the test to verify it fails**

Run: `cmake --preset dev && cmake --build --preset dev --target tst_music_lane`
Expected: FAIL to compile with `app/controllers/music/MusicLane.h: No such file or directory`.

- [x] **Step 3: Write `MusicLane`**

`src/app/controllers/music/MusicLane.h`:

```cpp
#pragma once

#include <QObject>
#include <QString>

#include "app/music/models/MusicModelBase.h"

namespace strmqt::music {

// One independently loading shelf or section (Crate spec §4). The owner calls
// begin() before a request and succeed()/fail() with the returned generation;
// a reply for an older generation changes nothing. `empty` is the one flag a
// shelf needs to hide itself: not loading, no error, no rows.
class MusicLane : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *model READ model CONSTANT)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY stateChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged)

public:
    explicit MusicLane(MusicModelBase *model, QObject *parent = nullptr);

    QObject *model() const { return m_model; }
    MusicModelBase *typedModel() const { return m_model; }
    bool loading() const { return m_loading; }
    QString error() const { return m_error; }
    bool ready() const { return m_ready; }
    bool empty() const;

    quint64 begin();
    // A refetch of a lane already on screen: loading stays false and a failure
    // keeps the rows. A lane that never loaded, or last failed, gets begin().
    quint64 beginRefresh();
    bool isCurrent(quint64 generation) const { return generation == m_generation; }
    void succeed(quint64 generation);
    void fail(quint64 generation, const QString &error);
    void reset();

    Q_INVOKABLE void retry();

signals:
    void stateChanged();
    void retryRequested();

private:
    MusicModelBase *m_model;
    quint64 m_generation = 0;
    bool m_loading = false;
    bool m_quiet = false;
    bool m_ready = false;
    QString m_error;
};

} // namespace strmqt::music
```

`src/app/controllers/music/MusicLane.cpp`:

```cpp
#include "app/controllers/music/MusicLane.h"

#include "core/Log.h"

namespace strmqt::music {

MusicLane::MusicLane(MusicModelBase *model, QObject *parent)
    : QObject(parent)
    , m_model(model)
{
    connect(m_model, &MusicModelBase::countChanged, this, &MusicLane::stateChanged);
}

bool MusicLane::empty() const
{
    return !m_loading && m_error.isEmpty() && m_model->count() == 0;
}

quint64 MusicLane::begin()
{
    ++m_generation;
    m_quiet = false;
    m_loading = true;
    m_error.clear();
    emit stateChanged();
    return m_generation;
}

quint64 MusicLane::beginRefresh()
{
    if (!m_ready || !m_error.isEmpty())
        return begin();
    ++m_generation;
    m_quiet = true;
    return m_generation;
}

void MusicLane::succeed(quint64 generation)
{
    if (!isCurrent(generation))
        return;
    m_loading = false;
    m_quiet = false;
    m_ready = true;
    m_error.clear();
    emit stateChanged();
}

void MusicLane::fail(quint64 generation, const QString &error)
{
    if (!isCurrent(generation))
        return;
    if (m_quiet) {
        // The shelf is already showing what it had; a failed background
        // refetch is not worth replacing it with an error line.
        m_quiet = false;
        qCWarning(logApp) << "music: shelf refresh failed" << error;
        return;
    }
    m_loading = false;
    m_error = error.isEmpty() ? tr("Couldn't load") : error;
    emit stateChanged();
}

void MusicLane::reset()
{
    ++m_generation;
    m_loading = false;
    m_quiet = false;
    m_ready = false;
    m_error.clear();
    emit stateChanged();
}

void MusicLane::retry()
{
    emit retryRequested();
}

} // namespace strmqt::music
```

Add to the `qt_add_library(strmqt_app STATIC …)` list in `src/CMakeLists.txt`, after the Phase 1 music sources:

```cmake
    app/controllers/music/MusicLane.h app/controllers/music/MusicLane.cpp
```

- [x] **Step 4: Run the test to verify it passes**

Run: `cmake --build --preset dev --target tst_music_lane && ctest --preset dev -R tst_music_lane --output-on-failure`
Expected: PASS (7 tests).

- [x] **Step 5: Commit**

```bash
git add src/app/controllers/music/MusicLane.h src/app/controllers/music/MusicLane.cpp src/CMakeLists.txt tests/unit/tst_music_lane.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'MSG'
feat(music): add MusicLane, the per-shelf load state

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
)"
```

---
### Task 2: `MusicHomeController`

**Files:**
- Create: `src/app/controllers/music/MusicHomeController.h`, `src/app/controllers/music/MusicHomeController.cpp`
- Modify: `src/app/music/MusicPlayback.h/.cpp` (`shuffleStation`, `queueStation`)
- Modify: `src/CMakeLists.txt` (`strmqt_app`)
- Test: `tests/integration/tst_music_home_controller.cpp`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes:
  - `MusicLane` (Task 1)
  - `MusicRepository::continueListening`, `recentAlbums`, `newAlbums`, `genreBins`, `topArtists`, `forgottenFavourites`, `randomAlbums`, `stations`, `resolveStation`, `setClockForTests` (Phase 1 Tasks 10–12)
  - `AlbumGridModel`, `ArtistGridModel`, `GenreBinModel`, `StationModel`, `MusicModelBase::idAt`/`get` (Phase 1 Tasks 13–14)
  - `MusicPlayback::playAlbum`, `shuffleAlbum`, `playStation` and its private `playResolved`/`Order` (Phase 1 Task 17)
  - `formatRuntime`, `formatTrackCount`, `joinNames`, `coverUrl` (`app/music/MusicFormat.h`)
  - `MockEmbyServer::addQueryRoute`/`addRoute`/`setRouteDelay`
- Produces (namespace `strmqt::music`):
  - `class MusicHomeController : QObject`, exposed as `MusicHomeCtl` in Task 3, with exactly the contract members plus the Contract-notes additions:
    - `libraryId`, `hero`, `heroLane`, `recentLane`, `newLane`, `addedThisWeekText`, `stationLane`, `genreLane`, `allGenresText`, `artistLane`, `forgottenLane`, `pullLane`, `sectionKeys`
    - `open`, `refreshStale`, `resumeHero`, `shuffleHero`, `anotherOne`, `reshuffle`, `playStation`, `shuffleStation`, `queueStation`, `cycleSection`
    - `models()`, `resetSessionState()`
    - signals `libraryIdChanged`, `heroChanged`, `shelfTextChanged`
  - `void MusicPlayback::shuffleStation(const QString &libraryId, const Station &station)`
  - `void MusicPlayback::queueStation(const QString &libraryId, const Station &station)`

- [ ] **Step 1: Write the failing test**

`tests/integration/tst_music_home_controller.cpp`:

```cpp
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QUrlQuery>
#include <QtTest>

#include "FakePlayerBackend.h"
#include "MockEmbyServer.h"
#include "app/ItemActions.h"
#include "app/PlayQueue.h"
#include "app/controllers/PlayerController.h"
#include "app/controllers/music/MusicHomeController.h"
#include "app/models/MediaItemModel.h"
#include "app/music/MusicPlayback.h"
#include "app/music/MusicRepository.h"
#include "app/music/UserDataPatch.h"
#include "core/Settings.h"
#include "server/emby/EmbyClient.h"
#include "server/emby/MusicServerCapabilities.h"

using namespace strmqt;
using namespace strmqt::music;

namespace {

const auto kUserId = QStringLiteral("a1b2c3d4e5f60718293a4b5c6d7e8f90");
const auto kToken = QStringLiteral("not-a-real-token-fixture-only");
const auto kLibrary = QStringLiteral("1868998");
const auto kOtherLibrary = QStringLiteral("2000001");

QString fixturePath(const QString &name)
{
    return QStringLiteral(STRMQT_FIXTURES_DIR "/") + name;
}

QString itemsPath() { return QStringLiteral("/Users/%1/Items").arg(kUserId); }
QString itemPath(const QString &id) { return QStringLiteral("/Users/%1/Items/%2").arg(kUserId, id); }

QByteArray page(const QJsonArray &items, int total = -1)
{
    return QJsonDocument(QJsonObject{{QStringLiteral("Items"), items},
                                     {QStringLiteral("TotalRecordCount"), total < 0 ? items.size() : total}})
        .toJson(QJsonDocument::Compact);
}

QByteArray object(const QJsonObject &json) { return QJsonDocument(json).toJson(QJsonDocument::Compact); }

QJsonObject albumJson(const QString &id, const QString &artistId = QStringLiteral("ar1"),
                      int tracks = 10, qint64 minutes = 45)
{
    return {{"Id", id}, {"Name", "Album " + id}, {"Type", "MusicAlbum"},
            {"AlbumArtists", QJsonArray{QJsonObject{{"Id", artistId}, {"Name", "Artist " + artistId}}}},
            {"ChildCount", tracks}, {"CumulativeRunTimeTicks", minutes * 60 * 10'000'000LL},
            {"ImageTags", QJsonObject{{"Primary", "tag-" + id}}}};
}

QJsonObject trackJson(const QString &id, const QString &albumId, int number, const QString &artistId)
{
    return {{"Id", id}, {"Name", "Track " + id}, {"Type", "Audio"}, {"AlbumId", albumId},
            {"Album", "Album " + albumId}, {"ParentIndexNumber", 1}, {"IndexNumber", number},
            {"RunTimeTicks", 4 * 60 * 10'000'000LL},
            {"ArtistItems", QJsonArray{QJsonObject{{"Id", artistId}, {"Name", "Artist " + artistId}}}},
            {"AlbumArtists", QJsonArray{QJsonObject{{"Id", artistId}, {"Name", "Artist " + artistId}}}},
            {"MediaStreams", QJsonArray{QJsonObject{{"Type", "Audio"}, {"Codec", "flac"},
                                                    {"BitDepth", 16}, {"SampleRate", 44100}}}}};
}

QJsonObject playedTrack(const QString &id, const QString &albumId, const QString &artistId,
                        const QString &lastPlayed)
{
    QJsonObject track = trackJson(id, albumId, 1, artistId);
    track.insert("UserData", QJsonObject{{"Played", true},
                                         {"PlaybackPositionTicks", 0},
                                         {"LastPlayedDate", lastPlayed}});
    return track;
}

using Q = QList<QPair<QString, QString>>;

} // namespace

class MusicHomeControllerTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { setEmbyImageSourceNamespace(QStringLiteral("t")); }
    void init();
    void cleanup();

    void heroResumesTheLastAlbum();
    void heroPullsOneOutWithoutHistory();
    void lanesFailIndependently();
    void staleRepliesAreDroppedOnReopen();
    void reshuffleRefillsOnlyThePullLane();
    void stationsResolveAndPlay();
    void refreshStaleIsFreeWithinTheTtl();
    void cycleSectionWalksTheStrip();
    void resetSessionStateForgetsTheLibrary();

private:
    void routeLibrary(const QString &library, const QString &tag, bool history);
    void routeFavourites(const QString &library, const QString &tag, int status);
    void routeGenres(const QString &library, const QString &tag, int status);
    QList<MusicLane *> lanes() const;
    bool allReady() const;
    PlayQueue *queue() const { return m_player->queue(); }

    MockEmbyServer *m_mock = nullptr;
    emby::EmbyClient *m_client = nullptr;
    FakePlayerBackend *m_backend = nullptr;
    QTemporaryDir *m_dir = nullptr;
    Settings *m_settings = nullptr;
    PlayerController *m_player = nullptr;
    ItemActions *m_actions = nullptr;
    MusicRepository *m_repo = nullptr;
    MusicPlayback *m_playback = nullptr;
    MusicHomeController *m_home = nullptr;
    QDateTime m_now = QDateTime::fromSecsSinceEpoch(1'800'000'000, QTimeZone::utc());
};

void MusicHomeControllerTest::init()
{
    m_now = QDateTime::fromSecsSinceEpoch(1'800'000'000, QTimeZone::utc());
    m_mock = new MockEmbyServer(this);
    QVERIFY(m_mock->start());
    for (const char *id : {"a-t1", "a-t2", "a-t3"}) {
        QVERIFY(m_mock->addRouteFromFile("POST", QStringLiteral("/Items/%1/PlaybackInfo").arg(QLatin1String(id)),
                                         fixturePath(QStringLiteral("playback_info.json"))));
    }
    m_mock->addRoute("POST", "/Sessions/Playing", 204, {});
    m_mock->addRoute("POST", "/Sessions/Playing/Progress", 204, {});
    m_mock->addRoute("POST", "/Sessions/Playing/Stopped", 204, {});
    // Anything a test did not route is an empty page, not a 404.
    m_mock->addRoute("GET", itemsPath(), 200, page({}));

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
    m_repo->setClockForTests([this] { return m_now; });
    m_repo->setShuffleSeedForTests(7);
    m_playback = new MusicPlayback(m_repo, m_actions, this);
    m_home = new MusicHomeController(m_repo, m_playback, this);
}

void MusicHomeControllerTest::cleanup()
{
    delete m_home;
    delete m_playback;
    delete m_repo;
    delete m_actions;
    delete m_player;
    delete m_settings;
    delete m_dir;
    delete m_backend;
    delete m_client;
    delete m_mock;
    m_home = nullptr;
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

// Every id a library answers with is prefixed by its tag ("a-hero", "b-hero"),
// so a reply that lands in the wrong library is visible in any assertion.
void MusicHomeControllerTest::routeLibrary(const QString &library, const QString &tag, bool history)
{
    const auto id = [&tag](const char *suffix) { return tag + QLatin1Char('-') + QLatin1String(suffix); };

    QJsonArray played;
    if (history) {
        played = {playedTrack(id("t2"), id("hero"), id("ar"), "2026-09-15T20:00:00Z"),
                  playedTrack(id("x1"), id("r1"), id("ar"), "2026-09-15T19:00:00Z"),
                  playedTrack(id("x2"), id("r2"), id("ar"), "2026-09-15T18:00:00Z")};
    }
    for (int limit : {1, 200, 500}) {
        m_mock->addQueryRoute("GET", itemsPath(),
                              Q{{"ParentId", library}, {"IncludeItemTypes", "Audio"},
                                {"SortBy", "DatePlayed"}, {"Limit", QString::number(limit)}},
                              200, page(limit == 1 && history ? QJsonArray{played.at(0)} : played));
    }

    QJsonObject hero = albumJson(id("hero"), id("ar"), 4, 16);
    hero.insert("ProductionYear", 1997);
    m_mock->addRoute("GET", itemPath(id("hero")), 200, object(hero));
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"ParentId", id("hero")}, {"IncludeItemTypes", "Audio"}}, 200,
                          page({trackJson(id("t1"), id("hero"), 1, id("ar")),
                                trackJson(id("t2"), id("hero"), 2, id("ar")),
                                trackJson(id("t3"), id("hero"), 3, id("ar")),
                                trackJson(id("t4"), id("hero"), 4, id("ar"))}));
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"Ids", id("hero") + "," + id("r1") + "," + id("r2")}}, 200,
                          page({albumJson(id("r2"), id("ar")), hero, albumJson(id("r1"), id("ar"))}));
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"Ids", id("ar")}}, 200,
                          page({QJsonObject{{"Id", id("ar")}, {"Name", "Artist " + id("ar")},
                                            {"Type", "MusicArtist"},
                                            {"ImageTags", QJsonObject{{"Primary", "tag-" + id("ar")}}}}}));

    const Q albums{{"ParentId", library}, {"IncludeItemTypes", "MusicAlbum"}};
    m_mock->addQueryRoute("GET", itemsPath(), Q(albums) << qMakePair(QStringLiteral("SortBy"), QStringLiteral("DateCreated")),
                          200, page({albumJson(id("n1"), id("ar")), albumJson(id("n2"), id("ar"))}));
    m_mock->addQueryRoute("GET", itemsPath(), Q(albums) << qMakePair(QStringLiteral("Limit"), QStringLiteral("0")),
                          200, page({}, 6));
    m_mock->addQueryRoute("GET", itemsPath(),
                          Q(albums) << qMakePair(QStringLiteral("SortBy"), QStringLiteral("Random"))
                                    << qMakePair(QStringLiteral("Limit"), QStringLiteral("20")),
                          200, page({albumJson(id("p1"), id("ar")), albumJson(id("p2"), id("ar"))}));
    m_mock->addQueryRoute("GET", itemsPath(),
                          Q(albums) << qMakePair(QStringLiteral("SortBy"), QStringLiteral("Random"))
                                    << qMakePair(QStringLiteral("Limit"), QStringLiteral("1")),
                          200, page({albumJson(id("pick"), id("ar"))}));
    routeFavourites(library, tag, 200);
    routeGenres(library, tag, 200);

    const Q audio{{"ParentId", library}, {"IncludeItemTypes", "Audio"}};
    m_mock->addQueryRoute("GET", itemsPath(), Q(audio) << qMakePair(QStringLiteral("SortBy"), QStringLiteral("PlayCount")),
                          200, page({trackJson(id("t1"), id("hero"), 1, id("ar")),
                                     trackJson(id("t2"), id("hero"), 2, id("ar"))}));
    m_mock->addQueryRoute("GET", itemsPath(), Q(audio) << qMakePair(QStringLiteral("Filters"), QStringLiteral("IsFavorite")),
                          200, page({trackJson(id("t3"), id("hero"), 3, id("ar"))}));
}

void MusicHomeControllerTest::routeFavourites(const QString &library, const QString &tag, int status)
{
    m_mock->addQueryRoute("GET", itemsPath(),
                          Q{{"ParentId", library}, {"IncludeItemTypes", "MusicAlbum"}, {"Filters", "IsFavorite"}},
                          status, status == 200 ? page({albumJson(tag + "-f1", tag + "-ar")}) : QByteArray("{}"));
}

void MusicHomeControllerTest::routeGenres(const QString &library, const QString &tag, int status)
{
    const QByteArray failed("{}");
    QJsonArray genres{QJsonObject{{"Id", tag + "-g1"}, {"Name", "Rock " + tag}, {"AlbumCount", 3}},
                      QJsonObject{{"Id", tag + "-g2"}, {"Name", "Folk " + tag}, {"AlbumCount", 2}},
                      QJsonObject{{"Id", tag + "-g3"}, {"Name", "Jazz " + tag}, {"AlbumCount", 1}}};
    m_mock->addQueryRoute("GET", "/MusicGenres", Q{{"ParentId", library}}, status,
                          status == 200 ? page(genres) : failed);
    if (!emby::caps::kGenreItemCounts) {
        QJsonArray walk;
        const QList<std::pair<QString, int>> counts{{tag + "-g1", 3}, {tag + "-g2", 2}, {tag + "-g3", 1}};
        int n = 0;
        for (const auto &[genreId, count] : counts) {
            const QString name = genres.at(n++).toObject().value("Name").toString();
            for (int i = 0; i < count; ++i) {
                QJsonObject album = albumJson(QStringLiteral("%1-w%2-%3").arg(tag, genreId).arg(i));
                album.insert("GenreItems", QJsonArray{QJsonObject{{"Id", genreId}, {"Name", name}}});
                walk.append(album);
            }
        }
        m_mock->addQueryRoute("GET", itemsPath(),
                              Q{{"ParentId", library}, {"IncludeItemTypes", "MusicAlbum"}, {"Fields", "Genres"}},
                              status, status == 200 ? page(walk) : failed);
    }
    for (int i = 1; i <= 3; ++i) {
        const QString genreId = QStringLiteral("%1-g%2").arg(tag).arg(i);
        m_mock->addQueryRoute("GET", itemsPath(), Q{{"GenreIds", genreId}, {"SortBy", "Random"}}, 200,
                              page({albumJson("c-" + genreId)}));
    }
}

QList<MusicLane *> MusicHomeControllerTest::lanes() const
{
    return {m_home->heroLane(),  m_home->recentLane(), m_home->newLane(),
            m_home->stationLane(), m_home->genreLane(), m_home->artistLane(),
            m_home->forgottenLane(), m_home->pullLane()};
}

bool MusicHomeControllerTest::allReady() const
{
    for (MusicLane *lane : lanes()) {
        if (!lane->ready() || lane->loading())
            return false;
    }
    return true;
}

void MusicHomeControllerTest::heroResumesTheLastAlbum()
{
    routeLibrary(kLibrary, QStringLiteral("a"), true);
    m_home->open(kLibrary);
    QCOMPARE(m_home->libraryId(), kLibrary);
    QVERIFY(m_home->heroLane()->loading());
    QTRY_VERIFY(allReady());

    const QVariantMap hero = m_home->hero();
    QCOMPARE(hero.value("mode").toString(), QStringLiteral("resume"));
    QCOMPARE(hero.value("albumId").toString(), QStringLiteral("a-hero"));
    QCOMPARE(hero.value("title").toString(), QStringLiteral("Album a-hero"));
    QCOMPARE(hero.value("artist").toString(), QStringLiteral("Artist a-ar"));
    QCOMPARE(hero.value("artistId").toString(), QStringLiteral("a-ar"));
    QCOMPARE(hero.value("year").toInt(), 1997);
    QCOMPARE(hero.value("summary").toString(), QStringLiteral("Artist a-ar · 1997 · 4 tracks · 16 min"));
    QCOMPARE(hero.value("resumeIndex").toInt(), 2); // finished track 2 → resume track 3
    QCOMPARE(hero.value("resumeLabel").toString(), QStringLiteral("Resume track 3"));
    QCOMPARE(hero.value("progress").toDouble(), 0.5);
    QVERIFY(!hero.value("coverUrl").toString().isEmpty());
    QCOMPARE(hero.value("albumItem").toMap().value("itemId").toString(), QStringLiteral("a-hero"));

    // Recently played beside the hero: the recent shelf minus the hero album.
    QCOMPARE(m_home->recentLane()->typedModel()->count(), 3);
    QCOMPARE(m_home->recentLane()->typedModel()->idAt(0), QStringLiteral("a-hero"));
    MusicModelBase *beside = m_home->heroLane()->typedModel();
    QCOMPARE(beside->count(), 2);
    QCOMPARE(beside->idAt(0), QStringLiteral("a-r1"));
    QCOMPARE(beside->idAt(1), QStringLiteral("a-r2"));

    QCOMPARE(m_home->newLane()->typedModel()->count(), 2);
    QCOMPARE(m_home->addedThisWeekText(),
             emby::caps::kMinDateCreated ? QStringLiteral("6 added this week") : QString());
    QCOMPARE(m_home->artistLane()->typedModel()->idAt(0), QStringLiteral("a-ar"));
    QCOMPARE(m_home->forgottenLane()->typedModel()->idAt(0), QStringLiteral("a-f1"));
    QCOMPARE(m_home->pullLane()->typedModel()->count(), 2);

    MusicModelBase *genres = m_home->genreLane()->typedModel();
    QCOMPARE(genres->count(), 4);
    QCOMPARE(genres->get(0).value("name").toString(), QStringLiteral("Rock a"));
    QCOMPARE(m_home->allGenresText(), QStringLiteral("All 3 genres"));
    const QVariantMap all = genres->get(3);
    QVERIFY(all.value("itemId").toString().isEmpty());
    QCOMPARE(all.value("name").toString(), QStringLiteral("All 3 genres"));
    QCOMPARE(all.value("covers").toStringList().size(), 3);

    // The heart follows the relay's in-place patch of the hidden hero model.
    MusicModelBase *heroModel = nullptr;
    for (MusicModelBase *model : m_home->models()) {
        if (model->count() == 1 && model->idAt(0) == QLatin1String("a-hero"))
            heroModel = model;
    }
    QVERIFY(heroModel);
    UserDataPatch patch;
    patch.favourite = true;
    heroModel->applyUserData(QStringLiteral("a-hero"), patch);
    QCOMPARE(m_home->hero().value("favourite").toBool(), true);
}

void MusicHomeControllerTest::heroPullsOneOutWithoutHistory()
{
    routeLibrary(kLibrary, QStringLiteral("a"), false);
    m_home->open(kLibrary);
    QTRY_VERIFY(allReady());

    QVariantMap hero = m_home->hero();
    QCOMPARE(hero.value("mode").toString(), QStringLiteral("pullOne"));
    QCOMPARE(hero.value("albumId").toString(), QStringLiteral("a-pick"));
    QCOMPARE(hero.value("resumeIndex").toInt(), 0);
    QCOMPARE(hero.value("resumeLabel").toString(), QStringLiteral("Play"));
    QCOMPARE(hero.value("progress").toDouble(), 0.0);
    QVERIFY(hero.value("summary").toString().startsWith(QStringLiteral("Artist a-ar")));

    // No history: the listening shelves hide themselves, the others do not.
    QVERIFY(m_home->recentLane()->empty());
    QVERIFY(m_home->artistLane()->empty());
    QCOMPARE(m_home->heroLane()->typedModel()->count(), 0);
    QVERIFY(!m_home->pullLane()->empty());
    QCOMPARE(m_home->stationLane()->typedModel()->count(), 4); // no "More like"

    m_mock->addQueryRoute("GET", itemsPath(),
                          Q{{"ParentId", kLibrary}, {"IncludeItemTypes", "MusicAlbum"},
                            {"SortBy", "Random"}, {"Limit", "1"}},
                          200, page({albumJson("a-pick2", "a-ar")}));
    m_home->anotherOne();
    QVERIFY(!m_home->heroLane()->loading()); // the current record stays up meanwhile
    QTRY_COMPARE(m_home->hero().value("albumId").toString(), QStringLiteral("a-pick2"));

    // A refresh keeps the record the user pulled rather than swapping it.
    m_home->refreshStale();
    QTest::qWait(250);
    QCOMPARE(m_home->hero().value("albumId").toString(), QStringLiteral("a-pick2"));
}

void MusicHomeControllerTest::lanesFailIndependently()
{
    routeLibrary(kLibrary, QStringLiteral("a"), true);
    routeFavourites(kLibrary, QStringLiteral("a"), 500);
    routeGenres(kLibrary, QStringLiteral("a"), 500);
    m_home->open(kLibrary);

    QTRY_VERIFY(!m_home->forgottenLane()->error().isEmpty());
    QTRY_VERIFY(!m_home->genreLane()->error().isEmpty());
    QVERIFY(!m_home->forgottenLane()->empty()); // an error line, not a hidden shelf
    for (MusicLane *lane : {m_home->heroLane(), m_home->recentLane(), m_home->newLane(),
                            m_home->stationLane(), m_home->artistLane(), m_home->pullLane()}) {
        QTRY_VERIFY(lane->ready());
        QVERIFY(lane->error().isEmpty());
    }

    routeFavourites(kLibrary, QStringLiteral("a"), 200);
    routeGenres(kLibrary, QStringLiteral("a"), 200);
    m_home->forgottenLane()->retry();
    m_home->genreLane()->retry();
    QTRY_VERIFY(m_home->forgottenLane()->ready());
    QTRY_VERIFY(m_home->genreLane()->ready());
    QVERIFY(m_home->forgottenLane()->error().isEmpty());
    QCOMPARE(m_home->forgottenLane()->typedModel()->count(), 1);
    QCOMPARE(m_home->genreLane()->typedModel()->count(), 4);
}

void MusicHomeControllerTest::staleRepliesAreDroppedOnReopen()
{
    routeLibrary(kLibrary, QStringLiteral("a"), true);
    routeLibrary(kOtherLibrary, QStringLiteral("b"), true);
    // The first library's hero lands well after the second library is done.
    m_mock->setRouteDelay("GET", itemPath(QStringLiteral("a-hero")), 400);

    m_home->open(kLibrary);
    m_home->open(kOtherLibrary);
    QCOMPARE(m_home->libraryId(), kOtherLibrary);
    QTRY_VERIFY(allReady());
    QTest::qWait(600);

    QCOMPARE(m_home->hero().value("albumId").toString(), QStringLiteral("b-hero"));
    QCOMPARE(m_home->heroLane()->typedModel()->idAt(0), QStringLiteral("b-r1"));
    QCOMPARE(m_home->recentLane()->typedModel()->idAt(0), QStringLiteral("b-hero"));
    QCOMPARE(m_home->newLane()->typedModel()->idAt(0), QStringLiteral("b-n1"));
    QCOMPARE(m_home->artistLane()->typedModel()->idAt(0), QStringLiteral("b-ar"));
    QCOMPARE(m_home->forgottenLane()->typedModel()->idAt(0), QStringLiteral("b-f1"));
    QCOMPARE(m_home->pullLane()->typedModel()->idAt(0), QStringLiteral("b-p1"));
    QCOMPARE(m_home->genreLane()->typedModel()->get(0).value("name").toString(), QStringLiteral("Rock b"));
    QCOMPARE(m_home->stationLane()->typedModel()->get(3).value("label").toString(),
             QStringLiteral("More like Artist b-ar"));
    for (MusicModelBase *model : m_home->models()) {
        for (int row = 0; row < model->count(); ++row)
            QVERIFY2(!model->idAt(row).startsWith(QLatin1String("a-")), qPrintable(model->idAt(row)));
    }
}

void MusicHomeControllerTest::reshuffleRefillsOnlyThePullLane()
{
    routeLibrary(kLibrary, QStringLiteral("a"), true);
    m_home->open(kLibrary);
    QTRY_VERIFY(allReady());
    QCOMPARE(m_home->pullLane()->typedModel()->idAt(0), QStringLiteral("a-p1"));

    m_mock->addQueryRoute("GET", itemsPath(),
                          Q{{"ParentId", kLibrary}, {"IncludeItemTypes", "MusicAlbum"},
                            {"SortBy", "Random"}, {"Limit", "20"}},
                          200, page({albumJson("a-p3", "a-ar")}));
    const int before = m_mock->requestCount();
    m_home->reshuffle();
    QVERIFY(!m_home->pullLane()->loading()); // the old records stay until the new ones land
    QTRY_COMPARE(m_home->pullLane()->typedModel()->idAt(0), QStringLiteral("a-p3"));

    const auto &requests = m_mock->requests();
    QVERIFY(requests.size() > before);
    for (qsizetype i = before; i < requests.size(); ++i) {
        const QUrlQuery query(requests.at(i).query);
        QCOMPARE(query.queryItemValue("SortBy"), QStringLiteral("Random"));
        QCOMPARE(query.queryItemValue("IncludeItemTypes"), QStringLiteral("MusicAlbum"));
    }
    QCOMPARE(m_home->stationLane()->typedModel()->count(), 5);
}

void MusicHomeControllerTest::stationsResolveAndPlay()
{
    routeLibrary(kLibrary, QStringLiteral("a"), true);
    m_home->open(kLibrary);
    QTRY_VERIFY(allReady());

    MusicModelBase *stations = m_home->stationLane()->typedModel();
    QCOMPARE(stations->count(), 5);
    QCOMPARE(stations->get(0).value("itemId").toString(), QStringLiteral("heavyRotation"));
    QCOMPARE(stations->get(3).value("label").toString(), QStringLiteral("More like Artist a-ar"));
    QCOMPARE(stations->get(0).value("covers").toStringList().size(), 4);

    m_home->playStation(0);
    QTRY_COMPARE(queue()->rowCount(), 2);
    QCOMPARE(queue()->sourceLabel(), QStringLiteral("Station · Heavy rotation"));

    m_home->queueStation(1);
    QTRY_COMPARE(queue()->rowCount(), 3);
    QCOMPARE(queue()->itemAt(2).value("itemId").toString(), QStringLiteral("a-t3"));

    m_home->shuffleStation(1);
    QTRY_COMPARE(queue()->sourceLabel(), QStringLiteral("Station · Favourites"));
    QCOMPARE(queue()->rowCount(), 1);

    m_home->playStation(99); // out of range: nothing happens
    QTest::qWait(100);
    QCOMPARE(queue()->sourceLabel(), QStringLiteral("Station · Favourites"));
}

void MusicHomeControllerTest::refreshStaleIsFreeWithinTheTtl()
{
    routeLibrary(kLibrary, QStringLiteral("a"), true);
    m_home->open(kLibrary);
    QTRY_VERIFY(allReady());
    QTest::qWait(100);

    const int before = m_mock->requestCount();
    m_home->refreshStale();
    m_home->open(kLibrary); // the same library again is a refresh, not a reload
    QTest::qWait(300);
    QCOMPARE(m_mock->requestCount(), before);
    QVERIFY(allReady());
    QCOMPARE(m_home->recentLane()->typedModel()->count(), 3);
    QCOMPARE(m_home->hero().value("albumId").toString(), QStringLiteral("a-hero"));

    m_now = m_now.addSecs(6 * 60);
    m_home->refreshStale();
    QVERIFY(!m_home->recentLane()->loading()); // quiet: the shelves stay on screen
    QTRY_VERIFY(m_mock->requestCount() > before);
    QTRY_VERIFY(allReady());
    QCOMPARE(m_home->recentLane()->typedModel()->count(), 3);
}

void MusicHomeControllerTest::cycleSectionWalksTheStrip()
{
    QCOMPARE(MusicHomeController::sectionKeys(),
             (QStringList{"home", "albums", "artists", "songs", "genres", "playlists"}));
    QCOMPARE(m_home->cycleSection(1), QStringLiteral("albums"));
    QCOMPARE(m_home->cycleSection(-1), QStringLiteral("playlists"));
    QCOMPARE(m_home->cycleSection(7), QStringLiteral("albums"));
}

void MusicHomeControllerTest::resetSessionStateForgetsTheLibrary()
{
    routeLibrary(kLibrary, QStringLiteral("a"), true);
    m_home->open(kLibrary);
    QTRY_VERIFY(allReady());

    m_home->resetSessionState();
    QVERIFY(m_home->libraryId().isEmpty());
    QVERIFY(m_home->hero().isEmpty());
    for (MusicModelBase *model : m_home->models())
        QCOMPARE(model->count(), 0);
    for (MusicLane *lane : lanes())
        QVERIFY(!lane->ready());
}

QTEST_MAIN(MusicHomeControllerTest)
#include "tst_music_home_controller.moc"
```

Append to `tests/CMakeLists.txt`:

```cmake
strmqt_add_test(tst_music_home_controller
    integration/tst_music_home_controller.cpp
    mocks/MockEmbyServer.h mocks/MockEmbyServer.cpp
    mocks/FakePlayerBackend.h
)
target_include_directories(tst_music_home_controller PRIVATE mocks)
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --preset dev && cmake --build --preset dev --target tst_music_home_controller`
Expected: FAIL to compile with `app/controllers/music/MusicHomeController.h: No such file or directory`.

- [ ] **Step 3: Add the station verbs to `MusicPlayback`**

In `src/app/music/MusicPlayback.h`, after `playStation`:

```cpp
    void shuffleStation(const QString &libraryId, const Station &station);
    // Appends without replacing what plays, so it reserves no intent.
    void queueStation(const QString &libraryId, const Station &station);
```

In `src/app/music/MusicPlayback.cpp`, after `MusicPlayback::playStation`:

```cpp
void MusicPlayback::shuffleStation(const QString &libraryId, const Station &station)
{
    const quint64 generation = m_actions->reservePlaybackIntent();
    playResolved(m_repository->resolveStation(libraryId, station), generation, 0, Order::Shuffled,
                 tr("Station · %1").arg(station.label));
}

void MusicPlayback::queueStation(const QString &libraryId, const Station &station)
{
    m_repository->resolveStation(libraryId, station).then(this, [this](Result<QList<Track>> result) {
        if (!result.ok()) {
            emit m_actions->actionFailed(tr("Couldn't add to the queue: %1").arg(result.error));
            return;
        }
        m_actions->addAllToQueue(toMaps(result.value));
    });
}
```

- [ ] **Step 4: Write the controller header**

`src/app/controllers/music/MusicHomeController.h`:

```cpp
#pragma once

#include <QFuture>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

#include <functional>
#include <optional>

#include "app/controllers/music/MusicLane.h"
#include "app/music/models/AlbumGridModel.h"
#include "app/music/models/ArtistGridModel.h"
#include "app/music/models/GenreBinModel.h"
#include "app/music/models/StationModel.h"
#include "core/Result.h"
#include "server/dto/music/MusicTypes.h"

namespace strmqt::music {

class MusicPlayback;
class MusicRepository;

// Music Home (Crate spec §4): a hero and eight shelves, each an independent
// MusicLane over a typed model. Every shelf loads, fails and retries on its
// own; a reply for a library the user has already left is dropped by the
// lane's generation. refreshStale() refetches through the repository cache,
// so within the TTL it sends nothing.
class MusicHomeController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString libraryId READ libraryId NOTIFY libraryIdChanged)
    Q_PROPERTY(QVariantMap hero READ hero NOTIFY heroChanged)
    Q_PROPERTY(strmqt::music::MusicLane *heroLane READ heroLane CONSTANT)
    Q_PROPERTY(strmqt::music::MusicLane *recentLane READ recentLane CONSTANT)
    Q_PROPERTY(strmqt::music::MusicLane *newLane READ newLane CONSTANT)
    Q_PROPERTY(QString addedThisWeekText READ addedThisWeekText NOTIFY shelfTextChanged)
    Q_PROPERTY(strmqt::music::MusicLane *stationLane READ stationLane CONSTANT)
    Q_PROPERTY(strmqt::music::MusicLane *genreLane READ genreLane CONSTANT)
    Q_PROPERTY(QString allGenresText READ allGenresText NOTIFY shelfTextChanged)
    Q_PROPERTY(strmqt::music::MusicLane *artistLane READ artistLane CONSTANT)
    Q_PROPERTY(strmqt::music::MusicLane *forgottenLane READ forgottenLane CONSTANT)
    Q_PROPERTY(strmqt::music::MusicLane *pullLane READ pullLane CONSTANT)
    Q_PROPERTY(QStringList sectionKeys READ sectionKeys CONSTANT)

public:
    static constexpr int kShelfLimit = 20;
    static constexpr int kGenreBins = 10;
    static constexpr int kHeroRecent = 3;

    MusicHomeController(MusicRepository *repository, MusicPlayback *playback, QObject *parent = nullptr);

    QString libraryId() const { return m_libraryId; }
    QVariantMap hero() const { return m_hero; }
    MusicLane *heroLane() const { return m_heroLane; }
    MusicLane *recentLane() const { return m_recentLane; }
    MusicLane *newLane() const { return m_newLane; }
    QString addedThisWeekText() const { return m_addedThisWeekText; }
    MusicLane *stationLane() const { return m_stationLane; }
    MusicLane *genreLane() const { return m_genreLane; }
    QString allGenresText() const { return m_allGenresText; }
    MusicLane *artistLane() const { return m_artistLane; }
    MusicLane *forgottenLane() const { return m_forgottenLane; }
    MusicLane *pullLane() const { return m_pullLane; }

    QList<MusicModelBase *> models() const;
    static QStringList sectionKeys();

    Q_INVOKABLE void open(const QString &libraryId);
    Q_INVOKABLE void refreshStale();
    Q_INVOKABLE void resumeHero();
    Q_INVOKABLE void shuffleHero();
    Q_INVOKABLE void anotherOne();
    Q_INVOKABLE void reshuffle();
    Q_INVOKABLE void playStation(int row);
    Q_INVOKABLE void shuffleStation(int row);
    Q_INVOKABLE void queueStation(int row);
    Q_INVOKABLE QString cycleSection(int step) const;

    void resetSessionState();

signals:
    void libraryIdChanged();
    void heroChanged();
    void shelfTextChanged();

private:
    enum class Load { First, Refresh };

    template<class T>
    void track(MusicLane *lane, Load load, QFuture<Result<T>> future, std::function<void(T &&)> apply,
               std::function<void()> settled = {});

    void loadHero(Load load);
    void pullOneHero(quint64 generation);
    void loadRecent(Load load);
    void loadNew(Load load);
    void loadGenres(Load load);
    void loadArtists(Load load);
    void loadForgotten(Load load);
    void loadPull(Load load);
    void maybeBuildStations();
    void buildStations();
    void rebuildStations();

    void setHero(const Album &album, const QString &mode, int resumeIndex, double progress);
    void clearHero();
    void syncHeroFavourite();
    void fillHeroRecent();
    void setShelfText(QString &field, const QString &value);
    std::optional<Station> stationAt(int row) const;
    void clearAll();

    MusicRepository *m_repository;
    MusicPlayback *m_playback;

    AlbumGridModel *m_heroAlbum;
    AlbumGridModel *m_heroRecent;
    AlbumGridModel *m_recent;
    AlbumGridModel *m_new;
    StationModel *m_stations;
    GenreBinModel *m_genres;
    ArtistGridModel *m_artists;
    AlbumGridModel *m_forgotten;
    AlbumGridModel *m_pull;

    MusicLane *m_heroLane;
    MusicLane *m_recentLane;
    MusicLane *m_newLane;
    MusicLane *m_stationLane;
    MusicLane *m_genreLane;
    MusicLane *m_artistLane;
    MusicLane *m_forgottenLane;
    MusicLane *m_pullLane;

    QString m_libraryId;
    QVariantMap m_hero;
    QString m_addedThisWeekText;
    QString m_allGenresText;

    QList<Album> m_pool;
    Artist m_topArtist;
    QString m_stationTopId;
    quint64 m_stationGeneration = 0;
    bool m_poolSettled = false;
    bool m_artistsSettled = false;
    bool m_stationsBuilt = false;
};

} // namespace strmqt::music
```

- [ ] **Step 5: Write the controller**

`src/app/controllers/music/MusicHomeController.cpp`:

```cpp
#include "app/controllers/music/MusicHomeController.h"

#include <QLocale>

#include "app/music/MusicFormat.h"
#include "app/music/MusicPlayback.h"
#include "app/music/MusicRepository.h"

namespace strmqt::music {

namespace {

// A refresh that answers the same ids in the same order leaves the model
// alone: a reset would drop the shelf's focus and scroll for no visible change.
template<class Model, class T>
void replaceItems(Model *model, QList<T> items, int total = -1)
{
    if (model->count() == items.size() && (total < 0 || total == model->totalRecordCount())) {
        bool same = true;
        for (int row = 0; row < items.size() && same; ++row)
            same = model->idAt(row) == items.at(row).id;
        if (same)
            return;
    }
    model->setItems(std::move(items), total);
}

} // namespace

MusicHomeController::MusicHomeController(MusicRepository *repository, MusicPlayback *playback,
                                         QObject *parent)
    : QObject(parent)
    , m_repository(repository)
    , m_playback(playback)
    , m_heroAlbum(new AlbumGridModel(this))
    , m_heroRecent(new AlbumGridModel(this))
    , m_recent(new AlbumGridModel(this))
    , m_new(new AlbumGridModel(this))
    , m_stations(new StationModel(this))
    , m_genres(new GenreBinModel(this))
    , m_artists(new ArtistGridModel(this))
    , m_forgotten(new AlbumGridModel(this))
    , m_pull(new AlbumGridModel(this))
    , m_heroLane(new MusicLane(m_heroRecent, this))
    , m_recentLane(new MusicLane(m_recent, this))
    , m_newLane(new MusicLane(m_new, this))
    , m_stationLane(new MusicLane(m_stations, this))
    , m_genreLane(new MusicLane(m_genres, this))
    , m_artistLane(new MusicLane(m_artists, this))
    , m_forgottenLane(new MusicLane(m_forgotten, this))
    , m_pullLane(new MusicLane(m_pull, this))
{
    connect(m_heroAlbum, &QAbstractItemModel::dataChanged, this, &MusicHomeController::syncHeroFavourite);

    const auto whenOpen = [this](void (MusicHomeController::*load)(Load)) {
        return [this, load] {
            if (!m_libraryId.isEmpty())
                (this->*load)(Load::First);
        };
    };
    connect(m_heroLane, &MusicLane::retryRequested, this, whenOpen(&MusicHomeController::loadHero));
    connect(m_recentLane, &MusicLane::retryRequested, this, whenOpen(&MusicHomeController::loadRecent));
    connect(m_newLane, &MusicLane::retryRequested, this, whenOpen(&MusicHomeController::loadNew));
    connect(m_genreLane, &MusicLane::retryRequested, this, whenOpen(&MusicHomeController::loadGenres));
    connect(m_artistLane, &MusicLane::retryRequested, this, whenOpen(&MusicHomeController::loadArtists));
    connect(m_forgottenLane, &MusicLane::retryRequested, this, whenOpen(&MusicHomeController::loadForgotten));
    connect(m_pullLane, &MusicLane::retryRequested, this, whenOpen(&MusicHomeController::loadPull));
    connect(m_stationLane, &MusicLane::retryRequested, this, [this] {
        if (m_poolSettled && m_artistsSettled)
            rebuildStations();
    });
}

QList<MusicModelBase *> MusicHomeController::models() const
{
    return {m_heroAlbum, m_heroRecent, m_recent, m_new, m_stations,
            m_genres,    m_artists,    m_forgotten, m_pull};
}

QStringList MusicHomeController::sectionKeys()
{
    return {QStringLiteral("home"),  QStringLiteral("albums"), QStringLiteral("artists"),
            QStringLiteral("songs"), QStringLiteral("genres"), QStringLiteral("playlists")};
}

template<class T>
void MusicHomeController::track(MusicLane *lane, Load load, QFuture<Result<T>> future,
                                std::function<void(T &&)> apply, std::function<void()> settled)
{
    const quint64 generation = load == Load::Refresh ? lane->beginRefresh() : lane->begin();
    future.then(this, [lane, generation, apply = std::move(apply),
                       settled = std::move(settled)](Result<T> result) {
        if (!lane->isCurrent(generation))
            return;
        if (result.ok()) {
            apply(std::move(result.value));
            lane->succeed(generation);
        } else {
            lane->fail(generation, result.error);
        }
        if (settled)
            settled();
    });
}

void MusicHomeController::open(const QString &libraryId)
{
    if (libraryId.isEmpty())
        return;
    if (libraryId == m_libraryId) {
        refreshStale();
        return;
    }
    clearAll();
    m_libraryId = libraryId;
    emit libraryIdChanged();

    m_stationGeneration = m_stationLane->begin();
    loadHero(Load::First);
    loadRecent(Load::First);
    loadNew(Load::First);
    loadGenres(Load::First);
    loadArtists(Load::First);
    loadForgotten(Load::First);
    loadPull(Load::First);
}

void MusicHomeController::refreshStale()
{
    if (m_libraryId.isEmpty())
        return;
    // Pull one out is random by design and the stations follow it, so neither
    // is refreshed behind the user's back.
    loadHero(Load::Refresh);
    loadRecent(Load::Refresh);
    loadNew(Load::Refresh);
    loadGenres(Load::Refresh);
    loadArtists(Load::Refresh);
    loadForgotten(Load::Refresh);
}

void MusicHomeController::loadHero(Load load)
{
    const quint64 generation = load == Load::Refresh ? m_heroLane->beginRefresh() : m_heroLane->begin();
    m_repository->continueListening(m_libraryId).then(this, [this, generation](Result<ContinueListening> result) {
        if (!m_heroLane->isCurrent(generation))
            return;
        if (!result.ok()) {
            m_heroLane->fail(generation, result.error);
            return;
        }
        if (result.value.isValid()) {
            const ContinueListening &resume = result.value;
            setHero(resume.album, QStringLiteral("resume"), resume.resumeIndex, resume.progress);
            m_heroLane->succeed(generation);
            return;
        }
        if (m_hero.value(QStringLiteral("mode")).toString() == QLatin1String("pullOne")) {
            // Still no history: keep the record already on screen.
            m_heroLane->succeed(generation);
            return;
        }
        pullOneHero(generation);
    });
}

void MusicHomeController::pullOneHero(quint64 generation)
{
    m_repository->randomAlbums(m_libraryId, 1).then(this, [this, generation](Result<QList<Album>> result) {
        if (!m_heroLane->isCurrent(generation))
            return;
        if (!result.ok()) {
            m_heroLane->fail(generation, result.error);
            return;
        }
        if (result.value.isEmpty())
            clearHero();
        else
            setHero(result.value.first(), QStringLiteral("pullOne"), 0, 0.0);
        m_heroLane->succeed(generation);
    });
}

void MusicHomeController::loadRecent(Load load)
{
    track<QList<Album>>(m_recentLane, load, m_repository->recentAlbums(m_libraryId, kShelfLimit),
                        [this](QList<Album> &&albums) {
                            replaceItems(m_recent, std::move(albums));
                            fillHeroRecent();
                        });
}

void MusicHomeController::loadNew(Load load)
{
    track<NewAlbums>(m_newLane, load, m_repository->newAlbums(m_libraryId, kShelfLimit),
                     [this](NewAlbums &&result) {
                         replaceItems(m_new, std::move(result.albums));
                         setShelfText(m_addedThisWeekText,
                                      result.addedThisWeek > 0
                                          ? tr("%n added this week", nullptr, result.addedThisWeek)
                                          : QString());
                     });
}

void MusicHomeController::loadGenres(Load load)
{
    track<Page<GenreBin>>(m_genreLane, load, m_repository->genreBins(m_libraryId, kGenreBins),
                          [this](Page<GenreBin> &&page) {
                              QList<GenreBin> bins = std::move(page.items);
                              QString allText;
                              if (!bins.isEmpty()) {
                                  allText = tr("All %1 genres").arg(QLocale().toString(page.totalRecordCount));
                                  // The last bin opens Genres. An empty id is how
                                  // the page tells it from a real genre.
                                  GenreBin all;
                                  all.name = allText;
                                  for (const GenreBin &bin : std::as_const(bins)) {
                                      if (all.covers.size() == 3)
                                          break;
                                      if (!bin.covers.isEmpty())
                                          all.covers.append(bin.covers.first());
                                  }
                                  bins.append(all);
                              }
                              setShelfText(m_allGenresText, allText);
                              replaceItems(m_genres, std::move(bins));
                          });
}

void MusicHomeController::loadArtists(Load load)
{
    track<QList<Artist>>(
        m_artistLane, load, m_repository->topArtists(m_libraryId, kShelfLimit),
        [this](QList<Artist> &&artists) {
            m_topArtist = artists.isEmpty() ? Artist{} : artists.first();
            replaceItems(m_artists, std::move(artists));
            if (m_stationsBuilt && m_topArtist.id != m_stationTopId)
                rebuildStations();
        },
        [this] {
            m_artistsSettled = true;
            maybeBuildStations();
        });
}

void MusicHomeController::loadForgotten(Load load)
{
    track<QList<Album>>(m_forgottenLane, load, m_repository->forgottenFavourites(m_libraryId, kShelfLimit),
                        [this](QList<Album> &&albums) { replaceItems(m_forgotten, std::move(albums)); });
}

void MusicHomeController::loadPull(Load load)
{
    track<QList<Album>>(
        m_pullLane, load, m_repository->randomAlbums(m_libraryId, kShelfLimit),
        [this](QList<Album> &&albums) {
            m_pool = albums;
            replaceItems(m_pull, std::move(albums));
        },
        [this] {
            m_poolSettled = true;
            maybeBuildStations();
        });
}

void MusicHomeController::maybeBuildStations()
{
    if (!m_stationsBuilt && m_poolSettled && m_artistsSettled)
        buildStations();
}

void MusicHomeController::buildStations()
{
    m_stations->setStations(MusicRepository::stations(m_pool, m_topArtist));
    m_stationTopId = m_topArtist.id;
    m_stationsBuilt = true;
    m_stationLane->succeed(m_stationGeneration);
}

void MusicHomeController::rebuildStations()
{
    m_stationGeneration = m_stationLane->beginRefresh();
    buildStations();
}

void MusicHomeController::setHero(const Album &album, const QString &mode, int resumeIndex, double progress)
{
    m_heroAlbum->setItems({album});

    const QString artist = joinNames(album.albumArtists);
    QStringList parts;
    if (!artist.isEmpty())
        parts.append(artist);
    if (album.year > 0)
        parts.append(QString::number(album.year));
    if (album.trackCount > 0)
        parts.append(formatTrackCount(album.trackCount));
    const QString runtime = formatRuntime(album.runtimeMs);
    if (!runtime.isEmpty())
        parts.append(runtime);

    const bool resume = mode == QLatin1String("resume");
    QVariantMap hero;
    hero.insert(QStringLiteral("mode"), mode);
    hero.insert(QStringLiteral("albumId"), album.id);
    hero.insert(QStringLiteral("title"), album.title);
    hero.insert(QStringLiteral("artist"), artist);
    hero.insert(QStringLiteral("artistId"), album.albumArtists.isEmpty() ? QString() : album.albumArtists.first().id);
    hero.insert(QStringLiteral("year"), album.year);
    hero.insert(QStringLiteral("summary"), parts.join(QStringLiteral(" · ")));
    hero.insert(QStringLiteral("coverUrl"), coverUrl(album.coverRef));
    hero.insert(QStringLiteral("progress"), progress);
    hero.insert(QStringLiteral("resumeIndex"), resumeIndex);
    hero.insert(QStringLiteral("resumeLabel"), resume ? tr("Resume track %1").arg(resumeIndex + 1) : tr("Play"));
    hero.insert(QStringLiteral("favourite"), album.favourite);
    hero.insert(QStringLiteral("albumItem"), m_heroAlbum->get(0));
    if (hero != m_hero) {
        m_hero = hero;
        emit heroChanged();
    }
    fillHeroRecent();
}

void MusicHomeController::clearHero()
{
    m_heroAlbum->clear();
    if (!m_hero.isEmpty()) {
        m_hero.clear();
        emit heroChanged();
    }
}

void MusicHomeController::syncHeroFavourite()
{
    if (m_hero.isEmpty() || m_heroAlbum->count() == 0)
        return;
    const QVariantMap item = m_heroAlbum->get(0);
    const bool favourite = item.value(QStringLiteral("favourite")).toBool();
    if (m_hero.value(QStringLiteral("favourite")).toBool() == favourite)
        return;
    m_hero.insert(QStringLiteral("favourite"), favourite);
    m_hero.insert(QStringLiteral("albumItem"), item);
    emit heroChanged();
}

void MusicHomeController::fillHeroRecent()
{
    const QString heroId = m_hero.value(QStringLiteral("albumId")).toString();
    QList<Album> beside;
    for (const Album &album : m_recent->items()) {
        if (album.id == heroId)
            continue;
        beside.append(album);
        if (beside.size() == kHeroRecent)
            break;
    }
    replaceItems(m_heroRecent, std::move(beside));
}

void MusicHomeController::setShelfText(QString &field, const QString &value)
{
    if (field == value)
        return;
    field = value;
    emit shelfTextChanged();
}

std::optional<Station> MusicHomeController::stationAt(int row) const
{
    const QString key = m_stations->idAt(row);
    return key.isEmpty() ? std::nullopt : m_stations->stationFor(key);
}

void MusicHomeController::resumeHero()
{
    const QString albumId = m_hero.value(QStringLiteral("albumId")).toString();
    if (albumId.isEmpty())
        return;
    m_playback->playAlbum(albumId, m_hero.value(QStringLiteral("title")).toString(),
                          m_hero.value(QStringLiteral("resumeIndex")).toInt());
}

void MusicHomeController::shuffleHero()
{
    const QString albumId = m_hero.value(QStringLiteral("albumId")).toString();
    if (!albumId.isEmpty())
        m_playback->shuffleAlbum(albumId, m_hero.value(QStringLiteral("title")).toString());
}

void MusicHomeController::anotherOne()
{
    if (!m_libraryId.isEmpty())
        pullOneHero(m_heroLane->beginRefresh());
}

void MusicHomeController::reshuffle()
{
    if (!m_libraryId.isEmpty())
        loadPull(Load::Refresh);
}

void MusicHomeController::playStation(int row)
{
    if (const auto station = stationAt(row))
        m_playback->playStation(m_libraryId, *station);
}

void MusicHomeController::shuffleStation(int row)
{
    if (const auto station = stationAt(row))
        m_playback->shuffleStation(m_libraryId, *station);
}

void MusicHomeController::queueStation(int row)
{
    if (const auto station = stationAt(row))
        m_playback->queueStation(m_libraryId, *station);
}

QString MusicHomeController::cycleSection(int step) const
{
    const QStringList keys = sectionKeys();
    const int count = static_cast<int>(keys.size());
    return keys.at(((step % count) + count) % count); // Home is index 0
}

void MusicHomeController::clearAll()
{
    for (MusicLane *lane : {m_heroLane, m_recentLane, m_newLane, m_stationLane, m_genreLane, m_artistLane,
                            m_forgottenLane, m_pullLane}) {
        lane->reset();
    }
    for (AlbumGridModel *albums : {m_heroRecent, m_recent, m_new, m_forgotten, m_pull})
        albums->clear();
    m_stations->setStations({});
    m_artists->clear();
    m_genres->clear();
    clearHero();
    setShelfText(m_addedThisWeekText, QString());
    setShelfText(m_allGenresText, QString());
    m_pool.clear();
    m_topArtist = Artist{};
    m_stationTopId.clear();
    m_poolSettled = false;
    m_artistsSettled = false;
    m_stationsBuilt = false;
}

void MusicHomeController::resetSessionState()
{
    clearAll();
    if (!m_libraryId.isEmpty()) {
        m_libraryId.clear();
        emit libraryIdChanged();
    }
}

} // namespace strmqt::music
```

Add to the `strmqt_app` list in `src/CMakeLists.txt`, after the `MusicLane` line:

```cmake
    app/controllers/music/MusicHomeController.h app/controllers/music/MusicHomeController.cpp
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `cmake --build --preset dev --target tst_music_home_controller && ctest --preset dev -R "tst_music_home_controller|tst_music_playback" --output-on-failure`
Expected: PASS (`tst_music_home_controller`: 9 tests; `tst_music_playback` unchanged).

If `heroResumesTheLastAlbum` fails only on `summary`, the mapper has read a different runtime or year from the fixture. Print `hero.value("summary")` and fix the **fixture**, never the expected format: `albumJson(…, 4, 16)` plus `ProductionYear` must read "Artist a-ar · 1997 · 4 tracks · 16 min".

If `lanesFailIndependently` still sees a genre lane without an error, check `MusicRepository::allGenres` in the Phase 1 source for the request it sends when `caps::kGenreItemCounts` is false. Route that request with status 500 in `routeGenres`, using the same required keys it actually sends.

- [ ] **Step 7: Commit**

```bash
git add src/app/controllers/music/MusicHomeController.h src/app/controllers/music/MusicHomeController.cpp src/app/music/MusicPlayback.h src/app/music/MusicPlayback.cpp src/CMakeLists.txt tests/integration/tst_music_home_controller.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'MSG'
feat(music): add MusicHomeController with independent shelves

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
)"
```

---
### Task 3: Application wiring and `MusicHomeCtl`

**Files:**
- Modify: `src/app/Application.h` (forward declaration, accessor, member)
- Modify: `src/app/Application.cpp` (construction after the Phase 1 music block; `teardownAuthenticatedSession`)
- Modify: `src/app/main.cpp` (the `MusicHomeCtl` context property)

**Interfaces:**
- Consumes: `MusicHomeController` (Task 2), `Application::musicRepository()`, `musicRelay()`, `musicPlayback()` (Phase 1 Task 19), `MusicUserDataRelay::addModel`.
- Produces:
  - `music::MusicHomeController *Application::musicHome() const`
  - The QML context property `MusicHomeCtl`.

This task only wires objects together, so it has no new unit test. The full suite and the self-test are its test: `Main.qml` constructs every page against the real context properties. `MusicHomePage` does not exist yet, so the self-test here only proves that start-up is clean.

- [ ] **Step 1: Declare the member**

In `src/app/Application.h`, add a line to the `namespace music { … }` forward declarations that Phase 1 added:

```cpp
class MusicHomeController;
```

Next to `musicPlayback()`:

```cpp
    music::MusicHomeController *musicHome() const { return m_musicHome; }
```

Next to `m_musicPlayback`:

```cpp
    music::MusicHomeController *m_musicHome = nullptr;
```

- [ ] **Step 2: Construct, register the models, reset on sign-out**

In `src/app/Application.cpp`, add:

```cpp
#include "controllers/music/MusicHomeController.h"
```

Immediately after the Phase 1 block (the last `connect(m_live, &LiveUpdateService::refreshRequested, m_musicRepository, …);`), add:

```cpp
    // Music Home (Crate spec §4). Every model it owns, the hidden hero album
    // included, is patched in place by the relay when user data changes.
    m_musicHome = new music::MusicHomeController(m_musicRepository, m_musicPlayback, this);
    for (music::MusicModelBase *model : m_musicHome->models())
        m_musicRelay->addModel(model);
```

In `teardownAuthenticatedSession()`, directly after `m_music->resetSessionState();`:

```cpp
    m_musicHome->resetSessionState();
```

- [ ] **Step 3: Expose `MusicHomeCtl`**

In `src/app/main.cpp`, add `#include "controllers/music/MusicHomeController.h"`. After the `MusicPlay` line that Phase 1 added:

```cpp
    engine.rootContext()->setContextProperty(QStringLiteral("MusicHomeCtl"), app.musicHome());
```

- [ ] **Step 4: Build, test, self-test**

Run:

```bash
cmake --preset dev && cmake --build --preset dev && ctest --preset dev --output-on-failure
STRMQT_SELFTEST=1 QT_QPA_PLATFORM=offscreen QT_ASSUME_STDERR_HAS_CONSOLE=1 ./build/dev/strmqt; echo "exit=$?"
```

Expected: every test passes; the self-test prints no `selftest FAIL` line and ends with `exit=0`.

- [ ] **Step 5: Commit**

```bash
git add src/app/Application.h src/app/Application.cpp src/app/main.cpp
git commit -m "$(cat <<'MSG'
feat(music): wire MusicHomeController into the application

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
)"
```

---

### Task 4: `cardComponent` in `StrmRail` and `StrmGrid`

**Files:**
- Modify: `src/ui/controls/StrmRail.qml`
- Modify: `src/ui/controls/StrmGrid.qml`
- Test: `tests/unit/tst_card_component.cpp`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: the existing delegates, `NavigationColumn`, `NavigationFocusRestorer`.
- Produces:
  - On both `StrmRail` and `StrmGrid`: `property Component cardComponent: null`, `property int customCardWidth: 0`, `property int customCardHeight: 0`, `readonly property bool customCards`.
  - On `StrmRail` only: `property bool showHeading: true`.
  - The custom-card protocol. The loaded item may declare plain (not `required`) properties `model`, `index`, `current` and `hovered`, and the view binds whichever of them it finds. The view connects whichever of the signals `activated()`, `playRequested()` and `menuRequested(real x, real y)` the item declares.

The cell stays the focus owner, and the view keeps its `currentIndex`, `NavigationColumn` behaviour, focus restorer and paging. Only the drawing changes. Hover comes from a `HoverHandler` on the cell, so the view's `hoveredIndex` keeps working and hover never moves focus.

- [x] **Step 1: Write the failing test**

`tests/unit/tst_card_component.cpp`:

```cpp
#include <QDir>
#include <QFile>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>
#include <QtTest>

namespace {

const char *kProbe = R"QML(
import QtQuick
import StrmQt

Item {
    id: root
    width: 900
    height: 760
    focus: true

    property int railActivated: -1
    property int railPlayed: -1
    property int railMenu: -1
    property int gridActivated: -1

    component Tile: Item {
        property var model
        property int index: -1
        property bool current: false
        property bool hovered: false
        signal activated()
        signal playRequested()
        signal menuRequested(real x, real y)
        width: 120
        height: 90
    }

    ListModel {
        id: rows
        ListElement { itemId: "a"; name: "A" }
        ListElement { itemId: "b"; name: "B" }
        ListElement { itemId: "c"; name: "C" }
        ListElement { itemId: "d"; name: "D" }
    }

    StrmRail {
        id: rail
        objectName: "rail"
        width: 900
        title: "Custom"
        showHeading: false
        railModel: rows
        customCardWidth: 120
        customCardHeight: 90
        navigationFocusKey: "probe-rail"
        cardComponent: Component {
            Tile { objectName: "rail-tile-" + (model ? model.itemId : "") }
        }
        onItemActivated: index => root.railActivated = index
        onItemPlayRequested: index => root.railPlayed = index
        onMenuRequested: (index, x, y) => root.railMenu = index
    }

    StrmRail {
        id: stock
        objectName: "stock"
        y: 200
        width: 900
        title: "Stock"
        railModel: rows
    }

    StrmGrid {
        id: grid
        objectName: "grid"
        y: 520
        width: 900
        height: 240
        gridModel: rows
        customCardWidth: 120
        customCardHeight: 90
        cardComponent: Component {
            Tile { objectName: "grid-tile-" + (model ? model.itemId : "") }
        }
        onItemActivated: index => root.gridActivated = index
    }
}
)QML";

QQuickItem *findItem(QQuickItem *from, const QString &name)
{
    if (!from)
        return nullptr;
    if (from->objectName() == name)
        return from;
    for (QQuickItem *child : from->childItems()) {
        if (QQuickItem *found = findItem(child, name))
            return found;
    }
    return nullptr;
}

QQuickItem *createProbe(QTemporaryDir &dir, QQuickView &view)
{
    const QString modulePath = dir.filePath(QStringLiteral("StrmQt"));
    if (!QDir().mkpath(modulePath))
        return nullptr;
    const QStringList moduleFiles = {
        QStringLiteral("Theme.qml"),          QStringLiteral("FocusRing.qml"),
        QStringLiteral("StrmIcon.qml"),       QStringLiteral("StrmTooltip.qml"),
        QStringLiteral("StrmIconButton.qml"), QStringLiteral("StrmButton.qml"),
        QStringLiteral("StrmCard.qml"),       QStringLiteral("StrmImage.qml"),
        QStringLiteral("StrmScrollBar.qml"),  QStringLiteral("NavigationFocusRestorer.qml"),
        QStringLiteral("NavigationColumn.qml"), QStringLiteral("StrmRail.qml"),
        QStringLiteral("StrmGrid.qml"),
    };
    for (const QString &name : moduleFiles) {
        const QString sourceRoot = name == QStringLiteral("Theme.qml")
                                       ? QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/")
                                       : QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/controls/");
        if (!QFile::copy(sourceRoot + name, modulePath + QLatin1Char('/') + name))
            return nullptr;
    }
    QFile qmldir(modulePath + QStringLiteral("/qmldir"));
    if (!qmldir.open(QIODevice::WriteOnly))
        return nullptr;
    qmldir.write("module StrmQt\n"
                 "singleton Theme 1.0 Theme.qml\n"
                 "singleton NavigationColumn 1.0 NavigationColumn.qml\n"
                 "FocusRing 1.0 FocusRing.qml\n"
                 "StrmIcon 1.0 StrmIcon.qml\n"
                 "StrmTooltip 1.0 StrmTooltip.qml\n"
                 "StrmIconButton 1.0 StrmIconButton.qml\n"
                 "StrmButton 1.0 StrmButton.qml\n"
                 "StrmCard 1.0 StrmCard.qml\n"
                 "StrmImage 1.0 StrmImage.qml\n"
                 "StrmScrollBar 1.0 StrmScrollBar.qml\n"
                 "NavigationFocusRestorer 1.0 NavigationFocusRestorer.qml\n"
                 "StrmRail 1.0 StrmRail.qml\n"
                 "StrmGrid 1.0 StrmGrid.qml\n");
    qmldir.close();

    QFile probe(dir.filePath(QStringLiteral("Probe.qml")));
    if (!probe.open(QIODevice::WriteOnly))
        return nullptr;
    probe.write(kProbe);
    probe.close();

    view.engine()->addImportPath(dir.path());
    view.setSource(QUrl::fromLocalFile(probe.fileName()));
    if (view.status() != QQuickView::Ready)
        return nullptr;
    view.resize(900, 760);
    view.show();
    if (!QTest::qWaitForWindowExposed(&view))
        return nullptr;
    return view.rootObject();
}

} // namespace

class CardComponentTest : public QObject
{
    Q_OBJECT

private slots:
    void railLoadsTheCustomCardWithItsRow();
    void currentFollowsKeyboardFocus();
    void cardSignalsReachTheRail();
    void menuKeyAsksForTheCurrentCard();
    void hoverIsNotFocus();
    void gridLoadsTheCustomCard();
    void stockRailIsUnchanged();

private:
    QTemporaryDir m_dir;
    QQuickView m_view;
    QQuickItem *m_root = nullptr;
    void ensureProbe();
};

void CardComponentTest::ensureProbe()
{
    if (!m_root) {
        QVERIFY(m_dir.isValid());
        m_root = createProbe(m_dir, m_view);
    }
    QVERIFY(m_root);
}

void CardComponentTest::railLoadsTheCustomCardWithItsRow()
{
    ensureProbe();
    QQuickItem *rail = findItem(m_root, QStringLiteral("rail"));
    QVERIFY(rail);
    QTRY_VERIFY(findItem(rail, QStringLiteral("rail-tile-a")));
    QQuickItem *tile = findItem(rail, QStringLiteral("rail-tile-b"));
    QVERIFY(tile);
    QCOMPARE(tile->property("index").toInt(), 1);
    QCOMPARE(rail->property("cardWidth").toInt(), 120);
    QCOMPARE(rail->property("cardHeight").toInt(), 90);
    QVERIFY(rail->property("customCards").toBool());
    // No heading: the rail is exactly its shelf.
    const int padding = rail->property("rowPadding").toInt();
    QCOMPARE(qRound(rail->height()), 90 + padding * 2);
}

void CardComponentTest::currentFollowsKeyboardFocus()
{
    ensureProbe();
    QQuickItem *rail = findItem(m_root, QStringLiteral("rail"));
    QVERIFY(rail);
    QQuickItem *a = findItem(rail, QStringLiteral("rail-tile-a"));
    QVERIFY(a);
    QVERIFY(!a->property("current").toBool());

    rail->forceActiveFocus();
    QTRY_VERIFY(a->property("current").toBool());
    QTest::keyClick(&m_view, Qt::Key_Right);
    QTRY_COMPARE(rail->property("currentIndex").toInt(), 1);
    QQuickItem *b = findItem(rail, QStringLiteral("rail-tile-b"));
    QVERIFY(b);
    QTRY_VERIFY(b->property("current").toBool());
    QVERIFY(!a->property("current").toBool());
}

void CardComponentTest::cardSignalsReachTheRail()
{
    ensureProbe();
    QQuickItem *rail = findItem(m_root, QStringLiteral("rail"));
    QQuickItem *c = findItem(rail, QStringLiteral("rail-tile-c"));
    QVERIFY(c);

    QVERIFY(QMetaObject::invokeMethod(c, "activated"));
    QCOMPARE(m_root->property("railActivated").toInt(), 2);
    QCOMPARE(rail->property("currentIndex").toInt(), 2); // a click is a commit

    QVERIFY(QMetaObject::invokeMethod(c, "playRequested"));
    QCOMPARE(m_root->property("railPlayed").toInt(), 2);

    QVERIFY(QMetaObject::invokeMethod(c, "menuRequested", Q_ARG(double, 10.0), Q_ARG(double, 20.0)));
    QCOMPARE(m_root->property("railMenu").toInt(), 2);
}

void CardComponentTest::menuKeyAsksForTheCurrentCard()
{
    ensureProbe();
    QQuickItem *rail = findItem(m_root, QStringLiteral("rail"));
    m_root->setProperty("railMenu", -1);
    rail->forceActiveFocus();
    const int current = rail->property("currentIndex").toInt();
    QTest::keyClick(&m_view, Qt::Key_Menu);
    QCOMPARE(m_root->property("railMenu").toInt(), current);
}

void CardComponentTest::hoverIsNotFocus()
{
    ensureProbe();
    QQuickItem *rail = findItem(m_root, QStringLiteral("rail"));
    QQuickItem *d = findItem(rail, QStringLiteral("rail-tile-d"));
    QVERIFY(d);
    const int before = rail->property("currentIndex").toInt();
    const QPointF centre = d->mapToScene(QPointF(d->width() / 2, d->height() / 2));
    QTest::mouseMove(&m_view, centre.toPoint());
    QTRY_VERIFY(d->property("hovered").toBool());
    QTRY_COMPARE(rail->property("hoveredIndex").toInt(), 3);
    QCOMPARE(rail->property("currentIndex").toInt(), before);
    QTest::mouseMove(&m_view, QPoint(5, 700));
}

void CardComponentTest::gridLoadsTheCustomCard()
{
    ensureProbe();
    QQuickItem *grid = findItem(m_root, QStringLiteral("grid"));
    QVERIFY(grid);
    QTRY_VERIFY(findItem(grid, QStringLiteral("grid-tile-a")));
    QCOMPARE(grid->property("cardWidth").toInt(), 120);
    QQuickItem *b = findItem(grid, QStringLiteral("grid-tile-b"));
    QVERIFY(b);
    QCOMPARE(b->property("index").toInt(), 1);

    grid->forceActiveFocus();
    QQuickItem *a = findItem(grid, QStringLiteral("grid-tile-a"));
    QTRY_VERIFY(a->property("current").toBool());
    QVERIFY(QMetaObject::invokeMethod(b, "activated"));
    QCOMPARE(m_root->property("gridActivated").toInt(), 1);
    QCOMPARE(grid->property("currentIndex").toInt(), 1);
}

void CardComponentTest::stockRailIsUnchanged()
{
    ensureProbe();
    QQuickItem *stock = findItem(m_root, QStringLiteral("stock"));
    QVERIFY(stock);
    QVERIFY(!stock->property("customCards").toBool());
    QVERIFY(stock->property("cardWidth").toInt() > 0);
    QVERIFY(stock->property("cardWidth").toInt() != 120);
    QVERIFY(!findItem(stock, QStringLiteral("rail-tile-a")));
}

QTEST_MAIN(CardComponentTest)
#include "tst_card_component.moc"
```

Append to `tests/CMakeLists.txt`:

```cmake
strmqt_add_test(tst_card_component unit/tst_card_component.cpp)
target_link_libraries(tst_card_component PRIVATE Qt6::Gui Qt6::Quick)
target_compile_definitions(tst_card_component PRIVATE STRMQT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
set_tests_properties(tst_card_component PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [x] **Step 2: Run the test to verify it fails**

Run: `cmake --preset dev && cmake --build --preset dev --target tst_card_component && ctest --preset dev -R tst_card_component --output-on-failure`
Expected: FAIL. The probe does not load (`Cannot assign to non-existent property "showHeading"`), so `QVERIFY(m_root)` fails in every test.

- [x] **Step 3: Add the protocol to `StrmRail.qml`**

After `property bool navigationFocusRefillActive: false`, add:

```qml
    // A different card in place of StrmCard (a Crate sleeve, a portrait, a
    // station tile). The loaded item receives `model`, `index`, `current` and
    // `hovered` when it declares them as plain properties, and the rail
    // listens to its activated / playRequested / menuRequested signals. The
    // cell stays the focus owner, so NavigationColumn, the focus restorer and
    // paging behave exactly as they do for StrmCard. A hidden StrmCard cannot
    // measure a custom card, so the caller states its size.
    property Component cardComponent: null
    property int customCardWidth: 0
    property int customCardHeight: 0
    readonly property bool customCards: rail.cardComponent !== null
    // A Crate shelf draws its own heading.
    property bool showHeading: true
```

Replace:

```qml
    readonly property int cardWidth: metrics.implicitWidth
    readonly property int cardHeight: metrics.implicitHeight
```

with:

```qml
    readonly property int cardWidth: rail.customCards ? rail.customCardWidth : metrics.implicitWidth
    readonly property int cardHeight: rail.customCards ? rail.customCardHeight : metrics.implicitHeight
```

Replace `height: headingRow.height + Theme.spacingValue + list.height` with:

```qml
    height: headingRow.height + (rail.showHeading ? Theme.spacingValue : 0) + list.height
```

In `Item { id: headingRow … }`, replace `height: heading.implicitHeight` with:

```qml
        visible: rail.showHeading
        height: rail.showHeading ? heading.implicitHeight : 0
```

In `ListView { id: list … }`, replace `anchors.topMargin: Theme.spacingValue` with:

```qml
        anchors.topMargin: rail.showHeading ? Theme.spacingValue : 0
```

In the delegate `FocusScope { id: cell … }`:

1. Replace `ListView.onReused: cell.setHovered(cardItem.hovered)` with:

```qml
            ListView.onReused: cell.setHovered(rail.customCards ? cellHover.hovered : cardItem.hovered)
```

2. Directly after the `onIndexChanged: { … }` block, add the shared verbs, the cell hover and the loader:

```qml
            // One implementation of each verb, whichever card drew the cell.
            function cardActivated() {
                rail._cancelNavigationFocusForUser()
                // A click makes this card the keyboard's place too, so a
                // subsequent arrow key continues from where the user
                // clicked. This is a *commit*, not a hover.
                list.currentIndex = cell.index
                list.forceActiveFocus(Qt.MouseFocusReason)
                rail.itemActivated(cell.index)
            }
            function cardPlayRequested() {
                rail._cancelNavigationFocusForUser()
                rail.itemPlayRequested(cell.index)
            }
            function cardMenuRequested(mx, my) {
                rail._cancelNavigationFocusForUser()
                rail.menuRequested(cell.index, mx, my)
            }

            HoverHandler {
                id: cellHover
                enabled: rail.customCards
                onHoveredChanged: cell.setHovered(cellHover.hovered)
            }

            Loader {
                id: cardLoader
                anchors.centerIn: parent
                active: rail.customCards
                sourceComponent: rail.cardComponent
                // By name, not by type: a card declares only what it uses.
                onLoaded: {
                    const card = cardLoader.item
                    const bind = (name, value) => {
                        if (name in card)
                            card[name] = Qt.binding(value)
                    }
                    bind("model", () => cell.model)
                    bind("index", () => cell.index)
                    bind("current", () => cell.ListView.isCurrentItem && list.activeFocus)
                    bind("hovered", () => cellHover.hovered)
                    const connect = (name, handler) => {
                        if (typeof card[name] === "function")
                            card[name].connect(handler)
                    }
                    connect("activated", cell.cardActivated)
                    connect("playRequested", cell.cardPlayRequested)
                    connect("menuRequested", cell.cardMenuRequested)
                }
            }
```

3. In `StrmCard { id: cardItem … }`:
   - add `visible: !rail.customCards` and `enabled: !rail.customCards` after `anchors.centerIn: parent`;
   - make the first line of the `imageUrl` block `if (rail.customCards) return "";`, so a hidden card never fetches artwork;
   - replace the `onActivated`, `onPlayRequested` and `onMenuRequested` handlers with:

```qml
                onActivated: cell.cardActivated()
                onPlayRequested: cell.cardPlayRequested()
                onMenuRequested: (mx, my) => cell.cardMenuRequested(mx, my)
```

Keep `onPlayedToggled`, `onFavoriteToggled` and `onHoveredChanged` unchanged.

- [x] **Step 4: Add the protocol to `StrmGrid.qml`**

After `property bool navigationFocusRefillActive: false`, add:

```qml
    // The same custom-card protocol as StrmRail.cardComponent. List mode
    // ignores it: the row is the list-mode drawing for every grid.
    property Component cardComponent: null
    property int customCardWidth: 0
    property int customCardHeight: 0
    readonly property bool customCards: grid.cardComponent !== null
```

Replace the two size lines:

```qml
    readonly property int cardWidth: Math.round(metrics.implicitWidth * grid.cardScale)
    readonly property int cardHeight: Math.round(metrics.implicitHeight * grid.cardScale)
```

with:

```qml
    readonly property int cardWidth: Math.round((grid.customCards ? grid.customCardWidth
                                                                  : metrics.implicitWidth) * grid.cardScale)
    readonly property int cardHeight: Math.round((grid.customCards ? grid.customCardHeight
                                                                   : metrics.implicitHeight) * grid.cardScale)
```

In the delegate `FocusScope { id: cell … }`:

1. Replace the `GridView.onReused` handler with:

```qml
            GridView.onReused: cell.setHovered(grid.listMode ? rowHover.hovered
                                               : grid.customCards ? cellHover.hovered
                                               : cardItem.hovered)
```

2. Directly after `function open() { … }`, add:

```qml
            HoverHandler {
                id: cellHover
                enabled: grid.customCards && !grid.listMode
                onHoveredChanged: cell.setHovered(cellHover.hovered)
            }

            Loader {
                id: cardLoader
                anchors.centerIn: parent
                active: grid.customCards && !grid.listMode
                scale: grid.cardScale
                sourceComponent: grid.cardComponent
                onLoaded: {
                    const card = cardLoader.item
                    const bind = (name, value) => {
                        if (name in card)
                            card[name] = Qt.binding(value)
                    }
                    bind("model", () => cell.model)
                    bind("index", () => cell.index)
                    bind("current", () => cell.current)
                    bind("hovered", () => cellHover.hovered)
                    const connect = (name, handler) => {
                        if (typeof card[name] === "function")
                            card[name].connect(handler)
                    }
                    connect("activated", cell.open)
                    connect("playRequested", () => {
                        grid._cancelNavigationFocusForUser()
                        grid.itemPlayRequested(cell.index)
                    })
                    connect("menuRequested", (mx, my) => {
                        grid._cancelNavigationFocusForUser()
                        grid.menuRequested(cell.index, mx, my)
                    })
                }
            }
```

3. In `StrmCard { id: cardItem … }`, replace `visible: !grid.listMode` and `enabled: !grid.listMode` with:

```qml
                visible: !grid.listMode && !grid.customCards
                enabled: !grid.listMode && !grid.customCards
```

   Make the first line of its `imageUrl` block `if (grid.listMode || grid.customCards) return "";`, replacing the existing `if (grid.listMode) return "";`.

The scaled `Loader` fills a cell whose size already includes `cardScale`, which matches how `StrmCard` scales.

- [x] **Step 5: Run the tests to verify they pass**

Run: `cmake --build --preset dev && ctest --preset dev -R "tst_card_component|tst_navigation_history|tst_home_rails|tst_qml_accessibility" --output-on-failure`
Expected: PASS. `tst_card_component` runs 7 tests, and the existing rail and grid tests are unchanged.

If `hoverIsNotFocus` fails only because offscreen `QTest::mouseMove` delivers no hover, send the move twice (the first move enters the window) before the `QTRY` checks. Do not weaken the `currentIndex` assertion.

Lint:

```bash
bash scripts/check-qmllint-baseline.sh build/dev
```

Expected: no new warnings. Properties and signals are reached by name (`card[name]`), so qmllint has no type to complain about.

- [x] **Step 6: Commit**

```bash
git add src/ui/controls/StrmRail.qml src/ui/controls/StrmGrid.qml tests/unit/tst_card_component.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'MSG'
feat(ui): let StrmRail and StrmGrid draw a custom card component

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
)"
```

---

### Task 5: Phase 2 Crate controls, `Music.cmake` and its registration

**Files:**
- Create: `src/ui/music/Music.cmake`
- Create: `src/ui/music/CrateHeading.qml`, `CrateKicker.qml`, `CrateBadge.qml`, `CoverCollage.qml`, `CrateSleeve.qml`, `CratePortrait.qml`, `StationTile.qml`, `GenreBinTile.qml`, `SectionStrip.qml`, `ShelfError.qml`
- Modify: `src/CMakeLists.txt` (the guarded include)
- Test: `tests/unit/tst_crate_controls.cpp`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: the Phase 1 Crate tokens in `Theme.qml`, plus `FocusRing`, `StrmIcon`, `StrmImage`, `StrmAvatar`, `StrmButton` and `StrmIconButton`.
- Produces: the contract's Phase 2 control table except `CrateShelf`, which is Task 6. Every control keeps the contract names exactly. The additions are:
  - `CrateSleeve`, `CratePortrait`, `StationTile` and `GenreBinTile` declare `property bool hovered` (their own `HoverHandler` by default), so a rail's `cardComponent` can bind it (Task 4 protocol).
  - `CoverCollage` has `readonly property bool grid` (four or more covers).
  - `SectionStrip` has `property int cursor` and `function labelFor(key): string`.
  - `GenreBinTile` has `readonly property Item allArrow`, for the test.
- `Music.cmake` lists each file under `qt_target_qml_sources(strmqt QML_FILES …)`. Later phases append to that list.

None of these controls takes focus by itself except `SectionStrip` and `ShelfError`. Inside a rail the cell is the focus owner, and `current` draws the ring. Hover scales the art but never sets `current` or focus.

No control adds a colour literal beyond `"transparent"`, and a shadow is `Theme.shadowColor` on plain rectangles. `StrmCard` avoids a per-card `MultiEffect` because it breaks batching, and the sleeve follows it. The one exception is `CratePortrait`, which needs a circular mask; the Artists shelf holds at most 20 of them.

- [x] **Step 1: Write the failing test**

`tests/unit/tst_crate_controls.cpp`:

```cpp
#include <QDir>
#include <QFile>
#include <QFont>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>
#include <QtTest>

namespace {

const char *kProbe = R"QML(
import QtQuick
import StrmQt

Item {
    id: root
    width: 1000
    height: 900
    focus: true

    property int sleeveActivated: 0
    property int sleevePlayed: 0
    property int sleeveMenus: 0
    property int portraitActivated: 0
    property int stationActivated: 0
    property int genreActivated: 0
    property int retried: 0
    property string lastSection: ""
    property bool leftEscaped: false
    readonly property color hiResTone: Theme.crateBadgeHiRes
    readonly property string displayFamily: Theme.fontDisplay
    readonly property string monoFamily: Theme.fontMono

    Keys.onLeftPressed: root.leftEscaped = true

    CrateHeading { id: heading; objectName: "heading"; text: "New in the crate" }
    CrateKicker { id: kicker; objectName: "kicker"; y: 40; text: "3 added this week" }
    CrateBadge { objectName: "emptyBadge"; y: 60; text: "" }
    CrateBadge { objectName: "plainBadge"; x: 100; y: 60; text: "FLAC" }
    CrateBadge { objectName: "hiResBadge"; x: 200; y: 60; text: "24/96"; hiRes: true }

    CoverCollage { objectName: "oneCover"; y: 90; size: 80; covers: [""] }
    CoverCollage { objectName: "fourCovers"; x: 100; y: 90; size: 80; covers: ["", "", "", ""] }

    CrateSleeve {
        objectName: "sleeve"
        x: 20; y: 200
        size: 160
        title: "Moon Safari"
        subtitle: "Air · 1998"
        badge: "FLAC"
        onActivated: root.sleeveActivated++
        onPlayRequested: root.sleevePlayed++
        onMenuRequested: (mx, my) => root.sleeveMenus++
    }

    CratePortrait {
        objectName: "portrait"
        x: 220; y: 200
        size: 120
        name: "Air"
        subtitle: "12 records"
        onActivated: root.portraitActivated++
    }

    StationTile {
        objectName: "station"
        x: 380; y: 200
        size: 140
        label: "Heavy rotation"
        covers: ["", "", "", ""]
        onActivated: root.stationActivated++
    }

    GenreBinTile {
        objectName: "genre"
        x: 560; y: 200
        size: 160
        name: "Electronic"
        subtitle: "84 records"
        covers: ["", "", ""]
        onActivated: root.genreActivated++
    }

    GenreBinTile {
        objectName: "allGenres"
        x: 760; y: 200
        size: 160
        name: "All 42 genres"
        isAllBin: true
    }

    SectionStrip {
        id: strip
        objectName: "strip"
        y: 520
        currentKey: "home"
        onSectionChosen: key => root.lastSection = key
    }

    ShelfError {
        objectName: "shelfError"
        y: 600
        message: "Couldn't load"
        onRetry: root.retried++
    }
}
)QML";

const QStringList kControls = {
    QStringLiteral("FocusRing"),      QStringLiteral("StrmIcon"),    QStringLiteral("StrmTooltip"),
    QStringLiteral("StrmIconButton"), QStringLiteral("StrmButton"),  QStringLiteral("StrmImage"),
    QStringLiteral("StrmAvatar"),
};

const QStringList kMusic = {
    QStringLiteral("CrateHeading"), QStringLiteral("CrateKicker"),  QStringLiteral("CrateBadge"),
    QStringLiteral("CoverCollage"), QStringLiteral("CrateSleeve"),  QStringLiteral("CratePortrait"),
    QStringLiteral("StationTile"),  QStringLiteral("GenreBinTile"), QStringLiteral("SectionStrip"),
    QStringLiteral("ShelfError"),
};

QQuickItem *findItem(QQuickItem *from, const QString &name)
{
    if (!from)
        return nullptr;
    if (from->objectName() == name)
        return from;
    for (QQuickItem *child : from->childItems()) {
        if (QQuickItem *found = findItem(child, name))
            return found;
    }
    return nullptr;
}

bool stage(const QString &sourceDir, const QString &type, const QString &modulePath, QByteArray &qmldir)
{
    const QString file = type + QStringLiteral(".qml");
    if (!QFile::copy(sourceDir + file, modulePath + QLatin1Char('/') + file))
        return false;
    // The one singleton among the staged controls.
    const QByteArray prefix = type == QStringLiteral("NavigationColumn") ? "singleton " : "";
    qmldir += prefix + type.toUtf8() + " 1.0 " + file.toUtf8() + '\n';
    return true;
}

QQuickItem *createProbe(QTemporaryDir &dir, QQuickView &view)
{
    const QString modulePath = dir.filePath(QStringLiteral("StrmQt"));
    if (!QDir().mkpath(modulePath))
        return nullptr;
    QByteArray qmldir = "module StrmQt\nsingleton Theme 1.0 Theme.qml\n";
    if (!QFile::copy(QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/Theme.qml"), modulePath + QStringLiteral("/Theme.qml")))
        return nullptr;
    for (const QString &type : kControls) {
        if (!stage(QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/controls/"), type, modulePath, qmldir))
            return nullptr;
    }
    for (const QString &type : kMusic) {
        if (!stage(QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/music/"), type, modulePath, qmldir))
            return nullptr;
    }
    QFile qmldirFile(modulePath + QStringLiteral("/qmldir"));
    if (!qmldirFile.open(QIODevice::WriteOnly))
        return nullptr;
    qmldirFile.write(qmldir);
    qmldirFile.close();

    QFile probe(dir.filePath(QStringLiteral("Probe.qml")));
    if (!probe.open(QIODevice::WriteOnly))
        return nullptr;
    probe.write(kProbe);
    probe.close();

    view.engine()->addImportPath(dir.path());
    view.setSource(QUrl::fromLocalFile(probe.fileName()));
    if (view.status() != QQuickView::Ready) {
        qWarning() << view.errors();
        return nullptr;
    }
    view.resize(1000, 900);
    view.show();
    if (!QTest::qWaitForWindowExposed(&view))
        return nullptr;
    return view.rootObject();
}

QPoint centreOf(QQuickItem *item)
{
    return item->mapToScene(QPointF(item->width() / 2, item->width() / 2)).toPoint();
}

} // namespace

class CrateControlsTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void headingAndKickerUseCrateType();
    void badgeHidesWhenEmptyAndTurnsAmberForHiRes();
    void collageSwitchesToAGridAtFourCovers();
    void sleeveClicksAndMenus();
    void hoverIsNotFocus();
    void tilesActivate();
    void allGenresBinShowsTheArrow();
    void stripCyclesWithoutMovingItsKey();
    void stripKeyboardChoosesAndDeclinesLeftAtTheEdge();
    void shelfErrorRetries();

private:
    QTemporaryDir m_dir;
    QQuickView m_view;
    QQuickItem *m_root = nullptr;
    QQuickItem *item(const char *name) { return findItem(m_root, QString::fromLatin1(name)); }
};

void CrateControlsTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_root = createProbe(m_dir, m_view);
    QVERIFY(m_root);
}

void CrateControlsTest::headingAndKickerUseCrateType()
{
    const QFont heading = item("heading")->property("font").value<QFont>();
    QCOMPARE(heading.family(), m_root->property("displayFamily").toString());
    QCOMPARE(heading.capitalization(), QFont::AllUppercase);
    QCOMPARE(int(heading.weight()), 820);
    QVERIFY(heading.letterSpacing() < 0);

    const QFont kicker = item("kicker")->property("font").value<QFont>();
    QCOMPARE(kicker.family(), m_root->property("monoFamily").toString());
    QCOMPARE(kicker.capitalization(), QFont::AllUppercase);
    QVERIFY(kicker.letterSpacing() > 0);
}

void CrateControlsTest::badgeHidesWhenEmptyAndTurnsAmberForHiRes()
{
    QVERIFY(!item("emptyBadge")->isVisible());
    QVERIFY(item("plainBadge")->isVisible());
    const QObject *plainBorder = item("plainBadge")->property("border").value<QObject *>();
    const QObject *hiResBorder = item("hiResBadge")->property("border").value<QObject *>();
    QVERIFY(plainBorder && hiResBorder);
    const QColor hiRes = m_root->property("hiResTone").value<QColor>();
    QCOMPARE(hiResBorder->property("color").value<QColor>(), hiRes);
    QVERIFY(plainBorder->property("color").value<QColor>() != hiRes);
}

void CrateControlsTest::collageSwitchesToAGridAtFourCovers()
{
    QVERIFY(!item("oneCover")->property("grid").toBool());
    QVERIFY(item("fourCovers")->property("grid").toBool());
    QCOMPARE(qRound(item("fourCovers")->width()), 80);
}

void CrateControlsTest::sleeveClicksAndMenus()
{
    QQuickItem *sleeve = item("sleeve");
    QTest::mouseClick(&m_view, Qt::LeftButton, {}, centreOf(sleeve));
    QTRY_COMPARE(m_root->property("sleeveActivated").toInt(), 1);
    QTest::mouseClick(&m_view, Qt::RightButton, {}, centreOf(sleeve));
    QTRY_COMPARE(m_root->property("sleeveMenus").toInt(), 1);
    // The caption sits below the square art, so the sleeve is taller than wide.
    QVERIFY(sleeve->height() > sleeve->width());
}

void CrateControlsTest::hoverIsNotFocus()
{
    QQuickItem *sleeve = item("sleeve");
    m_root->forceActiveFocus();
    QTest::mouseMove(&m_view, centreOf(sleeve));
    QTest::mouseMove(&m_view, centreOf(sleeve) + QPoint(1, 1));
    QTRY_VERIFY(sleeve->property("hovered").toBool());
    QVERIFY(!sleeve->property("current").toBool());
    QVERIFY(!sleeve->hasActiveFocus());
    QVERIFY(m_root->hasActiveFocus());
}

void CrateControlsTest::tilesActivate()
{
    QTest::mouseClick(&m_view, Qt::LeftButton, {}, centreOf(item("portrait")));
    QTRY_COMPARE(m_root->property("portraitActivated").toInt(), 1);
    QTest::mouseClick(&m_view, Qt::LeftButton, {}, centreOf(item("station")));
    QTRY_COMPARE(m_root->property("stationActivated").toInt(), 1);
    QTest::mouseClick(&m_view, Qt::LeftButton, {}, centreOf(item("genre")));
    QTRY_COMPARE(m_root->property("genreActivated").toInt(), 1);
}

void CrateControlsTest::allGenresBinShowsTheArrow()
{
    const QQuickItem *arrow = item("allGenres")->property("allArrow").value<QQuickItem *>();
    QVERIFY(arrow);
    QVERIFY(arrow->isVisible());
    const QQuickItem *plainArrow = item("genre")->property("allArrow").value<QQuickItem *>();
    QVERIFY(plainArrow);
    QVERIFY(!plainArrow->isVisible());
}

void CrateControlsTest::stripCyclesWithoutMovingItsKey()
{
    QQuickItem *strip = item("strip");
    QVariant consumed;
    QVERIFY(QMetaObject::invokeMethod(strip, "cycle", Q_RETURN_ARG(QVariant, consumed), Q_ARG(QVariant, 1)));
    QVERIFY(consumed.toBool());
    QCOMPARE(m_root->property("lastSection").toString(), QStringLiteral("albums"));
    QCOMPARE(strip->property("currentKey").toString(), QStringLiteral("home"));

    QVERIFY(QMetaObject::invokeMethod(strip, "cycle", Q_RETURN_ARG(QVariant, consumed), Q_ARG(QVariant, -1)));
    QCOMPARE(m_root->property("lastSection").toString(), QStringLiteral("playlists"));

    strip->setProperty("keys", QStringList{QStringLiteral("home")});
    QVERIFY(QMetaObject::invokeMethod(strip, "cycle", Q_RETURN_ARG(QVariant, consumed), Q_ARG(QVariant, 1)));
    QVERIFY(!consumed.toBool());
    strip->setProperty("keys", QStringList{QStringLiteral("home"), QStringLiteral("albums"), QStringLiteral("artists"),
                                           QStringLiteral("songs"), QStringLiteral("genres"),
                                           QStringLiteral("playlists")});
}

void CrateControlsTest::stripKeyboardChoosesAndDeclinesLeftAtTheEdge()
{
    QQuickItem *strip = item("strip");
    m_root->setProperty("lastSection", QString());
    m_root->setProperty("leftEscaped", false);
    strip->forceActiveFocus();
    QTRY_VERIFY(strip->hasActiveFocus());
    QCOMPARE(strip->property("cursor").toInt(), 0);

    QTest::keyClick(&m_view, Qt::Key_Left);
    QVERIFY(m_root->property("leftEscaped").toBool());

    QTest::keyClick(&m_view, Qt::Key_Right);
    QCOMPARE(strip->property("cursor").toInt(), 1);
    QVERIFY(m_root->property("lastSection").toString().isEmpty());
    QTest::keyClick(&m_view, Qt::Key_Return);
    QCOMPARE(m_root->property("lastSection").toString(), QStringLiteral("albums"));

    // Losing focus puts the cursor back on the section that is on screen.
    m_root->forceActiveFocus();
    QTRY_COMPARE(strip->property("cursor").toInt(), 0);
}

void CrateControlsTest::shelfErrorRetries()
{
    QQuickItem *error = item("shelfError");
    error->forceActiveFocus();
    QTest::keyClick(&m_view, Qt::Key_Return);
    QTRY_COMPARE(m_root->property("retried").toInt(), 1);
}

QTEST_MAIN(CrateControlsTest)
#include "tst_crate_controls.moc"
```

Append to `tests/CMakeLists.txt`:

```cmake
strmqt_add_test(tst_crate_controls unit/tst_crate_controls.cpp)
target_link_libraries(tst_crate_controls PRIVATE Qt6::Gui Qt6::Quick)
target_compile_definitions(tst_crate_controls PRIVATE STRMQT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
set_tests_properties(tst_crate_controls PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [x] **Step 2: Run the test to verify it fails**

Run: `cmake --build --preset dev --target tst_crate_controls && ctest --preset dev -R tst_crate_controls --output-on-failure`
Expected: FAIL in `initTestCase`, because `src/ui/music/CrateHeading.qml` does not exist and staging the module fails.

- [x] **Step 3: Write the controls**

`src/ui/music/CrateHeading.qml`:

```qml
import QtQuick
import StrmQt

// Crate display type (spec §2): wide Archivo caps. The section strip, shelf
// headings, hero titles and genre bins all speak in this one voice, so its
// settings live here and nowhere else.
Text {
    id: heading

    property int pixelSize: Theme.crateShelfHeading

    color: Theme.textPrimaryColor
    font.family: Theme.fontDisplay
    font.pixelSize: heading.pixelSize
    font.weight: Theme.crateDisplayWeight
    font.variableAxes: Theme.crateDisplayAxes
    font.capitalization: Font.AllUppercase
    // Tracking is an em value; Qt wants pixels.
    font.letterSpacing: Theme.crateDisplayTracking * heading.pixelSize
    textFormat: Text.PlainText
    maximumLineCount: 1
    elide: Text.ElideRight
}
```

`src/ui/music/CrateKicker.qml`:

```qml
import QtQuick
import StrmQt

// The small mono line above a heading (spec §2): Plex Mono, uppercase, wide
// tracking, tabular figures so "12 ADDED THIS WEEK" does not jitter as it counts.
Text {
    id: kicker

    color: Theme.textSecondaryColor
    font.family: Theme.fontMono
    font.pixelSize: Theme.crateKickerSize
    font.capitalization: Font.AllUppercase
    font.letterSpacing: Theme.crateKickerTracking * Theme.crateKickerSize
    font.features: ({ "tnum": 1 })
    textFormat: Text.PlainText
    maximumLineCount: 1
    elide: Text.ElideRight
}
```

`src/ui/music/CrateBadge.qml`:

```qml
import QtQuick
import StrmQt

// A format or release badge (spec §2): mono type in a hairline box. The hi-res
// variant is the accent, which is the only colour a badge may change to.
Rectangle {
    id: badge

    property string text: ""
    property bool hiRes: false

    readonly property color tone: badge.hiRes ? Theme.crateBadgeHiRes : Theme.crateBadgeBorder

    visible: badge.text.length > 0
    implicitWidth: label.implicitWidth + Theme.scale(10)
    implicitHeight: label.implicitHeight + Theme.scale(4)
    width: badge.implicitWidth
    height: badge.implicitHeight
    radius: Theme.crateBadgeRadius
    color: "transparent"
    border.width: Theme.crateBadgeBorderWidth
    border.color: badge.tone

    Accessible.role: Accessible.StaticText
    Accessible.name: badge.text

    Text {
        id: label

        anchors.centerIn: parent
        text: badge.text
        color: badge.hiRes ? Theme.crateBadgeHiRes : Theme.textSecondaryColor
        font.family: Theme.fontMono
        font.pixelSize: Theme.crateBadgeSize
        font.capitalization: Font.AllUppercase
        font.features: ({ "tnum": 1 })
        textFormat: Text.PlainText
    }
}
```

`src/ui/music/CoverCollage.qml`:

```qml
import QtQuick
import StrmQt

// A square of covers: a 2×2 grid when there are four, else the first one whole.
// Stations draw one; nothing about it is interactive.
Item {
    id: collage

    property var covers: []
    property int size: Theme.crateSleeveSize
    property int radius: Theme.crateSleeveRadius

    readonly property int coverCount: collage.covers ? collage.covers.length : 0
    readonly property bool grid: collage.coverCount >= 4

    width: collage.size
    height: collage.size

    Rectangle {
        anchors.fill: parent
        radius: collage.radius
        color: Theme.surfaceRaisedColor
        clip: true

        StrmIcon {
            anchors.centerIn: parent
            name: "lib-music"
            size: Math.round(collage.size / 3)
            color: Theme.textTertiary
        }

        StrmImage {
            anchors.fill: parent
            visible: !collage.grid
            source: !collage.grid && collage.coverCount > 0 ? String(collage.covers[0]) : ""
            suppressWarnings: true
        }

        Grid {
            anchors.fill: parent
            columns: 2
            visible: collage.grid

            Repeater {
                model: collage.grid ? 4 : 0

                delegate: StrmImage {
                    id: quarter

                    required property int index

                    width: collage.size / 2
                    height: collage.size / 2
                    source: String(collage.covers[quarter.index])
                    suppressWarnings: true
                }
            }
        }
    }
}
```

`src/ui/music/CrateSleeve.qml`:

```qml
import QtQuick
import StrmQt

// An album as a record shop shows it (spec §2): a square cover with a deep
// shadow and a caption under it. No card surface.
//
// Focus lives with whoever holds the sleeve. In a rail that is the cell, which
// drives `current`. Hover lifts the art a little, and never sets `current`.
Item {
    id: sleeve

    property string coverUrl: ""
    property string title: ""
    property string subtitle: ""
    property string badge: ""
    property bool hiRes: false
    property int size: Theme.crateSleeveSize
    property bool current: false
    property bool showCaption: true
    property bool hovered: hover.hovered

    signal activated()
    signal playRequested()
    signal menuRequested(real x, real y)

    readonly property real lift: sleeve.current ? Theme.focusScale
                                                : (sleeve.hovered ? Theme.hoverScale : 1.0)

    implicitWidth: sleeve.size
    implicitHeight: sleeve.size + (sleeve.showCaption ? Theme.spacingTight + caption.implicitHeight : 0)
    width: sleeve.implicitWidth
    height: sleeve.implicitHeight

    Accessible.role: Accessible.Button
    Accessible.name: sleeve.subtitle.length > 0 ? sleeve.title + ", " + sleeve.subtitle : sleeve.title
    Accessible.onPressAction: sleeve.activated()

    function requestMenuAt(px: real, py: real): void {
        const p = sleeve.mapToItem(null, px, py)
        sleeve.menuRequested(p.x, p.y)
    }

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        acceptedButtons: Qt.LeftButton
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: sleeve.activated()
        // A held press is the touch and remote path to the menu.
        onLongPressed: sleeve.requestMenuAt(art.width / 2, art.height * 0.75)
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: eventPoint => sleeve.requestMenuAt(eventPoint.position.x, eventPoint.position.y)
    }

    Item {
        id: art

        width: sleeve.size
        height: sleeve.size
        scale: sleeve.lift

        Behavior on scale {
            NumberAnimation {
                duration: sleeve.current ? Theme.animFastMs : Theme.animInstant
                easing.type: sleeve.current ? Theme.easeStandard : Theme.easeInstant
            }
        }

        // The deep shadow, as two offset plates. A MultiEffect per sleeve would
        // break batching across a shelf of twenty.
        Rectangle {
            x: -Theme.scale(2)
            y: Theme.crateSleeveElevation.y * 0.6
            width: parent.width + Theme.scale(4)
            height: parent.height
            radius: Theme.crateSleeveRadius + Theme.scale(4)
            color: Theme.shadowColor
            opacity: Theme.crateSleeveElevation.opacity * 0.3
        }

        Rectangle {
            y: Theme.crateSleeveElevation.y * 0.25
            width: parent.width
            height: parent.height
            radius: Theme.crateSleeveRadius
            color: Theme.shadowColor
            opacity: Theme.crateSleeveElevation.opacity * 0.6
        }

        Rectangle {
            id: cover

            anchors.fill: parent
            radius: Theme.crateSleeveRadius
            color: Theme.surfaceRaisedColor
            clip: true

            StrmIcon {
                anchors.centerIn: parent
                visible: image.status !== Image.Ready
                name: "lib-music"
                size: Math.round(sleeve.size / 3)
                color: Theme.textTertiary
            }

            StrmImage {
                id: image
                anchors.fill: parent
                source: sleeve.coverUrl
            }

            StrmIconButton {
                anchors.centerIn: parent
                // Shown for focus as well as hover: an affordance only a mouse can
                // find is the bug the controls library exists to prevent.
                opacity: (sleeve.hovered || sleeve.current) ? 1 : 0
                visible: opacity > 0.01
                iconName: "play"
                round: true
                tooltip: qsTr("Play")
                onClicked: sleeve.playRequested()

                Behavior on opacity {
                    NumberAnimation { duration: Theme.animInstant; easing.type: Theme.easeInstant }
                }
            }
        }

        FocusRing {
            active: sleeve.current
            radius: Theme.crateSleeveRadius + Theme.scale(3)
            inset: -Theme.scale(3)
        }
    }

    Column {
        id: caption

        visible: sleeve.showCaption
        anchors.top: art.bottom
        anchors.topMargin: Theme.spacingTight
        width: sleeve.size
        spacing: Theme.scale(2)

        Text {
            width: parent.width
            text: sleeve.title
            color: Theme.textPrimaryColor
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontSmall
            font.weight: Font.DemiBold
            textFormat: Text.PlainText
            elide: Text.ElideRight
            maximumLineCount: 1
        }

        Row {
            id: subtitleRow

            width: parent.width
            spacing: Theme.spacingTight

            Text {
                width: subtitleRow.width - (badgeItem.visible ? badgeItem.width + subtitleRow.spacing : 0)
                anchors.verticalCenter: parent.verticalCenter
                text: sleeve.subtitle
                color: Theme.textSecondaryColor
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontCaption
                textFormat: Text.PlainText
                elide: Text.ElideRight
                maximumLineCount: 1
            }

            CrateBadge {
                id: badgeItem
                anchors.verticalCenter: parent.verticalCenter
                text: sleeve.badge
                hiRes: sleeve.hiRes
            }
        }
    }
}
```

`src/ui/music/CratePortrait.qml`:

```qml
import QtQuick
import QtQuick.Effects
import StrmQt

// An artist as a round portrait with the name under it. StrmAvatar supplies the
// initials fallback; the circle is a mask because a clip cannot round an image.
Item {
    id: portrait

    property string imageUrl: ""
    property string name: ""
    property string subtitle: ""
    property int size: Theme.cratePortraitSize
    property bool current: false
    property bool hovered: hover.hovered

    signal activated()
    signal menuRequested(real x, real y)

    implicitWidth: portrait.size
    implicitHeight: portrait.size + Theme.spacingTight + caption.implicitHeight
    width: portrait.implicitWidth
    height: portrait.implicitHeight

    Accessible.role: Accessible.Button
    Accessible.name: portrait.subtitle.length > 0 ? portrait.name + ", " + portrait.subtitle : portrait.name
    Accessible.onPressAction: portrait.activated()

    function requestMenuAt(px: real, py: real): void {
        const p = portrait.mapToItem(null, px, py)
        portrait.menuRequested(p.x, p.y)
    }

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        acceptedButtons: Qt.LeftButton
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: portrait.activated()
        onLongPressed: portrait.requestMenuAt(frame.width / 2, frame.height * 0.75)
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: eventPoint => portrait.requestMenuAt(eventPoint.position.x, eventPoint.position.y)
    }

    Item {
        id: frame

        width: portrait.size
        height: portrait.size
        scale: portrait.current ? Theme.focusScale : (portrait.hovered ? Theme.hoverScale : 1.0)

        Behavior on scale {
            NumberAnimation {
                duration: portrait.current ? Theme.animFastMs : Theme.animInstant
                easing.type: portrait.current ? Theme.easeStandard : Theme.easeInstant
            }
        }

        StrmAvatar {
            id: avatar
            anchors.fill: parent
            imageUrl: portrait.imageUrl
            name: portrait.name
            iconName: "user"
            border.width: 0
            radius: 0
            visible: false
            layer.enabled: true
        }

        Rectangle {
            id: circle
            anchors.fill: parent
            radius: circle.width / 2
            visible: false
            layer.enabled: true
        }

        MultiEffect {
            anchors.fill: parent
            source: avatar
            maskEnabled: true
            maskSource: circle
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1.0
        }

        FocusRing {
            active: portrait.current
            radius: frame.width / 2
            inset: -Theme.scale(3)
        }
    }

    Column {
        id: caption

        anchors.top: frame.bottom
        anchors.topMargin: Theme.spacingTight
        width: portrait.size
        spacing: Theme.scale(2)

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: portrait.name
            color: Theme.textPrimaryColor
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontSmall
            font.weight: Font.DemiBold
            textFormat: Text.PlainText
            elide: Text.ElideRight
            maximumLineCount: 1
        }

        CrateKicker {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            visible: portrait.subtitle.length > 0
            text: portrait.subtitle
        }
    }
}
```

Every binding names its owner by id (`frame.width`, `portrait.subtitle`). A bare `width` or `text` is an `[unqualified]` lint warning, and warnings are errors.

`src/ui/music/StationTile.qml`:

```qml
import QtQuick
import StrmQt

// A station (spec §4): a 2×2 collage with a wide-caps label. One press
// resolves and plays; the menu offers Play / Shuffle / Add to queue.
Item {
    id: tile

    property var covers: []
    property string label: ""
    property int size: Theme.crateSleeveSize
    property bool current: false
    property bool hovered: hover.hovered

    signal activated()
    signal menuRequested(real x, real y)

    implicitWidth: tile.size
    implicitHeight: tile.size + Theme.spacingTight + caption.implicitHeight
    width: tile.implicitWidth
    height: tile.implicitHeight

    Accessible.role: Accessible.Button
    Accessible.name: qsTr("Station: %1").arg(tile.label)
    Accessible.onPressAction: tile.activated()

    function requestMenuAt(px: real, py: real): void {
        const p = tile.mapToItem(null, px, py)
        tile.menuRequested(p.x, p.y)
    }

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        acceptedButtons: Qt.LeftButton
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: tile.activated()
        onLongPressed: tile.requestMenuAt(art.width / 2, art.height * 0.75)
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: eventPoint => tile.requestMenuAt(eventPoint.position.x, eventPoint.position.y)
    }

    Item {
        id: art

        width: tile.size
        height: tile.size
        scale: tile.current ? Theme.focusScale : (tile.hovered ? Theme.hoverScale : 1.0)

        Behavior on scale {
            NumberAnimation {
                duration: tile.current ? Theme.animFastMs : Theme.animInstant
                easing.type: tile.current ? Theme.easeStandard : Theme.easeInstant
            }
        }

        Rectangle {
            y: Theme.crateSleeveElevation.y * 0.25
            width: parent.width
            height: parent.height
            radius: Theme.crateSleeveRadius
            color: Theme.shadowColor
            opacity: Theme.crateSleeveElevation.opacity * 0.6
        }

        CoverCollage {
            covers: tile.covers
            size: tile.size
        }

        FocusRing {
            active: tile.current
            radius: Theme.crateSleeveRadius + Theme.scale(3)
            inset: -Theme.scale(3)
        }
    }

    CrateHeading {
        id: caption

        anchors.top: art.bottom
        anchors.topMargin: Theme.spacingTight
        width: tile.size
        pixelSize: Theme.crateStripSize
        text: tile.label
    }
}
```

`src/ui/music/GenreBinTile.qml`:

```qml
import QtQuick
import StrmQt

// A genre as a record bin (spec §4): three covers fanned from the stack, the
// name in wide caps, "N records" under it. The all-genres bin ends in an arrow
// instead of a count.
Item {
    id: tile

    property string name: ""
    property string subtitle: ""
    property var covers: []
    property int size: Theme.crateSleeveSize
    property bool current: false
    property bool isAllBin: false
    property bool hovered: hover.hovered

    readonly property Item allArrow: arrow

    signal activated()

    implicitWidth: tile.size
    implicitHeight: stack.height + Theme.spacingTight + label.implicitHeight + Theme.scale(2) + meta.height
    width: tile.implicitWidth
    height: tile.implicitHeight

    Accessible.role: Accessible.Button
    Accessible.name: tile.subtitle.length > 0 ? tile.name + ", " + tile.subtitle : tile.name
    Accessible.onPressAction: tile.activated()

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        acceptedButtons: Qt.LeftButton
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: tile.activated()
    }

    Item {
        id: stack

        width: tile.size
        height: Math.round(tile.size * 0.8)
        scale: tile.current ? Theme.focusScale : (tile.hovered ? Theme.hoverScale : 1.0)

        Behavior on scale {
            NumberAnimation {
                duration: tile.current ? Theme.animFastMs : Theme.animInstant
                easing.type: tile.current ? Theme.easeStandard : Theme.easeInstant
            }
        }

        Repeater {
            model: 3

            // index 0 is the back of the stack, 2 the front cover.
            delegate: Rectangle {
                id: bin

                required property int index

                readonly property int depth: 2 - bin.index
                readonly property string url: tile.covers && tile.covers.length > bin.depth
                                              ? String(tile.covers[bin.depth]) : ""

                width: Math.round(tile.size * 0.62)
                height: bin.width
                x: (stack.width - bin.width) / 2
                   + (bin.depth === 1 ? -tile.size * 0.16 : bin.depth === 2 ? tile.size * 0.16 : 0)
                y: stack.height - bin.height - (bin.depth === 0 ? 0 : tile.size * 0.05)
                z: bin.index
                rotation: bin.depth === 1 ? -9 : bin.depth === 2 ? 9 : 0
                antialiasing: true
                radius: Theme.crateSleeveRadius
                color: Theme.surfaceRaisedColor
                border.width: 1
                border.color: Theme.hairline

                StrmImage {
                    anchors.fill: parent
                    anchors.margins: 1
                    source: bin.url
                    suppressWarnings: true
                }
            }
        }

        FocusRing {
            active: tile.current
            radius: Theme.radiusCardValue
            inset: -Theme.scale(3)
        }
    }

    CrateHeading {
        id: label

        anchors.top: stack.bottom
        anchors.topMargin: Theme.spacingTight
        width: tile.size
        pixelSize: Theme.crateStripSize
        text: tile.name
    }

    Item {
        id: meta

        anchors.top: label.bottom
        anchors.topMargin: Theme.scale(2)
        width: tile.size
        height: Math.max(kicker.implicitHeight, arrow.size)

        CrateKicker {
            id: kicker
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            visible: !tile.isAllBin && tile.subtitle.length > 0
            text: tile.subtitle
        }

        StrmIcon {
            id: arrow
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            visible: tile.isAllBin
            name: "arrow-right"
            size: Theme.scale(18)
            color: Theme.accentColor
        }
    }
}
```

`src/ui/music/SectionStrip.qml`:

```qml
import QtQuick
import StrmQt

// HOME · ALBUMS · ARTISTS · SONGS · GENRES · PLAYLISTS (spec §5.1).
//
// One tab stop that owns Left/Right while it has somewhere to go. Left at the
// first section is declined, so the page's edge rule opens the navigation rail.
// Choosing a section only asks: `sectionChosen(key)`. The page that owns the
// route decides, and `currentKey` changes when a page says it is on screen.
FocusScope {
    id: strip

    property string currentKey: "home"
    property var keys: ["home", "albums", "artists", "songs", "genres", "playlists"]
    property int cursor: Math.max(0, strip.keys.indexOf(strip.currentKey))

    signal sectionChosen(string key)

    readonly property var labels: ({
        "home": qsTr("Home"),
        "albums": qsTr("Albums"),
        "artists": qsTr("Artists"),
        "songs": qsTr("Songs"),
        "genres": qsTr("Genres"),
        "playlists": qsTr("Playlists")
    })

    function labelFor(key: string): string {
        const label = strip.labels[key]
        return label === undefined ? key : String(label)
    }

    // The shoulders. Emits the next section and reports whether it did;
    // currentKey stays until the page for that section is on screen.
    function cycle(step): bool {
        const count = strip.keys.length
        if (count <= 1)
            return false
        const from = Math.max(0, strip.keys.indexOf(strip.currentKey))
        const next = ((from + Number(step)) % count + count) % count
        strip.sectionChosen(strip.keys[next])
        return true
    }

    function choose(index: int): void {
        if (index < 0 || index >= strip.keys.length)
            return
        strip.cursor = index
        if (strip.keys[index] !== strip.currentKey)
            strip.sectionChosen(strip.keys[index])
    }

    function resetCursor(): void {
        strip.cursor = Math.max(0, strip.keys.indexOf(strip.currentKey))
    }

    activeFocusOnTab: true
    implicitWidth: row.implicitWidth
    implicitHeight: row.implicitHeight
    width: implicitWidth
    height: implicitHeight

    onCurrentKeyChanged: strip.resetCursor()
    onActiveFocusChanged: {
        if (!strip.activeFocus)
            strip.resetCursor()
    }

    Accessible.role: Accessible.PageTabList
    Accessible.name: qsTr("Music sections")

    Keys.onLeftPressed: event => {
        if (strip.cursor > 0) {
            strip.cursor--
            event.accepted = true
        } else {
            event.accepted = false
        }
    }
    Keys.onRightPressed: event => {
        // No wrap, and no escape to the right either: the strip is the row.
        if (strip.cursor < strip.keys.length - 1)
            strip.cursor++
        event.accepted = true
    }
    Keys.onReturnPressed: event => { if (!event.isAutoRepeat) strip.choose(strip.cursor) }
    Keys.onEnterPressed: event => { if (!event.isAutoRepeat) strip.choose(strip.cursor) }
    Keys.onSpacePressed: event => { if (!event.isAutoRepeat) strip.choose(strip.cursor) }

    Row {
        id: row

        spacing: Theme.spacingTight

        Repeater {
            model: strip.keys

            delegate: Row {
                id: cell

                required property int index
                required property string modelData

                readonly property bool selected: cell.modelData === strip.currentKey
                readonly property bool cursorHere: strip.activeFocus && strip.cursor === cell.index

                spacing: Theme.spacingTight

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: cell.index > 0
                    text: "·"
                    color: Theme.textTertiary
                    font.family: Theme.fontDisplay
                    font.pixelSize: Theme.crateStripSize
                }

                Item {
                    id: tab

                    width: label.implicitWidth + Theme.spacingTight * 2
                    height: label.implicitHeight + Theme.spacingTight * 2

                    Accessible.role: Accessible.PageTab
                    Accessible.name: label.text
                    Accessible.checkable: true
                    Accessible.checked: cell.selected
                    Accessible.onPressAction: strip.choose(cell.index)

                    CrateHeading {
                        id: label
                        anchors.centerIn: parent
                        pixelSize: Theme.crateStripSize
                        text: strip.labelFor(cell.modelData)
                        color: cell.selected || tabHover.hovered ? Theme.textPrimaryColor
                                                                 : Theme.textSecondaryColor
                    }

                    // The inset underline marks the section on screen, not the cursor.
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.leftMargin: Theme.spacingTight
                        anchors.rightMargin: Theme.spacingTight
                        height: Theme.scale(2)
                        visible: cell.selected
                        color: Theme.accentColor
                    }

                    FocusRing {
                        active: cell.cursorHere
                        radius: Theme.radiusChip
                    }

                    HoverHandler {
                        id: tabHover
                        cursorShape: Qt.PointingHandCursor
                    }

                    TapHandler {
                        gesturePolicy: TapHandler.ReleaseWithinBounds
                        onTapped: strip.choose(cell.index)
                    }
                }
            }
        }
    }
}
```

`src/ui/music/ShelfError.qml`:

```qml
import QtQuick
import StrmQt

// A failed shelf collapses to this one line (spec §4). The other shelves stay
// live, so this never claims the page and never takes focus on its own.
FocusScope {
    id: shelfError

    property string message: ""

    signal retry()

    implicitWidth: row.implicitWidth
    implicitHeight: row.implicitHeight
    height: implicitHeight

    Row {
        id: row

        spacing: Theme.spacingValue

        StrmIcon {
            anchors.verticalCenter: parent.verticalCenter
            name: "info"
            size: Theme.scale(18)
            color: Theme.textSecondaryColor
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: shelfError.message
            color: Theme.textSecondaryColor
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontSmall
            textFormat: Text.PlainText
            Accessible.role: Accessible.AlertMessage
            Accessible.name: shelfError.message
        }

        StrmButton {
            anchors.verticalCenter: parent.verticalCenter
            focus: true
            text: qsTr("Retry")
            iconName: "refresh"
            variant: "ghost"
            onClicked: shelfError.retry()
        }
    }
}
```

`src/ui/music/Music.cmake`:

```cmake
# Crate: the music dialect of the control library (spec §2). Its own fragment,
# so each music phase appends its controls without touching src/CMakeLists.txt.
qt_target_qml_sources(strmqt QML_FILES
    ui/music/CrateHeading.qml
    ui/music/CrateKicker.qml
    ui/music/CrateBadge.qml
    ui/music/CoverCollage.qml
    ui/music/CrateSleeve.qml
    ui/music/CratePortrait.qml
    ui/music/StationTile.qml
    ui/music/GenreBinTile.qml
    ui/music/SectionStrip.qml
    ui/music/ShelfError.qml
)
```

In `src/CMakeLists.txt`, directly after the guarded `Controls.cmake` include, add:

```cmake
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/ui/music/Music.cmake)
    include(${CMAKE_CURRENT_SOURCE_DIR}/ui/music/Music.cmake)
endif()
```

- [x] **Step 4: Run the test to verify it passes**

Run: `cmake --preset dev && cmake --build --preset dev && ctest --preset dev -R tst_crate_controls --output-on-failure`
Expected: PASS, 10 tests.

If `hoverIsNotFocus` fails only because offscreen hover is not delivered, it already sends two moves. Do not remove the `current` or focus assertions.

If `headingAndKickerUseCrateType` fails on the weight: QFont has clamped 820 to a named weight on a Qt build without variable-weight support. Compare against `Theme.crateDisplayWeight` read back from the probe, rather than dropping the check.

Lint (these files add no context properties, so this must be clean):

```bash
bash scripts/check-qmllint-baseline.sh build/dev
```

Expected: no new warnings.

- [x] **Step 5: Commit**

```bash
git add src/ui/music/ src/CMakeLists.txt tests/unit/tst_crate_controls.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'MSG'
feat(music): add the Crate controls for Music Home

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
)"
```

---

### Task 6: `CrateShelf`

**Files:**
- Create: `src/ui/music/CrateShelf.qml`
- Modify: `src/ui/music/Music.cmake`
- Test: `tests/unit/tst_crate_controls.cpp` (extended)

**Interfaces:**
- Consumes: Task 4 (`cardComponent`, `customCardWidth`/`customCardHeight`, `showHeading`), Task 5 (`CrateHeading`, `CrateKicker`, `ShelfError`), and the `MusicLane` properties `model`, `loading`, `error`, `empty`, plus `retry()`.
- Produces `CrateShelf` with the contract's names: `title`, `kicker`, `lane`, `delegate`, `actionText`, `navigationFocusKey`; the signals `actionTriggered()`, `itemActivated(int index)`, `itemPlayRequested(int index)` and `menuRequested(int index, real x, real y)`. The additions are:
  - `property string actionIcon: ""`
  - `property int cardWidth`, `property int cardHeight`: the delegate's cell size
  - `property string skeletonShape: "square"` (`"square"` | `"round"`) and `property int skeletonCount: 6`
  - `readonly property bool showSkeleton`, `showError`, `focusable`
  - `readonly property Item rail`, the inner `StrmRail`, so the page can page it and read `currentIndex`

`lane` is declared `property var lane`, because `MusicLane` is a context-property object rather than a registered QML type. Typing it `QtObject` would make every `lane.loading` a `[missing-property]` lint warning.

The shelf is **one tab stop** (`activeFocusOnTab: focusable`). It shows exactly one of three bodies:
- the skeleton row, while `lane.loading` with no rows;
- the error line, while `lane.error` is set (focus goes to its Retry button);
- the rail.

Up from the rail moves to the heading's action button when there is one; Up from there, or from a shelf with no action, is declined so the page moves on. Down from the action button returns to the rail. The action button is out of the Tab chain, so the shelf stays one stop.

- [ ] **Step 1: Extend the failing test**

In `tests/unit/tst_crate_controls.cpp`:

1. Add these types to `kControls`: `StrmCard`, `StrmScrollBar`, `NavigationFocusRestorer`, `NavigationColumn`, `StrmRail`, `StrmSkeleton`. Add `CrateShelf` to `kMusic`. The Task 5 `stage` function already writes `NavigationColumn` as a singleton.

2. Add these properties to the probe root, after `property bool leftEscaped: false`:

```qml
    property int shelfActivated: -1
    property int shelfActions: 0
    property bool upEscaped: false

    Keys.onUpPressed: root.upEscaped = true
```

3. Add this block just before the probe's final closing brace:

```qml
    ListModel { id: shelfRows }

    QtObject {
        id: fakeLane
        objectName: "fakeLane"

        property var model: shelfRows
        property bool loading: false
        property string error: ""
        readonly property bool empty: !fakeLane.loading && fakeLane.error.length === 0 && shelfRows.count === 0
        property int retries: 0

        function retry() { fakeLane.retries++ }
    }

    CrateShelf {
        id: shelf
        objectName: "shelf"
        y: 660
        width: 1000
        title: "Pull one out"
        lane: fakeLane
        actionText: "Reshuffle"
        actionIcon: "refresh"
        navigationFocusKey: "probe-shelf"
        cardWidth: 100
        cardHeight: 140
        skeletonShape: "round"
        delegate: Component {
            CrateSleeve {
                property var model
                property int index: -1
                objectName: "shelfSleeve-" + (model ? model.itemId : "")
                size: 100
                title: model ? model.title : ""
            }
        }
        onItemActivated: index => root.shelfActivated = index
        onActionTriggered: root.shelfActions++
    }

    function setLane(loading, error) {
        fakeLane.loading = loading
        fakeLane.error = error
    }

    function fillShelf() {
        shelfRows.clear()
        shelfRows.append({ itemId: "a", title: "A" })
        shelfRows.append({ itemId: "b", title: "B" })
        shelfRows.append({ itemId: "c", title: "C" })
    }
```

The probe keeps its 1000×900 size: the shelf at `y: 660`, with 140-pixel cards under its heading, ends above 900.

4. Add the private slots, after `shelfErrorRetries()`:

```cpp
    void shelfHidesWhileTheLaneIsEmpty();
    void shelfSkeletonHasTheRealShape();
    void shelfErrorLineRetriesTheLane();
    void shelfDrawsTheDelegateAndForwardsActivation();
    void shelfUpReachesTheActionThenDeclines();
```

and their bodies, before `QTEST_MAIN`:

```cpp
void CrateControlsTest::shelfHidesWhileTheLaneIsEmpty()
{
    QQuickItem *shelf = item("shelf");
    QVERIFY(shelf);
    QVERIFY(!shelf->isVisible());
    QVERIFY(!shelf->property("focusable").toBool());
}

void CrateControlsTest::shelfSkeletonHasTheRealShape()
{
    QQuickItem *shelf = item("shelf");
    QVERIFY(QMetaObject::invokeMethod(m_root, "setLane", Q_ARG(QVariant, true), Q_ARG(QVariant, QString())));
    QTRY_VERIFY(shelf->isVisible());
    QVERIFY(shelf->property("showSkeleton").toBool());
    QVERIFY(!shelf->property("focusable").toBool());
    QQuickItem *first = findItem(shelf, QStringLiteral("crateShelfSkeleton-0"));
    QVERIFY(first);
    QVERIFY(first->isVisible());
    QCOMPARE(qRound(first->width()), 100);
    QCOMPARE(qRound(first->property("radius").toReal()), 50); // round: a portrait's circle
    QQuickItem *rail = shelf->property("rail").value<QQuickItem *>();
    QVERIFY(rail);
    QVERIFY(!rail->isVisible());
}

void CrateControlsTest::shelfErrorLineRetriesTheLane()
{
    QQuickItem *shelf = item("shelf");
    QVERIFY(QMetaObject::invokeMethod(m_root, "setLane", Q_ARG(QVariant, false),
                                      Q_ARG(QVariant, QStringLiteral("Couldn't load"))));
    QTRY_VERIFY(shelf->property("showError").toBool());
    QVERIFY(!shelf->property("showSkeleton").toBool());
    QVERIFY(shelf->property("focusable").toBool());
    shelf->forceActiveFocus();
    QTest::keyClick(&m_view, Qt::Key_Return);
    QObject *lane = m_root->findChild<QObject *>(QStringLiteral("fakeLane"));
    QVERIFY(lane);
    QTRY_COMPARE(lane->property("retries").toInt(), 1);
    QVERIFY(QMetaObject::invokeMethod(m_root, "setLane", Q_ARG(QVariant, false), Q_ARG(QVariant, QString())));
}

void CrateControlsTest::shelfDrawsTheDelegateAndForwardsActivation()
{
    QQuickItem *shelf = item("shelf");
    QVERIFY(QMetaObject::invokeMethod(m_root, "fillShelf"));
    QTRY_VERIFY(findItem(shelf, QStringLiteral("shelfSleeve-b")));
    QVERIFY(!shelf->property("showSkeleton").toBool());
    QVERIFY(!shelf->property("showError").toBool());
    QQuickItem *rail = shelf->property("rail").value<QQuickItem *>();
    QVERIFY(rail->isVisible());

    QQuickItem *b = findItem(shelf, QStringLiteral("shelfSleeve-b"));
    QTest::mouseClick(&m_view, Qt::LeftButton, {}, b->mapToScene(QPointF(50, 50)).toPoint());
    QTRY_COMPARE(m_root->property("shelfActivated").toInt(), 1);
}

void CrateControlsTest::shelfUpReachesTheActionThenDeclines()
{
    QQuickItem *shelf = item("shelf");
    QVERIFY(QMetaObject::invokeMethod(m_root, "fillShelf"));
    m_root->setProperty("upEscaped", false);
    shelf->forceActiveFocus();
    QQuickItem *rail = shelf->property("rail").value<QQuickItem *>();
    QTRY_VERIFY(rail->hasActiveFocus());

    QTest::keyClick(&m_view, Qt::Key_Up);
    QQuickItem *action = findItem(shelf, QStringLiteral("crateShelfAction"));
    QVERIFY(action);
    QTRY_VERIFY(action->hasActiveFocus());
    QVERIFY(!m_root->property("upEscaped").toBool());

    QTest::keyClick(&m_view, Qt::Key_Return);
    QTRY_COMPARE(m_root->property("shelfActions").toInt(), 1);

    QTest::keyClick(&m_view, Qt::Key_Up);
    QVERIFY(m_root->property("upEscaped").toBool());

    QTest::keyClick(&m_view, Qt::Key_Down);
    QTRY_VERIFY(rail->hasActiveFocus());
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build --preset dev --target tst_crate_controls && ctest --preset dev -R tst_crate_controls --output-on-failure`
Expected: FAIL in `initTestCase`, because `src/ui/music/CrateShelf.qml` does not exist.

- [ ] **Step 3: Write `CrateShelf`**

`src/ui/music/CrateShelf.qml`:

```qml
import QtQuick
import StrmQt

// One Home shelf (spec §4): a Crate heading over a rail of Crate controls.
//
// Each shelf is an independent lane. It hides while its lane is empty, heading
// included; it shows a skeleton in its real shape while the first load runs;
// a failure collapses it to one line with Retry. The other shelves never notice.
//
// Focus: one tab stop. Up from the rail reaches the heading's action (↻) when
// there is one; everything else vertical is declined, so the page moves on.
FocusScope {
    id: shelf

    property string title: ""
    property string kicker: ""
    // A MusicLane. `var`, because MusicLane is a context-property object, not a
    // registered type, and a QtObject type would make every read a lint warning.
    property var lane: null
    property Component delegate: null
    property string actionText: ""
    property string actionIcon: ""
    property string navigationFocusKey: ""
    property int cardWidth: Theme.crateSleeveSize
    property int cardHeight: Theme.crateSleeveSize
    property string skeletonShape: "square"
    property int skeletonCount: 6

    signal actionTriggered()
    signal itemActivated(int index)
    signal itemPlayRequested(int index)
    signal menuRequested(int index, real x, real y)

    readonly property Item rail: railView
    readonly property bool loading: shelf.lane !== null && shelf.lane.loading === true
    readonly property string errorText: shelf.lane !== null && shelf.lane.error ? String(shelf.lane.error) : ""
    readonly property bool showError: shelf.errorText.length > 0
    readonly property bool showSkeleton: !shelf.showError && shelf.loading && railView.count === 0
    readonly property bool focusable: shelf.visible && !shelf.showSkeleton

    visible: shelf.lane !== null && shelf.lane.empty !== true
    activeFocusOnTab: shelf.focusable
    width: parent ? parent.width : implicitWidth
    implicitWidth: Theme.scale(800)
    height: header.height + Theme.spacingTight + body.height

    Accessible.role: Accessible.Grouping
    Accessible.name: shelf.title

    function focusRail(): void {
        if (shelf.showError)
            errorLine.forceActiveFocus(Qt.OtherFocusReason)
        else
            railView.forceActiveFocus(Qt.OtherFocusReason)
    }

    Keys.onUpPressed: event => {
        if (actionButton.visible && !actionButton.activeFocus && !shelf.showError) {
            actionButton.forceActiveFocus(Qt.TabFocusReason)
            event.accepted = true
            return
        }
        event.accepted = false
    }
    Keys.onDownPressed: event => {
        if (actionButton.activeFocus) {
            shelf.focusRail()
            event.accepted = true
            return
        }
        event.accepted = false
    }

    // ── Heading ────────────────────────────────────────────────────────────
    Item {
        id: header

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.pageMarginValue
        anchors.rightMargin: Theme.pageMarginValue
        height: Math.max(heading.implicitHeight, actionButton.visible ? actionButton.implicitHeight : 0)

        CrateHeading {
            id: heading
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(heading.implicitWidth, header.width * 0.6)
            text: shelf.title
        }

        CrateKicker {
            anchors.left: heading.right
            anchors.leftMargin: Theme.spacingValue
            anchors.baseline: heading.baseline
            anchors.right: actionButton.visible ? actionButton.left : parent.right
            anchors.rightMargin: Theme.spacingValue
            visible: shelf.kicker.length > 0
            text: shelf.kicker
        }

        StrmButton {
            id: actionButton
            objectName: "crateShelfAction"
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: shelf.actionText.length > 0 && !shelf.showError && !shelf.showSkeleton
            // Reached with Up from the rail, never with Tab: the shelf is one stop.
            activeFocusOnTab: false
            text: shelf.actionText
            iconName: shelf.actionIcon
            variant: "ghost"
            onClicked: shelf.actionTriggered()
        }
    }

    // ── Body: exactly one of skeleton, error line, rail ────────────────────
    Item {
        id: body

        anchors.top: header.bottom
        anchors.topMargin: Theme.spacingTight
        anchors.left: parent.left
        anchors.right: parent.right
        height: shelf.showError ? errorLine.height
              : shelf.showSkeleton ? skeletonRow.height
              : railView.height

        Row {
            id: skeletonRow

            visible: shelf.showSkeleton
            x: Theme.pageMarginValue
            spacing: Theme.spacingValue
            height: shelf.cardHeight

            Repeater {
                model: shelf.showSkeleton ? shelf.skeletonCount : 0

                delegate: Column {
                    id: ghost

                    required property int index

                    spacing: Theme.spacingTight

                    StrmSkeleton {
                        objectName: "crateShelfSkeleton-" + ghost.index
                        width: shelf.cardWidth
                        height: shelf.cardWidth
                        radius: shelf.skeletonShape === "round" ? shelf.cardWidth / 2 : Theme.crateSleeveRadius
                    }

                    // The caption bar: centred under a portrait, flush under a sleeve.
                    // A Column child takes x, not anchors.
                    StrmSkeleton {
                        x: shelf.skeletonShape === "round" ? shelf.cardWidth * 0.15 : 0
                        width: shelf.cardWidth * 0.7
                        height: Theme.fontSmall
                        radius: Theme.radiusChip
                    }
                }
            }
        }

        ShelfError {
            id: errorLine

            x: Theme.pageMarginValue
            visible: shelf.showError
            focus: shelf.showError
            message: shelf.errorText
            onRetry: {
                if (shelf.lane !== null)
                    shelf.lane.retry()
            }
        }

        StrmRail {
            id: railView

            width: body.width
            visible: !shelf.showError && !shelf.showSkeleton
            focus: !shelf.showError
            showHeading: false
            title: shelf.title
            railModel: shelf.lane !== null ? shelf.lane.model : null
            cardComponent: shelf.delegate
            customCardWidth: shelf.cardWidth
            customCardHeight: shelf.cardHeight
            navigationFocusKey: shelf.navigationFocusKey

            onItemActivated: index => shelf.itemActivated(index)
            onItemPlayRequested: index => shelf.itemPlayRequested(index)
            onMenuRequested: (index, mx, my) => shelf.menuRequested(index, mx, my)
        }
    }
}
```

Append `ui/music/CrateShelf.qml` to the `qt_target_qml_sources` list in `src/ui/music/Music.cmake`.

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --preset dev && cmake --build --preset dev && ctest --preset dev -R "tst_crate_controls|tst_card_component" --output-on-failure`
Expected: PASS. `tst_crate_controls` now runs 15 tests.

The shelf slots run in declaration order and share one probe. `shelfHidesWhileTheLaneIsEmpty` must stay first among them, because the others fill the lane.

Lint:

```bash
bash scripts/check-qmllint-baseline.sh build/dev
```

Expected: no new warnings. `lane` is `var`, so its members are dynamic lookups.

- [ ] **Step 5: Commit**

```bash
git add src/ui/music/CrateShelf.qml src/ui/music/Music.cmake tests/unit/tst_crate_controls.cpp
git commit -m "$(cat <<'MSG'
feat(music): add CrateShelf, a lane-driven Home shelf

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
)"
```

---

### Task 7: `MusicHomePage.qml`

**Files:**
- Create: `src/ui/pages/MusicHomePage.qml`
- Modify: `src/CMakeLists.txt` (the page list of `qt_add_qml_module`)

**Interfaces:**
- Consumes:
  - Task 3: `MusicHomeCtl` (`hero`, the eight lanes, `addedThisWeekText`, `allGenresText`, `resumeHero`, `shuffleHero`, `anotherOne`, `reshuffle`, `playStation`, `shuffleStation`, `queueStation`, `refreshStale`)
  - Tasks 5–6: the Crate controls and `CrateShelf`
  - Existing: `Actions` (`openDetails`, `setFavorite`, `toggleFavorite`), `MusicPlay.playAlbum`, `PlayerCtl`, `App.interactionContext`, `ItemMenu`, `StrmMenu`, `CoverWash`, `MappedShortcut`, `StrmSkeleton`
- Produces `MusicHomePage` (Main sets `objectName: "musicHomePage"` in Task 8):
  - `property string libraryId`, `property string libraryName`: the route's reconstructed properties
  - `signal sectionRequested(string key)`: a section strip choice other than Home, or the all-genres bin (`"genres"`)
  - `signal genreRequested(string genreId, string genreName)`: a genre bin
  - `function cycleTab(step): bool`: the shoulders, through `SectionStrip.cycle`
  - `function pageBy(step): bool`: the triggers, three sections at a time

**No failing test for this task.** It is QML only. Its checks are the lint baseline and the self-test page construction, which Task 8 adds to the page list, plus the visual check in Task 9. `tst_crate_controls` already covers the controls the page composes.

Page layout, top to bottom, inside one vertical `Flickable`:
1. `SectionStrip` with `currentKey: "home"`.
2. The hero (spec §4.1): the cover wash behind a 210 px sleeve, the kicker, the title in `crateHeroHome`, the summary in Plex Mono, the progress line (resume only), the actions, and "Recently played" beside it (up to three rows from `heroLane.model`).
   - Resume: **Resume track N**, **Shuffle album**, **♡**.
   - Pull one out: **Play**, **Another one ↻**.
   - A skeleton in the hero's shape while `heroLane.loading` with no hero; the one-line error on `heroLane.error`.
3. Seven `CrateShelf`s in spec order: Recently played, New in the crate (kicker `addedThisWeekText`), Stations, Dig by genre, Artists you play (round), Forgotten favourites, Pull one out (↻ reshuffles).

Focus model:
- The strip, the hero and each visible shelf are the page's **sections**, one tab stop each.
- Up/Down that a section declines move to the previous or next focusable section, then scroll it into view. Up from the strip is declined, so Main's shell rule moves to the top bar.
- Left/Right inside the hero walk its buttons and then the Recently played rows. Left at the first button is declined, so the navigation rail opens.
- A shelf's rail keeps its own `NavigationColumn` behaviour (Task 4), so Down from one shelf lands in the column it left.
- Hover never moves focus: every Crate control only scales on hover.

- [ ] **Step 1: Write the page**

`src/ui/pages/MusicHomePage.qml`:

```qml
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import StrmQt

// MusicHomePage — a music library's landing page (Crate spec §4).
//
// A hero ("Pick up where you left off", or "Pull one out" with no history) and
// seven shelves. Every shelf is an independent lane from MusicHomeCtl: it loads,
// fails and hides on its own, and this page only lays them out. Nothing here
// shapes data; the controller hands over rows and ready-made strings.
//
// Navigation: this page pushes nothing. Choosing a section or a genre raises a
// signal and Main.qml owns the route (ARCHITECTURE.md: Main alone navigates).
FocusScope {
    id: page

    property string libraryId: ""
    property string libraryName: ""

    signal sectionRequested(string key)
    signal genreRequested(string genreId, string genreName)

    readonly property var hero: MusicHomeCtl.hero
    readonly property string heroMode: page.hero && page.hero.mode ? String(page.hero.mode) : ""
    readonly property bool hasHero: page.heroMode.length > 0
    readonly property bool resumeMode: page.heroMode === "resume"
    readonly property bool heroLoading: MusicHomeCtl.heroLane.loading === true && !page.hasHero
    readonly property string heroError: !page.hasHero && MusicHomeCtl.heroLane.error
                                        ? String(MusicHomeCtl.heroLane.error) : ""

    readonly property int heroSleeveSize: Theme.scale(210)
    readonly property int sleeveSize: Theme.crateSleeveSize
    // A caption is a title line and a subtitle line under the art.
    readonly property int captionHeight: Theme.scale(48)

    readonly property bool musicActive: App.interactionContext === "music"
                                        && page.StackView.status === StackView.Active

    readonly property var sections: [strip, heroScope, recentShelf, newShelf, stationShelf,
                                     genreShelf, artistShelf, forgottenShelf, pullShelf]

    Accessible.role: Accessible.Pane
    Accessible.name: page.libraryName

    // Stale shelves refetch when Home comes back on screen. Within the TTL this
    // sends nothing (MusicRepository's cache answers).
    StackView.onActivated: MusicHomeCtl.refreshStale()

    // ── Sections ───────────────────────────────────────────────────────────
    function sectionFocusable(section): bool {
        if (!section || !section.visible)
            return false
        if (section === heroScope)
            return page.hasHero || page.heroError.length > 0
        if (section.focusable !== undefined)
            return section.focusable === true
        return true
    }

    function currentSection(): int {
        for (let i = 0; i < page.sections.length; ++i) {
            if (page.sections[i].activeFocus)
                return i
        }
        return -1
    }

    function focusSection(section): void {
        section.forceActiveFocus(Qt.TabFocusReason)
        page.ensureVisible(section)
    }

    function moveSection(step): bool {
        const from = page.currentSection()
        if (from < 0)
            return false
        for (let i = from + step; i >= 0 && i < page.sections.length; i += step) {
            if (page.sectionFocusable(page.sections[i])) {
                page.focusSection(page.sections[i])
                return true
            }
        }
        return false
    }

    // The triggers (Main.pageFocusedView): a screenful is about three shelves.
    function pageBy(step): bool {
        let moved = false
        for (let i = 0; i < 3; ++i) {
            if (!page.moveSection(step > 0 ? 1 : -1))
                break
            moved = true
        }
        return moved
    }

    // The shoulders (Main.cycleSection). The strip emits the next section and
    // the handler below turns it into a route request.
    function cycleTab(step): bool {
        return strip.cycle(step)
    }

    Keys.onUpPressed: event => { event.accepted = page.moveSection(-1) }
    // Down at the last shelf stays put rather than leaving the page.
    Keys.onDownPressed: event => {
        page.moveSection(1)
        event.accepted = true
    }

    // ── Scrolling ──────────────────────────────────────────────────────────
    function scrollTo(y): void {
        const maxY = Math.max(0, scroll.contentHeight - scroll.height)
        scrollAnim.stop()
        scrollAnim.from = scroll.contentY
        scrollAnim.to = Math.max(0, Math.min(maxY, y))
        scrollAnim.start()
    }

    function ensureVisible(section): void {
        if (!section || !section.visible)
            return
        if (section === strip || section === heroScope) {
            page.scrollTo(0)
            return
        }
        const top = section.mapToItem(content, 0, 0).y
        const bottom = top + section.height
        if (top - Theme.spacingValue < scroll.contentY)
            page.scrollTo(top - Theme.spacingValue)
        else if (bottom + Theme.spacingValue > scroll.contentY + scroll.height)
            page.scrollTo(bottom + Theme.spacingValue - scroll.height)
    }

    NumberAnimation {
        id: scrollAnim
        target: scroll
        property: "contentY"
        duration: Theme.animNormalMs
        easing.type: Theme.easeStandard
    }

    // ── Rows ───────────────────────────────────────────────────────────────
    function itemAt(lane, index): var {
        return lane && lane.model && index >= 0 ? lane.model.get(index) : null
    }

    function idOf(item): string {
        return item && item.itemId !== undefined ? String(item.itemId) : ""
    }

    function openItem(lane, index): void {
        const item = page.itemAt(lane, index)
        if (item)
            Actions.openDetails(item)
    }

    function playAlbum(lane, index): void {
        const item = page.itemAt(lane, index)
        const id = page.idOf(item)
        if (id.length > 0)
            MusicPlay.playAlbum(id, item.title !== undefined ? String(item.title) : "", 0)
    }

    function openGenre(index): void {
        const bin = page.itemAt(MusicHomeCtl.genreLane, index)
        if (!bin)
            return
        const id = page.idOf(bin)
        if (id.length === 0)
            page.sectionRequested("genres")
        else
            page.genreRequested(id, bin.name !== undefined ? String(bin.name) : "")
    }

    function stationRow(key: string): int {
        const model = MusicHomeCtl.stationLane.model
        for (let i = 0; model && i < model.count; ++i) {
            if (page.idOf(model.get(i)) === key)
                return i
        }
        return -1
    }

    // L: the thing under the keyboard. The hero's album, else the focused card.
    function toggleFocusedFavourite(): void {
        if (heroScope.activeFocus && page.hasHero) {
            Actions.setFavorite(String(page.hero.albumId), page.hero.favourite !== true)
            return
        }
        const shelves = [recentShelf, newShelf, artistShelf, forgottenShelf, pullShelf]
        for (let i = 0; i < shelves.length; ++i) {
            const shelf = shelves[i]
            if (shelf.activeFocus && shelf.rail) {
                const item = page.itemAt(shelf.lane, shelf.rail.currentIndex)
                if (item)
                    Actions.toggleFavorite(item)
                return
            }
        }
    }

    // ── Hero keyboard walk ─────────────────────────────────────────────────
    function heroStops(): var {
        const stops = [heroPrimary, heroShuffle, heroFavourite, heroAnother]
        for (let i = 0; i < recentRows.count; ++i)
            stops.push(recentRows.itemAt(i))
        return stops.filter(stop => stop !== null && stop.visible)
    }

    function moveHero(step): bool {
        const stops = page.heroStops()
        let at = -1
        for (let i = 0; i < stops.length; ++i) {
            if (stops[i].activeFocus)
                at = i
        }
        const next = at + step
        if (at < 0 || next < 0 || next >= stops.length)
            return false
        stops[next].forceActiveFocus(Qt.TabFocusReason)
        return true
    }

    component RecentRow: Item {
        id: row

        required property int index
        required property string title
        required property string subtitle
        required property string coverUrl

        width: parent ? parent.width : Theme.scale(320)
        height: Theme.scale(56)
        activeFocusOnTab: false

        Accessible.role: Accessible.Button
        Accessible.name: row.subtitle.length > 0 ? row.title + ", " + row.subtitle : row.title
        Accessible.onPressAction: page.openItem(MusicHomeCtl.heroLane, row.index)

        Keys.onReturnPressed: event => { if (!event.isAutoRepeat) page.openItem(MusicHomeCtl.heroLane, row.index) }
        Keys.onEnterPressed: event => { if (!event.isAutoRepeat) page.openItem(MusicHomeCtl.heroLane, row.index) }
        Keys.onMenuPressed: {
            const p = row.mapToItem(null, row.width / 2, row.height)
            albumMenu.popupForItem(page.itemAt(MusicHomeCtl.heroLane, row.index), p.x, p.y)
        }

        HoverHandler { id: rowHover; cursorShape: Qt.PointingHandCursor }

        TapHandler {
            acceptedButtons: Qt.LeftButton
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: page.openItem(MusicHomeCtl.heroLane, row.index)
        }

        TapHandler {
            acceptedButtons: Qt.RightButton
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: eventPoint => {
                const p = row.mapToItem(null, eventPoint.position.x, eventPoint.position.y)
                albumMenu.popupForItem(page.itemAt(MusicHomeCtl.heroLane, row.index), p.x, p.y)
            }
        }

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusChip
            color: Theme.surfaceRaisedColor
            opacity: rowHover.hovered ? 1 : 0
        }

        Rectangle {
            id: thumb
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingTight
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.scale(44)
            height: width
            radius: Theme.crateSleeveRadius
            color: Theme.surfaceRaisedColor
            clip: true

            StrmImage {
                anchors.fill: parent
                source: row.coverUrl
                suppressWarnings: true
            }
        }

        Column {
            anchors.left: thumb.right
            anchors.leftMargin: Theme.spacingValue
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingTight
            anchors.verticalCenter: parent.verticalCenter

            Text {
                width: parent.width
                text: row.title
                color: Theme.textPrimaryColor
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontSmall
                font.weight: Font.DemiBold
                textFormat: Text.PlainText
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                text: row.subtitle
                color: Theme.textSecondaryColor
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontCaption
                textFormat: Text.PlainText
                elide: Text.ElideRight
            }
        }

        FocusRing {
            active: row.activeFocus
            radius: Theme.radiusChip
        }
    }

    // ── Shelf delegates (Task 4 protocol: plain model/index/current/hovered) ─
    Component {
        id: sleeveCard

        CrateSleeve {
            id: sleeveItem

            property var model: null
            property int index: -1

            size: page.sleeveSize
            coverUrl: sleeveItem.model && sleeveItem.model.coverUrl ? String(sleeveItem.model.coverUrl) : ""
            title: sleeveItem.model && sleeveItem.model.title ? String(sleeveItem.model.title) : ""
            subtitle: sleeveItem.model && sleeveItem.model.subtitle ? String(sleeveItem.model.subtitle) : ""
            badge: sleeveItem.model && sleeveItem.model.releaseBadge ? String(sleeveItem.model.releaseBadge) : ""
        }
    }

    Component {
        id: portraitCard

        CratePortrait {
            id: portraitItem

            property var model: null
            property int index: -1

            size: Theme.cratePortraitSize
            imageUrl: portraitItem.model && portraitItem.model.coverUrl ? String(portraitItem.model.coverUrl) : ""
            name: portraitItem.model && portraitItem.model.name ? String(portraitItem.model.name) : ""
            subtitle: portraitItem.model && portraitItem.model.subtitle ? String(portraitItem.model.subtitle) : ""
        }
    }

    Component {
        id: stationCard

        StationTile {
            id: stationItem

            property var model: null
            property int index: -1

            size: page.sleeveSize
            covers: stationItem.model && stationItem.model.covers ? stationItem.model.covers : []
            label: stationItem.model && stationItem.model.label ? String(stationItem.model.label) : ""
        }
    }

    Component {
        id: genreCard

        GenreBinTile {
            id: genreItem

            property var model: null
            property int index: -1

            size: page.sleeveSize
            name: genreItem.model && genreItem.model.name ? String(genreItem.model.name) : ""
            subtitle: genreItem.model && genreItem.model.subtitle ? String(genreItem.model.subtitle) : ""
            covers: genreItem.model && genreItem.model.covers ? genreItem.model.covers : []
            isAllBin: genreItem.model !== null && String(genreItem.model.itemId || "").length === 0
        }
    }

    // ── Body ───────────────────────────────────────────────────────────────
    Flickable {
        id: scroll

        anchors.fill: parent
        contentWidth: width
        contentHeight: content.height
        boundsBehavior: Flickable.StopAtBounds
        interactive: scroll.contentHeight > scroll.height
        clip: true

        ScrollBar.vertical: StrmScrollBar {}

        Item {
            id: content

            width: scroll.width
            height: column.implicitHeight + Theme.pageMarginValue

            // The sleeve lights the room: the wash sits behind the strip and the
            // hero, and keeps CoverTint's clamp (CoverWash.qml decides nothing).
            CoverWash {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: heroScope.y + heroScope.height + Theme.railGap
                visible: page.hasHero
                source: page.hasHero ? String(page.hero.coverUrl) : ""
            }

            Column {
                id: column

                width: content.width
                spacing: Theme.railGap

                Item { width: 1; height: Math.max(0, Theme.spacingValue) }

                SectionStrip {
                    id: strip

                    x: Theme.pageMarginValue
                    currentKey: "home"
                    onSectionChosen: key => {
                        if (key !== "home")
                            page.sectionRequested(key)
                    }
                    onActiveFocusChanged: { if (strip.activeFocus) page.ensureVisible(strip) }
                }

                // ── Hero ───────────────────────────────────────────────────
                FocusScope {
                    id: heroScope

                    width: column.width
                    height: visible ? heroBody.height : 0
                    visible: page.hasHero || page.heroLoading || page.heroError.length > 0
                    focus: true

                    Keys.onLeftPressed: event => { event.accepted = page.moveHero(-1) }
                    Keys.onRightPressed: event => {
                        page.moveHero(1)
                        event.accepted = true
                    }
                    onActiveFocusChanged: { if (heroScope.activeFocus) page.ensureVisible(heroScope) }

                    Item {
                        id: heroBody

                        x: Theme.pageMarginValue
                        width: parent.width - Theme.pageMarginValue * 2
                        height: page.heroError.length > 0 ? heroErrorLine.height
                              : Math.max(page.heroSleeveSize, heroText.implicitHeight)

                        // Skeleton, in the hero's shape: the sleeve and three lines.
                        Row {
                            visible: page.heroLoading
                            spacing: Theme.spacingLoose

                            StrmSkeleton {
                                width: page.heroSleeveSize
                                height: page.heroSleeveSize
                                radius: Theme.crateSleeveRadius
                            }

                            Column {
                                spacing: Theme.spacingValue
                                anchors.verticalCenter: parent.verticalCenter

                                StrmSkeleton { width: Theme.scale(180); height: Theme.crateKickerSize; radius: Theme.radiusChip }
                                StrmSkeleton { width: Theme.scale(420); height: Theme.crateHeroHome; radius: Theme.radiusChip }
                                StrmSkeleton { width: Theme.scale(300); height: Theme.fontSmall; radius: Theme.radiusChip }
                            }
                        }

                        ShelfError {
                            id: heroErrorLine

                            visible: page.heroError.length > 0
                            focus: page.heroError.length > 0
                            message: page.heroError
                            onRetry: MusicHomeCtl.heroLane.retry()
                        }

                        CrateSleeve {
                            id: heroSleeve

                            visible: page.hasHero
                            size: page.heroSleeveSize
                            showCaption: false
                            coverUrl: page.hasHero ? String(page.hero.coverUrl) : ""
                            title: page.hasHero ? String(page.hero.title) : ""
                            onActivated: Actions.openDetails(page.hero.albumItem)
                            onPlayRequested: MusicHomeCtl.resumeHero()
                            onMenuRequested: (mx, my) => albumMenu.popupForItem(page.hero.albumItem, mx, my)
                        }

                        Column {
                            id: heroText

                            visible: page.hasHero
                            anchors.left: heroSleeve.right
                            anchors.leftMargin: Theme.spacingLoose
                            anchors.right: recentColumn.visible ? recentColumn.left : parent.right
                            anchors.rightMargin: recentColumn.visible ? Theme.spacingLoose : 0
                            anchors.verticalCenter: heroSleeve.verticalCenter
                            spacing: Theme.spacingValue

                            CrateKicker {
                                width: parent.width
                                text: page.resumeMode ? qsTr("Pick up where you left off") : qsTr("Pull one out")
                            }

                            CrateHeading {
                                width: parent.width
                                pixelSize: Theme.crateHeroHome
                                maximumLineCount: 2
                                wrapMode: Text.WordWrap
                                text: page.hasHero ? String(page.hero.title) : ""
                            }

                            Text {
                                width: parent.width
                                text: page.hasHero ? String(page.hero.summary) : ""
                                color: Theme.textSecondaryColor
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.fontSmall
                                font.features: ({ "tnum": 1 })
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                            }

                            // The progress line through the album (resume only).
                            Rectangle {
                                id: progressTrack

                                visible: page.resumeMode
                                width: Math.min(heroText.width, Theme.scale(420))
                                height: Theme.scale(3)
                                radius: progressTrack.height / 2
                                color: Theme.hairline
                                Accessible.role: Accessible.ProgressBar
                                Accessible.name: qsTr("Album progress")

                                Rectangle {
                                    width: progressTrack.width
                                           * Math.max(0, Math.min(1, Number(page.hero.progress || 0)))
                                    height: progressTrack.height
                                    radius: progressTrack.radius
                                    color: Theme.accentColor
                                }
                            }

                            Row {
                                spacing: Theme.spacingValue

                                StrmButton {
                                    id: heroPrimary
                                    focus: true
                                    variant: "primary"
                                    iconName: "play"
                                    text: page.hasHero ? String(page.hero.resumeLabel) : ""
                                    onClicked: MusicHomeCtl.resumeHero()
                                }

                                StrmButton {
                                    id: heroShuffle
                                    visible: page.resumeMode
                                    iconName: "shuffle"
                                    text: qsTr("Shuffle album")
                                    onClicked: MusicHomeCtl.shuffleHero()
                                }

                                StrmIconButton {
                                    id: heroFavourite
                                    visible: page.resumeMode
                                    anchors.verticalCenter: parent.verticalCenter
                                    iconName: page.hero.favourite === true ? "heart-filled" : "heart"
                                    checked: page.hero.favourite === true
                                    tooltip: page.hero.favourite === true ? qsTr("Remove from favourites")
                                                                          : qsTr("Add to favourites")
                                    onClicked: Actions.setFavorite(String(page.hero.albumId),
                                                                   page.hero.favourite !== true)
                                }

                                StrmButton {
                                    id: heroAnother
                                    visible: page.hasHero && !page.resumeMode
                                    iconName: "refresh"
                                    text: qsTr("Another one")
                                    onClicked: MusicHomeCtl.anotherOne()
                                }
                            }
                        }

                        // "Recently played": the three albums played before the hero.
                        Column {
                            id: recentColumn

                            visible: page.hasHero && recentRows.count > 0 && heroBody.width > Theme.scale(900)
                            anchors.right: parent.right
                            anchors.verticalCenter: heroSleeve.verticalCenter
                            width: Theme.scale(320)
                            spacing: Theme.spacingTight

                            CrateKicker {
                                text: qsTr("Recently played")
                            }

                            Repeater {
                                id: recentRows

                                model: MusicHomeCtl.heroLane.model
                                delegate: RecentRow {}
                            }
                        }
                    }
                }

                // ── Shelves, in spec order ─────────────────────────────────
                CrateShelf {
                    id: recentShelf

                    title: qsTr("Recently played")
                    lane: MusicHomeCtl.recentLane
                    delegate: sleeveCard
                    cardWidth: page.sleeveSize
                    cardHeight: page.sleeveSize + Theme.spacingTight + page.captionHeight
                    navigationFocusKey: "musicHome-recent"
                    onItemActivated: index => page.openItem(recentShelf.lane, index)
                    onItemPlayRequested: index => page.playAlbum(recentShelf.lane, index)
                    onMenuRequested: (index, mx, my) =>
                        albumMenu.popupForItem(page.itemAt(recentShelf.lane, index), mx, my)
                    onActiveFocusChanged: { if (recentShelf.activeFocus) page.ensureVisible(recentShelf) }
                }

                CrateShelf {
                    id: newShelf

                    title: qsTr("New in the crate")
                    kicker: MusicHomeCtl.addedThisWeekText
                    lane: MusicHomeCtl.newLane
                    delegate: sleeveCard
                    cardWidth: page.sleeveSize
                    cardHeight: page.sleeveSize + Theme.spacingTight + page.captionHeight
                    navigationFocusKey: "musicHome-new"
                    onItemActivated: index => page.openItem(newShelf.lane, index)
                    onItemPlayRequested: index => page.playAlbum(newShelf.lane, index)
                    onMenuRequested: (index, mx, my) =>
                        albumMenu.popupForItem(page.itemAt(newShelf.lane, index), mx, my)
                    onActiveFocusChanged: { if (newShelf.activeFocus) page.ensureVisible(newShelf) }
                }

                CrateShelf {
                    id: stationShelf

                    title: qsTr("Stations")
                    lane: MusicHomeCtl.stationLane
                    delegate: stationCard
                    cardWidth: page.sleeveSize
                    cardHeight: page.sleeveSize + Theme.spacingTight + Theme.scale(24)
                    navigationFocusKey: "musicHome-stations"
                    // One press resolves and plays.
                    onItemActivated: index => MusicHomeCtl.playStation(index)
                    onMenuRequested: (index, mx, my) => {
                        stationMenu.row = index
                        stationMenu.popupAt(mx, my)
                    }
                    onActiveFocusChanged: { if (stationShelf.activeFocus) page.ensureVisible(stationShelf) }
                }

                CrateShelf {
                    id: genreShelf

                    title: qsTr("Dig by genre")
                    lane: MusicHomeCtl.genreLane
                    delegate: genreCard
                    cardWidth: page.sleeveSize
                    cardHeight: Math.round(page.sleeveSize * 0.8) + Theme.spacingTight + page.captionHeight
                    navigationFocusKey: "musicHome-genres"
                    onItemActivated: index => page.openGenre(index)
                    onActiveFocusChanged: { if (genreShelf.activeFocus) page.ensureVisible(genreShelf) }
                }

                CrateShelf {
                    id: artistShelf

                    title: qsTr("Artists you play")
                    lane: MusicHomeCtl.artistLane
                    delegate: portraitCard
                    skeletonShape: "round"
                    cardWidth: Theme.cratePortraitSize
                    cardHeight: Theme.cratePortraitSize + Theme.spacingTight + page.captionHeight
                    navigationFocusKey: "musicHome-artists"
                    onItemActivated: index => page.openItem(artistShelf.lane, index)
                    onMenuRequested: (index, mx, my) =>
                        albumMenu.popupForItem(page.itemAt(artistShelf.lane, index), mx, my)
                    onActiveFocusChanged: { if (artistShelf.activeFocus) page.ensureVisible(artistShelf) }
                }

                CrateShelf {
                    id: forgottenShelf

                    title: qsTr("Forgotten favourites")
                    lane: MusicHomeCtl.forgottenLane
                    delegate: sleeveCard
                    cardWidth: page.sleeveSize
                    cardHeight: page.sleeveSize + Theme.spacingTight + page.captionHeight
                    navigationFocusKey: "musicHome-forgotten"
                    onItemActivated: index => page.openItem(forgottenShelf.lane, index)
                    onItemPlayRequested: index => page.playAlbum(forgottenShelf.lane, index)
                    onMenuRequested: (index, mx, my) =>
                        albumMenu.popupForItem(page.itemAt(forgottenShelf.lane, index), mx, my)
                    onActiveFocusChanged: { if (forgottenShelf.activeFocus) page.ensureVisible(forgottenShelf) }
                }

                CrateShelf {
                    id: pullShelf

                    title: qsTr("Pull one out")
                    actionText: qsTr("Reshuffle")
                    actionIcon: "refresh"
                    lane: MusicHomeCtl.pullLane
                    delegate: sleeveCard
                    cardWidth: page.sleeveSize
                    cardHeight: page.sleeveSize + Theme.spacingTight + page.captionHeight
                    navigationFocusKey: "musicHome-pull"
                    onActionTriggered: MusicHomeCtl.reshuffle()
                    onItemActivated: index => page.openItem(pullShelf.lane, index)
                    onItemPlayRequested: index => page.playAlbum(pullShelf.lane, index)
                    onMenuRequested: (index, mx, my) =>
                        albumMenu.popupForItem(page.itemAt(pullShelf.lane, index), mx, my)
                    onActiveFocusChanged: { if (pullShelf.activeFocus) page.ensureVisible(pullShelf) }
                }
            }
        }
    }

    // ── Menus ──────────────────────────────────────────────────────────────
    // Albums and artists: the central item menu, the same verbs as Browse.
    ItemMenu {
        id: albumMenu

        profile: "musicBrowse"
        allowMusicNavigation: true
    }

    StrmMenu {
        id: stationMenu

        property int row: -1

        actions: [
            { "text": qsTr("Play"), "iconName": "play" },
            { "text": qsTr("Shuffle"), "iconName": "shuffle" },
            { "text": qsTr("Add to queue"), "iconName": "queue" }
        ]
        onTriggered: index => {
            if (index === 0)
                MusicHomeCtl.playStation(stationMenu.row)
            else if (index === 1)
                MusicHomeCtl.shuffleStation(stationMenu.row)
            else if (index === 2)
                MusicHomeCtl.queueStation(stationMenu.row)
        }
    }

    // ── The music input context (spec §8) ──────────────────────────────────
    // Space plays or pauses, S shuffles, L favourites. Home has no instant mix
    // key: nothing on it is a single seed the way an album or artist page is.
    MappedShortcut {
        actionId: "music.playPause"
        fallback: ["Space"]
        // Only while something is loaded, so Space stays Select otherwise.
        active: page.musicActive && PlayerCtl.active
        onActivated: PlayerCtl.togglePause()
    }

    MappedShortcut {
        actionId: "music.shuffleAll"
        fallback: ["S"]
        // S on Home is the Shuffle all station: the whole library, sampled by the server.
        active: page.musicActive && page.stationRow("shuffleAll") >= 0
        onActivated: MusicHomeCtl.playStation(page.stationRow("shuffleAll"))
    }

    MappedShortcut {
        actionId: "music.favorite"
        fallback: ["L"]
        active: page.musicActive
        onActivated: page.toggleFocusedFavourite()
    }
}
```

Notes for the implementer:
- With only the error line on screen, its Retry button takes focus through `ShelfError`'s `focus: true`. `heroStops()` then finds no visible stop, so Left is declined (the rail opens) and Right does nothing.
- While the hero is still loading, `heroScope` itself holds the focus. `currentSection()` still finds it, so Up/Down work before any content arrives.
- `page.stationRow("shuffleAll")` inside `active` is re-evaluated when `stationLane.model` changes, because the binding reads `model.count`.
- `RecentRow` uses `parent.width`, which is an `Item` property, so it does not warn. Every other binding names its owner by id.

In `src/CMakeLists.txt`, add `ui/pages/MusicHomePage.qml` to the `qt_add_qml_module` `QML_FILES` list, directly after `ui/pages/PersonPage.qml`.

- [ ] **Step 2: Build and lint**

Run:

```bash
cmake --preset dev && cmake --build --preset dev
bash scripts/check-qmllint-baseline.sh build/dev
```

Expected: the build succeeds. The lint script reports new lines **only** for `MusicHomePage.qml`, and each is `[unqualified]` on `MusicHomeCtl`, `MusicPlay`, `Actions`, `PlayerCtl` or `App`, as every page's context properties do. Any line in a fatal category (`is not a type`, `was not found`, `unavailable`, `incompatible-type`), or any warning on a Crate control, is a failure to fix now. Task 9 re-baselines.

- [ ] **Step 3: Commit**

```bash
git add src/ui/pages/MusicHomePage.qml src/CMakeLists.txt
git commit -m "$(cat <<'MSG'
feat(music): add MusicHomePage with the hero and seven shelves

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
)"
```

The page is not reachable until Task 8 adds the route, and it is not constructed by the self-test until then. That is why Task 8 runs the self-test.

---

### Task 8: Routes, the stack, the input context and the self-test

**Files:**
- Modify: `src/ui/Main.qml`
- Modify: `src/ui/shell/BoundedNavigationStack.qml`
- Test: `tests/unit/tst_navigation_history.cpp`

**Interfaces:**
- Consumes: Task 3 (`MusicHomeCtl.open`), Task 7 (`MusicHomePage`, its `sectionRequested`/`genreRequested` signals, `cycleTab`, `pageBy`), and the existing `MusicCtl.setLibrary`, `MusicCtl.setGenreIds`, `capturePageDeparture`, `pushCapturedPage`, `prepareRoute`.
- Produces:
  - `root.openMusicHome(libraryId, name)` with route `{kind:"musicHome", id, name, key:"musicHome:"+id, title:name}` (contract)
  - `openLibrary` sends `collectionType === "music"` to `openMusicHome` (contract)
  - **Interim:** `root.openMusicSection(libraryId, name, section, genreId)` on the existing `music` route (Contract notes)
  - `readonly property string railKey`
  - `interactionContext` is `"music"` for `musicHomePage`
  - `BoundedNavigationStack.musicHomePageComponent`, `componentFor("musicHome")`, and reconstructed properties `{libraryId, libraryName}`
  - Self-test entry `["musicHome", musicHomeComponent]`

- [ ] **Step 1: Write the failing tests**

In `tests/unit/tst_navigation_history.cpp`:

1. In `kProbe`, after `component MusicProbe: FocusScope { … }`, add:

```qml
    component MusicHomeProbe: FocusScope {
        property string libraryId: ""
        property string libraryName: ""
        objectName: "musicHome-" + libraryId
        focus: true
    }
```

2. After `Component { id: musicComponent; MusicProbe {} }`, add:

```qml
    Component { id: musicHomeComponent; MusicHomeProbe {} }
```

3. In the probe's `BoundedNavigationStack { id: history … }`, after `musicPageComponent: musicComponent`, add:

```qml
        musicHomePageComponent: musicHomeComponent
```

4. After the probe function `openMusicLibraryFromMain(libraryId)`, add:

```qml
    function pushMusicHome(libraryId): void {
        history.pushRoute({ "kind": "musicHome", "id": String(libraryId), "name": "Music Home",
                            "key": "musicHome:" + libraryId, "title": "Music Home" });
    }
```

5. Add the slot `void reconstructsMusicHomeAfterEviction();` after `restoresPerEntryMusicTab();` in the slot list, and its body after `restoresPerEntryMusicTab()`'s definition:

```cpp
void NavigationHistoryTest::reconstructsMusicHomeAfterEviction()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QQuickView view;
    const auto [root, history] = createHistoryProbe(dir, view);
    QVERIFY(root);
    QVERIFY(history);

    QVERIFY(invoke(root, "resetRoute", QStringLiteral("base")));
    QVERIFY(invoke(root, "pushMusicHome", QStringLiteral("lib-1")));
    QTRY_COMPARE(currentItem(history)->objectName(), QStringLiteral("musicHome-lib-1"));
    QCOMPARE(currentItem(history)->property("libraryId").toString(), QStringLiteral("lib-1"));
    const QVariantMap entry = history->property("currentEntry").toMap();
    QCOMPARE(entry.value(QStringLiteral("key")).toString(), QStringLiteral("musicHome:lib-1"));

    // Evict Home's page graph, then walk back to it.
    QVERIFY(invoke(root, "pushRoute", 3));
    QVERIFY(invoke(root, "pushRoute", 4));
    QTRY_COMPARE(history->property("depth").toInt(), 1);
    QVERIFY(invoke(root, "goBack"));
    QVERIFY(invoke(root, "goBack"));
    QTRY_COMPARE(currentItem(history)->objectName(), QStringLiteral("musicHome-lib-1"));
    QCOMPARE(currentItem(history)->property("libraryId").toString(), QStringLiteral("lib-1"));
    QCOMPARE(currentItem(history)->property("libraryName").toString(), QStringLiteral("Music Home"));
    QVERIFY(root->property("preparedRoutes").toStringList().contains(QStringLiteral("musicHome:lib-1")));
}
```

6. In `productionRetargetOrderingRetainsDepartingScopes()`, replace the whole block from `const QByteArray libraryBody = functionBody("openLibrary", "openPlaylists");` through `QVERIFY(musicLoad < musicPush);` with:

```cpp
    // A music library lands on its Home. Home's section strip opens the music
    // route, which still retargets the shared MusicController before the push.
    const QByteArray libraryBody = functionBody("openLibrary", "openMusicHome");
    QVERIFY(!libraryBody.isEmpty());
    QVERIFY(libraryBody.contains("root.openMusicHome(libraryId, name)"));
    QVERIFY(!libraryBody.contains("MusicCtl.loadAlbums"));

    const QByteArray homeBody = functionBody("openMusicHome", "openMusicSection");
    QVERIFY(!homeBody.isEmpty());
    const qsizetype homeCapture = homeBody.indexOf("root.capturePageDeparture");
    const qsizetype homeOpen = homeBody.indexOf("MusicHomeCtl.open");
    const qsizetype homePush = homeBody.indexOf("root.pushCapturedPage");
    QVERIFY(homeCapture >= 0);
    QVERIFY(homeOpen >= 0);
    QVERIFY(homePush >= 0);
    QVERIFY(homeCapture < homeOpen);
    QVERIFY(homeOpen < homePush);

    const QByteArray sectionBody = functionBody("openMusicSection", "openPlaylists");
    QVERIFY(!sectionBody.isEmpty());
    const qsizetype musicCapture = sectionBody.indexOf("root.capturePageDeparture");
    const qsizetype musicPrepare = sectionBody.indexOf("MusicCtl.setLibrary");
    const qsizetype musicRoute = sectionBody.indexOf("root.prepareRoute");
    const qsizetype musicPush = sectionBody.indexOf("root.pushCapturedPage");
    QVERIFY(musicCapture >= 0);
    QVERIFY(musicPrepare >= 0);
    QVERIFY(musicRoute >= 0);
    QVERIFY(musicPush >= 0);
    QVERIFY(musicCapture < musicPrepare);
    QVERIFY(musicPrepare < musicRoute);
    QVERIFY(musicRoute < musicPush);
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build --preset dev --target tst_navigation_history && ctest --preset dev -R tst_navigation_history --output-on-failure`
Expected: FAIL.
- The probe does not load: `Cannot assign to non-existent property "musicHomePageComponent"`. Every slot fails at `createHistoryProbe`.
- After Step 3's stack change alone, `productionRetargetOrderingRetainsDepartingScopes` still fails on `QVERIFY(!libraryBody.isEmpty())`, because `Main.qml` has no `openMusicHome`.

- [ ] **Step 3: Extend `BoundedNavigationStack.qml`**

1. After `property Component musicPageComponent: null`, add:

```qml
    property Component musicHomePageComponent: null
```

2. In `componentFor(route)`, after `case "music": return navigation.musicPageComponent;`, add:

```qml
        case "musicHome": return navigation.musicHomePageComponent;
```

3. In `reconstructedProperties(route)`, after the `case "music": …` return, add:

```qml
        case "musicHome": return { "libraryId": route.id, "libraryName": route.name };
```

- [ ] **Step 4: Wire `Main.qml`**

1. `interactionContext`: add Home to the music pages, so the whole condition reads:

```qml
                                               : stack.currentItem !== null
                                                 && (stack.currentItem.objectName === "musicHomePage"
                                                     || stack.currentItem.objectName === "musicPage"
                                                     || stack.currentItem.objectName === "albumPage"
                                                     || stack.currentItem.objectName === "artistPage")
                                                 ? "music" : "browse"
```

2. Directly after `readonly property string currentKey: …`, add:

```qml
    // The nav rail's view of currentKey. A library's Music Home is that library
    // as far as the rail and the library cycle are concerned.
    readonly property string railKey: root.currentKey.startsWith("musicHome:")
                                      ? root.currentKey.substring("musicHome:".length)
                                      : root.currentKey
```

3. `NavRail { id: navRail … }`: change `current: root.currentKey` to `current: root.railKey`.

4. `cycleDestination(step)`: change `let index = keys.indexOf(root.currentKey);` to `let index = keys.indexOf(root.railKey);`.

5. `prepareRoute(route)`: before `case "music":`, add:

```qml
        case "musicHome":
            // The same library again is refreshStale(): free within the TTL.
            MusicHomeCtl.open(route.id);
            break;
```

6. `openLibrary(libraryId, name, collectionType)`: replace the whole `if (collectionType === "music") { … }` block, including its comment, with:

```qml
        // A music library lands on its Home (Crate spec §4).
        if (collectionType === "music") {
            root.openMusicHome(libraryId, name);
            return;
        }
```

7. Directly after the closing brace of `openLibrary`, and before `openPlaylists`, add:

```qml
    // Home's key is not the library id: MusicPage keeps that key, so Home and a
    // section are two history entries rather than one replacing the other.
    function openMusicHome(libraryId, name): void {
        const key = "musicHome:" + libraryId;
        if (root.currentKey === key) {
            root.focusCurrentPage();
            return;
        }
        root.capturePageDeparture();
        MusicHomeCtl.open(libraryId);
        root.pushCapturedPage({ "kind": "musicHome", "id": libraryId, "name": name,
                                "key": key, "title": name });
    }

    // Interim (Phase 2). Home's section strip and genre bins open the existing
    // MusicPage; Phase 3 replaces this body with openMusicBrowse/openMusicGenre
    // and keeps the page signals. MusicPage has no Genres tab, so "genres" is
    // Albums, and a genre bin is Albums filtered to that genre.
    function openMusicSection(libraryId, name, section, genreId): void {
        const tab = section === "artists" || section === "songs" || section === "playlists"
                  ? section : "albums";
        root.capturePageDeparture();
        MusicCtl.setLibrary(libraryId);
        MusicCtl.setGenreIds(genreId ? [genreId] : []);
        const route = { "kind": "music", "id": libraryId, "name": name,
                        "collectionType": "music",
                        "key": libraryId, "title": name, "tab": tab };
        root.prepareRoute(route);
        root.pushCapturedPage(route);
    }
```

8. After `Component { id: musicComponent … }`, add:

```qml
    Component {
        id: musicHomeComponent

        MusicHomePage {
            id: musicHomePage

            objectName: "musicHomePage"
            onSectionRequested: key =>
                root.openMusicSection(musicHomePage.libraryId, musicHomePage.libraryName, key, "")
            onGenreRequested: (genreId, genreName) =>
                root.openMusicSection(musicHomePage.libraryId, musicHomePage.libraryName, "albums", genreId)
        }
    }
```

9. In the `BoundedNavigationStack { id: stack … }` properties, after `musicPageComponent: musicComponent`, add `musicHomePageComponent: musicHomeComponent`.

10. In the self-test `pages` array, change `["music", musicComponent]` to `["music", musicComponent], ["musicHome", musicHomeComponent]`.

`genreName` is unused by the interim body. Phase 3's `openMusicGenre(libraryId, name, genreId, genreName)` takes it, which is why the signal carries it now.

- [ ] **Step 5: Run the tests, lint and self-test**

Run:

```bash
cmake --build --preset dev && ctest --preset dev -R "tst_navigation_history|tst_qml_accessibility|tst_home_rails" --output-on-failure
bash scripts/check-qmllint-baseline.sh build/dev
STRMQT_SELFTEST=1 QT_QPA_PLATFORM=offscreen QT_ASSUME_STDERR_HAS_CONSOLE=1 ./build/dev/strmqt 2>&1 | grep selftest; echo "exit=${PIPESTATUS[0]}"
```

Expected:
- ctest PASS, including the new `reconstructsMusicHomeAfterEviction`.
- The lint report is unchanged from Task 7: only `[unqualified]` context-property lines on `MusicHomePage.qml`. `Main.qml` gains `MusicHomeCtl` reads, which are the same `[unqualified]` class as its existing `MusicCtl` reads.
- The self-test prints `selftest ok   musicHome`, its page count line includes the new page, no line says `selftest FAIL`, and the exit status is 0.

If the self-test fails with `MusicHomeCtl is not defined`, Task 3's `main.cpp` context property is missing or registered after the QML engine loads `Main.qml`. Fix the order in `main.cpp`; do not guard the page with `typeof`.

- [ ] **Step 6: Commit**

```bash
git add src/ui/Main.qml src/ui/shell/BoundedNavigationStack.qml tests/unit/tst_navigation_history.cpp
git commit -m "$(cat <<'MSG'
feat(music): open a music library on its Home

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
)"
```

---

### Task 9: Phase gate

**Files:**
- Modify: `config/qmllint-baseline.txt` (re-baseline, checked first)

**Interfaces:**
- Consumes: Tasks 1–8, integrated and committed.
- Produces: a green tree, a lint baseline that includes Music Home's context-property reads, and the user's visual sign-off.

**No failing test for this task.** It is the gate: the full suite, the lint baseline, the self-test and a visual check by the user.

- [ ] **Step 1: Full build and suite**

Run:

```bash
cmake --preset dev && cmake --build --preset dev && ctest --preset dev --output-on-failure
```

Expected: every test passes, including `tst_music_lane`, `tst_music_home_controller`, `tst_card_component`, `tst_crate_controls` and `tst_navigation_history`. A failure is fixed in the task that owns the file, with its own commit; it is never skipped.

- [ ] **Step 2: Check the lint diff, then re-baseline**

Run:

```bash
bash scripts/check-qmllint-baseline.sh build/dev > build/dev/phase2-qmllint.txt 2>&1; echo "exit=$?"
grep -E '^[+-][^+-]' build/dev/phase2-qmllint.txt > build/dev/phase2-qmllint-diff.txt || true
cat build/dev/phase2-qmllint-diff.txt
grep -E '^\+' build/dev/phase2-qmllint-diff.txt | grep -v -E '\[(unqualified|unresolved-type)\]' || echo "added lines: context properties only"
grep -E '^\+' build/dev/phase2-qmllint-diff.txt | grep -v -E '^\+(src/ui/pages/MusicHomePage\.qml|src/ui/Main\.qml):' || echo "added lines: MusicHomePage.qml and Main.qml only"
```

Expected:
- `exit=1` only because of the baseline diff. The script prints no line from the fatal set (`is not a type`, `was not found`, `unavailable`, `incompatible-type`); if it does, stop and fix the QML. That set is never baselineable.
- Both checks print their "only" message.
- Every added line names a context property: `MusicHomeCtl`, `MusicPlay`, `Actions`, `PlayerCtl`, `App` or `MusicCtl`.
- Removed (`-`) lines are only the `MusicCtl.setLibrary`/`MusicCtl.tab`/`MusicCtl.loadAlbums` reads that Task 8 deleted from `openLibrary`.

Any other line (a Crate control, `CrateShelf`, a `[missing-property]`, an `[unqualified]` on something that is not a context property) is a defect: fix it in the owning file and rerun this step. Only then:

```bash
bash scripts/check-qmllint-baseline.sh build/dev --update
bash scripts/check-qmllint-baseline.sh build/dev
rm -f build/dev/phase2-qmllint.txt build/dev/phase2-qmllint-diff.txt
```

Expected: `Updated config/qmllint-baseline.txt (N warnings).`, then a clean second run.

- [ ] **Step 3: Self-test**

Run:

```bash
STRMQT_SELFTEST=1 QT_QPA_PLATFORM=offscreen QT_ASSUME_STDERR_HAS_CONSOLE=1 ./build/dev/strmqt 2>&1 | grep selftest; echo "exit=${PIPESTATUS[0]}"
```

Expected: `selftest ok   musicHome` among the page lines, no `selftest FAIL`, and `exit=0`.

- [ ] **Step 4: Commit the baseline**

```bash
git add config/qmllint-baseline.txt
git commit -m "$(cat <<'MSG'
chore(music): re-baseline qmllint for Music Home

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
)"
```

- [ ] **Step 5: Ask the user for the visual check**

This step needs the user's own server and eyes; the agent does not run the app against a real server. Stop and ask the user to run `./build/dev/strmqt`, open a music library, and confirm each item below. Record their answers in the phase report. Anything that fails goes back to its task as a fix commit, followed by Steps 1–3 again.

1. **Landing.** Choosing the music library in the navigation rail opens Music Home, and the rail keeps that library highlighted. The shoulders (Ctrl+Tab) move to Albums, and Back returns to Home with focus where it was.
2. **Hero, with history.** "Pick up where you left off", a 210 px sleeve, the cover wash behind it (muted, not saturated), the title in wide caps, "artist · year · N tracks · runtime" in mono, the amber progress line, **Resume track N**, **Shuffle album**, **♡**. "Recently played" lists up to three albums beside it. ♡ fills and survives leaving and returning.
3. **Hero, no history** (a library with nothing played, or a new user): "Pull one out", **Play**, **Another one ↻**, and ↻ changes the album.
4. **Shelves.** In order: Recently played, New in the crate (with "N added this week" when there are any), Stations (five tiles, "More like …" naming the top artist), Dig by genre (ten bins and "All N genres →"), Artists you play (round), Forgotten favourites, Pull one out (↻ reshuffles). A shelf with nothing in it is absent, heading included.
5. **Loading.** On a cold start each shelf shows a skeleton in its own shape (squares; circles for artists) and they fill independently.
6. **Errors.** With the server briefly unreachable (stop Emby or pull the network after opening the app), a failing shelf collapses to one line with **Retry**, the rest stay usable, and Retry recovers once the server is back.
7. **Keyboard and pad.** Tab stops once per shelf. Down from a shelf lands in the column it left. Left at a shelf's first card opens the rail. Up from the Pull one out shelf reaches ↻ first. Hovering a card never moves the ring. The Menu key (or a held A) opens the item menu on albums and artists, and Play / Shuffle / Add to queue on stations.
8. **Music keys.** Space pauses and resumes while something plays, S starts the Shuffle all station, L favourites the focused album or artist (or the hero album).
9. **Sections and genres.** Albums/Artists/Songs/Playlists in the strip open those tabs of the music page; Genres and a genre bin open Albums (filtered to that genre for a bin). This is the interim route until Phase 3.
10. **Freshness.** Play an album, go back to Home within five minutes: nothing reloads. After five minutes, Recently played updates in place without a skeleton.

---

## Self-review

**Spec §4 (Music Home), item by item:**
- Section strip at the top: `SectionStrip` in Task 5, placed first in Task 7. The non-home keys go through `sectionRequested` to Main (Task 8).
- Hero, resume: Task 2 builds `hero` (mode, summary joined by " · ", progress, `resumeLabel` "Resume track N", favourite via the relay). Task 7 draws the 210 px sleeve, the wash, `crateHeroHome` type, the mono summary, the progress line, and **Resume track N** / **Shuffle album** / **♡**. "Recently played" beside it is `heroRecent`, first three, hero excluded (Task 2 test `heroResumesTheLastAlbum`).
- Hero, no history: `pullOneHero` with kicker "Pull one out", **Play** and **Another one ↻** (Task 2 test `heroPullsOneOutWithoutHistory`, Task 7).
- Shelves 2–8 in order, with their data: `recentAlbums` (≤ 20), `newAlbums` plus `addedThisWeekText` (hidden at 0), five stations from `MusicRepository::stations`, ten genre bins plus the all-genres bin, `topArtists` as portraits, `forgottenFavourites`, `randomAlbums` with ↻ `reshuffle()` (Task 2 tests `stationsResolveAndPlay`, `reshuffleRefillsOnlyThePullLane`).
- Station menu Play / Shuffle / Add to queue: `playStation`, `shuffleStation`, `queueStation` (Tasks 2 and 7).
- Loading in real shape: `CrateShelf.skeletonShape` square or round, and the hero skeleton (Tasks 6 and 7, test `shelfSkeletonHasTheRealShape`). Station and genre shelves use the square silhouette; see the gaps below.
- Empty hidden, heading included: `CrateShelf.visible` follows `lane.empty` (test `shelfHidesWhileTheLaneIsEmpty`).
- Error is one line with Retry; the others stay live: `MusicLane` per shelf, `ShelfError`, and `lanesFailIndependently` in Task 2, `shelfErrorLineRetriesTheLane` in Task 6.
- Freshness: `refreshStale()` on `StackView.onActivated`; within the TTL no request is sent (`refreshStaleIsFreeWithinTheTtl`), and a quiet refresh keeps content on screen (`MusicLane.beginRefresh`).
- Moving about: one tab stop per shelf; `NavigationColumn` preserved by Task 4 (`currentFollowsKeyboardFocus`, `hoverIsNotFocus`); Left at the edge declined to the rail; Menu key through `StrmRail.requestMenuForCurrent`; a held press through each Crate control's `onLongPressed`.

**Spec §8 (navigation, input, errors):**
- Routes: `musicHome` with history, forward stack and focus memory (`reconstructsMusicHomeAfterEviction`, the shelves' `navigationFocusKey`s). `musicBrowse` and the album/artist replacements are Phases 3–4.
- Input: the music context is armed by `musicHomePage`. Space / S / L are mapped. Home implements `cycleTab` (the shoulders) and no `jumpLetter`, as §8 says.
- Errors: each shelf is a lane; `resetSessionState()` on session teardown (`resetSessionStateForgetsTheLibrary`); generation counters drop late replies (`staleRepliesAreDroppedOnReopen`).

**Contract (Phase 2–5 index):**
- `MusicLane`: `model`, `loading`, `error`, `empty`, `ready`, `retry()`, `begin`, `isCurrent`, `succeed`, `fail`, all unchanged. Additions are listed in Contract notes.
- `MusicHomeController` / `MusicHomeCtl`: every listed property and invokable keeps its name and type, including `hero`'s keys. Additions are listed in Contract notes.
- Crate controls: `CrateSleeve`, `CratePortrait`, `CrateBadge`, `CrateKicker`, `CrateHeading`, `CoverCollage`, `StationTile`, `GenreBinTile`, `SectionStrip`, `CrateShelf`, `ShelfError`, each with the contract's properties and signals, in `src/ui/music/` and listed in `Music.cmake`. `CrateShelf.lane` is declared `var` (name unchanged; see Task 6).
- `StrmRail`/`StrmGrid.cardComponent`, passing `model`, `index`, `current`, `hovered`, with focus, navigation memory and paging unchanged (Task 4).
- Routes: `openMusicHome` and its route shape, `openLibrary` for music, `interactionContext`, `cycleTab`, the self-test entry (Task 8).

**Placeholders:** none. Every code step is complete, and every command has its expected output.

**Known gaps, deliberate and small:**
- Station and genre shelves show the square skeleton, not a collage or a fanned stack. The shape and size match; the detail arrives with the content.
- The interim `openMusicSection` keeps the genre filter in `MusicCtl`, not in the route. Back to a MusicPage entry reconstructed after eviction shows whatever filter `MusicCtl` holds then. Phase 3's `musicBrowse` route owns its query and removes this.
- A genre bin has no item menu. The Menu key on the genre shelf does nothing.
- `refreshStale()` does not reset a model whose ids are unchanged, so a changed subtitle on an unchanged album waits for the TTL (Contract notes).
