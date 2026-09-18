# Music Crate, Phase 4: Album, Artist and Playlist Pages

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the album and artist pages with the Crate "back of a sleeve" and "poster and filed releases" designs, and give audio playlists their own Crate page. All three are fed by C++ controllers, so QML never reshapes data. When the phase ends, `AlbumPage.qml`, `ArtistPage.qml`, `MusicController` and `tst_music_query` are gone and nothing in `src/` or `tests/` names `MusicCtl` or `MusicController`.

**Architecture:** see the index, `docs/superpowers/plans/2026-09-16-music-crate.md`. **Read its Global Constraints, Shared vocabulary and "Phase 2–5 contract" before any task.** Every task in this file implicitly includes them.

The data path is:
- `MusicRepository::albumSleeve` / `artistProfile` (Phase 1) → `AlbumController` / `ArtistController` (this phase, `src/app/controllers/music/`) → `MusicAlbumPage` / `MusicArtistPage`.
- `PlaylistController` (existing) gains a member summary and a cover collage → `MusicPlaylistPage`.
- Every play verb goes through `MusicPlayback` (`MusicPlay`), which labels the queue.

Phases 1–3 have landed before this phase starts:
- the music DTOs, repository, models, relay and `MusicPlayback`;
- `src/ui/music/Music.cmake` and the Crate controls (`CrateSleeve`, `CratePortrait`, `CrateBadge`, `CrateKicker`, `CrateHeading`, `CoverCollage`, `ShelfError`);
- `StrmRail`/`StrmGrid` `cardComponent`;
- `MusicHomeController` (`MusicHomeCtl`), `MusicBrowseController` (`MusicBrowseCtl`), `root.openMusicGenre(...)`;
- Phase 3 removed `MusicPage.qml` and the `music` route.

`MusicController` itself is still alive at the start of this phase: Application builds it, `AlbumPage`/`ArtistPage` read it, and `ItemActions::orderedAlbumPlayRequested` is connected to it.

**Spec:** `docs/superpowers/specs/2026-09-16-music-crate-design.md`:
- §6.1–6.3 (the three pages), for the design;
- §8 (routes, input and errors), for navigation and failure states;
- §3.3 (`albumSleeve`, `artistProfile`), for the data each page is built from.

Mockup: `.superpowers/brainstorm/1687594-1789613149/content/album-artist.html` (approved as shown).

## Contract notes

These add to the index's contract or record deviations from it. Nothing the index lists is renamed or re-typed.

1. **`AlbumController` additions** beyond the contract:
   - `discs`: `QVariantList` of `{number, title ("Side A"), detail ("Disc 1 · 24 min"), firstRow}`. Empty unless the album has more than one disc. The track table reads `firstRow` to place the headings, so QML never walks the model to find disc boundaries.
   - `showArtistColumn`: true when any track `differsFromAlbumArtist`.
   - `trackIds` (`QStringList`), for the ＋ Playlist button.
   - `resetSessionState()`, slot `noteFavourite(QString, bool)`, and `setClockForTests(std::function<QDateTime()>)`, for the "Added 3 weeks ago" row.
   - Models are typed properties (`strmqt::music::TrackListModel *tracks`, `strmqt::music::AlbumGridModel *moreBy`). The controller header includes the model headers, so moc sees complete types.
   - `favourite` has its own NOTIFY, `favouriteChanged`. `loading` and `error` notify `stateChanged`. Every display property notifies `albumChanged`.
2. **`ArtistController` additions** beyond the contract:
   - `libraryId` (read-only).
   - `topTrackCaptions` (`QStringList`, one per top track): the album title, or "Guest on *album*" when the artist is not among the track's album artists (spec §6.2).
   - `Q_INVOKABLE playTopTrack(int row)`: plays the five tracks from `row`, labelled "Most played · *name*". The contract gave the right column no play verb, and a list of tracks that can't be played is a dead end.
   - `resetSessionState()` and slot `noteFavourite(QString, bool)`.
   - NOTIFY signals follow the album controller: `artistChanged`, `stateChanged`, `favouriteChanged`.
3. **`MusicRepository::artistTracks(artistId, limit = 200)`** is never cached, because every shuffle is a new random draw. **`MusicPlayback::shuffleArtist`** is `Q_INVOKABLE`. It queues the server's random order as given (`Order::AsGiven`) rather than shuffling a second time.
4. **`PlaylistController` gains `currentSummary`** ("31 tracks · 2 h 4 min") **and `currentCovers`** (up to four distinct cover URLs, in member order), both NOTIFY `summaryChanged`. The spec's "· updated" part is left out: neither `/Playlists/{id}/Items` nor the playlist list carries a last-modified date on 4.9.5, and inventing one from `DateCreated` would be wrong. Video playlists keep `PlaylistPage` and ignore both properties.
5. **Controls** (`src/ui/music/`, listed in `Music.cmake` in a second `qt_target_qml_sources` block, so Phase 2's block is untouched):
   - `LinerNotes`: `rows`; signal `linkActivated(string id, string name)`. This is the contract as written.
   - `CrateTrackTable` is a `TrackTable` with a built-in Crate row. It adds:
     - `discs` (the `AlbumCtl.discs` shape)
     - `artistColumnShown`
     - `showCovers`
     - `captions` (a `QStringList`, one second line per row)
     - `numberFromIndex` (number rows 1…N instead of by track number)
     - read-only `nowPlayingId`
   - `TrackRow` gains `discTitle`, `discDetail` and `titleSuffix`. With the defaults, every existing row draws exactly as before.
6. **Page → shell signals.** `MusicAlbumPage` emits `musicGenreRequested(string genreId, string genreName)` from a liner-notes genre link, and `MusicPlaylistPage` emits `backRequested()` when its playlist is deleted. `Main.qml` handles both through its existing `Connections { target: stack.currentItem; ignoreUnknownSignals: true }`.
   - It adds `root.musicLibraryId()`, which returns `MusicBrowseCtl.libraryId`, else `MusicHomeCtl.libraryId`.
   - It adds `root.openMusicGenreFromPage(genreId, genreName)`. This calls `root.openMusicGenre(musicLibraryId(), qsTr("Music"), …)`, or `Actions.browseGenre(...)` when no music library has been opened this session.
   - `ArtistCtl.open` receives that same library id. The server scopes nothing by it today, but the contract carries it.
7. **Playlist routing.**
   - `root.openRoute("playlist", target)` opens `root.openMusicPlaylist` when `root.interactionContext === "music"`, and the existing `openPlaylist` otherwise. The generic playlist map has no reliable media type, so the surface the user came from decides.
   - `BoundedNavigationStack` gains `musicPlaylistPageComponent`, and `componentFor` returns it for `kind "playlist"` with `mode "audio"`.
8. **`orderedAlbumPlayRequested` → `MusicPlayback::playAlbum(albumId, QString(), 0)`.**
   - The label is empty, because the signal carries only an id. `PlayQueue` then shows its own `contextLabel`, exactly as the contract's `sourceLabel` fallback says.
   - The expansion is now the repository's album query: `Recursive=true`, `SortBy=ParentIndexNumber,IndexNumber,SortName`, `Limit=1000`. The ported tests assert that query, not the old unsorted, non-recursive one. Spec §3.3 defines the album order, and sorting by disc and track makes it explicit.
9. **`PlaylistController::playlistsMutated`.**
   - The old connection to `MusicController::invalidatePlaylists` is removed.
   - If Phase 3 did not already connect the signal, Task 10 connects it to the browse controller's playlists lane: `retry()` on `MusicBrowseController::playlistsLane()`. Use the member name Phase 3 gave the browse controller in `Application.h`. Task 10 Step 5 shows the code with `m_musicBrowse`; substitute if it differs.
10. **qmllint.**
    - The new pages and controls read context properties: `AlbumCtl`, `ArtistCtl`, `PlaylistCtl`, `Actions`, `MusicPlay`, `PlayerCtl`, `App`, `MusicBrowseCtl` and `MusicHomeCtl`. qmllint reports those as `[unqualified]`, and they cannot be qualified.
    - The Phase 4 gate (Task 11) accepts exactly those fingerprints for `MusicAlbumPage.qml`, `MusicArtistPage.qml`, `MusicPlaylistPage.qml`, `LinerNotes.qml` and `CrateTrackTable.qml`, plus the removal of the fingerprints of `AlbumPage.qml`, `ArtistPage.qml` and the `MusicCtl` lines of `Main.qml`.
    - Every other new warning is fixed, never baselined.
11. **Order with Phase 3.** This file edits `Main.qml`, `BoundedNavigationStack.qml`, `tst_navigation_history.cpp`, `tst_item_actions_queue.cpp` and `tst_content_controllers.cpp` by *content*, not by line number, because Phases 2–3 shift every line. Where a step removes something Phase 3 may already have removed (`musicShuffleCarriesTheCurrentFilters`, `musicRetargetDropsTheInFlightPage`, `currentMusicTab`), the step says "if still present".
12. **Album page `R`** (`music.instantMix`) starts the **album** radio (`AlbumCtl.radio()`), the same verb as the ◎ button. The old page seeded a mix from the track under the cursor; that verb stays in the track's ⋯ menu.
13. **Album and artist order** is the repository's: tabs are newest first and top tracks by play count. The controllers never re-sort.

## Waves

| Wave | Tasks | Notes |
|---|---|---|
| 4a | 1 ‖ 4 ‖ 5 | Repository and playback verb; `PlaylistController` summary; `TrackRow` additions plus `LinerNotes`/`CrateTrackTable` (three agents) |
| 4b | 2 | Both controllers and their test (needs 1) |
| 4c | 3 ‖ 6 ‖ 7 ‖ 8 | App wiring; the three pages. The pages need 2's API, 4 and 5 to exist, but not 3 to build (four agents) |
| 4d | 9 | Routes, `interactionContext`, self-test, navigation-history tests |
| 4e | 10 | Deletions, the `orderedAlbumPlayRequested` rewire, test ports, comment sweep |
| 4f | 11 | Phase gate and manual visual check |

Agents in one wave all append to `src/CMakeLists.txt`, `tests/CMakeLists.txt` and `src/ui/music/Music.cmake`. The orchestrator merges those hunks at the wave gate. They only add lines, so the merge is mechanical.

**Commits in parallel waves:** agents share one working tree, so an agent in a wave with more than one agent does **not** run its task's commit step. It reports the files it touched. At the gate, the orchestrator:
1. builds and tests the integrated tree;
2. runs each task's `git add … && git commit` in task order, with that task's message;
3. runs `rm -rf /tmp/w<wave>*` in the same step as the last commit (AGENTS.md).

A task run by a single agent (waves 4b, 4d, 4e, 4f, or any serial execution) commits its own step.

Build commands used in every task (the default dev tree; parallel agents substitute their `/tmp/w<wave><agent>` directory as the index explains):

```bash
cmake --preset dev && cmake --build --preset dev
ctest --preset dev -R <test_name> --output-on-failure
```

---

### Task 1: `MusicRepository::artistTracks` and `MusicPlayback::shuffleArtist`

**Files:**
- Modify: `src/app/music/MusicRepository.h`, `src/app/music/MusicRepository.cpp`
- Modify: `src/app/music/MusicPlayback.h`, `src/app/music/MusicPlayback.cpp`
- Modify: `tests/integration/tst_music_repository.cpp`, `tests/integration/tst_music_playback.cpp`

**Interfaces:**
- Consumes (Phase 1): `MusicRepository::fetchTracks`, `trackFields()`, the anonymous-namespace `ready()` and `kAudio`, and `ItemsQuery::artistIds`. `MusicPlayback::playResolved`, `Order::AsGiven`, `ItemActions::reservePlaybackIntent`.
- Produces:
  - `QFuture<Result<QList<Track>>> MusicRepository::artistTracks(const QString &artistId, int limit = 200)`
  - `Q_INVOKABLE void MusicPlayback::shuffleArtist(const QString &artistId, const QString &name)`, with label `"Shuffle · " + name`

- [x] **Step 1: Write the failing repository test**

In `tests/integration/tst_music_repository.cpp`, add this slot at the end of the `private slots:` list:

```cpp
    void artistTracksAreRandomAndScopedToTheArtist();
```

Add the definition at the end of the file, before `QTEST_GUILESS_MAIN`:

```cpp
// ⇄ Shuffle artist: one random draw of everything the artist performs on. It
// is not scoped to a library and never cached, because each press is a new draw.
void MusicRepositoryTest::artistTracksAreRandomAndScopedToTheArtist()
{
    m_mock->addQueryRoute(QStringLiteral("GET"), itemsPath(),
                          Q{{"ArtistIds", "ar1"}, {"IncludeItemTypes", "Audio"}, {"SortBy", "Random"}},
                          200, page({trackJson("r2", "alX", 1, 2), trackJson("r1", "alY", 1, 1)}));

    const auto first = waitFor(m_repo->artistTracks(QStringLiteral("ar1"), 150));
    QVERIFY2(first.ok(), qPrintable(first.error));
    QCOMPARE(first.value.size(), 2);
    QCOMPARE(first.value.at(0).id, QStringLiteral("r2")); // the server's order, untouched

    const QUrlQuery query(m_mock->lastRequestFor(QStringLiteral("GET"), itemsPath()).query);
    QCOMPARE(query.queryItemValue(QStringLiteral("Recursive")), QStringLiteral("true"));
    QCOMPARE(query.queryItemValue(QStringLiteral("Limit")), QStringLiteral("150"));
    QVERIFY(!query.hasQueryItem(QStringLiteral("ParentId")));

    const int before = m_mock->requestCount();
    const auto second = waitFor(m_repo->artistTracks(QStringLiteral("ar1"), 150));
    QVERIFY(second.ok());
    QCOMPARE(m_mock->requestCount(), before + 1);

    const auto none = waitFor(m_repo->artistTracks(QString()));
    QVERIFY(!none.ok());
}
```

- [x] **Step 2: Write the failing playback test**

In `tests/integration/tst_music_playback.cpp`, add `#include <QUrlQuery>` after `#include <QSignalSpy>`. Then add this slot at the end of the `private slots:` list:

```cpp
    void shuffleArtistQueuesTheServersRandomDraw();
```

Add the definition before `QTEST_GUILESS_MAIN`:

```cpp
void MusicPlaybackTest::shuffleArtistQueuesTheServersRandomDraw()
{
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"ArtistIds", "ar1"}, {"SortBy", "Random"}}, 200,
                          page({trackJson("m2", "x", 2), trackJson("m1", "x", 1)}));

    m_playback->shuffleArtist(QStringLiteral("ar1"), QStringLiteral("Björk"));

    // The server already shuffled. A second shuffle here would be harmless but
    // would make the order untestable, so the draw is queued as given.
    QTRY_COMPARE(queueIds(queue()), (QStringList{QStringLiteral("m2"), QStringLiteral("m1")}));
    QCOMPARE(queue()->currentIndex(), 0);
    QCOMPARE(queue()->sourceLabel(), QStringLiteral("Shuffle · Björk"));
    QVERIFY(!queue()->shuffled());

    const QUrlQuery query(m_mock->lastRequestFor(QStringLiteral("GET"), itemsPath()).query);
    QCOMPARE(query.queryItemValue(QStringLiteral("Limit")), QStringLiteral("200"));
    QCOMPARE(query.queryItemValue(QStringLiteral("IncludeItemTypes")), QStringLiteral("Audio"));
}
```

- [x] **Step 3: Run the tests and watch them fail to compile**

Run: `cmake --build --preset dev --target tst_music_repository tst_music_playback`
Expected: FAIL, with `'class strmqt::music::MusicRepository' has no member named 'artistTracks'` and `… MusicPlayback' has no member named 'shuffleArtist'`.

- [x] **Step 4: Implement `artistTracks`**

In `src/app/music/MusicRepository.h`, directly after the `artistProfile` declaration:

```cpp
    // A random draw of the artist's tracks everywhere they perform (ArtistIds),
    // for ⇄ Shuffle artist. SortBy=Random cannot page, so this is one request,
    // and it is never cached: each press is a new draw.
    QFuture<Result<QList<Track>>> artistTracks(const QString &artistId, int limit = 200);
```

In `src/app/music/MusicRepository.cpp`, directly after the `MusicRepository::artistProfile` definition:

```cpp
QFuture<Result<QList<Track>>> MusicRepository::artistTracks(const QString &artistId, int limit)
{
    if (artistId.isEmpty())
        return ready(Result<QList<Track>>::failure(QStringLiteral("no artist")));

    ItemsQuery query;
    query.artistIds = {artistId};
    query.includeItemTypes = {kAudio};
    query.recursive = true;
    query.sortBy = QStringLiteral("Random");
    query.fields = trackFields();
    query.limit = limit;
    return fetchTracks(query);
}
```

- [x] **Step 5: Implement `shuffleArtist`**

In `src/app/music/MusicPlayback.h`, directly after the `shuffleAlbum` declaration:

```cpp
    // The server draws at random (MusicRepository::artistTracks); the draw is
    // queued as given. Label: "Shuffle · " + name.
    Q_INVOKABLE void shuffleArtist(const QString &artistId, const QString &name);
```

In `src/app/music/MusicPlayback.cpp`, directly after `MusicPlayback::shuffleAlbum`:

```cpp
void MusicPlayback::shuffleArtist(const QString &artistId, const QString &name)
{
    const quint64 generation = m_actions->reservePlaybackIntent();
    playResolved(m_repository->artistTracks(artistId), generation, 0, Order::AsGiven,
                 tr("Shuffle · %1").arg(name));
}
```

- [x] **Step 6: Run the tests and watch them pass**

Run: `cmake --build --preset dev && ctest --preset dev -R 'tst_music_repository|tst_music_playback' --output-on-failure`
Expected: PASS for both executables, including `artistTracksAreRandomAndScopedToTheArtist` and `shuffleArtistQueuesTheServersRandomDraw`.

- [x] **Step 7: Commit**

```bash
git add src/app/music/MusicRepository.h src/app/music/MusicRepository.cpp \
        src/app/music/MusicPlayback.h src/app/music/MusicPlayback.cpp \
        tests/integration/tst_music_repository.cpp tests/integration/tst_music_playback.cpp
git commit -m "feat(music): shuffle an artist from a server-side random draw

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---
### Task 2: `AlbumController` and `ArtistController`

**Files:**
- Create: `src/app/controllers/music/AlbumController.h`, `src/app/controllers/music/AlbumController.cpp`
- Create: `src/app/controllers/music/ArtistController.h`, `src/app/controllers/music/ArtistController.cpp`
- Create: `tests/integration/tst_album_artist_controllers.cpp`
- Modify: `src/CMakeLists.txt` (two source pairs in `strmqt_app`)
- Modify: `tests/CMakeLists.txt` (one test)

**Interfaces:**
- Consumes (Phase 1, plus Task 1 of this phase):
  - repository and playback: `MusicRepository::albumSleeve`, `artistProfile`, `artistTracks`; `MusicPlayback::playTracks`, `shuffleAlbum`, `shuffleArtist`, `radio`;
  - models: `TrackListModel`, `AlbumGridModel`, `ArtistGridModel` (`setItems`, `clear`, `items`, `count`);
  - formatting and DTOs: `formatRuntime`, `formatTrackCount`, `formatRecordCount`, `joinNames`, `coverUrl`, `releaseTypeName`, `toMediaItem`, `MediaItemModel::mapForItem`.
- Produces: `AlbumCtl`/`ArtistCtl` exactly as the index's contract plus Contract notes 1–2. Constructors: `(MusicRepository *repository, MusicPlayback *playback, QObject *parent = nullptr)`.

Rules both controllers follow:
- `open` with the id already shown is free: while it loads, or once it has loaded without error, nothing is requested.
- Otherwise the old content is cleared at once and the pending name becomes the title, so the page header never shows the previous record under a new route.
- Replies carry the generation they were started with, and a superseded reply is dropped.
- A failure keeps the id, sets `error` and leaves `retry()` able to try again.

- [ ] **Step 1: Write the failing test**

`tests/integration/tst_album_artist_controllers.cpp`:

```cpp
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSignalSpy>
#include <QTimeZone>
#include <QtTest>

#include "FakePlayerBackend.h"
#include "MockEmbyServer.h"
#include "app/ItemActions.h"
#include "app/PlayQueue.h"
#include "app/controllers/PlayerController.h"
#include "app/controllers/music/AlbumController.h"
#include "app/controllers/music/ArtistController.h"
#include "app/music/MusicPlayback.h"
#include "app/music/MusicRepository.h"
#include "core/Settings.h"
#include "server/emby/EmbyClient.h"
#include "server/emby/MusicServerCapabilities.h"

using namespace strmqt;
using namespace strmqt::music;

namespace {

const auto kUserId = QStringLiteral("a1b2c3d4e5f60718293a4b5c6d7e8f90");
const auto kToken = QStringLiteral("not-a-real-token-fixture-only");
const auto kLibrary = QStringLiteral("1868998");
constexpr qint64 kTicksPerMinute = 60 * 10'000'000LL;

QString fixturePath(const QString &name)
{
    return QStringLiteral(STRMQT_FIXTURES_DIR "/") + name;
}

QString itemsPath() { return QStringLiteral("/Users/%1/Items").arg(kUserId); }
QString itemPath(const QString &id) { return itemsPath() + QLatin1Char('/') + id; }

QByteArray page(const QJsonArray &items)
{
    return QJsonDocument(QJsonObject{{"Items", items}, {"TotalRecordCount", items.size()}})
        .toJson(QJsonDocument::Compact);
}

QByteArray object(const QJsonObject &json)
{
    return QJsonDocument(json).toJson(QJsonDocument::Compact);
}

QJsonArray refs(const QString &id, const QString &name)
{
    return {QJsonObject{{"Id", id}, {"Name", name}}};
}

QJsonObject flac2496()
{
    return {{"Type", "Audio"}, {"Codec", "flac"}, {"BitDepth", 24}, {"SampleRate", 96000}};
}

// One five-minute track filed under Hollow Coves (ar1). `withFormat` false
// sends no streams at all, so the album has no format summary.
QJsonObject trackJson(const QString &id, const QString &albumId, int disc, int number,
                      const QString &artistId = QStringLiteral("ar1"),
                      const QString &artistName = QStringLiteral("Hollow Coves"),
                      bool withFormat = true)
{
    QJsonObject track{{"Id", id}, {"Name", "Track " + id}, {"Type", "Audio"},
                      {"AlbumId", albumId}, {"Album", "Album " + albumId},
                      {"ParentIndexNumber", disc}, {"IndexNumber", number},
                      {"RunTimeTicks", 5 * kTicksPerMinute},
                      {"ArtistItems", refs(artistId, artistName)},
                      {"AlbumArtists", refs(QStringLiteral("ar1"), QStringLiteral("Hollow Coves"))}};
    if (withFormat) {
        track.insert("MediaStreams", QJsonArray{flac2496()});
        track.insert("MediaSources",
                     QJsonArray{QJsonObject{{"MediaStreams", QJsonArray{flac2496()}}}});
    }
    return track;
}

// A top track on `albumTitle`, filed under `albumArtistId`.
QJsonObject topTrackJson(const QString &id, const QString &albumId, const QString &albumTitle,
                         const QString &albumArtistId, const QString &albumArtistName)
{
    return {{"Id", id}, {"Name", "Track " + id}, {"Type", "Audio"},
            {"AlbumId", albumId}, {"Album", albumTitle},
            {"RunTimeTicks", 4 * kTicksPerMinute},
            {"ArtistItems", refs(QStringLiteral("ar1"), QStringLiteral("Hollow Coves"))},
            {"AlbumArtists", refs(albumArtistId, albumArtistName)}};
}

QJsonObject albumJson(const QString &id, const QString &name, int trackCount,
                      const QString &artistId = QStringLiteral("ar1"),
                      const QString &artistName = QStringLiteral("Hollow Coves"))
{
    return {{"Id", id}, {"Name", name}, {"Type", "MusicAlbum"},
            {"AlbumArtists", refs(artistId, artistName)},
            {"ChildCount", trackCount},
            {"CumulativeRunTimeTicks", trackCount * 5 * kTicksPerMinute},
            {"ImageTags", QJsonObject{{"Primary", "tag-" + id}}}};
}

QStringList queueIds(PlayQueue *queue)
{
    QStringList ids;
    for (int row = 0; row < queue->rowCount(); ++row)
        ids.append(queue->itemAt(row).value(QStringLiteral("itemId")).toString());
    return ids;
}

QVariantMap noteFor(const QVariantList &notes, const QString &label)
{
    for (const QVariant &row : notes) {
        if (row.toMap().value(QStringLiteral("label")).toString() == label)
            return row.toMap();
    }
    return {};
}

using Q = QList<QPair<QString, QString>>;

} // namespace

class AlbumArtistControllersTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void albumDisplayComesFromTheSleeve();
    void linerNotesOmitEmptyRows();
    void albumDropsStaleReplies();
    void albumFailureShowsErrorAndRetryRecovers();
    void albumVerbsCarrySourceLabels();
    void albumFavouriteFollowsItemActions();
    void albumReopenIsFreeAndResetClears();

    void artistProfileFillsTabsAndCaptions();
    void artistHidesEmptyTabs();
    void artistDropsStaleRepliesAndRecoversFromFailure();
    void artistVerbsCarrySourceLabels();

private:
    PlayQueue *queue() const { return m_player->queue(); }
    void routeFullAlbum();
    void routeBareAlbum(const QString &albumId);
    void routeArtist();

    MockEmbyServer *m_mock = nullptr;
    emby::EmbyClient *m_client = nullptr;
    FakePlayerBackend *m_backend = nullptr;
    QTemporaryDir *m_dir = nullptr;
    Settings *m_settings = nullptr;
    PlayerController *m_player = nullptr;
    ItemActions *m_actions = nullptr;
    MusicRepository *m_repo = nullptr;
    MusicPlayback *m_playback = nullptr;
    AlbumController *m_album = nullptr;
    ArtistController *m_artist = nullptr;
    QDateTime m_now = QDateTime::fromSecsSinceEpoch(1'800'000'000, QTimeZone::utc());
};

void AlbumArtistControllersTest::initTestCase()
{
    // "14 April 2023" is a locale decision. Pin it so the test does not depend
    // on the machine it runs on.
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedKingdom));
}

void AlbumArtistControllersTest::init()
{
    m_mock = new MockEmbyServer(this);
    QVERIFY(m_mock->start());
    const QStringList playable{"t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8",
                               "m1", "m2", "hit", "feature"};
    for (const QString &id : playable) {
        QVERIFY(m_mock->addRouteFromFile("POST", QStringLiteral("/Items/%1/PlaybackInfo").arg(id),
                                         fixturePath(QStringLiteral("playback_info.json"))));
    }
    m_mock->addRoute("POST", "/Sessions/Playing", 204, {});
    m_mock->addRoute("POST", "/Sessions/Playing/Progress", 204, {});
    m_mock->addRoute("POST", "/Sessions/Playing/Stopped", 204, {});

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
    m_playback = new MusicPlayback(m_repo, m_actions, this);
    m_album = new AlbumController(m_repo, m_playback, this);
    m_album->setClockForTests([this] { return m_now; });
    m_artist = new ArtistController(m_repo, m_playback, this);
}

void AlbumArtistControllersTest::cleanup()
{
    delete m_artist;
    delete m_album;
    delete m_playback;
    delete m_repo;
    delete m_actions;
    delete m_player;
    delete m_settings;
    delete m_dir;
    delete m_backend;
    delete m_client;
    delete m_mock;
    m_artist = nullptr;
    m_album = nullptr;
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

// "Sunburned Almanac": 8 FLAC 24/96 tracks over two discs, the last one by a
// guest, plus one other album by the same artist.
void AlbumArtistControllersTest::routeFullAlbum()
{
    QJsonObject album = albumJson(QStringLiteral("al1"), QStringLiteral("Sunburned Almanac"), 8);
    album.insert("PremiereDate", "2023-04-14T00:00:00.0000000Z");
    album.insert("ProductionYear", 2023);
    album.insert("GenreItems", QJsonArray{QJsonObject{{"Id", "g1"}, {"Name", "Post-rock"}},
                                          QJsonObject{{"Id", "g2"}, {"Name", "Ambient"}}});
    album.insert("Studios", QJsonArray{QJsonObject{{"Name", "Driftwood Records"}}});
    album.insert("DateCreated", m_now.addDays(-21).toString(Qt::ISODate));
    album.insert("UserData", QJsonObject{{"PlayCount", 7}, {"IsFavorite", false}});
    m_mock->addRoute("GET", itemPath(QStringLiteral("al1")), 200, object(album));

    QJsonArray tracks;
    for (int i = 1; i <= 8; ++i) {
        const QString id = QStringLiteral("t%1").arg(i);
        const int disc = i <= 4 ? 1 : 2;
        const int number = i <= 4 ? i : i - 4;
        tracks.append(i == 8 ? trackJson(id, "al1", disc, number, "ar9", "Kin")
                             : trackJson(id, "al1", disc, number));
    }
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"ParentId", "al1"}, {"IncludeItemTypes", "Audio"}},
                          200, page(tracks));
    m_mock->addQueryRoute("GET", itemsPath(),
                          Q{{"AlbumArtistIds", "ar1"}, {"IncludeItemTypes", "MusicAlbum"}}, 200,
                          page({albumJson("al1", "Sunburned Almanac", 8),
                                albumJson("al2", "Low Tide", 10)}));
}

// Two tracks, no dates, no genres, no label, no streams, no play count.
void AlbumArtistControllersTest::routeBareAlbum(const QString &albumId)
{
    m_mock->addRoute("GET", itemPath(albumId), 200,
                     object(albumJson(albumId, QStringLiteral("Bare Bones"), 2)));
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"ParentId", albumId}, {"IncludeItemTypes", "Audio"}},
                          200,
                          page({trackJson(albumId + "-1", albumId, 1, 1, "ar1", "Hollow Coves", false),
                                trackJson(albumId + "-2", albumId, 1, 2, "ar1", "Hollow Coves", false)}));
}

void AlbumArtistControllersTest::routeArtist()
{
    m_mock->addRoute("GET", itemPath(QStringLiteral("ar1")), 200,
                     object({{"Id", "ar1"}, {"Name", "Hollow Coves"}, {"Type", "MusicArtist"},
                             {"SongCount", 71},
                             {"ImageTags", QJsonObject{{"Primary", "tag-ar1"}}},
                             {"BackdropImageTags", QJsonArray{"bd-ar1"}}}));
    m_mock->addQueryRoute("GET", itemsPath(),
                          Q{{"AlbumArtistIds", "ar1"}, {"IncludeItemTypes", "MusicAlbum"}}, 200,
                          page({albumJson("lp", "Album lp", 10), albumJson("ep", "Album ep", 5),
                                albumJson("single", "Album single", 2)}));
    m_mock->addQueryRoute("GET", itemsPath(),
                          Q{{"ArtistIds", "ar1"}, {"IncludeItemTypes", "MusicAlbum"}}, 200,
                          page({albumJson("lp", "Album lp", 10),
                                albumJson("guest", "Album guest", 9, "ar9", "Kin")}));
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"ArtistIds", "ar1"}, {"IncludeItemTypes", "Audio"}},
                          200,
                          page({topTrackJson("hit", "lp", "Album lp", "ar1", "Hollow Coves"),
                                topTrackJson("feature", "guest", "Album guest", "ar9", "Kin")}));
    if (emby::caps::kSimilarArtists) {
        const QString similarPath = QString::fromLatin1(emby::caps::kSimilarArtistsPath)
                                        .replace(QStringLiteral("{id}"), QStringLiteral("ar1"));
        m_mock->addRoute("GET", similarPath, 200,
                         page({QJsonObject{{"Id", "ar2"}, {"Name", "Kin"}, {"Type", "MusicArtist"}}}));
    }
}

void AlbumArtistControllersTest::albumDisplayComesFromTheSleeve()
{
    routeFullAlbum();

    m_album->open(QStringLiteral("al1"), QStringLiteral("Sunburned Almanac"));
    QCOMPARE(m_album->albumId(), QStringLiteral("al1"));
    QCOMPARE(m_album->title(), QStringLiteral("Sunburned Almanac")); // the pending name
    QVERIFY(m_album->loading());

    QTRY_VERIFY(!m_album->loading());
    QVERIFY2(m_album->error().isEmpty(), qPrintable(m_album->error()));
    QCOMPARE(m_album->artist(), QStringLiteral("Hollow Coves"));
    QCOMPARE(m_album->artistId(), QStringLiteral("ar1"));
    QCOMPARE(m_album->kicker(), QStringLiteral("Album · 2023"));
    QVERIFY(!m_album->coverUrl().isEmpty());
    QCOMPARE(m_album->formatBadge(), QStringLiteral("FLAC 24/96"));
    QVERIFY(m_album->isHiRes());
    QCOMPARE(m_album->discCount(), 2);
    QCOMPARE(m_album->discBadge(), QStringLiteral("2 DISCS"));
    QCOMPARE(m_album->trackSummary(), QStringLiteral("8 tracks · 40 min"));
    QVERIFY(m_album->showArtistColumn());

    QCOMPARE(m_album->tracks()->count(), 8);
    QCOMPARE(m_album->trackIds().size(), 8);
    QCOMPARE(m_album->trackIds().first(), QStringLiteral("t1"));

    const QVariantList discs = m_album->discs();
    QCOMPARE(discs.size(), 2);
    QCOMPARE(discs.at(0).toMap().value("title").toString(), QStringLiteral("Side A"));
    QCOMPARE(discs.at(0).toMap().value("firstRow").toInt(), 0);
    QCOMPARE(discs.at(1).toMap().value("title").toString(), QStringLiteral("Side B"));
    QCOMPARE(discs.at(1).toMap().value("detail").toString(), QStringLiteral("Disc 2 · 20 min"));
    QCOMPARE(discs.at(1).toMap().value("number").toInt(), 2);
    QCOMPARE(discs.at(1).toMap().value("firstRow").toInt(), 4);

    const QVariantList notes = m_album->linerNotes();
    QCOMPARE(notes.size(), 5);
    QCOMPARE(noteFor(notes, "Released").value("value").toString(), QStringLiteral("14 April 2023"));
    const QVariantMap genre = noteFor(notes, "Genre");
    QCOMPARE(genre.value("value").toString(), QStringLiteral("Post-rock · Ambient"));
    const QVariantList links = genre.value("links").toList();
    QCOMPARE(links.size(), 2);
    QCOMPARE(links.at(0).toMap().value("id").toString(), QStringLiteral("g1"));
    QCOMPARE(links.at(0).toMap().value("name").toString(), QStringLiteral("Post-rock"));
    QCOMPARE(noteFor(notes, "Label").value("value").toString(), QStringLiteral("Driftwood Records"));
    QCOMPARE(noteFor(notes, "Format").value("value").toString(), QStringLiteral("FLAC 24/96"));
    QCOMPARE(noteFor(notes, "Added").value("value").toString(),
             QStringLiteral("3 weeks ago · played 7×"));
    QVERIFY(noteFor(notes, "Label").value("links").toList().isEmpty());

    QCOMPARE(m_album->moreBy()->count(), 1); // al1 itself is excluded
    QCOMPARE(m_album->moreByTitle(), QStringLiteral("More by Hollow Coves"));
    QCOMPARE(m_album->albumItem().value("itemId").toString(), QStringLiteral("al1"));
}

void AlbumArtistControllersTest::linerNotesOmitEmptyRows()
{
    routeBareAlbum(QStringLiteral("bare"));

    m_album->open(QStringLiteral("bare"), QStringLiteral("Bare Bones"));
    QTRY_VERIFY(!m_album->loading());
    QVERIFY2(m_album->error().isEmpty(), qPrintable(m_album->error()));

    QVERIFY(m_album->linerNotes().isEmpty());
    QVERIFY(m_album->formatBadge().isEmpty());
    QVERIFY(!m_album->isHiRes());
    QVERIFY(m_album->discBadge().isEmpty());
    QVERIFY(m_album->discs().isEmpty());
    QVERIFY(!m_album->showArtistColumn());
    QCOMPARE(m_album->moreBy()->count(), 0);
    QCOMPARE(m_album->tracks()->count(), 2);
}

void AlbumArtistControllersTest::albumDropsStaleReplies()
{
    routeFullAlbum();
    routeBareAlbum(QStringLiteral("al2"));
    m_mock->setRouteDelay("GET", itemPath(QStringLiteral("al1")), 300);

    m_album->open(QStringLiteral("al1"), QStringLiteral("Sunburned Almanac"));
    m_album->open(QStringLiteral("al2"), QStringLiteral("Bare Bones"));
    QCOMPARE(m_album->title(), QStringLiteral("Bare Bones"));

    QTRY_VERIFY(!m_album->loading());
    QCOMPARE(m_album->tracks()->count(), 2);

    // al1's reply lands now. It belongs to a superseded generation.
    QTest::qWait(450);
    QCOMPARE(m_album->albumId(), QStringLiteral("al2"));
    QCOMPARE(m_album->title(), QStringLiteral("Bare Bones"));
    QCOMPARE(m_album->tracks()->count(), 2);
    QVERIFY(!m_album->loading());
}

void AlbumArtistControllersTest::albumFailureShowsErrorAndRetryRecovers()
{
    m_mock->addRoute("GET", itemPath(QStringLiteral("gone")), 500, "{}");

    m_album->open(QStringLiteral("gone"), QStringLiteral("Gone"));
    QTRY_VERIFY(!m_album->loading());
    QVERIFY(!m_album->error().isEmpty());
    QCOMPARE(m_album->tracks()->count(), 0);
    QCOMPARE(m_album->title(), QStringLiteral("Gone"));

    routeBareAlbum(QStringLiteral("gone"));
    m_album->retry();
    QVERIFY(m_album->loading());
    QTRY_VERIFY(!m_album->loading());
    QVERIFY2(m_album->error().isEmpty(), qPrintable(m_album->error()));
    QCOMPARE(m_album->tracks()->count(), 2);
    QCOMPARE(m_album->title(), QStringLiteral("Bare Bones"));
}

void AlbumArtistControllersTest::albumVerbsCarrySourceLabels()
{
    routeFullAlbum();
    m_mock->addRoute("GET", "/Items/al1/InstantMix", 200,
                     page({trackJson("m1", "x", 1, 1), trackJson("m2", "x", 1, 2)}));
    m_album->open(QStringLiteral("al1"), QStringLiteral("Sunburned Almanac"));
    QTRY_VERIFY(!m_album->loading());

    m_album->play(2);
    QTRY_COMPARE(queue()->rowCount(), 8);
    QCOMPARE(queue()->currentIndex(), 2);
    QCOMPARE(queue()->sourceLabel(), QStringLiteral("Sunburned Almanac"));

    m_album->play(99); // clamped to the last row
    QTRY_COMPARE(queue()->currentIndex(), 7);

    m_album->shuffle();
    QTRY_COMPARE(queue()->sourceLabel(), QStringLiteral("Shuffle · Sunburned Almanac"));
    QCOMPARE(queue()->rowCount(), 8);

    m_album->radio();
    QTRY_COMPARE(queue()->sourceLabel(), QStringLiteral("Radio · Sunburned Almanac"));
    QCOMPARE(queueIds(queue()), (QStringList{QStringLiteral("m1"), QStringLiteral("m2")}));
}

void AlbumArtistControllersTest::albumFavouriteFollowsItemActions()
{
    routeFullAlbum();
    m_album->open(QStringLiteral("al1"), QStringLiteral("Sunburned Almanac"));
    QTRY_VERIFY(!m_album->loading());
    QVERIFY(!m_album->favourite());

    QSignalSpy spy(m_album, &AlbumController::favouriteChanged);
    m_album->noteFavourite(QStringLiteral("someone-else"), true);
    QCOMPARE(spy.count(), 0);

    m_album->noteFavourite(QStringLiteral("al1"), true);
    QCOMPARE(spy.count(), 1);
    QVERIFY(m_album->favourite());
    QVERIFY(m_album->albumItem().value("favorite").toBool());

    m_album->noteFavourite(QStringLiteral("al1"), true); // unchanged: no signal
    QCOMPARE(spy.count(), 1);
}

void AlbumArtistControllersTest::albumReopenIsFreeAndResetClears()
{
    routeFullAlbum();
    m_album->open(QStringLiteral("al1"), QStringLiteral("Sunburned Almanac"));
    QTRY_VERIFY(!m_album->loading());

    const int before = m_mock->requestCount();
    m_album->open(QStringLiteral("al1"), QStringLiteral("Sunburned Almanac"));
    QVERIFY(!m_album->loading());
    QCOMPARE(m_mock->requestCount(), before);

    m_album->resetSessionState();
    QVERIFY(m_album->albumId().isEmpty());
    QVERIFY(m_album->title().isEmpty());
    QCOMPARE(m_album->tracks()->count(), 0);
    QCOMPARE(m_album->moreBy()->count(), 0);
    QVERIFY(!m_album->loading());
}

void AlbumArtistControllersTest::artistProfileFillsTabsAndCaptions()
{
    routeArtist();

    m_artist->open(QStringLiteral("ar1"), QStringLiteral("Hollow Coves"), kLibrary);
    QCOMPARE(m_artist->name(), QStringLiteral("Hollow Coves"));
    QVERIFY(m_artist->loading());
    QTRY_VERIFY(!m_artist->loading());
    QVERIFY2(m_artist->error().isEmpty(), qPrintable(m_artist->error()));

    QCOMPARE(m_artist->artistId(), QStringLiteral("ar1"));
    QCOMPARE(m_artist->libraryId(), kLibrary);
    QCOMPARE(m_artist->kicker(), QStringLiteral("Artist · 3 records · 71 tracks"));
    QVERIFY(!m_artist->coverUrl().isEmpty());
    QVERIFY(!m_artist->backdropUrl().isEmpty());
    QCOMPARE(m_artist->artistItem().value("itemId").toString(), QStringLiteral("ar1"));

    const QVariantList tabs = m_artist->tabs();
    QCOMPARE(tabs.size(), 3);
    QCOMPARE(tabs.at(0).toMap().value("key").toString(), QStringLiteral("albums"));
    QCOMPARE(tabs.at(0).toMap().value("label").toString(), QStringLiteral("Albums"));
    QCOMPARE(tabs.at(0).toMap().value("count").toInt(), 1);
    QCOMPARE(tabs.at(1).toMap().value("key").toString(), QStringLiteral("epsAndSingles"));
    QCOMPARE(tabs.at(1).toMap().value("label").toString(), QStringLiteral("EPs & Singles"));
    QCOMPARE(tabs.at(1).toMap().value("count").toInt(), 2);
    QCOMPARE(tabs.at(2).toMap().value("key").toString(), QStringLiteral("appearsOn"));
    QCOMPARE(tabs.at(2).toMap().value("label").toString(), QStringLiteral("Appears on"));
    QCOMPARE(tabs.at(2).toMap().value("count").toInt(), 1);

    QCOMPARE(m_artist->albums()->count(), 1);
    QCOMPARE(m_artist->epsAndSingles()->count(), 2);
    QCOMPARE(m_artist->appearsOn()->count(), 1);
    QCOMPARE(m_artist->topTracks()->count(), 2);
    QCOMPARE(m_artist->topTrackCaptions(),
             (QStringList{QStringLiteral("Album lp"), QStringLiteral("Guest on Album guest")}));
    QCOMPARE(m_artist->similar()->count(), emby::caps::kSimilarArtists ? 1 : 0);

    m_artist->playTopTrack(1);
    QTRY_COMPARE(queueIds(queue()), (QStringList{QStringLiteral("hit"), QStringLiteral("feature")}));
    QCOMPARE(queue()->currentIndex(), 1);
    QCOMPARE(queue()->sourceLabel(), QStringLiteral("Most played · Hollow Coves"));
}

void AlbumArtistControllersTest::artistHidesEmptyTabs()
{
    m_mock->addRoute("GET", itemPath(QStringLiteral("ar2")), 200,
                     object({{"Id", "ar2"}, {"Name", "Solo"}, {"Type", "MusicArtist"}}));
    m_mock->addQueryRoute("GET", itemsPath(),
                          Q{{"AlbumArtistIds", "ar2"}, {"IncludeItemTypes", "MusicAlbum"}}, 200,
                          page({albumJson("solo-lp", "Solo LP", 11, "ar2", "Solo")}));
    // Appears-on, top tracks and similar are not routed: each 404s, and a
    // secondary failure comes back empty.

    m_artist->open(QStringLiteral("ar2"), QStringLiteral("Solo"));
    QTRY_VERIFY(!m_artist->loading());
    QVERIFY2(m_artist->error().isEmpty(), qPrintable(m_artist->error()));

    const QVariantList tabs = m_artist->tabs();
    QCOMPARE(tabs.size(), 1);
    QCOMPARE(tabs.at(0).toMap().value("key").toString(), QStringLiteral("albums"));
    QCOMPARE(m_artist->kicker(), QStringLiteral("Artist · 1 record"));
    QVERIFY(m_artist->topTrackCaptions().isEmpty());
    QVERIFY(m_artist->libraryId().isEmpty());
}

void AlbumArtistControllersTest::artistDropsStaleRepliesAndRecoversFromFailure()
{
    routeArtist();
    m_mock->addRoute("GET", itemPath(QStringLiteral("ar2")), 200,
                     object({{"Id", "ar2"}, {"Name", "Solo"}, {"Type", "MusicArtist"}}));
    m_mock->setRouteDelay("GET", itemPath(QStringLiteral("ar1")), 300);

    m_artist->open(QStringLiteral("ar1"), QStringLiteral("Hollow Coves"));
    m_artist->open(QStringLiteral("ar2"), QStringLiteral("Solo"));
    QTRY_VERIFY(!m_artist->loading());
    QTest::qWait(450);
    QCOMPARE(m_artist->artistId(), QStringLiteral("ar2"));
    QCOMPARE(m_artist->name(), QStringLiteral("Solo"));
    QCOMPARE(m_artist->albums()->count(), 0);

    m_artist->open(QStringLiteral("nobody"), QStringLiteral("Nobody"));
    QTRY_VERIFY(!m_artist->loading());
    QVERIFY(!m_artist->error().isEmpty());

    m_mock->addRoute("GET", itemPath(QStringLiteral("nobody")), 200,
                     object({{"Id", "nobody"}, {"Name", "Somebody"}, {"Type", "MusicArtist"}}));
    m_artist->retry();
    QTRY_VERIFY(!m_artist->loading());
    QVERIFY2(m_artist->error().isEmpty(), qPrintable(m_artist->error()));
    QCOMPARE(m_artist->name(), QStringLiteral("Somebody"));
}

void AlbumArtistControllersTest::artistVerbsCarrySourceLabels()
{
    // Top tracks are deliberately not routed: their query also carries
    // ArtistIds=ar1 and IncludeItemTypes=Audio, and the random draw below must
    // be the only route that answers it.
    m_mock->addRoute("GET", itemPath(QStringLiteral("ar1")), 200,
                     object({{"Id", "ar1"}, {"Name", "Hollow Coves"}, {"Type", "MusicArtist"}}));
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"ArtistIds", "ar1"}, {"SortBy", "Random"}}, 200,
                          page({trackJson("m2", "x", 1, 2), trackJson("m1", "x", 1, 1)}));
    m_mock->addRoute("GET", "/Items/ar1/InstantMix", 200,
                     page({trackJson("m1", "x", 1, 1), trackJson("m2", "x", 1, 2)}));

    m_artist->open(QStringLiteral("ar1"), QStringLiteral("Hollow Coves"));
    QTRY_VERIFY(!m_artist->loading());

    m_artist->shuffle();
    QTRY_COMPARE(queueIds(queue()), (QStringList{QStringLiteral("m2"), QStringLiteral("m1")}));
    QCOMPARE(queue()->sourceLabel(), QStringLiteral("Shuffle · Hollow Coves"));

    m_artist->radio();
    QTRY_COMPARE(queue()->sourceLabel(), QStringLiteral("Radio · Hollow Coves"));

    QSignalSpy spy(m_artist, &ArtistController::favouriteChanged);
    m_artist->noteFavourite(QStringLiteral("ar1"), true);
    QCOMPARE(spy.count(), 1);
    QVERIFY(m_artist->favourite());
}

QTEST_GUILESS_MAIN(AlbumArtistControllersTest)
#include "tst_album_artist_controllers.moc"
```

In `tests/CMakeLists.txt`, directly after the `tst_music_playback` block Phase 1 added:

```cmake
strmqt_add_test(tst_album_artist_controllers
    integration/tst_album_artist_controllers.cpp
    mocks/MockEmbyServer.h mocks/MockEmbyServer.cpp
    mocks/FakePlayerBackend.h
)
target_include_directories(tst_album_artist_controllers PRIVATE mocks)
```

- [ ] **Step 2: Run the test and watch it fail**

Run: `cmake --preset dev && cmake --build --preset dev --target tst_album_artist_controllers`
Expected: FAIL: `app/controllers/music/AlbumController.h: No such file or directory`.

- [ ] **Step 3: Write `AlbumController`**

`src/app/controllers/music/AlbumController.h`:

```cpp
#pragma once

#include <QDateTime>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

#include "app/music/models/AlbumGridModel.h"
#include "app/music/models/TrackListModel.h"
#include "server/dto/music/MusicTypes.h"

namespace strmqt::music {

class MusicPlayback;
class MusicRepository;

// The album page's data (Crate spec §6.1), exposed to QML as AlbumCtl. Every
// display string is composed here from MusicRepository::albumSleeve, so the
// page only binds.
class AlbumController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString albumId READ albumId NOTIFY albumChanged)
    Q_PROPERTY(QString title READ title NOTIFY albumChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY albumChanged)
    Q_PROPERTY(QString artistId READ artistId NOTIFY albumChanged)
    Q_PROPERTY(QString kicker READ kicker NOTIFY albumChanged)
    Q_PROPERTY(QString coverUrl READ coverUrl NOTIFY albumChanged)
    Q_PROPERTY(QString formatBadge READ formatBadge NOTIFY albumChanged)
    Q_PROPERTY(bool isHiRes READ isHiRes NOTIFY albumChanged)
    Q_PROPERTY(int discCount READ discCount NOTIFY albumChanged)
    Q_PROPERTY(QString discBadge READ discBadge NOTIFY albumChanged)
    Q_PROPERTY(QString trackSummary READ trackSummary NOTIFY albumChanged)
    Q_PROPERTY(QVariantList discs READ discs NOTIFY albumChanged)
    Q_PROPERTY(bool showArtistColumn READ showArtistColumn NOTIFY albumChanged)
    Q_PROPERTY(QStringList trackIds READ trackIds NOTIFY albumChanged)
    Q_PROPERTY(QVariantList linerNotes READ linerNotes NOTIFY albumChanged)
    Q_PROPERTY(QVariantMap albumItem READ albumItem NOTIFY albumChanged)
    Q_PROPERTY(QString moreByTitle READ moreByTitle NOTIFY albumChanged)
    Q_PROPERTY(bool favourite READ favourite NOTIFY favouriteChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(strmqt::music::TrackListModel *tracks READ tracks CONSTANT)
    Q_PROPERTY(strmqt::music::AlbumGridModel *moreBy READ moreBy CONSTANT)

public:
    AlbumController(MusicRepository *repository, MusicPlayback *playback,
                    QObject *parent = nullptr);

    QString albumId() const { return m_album.id; }
    QString title() const { return m_album.title; }
    QString artist() const;
    QString artistId() const;
    QString kicker() const;
    QString coverUrl() const;
    QString formatBadge() const { return m_album.formatSummary; }
    bool isHiRes() const;
    int discCount() const { return static_cast<int>(m_discs.size()); }
    QString discBadge() const;
    QString trackSummary() const;
    QVariantList discs() const;
    bool showArtistColumn() const;
    QStringList trackIds() const;
    QVariantList linerNotes() const;
    QVariantMap albumItem() const;
    QString moreByTitle() const;
    bool favourite() const { return m_album.favourite; }
    bool loading() const { return m_loading; }
    QString error() const { return m_error; }
    TrackListModel *tracks() const { return m_tracks; }
    AlbumGridModel *moreBy() const { return m_moreBy; }

    Q_INVOKABLE void open(const QString &albumId, const QString &name);
    Q_INVOKABLE void retry();
    Q_INVOKABLE void play(int fromIndex);
    Q_INVOKABLE void shuffle();
    Q_INVOKABLE void radio();

    void resetSessionState();
    void setClockForTests(std::function<QDateTime()> clock) { m_clock = std::move(clock); }

public slots:
    // ItemActions::favoriteChanged. Only the open album's own id matters here;
    // the track rows are patched by MusicUserDataRelay.
    void noteFavourite(const QString &itemId, bool favourite);

signals:
    void albumChanged();
    void favouriteChanged();
    void stateChanged();

private:
    void load();
    void apply(const AlbumSleeve &sleeve);
    QDateTime now() const;
    static QString relativeAge(const QDateTime &then, const QDateTime &now);

    MusicRepository *m_repository = nullptr;
    MusicPlayback *m_playback = nullptr;
    TrackListModel *m_tracks = nullptr;
    AlbumGridModel *m_moreBy = nullptr;
    Album m_album;
    QList<Disc> m_discs;
    bool m_loaded = false;
    bool m_loading = false;
    QString m_error;
    quint64 m_generation = 0;
    std::function<QDateTime()> m_clock;
};

} // namespace strmqt::music
```

`src/app/controllers/music/AlbumController.cpp`:

```cpp
#include "app/controllers/music/AlbumController.h"

#include <QLocale>

#include <algorithm>

#include "app/models/MediaItemModel.h"
#include "app/music/MusicFormat.h"
#include "app/music/MusicPlayback.h"
#include "app/music/MusicRepository.h"

namespace strmqt::music {

namespace {

const QString kDot = QStringLiteral(" · ");

} // namespace

AlbumController::AlbumController(MusicRepository *repository, MusicPlayback *playback,
                                 QObject *parent)
    : QObject(parent)
    , m_repository(repository)
    , m_playback(playback)
    , m_tracks(new TrackListModel(this))
    , m_moreBy(new AlbumGridModel(this))
{
}

QString AlbumController::artist() const
{
    return joinNames(m_album.albumArtists);
}

QString AlbumController::artistId() const
{
    return m_album.albumArtists.isEmpty() ? QString() : m_album.albumArtists.first().id;
}

QString AlbumController::kicker() const
{
    if (!m_loaded)
        return {};
    QStringList parts{releaseTypeName(m_album.releaseType)};
    if (m_album.year > 0)
        parts.append(QString::number(m_album.year));
    return parts.join(kDot);
}

QString AlbumController::coverUrl() const
{
    return music::coverUrl(m_album.coverRef);
}

bool AlbumController::isHiRes() const
{
    int valid = 0;
    int hiRes = 0;
    for (const Track &track : m_tracks->items()) {
        if (!track.format.isValid())
            continue;
        ++valid;
        if (track.format.isHiRes)
            ++hiRes;
    }
    return valid > 0 && hiRes * 2 > valid;
}

QString AlbumController::discBadge() const
{
    return m_discs.size() > 1 ? tr("%1 DISCS").arg(m_discs.size()) : QString();
}

QString AlbumController::trackSummary() const
{
    if (!m_loaded)
        return {};
    qint64 runtime = 0;
    for (const Track &track : m_tracks->items())
        runtime += track.runtimeMs;
    const QString length = formatRuntime(runtime);
    const QString count = formatTrackCount(static_cast<int>(m_tracks->items().size()));
    return length.isEmpty() ? count : count + kDot + length;
}

QVariantList AlbumController::discs() const
{
    QVariantList rows;
    if (m_discs.size() <= 1)
        return rows;
    int firstRow = 0;
    for (qsizetype i = 0; i < m_discs.size(); ++i) {
        const Disc &disc = m_discs.at(i);
        const QString title = i < 26
            ? tr("Side %1").arg(QChar(char16_t(u'A' + i)))
            : tr("Disc %1").arg(disc.number);
        QString detail = tr("Disc %1").arg(disc.number);
        const QString length = formatRuntime(disc.runtimeMs);
        if (!length.isEmpty())
            detail += kDot + length;
        rows.append(QVariantMap{{QStringLiteral("number"), disc.number},
                                {QStringLiteral("title"), title},
                                {QStringLiteral("detail"), detail},
                                {QStringLiteral("firstRow"), firstRow}});
        firstRow += static_cast<int>(disc.tracks.size());
    }
    return rows;
}

bool AlbumController::showArtistColumn() const
{
    const QList<Track> &tracks = m_tracks->items();
    return std::any_of(tracks.cbegin(), tracks.cend(),
                       [](const Track &track) { return track.differsFromAlbumArtist; });
}

QStringList AlbumController::trackIds() const
{
    QStringList ids;
    for (const Track &track : m_tracks->items())
        ids.append(track.id);
    return ids;
}

QVariantList AlbumController::linerNotes() const
{
    QVariantList rows;
    const auto add = [&rows](const QString &label, const QString &value,
                             const QVariantList &links = {}) {
        if (value.isEmpty())
            return;
        rows.append(QVariantMap{{QStringLiteral("label"), label},
                                {QStringLiteral("value"), value},
                                {QStringLiteral("links"), links}});
    };
    if (!m_loaded)
        return rows;

    if (m_album.premiereDate.isValid()) {
        add(tr("Released"),
            QLocale().toString(m_album.premiereDate.toUTC().date(), QStringLiteral("d MMMM yyyy")));
    } else if (m_album.year > 0) {
        add(tr("Released"), QString::number(m_album.year));
    }

    QStringList genreNames;
    QVariantList genreLinks;
    for (const GenreRef &genre : m_album.genres) {
        if (genre.name.isEmpty())
            continue;
        genreNames.append(genre.name);
        if (!genre.id.isEmpty()) {
            genreLinks.append(QVariantMap{{QStringLiteral("id"), genre.id},
                                          {QStringLiteral("name"), genre.name}});
        }
    }
    add(tr("Genre"), genreNames.join(kDot), genreLinks);
    add(tr("Label"), m_album.studios.join(kDot));
    add(tr("Format"), m_album.formatSummary);

    const QString plays = m_album.playCount > 0 ? tr("played %1×").arg(m_album.playCount) : QString();
    if (m_album.dateAdded.isValid()) {
        QString value = relativeAge(m_album.dateAdded, now());
        if (!plays.isEmpty())
            value += kDot + plays;
        add(tr("Added"), value);
    } else if (m_album.playCount > 0) {
        add(tr("Played"), tr("%1×").arg(m_album.playCount));
    }
    return rows;
}

QVariantMap AlbumController::albumItem() const
{
    if (m_album.id.isEmpty())
        return {};
    return MediaItemModel::mapForItem(toMediaItem(m_album));
}

QString AlbumController::moreByTitle() const
{
    const QString name = artist();
    return name.isEmpty() ? tr("More like this") : tr("More by %1").arg(name);
}

void AlbumController::open(const QString &albumId, const QString &name)
{
    if (albumId.isEmpty())
        return;
    if (albumId == m_album.id && (m_loading || (m_loaded && m_error.isEmpty())))
        return;
    if (albumId != m_album.id) {
        m_album = Album{};
        m_album.id = albumId;
        m_album.title = name;
        m_discs.clear();
        m_tracks->clear();
        m_moreBy->clear();
        m_loaded = false;
        emit albumChanged();
        emit favouriteChanged();
    }
    load();
}

void AlbumController::retry()
{
    if (!m_album.id.isEmpty())
        load();
}

void AlbumController::load()
{
    const quint64 generation = ++m_generation;
    m_loading = true;
    m_error.clear();
    emit stateChanged();
    m_repository->albumSleeve(m_album.id).then(this, [this, generation](const Result<AlbumSleeve> &result) {
        if (generation != m_generation)
            return;
        m_loading = false;
        if (!result.ok()) {
            m_error = result.error;
            emit stateChanged();
            return;
        }
        apply(result.value);
        emit stateChanged();
    });
}

void AlbumController::apply(const AlbumSleeve &sleeve)
{
    m_album = sleeve.album;
    m_discs = sleeve.discs;
    QList<Track> tracks;
    for (const Disc &disc : m_discs)
        tracks.append(disc.tracks);
    m_tracks->setItems(std::move(tracks));
    m_moreBy->setItems(sleeve.moreByArtist);
    m_loaded = true;
    emit albumChanged();
    emit favouriteChanged();
}

void AlbumController::play(int fromIndex)
{
    const QList<Track> &tracks = m_tracks->items();
    if (tracks.isEmpty())
        return;
    const int last = static_cast<int>(tracks.size()) - 1;
    m_playback->playTracks(tracks, std::clamp(fromIndex, 0, last), m_album.title);
}

void AlbumController::shuffle()
{
    if (!m_album.id.isEmpty())
        m_playback->shuffleAlbum(m_album.id, m_album.title);
}

void AlbumController::radio()
{
    if (!m_album.id.isEmpty())
        m_playback->radio(m_album.id, m_album.title);
}

void AlbumController::noteFavourite(const QString &itemId, bool favourite)
{
    if (itemId.isEmpty() || itemId != m_album.id || m_album.favourite == favourite)
        return;
    m_album.favourite = favourite;
    emit favouriteChanged();
    emit albumChanged(); // albumItem carries the flag for ItemMenu
}

void AlbumController::resetSessionState()
{
    ++m_generation;
    m_album = Album{};
    m_discs.clear();
    m_tracks->clear();
    m_moreBy->clear();
    m_loaded = false;
    m_loading = false;
    m_error.clear();
    emit albumChanged();
    emit favouriteChanged();
    emit stateChanged();
}

QDateTime AlbumController::now() const
{
    return m_clock ? m_clock() : QDateTime::currentDateTimeUtc();
}

QString AlbumController::relativeAge(const QDateTime &then, const QDateTime &now)
{
    const qint64 days = then.daysTo(now);
    if (days <= 0)
        return tr("today");
    if (days == 1)
        return tr("yesterday");
    if (days < 7)
        return tr("%1 days ago").arg(days);
    if (days < 30) {
        const qint64 weeks = days / 7;
        return weeks == 1 ? tr("1 week ago") : tr("%1 weeks ago").arg(weeks);
    }
    if (days < 365) {
        const qint64 months = days / 30;
        return months == 1 ? tr("1 month ago") : tr("%1 months ago").arg(months);
    }
    const qint64 years = days / 365;
    return years == 1 ? tr("1 year ago") : tr("%1 years ago").arg(years);
}

} // namespace strmqt::music
```

`QFuture::then(QObject *context, …)` runs the continuation on the controller's thread and drops it if the controller is destroyed first. That is the same pattern `MusicPlayback` uses. `MusicModelBase::clear()` comes from `MusicListModel<T>` (Phase 1 Task 11).

- [ ] **Step 4: Write `ArtistController`**

`src/app/controllers/music/ArtistController.h`:

```cpp
#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include "app/music/models/AlbumGridModel.h"
#include "app/music/models/ArtistGridModel.h"
#include "app/music/models/TrackListModel.h"
#include "server/dto/music/MusicTypes.h"

namespace strmqt::music {

class MusicPlayback;
class MusicRepository;

// The artist page's data (Crate spec §6.2), exposed to QML as ArtistCtl. Built
// from MusicRepository::artistProfile: one model per filed-release tab, the
// top tracks with their captions, and similar artists.
class ArtistController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString artistId READ artistId NOTIFY artistChanged)
    Q_PROPERTY(QString libraryId READ libraryId NOTIFY artistChanged)
    Q_PROPERTY(QString name READ name NOTIFY artistChanged)
    Q_PROPERTY(QString kicker READ kicker NOTIFY artistChanged)
    Q_PROPERTY(QString coverUrl READ coverUrl NOTIFY artistChanged)
    Q_PROPERTY(QString backdropUrl READ backdropUrl NOTIFY artistChanged)
    Q_PROPERTY(QVariantList tabs READ tabs NOTIFY artistChanged)
    Q_PROPERTY(QStringList topTrackCaptions READ topTrackCaptions NOTIFY artistChanged)
    Q_PROPERTY(QVariantMap artistItem READ artistItem NOTIFY artistChanged)
    Q_PROPERTY(bool favourite READ favourite NOTIFY favouriteChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(strmqt::music::AlbumGridModel *albums READ albums CONSTANT)
    Q_PROPERTY(strmqt::music::AlbumGridModel *epsAndSingles READ epsAndSingles CONSTANT)
    Q_PROPERTY(strmqt::music::AlbumGridModel *appearsOn READ appearsOn CONSTANT)
    Q_PROPERTY(strmqt::music::TrackListModel *topTracks READ topTracks CONSTANT)
    Q_PROPERTY(strmqt::music::ArtistGridModel *similar READ similar CONSTANT)

public:
    ArtistController(MusicRepository *repository, MusicPlayback *playback,
                     QObject *parent = nullptr);

    QString artistId() const { return m_artist.id; }
    QString libraryId() const { return m_libraryId; }
    QString name() const { return m_artist.name; }
    QString kicker() const;
    QString coverUrl() const;
    QString backdropUrl() const;
    QVariantList tabs() const;
    QStringList topTrackCaptions() const;
    QVariantMap artistItem() const;
    bool favourite() const { return m_artist.favourite; }
    bool loading() const { return m_loading; }
    QString error() const { return m_error; }
    AlbumGridModel *albums() const { return m_albums; }
    AlbumGridModel *epsAndSingles() const { return m_epsAndSingles; }
    AlbumGridModel *appearsOn() const { return m_appearsOn; }
    TrackListModel *topTracks() const { return m_topTracks; }
    ArtistGridModel *similar() const { return m_similar; }

    Q_INVOKABLE void open(const QString &artistId, const QString &name,
                          const QString &libraryId = QString());
    Q_INVOKABLE void retry();
    Q_INVOKABLE void shuffle();
    Q_INVOKABLE void radio();
    Q_INVOKABLE void playTopTrack(int row);

    void resetSessionState();

public slots:
    void noteFavourite(const QString &itemId, bool favourite);

signals:
    void artistChanged();
    void favouriteChanged();
    void stateChanged();

private:
    void load();
    void apply(const ArtistProfile &profile);
    void clearModels();

    MusicRepository *m_repository = nullptr;
    MusicPlayback *m_playback = nullptr;
    AlbumGridModel *m_albums = nullptr;
    AlbumGridModel *m_epsAndSingles = nullptr;
    AlbumGridModel *m_appearsOn = nullptr;
    TrackListModel *m_topTracks = nullptr;
    ArtistGridModel *m_similar = nullptr;
    Artist m_artist;
    QString m_libraryId;
    bool m_loaded = false;
    bool m_loading = false;
    QString m_error;
    quint64 m_generation = 0;
};

} // namespace strmqt::music
```

`src/app/controllers/music/ArtistController.cpp`:

```cpp
#include "app/controllers/music/ArtistController.h"

#include <algorithm>

#include "app/models/MediaItemModel.h"
#include "app/music/MusicFormat.h"
#include "app/music/MusicPlayback.h"
#include "app/music/MusicRepository.h"

namespace strmqt::music {

namespace {

const QString kDot = QStringLiteral(" · ");

QVariantMap tab(const QString &key, const QString &label, int count)
{
    return {{QStringLiteral("key"), key},
            {QStringLiteral("label"), label},
            {QStringLiteral("count"), count}};
}

} // namespace

ArtistController::ArtistController(MusicRepository *repository, MusicPlayback *playback,
                                   QObject *parent)
    : QObject(parent)
    , m_repository(repository)
    , m_playback(playback)
    , m_albums(new AlbumGridModel(this))
    , m_epsAndSingles(new AlbumGridModel(this))
    , m_appearsOn(new AlbumGridModel(this))
    , m_topTracks(new TrackListModel(this))
    , m_similar(new ArtistGridModel(this))
{
}

QString ArtistController::kicker() const
{
    if (!m_loaded)
        return {};
    QStringList parts{tr("Artist")};
    const int records = m_albums->count() + m_epsAndSingles->count();
    if (records > 0)
        parts.append(formatRecordCount(records));
    if (m_artist.trackCount > 0)
        parts.append(formatTrackCount(m_artist.trackCount));
    return parts.join(kDot);
}

QString ArtistController::coverUrl() const
{
    return music::coverUrl(m_artist.coverRef);
}

QString ArtistController::backdropUrl() const
{
    return music::coverUrl(m_artist.backdropRef);
}

QVariantList ArtistController::tabs() const
{
    QVariantList rows;
    if (m_albums->count() > 0)
        rows.append(tab(QStringLiteral("albums"), tr("Albums"), m_albums->count()));
    if (m_epsAndSingles->count() > 0)
        rows.append(tab(QStringLiteral("epsAndSingles"), tr("EPs & Singles"), m_epsAndSingles->count()));
    if (m_appearsOn->count() > 0)
        rows.append(tab(QStringLiteral("appearsOn"), tr("Appears on"), m_appearsOn->count()));
    return rows;
}

QStringList ArtistController::topTrackCaptions() const
{
    QStringList captions;
    for (const Track &track : m_topTracks->items()) {
        const bool filedUnderArtist = std::any_of(
            track.albumArtists.cbegin(), track.albumArtists.cend(),
            [this](const ArtistRef &ref) { return ref.id == m_artist.id; });
        captions.append(filedUnderArtist || track.albumArtists.isEmpty()
                            ? track.albumTitle
                            : tr("Guest on %1").arg(track.albumTitle));
    }
    return captions;
}

QVariantMap ArtistController::artistItem() const
{
    if (m_artist.id.isEmpty())
        return {};
    return MediaItemModel::mapForItem(toMediaItem(m_artist));
}

void ArtistController::open(const QString &artistId, const QString &name, const QString &libraryId)
{
    if (artistId.isEmpty())
        return;
    if (artistId == m_artist.id && (m_loading || (m_loaded && m_error.isEmpty())))
        return;
    if (artistId != m_artist.id) {
        m_artist = Artist{};
        m_artist.id = artistId;
        m_artist.name = name;
        m_libraryId = libraryId;
        clearModels();
        m_loaded = false;
        emit artistChanged();
        emit favouriteChanged();
    }
    load();
}

void ArtistController::retry()
{
    if (!m_artist.id.isEmpty())
        load();
}

void ArtistController::load()
{
    const quint64 generation = ++m_generation;
    m_loading = true;
    m_error.clear();
    emit stateChanged();
    m_repository->artistProfile(m_libraryId, m_artist.id)
        .then(this, [this, generation](const Result<ArtistProfile> &result) {
            if (generation != m_generation)
                return;
            m_loading = false;
            if (!result.ok()) {
                m_error = result.error;
                emit stateChanged();
                return;
            }
            apply(result.value);
            emit stateChanged();
        });
}

void ArtistController::apply(const ArtistProfile &profile)
{
    m_artist = profile.artist;
    m_albums->setItems(profile.albums);
    m_epsAndSingles->setItems(profile.epsAndSingles);
    m_appearsOn->setItems(profile.appearsOn);
    m_topTracks->setItems(profile.topTracks);
    m_similar->setItems(profile.similar);
    m_loaded = true;
    emit artistChanged();
    emit favouriteChanged();
}

void ArtistController::clearModels()
{
    m_albums->clear();
    m_epsAndSingles->clear();
    m_appearsOn->clear();
    m_topTracks->clear();
    m_similar->clear();
}

void ArtistController::shuffle()
{
    if (!m_artist.id.isEmpty())
        m_playback->shuffleArtist(m_artist.id, m_artist.name);
}

void ArtistController::radio()
{
    if (!m_artist.id.isEmpty())
        m_playback->radio(m_artist.id, m_artist.name);
}

void ArtistController::playTopTrack(int row)
{
    const QList<Track> &tracks = m_topTracks->items();
    if (tracks.isEmpty())
        return;
    const int last = static_cast<int>(tracks.size()) - 1;
    m_playback->playTracks(tracks, std::clamp(row, 0, last), tr("Most played · %1").arg(m_artist.name));
}

void ArtistController::noteFavourite(const QString &itemId, bool favourite)
{
    if (itemId.isEmpty() || itemId != m_artist.id || m_artist.favourite == favourite)
        return;
    m_artist.favourite = favourite;
    emit favouriteChanged();
    emit artistChanged();
}

void ArtistController::resetSessionState()
{
    ++m_generation;
    m_artist = Artist{};
    m_libraryId.clear();
    clearModels();
    m_loaded = false;
    m_loading = false;
    m_error.clear();
    emit artistChanged();
    emit favouriteChanged();
    emit stateChanged();
}

} // namespace strmqt::music
```

The "Guest on" rule tests album-artist ids, not names. A compilation filed under "Various Artists" therefore captions as a guest appearance, which is what spec §6.2 wants. A track with no album artists at all keeps the plain album title, because there is nothing to compare against.

- [ ] **Step 5: Register the sources**

In `src/CMakeLists.txt`, inside the `strmqt_app` source list, directly after the `app/music/MusicPlayback.cpp` entry Phase 1 added:

```cmake
    app/controllers/music/AlbumController.h app/controllers/music/AlbumController.cpp
    app/controllers/music/ArtistController.h app/controllers/music/ArtistController.cpp
```

If Phase 2 or 3 already added `app/controllers/music/…` entries, place these two lines after the last of them, keeping the block alphabetical.

- [ ] **Step 6: Run the test and watch it pass**

Run: `cmake --build --preset dev --target tst_album_artist_controllers && ctest --preset dev -R tst_album_artist_controllers --output-on-failure`
Expected: PASS (11 tests).

If `albumDisplayComesFromTheSleeve` reports `formatBadge` as empty, Phase 1's mapper read the streams from `MediaSources[0].MediaStreams` only, or from `MediaStreams` only. The fixture sends both, so check that `trackFields()` still includes `MediaStreams` and `MediaSources`, and don't change the controller.

If `kicker()` reads "EP · 2023" for al1, Phase 1's release-type rule classified eight tracks and 40 minutes as an EP. Check Phase 1's `classifyRelease` thresholds against the spec (§3.4) before touching the fixture. The fixture is a normal album.

Then run the neighbouring suites to confirm nothing moved:
Run: `ctest --preset dev -R "tst_music_(repository|playback|models)" --output-on-failure`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add src/app/controllers/music/AlbumController.h src/app/controllers/music/AlbumController.cpp \
        src/app/controllers/music/ArtistController.h src/app/controllers/music/ArtistController.cpp \
        tests/integration/tst_album_artist_controllers.cpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "$(cat <<'MSG'
feat(music): album and artist controllers built from the sleeve and profile

AlbumCtl composes the kicker, disc sides, liner notes and summaries from
MusicRepository::albumSleeve; ArtistCtl splits the profile into tab models
and captions the top tracks. Stale replies are dropped by generation.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
)"
```

---

### Task 3: Application wiring for `AlbumCtl` and `ArtistCtl`

**Files:**
- Modify: `src/app/Application.h`, `src/app/Application.cpp`
- Modify: `src/app/main.cpp`

**Interfaces:**
- Consumes: `AlbumController`/`ArtistController` (Task 2); Phase 1's `m_musicRepository`, `m_musicRelay`, `m_musicPlayback`; `ItemActions::favoriteChanged(const QString &, bool)`.
- Produces:
  - `music::AlbumController *Application::albumController() const` and `music::ArtistController *Application::artistController() const`
  - the QML context properties `AlbumCtl` and `ArtistCtl`

`MusicController` and `MusicCtl` stay in place. Task 10 removes them, once nothing reads them.

- [ ] **Step 1: Declare the controllers**

In `src/app/Application.h`, extend the `namespace music { … }` forward declarations Phase 1 added (Phases 2 and 3 extended the same block) with:

```cpp
class AlbumController;
class ArtistController;
```

Next to the `musicPlayback()` accessor, and after any Phase 2/3 music accessors:

```cpp
    music::AlbumController *albumController() const { return m_albumCtl; }
    music::ArtistController *artistController() const { return m_artistCtl; }
```

Next to `m_musicPlayback`, after any Phase 2/3 music members:

```cpp
    music::AlbumController *m_albumCtl = nullptr;
    music::ArtistController *m_artistCtl = nullptr;
```

- [ ] **Step 2: Construct and connect them**

In `src/app/Application.cpp`, next to the other `controllers/music/…` includes:

```cpp
#include "controllers/music/AlbumController.h"
#include "controllers/music/ArtistController.h"
```

Directly after the last music-controller construction block (Phase 3's `MusicBrowseController` block; if that is missing, Phase 2's `MusicHomeController` block), add:

```cpp
    // The album and artist pages (Crate spec §6.1–6.2). Their track and album
    // models are patched in place by the relay; the page's own heart follows
    // ItemActions directly, because the album or artist item is not a row in
    // any of those models.
    m_albumCtl = new music::AlbumController(m_musicRepository, m_musicPlayback, this);
    m_musicRelay->addModel(m_albumCtl->tracks());
    m_musicRelay->addModel(m_albumCtl->moreBy());
    connect(m_actions, &ItemActions::favoriteChanged, m_albumCtl,
            &music::AlbumController::noteFavourite);

    m_artistCtl = new music::ArtistController(m_musicRepository, m_musicPlayback, this);
    m_musicRelay->addModel(m_artistCtl->albums());
    m_musicRelay->addModel(m_artistCtl->epsAndSingles());
    m_musicRelay->addModel(m_artistCtl->appearsOn());
    m_musicRelay->addModel(m_artistCtl->topTracks());
    m_musicRelay->addModel(m_artistCtl->similar());
    connect(m_actions, &ItemActions::favoriteChanged, m_artistCtl,
            &music::ArtistController::noteFavourite);
```

In `teardownAuthenticatedSession()`, directly after `m_playlists->resetSessionState();`:

```cpp
    m_albumCtl->resetSessionState();
    m_artistCtl->resetSessionState();
```

Before adding the connects, check how `ItemActions::favoriteChanged` is declared (`grep -n "favoriteChanged" src/app/ItemActions.h`). If it is overloaded, use `qOverload<const QString &, bool>(&ItemActions::favoriteChanged)`.

- [ ] **Step 3: Expose `AlbumCtl` and `ArtistCtl`**

In `src/app/main.cpp`, next to the other `controllers/music/…` includes:

```cpp
#include "controllers/music/AlbumController.h"
#include "controllers/music/ArtistController.h"
```

After the `MusicPlay` context-property line Phase 1 added, and after any Phase 2/3 music lines:

```cpp
    engine.rootContext()->setContextProperty(QStringLiteral("AlbumCtl"), app.albumController());
    engine.rootContext()->setContextProperty(QStringLiteral("ArtistCtl"), app.artistController());
```

The model types are already registered for QML: Phase 1 declared `strmqt::music::TrackListModel *` and the grid models as metatypes, and Phase 2 relies on that. If the build reports `QMetaProperty::read: Unable to handle unregistered datatype 'strmqt::music::TrackListModel*'`, add `qRegisterMetaType<strmqt::music::TrackListModel *>();` beside Phase 1's existing registrations. Don't register anything a second time.

- [ ] **Step 4: Build, test, self-test**

Run:

```bash
cmake --preset dev && cmake --build --preset dev && ctest --preset dev --output-on-failure
STRMQT_SELFTEST=1 QT_QPA_PLATFORM=offscreen QT_ASSUME_STDERR_HAS_CONSOLE=1 ./build/dev/strmqt; echo "exit=$?"
```

Expected: every test passes, the self-test prints no `selftest FAIL` line, and the run ends with `exit=0`. The old pages still read `MusicCtl`, so nothing visible changes yet.

- [ ] **Step 5: Commit**

```bash
git add src/app/Application.h src/app/Application.cpp src/app/main.cpp
git commit -m "$(cat <<'MSG'
feat(music): expose AlbumCtl and ArtistCtl to QML

Both controllers are built beside MusicPlayback, their models join the
user-data relay, their hearts follow ItemActions::favoriteChanged and a
session teardown clears them.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
)"
```

---

### Task 4: Playlist summary and cover collage on `PlaylistController`

**Files:**
- Modify: `src/app/controllers/PlaylistController.h`, `src/app/controllers/PlaylistController.cpp`
- Test: `tests/integration/tst_content_controllers.cpp`

**Interfaces:**
- Consumes: `MediaItemModel` signals and `MediaItemModel::dataForItem(item, PosterUrlRole)`, plus `music::formatTrackCount` and `music::formatRuntime` (Phase 1 `MusicFormat`).
- Produces:
  - `Q_PROPERTY(QString currentSummary READ currentSummary NOTIFY summaryChanged)`
  - `Q_PROPERTY(QStringList currentCovers READ currentCovers NOTIFY summaryChanged)`
  - `signal summaryChanged()`

The summary is derived from `m_items`, so every path that changes the members also updates it: the member walk, a removal, a reorder, a reload and a session reset. No verb has to remember to call it.

- [x] **Step 1: Write the failing test**

In `tests/integration/tst_content_controllers.cpp`, declare the slot after `playlistBrowseFilteringIsControllerOwned();`:

```cpp
    void playlistSummaryAndCoversFollowMembers();
```

Define it directly after the body of `playlistBrowseFilteringIsControllerOwned`:

```cpp
void ContentControllersTest::playlistSummaryAndCoversFollowMembers()
{
    PlaylistController playlists(m_client);
    QSignalSpy spy(&playlists, &PlaylistController::summaryChanged);
    QVERIFY(playlists.currentSummary().isEmpty());
    QVERIFY(playlists.currentCovers().isEmpty());

    // Five six-minute tracks over three albums, in member order b, a, b, c, a.
    const QStringList albums{QStringLiteral("alb-b"), QStringLiteral("alb-a"),
                             QStringLiteral("alb-b"), QStringLiteral("alb-c"),
                             QStringLiteral("alb-a")};
    QList<MediaItem> members;
    for (int i = 0; i < albums.size(); ++i) {
        MediaItem item;
        item.id = QStringLiteral("track%1").arg(i);
        item.name = QStringLiteral("Track %1").arg(i);
        item.type = QStringLiteral("Audio");
        item.playlistItemId = QStringLiteral("entry%1").arg(i);
        item.albumId = albums.at(i);
        item.albumPrimaryImageTag = QStringLiteral("tag-") + albums.at(i);
        item.runtimeTicks = 6LL * 60LL * 10'000'000LL;
        members.append(item);
    }
    playlists.items()->setItems(members, members.size());

    QVERIFY(spy.count() >= 1);
    QCOMPARE(playlists.currentSummary(), QStringLiteral("5 tracks · 30 min"));
    const QStringList covers = playlists.currentCovers();
    QCOMPARE(covers.size(), 3);
    QVERIFY(covers.at(0).contains(QStringLiteral("alb-b")));
    QVERIFY(covers.at(1).contains(QStringLiteral("alb-a")));
    QVERIFY(covers.at(2).contains(QStringLiteral("alb-c")));

    playlists.items()->setItems({}, 0);
    QVERIFY(playlists.currentSummary().isEmpty());
    QVERIFY(playlists.currentCovers().isEmpty());
}
```

Six-minute tracks keep the total (30 min) clear of `formatRuntime`'s rounding to the nearest minute.

- [x] **Step 2: Run the test and watch it fail**

Run: `cmake --build --preset dev --target tst_content_controllers`
Expected: FAIL: `'class strmqt::PlaylistController' has no member named 'summaryChanged'`.

- [x] **Step 3: Implement**

In `src/app/controllers/PlaylistController.h`, add `#include <QStringList>` to the includes. After the `errorMessage` property:

```cpp
    // The Crate playlist page's header (spec §6.3), derived from the members:
    // "31 tracks · 2 h 4 min", and up to four distinct album covers in member
    // order for the collage. Both are empty while there are no members.
    Q_PROPERTY(QString currentSummary READ currentSummary NOTIFY summaryChanged)
    Q_PROPERTY(QStringList currentCovers READ currentCovers NOTIFY summaryChanged)
```

After `QString errorMessage() const { return m_error; }`:

```cpp
    QString currentSummary() const { return m_currentSummary; }
    QStringList currentCovers() const { return m_currentCovers; }
```

In `signals:`, after `void errorChanged();`:

```cpp
    void summaryChanged();
```

In `private:`, next to the other helper declarations:

```cpp
    void updateSummary();
```

Next to `m_error`:

```cpp
    QString m_currentSummary;
    QStringList m_currentCovers;
```

In `src/app/controllers/PlaylistController.cpp`, add `#include "app/music/MusicFormat.h"` after `#include "core/Log.h"`. Extend the constructor body:

```cpp
    // The summary follows the member model rather than the verbs, so a load,
    // removal, reorder or reset each keep it right without remembering to.
    connect(m_items, &QAbstractItemModel::modelReset, this, &PlaylistController::updateSummary);
    connect(m_items, &QAbstractItemModel::rowsInserted, this, &PlaylistController::updateSummary);
    connect(m_items, &QAbstractItemModel::rowsRemoved, this, &PlaylistController::updateSummary);
    connect(m_items, &QAbstractItemModel::rowsMoved, this, &PlaylistController::updateSummary);
    connect(m_items, &QAbstractItemModel::dataChanged, this, &PlaylistController::updateSummary);
```

Add the definition after `resetSessionState()`:

```cpp
void PlaylistController::updateSummary()
{
    constexpr int kCollageCovers = 4;
    QString summary;
    QStringList covers;
    const QList<MediaItem> &members = m_items->items();
    if (!members.isEmpty()) {
        qint64 runtimeMs = 0;
        for (const MediaItem &member : members) {
            runtimeMs += member.runtimeMs();
            if (covers.size() < kCollageCovers) {
                const QString cover =
                    MediaItemModel::dataForItem(member, MediaItemModel::PosterUrlRole).toString();
                if (!cover.isEmpty() && !covers.contains(cover))
                    covers.append(cover);
            }
        }
        summary = music::formatTrackCount(static_cast<int>(members.size()));
        const QString length = music::formatRuntime(runtimeMs);
        if (!length.isEmpty())
            summary += QStringLiteral(" · ") + length;
    }
    if (summary == m_currentSummary && covers == m_currentCovers)
        return;
    m_currentSummary = summary;
    m_currentCovers = covers;
    emit summaryChanged();
}
```

`dataChanged` fires for every user-data patch (a heart, a play count), and each one walks the members again. That costs O(n) over at most `kMemberRowLimit` (10,000) rows of plain field reads, and the early return means QML sees no signal when nothing moved.

- [x] **Step 4: Run the test and watch it pass**

Run: `cmake --build --preset dev --target tst_content_controllers && ctest --preset dev -R tst_content_controllers --output-on-failure`
Expected: PASS, including every existing playlist test.

- [x] **Step 5: Commit**

```bash
git add src/app/controllers/PlaylistController.h src/app/controllers/PlaylistController.cpp \
        tests/integration/tst_content_controllers.cpp
git commit -m "$(cat <<'MSG'
feat(playlists): derive a member summary and cover collage

currentSummary ("31 tracks · 2 h 4 min") and currentCovers (up to four
distinct album covers in member order) follow the member model, for the
Crate playlist page's header.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
)"
```

---

### Task 5: `TrackRow` disc sides and title suffix, `LinerNotes`, `CrateTrackTable`

**Files:**
- Modify: `src/ui/controls/TrackRow.qml`
- Create: `src/ui/music/LinerNotes.qml`, `src/ui/music/CrateTrackTable.qml`
- Modify: `src/ui/music/Music.cmake` (a second `qt_target_qml_sources` block)

**Interfaces:**
- Consumes:
  - Phase 2's `CrateKicker` (`text`, `color`)
  - `LinkChip` (`label`, `iconName`, `linked`, `highlighted`, `activated()`)
  - `TrackTable`/`TrackRow` as they are today
  - Phase 1's `TrackListModel` roles: `itemId`, `name`, `displayTitle`, `featuredText`, `artistText`, `trackNumber`, `durationText`, `favourite`, `coverUrl`, `differsFromAlbumArtist`
- Produces:
  - `TrackRow`: `property string discTitle`, `property string discDetail`, `property string titleSuffix`
  - `LinerNotes`: `property var rows`, `signal linkActivated(string id, string name)`
  - `CrateTrackTable` (root `TrackTable`):
    - `property var discs`, `property bool artistColumnShown`, `property bool showCovers`, `property var captions`, `property bool numberFromIndex`
    - `readonly property string nowPlayingId`, `readonly property var discStarts`
    - column metrics `numberColumn`, `durationColumn`, `verbsColumn`, `artistColumnWidth`, `discHeaderHeight`

No C++ changes, so there is no unit test. Verification is the build, the qmllint baseline check and the self-test. The pages in Tasks 6–8 are the consumers, and each one loads these controls in the self-test.

- [x] **Step 1: Give `TrackRow` a disc side and a title suffix**

In `src/ui/controls/TrackRow.qml`, after `property int discNumber: -1` (and its comment), add:

```qml
    // A Crate side heading, "Side A", drawn in the display face instead of
    // the mono "DISC 1" readout. `discDetail` ("Disc 1 · 24 min") sits beside
    // it. Both empty keeps the readout, so every existing table draws as
    // before.
    property string discTitle: ""
    property string discDetail: ""
    // Drawn after the title in the tertiary colour, e.g. "feat. Kin". The
    // title elides first, so the suffix stays readable.
    property string titleSuffix: ""
```

Replace the whole `discBanner` Item (from `Item {` / `id: discBanner` through its closing brace) with:

```qml
    Item {
        id: discBanner

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: row.startsDisc ? row.discHeaderHeight : 0
        visible: row.startsDisc

        Row {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.scale(4)
            spacing: Theme.spacingValue

            Text {
                id: discCaption

                text: row.discTitle.length > 0 ? row.discTitle : qsTr("DISC %1").arg(row.discNumber)
                color: row.discTitle.length > 0 ? Theme.textPrimaryColor : Theme.textTertiary
                font.family: row.discTitle.length > 0 ? Theme.fontDisplay : Theme.fontMono
                font.pixelSize: row.discTitle.length > 0 ? Theme.fontBodyLarge : Theme.fontCaption
                font.weight: row.discTitle.length > 0 ? Font.DemiBold : Font.Normal
                font.letterSpacing: row.discTitle.length > 0 ? 0 : Theme.fontCaption * Theme.trackLabel
            }

            Text {
                anchors.baseline: discCaption.baseline
                visible: row.discDetail.length > 0
                text: row.discDetail
                color: Theme.textTertiary
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontCaption
                font.letterSpacing: Theme.fontCaption * Theme.trackLabel
            }
        }
    }
```

In the `labels` Column, replace the first `Text` (the title, whose `text: row.title`) with:

```qml
            Item {
                width: parent.width
                height: titleText.implicitHeight

                Text {
                    id: titleText

                    anchors.left: parent.left
                    anchors.top: parent.top
                    width: Math.min(titleText.implicitWidth,
                                    parent.width - (suffixText.visible
                                                    ? Math.min(suffixText.implicitWidth, parent.width * 0.4)
                                                      + Theme.spacingTight
                                                    : 0))
                    text: row.title
                    color: row.playing ? Theme.accentColor
                         : (row.hovered || row.current) ? Theme.textPrimaryColor
                         : Theme.textSecondaryColor
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontBodySize
                    elide: Text.ElideRight
                    maximumLineCount: 1

                    Behavior on color {
                        ColorAnimation {
                            duration: Theme.animInstant
                            easing.type: Theme.easeInstant
                        }
                    }
                }

                Text {
                    id: suffixText

                    anchors.left: titleText.right
                    anchors.leftMargin: Theme.spacingTight
                    anchors.right: parent.right
                    anchors.baseline: titleText.baseline
                    visible: row.titleSuffix.length > 0
                    text: row.titleSuffix
                    color: Theme.textTertiary
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontSmall
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }
            }
```

`Accessible.name` still reads `row.title`. Extend it so a screen reader hears the credit:

```qml
    Accessible.name: row.titleSuffix.length > 0 ? row.title + ", " + row.titleSuffix : row.title
```

- [x] **Step 2: Write `LinerNotes`**

`src/ui/music/LinerNotes.qml`:

```qml
pragma ComponentBehavior: Bound
import QtQuick
import StrmQt

// LinerNotes — the facts on the back of a sleeve (Crate spec §6.1).
//
// Rows come finished from AlbumCtl.linerNotes: {label, value, links}. Empty
// rows are already left out, so nothing here decides what is worth showing. A
// row with links (Genre) draws them as chips that open the genre. Any other
// row is a plain value.
Column {
    id: notes

    property var rows: []

    signal linkActivated(string id, string name)

    spacing: Theme.spacingValue

    Repeater {
        model: notes.rows

        delegate: Column {
            id: noteRow

            required property var modelData

            readonly property var links: noteRow.modelData.links ? noteRow.modelData.links : []

            width: notes.width
            spacing: Theme.scale(4)

            CrateKicker {
                text: String(noteRow.modelData.label)
            }

            Text {
                width: parent.width
                visible: noteRow.links.length === 0
                text: String(noteRow.modelData.value)
                color: Theme.textPrimaryColor
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontBodySize
                wrapMode: Text.WordWrap
                maximumLineCount: 3
                elide: Text.ElideRight
            }

            Flow {
                width: parent.width
                visible: noteRow.links.length > 0
                spacing: Theme.spacingTight

                Repeater {
                    model: noteRow.links

                    delegate: LinkChip {
                        id: chip

                        required property var modelData

                        label: String(chip.modelData.name)
                        iconName: "lib-music"
                        onActivated: notes.linkActivated(String(chip.modelData.id),
                                                         String(chip.modelData.name))
                    }
                }
            }
        }
    }
}
```

The chips are pointer targets only (`LinkChip` takes no keyboard focus of its own). Keyboard users reach a genre from the track menu's genre entry, as they do today. Spec §8 asks for pointer and keyboard parity on *verbs*, and a genre link is navigation that the menu already offers.

`lib-music` is the existing glyph for a music destination. The icon set has no tag glyph, and adding icons is out of scope.

- [x] **Step 3: Write `CrateTrackTable`**

`src/ui/music/CrateTrackTable.qml`:

```qml
pragma ComponentBehavior: Bound
import QtQuick
import StrmQt

// CrateTrackTable — TrackTable with the Crate row built in (spec §6.1–6.3).
//
// Everything a Crate row says comes from TrackListModel roles or from the
// controller: the display title with "feat." split off, the artist only where
// it differs from the album credit, disc sides placed at `discs[i].firstRow`.
// The table's own disc and artist walks are off, because the controller has
// already decided both.
TrackTable {
    id: crate

    // AlbumCtl.discs: [{number, title, detail, firstRow}]. Empty draws no
    // headings.
    property var discs: []
    property bool artistColumnShown: false
    property bool showCovers: false
    // One second line per row (ArtistCtl.topTrackCaptions). Empty for none.
    property var captions: []
    // Number rows 1…N (a chart) instead of by track number (a record).
    property bool numberFromIndex: false

    // ── Column metrics, shared by every row ────────────────────────────────
    property int numberColumn: Theme.scale(46)
    property int durationColumn: Theme.scale(64)
    property int verbsColumn: Theme.scale(72)
    property int artistColumnWidth: Theme.scale(200)
    property int discHeaderHeight: Theme.scale(46)

    readonly property var discStarts: {
        const starts = {}
        const list = crate.discs ? crate.discs : []
        for (let i = 0; i < list.length; ++i)
            starts[list[i].firstRow] = list[i]
        return starts
    }

    // Reading queue.currentIndex makes this re-evaluate; currentItem() alone
    // would never notify.
    readonly property string nowPlayingId: {
        const queue = PlayerCtl.queue
        if (!queue || queue.currentIndex < 0)
            return ""
        const current = queue.currentItem()
        return (current && current.itemId !== undefined) ? String(current.itemId) : ""
    }

    discGrouping: false
    artistRule: false
    multiSelect: true
    jumpRole: "name"
    rowHeight: crate.showCovers ? Theme.scale(52) : Theme.scale(38)

    delegate: TrackRow {
        id: trackRow

        required property int index
        required property var model

        readonly property string trackId: trackRow.model.itemId !== undefined
                                          ? String(trackRow.model.itemId) : ""
        readonly property var disc: crate.discStarts[trackRow.index]

        width: crate.width
        navigationFocusOwner: crate

        rowHeight: crate.rowHeight
        discHeaderHeight: crate.discHeaderHeight
        numberColumn: crate.numberColumn
        durationColumn: crate.durationColumn
        verbsColumn: crate.verbsColumn
        artistColumn: crate.artistColumnShown ? crate.artistColumnWidth : 0
        coverSize: Theme.scale(40)

        title: {
            const shown = trackRow.model.displayTitle
            return shown !== undefined && String(shown).length > 0
                   ? String(shown)
                   : (trackRow.model.name !== undefined ? String(trackRow.model.name) : "")
        }
        titleSuffix: trackRow.model.featuredText !== undefined ? String(trackRow.model.featuredText) : ""
        secondary: crate.captions && trackRow.index < crate.captions.length
                   ? String(crate.captions[trackRow.index]) : ""
        artist: crate.artistColumnShown && trackRow.model.differsFromAlbumArtist === true
                ? String(trackRow.model.artistText) : ""
        durationText: trackRow.model.durationText !== undefined ? String(trackRow.model.durationText) : ""
        number: crate.numberFromIndex
                ? trackRow.index + 1
                : (trackRow.model.trackNumber !== undefined ? Number(trackRow.model.trackNumber) : -1)
        discNumber: trackRow.disc ? Number(trackRow.disc.number) : -1
        discTitle: trackRow.disc ? String(trackRow.disc.title) : ""
        discDetail: trackRow.disc ? String(trackRow.disc.detail) : ""
        showCover: crate.showCovers
        coverUrl: trackRow.model.coverUrl !== undefined ? String(trackRow.model.coverUrl) : ""

        current: crate.currentIndex === trackRow.index && crate.activeFocus
        selected: crate.isSelected(trackRow.index)
        playing: trackRow.trackId.length > 0 && trackRow.trackId === crate.nowPlayingId
        favorite: trackRow.model.favourite === true
        showFavorite: true
        showMenu: true
        verbsRevealed: trackRow.hovered || trackRow.favorite

        onActivated: modifiers => {
            crate.forceActiveFocus(Qt.MouseFocusReason)
            crate.activateAt(trackRow.index, modifiers)
        }
        onFavoriteToggled: {
            const item = crate.rowAt(trackRow.index)
            if (item)
                Actions.toggleFavorite(item)
        }
        onMenuRequested: (sceneX, sceneY) => crate.menuRequested(trackRow.index, sceneX, sceneY)
    }
}
```

`crate.rowAt(index)` returns `TrackListModel::get(row)`, which is the media-role map `Actions.toggleFavorite` and `ItemMenu` already accept (Phase 1: every music model answers `get` with `mapForItem` merged with its roles).

- [x] **Step 4: List the controls**

Append to `src/ui/music/Music.cmake`, after Phase 2's block, which stays untouched:

```cmake
# Phase 4: the album, artist and playlist page controls.
qt_target_qml_sources(strmqt
    QML_FILES
        ui/music/LinerNotes.qml
        ui/music/CrateTrackTable.qml
)
```

Match the path prefix Phase 2's block uses. If its entries are written `ui/music/CrateSleeve.qml`, use the form above. If they are relative to the `.cmake` file's own directory, drop the `ui/music/` prefix.

- [x] **Step 5: Build, lint, self-test**

Run:

```bash
cmake --preset dev && cmake --build --preset dev
bash scripts/check-qmllint-baseline.sh build/dev
STRMQT_SELFTEST=1 QT_QPA_PLATFORM=offscreen QT_ASSUME_STDERR_HAS_CONSOLE=1 ./build/dev/strmqt; echo "exit=$?"
```

Expected:
- The build succeeds.
- The self-test ends with `exit=0`. The existing album, playlist and queue pages draw `TrackRow` with the new defaults.
- The lint check may list new `[unqualified]` lines for `PlayerCtl` and `Actions` in `CrateTrackTable.qml`, and nothing else. Record them for the phase gate (Contract note 10), and don't run `--update` here.
- Any other new lint category (`is not a type`, `was not found`, `unavailable`, `incompatible-type`, `missing-property`) is a real error. Fix it before you continue.

- [x] **Step 6: Commit**

```bash
git add src/ui/controls/TrackRow.qml src/ui/music/LinerNotes.qml src/ui/music/CrateTrackTable.qml \
        src/ui/music/Music.cmake
git commit -m "$(cat <<'MSG'
feat(music): Crate track table, liner notes and side headings

TrackRow gains a display-face side heading and a tertiary title suffix,
both off by default. CrateTrackTable binds TrackListModel roles and the
controller's disc sides; LinerNotes draws the sleeve's facts with genre
links.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
)"
```

---

### Task 6: `MusicAlbumPage`

**Files:**
- Create: `src/ui/pages/MusicAlbumPage.qml`
- Modify: `src/CMakeLists.txt` (QML page list)

**Interfaces:**
- Consumes:
  - `AlbumCtl` (Tasks 2–3; the page builds against the context property, and Task 3 only has to land before the self-test in Task 9)
  - Task 5's `CrateTrackTable` and `LinerNotes`
  - Phase 2's `CrateSleeve`, `CrateBadge`, `CrateKicker`, `CrateHeading`, and `StrmRail.cardComponent`/`customCardWidth`/`customCardHeight`
  - `MusicPlay.playAlbum`
  - `Actions.openArtist`, `openAlbum`, `toggleFavorite`, `setFavoriteAll`, `addAllToQueue`
  - `PlayerCtl.togglePause`
  - existing `ItemMenu`, `PlaylistPicker`, `SelectionBar`, `StrmToastHost`, `LoadingState`, `EmptyState`, `CoverWash`, `MappedShortcut`
- Produces `MusicAlbumPage`:
  - `property var albumItem`
  - `signal musicGenreRequested(string genreId, string genreName)`
  - `function cycleTab(step): bool`
  - `readonly property string albumId`

The design is spec §6.1: the back of a sleeve.
- The left column holds the sleeve and the liner notes.
- The right column holds the header and the tracks, headed by side (A/B) when there is more than one disc.
- A More-by rail is the table's footer, so it scrolls with the record rather than stealing height from it.
- Below `Theme.scale(1000)` of page width, the left column folds away: a smaller sleeve joins the header, and the liner notes move into the footer above More-by.

Two strings in this file are pinned by `tst_navigation_history` (Task 9), so write them exactly: `Actions.openArtist(AlbumCtl.artistId, AlbumCtl.artist)` and the absence of any `MusicCtl`.

- [ ] **Step 1: Write the page**

`src/ui/pages/MusicAlbumPage.qml`:

```qml
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import StrmQt

// MusicAlbumPage — the back of a sleeve (Crate spec §6.1).
//
// Everything on it is AlbumCtl's: the kicker, the badges, the sides, the liner
// notes and the More-by shelf are composed in C++ from one albumSleeve call, so
// this file only lays them out. Play verbs go through AlbumCtl, which hands the
// queue its source label ("Sunburned Almanac", "Shuffle · …", "Radio · …").
//
// The page pushes nothing itself. The artist credit uses Actions.openArtist,
// a More-by sleeve uses Actions.openAlbum, and a genre link is raised to the
// shell as musicGenreRequested, where Main decides which library it opens in.
FocusScope {
    id: page

    // ── Contract ───────────────────────────────────────────────────────────
    // The album's item map as the route carried it. Only itemId and name are
    // read: the sleeve comes from AlbumCtl, so a stale map can never show an
    // old cover or year. A page with no map (the self-test) shows whatever
    // AlbumCtl holds.
    property var albumItem: ({})

    signal musicGenreRequested(string genreId, string genreName)

    readonly property string albumId: page.albumItem && page.albumItem.itemId !== undefined
                                      ? String(page.albumItem.itemId) : AlbumCtl.albumId
    readonly property string albumName: page.albumItem && page.albumItem.name !== undefined
                                        ? String(page.albumItem.name) : AlbumCtl.title
    readonly property bool hasTracks: AlbumCtl.albumId === page.albumId && AlbumCtl.tracks.count > 0
    readonly property bool narrow: page.width < Theme.scale(1000)
    readonly property bool isActivePage: page.StackView.status === StackView.Active
                                         || page.StackView.view === null
    readonly property int sleeveSize: page.narrow ? Theme.scale(200) : Theme.scale(340)

    function ensureOpen(): void {
        if (page.albumId.length > 0 && AlbumCtl.albumId !== page.albumId)
            AlbumCtl.open(page.albumId, page.albumName)
    }

    Component.onCompleted: Qt.callLater(page.ensureOpen)
    // A covered album page becomes visible again after a second album was
    // opened on top of it: take the shared controller back.
    onVisibleChanged: { if (page.visible) Qt.callLater(page.ensureOpen) }

    // The pad's shoulders (Main.qml cycleSection). An album has no tab bar, so
    // they walk the page's three regions: the verbs, the tracks and the shelf
    // under them.
    function cycleTab(step): bool {
        const footer = trackTable.footerItem
        const stops = [playButton, trackTable]
        if (footer && footer.rail && footer.rail.visible)
            stops.push(footer.rail)
        let at = 0
        for (let i = 0; i < stops.length; ++i) {
            if (stops[i].activeFocus)
                at = i
        }
        const next = stops[(at + step + stops.length) % stops.length]
        next.forceActiveFocus(Qt.TabFocusReason)
        if (next !== playButton)
            trackTable.positionViewAtIndex(next === trackTable ? Math.max(0, trackTable.currentIndex)
                                                               : trackTable.count - 1,
                                           next === trackTable ? ListView.Contain : ListView.End)
        return true
    }

    function fileSelection(): void {
        const ids = trackTable.selectedIds()
        if (ids.length > 0)
            playlistPicker.show(AlbumCtl.title, ids)
    }

    function toggleFavouriteInScope(): void {
        if (trackTable.selectionCount > 0) {
            Actions.setFavoriteAll(trackTable.selectedIds(), true)
            return
        }
        if (trackTable.activeFocus) {
            const track = trackTable.rowAt(trackTable.currentIndex)
            if (track) {
                Actions.toggleFavorite(track)
                return
            }
        }
        if (AlbumCtl.albumId.length > 0)
            Actions.toggleFavorite(AlbumCtl.albumItem)
    }

    // ── The music input context (spec §8) ──────────────────────────────────
    // Window-scoped shortcuts, so each one is gated on this page being the
    // one on top: a covered album must not answer S for the album above it.
    MappedShortcut {
        actionId: "music.playPause"
        fallback: ["Space"]
        active: App.interactionContext === "music" && page.isActivePage && PlayerCtl.active
        onActivated: PlayerCtl.togglePause()
    }

    MappedShortcut {
        actionId: "music.shuffleAll"
        fallback: ["S"]
        active: App.interactionContext === "music" && page.isActivePage && page.hasTracks
        onActivated: AlbumCtl.shuffle()
    }

    MappedShortcut {
        actionId: "music.favorite"
        fallback: ["L"]
        active: App.interactionContext === "music" && page.isActivePage
        onActivated: page.toggleFavouriteInScope()
    }

    MappedShortcut {
        actionId: "music.instantMix"
        fallback: ["R"]
        active: App.interactionContext === "music" && page.isActivePage && AlbumCtl.albumId.length > 0
        onActivated: AlbumCtl.radio()
    }

    // ── Atmosphere ─────────────────────────────────────────────────────────
    CoverWash {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Math.round(page.height * 0.5)
        z: -1
        source: AlbumCtl.coverUrl
    }

    // ── Left column: the sleeve and its notes ──────────────────────────────
    Flickable {
        id: sleeveColumn

        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.pageMarginValue
        anchors.topMargin: Theme.spacingLoose
        width: page.narrow ? 0 : page.sleeveSize
        visible: !page.narrow
        contentHeight: sleeveStack.implicitHeight + Theme.spacingLoose
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: sleeveStack

            width: sleeveColumn.width
            spacing: Theme.spacingLoose

            CrateSleeve {
                size: page.sleeveSize
                coverUrl: AlbumCtl.coverUrl
                title: AlbumCtl.title
                showCaption: false
                badge: ""
                hiRes: false
            }

            LinerNotes {
                width: parent.width
                rows: AlbumCtl.linerNotes
                onLinkActivated: (id, name) => page.musicGenreRequested(id, name)
            }
        }
    }

    // ── Right column: header, selection, tracks ────────────────────────────
    Item {
        id: mainColumn

        anchors.left: page.narrow ? parent.left : sleeveColumn.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.leftMargin: page.narrow ? Theme.pageMarginValue : Theme.spacingLoose * 2
        anchors.rightMargin: Theme.pageMarginValue
        anchors.topMargin: Theme.spacingLoose

        Row {
            id: header

            anchors.left: parent.left
            anchors.right: parent.right
            spacing: Theme.spacingLoose

            CrateSleeve {
                id: narrowSleeve

                visible: page.narrow
                size: page.sleeveSize
                coverUrl: AlbumCtl.coverUrl
                title: AlbumCtl.title
                showCaption: false
            }

            Column {
                id: headerText

                width: header.width - (narrowSleeve.visible ? narrowSleeve.width + header.spacing : 0)
                spacing: Theme.spacingTight

                CrateKicker {
                    text: AlbumCtl.kicker
                    visible: text.length > 0
                }

                CrateHeading {
                    width: parent.width
                    text: AlbumCtl.title.length > 0 ? AlbumCtl.title : page.albumName
                    pixelSize: Theme.crateHeroAlbum
                }

                Item {
                    id: artistLink

                    width: artistChip.implicitWidth
                    height: artistChip.implicitHeight
                    visible: AlbumCtl.artist.length > 0
                    activeFocusOnTab: AlbumCtl.artistId.length > 0

                    KeyNavigation.down: playButton
                    Keys.onReturnPressed: event => { if (!event.isAutoRepeat) artistChip.activated() }
                    Keys.onEnterPressed: event => { if (!event.isAutoRepeat) artistChip.activated() }

                    LinkChip {
                        id: artistChip

                        anchors.fill: parent
                        label: AlbumCtl.artist
                        iconName: "user"
                        linked: AlbumCtl.artistId.length > 0
                        highlighted: artistLink.activeFocus
                        onActivated: Actions.openArtist(AlbumCtl.artistId, AlbumCtl.artist)
                    }
                }

                Flow {
                    width: parent.width
                    spacing: Theme.spacingTight

                    CrateBadge {
                        visible: AlbumCtl.formatBadge.length > 0
                        text: AlbumCtl.formatBadge
                        hiRes: AlbumCtl.isHiRes
                    }

                    CrateBadge {
                        visible: AlbumCtl.discBadge.length > 0
                        text: AlbumCtl.discBadge
                        hiRes: false
                    }

                    Text {
                        height: Theme.scale(24)
                        verticalAlignment: Text.AlignVCenter
                        visible: AlbumCtl.trackSummary.length > 0
                        text: AlbumCtl.trackSummary
                        color: Theme.textSecondaryColor
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontSmall
                    }
                }

                Item {
                    width: 1
                    height: Theme.spacingTight
                }

                Flow {
                    width: parent.width
                    spacing: Theme.spacingTight

                    StrmButton {
                        id: playButton

                        text: qsTr("Play")
                        iconName: "play"
                        variant: "primary"
                        enabled: page.hasTracks
                        onClicked: AlbumCtl.play(0)

                        KeyNavigation.up: artistLink
                        KeyNavigation.right: shuffleButton
                        KeyNavigation.down: trackTable
                    }

                    StrmButton {
                        id: shuffleButton

                        text: qsTr("Shuffle")
                        iconName: "shuffle"
                        enabled: page.hasTracks
                        onClicked: AlbumCtl.shuffle()

                        KeyNavigation.up: artistLink
                        KeyNavigation.left: playButton
                        KeyNavigation.right: radioButton
                        KeyNavigation.down: trackTable
                    }

                    StrmButton {
                        id: radioButton

                        text: qsTr("Album radio")
                        iconName: "audio-track"
                        enabled: AlbumCtl.albumId.length > 0
                        onClicked: AlbumCtl.radio()

                        KeyNavigation.up: artistLink
                        KeyNavigation.left: shuffleButton
                        KeyNavigation.right: playlistButton
                        KeyNavigation.down: trackTable
                    }

                    StrmButton {
                        id: playlistButton

                        text: qsTr("Playlist")
                        iconName: "plus"
                        enabled: page.hasTracks
                        onClicked: playlistPicker.show(AlbumCtl.title, AlbumCtl.trackIds)

                        KeyNavigation.up: artistLink
                        KeyNavigation.left: radioButton
                        KeyNavigation.right: favouriteButton
                        KeyNavigation.down: trackTable
                    }

                    StrmIconButton {
                        id: favouriteButton

                        iconName: AlbumCtl.favourite ? "heart-filled" : "heart"
                        checked: AlbumCtl.favourite
                        enabled: AlbumCtl.albumId.length > 0
                        tooltip: AlbumCtl.favourite ? qsTr("Remove from favourites")
                                                    : qsTr("Add to favourites")
                        shortcut: "L"
                        onClicked: Actions.toggleFavorite(AlbumCtl.albumItem)

                        KeyNavigation.up: artistLink
                        KeyNavigation.left: playlistButton
                        KeyNavigation.down: trackTable
                    }
                }
            }
        }

        SelectionBar {
            id: selectionBar

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: header.bottom
            anchors.topMargin: Theme.spacingLoose

            count: trackTable.selectionCount
            onQueueRequested: Actions.addAllToQueue(trackTable.selectedItems())
            onPlaylistRequested: page.fileSelection()
            onFavoriteRequested: Actions.setFavoriteAll(trackTable.selectedIds(), true)
            onClearRequested: {
                trackTable.clearSelection()
                trackTable.forceActiveFocus(Qt.OtherFocusReason)
            }
        }

        CrateTrackTable {
            id: trackTable

            navigationFocusKey: "album-tracks"
            navigationFocusFallbackItem: playButton
            navigationFocusRefillActive: AlbumCtl.loading

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: selectionBar.bottom
            anchors.bottom: parent.bottom
            anchors.topMargin: Theme.spacingTight
            clip: true
            focus: page.hasTracks
            visible: page.hasTracks

            model: AlbumCtl.tracks
            discs: AlbumCtl.discs
            artistColumnShown: AlbumCtl.showArtistColumn

            KeyNavigation.up: playButton

            onActivated: index => AlbumCtl.play(index)
            onMenuRequested: (index, mx, my) =>
                trackMenu.popupForItemNoDetails(trackTable.rowAt(index), mx, my)

            footer: Column {
                id: footerColumn

                property alias rail: moreByRail

                width: trackTable.width
                topPadding: Theme.spacingLoose * 2
                spacing: Theme.spacingLoose

                LinerNotes {
                    width: parent.width
                    visible: page.narrow
                    rows: AlbumCtl.linerNotes
                    onLinkActivated: (id, name) => page.musicGenreRequested(id, name)
                }

                StrmRail {
                    id: moreByRail

                    width: parent.width
                    visible: AlbumCtl.moreBy.count > 0
                    title: AlbumCtl.moreByTitle
                    railModel: AlbumCtl.moreBy
                    navigationFocusKey: "album-more-by"
                    customCardWidth: Theme.crateSleeveSize
                    customCardHeight: Theme.crateSleeveSize + Theme.scale(52)

                    KeyNavigation.up: trackTable

                    cardComponent: Component {
                        CrateSleeve {
                            property var model
                            property int index: -1

                            size: Theme.crateSleeveSize
                            showCaption: true
                            coverUrl: model ? String(model.coverUrl) : ""
                            title: model ? String(model.title) : ""
                            subtitle: model ? String(model.subtitle) : ""
                            badge: model ? String(model.releaseBadge) : ""
                            hiRes: false
                        }
                    }

                    onItemActivated: index => {
                        const album = AlbumCtl.moreBy.get(index)
                        if (album)
                            Actions.openAlbum(String(album.itemId), String(album.title))
                    }
                    onItemPlayRequested: index => {
                        const album = AlbumCtl.moreBy.get(index)
                        if (album)
                            MusicPlay.playAlbum(String(album.itemId), String(album.title), 0)
                    }
                    onMenuRequested: (index, mx, my) =>
                        albumMenu.popupForItem(AlbumCtl.moreBy.get(index), mx, my)
                }
            }
        }

        LoadingState {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: selectionBar.bottom
            anchors.bottom: parent.bottom
            visible: AlbumCtl.loading && !page.hasTracks
            shape: "list"
            margins: 0
        }

        EmptyState {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: selectionBar.bottom
            anchors.bottom: parent.bottom
            visible: !AlbumCtl.loading && !page.hasTracks
            iconName: "lib-music"
            severity: AlbumCtl.error.length > 0 ? "error" : "info"
            headline: page.albumId.length === 0 ? qsTr("No album open")
                    : AlbumCtl.error.length > 0 ? qsTr("Couldn't load this album")
                    : qsTr("No tracks came back")
            body: AlbumCtl.error.length > 0 ? AlbumCtl.error
                : page.albumId.length === 0 ? qsTr("Open an album from your music library.")
                : qsTr("The server returned no tracks for this album.")
            actionText: page.albumId.length > 0 ? qsTr("Try again") : ""
            actionIcon: page.albumId.length > 0 ? "refresh" : ""
            onActionTriggered: AlbumCtl.retry()
        }
    }

    // ── Menus and overlays ─────────────────────────────────────────────────
    // For a track the album page is its details page, so the menu leaves
    // Details out.
    ItemMenu {
        id: trackMenu

        allowAddToPlaylist: true
        onAddToPlaylistRequested: item => {
            const id = (item && item.itemId !== undefined) ? String(item.itemId) : ""
            const name = (item && item.name !== undefined) ? String(item.name) : ""
            if (id.length > 0)
                playlistPicker.show(name, [id])
        }
    }

    ItemMenu {
        id: albumMenu
    }

    PlaylistPicker {
        id: playlistPicker

        z: 800
        mediaType: "Audio"
        onDismissed: playlistButton.forceActiveFocus(Qt.OtherFocusReason)
    }

    Connections {
        target: PlaylistCtl

        function onActionSucceeded(message) {
            if (!playlistPicker.pending)
                return
            playlistPicker.pending = false
            albumToasts.show(message, "success")
        }
        function onActionFailed(message) {
            if (!playlistPicker.pending)
                return
            playlistPicker.pending = false
            albumToasts.show(message, "error")
        }
    }

    StrmToastHost {
        id: albumToasts

        anchors.fill: parent
        z: 900
    }
}
```

Notes for the implementer:
- `page.StackView.view === null` keeps the shortcuts live for a page that isn't inside a StackView (the self-test harness, an embedded preview).
- `LoadingState.margins` and `EmptyState.severity: "error"` both exist (`src/ui/shell/`).
- If Phase 2's `CrateSleeve` has no `hiRes` or `badge` property, the contract table in the index is wrong, not this page. Check `src/ui/music/CrateSleeve.qml` and remove only the assignment it lacks.
- `albumMenu.popupForItem` offers Details for an album. That is today's behaviour for album cards in the library, and it routes back to this page through `Actions`.

- [ ] **Step 2: List the page**

In `src/CMakeLists.txt`, in the QML page list, after `ui/pages/AlbumPage.qml`:

```cmake
        ui/pages/MusicAlbumPage.qml
```

- [ ] **Step 3: Build and lint**

Run:

```bash
cmake --preset dev && cmake --build --preset dev
bash scripts/check-qmllint-baseline.sh build/dev
```

Expected:
- The build succeeds.
- The only new lint lines are `[unqualified]` reads of `AlbumCtl`, `Actions`, `MusicPlay`, `PlayerCtl`, `PlaylistCtl` and `App` in `MusicAlbumPage.qml`. Record them for the phase gate.
- Any `missing-property` or `is not a type` line is a real error. It usually means a Phase 2 control's property is named differently, so fix the page.

The page is not routed until Task 9, so the self-test does not load it yet.

- [ ] **Step 4: Commit**

```bash
git add src/ui/pages/MusicAlbumPage.qml src/CMakeLists.txt
git commit -m "$(cat <<'MSG'
feat(music): Crate album page, the back of a sleeve

Sleeve and liner notes on the left, kicker, badges and verbs over the
side-headed track table on the right, More-by as the table's footer. All
display comes from AlbumCtl; play verbs label the queue.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
)"
```

---

### Task 7: `MusicArtistPage`

**Files:**
- Create: `src/ui/pages/MusicArtistPage.qml`
- Modify: `src/CMakeLists.txt` (QML page list)

**Interfaces:**
- Consumes:
  - `ArtistCtl` (Tasks 2–3)
  - Task 5's `CrateTrackTable`
  - Phase 2's `CrateSleeve`, `CratePortrait`, `CrateKicker`, `CrateHeading`, and `StrmGrid.cardComponent`/`customCardWidth`/`customCardHeight`
  - `StrmTabBar` (`tabs` of `{text, badge}`, `currentIndex`, `tabSelected`)
  - `MusicPlay.playAlbum`; `Actions.openAlbum`, `openArtist`, `toggleFavorite`
  - `ItemMenu`, `PlaylistPicker`, `LoadingState`, `EmptyState`, `CoverWash`, `MappedShortcut`, `StrmImage`
- Produces `MusicArtistPage`:
  - `property var artistItem`
  - `readonly property string artistId`
  - `readonly property bool hasTopTracks`
  - `function cycleTab(step): bool`

The design is spec §6.2: a poster and the filed releases.
- The hero is the artist's backdrop, or their colour when there is none, with the name set large.
- The main column holds the release tabs (Albums, EPs & Singles, Appears on; empty tabs are hidden) over a sleeve grid.
- The right column holds Most played, five rows captioned by album or "Guest on …", then Similar artists.
- Below `Theme.scale(1100)` of page width, Most played moves above the grid and Similar folds away.

`tst_navigation_history` (Task 9) pins the Most played owner. Between `navigationFocusKey: "artist-top-tracks"` and `model: ArtistCtl.topTracks`, the text must contain `navigationFocusRefillActive: ArtistCtl.loading` and `visible: page.hasTopTracks`, and no `enabled:`. The file must contain `MusicPlay.playAlbum(` and must not contain `MusicCtl`. Keep those properties in that order.

- [ ] **Step 1: Write the page**

`src/ui/pages/MusicArtistPage.qml`:

```qml
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import StrmQt

// MusicArtistPage — a poster and the filed releases (Crate spec §6.2).
//
// ArtistCtl splits the profile before it gets here: one model per tab, and
// only non-empty tabs listed; the top tracks come with their captions. The
// page lays those out and routes verbs: a sleeve opens through Actions,
// a sleeve's play button and the page's verbs go through MusicPlay/ArtistCtl,
// so every queue carries a source label.
FocusScope {
    id: page

    // ── Contract ───────────────────────────────────────────────────────────
    // The route's artist map. Only itemId and name are read; everything shown
    // is ArtistCtl's.
    property var artistItem: ({})

    readonly property string artistId: page.artistItem && page.artistItem.itemId !== undefined
                                       ? String(page.artistItem.itemId) : ArtistCtl.artistId
    readonly property string artistName: page.artistItem && page.artistItem.name !== undefined
                                         ? String(page.artistItem.name) : ArtistCtl.name
    readonly property bool mine: page.artistId.length > 0 && ArtistCtl.artistId === page.artistId
    readonly property bool hasTopTracks: page.mine && ArtistCtl.topTracks.count > 0
    readonly property bool hasReleases: page.mine && ArtistCtl.tabs.length > 0
    readonly property bool narrow: page.width < Theme.scale(1100)
    readonly property bool isActivePage: page.StackView.status === StackView.Active
                                         || page.StackView.view === null
    readonly property int sideWidth: Theme.scale(420)

    // The shown tab, clamped: a profile with fewer tabs than the last one
    // must not index past the end.
    readonly property int tabIndex: Math.max(0, Math.min(tabBar.currentIndex, ArtistCtl.tabs.length - 1))
    readonly property string tabKey: ArtistCtl.tabs.length > 0 ? String(ArtistCtl.tabs[page.tabIndex].key) : ""
    readonly property var tabModel: page.tabKey === "epsAndSingles" ? ArtistCtl.epsAndSingles
                                  : page.tabKey === "appearsOn" ? ArtistCtl.appearsOn
                                  : ArtistCtl.albums

    function ensureOpen(): void {
        if (page.artistId.length > 0 && ArtistCtl.artistId !== page.artistId)
            ArtistCtl.open(page.artistId, page.artistName, "")
    }

    Component.onCompleted: Qt.callLater(page.ensureOpen)
    onVisibleChanged: { if (page.visible) Qt.callLater(page.ensureOpen) }

    // A different artist starts on their first tab. Bound to the id, not
    // to `tabs`: a heart on this artist re-emits artistChanged and must not
    // throw the user back to Albums.
    readonly property string shownArtistId: ArtistCtl.artistId
    onShownArtistIdChanged: tabBar.currentIndex = 0

    // The pad's shoulders walk the release tabs; with one tab there is
    // nothing to walk and the shell's library cycle keeps them.
    function cycleTab(step): bool {
        const count = ArtistCtl.tabs.length
        if (count < 2)
            return false
        tabBar.currentIndex = (page.tabIndex + step + count) % count
        return true
    }

    function albumAt(index) {
        const model = page.tabModel
        return model && index >= 0 && index < model.count ? model.get(index) : null
    }

    // ── The music input context (spec §8) ──────────────────────────────────
    MappedShortcut {
        actionId: "music.playPause"
        fallback: ["Space"]
        active: App.interactionContext === "music" && page.isActivePage && PlayerCtl.active
        onActivated: PlayerCtl.togglePause()
    }

    MappedShortcut {
        actionId: "music.shuffleAll"
        fallback: ["S"]
        active: App.interactionContext === "music" && page.isActivePage && page.mine
        onActivated: ArtistCtl.shuffle()
    }

    MappedShortcut {
        actionId: "music.favorite"
        fallback: ["L"]
        active: App.interactionContext === "music" && page.isActivePage && page.mine
        onActivated: {
            if (topTracks.activeFocus) {
                const track = topTracks.rowAt(topTracks.currentIndex)
                if (track) {
                    Actions.toggleFavorite(track)
                    return
                }
            }
            Actions.toggleFavorite(ArtistCtl.artistItem)
        }
    }

    MappedShortcut {
        actionId: "music.instantMix"
        fallback: ["R"]
        active: App.interactionContext === "music" && page.isActivePage && page.mine
        onActivated: ArtistCtl.radio()
    }

    // ── Hero ───────────────────────────────────────────────────────────────
    Item {
        id: hero

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: page.narrow ? Theme.scale(240) : Theme.scale(320)
        clip: true

        // The artist's colour, always: it is what shows while the backdrop
        // loads, and all there is when the server has no backdrop.
        CoverWash {
            anchors.fill: parent
            source: ArtistCtl.coverUrl
        }

        StrmImage {
            id: backdrop

            anchors.fill: parent
            visible: Prefs.backdropEnabled && ArtistCtl.backdropUrl.length > 0
            opacity: Prefs.backdropEnabled ? Prefs.backdropOpacity / 100 : 0
            source: visible ? ArtistCtl.backdropUrl : ""
            sourceSize.width: Theme.scale(1600)
        }

        // The scrim that lets the name read over any photograph, ending in
        // the page ground so the hero has no hard lower edge.
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 0.35; color: Qt.rgba(Theme.ground.r, Theme.ground.g, Theme.ground.b, 0.35) }
                GradientStop { position: 1.0; color: Theme.ground }
            }
        }

        Row {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: Theme.pageMarginValue
            anchors.rightMargin: Theme.pageMarginValue
            anchors.bottomMargin: Theme.spacingLoose
            spacing: Theme.spacingLoose

            // The portrait only when a backdrop took the hero; without one the
            // wash is already the artist's picture, and a second copy is noise.
            CratePortrait {
                id: portrait

                anchors.bottom: parent.bottom
                visible: backdrop.visible && ArtistCtl.coverUrl.length > 0 && !page.narrow
                imageUrl: ArtistCtl.coverUrl
                name: ""
                subtitle: ""
                size: Theme.cratePortraitSize
            }

            Column {
                anchors.bottom: parent.bottom
                width: parent.width - (portrait.visible ? portrait.width + parent.spacing : 0)
                spacing: Theme.spacingTight

                CrateKicker {
                    text: ArtistCtl.kicker
                    visible: text.length > 0
                }

                CrateHeading {
                    width: parent.width
                    text: ArtistCtl.name.length > 0 ? ArtistCtl.name : page.artistName
                    pixelSize: Theme.crateHeroArtist
                }

                Flow {
                    width: parent.width
                    spacing: Theme.spacingTight

                    StrmButton {
                        id: shuffleButton

                        text: qsTr("Shuffle artist")
                        iconName: "shuffle"
                        variant: "primary"
                        enabled: page.mine
                        onClicked: ArtistCtl.shuffle()

                        KeyNavigation.right: radioButton
                        KeyNavigation.down: page.narrow && page.hasTopTracks ? topTracks : tabBar
                    }

                    StrmButton {
                        id: radioButton

                        text: qsTr("Artist radio")
                        iconName: "audio-track"
                        enabled: page.mine
                        onClicked: ArtistCtl.radio()

                        KeyNavigation.left: shuffleButton
                        KeyNavigation.right: favouriteButton
                        KeyNavigation.down: page.narrow && page.hasTopTracks ? topTracks : tabBar
                    }

                    StrmIconButton {
                        id: favouriteButton

                        iconName: ArtistCtl.favourite ? "heart-filled" : "heart"
                        checked: ArtistCtl.favourite
                        enabled: page.mine
                        tooltip: ArtistCtl.favourite ? qsTr("Remove from favourites")
                                                     : qsTr("Add to favourites")
                        shortcut: "L"
                        onClicked: Actions.toggleFavorite(ArtistCtl.artistItem)

                        KeyNavigation.left: radioButton
                        KeyNavigation.down: page.narrow && page.hasTopTracks ? topTracks : tabBar
                    }
                }
            }
        }
    }

    // ── Side column: Most played, Similar artists ──────────────────────────
    // Wide: a scrolling column at the right. Narrow: Most played alone, above
    // the releases.
    Flickable {
        id: side

        x: page.narrow ? Theme.pageMarginValue : page.width - Theme.pageMarginValue - page.sideWidth
        y: hero.y + hero.height + Theme.spacingValue
        width: page.narrow ? page.width - Theme.pageMarginValue * 2 : page.sideWidth
        height: page.narrow ? (page.hasTopTracks ? sideColumn.implicitHeight : 0)
                            : page.height - side.y
        visible: page.mine && (page.hasTopTracks || (!page.narrow && ArtistCtl.similar.count > 0))
        contentHeight: sideColumn.implicitHeight + Theme.spacingLoose
        interactive: !page.narrow
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: sideColumn

            width: side.width
            spacing: Theme.spacingValue

            CrateHeading {
                width: parent.width
                visible: page.hasTopTracks
                text: qsTr("Most played")
            }

            CrateTrackTable {
                id: topTracks

                navigationFocusKey: "artist-top-tracks"
                navigationFocusFallbackItem: shuffleButton
                navigationFocusRefillActive: ArtistCtl.loading
                visible: page.hasTopTracks
                model: ArtistCtl.topTracks

                width: parent.width
                height: page.hasTopTracks ? topTracks.count * topTracks.rowHeight : 0
                interactive: false
                showCovers: true
                numberFromIndex: true
                captions: ArtistCtl.topTrackCaptions
                verbsColumn: Theme.scale(64)
                durationColumn: Theme.scale(56)

                KeyNavigation.up: shuffleButton
                KeyNavigation.down: page.narrow ? tabBar : null
                KeyNavigation.left: page.narrow ? null : releases

                onActivated: index => ArtistCtl.playTopTrack(index)
                onMenuRequested: (index, mx, my) =>
                    trackMenu.popupForItemNoDetails(topTracks.rowAt(index), mx, my)
            }

            Item {
                width: 1
                height: Theme.spacingValue
                visible: !page.narrow && ArtistCtl.similar.count > 0
            }

            CrateHeading {
                width: parent.width
                visible: !page.narrow && ArtistCtl.similar.count > 0
                text: qsTr("Similar artists")
            }

            Flow {
                width: parent.width
                visible: !page.narrow && ArtistCtl.similar.count > 0
                spacing: Theme.spacingValue

                Repeater {
                    model: page.narrow ? null : ArtistCtl.similar

                    delegate: CratePortrait {
                        id: similarArtist

                        // `model`, not one required property per role:
                        // CratePortrait already declares `name` and
                        // `subtitle`, and re-declaring them would shadow the
                        // control's own properties.
                        required property var model

                        imageUrl: String(similarArtist.model.coverUrl)
                        name: String(similarArtist.model.name)
                        subtitle: String(similarArtist.model.subtitle)
                        size: Theme.scale(120)
                        onActivated: Actions.openArtist(String(similarArtist.model.itemId),
                                                        String(similarArtist.model.name))
                    }
                }
            }
        }
    }

    // ── Main column: release tabs over sleeves ─────────────────────────────
    Item {
        id: releasesColumn

        x: Theme.pageMarginValue
        y: page.narrow && side.visible ? side.y + side.height + Theme.spacingLoose
                                       : hero.y + hero.height + Theme.spacingValue
        width: page.narrow || !side.visible
               ? page.width - Theme.pageMarginValue * 2
               : side.x - Theme.pageMarginValue - Theme.spacingLoose * 2
        height: page.height - releasesColumn.y
        visible: page.hasReleases

        StrmTabBar {
            id: tabBar

            anchors.left: parent.left
            anchors.top: parent.top

            tabs: ArtistCtl.tabs.map(tab => ({ text: String(tab.label), badge: Number(tab.count) }))

            KeyNavigation.up: page.narrow && page.hasTopTracks ? topTracks : shuffleButton
            KeyNavigation.down: releases
            KeyNavigation.right: page.narrow ? null : topTracks
        }

        StrmGrid {
            id: releases

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: tabBar.bottom
            anchors.bottom: parent.bottom
            anchors.topMargin: Theme.spacingValue

            navigationFocusKey: "artist-" + page.tabKey
            navigationFocusFallbackItem: tabBar
            navigationFocusRefillActive: ArtistCtl.loading
            gridModel: page.tabModel
            emptyText: ""
            customCardWidth: Theme.crateSleeveSize
            customCardHeight: Theme.crateSleeveSize + Theme.scale(52)
            focus: page.hasReleases

            KeyNavigation.up: tabBar
            KeyNavigation.right: page.narrow ? null : topTracks

            cardComponent: Component {
                CrateSleeve {
                    property var model
                    property int index: -1

                    size: Theme.crateSleeveSize
                    showCaption: true
                    coverUrl: model ? String(model.coverUrl) : ""
                    title: model ? String(model.title) : ""
                    // On an artist's own page the artist is known; the year
                    // (and the release badge) is what tells two sleeves apart.
                    subtitle: model && Number(model.year) > 0 ? String(model.year) : ""
                    badge: model ? String(model.releaseBadge) : ""
                    hiRes: false
                }
            }

            onItemActivated: index => {
                const album = page.albumAt(index)
                if (album)
                    Actions.openAlbum(String(album.itemId), String(album.title))
            }
            onItemPlayRequested: index => {
                const album = page.albumAt(index)
                if (album)
                    MusicPlay.playAlbum(String(album.itemId), String(album.title), 0)
            }
            onMenuRequested: (index, mx, my) => albumMenu.popupForItem(page.albumAt(index), mx, my)
        }
    }

    // ── States ─────────────────────────────────────────────────────────────
    LoadingState {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: hero.bottom
        anchors.bottom: parent.bottom
        visible: ArtistCtl.loading && !page.hasReleases && !page.hasTopTracks
        shape: "rails"
    }

    EmptyState {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: hero.bottom
        anchors.bottom: parent.bottom
        visible: !ArtistCtl.loading && !page.hasReleases && !page.hasTopTracks
        iconName: "user"
        severity: ArtistCtl.error.length > 0 ? "error" : "info"
        headline: page.artistId.length === 0 ? qsTr("No artist open")
                : ArtistCtl.error.length > 0 ? qsTr("Couldn't load this artist")
                : qsTr("Nothing filed under this artist")
        body: ArtistCtl.error.length > 0 ? ArtistCtl.error
            : page.artistId.length === 0 ? qsTr("Open an artist from your music library.")
            : qsTr("The server lists no releases or tracks for them.")
        actionText: page.artistId.length > 0 ? qsTr("Try again") : ""
        actionIcon: page.artistId.length > 0 ? "refresh" : ""
        onActionTriggered: ArtistCtl.retry()
    }

    // ── Menus and overlays ─────────────────────────────────────────────────
    ItemMenu {
        id: albumMenu
    }

    ItemMenu {
        id: trackMenu

        allowAddToPlaylist: true
        onAddToPlaylistRequested: item => {
            const id = (item && item.itemId !== undefined) ? String(item.itemId) : ""
            const name = (item && item.name !== undefined) ? String(item.name) : ""
            if (id.length > 0)
                playlistPicker.show(name, [id])
        }
    }

    PlaylistPicker {
        id: playlistPicker

        z: 800
        mediaType: "Audio"
        onDismissed: topTracks.forceActiveFocus(Qt.OtherFocusReason)
    }

    Connections {
        target: PlaylistCtl

        function onActionSucceeded(message) {
            if (!playlistPicker.pending)
                return
            playlistPicker.pending = false
            artistToasts.show(message, "success")
        }
        function onActionFailed(message) {
            if (!playlistPicker.pending)
                return
            playlistPicker.pending = false
            artistToasts.show(message, "error")
        }
    }

    StrmToastHost {
        id: artistToasts

        anchors.fill: parent
        z: 900
    }
}
```

Notes for the implementer:
- The similar-artist delegate reads `ArtistGridModel` roles (`itemId`, `name`, `coverUrl`, `subtitle`, Phase 1) through `model`. The hero portrait passes an empty `name`, so it draws no caption.
- `releases.navigationFocusKey` changes with the tab, so each tab keeps its own focus memory across Back.
- `QtQuick.Effects` is imported for parity with the page this replaces. If qmllint reports it unused, remove the import.

- [ ] **Step 2: List the page**

In `src/CMakeLists.txt`, in the QML page list, after `ui/pages/ArtistPage.qml`:

```cmake
        ui/pages/MusicArtistPage.qml
```

- [ ] **Step 3: Build and lint**

Run:

```bash
cmake --preset dev && cmake --build --preset dev
bash scripts/check-qmllint-baseline.sh build/dev
```

Expected:
- The build succeeds.
- The only new lint lines are `[unqualified]` reads of `ArtistCtl`, `Actions`, `MusicPlay`, `PlayerCtl`, `PlaylistCtl`, `Prefs` and `App`. Record them for the phase gate.
- A `missing-property` line on a Crate control means its property is named differently from the index's contract table. Fix the page to match the control.

- [ ] **Step 4: Commit**

```bash
git add src/ui/pages/MusicArtistPage.qml src/CMakeLists.txt
git commit -m "$(cat <<'MSG'
feat(music): Crate artist page, a poster and the filed releases

Backdrop hero with shuffle, radio and favourite; release tabs over a sleeve
grid; Most played with album or guest captions and Similar artists at the
side. All display comes from ArtistCtl.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
)"
```

---

### Task 8: `MusicPlaylistPage`

**Files:**
- Create: `src/ui/pages/MusicPlaylistPage.qml`
- Modify: `src/CMakeLists.txt` (QML page list)

**Interfaces:**
- Consumes:
  - `PlaylistCtl`: `currentId`, `currentName`, `currentSummary`, `currentCovers`, `items`, `loading`, `errorMessage`, `open`, `reload`, `requestPlayFrom`, `requestShuffle`, `requestQueueSelection`, `selectedItemIds`, `removeRow`, `removeItem`, `removeSelection`, `moveRow`, `itemAt`, `rename`, `remove`, and the signals `playItemsRequested`, `queueItemsRequested`, `moveFocusRequested`, `currentRemoved`, `actionSucceeded`, `actionFailed`, `actionWarning`
  - Phase 2's `CoverCollage`, `CrateKicker`, `CrateHeading`
  - `TrackTable`/`TrackRow`, `SelectionBar`, `ItemMenu`, `StrmPanel`, `StrmSearchField`, `StrmToastHost`, `LoadingState`, `EmptyState`, `MappedShortcut`, `CoverWash`
- Produces `MusicPlaylistPage`:
  - `property string playlistId`, `property string playlistName`
  - `signal backRequested()`
  - `function cycleTab(step): bool`

The design is spec §6.3.
- The left column holds a collage of up to four album covers and the summary ("31 tracks · 2 h 4 min").
- The right column holds the Crate header, the verbs and the member table with the edit verbs `PlaylistPage` already has: reorder, remove, rename and delete.
- The member table stays a plain `TrackTable`, not `CrateTrackTable`, because its rows are `MediaItemModel` rows with edit verbs inline.
- Every playlist behaviour is `PlaylistPage`'s, moved as-is. The behaviour worth keeping is the `StackView.Active` guards on the controller's play and queue intents, the Alt+Up/Down and Delete handling, and focus following a moved entry.

- [ ] **Step 1: Write the page**

`src/ui/pages/MusicPlaylistPage.qml`:

```qml
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import StrmQt

// MusicPlaylistPage — an audio playlist as a Crate record (spec §6.3).
//
// The collage and the summary are PlaylistCtl's, derived from the members in
// C++. The member table, its edit verbs and every guard on them are
// PlaylistPage's, moved as-is: the controller owns order and identity, and
// this page only asks.
//
// Video playlists keep PlaylistPage. Main routes here only from the music
// context.
FocusScope {
    id: page

    // ── Contract ───────────────────────────────────────────────────────────
    property string playlistId: ""
    property string playlistName: ""

    // The open playlist was deleted; the shell goes back.
    signal backRequested()

    readonly property int memberCount: PlaylistCtl.items.count
    readonly property bool failed: PlaylistCtl.errorMessage.length > 0
    readonly property bool narrow: page.width < Theme.scale(1000)
    readonly property bool isActivePage: page.StackView.status === StackView.Active
                                         || page.StackView.view === null
    readonly property int collageSize: page.narrow ? Theme.scale(160) : Theme.scale(340)

    function ensureOpen(): void {
        if (page.playlistId.length > 0 && PlaylistCtl.currentId !== page.playlistId)
            PlaylistCtl.open(page.playlistId, page.playlistName)
    }

    Component.onCompleted: Qt.callLater(page.ensureOpen)
    // PlaylistCtl is shared with PlaylistPage and with any other playlist
    // route in history: take it back when this page shows again.
    onVisibleChanged: { if (page.visible) Qt.callLater(page.ensureOpen) }

    function cycleTab(step): bool {
        const stops = [playButton, memberList]
        const at = memberList.activeFocus ? 1 : 0
        stops[(at + step + stops.length) % stops.length].forceActiveFocus(Qt.TabFocusReason)
        return true
    }

    function showMemberMenu(row, sceneX, sceneY): void {
        const item = PlaylistCtl.itemAt(row)
        if (!item)
            return
        memberList.currentIndex = row
        itemMenu.popupForItem(item, sceneX, sceneY)
    }

    function formatDuration(ms): string {
        return NowPlayingInfo.formatDuration(ms, "")
    }

    // ── Controller intents ─────────────────────────────────────────────────
    Connections {
        target: PlaylistCtl

        function onActionSucceeded(message) {
            if (page.isActivePage)
                toasts.show(message, "success")
        }
        function onActionFailed(message) {
            if (page.isActivePage)
                toasts.show(message, "error")
        }
        function onActionWarning(message) {
            if (page.isActivePage)
                toasts.show(message, "warning")
        }
        // StackView may retain a covered playlist page. Only the page whose
        // gesture raised the intent forwards it, or the queue would be built
        // twice.
        function onPlayItemsRequested(items, startIndex) {
            if (page.StackView.status === StackView.Active)
                Actions.playAllFrom(items, startIndex)
        }
        function onQueueItemsRequested(items) {
            if (page.StackView.status === StackView.Active)
                Actions.addAllToQueue(items)
        }
        function onMoveFocusRequested(row) {
            if (page.StackView.status !== StackView.Active)
                return
            Qt.callLater(() => {
                if (page.StackView.status !== StackView.Active)
                    return
                memberList.currentIndex = row
                memberList.positionViewAtIndex(row, ListView.Contain)
            })
        }
        function onCurrentRemoved() {
            if (page.StackView.status === StackView.Active)
                page.backRequested()
        }
    }

    // ── The music input context (spec §8) ──────────────────────────────────
    MappedShortcut {
        actionId: "music.playPause"
        fallback: ["Space"]
        active: App.interactionContext === "music" && page.isActivePage && PlayerCtl.active
        onActivated: PlayerCtl.togglePause()
    }

    MappedShortcut {
        actionId: "music.shuffleAll"
        fallback: ["S"]
        active: App.interactionContext === "music" && page.isActivePage && page.memberCount > 0
        onActivated: PlaylistCtl.requestShuffle()
    }

    MappedShortcut {
        actionId: "music.favorite"
        fallback: ["L"]
        active: App.interactionContext === "music" && page.isActivePage && page.memberCount > 0
        onActivated: {
            if (memberList.selectionCount > 0) {
                Actions.setFavoriteAll(PlaylistCtl.selectedItemIds(memberList.selectedRows), true)
                return
            }
            const item = PlaylistCtl.itemAt(memberList.currentIndex)
            if (item)
                Actions.toggleFavorite(item)
        }
    }

    MappedShortcut {
        actionId: "music.instantMix"
        fallback: ["R"]
        active: App.interactionContext === "music" && page.isActivePage && page.memberCount > 0
        onActivated: {
            const item = PlaylistCtl.itemAt(Math.max(0, memberList.currentIndex))
            if (item && item.itemId !== undefined)
                MusicPlay.radio(String(item.itemId), String(item.name))
        }
    }

    // ── Atmosphere ─────────────────────────────────────────────────────────
    CoverWash {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Math.round(page.height * 0.5)
        z: -1
        source: PlaylistCtl.currentCovers.length > 0 ? PlaylistCtl.currentCovers[0] : ""
    }

    // ── Left column: collage and summary ───────────────────────────────────
    Column {
        id: collageColumn

        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: Theme.pageMarginValue
        anchors.topMargin: Theme.spacingLoose
        width: page.narrow ? 0 : page.collageSize
        visible: !page.narrow
        spacing: Theme.spacingValue

        CoverCollage {
            covers: PlaylistCtl.currentCovers
            size: page.collageSize
            radius: Theme.radiusCardValue
        }

        Text {
            width: parent.width
            visible: text.length > 0
            text: PlaylistCtl.currentSummary
            color: Theme.textSecondaryColor
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontSmall
        }
    }

    // ── Right column ───────────────────────────────────────────────────────
    Item {
        id: mainColumn

        anchors.left: page.narrow ? parent.left : collageColumn.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.leftMargin: page.narrow ? Theme.pageMarginValue : Theme.spacingLoose * 2
        anchors.rightMargin: Theme.pageMarginValue
        anchors.topMargin: Theme.spacingLoose

        Row {
            id: header

            anchors.left: parent.left
            anchors.right: parent.right
            spacing: Theme.spacingLoose

            CoverCollage {
                id: narrowCollage

                visible: page.narrow
                covers: PlaylistCtl.currentCovers
                size: page.collageSize
                radius: Theme.radiusCardValue
            }

            Column {
                width: header.width - (narrowCollage.visible ? narrowCollage.width + header.spacing : 0)
                spacing: Theme.spacingTight

                CrateKicker {
                    text: page.narrow && PlaylistCtl.currentSummary.length > 0
                          ? qsTr("Playlist · %1").arg(PlaylistCtl.currentSummary)
                          : qsTr("Playlist")
                }

                CrateHeading {
                    width: parent.width
                    text: PlaylistCtl.currentName.length > 0 ? PlaylistCtl.currentName : page.playlistName
                    pixelSize: Theme.crateHeroAlbum
                }

                Item {
                    width: 1
                    height: Theme.spacingTight
                }

                Flow {
                    width: parent.width
                    spacing: Theme.spacingTight

                    StrmButton {
                        id: playButton

                        text: qsTr("Play")
                        iconName: "play"
                        variant: "primary"
                        enabled: page.memberCount > 0
                        onClicked: PlaylistCtl.requestPlayFrom(0)

                        KeyNavigation.right: shuffleButton
                        KeyNavigation.down: memberList
                    }

                    StrmButton {
                        id: shuffleButton

                        text: qsTr("Shuffle")
                        iconName: "shuffle"
                        enabled: page.memberCount > 0
                        onClicked: PlaylistCtl.requestShuffle()

                        KeyNavigation.left: playButton
                        KeyNavigation.right: renameButton
                        KeyNavigation.down: memberList
                    }

                    StrmIconButton {
                        id: renameButton

                        iconName: "edit"
                        tooltip: qsTr("Rename this playlist")
                        enabled: PlaylistCtl.currentId.length > 0
                        onClicked: {
                            renameSheet.seed = PlaylistCtl.currentName
                            renameSheet.open()
                        }

                        KeyNavigation.left: shuffleButton
                        KeyNavigation.right: reloadButton
                        KeyNavigation.down: memberList
                    }

                    StrmIconButton {
                        id: reloadButton

                        iconName: "refresh"
                        tooltip: qsTr("Reload from the server")
                        enabled: PlaylistCtl.currentId.length > 0
                        onClicked: PlaylistCtl.reload()

                        KeyNavigation.left: renameButton
                        KeyNavigation.right: deleteButton
                        KeyNavigation.down: memberList
                    }

                    StrmIconButton {
                        id: deleteButton

                        iconName: "trash"
                        tooltip: qsTr("Delete this playlist")
                        enabled: PlaylistCtl.currentId.length > 0
                        onClicked: confirmDelete.open()

                        KeyNavigation.left: reloadButton
                        KeyNavigation.down: memberList
                    }
                }
            }
        }

        SelectionBar {
            id: memberSelection

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: header.bottom
            anchors.topMargin: Theme.spacingLoose

            count: memberList.selectionCount
            allowPlaylist: false
            allowRemove: true

            onQueueRequested: PlaylistCtl.requestQueueSelection(memberList.selectedRows)
            onFavoriteRequested: Actions.setFavoriteAll(
                                     PlaylistCtl.selectedItemIds(memberList.selectedRows), true)
            onRemoveRequested: PlaylistCtl.removeSelection(memberList.selectedRows)
            onClearRequested: {
                memberList.clearSelection()
                memberList.forceActiveFocus(Qt.OtherFocusReason)
            }
        }

        TrackTable {
            id: memberList

            navigationFocusKey: "music-playlist-members"
            navigationFocusFallbackItem: playButton
            navigationFocusRefillActive: PlaylistCtl.loading

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: memberSelection.bottom
            anchors.bottom: parent.bottom
            anchors.topMargin: Theme.spacingTight
            clip: true
            focus: page.memberCount > 0
            model: PlaylistCtl.items
            rowHeight: Theme.scale(56)
            multiSelect: true
            jumpRole: "name"
            // Opacity, not visibility: an invisible view drops active focus
            // and a reload would eject the keyboard on every move.
            opacity: PlaylistCtl.loading ? 0.0 : 1.0

            Behavior on opacity {
                NumberAnimation { duration: Theme.animFastMs; easing.type: Theme.easeStandard }
            }

            KeyNavigation.up: playButton

            onActivated: index => PlaylistCtl.requestPlayFrom(index)

            onKeyPressed: event => {
                if (memberList.count === 0)
                    return
                const row = memberList.currentIndex
                if (event.modifiers & Qt.AltModifier) {
                    if (event.key === Qt.Key_Up && !event.isAutoRepeat) {
                        PlaylistCtl.moveRow(row, -1)
                        event.accepted = true
                    } else if (event.key === Qt.Key_Down && !event.isAutoRepeat) {
                        PlaylistCtl.moveRow(row, 1)
                        event.accepted = true
                    }
                    return
                }
                if (event.key === Qt.Key_Delete && !event.isAutoRepeat) {
                    if (memberList.selectionCount > 0)
                        PlaylistCtl.removeSelection(memberList.selectedRows)
                    else
                        PlaylistCtl.removeRow(row)
                    event.accepted = true
                } else if (event.key === Qt.Key_Menu && !event.isAutoRepeat) {
                    const item = memberList.currentItem
                    if (item) {
                        const p = item.mapToItem(null, Theme.spacingValue, item.height)
                        page.showMemberMenu(row, p.x, p.y)
                    }
                    event.accepted = true
                }
            }

            onMenuRequested: (index, mx, my) => page.showMemberMenu(index, mx, my)

            delegate: TrackRow {
                id: memberRow

                required property int index
                required property var model

                width: memberList.width
                navigationFocusOwner: memberList

                rowHeight: Theme.scale(56)
                surfaceTopMargin: Theme.scale(2)
                surfaceBottomMargin: Theme.scale(2)
                numberColumn: Theme.scale(42)
                verbsColumn: Theme.scale(176)
                number: memberRow.index + 1
                hoverPlayGlyph: false
                showCover: true
                coverSize: Theme.scale(40)

                coverUrl: memberRow.model.posterUrl !== undefined ? String(memberRow.model.posterUrl) : ""
                title: memberRow.model.name !== undefined ? String(memberRow.model.name) : ""
                secondary: memberRow.model.subtitle !== undefined ? String(memberRow.model.subtitle) : ""
                durationText: page.formatDuration(memberRow.model.runtimeMs)
                favorite: memberRow.model.favorite === true

                current: memberRow.ListView.isCurrentItem && memberList.activeFocus
                selected: memberList.isSelected(memberRow.index)
                playing: false
                showFavorite: true
                showMenu: true
                verbsRevealed: memberRow.hovered || memberRow.current || memberRow.favorite

                onActivated: modifiers => {
                    memberList.forceActiveFocus(Qt.MouseFocusReason)
                    memberList.activateAt(memberRow.index, modifiers)
                }
                onFavoriteToggled: {
                    const item = PlaylistCtl.itemAt(memberRow.index)
                    if (item)
                        Actions.toggleFavorite(item)
                }
                onMenuRequested: (sceneX, sceneY) => page.showMemberMenu(memberRow.index, sceneX, sceneY)

                StrmIconButton {
                    iconName: "chevron-up"
                    round: true
                    size: Theme.scale(28)
                    activeFocusOnTab: false
                    enabled: memberRow.index > 0
                    tooltip: qsTr("Move up")
                    shortcut: "Alt+Up"
                    onClicked: {
                        memberList.cancelNavigationFocusRestore()
                        PlaylistCtl.moveRow(memberRow.index, -1)
                    }
                }

                StrmIconButton {
                    iconName: "chevron-down"
                    round: true
                    size: Theme.scale(28)
                    activeFocusOnTab: false
                    enabled: memberRow.index < page.memberCount - 1
                    tooltip: qsTr("Move down")
                    shortcut: "Alt+Down"
                    onClicked: {
                        memberList.cancelNavigationFocusRestore()
                        PlaylistCtl.moveRow(memberRow.index, 1)
                    }
                }

                StrmIconButton {
                    iconName: "trash"
                    round: true
                    size: Theme.scale(28)
                    activeFocusOnTab: false
                    tooltip: qsTr("Remove from this playlist")
                    shortcut: "Del"
                    onClicked: {
                        memberList.cancelNavigationFocusRestore()
                        PlaylistCtl.removeRow(memberRow.index)
                    }
                }
            }
        }

        // Swallows clicks aimed at rows that are being replaced underneath.
        MouseArea {
            anchors.fill: memberList
            visible: PlaylistCtl.loading
            acceptedButtons: Qt.AllButtons
        }

        LoadingState {
            anchors.fill: memberList
            shape: "list"
            active: PlaylistCtl.loading
            margins: 0
        }

        EmptyState {
            anchors.fill: memberList
            visible: !PlaylistCtl.loading && page.memberCount === 0 && !page.failed
            iconName: "playlist"
            headline: qsTr("This playlist is empty")
            body: qsTr("Use “Add to playlist” on any album or track to put it here.")
        }

        EmptyState {
            anchors.fill: memberList
            visible: !PlaylistCtl.loading && page.memberCount === 0 && page.failed
            severity: "error"
            iconName: "info"
            headline: qsTr("Couldn't load this playlist")
            body: PlaylistCtl.errorMessage
            actionText: qsTr("Try again")
            actionIcon: "refresh"
            onActionTriggered: PlaylistCtl.reload()
        }
    }

    // ── Context menu ───────────────────────────────────────────────────────
    ItemMenu {
        id: itemMenu

        allowRemoveFromPlaylist: true
        onRemoveFromPlaylistRequested: item => PlaylistCtl.removeItem(item)
    }

    StrmToastHost {
        id: toasts

        anchors.fill: parent
        z: 200
    }

    // ── Rename / delete ────────────────────────────────────────────────────
    StrmPanel {
        id: renameSheet

        property string seed: ""

        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(Theme.scale(460), page.width - Theme.spacingLoose * 2)
        visible: false
        z: 900
        title: qsTr("Rename playlist")

        function open() {
            visible = true
            renameField.text = renameSheet.seed
            renameField.forceActiveFocus()
        }
        function close() {
            visible = false
            renameButton.forceActiveFocus()
        }
        function commit() {
            const wanted = renameField.text.trim()
            if (wanted.length === 0 || wanted === renameSheet.seed) {
                renameSheet.close()
                return
            }
            PlaylistCtl.rename(PlaylistCtl.currentId, wanted)
            renameSheet.close()
        }

        Column {
            width: parent.width
            spacing: Theme.spacingValue

            StrmSearchField {
                id: renameField

                width: parent.width
                placeholderText: qsTr("Playlist name")
                onAccepted: renameSheet.commit()
                Keys.onEscapePressed: renameSheet.close()
            }

            Row {
                anchors.right: parent.right
                spacing: Theme.spacingTight

                StrmButton {
                    text: qsTr("Cancel")
                    variant: "ghost"
                    onClicked: renameSheet.close()
                }

                StrmButton {
                    text: qsTr("Rename")
                    variant: "primary"
                    enabled: renameField.text.trim().length > 0
                             && renameField.text.trim() !== renameSheet.seed
                    onClicked: renameSheet.commit()
                }
            }
        }
    }

    StrmPanel {
        id: confirmDelete

        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(Theme.scale(440), page.width - Theme.spacingLoose * 2)
        visible: false
        z: 900
        title: qsTr("Delete this playlist?")

        function open() {
            visible = true
            cancelDelete.forceActiveFocus()
        }
        function close() {
            visible = false
            page.forceActiveFocus()
        }

        Column {
            width: parent.width
            spacing: Theme.spacingValue

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("\"%1\" will be removed from the server. The tracks themselves are not deleted.")
                          .arg(PlaylistCtl.currentName)
                color: Theme.textSecondaryColor
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontBodySize
            }

            Row {
                anchors.right: parent.right
                spacing: Theme.spacingTight

                StrmButton {
                    id: cancelDelete

                    text: qsTr("Cancel")
                    variant: "ghost"
                    onClicked: confirmDelete.close()
                    KeyNavigation.right: confirmDeleteButton
                }

                StrmButton {
                    id: confirmDeleteButton

                    text: qsTr("Delete")
                    destructive: true
                    onClicked: {
                        PlaylistCtl.remove(PlaylistCtl.currentId)
                        confirmDelete.close()
                    }
                    KeyNavigation.left: cancelDelete
                }
            }
        }
    }
}
```

Notes for the implementer:
- `NowPlayingInfo.formatDuration(ms, placeholder)` is the helper `AlbumPage` uses today. Confirm it is still a singleton with `grep -rn "NowPlayingInfo" src/ui --include=*.qml | head -3`. If Phase 5 renamed it, use the replacement Phase 5 names.
- `MediaItemModel`'s `subtitle` role for an audio member is the artist and album line that `PlaylistPage` already shows. Nothing is composed here.
- `playing: false` is deliberate: `MediaItemModel` rows carry no queue identity, and a duplicate track in a playlist would light up twice. Don't copy `nowPlayingId` in.

- [ ] **Step 2: List the page**

In `src/CMakeLists.txt`, in the QML page list, after `ui/pages/PlaylistPage.qml`:

```cmake
        ui/pages/MusicPlaylistPage.qml
```

- [ ] **Step 3: Build and lint**

Run:

```bash
cmake --preset dev && cmake --build --preset dev
bash scripts/check-qmllint-baseline.sh build/dev
```

Expected: the build succeeds, and the only new lint lines are `[unqualified]` reads of `PlaylistCtl`, `Actions`, `MusicPlay`, `PlayerCtl` and `App`. `NowPlayingInfo` is a QML singleton and does not lint. Record them for the phase gate.

- [ ] **Step 4: Commit**

```bash
git add src/ui/pages/MusicPlaylistPage.qml src/CMakeLists.txt
git commit -m "$(cat <<'MSG'
feat(music): Crate playlist page for audio playlists

Cover collage and member summary beside a Crate header; the member table
keeps PlaylistPage's reorder, remove, rename and delete verbs and its
active-page guards. Deleting the open playlist asks the shell to go back.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
)"
```

---

### Task 9: Route the album, artist and audio-playlist pages

**Files:**
- Modify: `src/ui/Main.qml`
- Modify: `src/ui/shell/BoundedNavigationStack.qml`
- Test: `tests/unit/tst_navigation_history.cpp`

**Interfaces:**
- Consumes:
  - Tasks 3 and 6–8
  - Phase 3's `root.openMusicGenre(libraryId, name, genreId, genreName)`, plus `MusicBrowseCtl.libraryId` and `MusicHomeCtl.libraryId`
  - `PlaylistCtl.open`/`currentId`
- Produces (the index's route table plus Contract notes 6 and 7):
  - `root.openMusicPlaylist(id, name)`
  - `root.musicLibraryId()`
  - `root.openMusicGenreFromPage(genreId, genreName)`
  - `BoundedNavigationStack.musicPlaylistPageComponent`
  - `componentFor` returns it for `{kind:"playlist", mode:"audio"}`
  - `reconstructedProperties` gives an evicted audio playlist `{playlistId, playlistName}`

Main.qml changes a lot in Phases 2 and 3, so every edit below is anchored on content, not line numbers. Where a step says "if still present", Phase 3 may already have removed that code, and there is nothing to do.

- [ ] **Step 1: Write the failing test**

In `tests/unit/tst_navigation_history.cpp`:

1. Declare the slot after `void preservesBaseAndKeepsTransientPagesOutOfHistory();`:

```cpp
    void audioPlaylistRouteSelectsTheMusicPlaylistPage();
```

2. In `kProbe`, after the `AlbumProbe` component, add two playlist probes:

```qml
    component PlaylistProbe: FocusScope {
        objectName: "playlistPage"
        focus: true
    }

    component MusicPlaylistProbe: FocusScope {
        property string playlistId: ""
        property string playlistName: ""
        objectName: "musicPlaylistPage"
        focus: true
    }
```

3. After `Component { id: albumComponent; AlbumProbe {} }`:

```qml
    Component { id: playlistComponent; PlaylistProbe {} }
    Component { id: musicPlaylistComponent; MusicPlaylistProbe {} }
```

4. In the probe's `BoundedNavigationStack { id: history … }`, after `albumPageComponent: albumComponent`:

```qml
        playlistPageComponent: playlistComponent
        musicPlaylistPageComponent: musicPlaylistComponent
```

5. After the probe's `pushAlbum(id)` function:

```qml
    function pushAudioPlaylist(id): void {
        const text = String(id);
        history.pushRoute({ "kind": "playlist", "mode": "audio", "id": text,
                            "name": "Playlist " + text, "key": "musicPlaylist:" + text,
                            "title": "Playlist " + text },
                          { "playlistId": text, "playlistName": "Playlist " + text });
    }

    function pushVideoPlaylist(id): void {
        const text = String(id);
        history.pushRoute({ "kind": "playlist", "id": text, "name": "Playlist " + text,
                            "key": "playlists", "title": "Playlists" });
    }
```

6. Add the test body before `QTEST_MAIN` (or with the other test definitions):

```cpp
void NavigationHistoryTest::audioPlaylistRouteSelectsTheMusicPlaylistPage()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QQuickView view;
    const auto [root, history] = createHistoryProbe(dir, view);
    QVERIFY(root);
    QVERIFY(history);

    QVERIFY(invoke(root, "pushAudioPlaylist", QStringLiteral("pl-a")));
    QTRY_COMPARE(currentItem(history)->objectName(), QStringLiteral("musicPlaylistPage"));
    QCOMPARE(currentItem(history)->property("playlistId").toString(), QStringLiteral("pl-a"));

    QVERIFY(invoke(root, "pushVideoPlaylist", QStringLiteral("pl-v")));
    QTRY_COMPARE(currentItem(history)->objectName(), QStringLiteral("playlistPage"));

    QVERIFY(invoke(root, "goBack"));
    QTRY_COMPARE(currentItem(history)->objectName(), QStringLiteral("musicPlaylistPage"));
    const QVariantMap entry = history->property("currentEntry").toMap();
    QCOMPARE(entry.value(QStringLiteral("kind")).toString(), QStringLiteral("playlist"));
    QCOMPARE(entry.value(QStringLiteral("mode")).toString(), QStringLiteral("audio"));
    QCOMPARE(currentItem(history)->property("playlistId").toString(), QStringLiteral("pl-a"));
    QVERIFY(root->property("preparedRoutes").toStringList().contains(QStringLiteral("playlist:pl-a")));
}
```

The last `playlistId` check covers both paths back to the page. If the retained page survived, its push properties still hold. If the history limit evicted it, `reconstructedProperties` rebuilt them.

7. In `itemPolicyIsCentralizedAcrossQmlSurfaces`, replace the playlist route assertion

```cpp
    QVERIFY(main.contains("case \"playlist\": root.openPlaylist(id, name)"));
```

with

```cpp
    QVERIFY(main.contains("root.openMusicPlaylist(id, name)"));
    QVERIFY(main.contains("root.openPlaylist(id, name)"));
    QVERIFY(main.contains("AlbumCtl.open(route.id, route.name)"));
    QVERIFY(main.contains("ArtistCtl.open(route.id, route.name, root.musicLibraryId())"));
```

Then replace the album block:

```cpp
    const QByteArray album = sourceFor(QStringLiteral("src/ui/pages/AlbumPage.qml"));
    QVERIFY(album.contains("Actions.artistTarget(page.albumItem)"));
    QVERIFY(album.contains("Actions.openArtist(page.albumArtistId"));
    QVERIFY(!album.contains("\"type\": \"MusicArtist\""));
```

with:

```cpp
    const QByteArray album = sourceFor(QStringLiteral("src/ui/pages/MusicAlbumPage.qml"));
    QVERIFY(!album.isEmpty());
    QVERIFY(album.contains("Actions.openArtist(AlbumCtl.artistId, AlbumCtl.artist)"));
    QVERIFY(!album.contains("\"type\": \"MusicArtist\""));
    QVERIFY(!album.contains("MusicCtl"));
```

Replace the artist lines:

```cpp
    const QByteArray artist = sourceFor(QStringLiteral("src/ui/pages/ArtistPage.qml"));
    QVERIFY(artist.contains("Actions.play(item)"));
    QVERIFY(!artist.contains("MusicCtl.playAlbum("));
```

with:

```cpp
    const QByteArray artist = sourceFor(QStringLiteral("src/ui/pages/MusicArtistPage.qml"));
    QVERIFY(!artist.isEmpty());
    QVERIFY(artist.contains("MusicPlay.playAlbum("));
    QVERIFY(!artist.contains("MusicCtl"));

    const QByteArray musicPlaylist = sourceFor(QStringLiteral("src/ui/pages/MusicPlaylistPage.qml"));
    QVERIFY(!musicPlaylist.isEmpty());
    QVERIFY(musicPlaylist.contains(
        "onRemoveFromPlaylistRequested: item => PlaylistCtl.removeItem(item)"));
```

Leave every line that reads `music` (`MusicPage.qml`) as Phase 3 left it. If Phase 3 deleted those lines, nothing here refers to them.

8. In `searchTrackOwnerRestoresAcrossResultLifecycle`, replace the artist top-tracks block, from `QFile artistPage(` through `QVERIFY(!topTracksOwner.contains("enabled: page.hasTopTracks"));`, with:

```cpp
    QFile artistPage(QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/pages/MusicArtistPage.qml"));
    QVERIFY(artistPage.open(QIODevice::ReadOnly));
    const QByteArray artistSource = artistPage.readAll();
    const qsizetype topTracksBegin =
        artistSource.indexOf("navigationFocusKey: \"artist-top-tracks\"");
    const qsizetype topTracksModel =
        artistSource.indexOf("model: ArtistCtl.topTracks", topTracksBegin);
    QVERIFY(topTracksBegin >= 0);
    QVERIFY(topTracksModel > topTracksBegin);
    const QByteArray topTracksOwner =
        artistSource.mid(topTracksBegin, topTracksModel - topTracksBegin);
    QVERIFY(topTracksOwner.contains("navigationFocusRefillActive: ArtistCtl.loading"));
    QVERIFY(topTracksOwner.contains("visible: page.hasTopTracks"));
    QVERIFY(!topTracksOwner.contains("enabled:"));
```

- [ ] **Step 2: Run the test and watch it fail**

Run: `cmake --build --preset dev --target tst_navigation_history && ctest --preset dev -R tst_navigation_history --output-on-failure`
Expected: FAIL.
- `audioPlaylistRouteSelectsTheMusicPlaylistPage` fails: `Cannot assign to non-existent property "musicPlaylistPageComponent"`, so the probe does not load, and every test in the file fails at `QVERIFY(root)`.
- `itemPolicyIsCentralizedAcrossQmlSurfaces` fails on `root.openMusicPlaylist(id, name)`.

- [ ] **Step 3: `BoundedNavigationStack`**

In `src/ui/shell/BoundedNavigationStack.qml`, after `property Component playlistPageComponent: null`:

```qml
    // Audio playlists open as the Crate page. Chosen by the route's mode, so
    // a retained or reconstructed entry keeps the page it was opened as.
    property Component musicPlaylistPageComponent: null
```

In `componentFor`, replace

```qml
        case "playlist": return navigation.playlistPageComponent;
```

with

```qml
        case "playlist":
            return route.mode === "audio" && navigation.musicPlaylistPageComponent !== null
                   ? navigation.musicPlaylistPageComponent
                   : navigation.playlistPageComponent;
```

In `reconstructedProperties`, before `default: return ({});`:

```qml
        case "playlist": return route.mode === "audio"
                                ? { "playlistId": route.id, "playlistName": route.name }
                                : ({});
```

- [ ] **Step 4: `Main.qml`**

1. **`interactionContext`**: in the `objectName` chain that yields `"music"`, add the playlist page next to `"artistPage"`:

```qml
                                                     || stack.currentItem.objectName === "artistPage"
                                                     || stack.currentItem.objectName === "musicPlaylistPage")
```

   Phase 2/3 extended this chain with `musicHomePage` and `musicBrowsePage`. Keep their lines, and put this one last before the closing parenthesis.

2. **Helpers**: after `function openPlaylist(playlistId, name)`'s closing brace, add:

```qml
    // The Crate playlist page, for an audio playlist opened from music. Its
    // own history key: the generic Playlists destination is a two-pane
    // browser, while this is one record, and Back between the two must not
    // collapse them into one entry.
    function openMusicPlaylist(playlistId, name): void {
        if (!playlistId)
            return;
        const key = "musicPlaylist:" + playlistId;
        if (root.currentKey === key) {
            root.focusCurrentPage();
            return;
        }
        PlaylistCtl.open(playlistId, name);
        root.pushPage({ "kind": "playlist", "mode": "audio", "id": playlistId, "name": name,
                        "key": key, "title": name },
                      { "playlistId": playlistId, "playlistName": name });
    }

    // The music library the user is in, for surfaces that know only an item:
    // Browse's scope when Browse has been opened, else Home's. Empty before
    // either has been visited this session.
    function musicLibraryId(): string {
        if (MusicBrowseCtl.libraryId.length > 0)
            return MusicBrowseCtl.libraryId;
        return MusicHomeCtl.libraryId;
    }

    // A liner-notes genre link. Inside the music library it filters Browse
    // by that genre; with no music library known it falls back to the
    // generic genre destination rather than doing nothing.
    function openMusicGenreFromPage(genreId, genreName): void {
        const libraryId = root.musicLibraryId();
        if (libraryId.length > 0)
            root.openMusicGenre(libraryId, qsTr("Music"), genreId, genreName);
        else
            Actions.browseGenre(genreId, genreName);
    }
```

3. **`openRoute`**: replace

```qml
        case "playlist": root.openPlaylist(id, name); break;
```

with

```qml
        case "playlist":
            // A generic playlist map carries no reliable media type; the
            // surface the request came from does.
            if (root.interactionContext === "music")
                root.openMusicPlaylist(id, name);
            else
                root.openPlaylist(id, name);
            break;
```

4. **`openAlbum` and `openArtist`**: replace `MusicCtl.openAlbum(id, name);` with `AlbumCtl.open(id, name);`, and `MusicCtl.openArtist(id, name);` with `ArtistCtl.open(id, name, root.musicLibraryId());`.

5. **`prepareRoute`**: replace the `case "playlist":` body with:

```qml
        case "playlist":
            if (route.mode !== "audio" && PlaylistCtl.playlists.count === 0 && !PlaylistCtl.loading)
                PlaylistCtl.refresh();
            if (route.id.length > 0 && PlaylistCtl.currentId !== route.id)
                PlaylistCtl.open(route.id, route.name);
            break;
```

   Replace the `case "artist":` and `case "album":` bodies (the ones reading `MusicCtl.detailKind`) with:

```qml
        case "artist":
            if (ArtistCtl.artistId !== route.id)
                ArtistCtl.open(route.id, route.name, root.musicLibraryId());
            break;
        case "album":
            if (AlbumCtl.albumId !== route.id)
                AlbumCtl.open(route.id, route.name);
            break;
```

   If a `case "music":` block that calls `MusicCtl.setLibrary` is still present, Phase 3 did not remove the old route. Stop and report it, because Phase 4 builds on Phase 3's removal.

6. **Stack properties**: after `playlistPageComponent: playlistComponent`, add

```qml
        musicPlaylistPageComponent: musicPlaylistComponent
```

7. **Components**: replace

```qml
    Component {
        id: artistComponent
        ArtistPage { objectName: "artistPage" }
    }

    Component {
        id: albumComponent
        AlbumPage { objectName: "albumPage" }
    }
```

with

```qml
    Component {
        id: artistComponent
        MusicArtistPage { objectName: "artistPage" }
    }

    Component {
        id: albumComponent
        MusicAlbumPage { objectName: "albumPage" }
    }

    Component {
        id: musicPlaylistComponent
        MusicPlaylistPage { objectName: "musicPlaylistPage" }
    }
```

8. **Self-test**: in the self-test `pages` array, after `["album", albumComponent]`, add `["musicPlaylist", musicPlaylistComponent]`, keeping the array's comma layout.

9. **Page signals**: in `Connections { target: stack.currentItem; ignoreUnknownSignals: true … }`, next to `function onBackRequested() { root.goBack(); }`:

```qml
        function onMusicGenreRequested(genreId, genreName) {
            root.openMusicGenreFromPage(genreId, genreName);
        }
```

   `MusicPlaylistPage.backRequested` already reaches `onBackRequested`, which is the same transaction Search uses.

- [ ] **Step 5: Run the tests and the self-test**

Run:

```bash
cmake --build --preset dev && ctest --preset dev -R tst_navigation_history --output-on-failure
STRMQT_SELFTEST=1 QT_QPA_PLATFORM=offscreen QT_ASSUME_STDERR_HAS_CONSOLE=1 ./build/dev/strmqt 2>&1 | grep -E "selftest (ok|FAIL)|selftest:"; echo "exit=${PIPESTATUS[0]}"
```

Expected:
- `tst_navigation_history` passes (every test, including the new one).
- The self-test prints `selftest ok   artist`, `selftest ok   album` and `selftest ok   musicPlaylist`, and no `selftest FAIL` line.
- The run ends with `exit=0`.

If a page fails with `Cannot assign to non-existent property`, the named Crate control's property differs from the index's contract table. Fix the page (Tasks 6–8) to match the control.

- [ ] **Step 6: Commit**

```bash
git add src/ui/Main.qml src/ui/shell/BoundedNavigationStack.qml tests/unit/tst_navigation_history.cpp
git commit -m "$(cat <<'MSG'
feat(music): route albums, artists and audio playlists to Crate pages

Album and artist routes build MusicAlbumPage and MusicArtistPage from
AlbumCtl and ArtistCtl; a playlist opened from music takes the audio mode
and MusicPlaylistPage. Liner-notes genre links open Browse filtered by
that genre.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
)"
```

---


### Task 10: Remove `AlbumPage`, `ArtistPage`, `MusicController` and `tst_music_query`

**Files:**
- Delete: `src/ui/pages/AlbumPage.qml`, `src/ui/pages/ArtistPage.qml`, `src/app/controllers/MusicController.h`, `src/app/controllers/MusicController.cpp`, `tests/integration/tst_music_query.cpp`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`, `src/app/Application.h`, `src/app/Application.cpp`, `src/app/main.cpp`, `src/ui/Main.qml`
- Modify (tests): `tests/integration/tst_item_actions_queue.cpp`, `tests/integration/tst_content_controllers.cpp`
- Modify (comments only): `src/server/dto/ItemsQuery.h`, `src/server/emby/EmbyClient.h`, `src/app/ItemActions.h`, `src/app/ItemActions.cpp`, `src/app/controllers/PlaylistController.h`, `src/app/controllers/PlaylistController.cpp`, `src/ui/controls/PlaylistPicker.qml`, `src/ui/controls/StrmGrid.qml`, `src/ui/controls/TrackTable.qml`, and `src/ui/shell/BoundedNavigationStack.qml` if its `MusicController` comment is still present

**Interfaces:**
- Consumes: `music::MusicPlayback::playAlbum(const QString &albumId, const QString &title, int startIndex)` and `music::MusicRepository(EmbyClient*, QObject*)` (Phase 1); `ItemActions::orderedAlbumPlayRequested(const QString &albumId)`; `PlaylistController::playlistsMutated()`; `music::MusicBrowseController::playlistsLane()` returning `MusicLane*` with `retry()` (Phase 3).
- Produces: no new API. After this task, the only `MusicCtl` text left in `src/` or `tests/` is the negative assertions `QVERIFY(!….contains("MusicCtl"))` that Task 9 added to `tst_navigation_history.cpp`.

Every edit below is anchored on content, not line numbers (Contract note 11). Line numbers in parentheses are from the pre-Crate tree and only help you find the spot.

- [ ] **Step 1: Port the album-play tests in `tst_item_actions_queue.cpp` to `MusicPlayback`**

These tests still guard real behaviour (an album queues in order without touching page state; a newer leaf play retires the album fetch), so they move to the new owner instead of being deleted.

Replace the include:

```cpp
#include "app/controllers/MusicController.h"
```

with:

```cpp
#include "app/music/MusicPlayback.h"
#include "app/music/MusicRepository.h"
```

Delete these slots from `private slots:` and delete their bodies, including the comment block directly above each body:
- `void musicShuffleCarriesTheCurrentFilters();` (if still present — Phase 3's browse controller owns the filtered shuffle and tests it in `tst_music_browse_controller`)
- `void collectAlbumTracksReportsIdsWithoutTouchingThePlayer();` (the "add album to playlist" collection moved to `MusicBrowseController`, Phase 3)

Replace the whole body of `playAlbumQueuesTheServersOrderWithoutOpeningTheAlbum` with:

```cpp
void ItemActionsQueueTest::playAlbumQueuesTheServersOrderWithoutOpeningTheAlbum()
{
    m_mock->addRoute(
        QStringLiteral("GET"), itemsPath(), 200,
        QByteArrayLiteral("{\"Items\":["
                          "{\"Id\":\"301001\",\"Name\":\"So What\",\"Type\":\"Audio\","
                          "\"AlbumId\":\"al-kob\",\"ParentIndexNumber\":1,\"IndexNumber\":1},"
                          "{\"Id\":\"301002\",\"Name\":\"Freddie Freeloader\",\"Type\":\"Audio\","
                          "\"AlbumId\":\"al-kob\",\"ParentIndexNumber\":1,\"IndexNumber\":2},"
                          "{\"Id\":\"301003\",\"Name\":\"Blue In Green\",\"Type\":\"Audio\","
                          "\"AlbumId\":\"al-kob\",\"ParentIndexNumber\":1,\"IndexNumber\":3}],"
                          "\"TotalRecordCount\":3}"));

    music::MusicRepository repository(m_client);
    music::MusicPlayback playback(&repository, m_actions);

    QSignalSpy queueSpy(m_actions, &ItemActions::queueChanged);
    playback.playAlbum(QStringLiteral("al-kob"), QStringLiteral("Kind of Blue"), 0);

    QTRY_COMPARE(queueSpy.count(), 1);
    QCOMPARE(m_player->queue()->rowCount(), 3);
    QCOMPARE(m_player->queue()->currentIndex(), 0);
    // Disc/track order, which is the order the query asked for. An album
    // queued alphabetically is the bug this verb exists to avoid.
    QCOMPARE(m_player->queue()->itemAt(0).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("301001"));
    QCOMPARE(m_player->queue()->itemAt(2).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("301003"));

    // The repository's album query: disc then track, stated rather than
    // assumed (spec §3.3).
    const QUrlQuery query(m_mock->lastRequestFor(QStringLiteral("GET"), itemsPath()).query);
    QCOMPARE(query.queryItemValue(QStringLiteral("ParentId")), QStringLiteral("al-kob"));
    QCOMPARE(query.queryItemValue(QStringLiteral("Recursive")), QStringLiteral("true"));
    QCOMPARE(query.queryItemValue(QStringLiteral("SortBy")),
             QStringLiteral("ParentIndexNumber,IndexNumber,SortName"));
}
```

Replace the whole body of `canonicalAlbumPlayUsesTheOrderedControllerExpansion` with:

```cpp
void ItemActionsQueueTest::canonicalAlbumPlayUsesTheOrderedControllerExpansion()
{
    m_mock->addRoute(QStringLiteral("GET"), itemsPath(), 200,
                     QByteArrayLiteral("{\"Items\":["
                                       "{\"Id\":\"301002\",\"Name\":\"Second\",\"Type\":\"Audio\","
                                       "\"ParentIndexNumber\":1,\"IndexNumber\":1},"
                                       "{\"Id\":\"301001\",\"Name\":\"First\",\"Type\":\"Audio\","
                                       "\"ParentIndexNumber\":1,\"IndexNumber\":2}],"
                                       "\"TotalRecordCount\":2}"));
    music::MusicRepository repository(m_client);
    music::MusicPlayback playback(&repository, m_actions);
    // The same wiring Application makes (Task 10 Step 3).
    connect(m_actions, &ItemActions::orderedAlbumPlayRequested, &playback,
            [&playback](const QString &albumId) { playback.playAlbum(albumId, QString(), 0); });

    QVariantMap album{{QStringLiteral("itemId"), QStringLiteral("album-ordered")},
                      {QStringLiteral("name"), QStringLiteral("Server order")},
                      {QStringLiteral("type"), QStringLiteral("MusicAlbum")}};
    QSignalSpy ordered(m_actions, &ItemActions::orderedAlbumPlayRequested);
    QSignalSpy queue(m_actions, &ItemActions::queueChanged);
    m_actions->play(album);

    QCOMPARE(ordered.count(), 1);
    QTRY_COMPARE(queue.count(), 1);
    QCOMPARE(m_player->queue()->rowCount(), 2);
    QCOMPARE(m_player->queue()->itemAt(0).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("301002"));
    const QUrlQuery query(m_mock->lastRequestFor(QStringLiteral("GET"), itemsPath()).query);
    QCOMPARE(query.queryItemValue(QStringLiteral("ParentId")), QStringLiteral("album-ordered"));
    QCOMPARE(query.queryItemValue(QStringLiteral("Recursive")), QStringLiteral("true"));
    QCOMPARE(query.queryItemValue(QStringLiteral("SortBy")),
             QStringLiteral("ParentIndexNumber,IndexNumber,SortName"));
}
```

In `newerLeafPlayRetiresEveryAsynchronousQueueBuilder`, replace this block:

```cpp
    // MusicController expands an album before handing it to ItemActions, so it
    // reserves the same global playback intent before starting that fetch.
    MusicController music(m_client, this);
    music.setActions(m_actions);
    const int beforeAlbum = m_mock->requestCount();
    music.playAlbum(QStringLiteral("album-slow"));
```

with:

```cpp
    // MusicPlayback expands an album before handing it to ItemActions, so it
    // reserves the same global playback intent before starting that fetch.
    music::MusicRepository repository(m_client);
    music::MusicPlayback playback(&repository, m_actions);
    const int beforeAlbum = m_mock->requestCount();
    playback.playAlbum(QStringLiteral("album-slow"), QStringLiteral("Slow"), 0);
```

The lines after it (`QTRY_VERIFY(m_mock->requestCount() > beforeAlbum);` and the `directPlay` check) stay as they are. The `/Items` delay of 180 ms set earlier in that test is still active at this point, so the album reply still lands after the newer play.

- [ ] **Step 2: Run the ported tests**

```bash
cmake --build --preset dev --target tst_item_actions_queue && ctest --preset dev -R tst_item_actions_queue --output-on-failure
```

Expected: PASS. This is a port of an existing behaviour onto code that already exists (Phase 1), so there is no red step. If `playAlbumQueuesTheServersOrderWithoutOpeningTheAlbum` fails on the query assertions, compare with `MusicRepository::albumTracks` in Phase 1 and fix the test only if the repository matches the Phase 1 contract (`Recursive=true`, `SortBy=ParentIndexNumber,IndexNumber,SortName`); otherwise the repository is wrong.

- [ ] **Step 3: Rewire `Application`**

In `src/app/Application.cpp`:

1. Delete `#include "controllers/MusicController.h"`.
2. Delete `m_music = new MusicController(m_client, this);`.
3. In the `registerModel` initializer list, delete the three lines naming `m_music->…` (`albums()`, `artists()`, `tracks()`, `songs()`, `artistAlbums()`, `artistTracks()`, `playlists()`). The list then reads:

```cpp
    for (MediaItemModel *model : { m_home->resume(), m_home->nextUp(), m_home->favorites(),
                                   m_library->model(), m_search->model(), m_series->episodes(),
                                   m_details->similar(), m_details->upcomingEpisodes(),
                                   m_playlists->items() })
        m_actions->registerModel(model);
```

4. Delete the comment that starts `// Two lists of playlists exist on purpose (see MusicController::playlists):` and the `connect(m_playlists, &PlaylistController::playlistsMutated, m_music, &MusicController::invalidatePlaylists);` after it.
5. Delete the comment that starts `// The queue verbs live in ItemActions (ARCHITECTURE.md rule 3), and`, `m_music->setActions(m_actions);`, and `connect(m_actions, &ItemActions::orderedAlbumPlayRequested, m_music, &MusicController::playAlbum);`.
6. Delete `m_music->resetSessionState();` in `teardownAuthenticatedSession()`.

Directly after `m_musicPlayback = new music::MusicPlayback(m_musicRepository, m_actions, this);` (Phase 1), add:

```cpp
    // A canonical album play (▸ on an album card anywhere) is a semantic verb
    // ItemActions owns, but the ordered expansion is the music repository's
    // album query. The signal carries only an id, so the queue falls back to
    // its own context label.
    connect(m_actions, &ItemActions::orderedAlbumPlayRequested, m_musicPlayback,
            [this](const QString &albumId) { m_musicPlayback->playAlbum(albumId, QString(), 0); });
```

Search `Application.cpp` for `playlistsMutated`. If no remaining connection exists (Phase 3 did not add one), add directly after the Phase 3 block that constructs `m_musicBrowse` and registers its models:

```cpp
    // A playlist made, renamed or deleted from any surface changes the set the
    // browse Playlists section lists; PlaylistController refreshes only its own.
    connect(m_playlists, &PlaylistController::playlistsMutated, m_musicBrowse,
            [this] { m_musicBrowse->playlistsLane()->retry(); });
```

If Phase 3 named the member differently, substitute its name (Contract note 9). `MusicLane::retry()` (Phase 2) emits `retryRequested`, which the browse controller answers by reloading that section. `MusicBrowseController.h` already includes `app/controllers/music/MusicLane.h`, so no new include is needed.

In `src/app/Application.h`, delete `class MusicController;`, `MusicController *music() const { return m_music; }` and `MusicController *m_music = nullptr;`.

In `src/app/main.cpp`, delete `#include "controllers/MusicController.h"` and the `setContextProperty(QStringLiteral("MusicCtl"), app.music());` line.

- [ ] **Step 4: Remove the last `MusicCtl` readers in `Main.qml`**

1. Delete this whole block, including the three comment lines above it:

```qml
    // ▸ on an album card is a one-shot verb, and neither MusicPage nor
    // ArtistPage carries a toast host of its own. Here, so both are covered by
    // one connection.
    Connections {
        target: MusicCtl

        function onActionFailed(message) {
            toasts.show(message, "error");
        }
    }
```

`MusicPlayback` reports failures through `ItemActions::actionFailed`, which the existing `Connections { target: Actions … onActionFailed }` already toasts.

2. If `currentMusicTab: MusicCtl.tab` is still present on the stack, delete that line (Phase 3 replaces it with `currentMusicBrowseSection`).
3. Run `grep -n "MusicCtl" src/ui/Main.qml`. Expected: no output. If a `case "music":` or `openLibrary` branch still calls `MusicCtl`, Phase 3 is incomplete: stop and report it rather than deleting routes Phase 3 owns.

If `BoundedNavigationStack.qml` still has the comment `// MusicController is process-wide while each retained music route owns the` above `currentMusicTab`, delete the comment together with the property only when nothing sets `currentMusicTab` (`grep -rn currentMusicTab src tests` returns only that line). Otherwise leave it for Phase 3's owner and report it.

- [ ] **Step 5: Delete the files and their build entries**

```bash
git rm src/ui/pages/AlbumPage.qml src/ui/pages/ArtistPage.qml \
       src/app/controllers/MusicController.h src/app/controllers/MusicController.cpp \
       tests/integration/tst_music_query.cpp
```

In `src/CMakeLists.txt`, delete the `app/controllers/MusicController.cpp` source line (and `app/controllers/MusicController.h` if it is listed), and the `ui/pages/ArtistPage.qml` and `ui/pages/AlbumPage.qml` QML entries. If `ui/pages/MusicPage.qml` is still listed, Phase 3 is incomplete: stop and report it.

In `tests/CMakeLists.txt`, delete the whole block:

```cmake
strmqt_add_test(tst_music_query
    integration/tst_music_query.cpp
    mocks/MockEmbyServer.h mocks/MockEmbyServer.cpp
)
target_include_directories(tst_music_query PRIVATE mocks)
```

In `tests/integration/tst_content_controllers.cpp`, delete `#include "app/controllers/MusicController.h"`. If `musicRetargetDropsTheInFlightPage` is still present, delete its slot declaration, its body and the comment block above the body. Phase 3's `tst_music_browse_controller` covers the retarget case for the browse lanes, and `tst_album_artist_controllers` (Task 2) covers it for album and artist loads.

- [ ] **Step 6: Reword the comments that name `MusicController`**

Comments only; no code changes. Replace each old text with the new text.

`src/server/dto/ItemsQuery.h`:

```cpp
    // index. MusicController translates its letter into these; LibraryController
    // still sends nameStartsWith above.
```

→

```cpp
    // index. The music query translator sends its letter as these;
    // LibraryController still sends nameStartsWith above.
```

`src/server/emby/EmbyClient.h`:

```cpp
    // MusicController nevertheless sends its letter as NameStartsWithOrGreater +
```

→

```cpp
    // Music browse nevertheless sends its letter as NameStartsWithOrGreater +
```

`src/app/ItemActions.h` (shuffle comment):

```cpp
    // constraints already applied (MusicController's letter, genres and
    // favourites, for the music library's ▸ Shuffle); what stays owned here is
```

→

```cpp
    // constraints already applied (a controller's letter, genres and
    // favourites); what stays owned here is
```

`src/app/ItemActions.h` (signal comment):

```cpp
    // MusicController owns the server-ordered, non-recursive album expansion.
```

→

```cpp
    // MusicPlayback owns the disc-and-track-ordered album expansion.
```

`src/app/ItemActions.cpp`:

```cpp
    // registered models, and **MusicController's four models are not registered
    // here** — so every album, artist and track a music page could name would
```

→

```cpp
    // registered models, and **the music models are not registered here** —
    // so every album, artist and track a music page could name would
```

`src/app/controllers/PlaylistController.cpp`:

```cpp
// A hard stop on that walk, the same guard MusicController's genre walk carries:
```

→

```cpp
// A hard stop on that walk, the same guard every paged walk here carries:
```

`src/app/controllers/PlaylistController.h` (the `ensureAllPlaylists` comment):

```cpp
    // the page that stopped it. The same contract MusicController::loadGenres()
    // has, and for the same reason — a walk that broke on page 2 is not a
```

→

```cpp
    // the page that stopped it. A walk that broke on page 2 is not a
```

`src/app/controllers/PlaylistController.h` (the `playlistsMutated` comment):

```cpp
    // list of playlists in the app — MusicController keeps an audio-scoped one
    // for the music library's Playlists tab, and nothing else would ever tell
```

→

```cpp
    // list of playlists in the app — music browse keeps an audio-scoped one
    // for its Playlists section, and nothing else would ever tell
```

`src/ui/controls/PlaylistPicker.qml`:

```qml
    // (measured; see MusicController::loadPlaylists). Consumers set it once,
```

→

```qml
    // (measured). Consumers set it once,
```

`src/ui/controls/StrmGrid.qml`:

```qml
    // rows are replaced rather than added to. MusicCtl.albums and .artists are
    // CONSTANT Q_PROPERTIES — the model object never changes — and a filter
```

→

```qml
    // rows are replaced rather than added to. A controller's grid models are
    // CONSTANT Q_PROPERTIES — the model object never changes — and a filter
```

`src/ui/controls/TrackTable.qml`:

```qml
    // …and a different list is very often the SAME model object. MusicCtl.songs
    // is a CONSTANT Q_PROPERTY, so `onModelChanged` above fires once in the life
```

→

```qml
    // …and a different list is very often the SAME model object. A songs lane
    // is a CONSTANT Q_PROPERTY, so `onModelChanged` above fires once in the life
```

If Phase 3 already rewrote a comment (`StrmGrid.qml`, `TrackTable.qml`, `FilterBar.qml` are the likely ones), its current text no longer matches; leave it.

`FilterBar.qml` (`MusicCtl` at the `controller` comment, `MusicController` at the `tabChanged` and letter comments): if Phase 3 left these comments, apply the same rule — name "music browse" instead of `MusicCtl`/`MusicController`.

- [ ] **Step 7: Prove nothing names the old controller**

```bash
grep -rn "MusicController\|MusicCtl\|AlbumPage\.qml\|ArtistPage\.qml\|tst_music_query\b" src tests CMakeLists.txt \
  | grep -v 'contains("MusicCtl")' \
  | grep -v 'MusicAlbumPage\|MusicArtistPage\|NowPlayingMusicController\|tst_music_query_translator'
```

Expected: no output. The first `grep -v` keeps Task 9's negative assertions; the second keeps names that only contain the old ones as substrings. Any other line is a leftover reader: remove it, or report it if it belongs to a phase that has not landed.

- [ ] **Step 8: Build, test, self-test**

```bash
cmake --preset dev && cmake --build --preset dev
ctest --preset dev --output-on-failure
STRMQT_SELFTEST=1 QT_QPA_PLATFORM=offscreen QT_ASSUME_STDERR_HAS_CONSOLE=1 ./build/dev/strmqt 2>&1 | grep -E "selftest (ok|FAIL)|selftest:"; echo "exit=${PIPESTATUS[0]}"
```

Expected:
- The build finishes with no errors (a stale `MusicController` include shows up here as `No such file or directory`).
- Every test passes; `tst_music_query` is no longer listed.
- The self-test prints `selftest ok   album`, `selftest ok   artist` and `selftest ok   musicPlaylist`, no `selftest FAIL` line, and `exit=0`.
- No `ReferenceError: MusicCtl is not defined` appears anywhere in the self-test output (`STRMQT_SELFTEST=1 … ./build/dev/strmqt 2>&1 | grep -c MusicCtl` prints `0`).

- [ ] **Step 9: Commit**

```bash
git add -A src/ui/pages/AlbumPage.qml src/ui/pages/ArtistPage.qml \
    src/app/controllers/MusicController.h src/app/controllers/MusicController.cpp \
    tests/integration/tst_music_query.cpp \
    src/CMakeLists.txt tests/CMakeLists.txt src/app/Application.h src/app/Application.cpp \
    src/app/main.cpp src/ui/Main.qml src/ui/shell/BoundedNavigationStack.qml \
    tests/integration/tst_item_actions_queue.cpp tests/integration/tst_content_controllers.cpp \
    src/server/dto/ItemsQuery.h src/server/emby/EmbyClient.h src/app/ItemActions.h \
    src/app/ItemActions.cpp src/app/controllers/PlaylistController.h \
    src/app/controllers/PlaylistController.cpp src/ui/controls/PlaylistPicker.qml \
    src/ui/controls/StrmGrid.qml src/ui/controls/TrackTable.qml src/ui/shell/FilterBar.qml
git commit -m "$(cat <<'MSG'
refactor(music): remove MusicController and the old album and artist pages

Canonical album plays expand through MusicPlayback and the repository's
disc-ordered album query. The playlist-set refresh reaches the browse
Playlists lane. tst_music_query goes with the controller it tested; the
album-play queue tests move to MusicPlayback.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
)"
```

`git add -A` on a path that no longer exists or did not change is harmless; drop `FilterBar.qml` or `BoundedNavigationStack.qml` from the list if `git add` reports it as missing.

---

### Task 11: Phase 4 gate

**Files:**
- Modify: `config/qmllint-baseline.txt` (only through the script's `--update`, after the review in Step 3)

**Interfaces:**
- Consumes: everything Tasks 1–10 produced.
- Produces: a green tree, a reviewed qmllint baseline, and the user's sign-off on the three pages.

- [ ] **Step 1: Clean configure, build and full test run**

```bash
cmake --preset dev && cmake --build --preset dev 2>&1 | tail -n 5
ctest --preset dev --output-on-failure
```

Expected:
- The build ends without errors or new warnings from the Phase 4 files.
- `100% tests passed`. The list includes `tst_album_artist_controllers`, the extended `tst_music_repository`, `tst_music_playback`, `tst_content_controllers` and `tst_navigation_history`, and does **not** include `tst_music_query`.

If a test fails, fix the cause in the task that owns the file and rerun the whole suite. Do not continue to Step 2 with a red suite.

- [ ] **Step 2: Run the qmllint baseline check**

```bash
bash scripts/check-qmllint-baseline.sh build/dev; echo "exit=$?"
```

Expected: `exit=1` with a diff, because the new pages add `[unqualified]` context-property warnings and the old pages' warnings are gone. If the script instead prints `qmllint found unresolvable QML types (never baselineable).`, one of the four fatal categories fired: fix the named file and rerun. Never re-baseline past that message.

- [ ] **Step 3: Review the diff before re-baselining**

Save the added and removed warning records:

```bash
cmake --build build/dev --target strmqt_qmllint > /tmp/p4-qmllint.log 2>&1 || true
bash scripts/check-qmllint-baseline.sh build/dev > /tmp/p4-qmllint.diff 2>&1 || true
grep '^+src/' /tmp/p4-qmllint.diff > /tmp/p4-added.txt || true
grep '^-src/' /tmp/p4-qmllint.diff > /tmp/p4-removed.txt || true
wc -l /tmp/p4-added.txt /tmp/p4-removed.txt
```

Every **added** line must pass both checks:

```bash
# 1. Only the five Phase 4 QML files, plus Main.qml and BoundedNavigationStack.qml.
grep -v -E '^\+src/ui/(pages/Music(Album|Artist|Playlist)Page|music/(LinerNotes|CrateTrackTable)|Main|shell/BoundedNavigationStack)\.qml:' /tmp/p4-added.txt
# 2. Only unqualified access to a context property.
grep -v -F '[unqualified]' /tmp/p4-added.txt
grep -F '[unqualified]' /tmp/p4-added.txt \
  | grep -v -E '\b(AlbumCtl|ArtistCtl|PlaylistCtl|Actions|MusicPlay|PlayerCtl|App|MusicBrowseCtl|MusicHomeCtl|Prefs|Input)\b'
```

Expected: all three commands print nothing. Any line they print is a real warning (a missing `required property`, an unqualified delegate role, an id used across a component boundary). Fix it in the file and rerun Steps 2–3. Do not baseline it (Contract note 10).

Every **removed** line should belong to a file this phase deleted or to a `MusicCtl` read that went away:

```bash
grep -v -E '^-src/ui/pages/(AlbumPage|ArtistPage)\.qml:|MusicCtl' /tmp/p4-removed.txt
```

Expected: no output. A removed line elsewhere means a warning disappeared from an untouched file; open the file and confirm the edit that fixed it was intentional (a Task 5 `TrackRow` edit may legitimately remove one). Then confirm the old fingerprints are fully gone:

```bash
bash scripts/check-qmllint-baseline.sh build/dev --update
grep -c -E 'src/ui/pages/(AlbumPage|ArtistPage)\.qml|MusicCtl' config/qmllint-baseline.txt
git diff --stat config/qmllint-baseline.txt
rm -f /tmp/p4-qmllint.log /tmp/p4-qmllint.diff /tmp/p4-added.txt /tmp/p4-removed.txt
```

Expected: the update prints `Updated … (N warnings).`, the count is `0`, and the stat shows only `config/qmllint-baseline.txt`. Rerun `bash scripts/check-qmllint-baseline.sh build/dev` and expect `qmllint warning baseline matches`.

- [ ] **Step 4: Self-test**

```bash
STRMQT_SELFTEST=1 QT_QPA_PLATFORM=offscreen QT_ASSUME_STDERR_HAS_CONSOLE=1 ./build/dev/strmqt 2>&1 | tee /tmp/p4-selftest.log | grep -E "selftest (ok|FAIL)|selftest:"; echo "exit=${PIPESTATUS[0]}"
grep -c -E "ReferenceError|TypeError|Cannot assign to non-existent property|is not defined" /tmp/p4-selftest.log
rm -f /tmp/p4-selftest.log
```

Expected: `selftest ok` for `album`, `artist` and `musicPlaylist` (and every page from earlier phases), no `selftest FAIL`, `exit=0`, and a count of `0`.

- [ ] **Step 5: Security sweep of the phase's diff**

```bash
git diff --cached --stat; git log --oneline -12
git diff HEAD~10 -- src tests | grep -n -i -E "https?://[a-z0-9.-]+(:[0-9]+)?|api_key|X-Emby-Token: [A-Za-z0-9]{16,}|ignoreSslErrors|QSslSocket::VerifyNone" \
  | grep -v -E "127\.0\.0\.1|localhost|not-a-real-token-fixture-only|m_mock->baseUrl"
```

Adjust `HEAD~10` to the number of Phase 4 commits. Expected: the second command prints nothing. Any server URL, real token or TLS bypass is removed before the gate commit (AGENTS.md).

- [ ] **Step 6: The user's visual check**

Run the app against the user's own server (`./build/dev/strmqt`) and ask the user to walk this list. Do not tick it on their behalf.

1. **Album** (open one from Music Home and one from Browse):
   - Sleeve and liner notes on the left; rows with no data are omitted.
   - A multi-disc album shows "Side A / Side B" headings with "Disc N · runtime"; a single-disc album shows none.
   - The artist column appears only on rows whose artist differs from the album artist; featured artists sit dimmed after the title.
   - The now-playing row is amber; ▶, ⇄, ◎, ＋ Playlist and ♡ work; `S`, `L`, `R` and Space work.
   - A genre link opens Browse → Albums with that genre pill; Back returns to the album with focus restored.
   - "More by" shows other albums and is absent for an artist with one album.
2. **Artist:**
   - Backdrop (or the portrait wash when there is none), kicker "Artist · N records · N tracks", name in the 84 px display.
   - Tabs list only the non-empty groups with counts, newest first; the shoulder buttons cycle them.
   - Most played shows five tracks with covers and a "Guest on" caption where it applies; Similar artists is hidden when empty.
3. **Audio playlist** (open one from Browse → Playlists):
   - Collage and "N tracks · runtime"; the table shows cover thumbnails.
   - Reorder (drag and keyboard), remove, rename and delete behave as before; deleting the open playlist returns to the previous page.
   - A **video** playlist still opens the old `PlaylistPage`.
4. **Narrow window** (about 900 px wide, then phone-like widths): the album's left column folds under the tracklist; the artist's side column stacks; nothing clips or scrolls sideways.
5. **Session:** sign out and back in as another user; no album, artist or playlist data from the first session is visible.

Record the user's answers. If something fails, fix it in the owning task's files, rerun Steps 1–4, and repeat the failed item.

- [ ] **Step 7: Commit the gate**

```bash
git add config/qmllint-baseline.txt
git commit -m "$(cat <<'MSG'
chore(music): phase 4 gate

Re-baseline qmllint after review: the album, artist and audio playlist
pages add only unqualified context-property reads, and every AlbumPage,
ArtistPage and MusicCtl fingerprint is gone.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
)"
```

If the baseline did not change (the diff in Step 3 was empty), skip this commit and say so in the phase report.

---

## Self-review against the spec

**§6.1 Album: the back of a sleeve**

| Requirement | Where |
|---|---|
| 340 px sleeve, then liner notes; empty rows omitted | Task 6 left column (`page.sleeveSize`); Task 2 `linerNotes` builds only rows with data; Task 5 `LinerNotes` |
| Genre links open Browse → Albums with the genre pill | Task 5 `LinerNotes.linkActivated`; Task 6 forwards it as `musicGenreRequested`; Task 9 `openMusicGenreFromPage` |
| Kicker (release type and year), hero title, artist link | Task 6 header: `CrateKicker`, `CrateHeading` at `crateHeroAlbum`, `Actions.openArtist(AlbumCtl.artistId, AlbumCtl.artist)` (pinned by Task 9's test) |
| Badges: format (amber when hi-res), "N DISCS" when discCount > 1, "N tracks · runtime" | Task 2 controller properties; Task 6 badge row |
| ▶ Play · ⇄ Shuffle · ◎ Album radio · ＋ Playlist · ♡ | Task 6 header buttons; `AlbumCtl.play/shuffle/radio`, `PlaylistPicker`, `Actions.toggleFavorite` |
| "Side A / Side B" headings with "Disc N · runtime", only when discCount > 1 | Task 2 `discs`; Task 5 `TrackRow.discTitle/discDetail` and `CrateTrackTable.discStarts` |
| Artist column only where `differsFromAlbumArtist` | Task 5 `CrateTrackTable.artistColumnShown` per row |
| Featured artists dim after the title; now-playing amber; multi-select and type-to-jump | Task 5 `TrackRow.titleSuffix`, `nowPlayingId`, `jumpRole: "name"`; Task 6 `SelectionBar` |
| More by *artist*, hidden when empty | Task 6 footer `StrmRail`, `visible` on the more-by count |
| Everything from `AlbumSleeve` | Task 2 `AlbumController` reads only `albumSleeve(id)` |

**§6.2 Artist: poster and filed releases**

| Requirement | Where |
|---|---|
| Backdrop, fallback portrait with cover wash; kicker "Artist · N records · N tracks"; 84 px name | Task 7 hero (`StrmImage` gated on Prefs, `CoverWash`, `CratePortrait`, `CrateHeading` at `crateHeroArtist` = `scale(84)`) |
| ⇄ Shuffle artist · ◎ Artist radio · ♡ | Task 1 `MusicPlayback::shuffleArtist`; Task 2 `ArtistController`; Task 7 buttons |
| Tabs Albums / EPs & Singles / Appears on with counts, newest first, empty hidden | Task 2 `tabs` (non-empty only, repository order, Contract note 13); Task 7 `StrmTabBar` badges; `cycleTab` |
| Most played: five tracks with covers, guest appearances labelled | Task 1 `artistTracks`; Task 2 `topTrackCaptions` ("Guest on …"); Task 7 `CrateTrackTable` with `showCovers: true` |
| Similar artists: round portraits, hidden when empty | Task 7 `Repeater` of `CratePortrait` |
| Everything from `ArtistProfile` | Task 2 `ArtistController` reads only `artistProfile(libraryId, artistId)` |

**§6.3 Playlist (audio)**

| Requirement | Where |
|---|---|
| 2×2 collage and "N tracks · runtime · updated" | Task 4 `currentCovers`/`currentSummary`; Task 8 `CoverCollage`. **Deviation:** "· updated" is omitted because Emby 4.9.5 exposes no playlist modification date (Contract note 4). |
| Title in hero type; Play · Shuffle · Rename · Delete | Task 8 header and `StrmPanel`s |
| Track table with cover thumbnails | Task 8 `TrackRow { showCover: true }` |
| Reorder, remove, rename unchanged; video playlists keep `PlaylistPage` | Task 8 reuses `PlaylistCtl` verbs and `PlaylistPage`'s key handling; Task 9 routes `mode "audio"` only to `MusicPlaylistPage` (tested by `audioPlaylistRouteSelectsTheMusicPlaylistPage`) |

**§8 Navigation, input and errors**

| Requirement | Where |
|---|---|
| `album` and `artist` routes with history, forward stack and focus memory by stable identity | Task 9: existing routes keep their `id`-keyed snapshots; `prepareRoute` calls `AlbumCtl.open`/`ArtistCtl.open`; focus keys `album-tracks`, `artist-top-tracks` |
| Music context armed by Album and Artist | Task 9 `interactionContext` (album, artist and the audio playlist page) |
| Space plays/pauses, `S` shuffles the view, `L` favourites | Tasks 6–8 `MappedShortcut`s gated on `App.interactionContext === "music" && page.isActivePage` |
| `cycleSection` | Tasks 6–7 `cycleTab(step)` (the album returns `false`; the artist cycles release tabs) |
| Partial composition hides the failed part; only a core failure shows the error state with Retry | Phase 1 repository composition; Tasks 6–7 hide "More by", Similar and Most played when empty and show `EmptyState { severity: "error" }` with `retry()` only when the controller's core load failed |
| Session change clears; generations drop late replies | Task 2 generation-guarded `load` and `resetSessionState`; Task 3 reset wiring; Task 10 removes the old controller's parallel state |

**Other checks**
- **Placeholders:** none. Contract note 9 names the one substitution (`m_musicBrowse`) that depends on Phase 3's member name, and Tasks 9–10 mark every "if still present" edit that depends on Phase 3's removals.
- **Type consistency:** `AlbumCtl`/`ArtistCtl` property names used in Tasks 6–7 match Task 2's headers; `PlaylistCtl.currentSummary`/`currentCovers` match Task 4; `CrateTrackTable` properties match Task 5; `MusicPlayback::playAlbum(albumId, title, startIndex)` matches Phase 1 and is used with three arguments in Task 10.
- **Behaviour change called out:** the canonical album expansion is now recursive and sorted by disc and track (Contract note 8); Task 10's ported tests assert it. Album `R` is album radio (Contract note 12).
