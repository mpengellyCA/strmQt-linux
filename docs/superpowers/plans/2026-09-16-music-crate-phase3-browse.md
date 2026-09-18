# Music Crate, Phase 3: Browse

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace `MusicPage.qml` with the Crate browse page. That means:
- a section strip, filter pills with a genre picker, crate dividers and a count readout
- per-section sort memory, filters shared across sections, and independent lanes
- the `musicBrowse` route

Retire `MusicPage.qml`, the music branches of `FilterBar.qml` and the `music` route.

**Architecture:** see the index, `docs/superpowers/plans/2026-09-16-music-crate.md`. **Read its Global Constraints, Shared vocabulary and Phase 2–5 contract before any task.** Every task in this file implicitly includes them.
- `MusicBrowseController` owns the whole query and each section's lane.
- QML states intent: pick a section, set a filter, jump a letter. The controller decides what to fetch, what to invalidate and every string shown.

**Spec:** `docs/superpowers/specs/2026-09-16-music-crate-design.md`:
- §5 (Browse: frame, filters, sections)
- §8 (routes, input, errors)
- §3.5 (controllers)
- mockup `docs/superpowers/specs/2026-09-16-music-crate-mockups/browse-structure.html`, option A

**Depends on:** Phase 1 (data layer) and Phase 2 (lanes, Crate controls, `cardComponent`, Home, `openMusicHome`), both merged.

## Contract notes

These add to the index's contract or record deviations from it. Nothing listed in the index is renamed or re-typed.

1. **Namespace of `MusicLane`.** Phase 2 declares `MusicLane` in `strmqt::music` (`app/controllers/music/MusicLane.h`), beside the models, and this file relies on that. `MusicLane::reset()` (Phase 2) is used to drop in-flight pages when a section's query moves.
2. **Phase 1 additions (Task 1):**
   - `QFuture<Result<QList<GenreBin>>> MusicRepository::coverGenres(const QString &libraryId, QList<GenreBin> genres)` samples covers (session-cached) for every bin that has none. `genreBins` is refactored onto it.
     - The Genres section needs covers for a page of bins sorted by *name* too, which `genreBins` (largest first) cannot give.
   - `void MusicPlayback::playQuery(const MusicQuery &query, const QString &label, int limit = 500)` queues the filtered Songs scope in the query's own order, labelled `label`.
     - **▶ Play** needs this. `shuffleQuery` covers only **⇄ Shuffle**.
3. **Additions to `MusicBrowseController`**, beyond the contract:
   - **Properties:**
     - `filtersAvailable`: the pill row applies (Albums, Artists, Songs)
     - `decadeAvailable`: false on Artists, whose endpoints ignore years
     - `scopeLabel`: "Jazz · Favourites", or "All music"; the queue label for Play and Shuffle
     - `routeState`: compact JSON for history
     - `genreOptionsLoading`, `genreOptionsFailed`
   - **Invokables:**
     - `restore(libraryId, section, state)`
     - `toggleLetter(letter)`, `ensureGenreOptions()`
     - `collectAlbumTracks(albumId, name)`: an album card's "Add to playlist"
   - **Signals:** `libraryChanged`, `sectionChanged`, `queryChanged`, `countsChanged`, `genreOptionsChanged`, `albumTracksCollected(QString subject, QStringList trackIds)`, `actionFailed(QString message)`
   - **C++ only:** `QList<MusicModelBase *> models()`, `resetSessionState()`, typed lane and model getters
   - **Types:** lane properties are typed `MusicLane *` and model properties are typed models. QML sees the same members.
4. **`genreOptions` rows** also carry `subtitle` ("40 records"), so the picker builds no strings.
5. **The count readout:**
   - The noun follows the section: `RECORDS`, `ARTISTS`, `SONGS`, `GENRES`, `PLAYLISTS` (singular for 1).
   - The unfiltered half (`1,204 →`) appears only when the view is narrowed by a filter or a letter and the unfiltered total is known. The total comes from the section's own unnarrowed fetch, or else from one `Limit=1` request.
   - Random shows no arrow.
   - Numbers use en-US grouping, like the mono readout in the mockup.
6. **The Decade menu shows no counts.** Spec §5.2 asks for "with counts". Emby has no per-decade facet, so counts would cost eight extra requests per filter change for a menu opened rarely. Decade options are plain labels.
7. **Which pills show where:**
   - Decade is hidden on Artists (`decadeAvailable`), and Format wherever `formatFilterable` is false.
   - The pill row, the count readout's filter half, and ▶ Play / ⇄ Shuffle are hidden on Genres and Playlists. Those sections take no filters (the translator drops them).
   - The filters stay set and apply again on returning to Albums, Artists or Songs.
8. **`openGenre` replaces every filter** with that one genre and clears the Albums letter. It reads as "show me this genre", and a leftover Favourites or Decade would hide most of it.
9. **Letters:**
   - The dividers show only for sort *Name* on Albums, Artists and Songs.
   - `jumpLetter` returns false elsewhere, so Main pages the view instead.
   - From no letter, forward lands on `A` and back on `Z`. After that it clamps, including `#`, and still returns true at either end.
10. **＋ New playlist** is a fixed tile above the Playlists grid, not a row of the grid model. That keeps `PlaylistGridModel` a pure DTO model and focus memory stable. It opens a create prompt that calls `PlaylistCtl.create(name, [], "Audio")`.
11. **Route state:**
    - `musicBrowse` retains `tab` (the section) and `query` (`MusicBrowseCtl.routeState`, JSON `{"v":1,…}`).
    - `BoundedNavigationStack.currentMusicTab` is replaced by `currentMusicBrowseSection` and `currentMusicBrowseState`.
    - `query` is bounded to 1024 characters. A state past that (dozens of genres) fails to parse on restore, and then the live query stays: a degraded restore, never a wrong one.
12. **Songs keeps the existing `TrackTable`/`TrackRow`** until Phase 4's `CrateTrackTable`.
    - Row layout: # · title · artist · "album · FORMAT" as the secondary line · duration · ♡.
    - The format badge lives in that secondary text.
13. **`MusicCtl` stays** until Phase 4. The album and artist pages still use it. This phase removes only its browse consumers: `MusicPage`, the FilterBar music branches, the `music` route, and `tst_music_query::musicPageInstantiatesOnlyTheActiveTab`.
14. **The Phase 2 interim route.** Phase 2 Task 8 wired Home's `sectionRequested`/`genreRequested` to an interim `root.openMusicSection(libraryId, name, section, genreId)`, which pushes the old `music` route through `MusicCtl`.
    - Task 6 deletes that function and its `sectionBody` ordering pin.
    - It rewires both handlers to `openMusicBrowse`/`openMusicGenre` (the genre handler now passes `genreName` on).
    - It retargets Phase 2's `homeBody` pin to end at `openPlaylists`.
    - Task 6's anchors are Phase 2 Task 8's exact text.
15. **Card delegates.** The Crate cards follow the Phase 2 `cardComponent` protocol. The grid's Loader sets `model`, `index`, `current` and `hovered`, and forwards `activated()`, `playRequested()` and `menuRequested(x, y)` to `itemActivated`, `itemPlayRequested` and `menuRequested`. So the page handles card actions on the grid, once, for pointer and keyboard alike.

16. **Playlist cards draw one cover.** Spec §5.3 asks for 2×2 collages. `PlaylistGridModel` (Phase 1) has only `coverUrl` and no cover list, so the card passes `[coverUrl]` to `CoverCollage`, which draws a single cover whole. A cover-list role is a Phase 1 model change, left for a later pass. When that role arrives, only the card's `covers:` binding changes.
17. **`railKey` also strips `"musicBrowse:"`.** Phase 2 strips `"musicHome:"`. Task 6 widens the pattern to `/^music(?:Home|Browse):/`, so the rail highlights the music library on Browse too.
18. **`openMusicGenre` from another page** calls `MusicBrowseCtl.open(libraryId, "albums")` and then `openGenre`. When the library or section moved, `open` starts an Albums page that `openGenre`'s invalidation drops at once, through `MusicLane::reset()` and the generation check. That costs one discarded request and never a wrong page. It keeps both route functions on the controller's public API instead of adding a combined invokable.
19. **Keys on Genres and Playlists.**
    - `S` (`music.shuffleAll`) and ▶ Play / ⇄ Shuffle follow `filtersAvailable`, so they are inert on Genres and Playlists. Spec §8's "shuffles the current view" has no filtered scope to sample there: a bin opens, and a playlist plays from its own page.
    - `L` favourites the focused card or the Songs selection, and is off on Genres.
    - `R` (instant mix) is off on Genres and Playlists.
20. **The lazy-lane structural pin moves.** `tst_music_query::musicPageInstantiatesOnlyTheActiveTab` is deleted with `MusicPage`. The same guarantee is pinned on `MusicBrowsePage` by `tst_navigation_history::musicBrowsePageInstantiatesOnlyTheActiveSection`:
    - one `Loader`, five inert Components
    - the view is captured before the section swaps
    - a rebuilt view restores its cursor
21. **FilterBar loses its music shape entirely:** the `favoritesOnly` toggle, `extraFilters`/`extraFilterActivated`, `toggledSelection`, the `tabChanged` connection and the `ProductionYear`/`PlayCount` direction seeds. None had a `LibraryCtl` consumer. `upTarget`, `stepLetter` and multi-select support in `StrmSelect` stay, because they are generic.
22. **Songs rows until Phase 4.**
    - Feat. text is not dimmed separately: `TrackRow` has one title.
    - Type-to-jump is whatever `TrackTable` does today.
    - Phase 4's `CrateTrackTable` takes both over (see note 12).

## Waves

| Wave | Tasks | Notes |
|---|---|---|
| 3a | 1 ‖ 4 | Agent A: repository and playback additions (C++). Agent B: the three QML controls (no C++) |
| 3b | 2 | `MusicBrowseController` and its integration test (needs 1) |
| 3c | 3 | Application wiring and `MusicBrowseCtl` (needs 2) |
| 3d | 5 | `MusicBrowsePage.qml` (needs 3, 4) |
| 3e | 6 | Routes, deletions, test ports (needs 5) |
| 3f | 7 | Phase gate and manual visual check (orchestrator) |

**Commits in parallel waves:** agents share one working tree, so an agent in a wave with more than one agent (3a) does **not** run its task's commit step. It reports the files it touched. At the gate, the orchestrator:
1. builds and tests the integrated tree;
2. runs each task's `git add … && git commit` in task order, with that task's message;
3. runs `rm -rf /tmp/w3a*` in the same step as the last commit (AGENTS.md).

A task run by a single agent (waves 3b–3f, or any serial execution) commits its own step.

Build commands used in every task (the default dev tree; parallel agents substitute their `/tmp/w<wave><agent>` directory as the index explains):

```bash
cmake --preset dev && cmake --build --preset dev
ctest --preset dev -R <test_name> --output-on-failure
```

QML-only tasks have no unit test. Their verification is:
- `bash scripts/check-qmllint-baseline.sh build/dev`, reviewed as described in the index
- the offscreen self-test: `STRMQT_SELFTEST=1 QT_QPA_PLATFORM=offscreen QT_ASSUME_STDERR_HAS_CONSOLE=1 ./build/dev/strmqt` must exit 0

---

### Task 1: `MusicRepository::coverGenres` and `MusicPlayback::playQuery`

**Files:**
- Modify: `src/app/music/MusicRepository.h`, `src/app/music/MusicRepository.cpp`
- Modify: `src/app/music/MusicPlayback.h`, `src/app/music/MusicPlayback.cpp`
- Test: `tests/integration/tst_music_repository.cpp`, `tests/integration/tst_music_playback.cpp`

**Interfaces:**
- Consumes (Phase 1):
  - `MusicRepository::genreCovers` (private), `Pending`, `Fanout`, `browseTracks`
  - `MusicPlayback::playResolved`, `Order::AsGiven`
- Produces:
  - `QFuture<Result<QList<GenreBin>>> MusicRepository::coverGenres(const QString &libraryId, QList<GenreBin> genres)`
    - Every bin whose `covers` is empty gets up to 3 sampled album covers. Bins that already have covers are untouched.
    - Order is preserved. A failed sample leaves that bin coverless and never fails the call.
  - `void MusicPlayback::playQuery(const MusicQuery &query, const QString &label, int limit = 500)`
    - Queues the first `limit` tracks of `query` scoped to Songs, with the letter dropped, in the query's order, labelled `label`.
    - Reserves the playback intent before the request.

- [x] **Step 1: Write the failing repository test**

In `tests/integration/tst_music_repository.cpp`, declare under `private slots:`:

```cpp
    void coverGenresSamplesOnlyBinsWithoutCovers();
```

Add the definition before `QTEST_MAIN`:

```cpp
void MusicRepositoryTest::coverGenresSamplesOnlyBinsWithoutCovers()
{
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"GenreIds", "g1"}, {"SortBy", "Random"}}, 200,
                          page({albumJson("c-g1a"), albumJson("c-g1b")}));
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"GenreIds", "g3"}, {"SortBy", "Random"}}, 500, "{}");

    GenreBin jazz;
    jazz.id = QStringLiteral("g1");
    jazz.name = QStringLiteral("Jazz");
    GenreBin rock;
    rock.id = QStringLiteral("g2");
    rock.name = QStringLiteral("Rock");
    rock.covers = {ImageRef{QStringLiteral("kept"), QStringLiteral("Primary"), QStringLiteral("tag-kept")}};
    GenreBin folk;
    folk.id = QStringLiteral("g3");
    folk.name = QStringLiteral("Folk");

    const auto result = waitFor(m_repo->coverGenres(kLibrary, {jazz, rock, folk}));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.value.size(), 3);
    QCOMPARE(result.value.at(0).id, QStringLiteral("g1"));
    QCOMPARE(result.value.at(0).covers.size(), 2);
    QCOMPARE(result.value.at(1).covers.size(), 1);
    QCOMPARE(result.value.at(1).covers.first().itemId, QStringLiteral("kept"));
    QVERIFY(result.value.at(2).covers.isEmpty()); // a failed sample degrades, never fails

    for (const auto &request : m_mock->requests())
        QVERIFY(QUrlQuery(request.query).queryItemValue("GenreIds") != QStringLiteral("g2"));
}
```

- [x] **Step 2: Write the failing playback test**

In `tests/integration/tst_music_playback.cpp`, declare under `private slots:`:

```cpp
    void playQueryQueuesTheFilteredScopeInOrder();
```

Add the definition before `QTEST_GUILESS_MAIN`:

```cpp
void MusicPlaybackTest::playQueryQueuesTheFilteredScopeInOrder()
{
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"IncludeItemTypes", "Audio"}, {"Filters", "IsFavorite"}}, 200,
                          page({trackJson("a1", "alA", 1), trackJson("a2", "alA", 2), trackJson("a3", "alA", 3)}));

    MusicQuery query;
    query.libraryId = kLibrary;
    query.section = Section::Albums; // playQuery scopes to Songs itself
    query.sortKey = QStringLiteral("album");
    query.letter = QStringLiteral("Q");
    query.favouritesOnly = true;
    m_playback->playQuery(query, QStringLiteral("Favourites"));

    QTRY_COMPARE(queueIds(queue()), (QStringList{"a1", "a2", "a3"}));
    QCOMPARE(queue()->currentIndex(), 0);
    QCOMPARE(queue()->sourceLabel(), QStringLiteral("Favourites"));

    const QUrlQuery sent(m_mock->lastRequestFor("GET", itemsPath()).query);
    QCOMPARE(sent.queryItemValue("Limit"), QStringLiteral("500"));
    QCOMPARE(sent.queryItemValue("SortBy"), QStringLiteral("Album,ParentIndexNumber,IndexNumber,SortName"));
    QVERIFY(!sent.hasQueryItem("NameStartsWithOrGreater"));
}
```

Add `#include <QUrlQuery>` to the includes if the file lacks it.

- [x] **Step 3: Run the tests to verify they fail**

Run: `cmake --build --preset dev --target tst_music_repository tst_music_playback`
Expected: a compile error (`coverGenres` and `playQuery` are not members).

- [x] **Step 4: Implement `coverGenres` and refactor `genreBins` onto it**

In `MusicRepository.h`, after `genreBins`:

```cpp
    // Samples up to three album covers for every bin that has none (session
    // cache per genre). Order is kept; a failed sample leaves that bin bare.
    QFuture<Result<QList<GenreBin>>> coverGenres(const QString &libraryId, QList<GenreBin> genres);
```

In `MusicRepository.cpp`, replace `genreBins` with the following and add `coverGenres` after it:

```cpp
QFuture<Result<Page<GenreBin>>> MusicRepository::genreBins(const QString &libraryId, int limit)
{
    Pending<Page<GenreBin>> pending;
    allGenres(libraryId).then(this, [this, pending, libraryId, limit](Result<QList<GenreBin>> all) {
        if (!all.ok())
            return pending.resolve(Result<Page<GenreBin>>::failure(all.error));
        const int total = static_cast<int>(all.value.size());
        coverGenres(libraryId, all.value.mid(0, limit))
            .then(this, [pending, total](Result<QList<GenreBin>> covered) {
                Page<GenreBin> page;
                page.items = covered.value; // coverGenres never fails
                page.totalRecordCount = total;
                pending.resolve(Result<Page<GenreBin>>::success(page));
            });
    });
    return pending.future();
}

QFuture<Result<QList<GenreBin>>> MusicRepository::coverGenres(const QString &libraryId, QList<GenreBin> genres)
{
    Pending<QList<GenreBin>> pending;
    auto shared = std::make_shared<QList<GenreBin>>(std::move(genres));
    auto fan = Fanout::create(this, [pending, shared] {
        pending.resolve(Result<QList<GenreBin>>::success(*shared));
    });
    for (int i = 0; i < shared->size(); ++i) {
        if (!shared->at(i).covers.isEmpty())
            continue;
        fan->add<QList<ImageRef>>(genreCovers(libraryId, shared->at(i).id),
                                  [shared, i](Result<QList<ImageRef>> r) {
                                      if (r.ok())
                                          (*shared)[i].covers = r.value;
                                      else
                                          qCWarning(logApp) << "music: genre covers failed" << r.error;
                                  });
    }
    fan->seal();
    return pending.future();
}
```

- [x] **Step 5: Implement `playQuery`**

In `MusicPlayback.h`, after `shuffleQuery`:

```cpp
    // ▶ Play on a filtered view: the Songs scope in its own order (spec §5.1).
    void playQuery(const MusicQuery &query, const QString &label, int limit = 500);
```

In `MusicPlayback.cpp`, after `shuffleQuery`:

```cpp
void MusicPlayback::playQuery(const MusicQuery &query, const QString &label, int limit)
{
    const quint64 generation = m_actions->reservePlaybackIntent();
    MusicQuery scoped = query;
    scoped.section = Section::Songs;
    // The letter is a place to land, not part of what the view means.
    scoped.letter.clear();
    auto tracks = m_repository->browseTracks(scoped, 0, limit).then(this, [](Result<Page<Track>> r) {
        if (!r.ok())
            return Result<QList<Track>>::failure(r.error);
        return Result<QList<Track>>::success(r.value.items);
    });
    playResolved(tracks, generation, 0, Order::AsGiven, label);
}
```

- [x] **Step 6: Run the tests to verify they pass**

Run: `cmake --build --preset dev --target tst_music_repository tst_music_playback && ctest --preset dev -R "tst_music_repository|tst_music_playback" --output-on-failure`
Expected: PASS. `genreBinsSampleCoversOnce` still passes on the refactored `genreBins`.

- [x] **Step 7: Commit**

```bash
git add src/app/music/MusicRepository.* src/app/music/MusicPlayback.* tests/integration/tst_music_repository.cpp tests/integration/tst_music_playback.cpp
git commit -m "feat(music): cover arbitrary genre pages and play a filtered scope

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---
### Task 2: `MusicBrowseController`

**Files:**
- Create: `src/app/controllers/music/MusicBrowseController.h`, `src/app/controllers/music/MusicBrowseController.cpp`
- Modify: `src/CMakeLists.txt` (add both to `strmqt_app`, next to the Phase 2 music controllers)
- Create: `tests/integration/tst_music_browse_controller.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes:
  - `MusicRepository::browseAlbums`, `browseArtists`, `browseTracks`, `browsePlaylists`, `allGenres`, `albumTracks`, and `coverGenres` (Task 1)
  - `MusicPlayback::playQuery` (Task 1), `shuffleQuery`
  - `MusicQueryTranslator::sortKeysFor`, `formatFilterable`, `formatOptions`
  - `formatRecordCount` (`MusicFormat.h`)
  - The five grid and list models; `MusicLane` (Phase 2)
- Produces: the contract's `MusicBrowseController` plus the additions in Contract notes 3–4. Behaviour:
  - **Query:**
    - The filters (genres, decade, format, favourites, unplayed) and `artistMode` are shared.
    - Sort key, direction and letter are remembered per section.
    - `queryFor(section)` is what that section actually sends:
      - letter only on sort `name` for Albums, Artists and Songs
      - no filters on Genres or Playlists
      - no decade on Artists
      - no format where it is not filterable
  - **Fetching:**
    - A change to the effective query of a section marks that section stale and clears its model. Only the visible section refetches; a hidden one refetches when it is shown.
    - Replies carry the lane generation, the session epoch, the query they asked and their start index. Anything that no longer matches is dropped.
  - **Counts:**
    - `resultCount` is the server total of the visible section.
    - `unfilteredCount` is the same section with no filters and no letter. It comes from that section's own unnarrowed fetch, or from one `Limit=1` request when the view opened already narrowed.
  - **Pages:**
    - 100 rows per page (one page for Random).
    - Genres are sorted client-side from `allGenres`, 48 bins per page, with covers from `coverGenres`.
  - **Route state:** `routeState` is JSON of the form

    ```json
    {"v":1,"g":[…],"d":0,"f":"any","fav":false,"un":false,"am":"albumArtists","s":[{"k":"name","d":false,"l":""},×5]}
    ```

    `restore` validates every field and ignores what it cannot read.

- [x] **Step 1: Write the failing test**

`tests/integration/tst_music_browse_controller.cpp`:

```cpp
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QtTest>

#include <algorithm>

#include "FakePlayerBackend.h"
#include "MockEmbyServer.h"
#include "app/ItemActions.h"
#include "app/PlayQueue.h"
#include "app/controllers/PlayerController.h"
#include "app/controllers/music/MusicBrowseController.h"
#include "app/controllers/music/MusicLane.h"
#include "app/music/MusicPlayback.h"
#include "app/music/MusicQueryTranslator.h"
#include "app/music/MusicRepository.h"
#include "app/music/models/AlbumGridModel.h"
#include "app/music/models/ArtistGridModel.h"
#include "core/Settings.h"
#include "server/emby/EmbyClient.h"
#include "server/emby/MusicServerCapabilities.h"

using namespace strmqt;
using namespace strmqt::music;

namespace {

const auto kUserId = QStringLiteral("a1b2c3d4e5f60718293a4b5c6d7e8f90");
const auto kToken = QStringLiteral("not-a-real-token-fixture-only");
const auto kLibrary = QStringLiteral("1868998");
const auto kAlbumArtistsPath = QStringLiteral("/Artists/AlbumArtists");

QString fixturePath(const QString &name)
{
    return QStringLiteral(STRMQT_FIXTURES_DIR "/") + name;
}

QString itemsPath() { return QStringLiteral("/Users/%1/Items").arg(kUserId); }

QJsonObject albumJson(const QString &id)
{
    return {{"Id", id}, {"Name", "Album " + id}, {"Type", "MusicAlbum"},
            {"AlbumArtists", QJsonArray{QJsonObject{{"Id", "ar1"}, {"Name", "Artist ar1"}}}},
            {"ChildCount", 10}, {"ImageTags", QJsonObject{{"Primary", "tag-" + id}}}};
}

QJsonObject artistJson(const QString &id)
{
    return {{"Id", id}, {"Name", "Artist " + id}, {"Type", "MusicArtist"}};
}

QJsonObject trackJson(const QString &id, const QString &albumId, int number)
{
    return {{"Id", id}, {"Name", "Track " + id}, {"Type", "Audio"}, {"AlbumId", albumId},
            {"Album", "Album " + albumId}, {"ParentIndexNumber", 1}, {"IndexNumber", number},
            {"RunTimeTicks", 4 * 60 * 10'000'000LL}};
}

QByteArray page(const QJsonArray &items, int total = -1)
{
    return QJsonDocument(QJsonObject{{"Items", items}, {"TotalRecordCount", total < 0 ? items.size() : total}})
        .toJson(QJsonDocument::Compact);
}

QJsonArray albums(const QString &prefix, int count)
{
    QJsonArray out;
    for (int i = 0; i < count; ++i)
        out.append(albumJson(prefix + QString::number(i)));
    return out;
}

QStringList modelIds(MusicModelBase *model)
{
    QStringList ids;
    for (int row = 0; row < model->count(); ++row)
        ids.append(model->idAt(row));
    return ids;
}

QStringList queueIds(PlayQueue *queue)
{
    QStringList ids;
    for (int row = 0; row < queue->rowCount(); ++row)
        ids.append(queue->itemAt(row).value(QStringLiteral("itemId")).toString());
    return ids;
}

using Q = QList<QPair<QString, QString>>;

} // namespace

class MusicBrowseControllerTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void sendsTheTranslatedQuery();
    void remembersSortPerSection();
    void filterChangeRefetchesVisibleAndInvalidatesHidden();
    void dropsRepliesForASupersededQuery();
    void loadMoreAppendsTheNextPage();
    void openGenreSwitchesToAlbumsWithOnlyThatGenre();
    void countTextShowsUnfilteredArrowFiltered();
    void formatAvailabilityFollowsCapabilities();
    void playAndShuffleUseTheScopeLabel();
    void genreOptionsCarryCountsFromAllGenres();
    void lettersAndSectionsCycle();
    void routeStateRoundTrips();

private:
    PlayQueue *queue() const { return m_player->queue(); }
    int requestsTo(const QString &path, const Q &required = {}) const;
    QUrlQuery lastItems() const { return QUrlQuery(m_mock->lastRequestFor("GET", itemsPath()).query); }

    MockEmbyServer *m_mock = nullptr;
    emby::EmbyClient *m_client = nullptr;
    FakePlayerBackend *m_backend = nullptr;
    QTemporaryDir *m_dir = nullptr;
    Settings *m_settings = nullptr;
    PlayerController *m_player = nullptr;
    ItemActions *m_actions = nullptr;
    MusicRepository *m_repo = nullptr;
    MusicPlayback *m_playback = nullptr;
    MusicBrowseController *m_ctl = nullptr;
};

int MusicBrowseControllerTest::requestsTo(const QString &path, const Q &required) const
{
    int count = 0;
    for (const auto &request : m_mock->requests()) {
        if (request.path != path)
            continue;
        const QUrlQuery query(request.query);
        const bool matches = std::all_of(required.cbegin(), required.cend(), [&](const auto &pair) {
            return query.queryItemValue(pair.first) == pair.second;
        });
        if (matches)
            ++count;
    }
    return count;
}

void MusicBrowseControllerTest::init()
{
    m_mock = new MockEmbyServer(this);
    QVERIFY(m_mock->start());
    for (const char *id : {"t1", "t2"}) {
        QVERIFY(m_mock->addRouteFromFile("POST", QStringLiteral("/Items/%1/PlaybackInfo").arg(id),
                                         fixturePath(QStringLiteral("playback_info.json"))));
    }
    m_mock->addRoute("POST", "/Sessions/Playing", 204, {});
    m_mock->addRoute("POST", "/Sessions/Playing/Progress", 204, {});
    m_mock->addRoute("POST", "/Sessions/Playing/Stopped", 204, {});
    m_mock->addRoute("GET", itemsPath(), 200, page(albums("a", 3), 3));
    m_mock->addRoute("GET", kAlbumArtistsPath, 200, page({artistJson("ar1"), artistJson("ar2")}));
    m_mock->addRoute("GET", "/Artists", 200, page({artistJson("ar1"), artistJson("ar2"), artistJson("ar3")}));

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
    m_ctl = new MusicBrowseController(m_repo, m_playback, this);
}

void MusicBrowseControllerTest::cleanup()
{
    delete m_ctl;
    delete m_playback;
    delete m_repo;
    delete m_actions;
    delete m_player;
    delete m_settings;
    delete m_dir;
    delete m_backend;
    delete m_client;
    delete m_mock;
    m_ctl = nullptr;
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

void MusicBrowseControllerTest::sendsTheTranslatedQuery()
{
    m_ctl->open(kLibrary, QStringLiteral("albums"));
    QCOMPARE(m_ctl->libraryId(), kLibrary);
    QCOMPARE(m_ctl->section(), QStringLiteral("albums"));
    QTRY_VERIFY(!m_ctl->albumsLane()->loading());
    QCOMPARE(m_ctl->albums()->count(), 3);

    QUrlQuery sent = lastItems();
    QCOMPARE(sent.queryItemValue("ParentId"), kLibrary);
    QCOMPARE(sent.queryItemValue("IncludeItemTypes"), QStringLiteral("MusicAlbum"));
    QCOMPARE(sent.queryItemValue("SortBy"), QStringLiteral("SortName"));
    QCOMPARE(sent.queryItemValue("SortOrder"), QStringLiteral("Ascending"));
    QCOMPARE(sent.queryItemValue("StartIndex"), QStringLiteral("0"));
    QCOMPARE(sent.queryItemValue("Limit"), QStringLiteral("100"));

    m_ctl->setGenres({QStringLiteral("g1")});
    m_ctl->setLetter(QStringLiteral("B"));
    QTRY_VERIFY(!m_ctl->albumsLane()->loading());
    sent = lastItems();
    QCOMPARE(sent.queryItemValue("GenreIds"), QStringLiteral("g1"));
    QCOMPARE(sent.queryItemValue("NameStartsWithOrGreater"), QStringLiteral("B"));
    QCOMPARE(sent.queryItemValue("NameLessThan"), QStringLiteral("C"));
    QVERIFY(m_ctl->filtered());
    QCOMPARE(m_ctl->activeFilterCount(), 1);
}

void MusicBrowseControllerTest::remembersSortPerSection()
{
    m_ctl->open(kLibrary, QStringLiteral("albums"));
    QTRY_VERIFY(!m_ctl->albumsLane()->loading());

    m_ctl->setSortKey(QStringLiteral("year"));
    QVERIFY(m_ctl->sortDescending()); // the key's default direction
    QTRY_VERIFY(!m_ctl->albumsLane()->loading());
    QUrlQuery sent = lastItems();
    QCOMPARE(sent.queryItemValue("SortBy"), QStringLiteral("ProductionYear,PremiereDate,SortName"));
    QCOMPARE(sent.queryItemValue("SortOrder"), QStringLiteral("Descending"));

    m_ctl->setSection(QStringLiteral("songs"));
    QCOMPARE(m_ctl->sortKey(), QStringLiteral("name"));
    QVERIFY(!m_ctl->sortDescending());
    m_ctl->setSortKey(QStringLiteral("duration"));
    QCOMPARE(m_ctl->sortLabel(), QStringLiteral("Duration"));
    QTRY_VERIFY(!m_ctl->songsLane()->loading());

    const int before = requestsTo(itemsPath());
    m_ctl->setSection(QStringLiteral("albums"));
    QCOMPARE(m_ctl->sortKey(), QStringLiteral("year"));
    QVERIFY(m_ctl->sortDescending());
    QCOMPARE(m_ctl->sortLabel(), QStringLiteral("Year"));
    QCOMPARE(requestsTo(itemsPath()), before); // still fresh: no refetch
    m_ctl->setSection(QStringLiteral("songs"));
    QCOMPARE(m_ctl->sortKey(), QStringLiteral("duration"));
}

void MusicBrowseControllerTest::filterChangeRefetchesVisibleAndInvalidatesHidden()
{
    m_ctl->open(kLibrary, QStringLiteral("albums"));
    QTRY_VERIFY(!m_ctl->albumsLane()->loading());
    m_ctl->setSection(QStringLiteral("artists"));
    QTRY_COMPARE(m_ctl->artists()->count(), 2);
    m_ctl->setSection(QStringLiteral("albums"));

    const int items = requestsTo(itemsPath());
    const int artists = requestsTo(kAlbumArtistsPath);
    m_ctl->setFavouritesOnly(true);
    QTRY_VERIFY(!m_ctl->albumsLane()->loading());

    QCOMPARE(requestsTo(itemsPath()), items + 1); // visible only; the unfiltered count is already known
    QCOMPARE(lastItems().queryItemValue("Filters"), QStringLiteral("IsFavorite"));
    QCOMPARE(requestsTo(kAlbumArtistsPath), artists); // hidden: not fetched
    QCOMPARE(m_ctl->artists()->count(), 0);            // …but invalidated

    m_ctl->setSection(QStringLiteral("artists"));
    QTRY_COMPARE(requestsTo(kAlbumArtistsPath), artists + 1);
    QTRY_COMPARE(m_ctl->artists()->count(), 2);
}

void MusicBrowseControllerTest::dropsRepliesForASupersededQuery()
{
    m_mock->addRoute("GET", itemsPath(), 200, page({albumJson("n1")}));
    m_mock->setRouteDelay("GET", itemsPath(), 400);
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"SortBy", "ProductionYear,PremiereDate,SortName"}}, 200,
                          page({albumJson("y1")}));

    QStringList seen;
    const auto connection = connect(m_ctl->albums(), &QAbstractItemModel::modelReset, this,
                                    [&] { seen += modelIds(m_ctl->albums()); });
    m_ctl->open(kLibrary, QStringLiteral("albums"));
    m_ctl->setSortKey(QStringLiteral("year")); // before the name reply lands

    QTRY_COMPARE(modelIds(m_ctl->albums()), QStringList{QStringLiteral("y1")});
    QTest::qWait(700); // the delayed name reply has arrived by now
    QCOMPARE(modelIds(m_ctl->albums()), QStringList{QStringLiteral("y1")});
    QVERIFY(!seen.contains(QStringLiteral("n1")));
    QVERIFY(!m_ctl->albumsLane()->loading());
    QVERIFY(m_ctl->albumsLane()->error().isEmpty());
    disconnect(connection);
}

void MusicBrowseControllerTest::loadMoreAppendsTheNextPage()
{
    m_mock->addRoute("GET", itemsPath(), 200, page(albums("a", 100), 250));
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"IncludeItemTypes", "MusicAlbum"}, {"StartIndex", "100"}}, 200,
                          page(albums("b", 100), 250));

    m_ctl->open(kLibrary, QStringLiteral("albums"));
    QTRY_COMPARE(m_ctl->albums()->count(), 100);
    QVERIFY(m_ctl->albums()->canLoadMore());

    m_ctl->loadMore();
    m_ctl->loadMore(); // ignored while the first is in flight
    QTRY_COMPARE(m_ctl->albums()->count(), 200);
    QCOMPARE(m_ctl->albums()->idAt(100), QStringLiteral("b0"));
    QCOMPARE(requestsTo(itemsPath(), Q{{"StartIndex", "100"}}), 1);
    QCOMPARE(m_ctl->resultCount(), 250);
}

void MusicBrowseControllerTest::openGenreSwitchesToAlbumsWithOnlyThatGenre()
{
    m_ctl->open(kLibrary, QStringLiteral("songs"));
    m_ctl->setFavouritesOnly(true);
    m_ctl->setDecade(1970);
    m_ctl->setSection(QStringLiteral("albums"));
    m_ctl->setLetter(QStringLiteral("M"));
    QTRY_VERIFY(!m_ctl->albumsLane()->loading());

    m_ctl->openGenre(QStringLiteral("g7"), QStringLiteral("Jazz"));
    QCOMPARE(m_ctl->section(), QStringLiteral("albums"));
    QCOMPARE(m_ctl->genreIds(), QStringList{QStringLiteral("g7")});
    QCOMPARE(m_ctl->decade(), 0);
    QVERIFY(!m_ctl->favouritesOnly());
    QCOMPARE(m_ctl->letter(), QString());
    QCOMPARE(m_ctl->genrePillText(), QStringLiteral("Genre: Jazz"));

    QTRY_VERIFY(!m_ctl->albumsLane()->loading());
    const QUrlQuery sent = lastItems();
    QCOMPARE(sent.queryItemValue("GenreIds"), QStringLiteral("g7"));
    QVERIFY(!sent.hasQueryItem("Filters"));
    QVERIFY(!sent.hasQueryItem("NameStartsWithOrGreater"));
}

void MusicBrowseControllerTest::countTextShowsUnfilteredArrowFiltered()
{
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"IncludeItemTypes", "MusicAlbum"}, {"Filters", "IsFavorite"}}, 200,
                          page(albums("f", 3), 38));
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"IncludeItemTypes", "MusicAlbum"}, {"Limit", "1"}}, 200,
                          page(albums("u", 1), 1204));

    // Opened already narrowed (a restored route), so the unfiltered total is unknown.
    m_ctl->restore(kLibrary, QStringLiteral("albums"),
                   QStringLiteral(R"({"v":1,"fav":true,"s":[{"k":"year","d":true,"l":""}]})"));
    QVERIFY(m_ctl->favouritesOnly());
    QCOMPARE(m_ctl->sortKey(), QStringLiteral("year"));

    QTRY_COMPARE(m_ctl->countText(), QStringLiteral("1,204 → 38 RECORDS · SORT: YEAR ↓"));
    QCOMPARE(m_ctl->resultCount(), 38);
    QCOMPARE(m_ctl->unfilteredCount(), 1204);
    QCOMPARE(requestsTo(itemsPath(), Q{{"Limit", "1"}}), 1);

    m_ctl->setFavouritesOnly(false);
    QTRY_COMPARE(m_ctl->countText(), QStringLiteral("3 RECORDS · SORT: YEAR ↓"));
    m_ctl->setSortKey(QStringLiteral("random"));
    QTRY_COMPARE(m_ctl->countText(), QStringLiteral("3 RECORDS · SORT: RANDOM"));
    QCOMPARE(requestsTo(itemsPath(), Q{{"Limit", "1"}}), 1);
}

void MusicBrowseControllerTest::formatAvailabilityFollowsCapabilities()
{
    m_ctl->open(kLibrary, QStringLiteral("albums"));
    const QList<QPair<QString, Section>> sections{{"albums", Section::Albums},
                                                  {"artists", Section::Artists},
                                                  {"songs", Section::Songs},
                                                  {"genres", Section::Genres},
                                                  {"playlists", Section::Playlists}};
    for (const auto &[key, section] : sections) {
        m_ctl->setSection(key);
        QCOMPARE(m_ctl->section(), key);
        QCOMPARE(m_ctl->formatAvailable(), MusicQueryTranslator::formatFilterable(section));
        QCOMPARE(m_ctl->formatOptions().size(), MusicQueryTranslator::formatOptions(section).size());
        const bool filterable = section == Section::Albums || section == Section::Artists
                                || section == Section::Songs;
        QCOMPARE(m_ctl->filtersAvailable(), filterable);
        QCOMPARE(m_ctl->decadeAvailable(), section == Section::Albums || section == Section::Songs);
    }

    const bool anyFormat = MusicQueryTranslator::formatFilterable(Section::Albums)
                           || MusicQueryTranslator::formatFilterable(Section::Songs);
    m_ctl->setFormat(QStringLiteral("lossless"));
    QCOMPARE(m_ctl->format(), anyFormat ? QStringLiteral("lossless") : QStringLiteral("any"));
    QCOMPARE(m_ctl->formatPillText(), anyFormat ? QStringLiteral("Format: Lossless") : QStringLiteral("Format"));
}

void MusicBrowseControllerTest::playAndShuffleUseTheScopeLabel()
{
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"IncludeItemTypes", "Audio"}, {"GenreIds", "g1"}}, 200,
                          page({trackJson("t1", "al", 1), trackJson("t2", "al", 2)}));

    m_ctl->open(kLibrary, QStringLiteral("albums"));
    QCOMPARE(m_ctl->scopeLabel(), QStringLiteral("All music"));
    m_ctl->openGenre(QStringLiteral("g1"), QStringLiteral("Jazz"));
    m_ctl->setFavouritesOnly(true);
    QCOMPARE(m_ctl->scopeLabel(), QStringLiteral("Jazz · Favourites"));

    m_ctl->playFiltered();
    QTRY_COMPARE(queueIds(queue()), (QStringList{"t1", "t2"}));
    QCOMPARE(queue()->sourceLabel(), QStringLiteral("Jazz · Favourites"));

    m_ctl->shuffleFiltered();
    QTRY_COMPARE(queue()->sourceLabel(), QStringLiteral("Shuffle · Jazz · Favourites"));
    QCOMPARE(queue()->rowCount(), 2);
    QVERIFY(requestsTo(itemsPath(), Q{{"IncludeItemTypes", "Audio"}, {"SortBy", "Random"}}) >= 1);
}

void MusicBrowseControllerTest::genreOptionsCarryCountsFromAllGenres()
{
    QJsonArray genres{QJsonObject{{"Id", "g1"}, {"Name", "Jazz"}, {"AlbumCount", 3}},
                      QJsonObject{{"Id", "g2"}, {"Name", "Rock"}, {"AlbumCount", 40}},
                      QJsonObject{{"Id", "g3"}, {"Name", "Folk"}, {"AlbumCount", 12}}};
    m_mock->addRoute("GET", "/MusicGenres", 200, page(genres));
    if (!emby::caps::kGenreItemCounts) {
        QJsonArray walk;
        auto tagged = [](const QString &id, const QString &genreId, const QString &name) {
            QJsonObject album = albumJson(id);
            album.insert("GenreItems", QJsonArray{QJsonObject{{"Id", genreId}, {"Name", name}}});
            return album;
        };
        for (int i = 0; i < 40; ++i)
            walk.append(tagged("r" + QString::number(i), "g2", "Rock"));
        for (int i = 0; i < 12; ++i)
            walk.append(tagged("f" + QString::number(i), "g3", "Folk"));
        for (int i = 0; i < 3; ++i)
            walk.append(tagged("j" + QString::number(i), "g1", "Jazz"));
        m_mock->addQueryRoute("GET", itemsPath(), Q{{"IncludeItemTypes", "MusicAlbum"}, {"Fields", "Genres"}}, 200,
                              page(walk));
    }

    m_ctl->open(kLibrary, QStringLiteral("albums"));
    QTRY_COMPARE(m_ctl->genreOptions().size(), 3);
    QVERIFY(!m_ctl->genreOptionsLoading());
    QVariantMap first = m_ctl->genreOptions().first().toMap();
    QCOMPARE(first.value("id").toString(), QStringLiteral("g2"));
    QCOMPARE(first.value("name").toString(), QStringLiteral("Rock"));
    QCOMPARE(first.value("count").toInt(), 40);
    QCOMPARE(first.value("subtitle").toString(), QStringLiteral("40 records"));
    QVERIFY(!first.value("selected").toBool());

    m_ctl->setGenres({QStringLiteral("g2")});
    QVERIFY(m_ctl->genreOptions().first().toMap().value("selected").toBool());
    QCOMPARE(m_ctl->genrePillText(), QStringLiteral("Genre: Rock"));

    m_ctl->setGenres({QStringLiteral("g1"), QStringLiteral("g2"), QStringLiteral("g1")});
    QCOMPARE(m_ctl->genreIds(), (QStringList{"g1", "g2"}));
    QCOMPARE(m_ctl->genrePillText(), QStringLiteral("Genres: 2"));
    QCOMPARE(m_ctl->activeFilterCount(), 1);
    m_ctl->toggleGenre(QStringLiteral("g1"));
    QCOMPARE(m_ctl->genreIds(), QStringList{QStringLiteral("g2")});
}

void MusicBrowseControllerTest::lettersAndSectionsCycle()
{
    m_ctl->open(kLibrary, QStringLiteral("albums"));
    QVERIFY(m_ctl->letterStripVisible());
    QCOMPARE(m_ctl->letters().size(), 27);
    QCOMPARE(m_ctl->letters().first(), QStringLiteral("#"));

    QVERIFY(m_ctl->jumpLetter(1));
    QCOMPARE(m_ctl->letter(), QStringLiteral("A"));
    QVERIFY(m_ctl->jumpLetter(-1));
    QCOMPARE(m_ctl->letter(), QStringLiteral("#"));
    QVERIFY(m_ctl->jumpLetter(-1)); // clamped, still handled
    QCOMPARE(m_ctl->letter(), QStringLiteral("#"));
    m_ctl->setLetter(QString());
    QVERIFY(m_ctl->jumpLetter(-1));
    QCOMPARE(m_ctl->letter(), QStringLiteral("Z"));
    m_ctl->toggleLetter(QStringLiteral("Z"));
    QCOMPARE(m_ctl->letter(), QString());

    m_ctl->setSortKey(QStringLiteral("year"));
    QVERIFY(!m_ctl->letterStripVisible());
    QVERIFY(!m_ctl->jumpLetter(1));
    m_ctl->setLetter(QStringLiteral("B"));
    QCOMPARE(m_ctl->letter(), QString()); // refused off a name sort

    QCOMPARE(m_ctl->cycleSection(1), QStringLiteral("artists"));
    QCOMPARE(m_ctl->section(), QStringLiteral("artists"));
    QCOMPARE(m_ctl->cycleSection(-1), QStringLiteral("albums"));
    QCOMPARE(m_ctl->cycleSection(-1), QStringLiteral("home"));
    QCOMPARE(m_ctl->section(), QStringLiteral("albums")); // Home is a route, not a section
    m_ctl->setSection(QStringLiteral("playlists"));
    QCOMPARE(m_ctl->cycleSection(1), QStringLiteral("home"));
    QCOMPARE(m_ctl->section(), QStringLiteral("playlists"));

    m_ctl->setSection(QStringLiteral("genres"));
    QVERIFY(!m_ctl->letterStripVisible());
    QCOMPARE(m_ctl->sortKey(), QStringLiteral("size"));
    QVERIFY(m_ctl->sortDescending());
    m_ctl->setSection(QStringLiteral("nonsense"));
    QCOMPARE(m_ctl->section(), QStringLiteral("genres"));
}

void MusicBrowseControllerTest::routeStateRoundTrips()
{
    m_ctl->open(kLibrary, QStringLiteral("albums"));
    m_ctl->setSortKey(QStringLiteral("year"));
    m_ctl->setSortDescending(false);
    m_ctl->setDecade(1990);
    m_ctl->setSection(QStringLiteral("artists"));
    m_ctl->setLetter(QStringLiteral("M"));
    m_ctl->setArtistMode(QStringLiteral("everyone"));
    m_ctl->setGenres({QStringLiteral("g1"), QStringLiteral("g2")});
    m_ctl->setUnplayedOnly(true);
    const QString state = m_ctl->routeState();
    QVERIFY(QJsonDocument::fromJson(state.toUtf8()).isObject());

    MusicBrowseController other(m_repo, m_playback);
    other.restore(kLibrary, QStringLiteral("artists"), state);
    QCOMPARE(other.routeState(), state);
    QCOMPARE(other.section(), QStringLiteral("artists"));
    QCOMPARE(other.letter(), QStringLiteral("M"));
    QCOMPARE(other.artistMode(), QStringLiteral("everyone"));
    QCOMPARE(other.decadePillText(), QStringLiteral("Decade: 90s"));
    other.setSection(QStringLiteral("albums"));
    QCOMPARE(other.sortKey(), QStringLiteral("year"));
    QVERIFY(!other.sortDescending());
    QCOMPARE(other.decade(), 1990);

    other.restore(kLibrary, QStringLiteral("songs"), QStringLiteral("not json"));
    QCOMPARE(other.section(), QStringLiteral("songs"));
    QCOMPARE(other.routeState(), state); // unreadable state keeps the live query
}

QTEST_GUILESS_MAIN(MusicBrowseControllerTest)
#include "tst_music_browse_controller.moc"
```

Add to `tests/CMakeLists.txt`, after `tst_music_playback`:

```cmake
strmqt_add_test(tst_music_browse_controller
    integration/tst_music_browse_controller.cpp
    mocks/MockEmbyServer.h mocks/MockEmbyServer.cpp mocks/FakePlayerBackend.h)
target_include_directories(tst_music_browse_controller PRIVATE mocks)
```

- [x] **Step 2: Run the test to verify it fails**

Run: `cmake --preset dev && cmake --build --preset dev --target tst_music_browse_controller`
Expected: a compile error (`app/controllers/music/MusicBrowseController.h` not found).

- [x] **Step 3: Write the header**

`src/app/controllers/music/MusicBrowseController.h`:

```cpp
#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVariantList>

#include <array>
#include <optional>

#include "app/controllers/music/MusicLane.h"
#include "app/music/MusicQueryTranslator.h"
#include "app/music/models/AlbumGridModel.h"
#include "app/music/models/ArtistGridModel.h"
#include "app/music/models/GenreBinModel.h"
#include "app/music/models/PlaylistGridModel.h"
#include "app/music/models/TrackListModel.h"
#include "core/Result.h"
#include "server/dto/music/MusicQuery.h"
#include "server/dto/music/MusicTypes.h"

namespace strmqt::music {

class MusicPlayback;
class MusicRepository;

// Browse (Crate spec §5): one music library read five ways. It owns the whole
// query: filters are shared across sections, while sort and letter are
// remembered per section. Each section is an independent lane.
//
// QML states intent (a section, a filter, a letter). This class decides what to
// fetch, what to invalidate, and every string the page shows. Replies carry the
// lane generation, the session epoch and the query they asked for; anything
// that no longer matches is dropped.
class MusicBrowseController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString libraryId READ libraryId NOTIFY libraryChanged)
    Q_PROPERTY(QString section READ section WRITE setSection NOTIFY sectionChanged)

    Q_PROPERTY(strmqt::music::AlbumGridModel *albums READ albums CONSTANT)
    Q_PROPERTY(strmqt::music::ArtistGridModel *artists READ artists CONSTANT)
    Q_PROPERTY(strmqt::music::TrackListModel *songs READ songs CONSTANT)
    Q_PROPERTY(strmqt::music::GenreBinModel *genres READ genres CONSTANT)
    Q_PROPERTY(strmqt::music::PlaylistGridModel *playlists READ playlists CONSTANT)
    Q_PROPERTY(strmqt::music::MusicLane *albumsLane READ albumsLane CONSTANT)
    Q_PROPERTY(strmqt::music::MusicLane *artistsLane READ artistsLane CONSTANT)
    Q_PROPERTY(strmqt::music::MusicLane *songsLane READ songsLane CONSTANT)
    Q_PROPERTY(strmqt::music::MusicLane *genresLane READ genresLane CONSTANT)
    Q_PROPERTY(strmqt::music::MusicLane *playlistsLane READ playlistsLane CONSTANT)
    Q_PROPERTY(strmqt::music::MusicLane *currentLane READ currentLane NOTIFY sectionChanged)

    Q_PROPERTY(QString sortKey READ sortKey WRITE setSortKey NOTIFY queryChanged)
    Q_PROPERTY(bool sortDescending READ sortDescending WRITE setSortDescending NOTIFY queryChanged)
    Q_PROPERTY(QVariantList availableSorts READ availableSorts NOTIFY sectionChanged)
    Q_PROPERTY(QString sortLabel READ sortLabel NOTIFY queryChanged)
    Q_PROPERTY(QString letter READ letter WRITE setLetter NOTIFY queryChanged)
    Q_PROPERTY(QStringList letters READ letters CONSTANT)
    Q_PROPERTY(bool letterStripVisible READ letterStripVisible NOTIFY queryChanged)

    Q_PROPERTY(QStringList genreIds READ genreIds NOTIFY queryChanged)
    Q_PROPERTY(QString genrePillText READ genrePillText NOTIFY queryChanged)
    Q_PROPERTY(int decade READ decade WRITE setDecade NOTIFY queryChanged)
    Q_PROPERTY(QString decadePillText READ decadePillText NOTIFY queryChanged)
    Q_PROPERTY(QString format READ format WRITE setFormat NOTIFY queryChanged)
    Q_PROPERTY(QString formatPillText READ formatPillText NOTIFY queryChanged)
    Q_PROPERTY(bool formatAvailable READ formatAvailable NOTIFY sectionChanged)
    Q_PROPERTY(bool decadeAvailable READ decadeAvailable NOTIFY sectionChanged)
    Q_PROPERTY(bool filtersAvailable READ filtersAvailable NOTIFY sectionChanged)
    Q_PROPERTY(bool favouritesOnly READ favouritesOnly WRITE setFavouritesOnly NOTIFY queryChanged)
    Q_PROPERTY(bool unplayedOnly READ unplayedOnly WRITE setUnplayedOnly NOTIFY queryChanged)
    Q_PROPERTY(QString artistMode READ artistMode WRITE setArtistMode NOTIFY queryChanged)
    Q_PROPERTY(bool filtered READ filtered NOTIFY queryChanged)
    Q_PROPERTY(int activeFilterCount READ activeFilterCount NOTIFY queryChanged)
    Q_PROPERTY(QString scopeLabel READ scopeLabel NOTIFY queryChanged)
    Q_PROPERTY(QString routeState READ routeState NOTIFY queryChanged)

    Q_PROPERTY(int resultCount READ resultCount NOTIFY countsChanged)
    Q_PROPERTY(int unfilteredCount READ unfilteredCount NOTIFY countsChanged)
    Q_PROPERTY(QString countText READ countText NOTIFY countsChanged)

    Q_PROPERTY(QVariantList genreOptions READ genreOptions NOTIFY genreOptionsChanged)
    Q_PROPERTY(bool genreOptionsLoading READ genreOptionsLoading NOTIFY genreOptionsChanged)
    Q_PROPERTY(bool genreOptionsFailed READ genreOptionsFailed NOTIFY genreOptionsChanged)
    Q_PROPERTY(QVariantList decadeOptions READ decadeOptions CONSTANT)
    Q_PROPERTY(QVariantList formatOptions READ formatOptions NOTIFY sectionChanged)

public:
    MusicBrowseController(MusicRepository *repository, MusicPlayback *playback, QObject *parent = nullptr);

    QString libraryId() const { return m_query.libraryId; }
    QString section() const;
    void setSection(const QString &section);

    AlbumGridModel *albums() const { return m_albums; }
    ArtistGridModel *artists() const { return m_artists; }
    TrackListModel *songs() const { return m_songs; }
    GenreBinModel *genres() const { return m_genres; }
    PlaylistGridModel *playlists() const { return m_playlists; }
    MusicLane *albumsLane() const { return m_lanes[0]; }
    MusicLane *artistsLane() const { return m_lanes[1]; }
    MusicLane *songsLane() const { return m_lanes[2]; }
    MusicLane *genresLane() const { return m_lanes[3]; }
    MusicLane *playlistsLane() const { return m_lanes[4]; }
    MusicLane *currentLane() const;
    QList<MusicModelBase *> models() const;

    QString sortKey() const;
    void setSortKey(const QString &key);
    bool sortDescending() const;
    void setSortDescending(bool descending);
    QVariantList availableSorts() const;
    QString sortLabel() const;
    QString letter() const;
    void setLetter(const QString &letter);
    QStringList letters() const;
    bool letterStripVisible() const;

    QStringList genreIds() const { return m_query.genreIds; }
    QString genrePillText() const;
    int decade() const { return m_query.decade; }
    void setDecade(int decade);
    QString decadePillText() const;
    QString format() const;
    void setFormat(const QString &key);
    QString formatPillText() const;
    bool formatAvailable() const;
    bool decadeAvailable() const;
    bool filtersAvailable() const;
    bool favouritesOnly() const { return m_query.favouritesOnly; }
    void setFavouritesOnly(bool on);
    bool unplayedOnly() const { return m_query.unplayedOnly; }
    void setUnplayedOnly(bool on);
    QString artistMode() const;
    void setArtistMode(const QString &mode);
    bool filtered() const;
    int activeFilterCount() const;
    QString scopeLabel() const;
    QString routeState() const;

    int resultCount() const;
    int unfilteredCount() const;
    QString countText() const;

    QVariantList genreOptions() const;
    bool genreOptionsLoading() const { return m_genreOptionsLoading; }
    bool genreOptionsFailed() const { return m_genreOptionsFailed; }
    QVariantList decadeOptions() const;
    QVariantList formatOptions() const;

    Q_INVOKABLE void open(const QString &libraryId, const QString &section);
    // Back/Forward: the section plus a routeState captured when the route was
    // left. Unreadable state keeps the live query.
    Q_INVOKABLE void restore(const QString &libraryId, const QString &section, const QString &state);
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void setGenres(const QStringList &genreIds);
    Q_INVOKABLE void toggleGenre(const QString &genreId);
    Q_INVOKABLE void clearGenres();
    Q_INVOKABLE void clearFilters();
    Q_INVOKABLE void openGenre(const QString &genreId, const QString &genreName);
    Q_INVOKABLE void playFiltered();
    Q_INVOKABLE void shuffleFiltered();
    Q_INVOKABLE void toggleLetter(const QString &letter);
    Q_INVOKABLE bool jumpLetter(int step);
    Q_INVOKABLE QString cycleSection(int step);
    Q_INVOKABLE void ensureGenreOptions();
    Q_INVOKABLE void collectAlbumTracks(const QString &albumId, const QString &name);

    void resetSessionState();

signals:
    void libraryChanged();
    void sectionChanged();
    void queryChanged();
    void countsChanged();
    void genreOptionsChanged();
    void albumTracksCollected(const QString &subject, const QStringList &trackIds);
    void actionFailed(const QString &message);

private:
    struct SectionState
    {
        QString sortKey;
        bool descending = false;
        QString letter;
        bool stale = true;
        int resultCount = -1;
        int unfilteredCount = -1;
        quint64 countGeneration = 0;
        bool countPending = false;
    };

    struct Snapshot
    {
        std::array<MusicQuery, 5> queries;
        std::array<MusicQuery, 5> bases;
    };

    const SectionState &stateOf(Section section) const { return m_sections[static_cast<int>(section)]; }
    SectionState &stateOf(Section section) { return m_sections[static_cast<int>(section)]; }
    MusicQuery queryFor(Section section) const;
    MusicQuery baseFor(Section section) const;
    MusicQuery playScope() const;
    bool narrowed(Section section) const;
    Snapshot snapshot() const;
    void commit(const Snapshot &before);
    bool moveTo(const QString &sectionKey);
    void resetQuery(const QString &libraryId);
    void resetForLibrary(const QString &libraryId);
    void applyRouteState(const QString &state);
    void invalidate(Section section);
    void clearModel(Section section);
    void ensureVisible();
    void ensureCounts();
    void fetch(Section section, int startIndex);
    void fetchGenres(quint64 generation, quint64 epoch, const MusicQuery &query, int startIndex);
    template<class T, class Model>
    void acceptPage(Section section, quint64 generation, quint64 epoch, const MusicQuery &query,
                    int startIndex, const Result<Page<T>> &result, Model *model);
    void acceptCount(Section section, quint64 countGeneration, quint64 epoch, const QString &error, int total);
    void retry(Section section);
    void loadGenreOptions();
    void rememberGenreNames(const QList<GenreBin> &genres);
    std::optional<SortKey> sortFor(Section section, const QString &key) const;
    QString countNoun(Section section, int count) const;
    QString decadeLabel(int decade) const;
    QString formatLabel(FormatFilter format) const;

    MusicRepository *m_repository;
    MusicPlayback *m_playback;
    AlbumGridModel *m_albums;
    ArtistGridModel *m_artists;
    TrackListModel *m_songs;
    GenreBinModel *m_genres;
    PlaylistGridModel *m_playlists;
    std::array<MusicModelBase *, 5> m_models{};
    std::array<MusicLane *, 5> m_lanes{};

    MusicQuery m_query; // library, shared filters, artist mode; sort lives per section
    Section m_section = Section::Albums;
    std::array<SectionState, 5> m_sections;
    quint64 m_epoch = 0;
    quint64 m_collectGeneration = 0;

    QList<GenreBin> m_genreOptions;
    QHash<QString, QString> m_genreNames; // id → name, from options and openGenre
    bool m_genreOptionsLoaded = false;
    bool m_genreOptionsLoading = false;
    bool m_genreOptionsFailed = false;
};

} // namespace strmqt::music
```

- [x] **Step 4: Write the implementation**

`src/app/controllers/music/MusicBrowseController.cpp`:

```cpp
#include "app/controllers/music/MusicBrowseController.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>

#include <algorithm>

#include "app/music/MusicFormat.h"
#include "app/music/MusicPlayback.h"
#include "app/music/MusicQueryTranslator.h"
#include "app/music/MusicRepository.h"
#include "core/Log.h"

namespace strmqt::music {

namespace {

constexpr int kSectionCount = 5;
constexpr int kPageSize = 100;
constexpr int kGenrePageSize = 48;
const auto kName = QStringLiteral("name");
const auto kRandom = QStringLiteral("random");
const auto kHome = QStringLiteral("home");

int slot(Section section) { return static_cast<int>(section); }

std::optional<Section> parseSection(const QString &key)
{
    if (key == QLatin1String("albums"))
        return Section::Albums;
    if (key == QLatin1String("artists"))
        return Section::Artists;
    if (key == QLatin1String("songs"))
        return Section::Songs;
    if (key == QLatin1String("genres"))
        return Section::Genres;
    if (key == QLatin1String("playlists"))
        return Section::Playlists;
    return std::nullopt;
}

QString keyOf(Section section)
{
    switch (section) {
    case Section::Albums: return QStringLiteral("albums");
    case Section::Artists: return QStringLiteral("artists");
    case Section::Songs: return QStringLiteral("songs");
    case Section::Genres: return QStringLiteral("genres");
    case Section::Playlists: return QStringLiteral("playlists");
    }
    return QStringLiteral("albums");
}

// Albums, Artists and Songs take the shared filters and a letter; Genres and
// Playlists take neither (the translator drops them too).
bool takesFilters(Section section)
{
    return section == Section::Albums || section == Section::Artists || section == Section::Songs;
}

std::optional<FormatFilter> parseFormat(const QString &key)
{
    if (key == QLatin1String("any"))
        return FormatFilter::Any;
    if (key == QLatin1String("lossless"))
        return FormatFilter::Lossless;
    if (key == QLatin1String("lossy"))
        return FormatFilter::Lossy;
    if (key == QLatin1String("hires"))
        return FormatFilter::HiRes;
    return std::nullopt;
}

QString formatKeyOf(FormatFilter format)
{
    switch (format) {
    case FormatFilter::Lossless: return QStringLiteral("lossless");
    case FormatFilter::Lossy: return QStringLiteral("lossy");
    case FormatFilter::HiRes: return QStringLiteral("hires");
    case FormatFilter::Any: break;
    }
    return QStringLiteral("any");
}

// A format is settable when some section can filter by it; the sections that
// cannot simply do not send it (queryFor).
bool formatSupported(FormatFilter format)
{
    return format == FormatFilter::Any
           || MusicQueryTranslator::formatOptions(Section::Albums).contains(format)
           || MusicQueryTranslator::formatOptions(Section::Songs).contains(format);
}

bool validDecade(int decade)
{
    return decade == kDecadeAny || decade == kDecadeEarlier
           || (decade >= 1950 && decade <= 2020 && decade % 10 == 0);
}

const QStringList &letterList()
{
    static const QStringList letters = [] {
        QStringList out{QStringLiteral("#")};
        for (char c = 'A'; c <= 'Z'; ++c)
            out.append(QString(QLatin1Char(c)));
        return out;
    }();
    return letters;
}

const QStringList &cycleKeys()
{
    static const QStringList keys{kHome, QStringLiteral("albums"), QStringLiteral("artists"),
                                  QStringLiteral("songs"), QStringLiteral("genres"),
                                  QStringLiteral("playlists")};
    return keys;
}

// Genres come whole from allGenres, so the Genres section sorts client-side.
// Size keeps names A–Z among equal counts in either direction.
QList<GenreBin> sortGenres(QList<GenreBin> genres, const QString &key, bool descending)
{
    const bool byName = key == kName;
    std::stable_sort(genres.begin(), genres.end(), [&](const GenreBin &a, const GenreBin &b) {
        if (!byName && a.recordCount != b.recordCount)
            return descending ? a.recordCount > b.recordCount : a.recordCount < b.recordCount;
        const int order = QString::localeAwareCompare(a.name, b.name);
        return byName && descending ? order > 0 : order < 0;
    });
    return genres;
}

} // namespace

MusicBrowseController::MusicBrowseController(MusicRepository *repository, MusicPlayback *playback,
                                             QObject *parent)
    : QObject(parent)
    , m_repository(repository)
    , m_playback(playback)
    , m_albums(new AlbumGridModel(this))
    , m_artists(new ArtistGridModel(this))
    , m_songs(new TrackListModel(this))
    , m_genres(new GenreBinModel(this))
    , m_playlists(new PlaylistGridModel(this))
{
    m_models = {m_albums, m_artists, m_songs, m_genres, m_playlists};
    for (int i = 0; i < kSectionCount; ++i) {
        const auto section = static_cast<Section>(i);
        m_lanes[i] = new MusicLane(m_models[i], this);
        connect(m_lanes[i], &MusicLane::retryRequested, this, [this, section] { retry(section); });
    }
    resetQuery(QString());
}

// ── Reading ─────────────────────────────────────────────────────────────────

QString MusicBrowseController::section() const { return keyOf(m_section); }
MusicLane *MusicBrowseController::currentLane() const { return m_lanes[slot(m_section)]; }

QList<MusicModelBase *> MusicBrowseController::models() const
{
    return QList<MusicModelBase *>(m_models.cbegin(), m_models.cend());
}

std::optional<SortKey> MusicBrowseController::sortFor(Section section, const QString &key) const
{
    const QList<SortKey> keys = MusicQueryTranslator::sortKeysFor(section);
    for (const SortKey &candidate : keys) {
        if (candidate.key == key)
            return candidate;
    }
    return std::nullopt;
}

QString MusicBrowseController::sortKey() const { return stateOf(m_section).sortKey; }
bool MusicBrowseController::sortDescending() const { return stateOf(m_section).descending; }

QVariantList MusicBrowseController::availableSorts() const
{
    QVariantList out;
    for (const SortKey &key : MusicQueryTranslator::sortKeysFor(m_section))
        out.append(QVariantMap{{QStringLiteral("key"), key.key}, {QStringLiteral("label"), key.label}});
    return out;
}

QString MusicBrowseController::sortLabel() const
{
    const auto key = sortFor(m_section, sortKey());
    return key ? key->label : QString();
}

QString MusicBrowseController::letter() const { return stateOf(m_section).letter; }
QStringList MusicBrowseController::letters() const { return letterList(); }

bool MusicBrowseController::letterStripVisible() const
{
    return takesFilters(m_section) && stateOf(m_section).sortKey == kName;
}

QString MusicBrowseController::genrePillText() const
{
    if (m_query.genreIds.isEmpty())
        return tr("Genre");
    const QString name = m_query.genreIds.size() == 1 ? m_genreNames.value(m_query.genreIds.first()) : QString();
    return name.isEmpty() ? tr("Genres: %1").arg(m_query.genreIds.size()) : tr("Genre: %1").arg(name);
}

QString MusicBrowseController::decadeLabel(int decade) const
{
    return decade == kDecadeEarlier ? tr("Earlier") : tr("%1s").arg(decade);
}

QString MusicBrowseController::decadePillText() const
{
    if (m_query.decade == kDecadeAny)
        return tr("Decade");
    if (m_query.decade == kDecadeEarlier)
        return tr("Decade: Earlier");
    return tr("Decade: %1s").arg(m_query.decade % 100, 2, 10, QLatin1Char('0'));
}

QString MusicBrowseController::format() const { return formatKeyOf(m_query.format); }

QString MusicBrowseController::formatLabel(FormatFilter format) const
{
    switch (format) {
    case FormatFilter::Lossless: return tr("Lossless");
    case FormatFilter::Lossy: return tr("Lossy");
    case FormatFilter::HiRes: return tr("Hi-res");
    case FormatFilter::Any: break;
    }
    return tr("Any");
}

QString MusicBrowseController::formatPillText() const
{
    return m_query.format == FormatFilter::Any ? tr("Format") : tr("Format: %1").arg(formatLabel(m_query.format));
}

bool MusicBrowseController::formatAvailable() const { return MusicQueryTranslator::formatFilterable(m_section); }
bool MusicBrowseController::decadeAvailable() const
{
    return m_section == Section::Albums || m_section == Section::Songs;
}
bool MusicBrowseController::filtersAvailable() const { return takesFilters(m_section); }

QString MusicBrowseController::artistMode() const
{
    return m_query.artistMode == ArtistMode::Everyone ? QStringLiteral("everyone") : QStringLiteral("albumArtists");
}

bool MusicBrowseController::filtered() const { return queryFor(m_section).hasFilters(); }

int MusicBrowseController::activeFilterCount() const
{
    const MusicQuery query = queryFor(m_section);
    return int(!query.genreIds.isEmpty()) + int(query.decade != kDecadeAny)
           + int(query.format != FormatFilter::Any) + int(query.favouritesOnly) + int(query.unplayedOnly);
}

QString MusicBrowseController::scopeLabel() const
{
    const MusicQuery scope = playScope();
    QStringList parts;
    if (scope.genreIds.size() == 1) {
        const QString name = m_genreNames.value(scope.genreIds.first());
        parts.append(name.isEmpty() ? tr("1 genre") : name);
    } else if (scope.genreIds.size() > 1) {
        parts.append(tr("%1 genres").arg(scope.genreIds.size()));
    }
    if (scope.decade == kDecadeEarlier)
        parts.append(tr("Before 1950"));
    else if (scope.decade > 0)
        parts.append(decadeLabel(scope.decade));
    if (scope.format != FormatFilter::Any)
        parts.append(formatLabel(scope.format));
    if (scope.favouritesOnly)
        parts.append(tr("Favourites"));
    if (scope.unplayedOnly)
        parts.append(tr("Unplayed"));
    return parts.isEmpty() ? tr("All music") : parts.join(QStringLiteral(" · "));
}

QString MusicBrowseController::routeState() const
{
    QJsonArray genres;
    for (const QString &id : m_query.genreIds)
        genres.append(id);
    QJsonArray sorts;
    for (const SectionState &state : m_sections) {
        sorts.append(QJsonObject{{QStringLiteral("k"), state.sortKey},
                                 {QStringLiteral("d"), state.descending},
                                 {QStringLiteral("l"), state.letter}});
    }
    const QJsonObject root{{QStringLiteral("v"), 1},
                           {QStringLiteral("g"), genres},
                           {QStringLiteral("d"), m_query.decade},
                           {QStringLiteral("f"), format()},
                           {QStringLiteral("fav"), m_query.favouritesOnly},
                           {QStringLiteral("un"), m_query.unplayedOnly},
                           {QStringLiteral("am"), artistMode()},
                           {QStringLiteral("s"), sorts}};
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

int MusicBrowseController::resultCount() const { return stateOf(m_section).resultCount; }
int MusicBrowseController::unfilteredCount() const { return stateOf(m_section).unfilteredCount; }

QString MusicBrowseController::countNoun(Section section, int count) const
{
    const bool one = count == 1;
    switch (section) {
    case Section::Albums: return one ? tr("RECORD") : tr("RECORDS");
    case Section::Artists: return one ? tr("ARTIST") : tr("ARTISTS");
    case Section::Songs: return one ? tr("SONG") : tr("SONGS");
    case Section::Genres: return one ? tr("GENRE") : tr("GENRES");
    case Section::Playlists: return one ? tr("PLAYLIST") : tr("PLAYLISTS");
    }
    return {};
}

QString MusicBrowseController::countText() const
{
    const SectionState &state = stateOf(m_section);
    if (state.resultCount < 0)
        return {};
    // The mono readout groups like the mockup, whatever the system locale.
    const QLocale en(QLocale::English, QLocale::UnitedStates);
    const QString noun = countNoun(m_section, state.resultCount);
    QString text = narrowed(m_section) && state.unfilteredCount >= 0
                       ? tr("%1 → %2 %3").arg(en.toString(state.unfilteredCount), en.toString(state.resultCount), noun)
                       : tr("%1 %2").arg(en.toString(state.resultCount), noun);
    text += tr(" · SORT: %1").arg(sortLabel().toUpper());
    if (state.sortKey != kRandom)
        text += state.descending ? QStringLiteral(" ↓") : QStringLiteral(" ↑");
    return text;
}

QVariantList MusicBrowseController::genreOptions() const
{
    QVariantList out;
    out.reserve(m_genreOptions.size());
    for (const GenreBin &genre : m_genreOptions) {
        out.append(QVariantMap{{QStringLiteral("id"), genre.id},
                               {QStringLiteral("name"), genre.name},
                               {QStringLiteral("count"), genre.recordCount},
                               {QStringLiteral("subtitle"), formatRecordCount(genre.recordCount)},
                               {QStringLiteral("selected"), m_query.genreIds.contains(genre.id)}});
    }
    return out;
}

QVariantList MusicBrowseController::decadeOptions() const
{
    QVariantList out;
    for (int decade = 1950; decade <= 2020; decade += 10)
        out.append(QVariantMap{{QStringLiteral("value"), decade}, {QStringLiteral("label"), decadeLabel(decade)}});
    out.append(QVariantMap{{QStringLiteral("value"), kDecadeEarlier}, {QStringLiteral("label"), decadeLabel(kDecadeEarlier)}});
    return out;
}

QVariantList MusicBrowseController::formatOptions() const
{
    QVariantList out;
    for (FormatFilter format : MusicQueryTranslator::formatOptions(m_section))
        out.append(QVariantMap{{QStringLiteral("key"), formatKeyOf(format)}, {QStringLiteral("label"), formatLabel(format)}});
    return out;
}

// ── Query model ─────────────────────────────────────────────────────────────

// What a section actually sends: the shared filters it can take plus its own
// sort and letter.
MusicQuery MusicBrowseController::queryFor(Section section) const
{
    const SectionState &state = stateOf(section);
    MusicQuery query = m_query;
    query.section = section;
    query.sortKey = state.sortKey;
    query.descending = state.descending;
    query.letter = takesFilters(section) && state.sortKey == kName ? state.letter : QString();
    if (!takesFilters(section)) {
        query.genreIds.clear();
        query.decade = kDecadeAny;
        query.format = FormatFilter::Any;
        query.favouritesOnly = false;
        query.unplayedOnly = false;
    }
    if (section == Section::Artists)
        query.decade = kDecadeAny; // the /Artists endpoints ignore years
    else
        query.artistMode = ArtistMode::AlbumArtists;
    if (!MusicQueryTranslator::formatFilterable(section))
        query.format = FormatFilter::Any;
    return query;
}

// The same section with nothing narrowing it: the left half of the readout.
MusicQuery MusicBrowseController::baseFor(Section section) const
{
    MusicQuery base = queryFor(section);
    base.genreIds.clear();
    base.decade = kDecadeAny;
    base.format = FormatFilter::Any;
    base.favouritesOnly = false;
    base.unplayedOnly = false;
    base.letter.clear();
    base.sortKey = kName;
    base.descending = false;
    return base;
}

// ▶ Play and ⇄ Shuffle: the Songs scope of the shared filters. Songs keeps its
// own order; any other section plays in album order.
MusicQuery MusicBrowseController::playScope() const
{
    MusicQuery scope = queryFor(Section::Songs);
    scope.letter.clear();
    if (m_section != Section::Songs) {
        scope.sortKey = QStringLiteral("album");
        scope.descending = false;
    }
    return scope;
}

bool MusicBrowseController::narrowed(Section section) const
{
    const MusicQuery query = queryFor(section);
    return query.hasFilters() || !query.letter.isEmpty();
}

MusicBrowseController::Snapshot MusicBrowseController::snapshot() const
{
    Snapshot snapshot;
    for (int i = 0; i < kSectionCount; ++i) {
        snapshot.queries[i] = queryFor(static_cast<Section>(i));
        snapshot.bases[i] = baseFor(static_cast<Section>(i));
    }
    return snapshot;
}

// Every mutation ends here: sections whose effective query moved are
// invalidated, the visible one refetches, and the readout is re-derived.
void MusicBrowseController::commit(const Snapshot &before)
{
    for (int i = 0; i < kSectionCount; ++i) {
        const auto section = static_cast<Section>(i);
        SectionState &state = m_sections[i];
        if (baseFor(section) != before.bases[i]) {
            state.unfilteredCount = -1;
            ++state.countGeneration;
            state.countPending = false;
        }
        if (queryFor(section) != before.queries[i])
            invalidate(section);
    }
    emit queryChanged();
    emit genreOptionsChanged(); // `selected` follows the query
    ensureVisible();
    ensureCounts();
    emit countsChanged();
}

bool MusicBrowseController::moveTo(const QString &sectionKey)
{
    const auto parsed = parseSection(sectionKey);
    if (!parsed || *parsed == m_section)
        return false;
    m_section = *parsed;
    return true;
}

void MusicBrowseController::resetQuery(const QString &libraryId)
{
    m_query = MusicQuery{};
    m_query.libraryId = libraryId;
    for (int i = 0; i < kSectionCount; ++i) {
        SectionState fresh;
        const QList<SortKey> keys = MusicQueryTranslator::sortKeysFor(static_cast<Section>(i));
        if (!keys.isEmpty()) {
            fresh.sortKey = keys.first().key;
            fresh.descending = keys.first().defaultDescending;
        }
        fresh.countGeneration = m_sections[i].countGeneration + 1;
        m_sections[i] = fresh;
    }
}

void MusicBrowseController::resetForLibrary(const QString &libraryId)
{
    ++m_epoch;
    resetQuery(libraryId);
    for (int i = 0; i < kSectionCount; ++i) {
        m_lanes[i]->reset();
        clearModel(static_cast<Section>(i));
    }
    m_genreOptions.clear();
    m_genreNames.clear();
    m_genreOptionsLoaded = false;
    m_genreOptionsLoading = false;
    m_genreOptionsFailed = false;
    emit genreOptionsChanged();
    loadGenreOptions();
}

void MusicBrowseController::applyRouteState(const QString &state)
{
    const QJsonDocument document = QJsonDocument::fromJson(state.toUtf8());
    if (!document.isObject() || document.object().value(QStringLiteral("v")).toInt() != 1)
        return;
    const QJsonObject root = document.object();

    QStringList genres;
    for (const QJsonValue &value : root.value(QStringLiteral("g")).toArray()) {
        const QString id = value.toString();
        if (!id.isEmpty() && !genres.contains(id))
            genres.append(id);
    }
    m_query.genreIds = genres;
    const int decade = root.value(QStringLiteral("d")).toInt(kDecadeAny);
    m_query.decade = validDecade(decade) ? decade : kDecadeAny;
    const auto format = parseFormat(root.value(QStringLiteral("f")).toString());
    m_query.format = format && formatSupported(*format) ? *format : FormatFilter::Any;
    m_query.favouritesOnly = root.value(QStringLiteral("fav")).toBool(false);
    m_query.unplayedOnly = root.value(QStringLiteral("un")).toBool(false);
    m_query.artistMode = root.value(QStringLiteral("am")).toString() == QLatin1String("everyone")
                             ? ArtistMode::Everyone
                             : ArtistMode::AlbumArtists;

    const QJsonArray sorts = root.value(QStringLiteral("s")).toArray();
    for (int i = 0; i < kSectionCount && i < sorts.size(); ++i) {
        const auto section = static_cast<Section>(i);
        const QJsonObject entry = sorts.at(i).toObject();
        SectionState &target = m_sections[i];
        if (const auto sort = sortFor(section, entry.value(QStringLiteral("k")).toString())) {
            target.sortKey = sort->key;
            target.descending = sort->key != kRandom
                                && entry.value(QStringLiteral("d")).toBool(sort->defaultDescending);
        }
        const QString letter = entry.value(QStringLiteral("l")).toString();
        target.letter = takesFilters(section) && target.sortKey == kName && letterList().contains(letter)
                            ? letter
                            : QString();
    }
}

// ── Mutations ───────────────────────────────────────────────────────────────

// Section signals go out after commit(): by then the section's lane has
// already begun (or kept) its fetch, so a view built on sectionChanged never
// sees a stale model that is about to be cleared.
void MusicBrowseController::open(const QString &libraryId, const QString &section)
{
    const Snapshot before = snapshot();
    const bool libraryMoved = libraryId != m_query.libraryId;
    if (libraryMoved)
        resetForLibrary(libraryId);
    const bool sectionMoved = moveTo(section);
    commit(before);
    if (libraryMoved)
        emit libraryChanged();
    if (libraryMoved || sectionMoved)
        emit sectionChanged();
}

void MusicBrowseController::restore(const QString &libraryId, const QString &section, const QString &state)
{
    const Snapshot before = snapshot();
    const bool libraryMoved = libraryId != m_query.libraryId;
    if (libraryMoved)
        resetForLibrary(libraryId);
    const bool sectionMoved = moveTo(section);
    applyRouteState(state);
    commit(before);
    if (libraryMoved)
        emit libraryChanged();
    if (libraryMoved || sectionMoved)
        emit sectionChanged();
}

void MusicBrowseController::setSection(const QString &section)
{
    if (!moveTo(section))
        return;
    ensureVisible();
    ensureCounts();
    emit sectionChanged();
    emit queryChanged();
    emit genreOptionsChanged();
    emit countsChanged();
}

void MusicBrowseController::setSortKey(const QString &key)
{
    const auto sort = sortFor(m_section, key);
    SectionState &state = stateOf(m_section);
    if (!sort || state.sortKey == key)
        return;
    const Snapshot before = snapshot();
    state.sortKey = sort->key;
    state.descending = sort->defaultDescending;
    if (key != kName)
        state.letter.clear();
    commit(before);
}

void MusicBrowseController::setSortDescending(bool descending)
{
    SectionState &state = stateOf(m_section);
    if (state.sortKey == kRandom || state.descending == descending)
        return;
    const Snapshot before = snapshot();
    state.descending = descending;
    commit(before);
}

void MusicBrowseController::setLetter(const QString &letter)
{
    SectionState &state = stateOf(m_section);
    if (!letter.isEmpty() && (!letterStripVisible() || !letterList().contains(letter)))
        return;
    if (state.letter == letter)
        return;
    const Snapshot before = snapshot();
    state.letter = letter;
    commit(before);
}

void MusicBrowseController::toggleLetter(const QString &letter)
{
    setLetter(letter == stateOf(m_section).letter ? QString() : letter);
}

bool MusicBrowseController::jumpLetter(int step)
{
    if (!letterStripVisible() || step == 0)
        return false;
    const QStringList &all = letterList();
    const int last = static_cast<int>(all.size()) - 1;
    const int current = all.indexOf(stateOf(m_section).letter);
    // From no letter, forward starts at A and back at Z; then it clamps, and a
    // clamped step is still handled so the trigger never falls through to paging.
    const int next = current < 0 ? (step > 0 ? 1 : last) : std::clamp(current + step, 0, last);
    setLetter(all.at(next));
    return true;
}

QString MusicBrowseController::cycleSection(int step)
{
    const QStringList &keys = cycleKeys();
    const int count = static_cast<int>(keys.size());
    const int current = keys.indexOf(section());
    const QString key = keys.at(((current + step) % count + count) % count);
    if (key != kHome)
        setSection(key);
    return key;
}

void MusicBrowseController::setGenres(const QStringList &genreIds)
{
    QStringList unique;
    for (const QString &id : genreIds) {
        if (!id.isEmpty() && !unique.contains(id))
            unique.append(id);
    }
    if (unique == m_query.genreIds)
        return;
    const Snapshot before = snapshot();
    m_query.genreIds = unique;
    commit(before);
}

void MusicBrowseController::toggleGenre(const QString &genreId)
{
    QStringList ids = m_query.genreIds;
    if (!ids.removeAll(genreId))
        ids.append(genreId);
    setGenres(ids);
}

void MusicBrowseController::clearGenres() { setGenres({}); }

void MusicBrowseController::setDecade(int decade)
{
    if (!validDecade(decade) || decade == m_query.decade)
        return;
    const Snapshot before = snapshot();
    m_query.decade = decade;
    commit(before);
}

void MusicBrowseController::setFormat(const QString &key)
{
    const auto format = parseFormat(key);
    if (!format || !formatSupported(*format) || *format == m_query.format)
        return;
    const Snapshot before = snapshot();
    m_query.format = *format;
    commit(before);
}

void MusicBrowseController::setFavouritesOnly(bool on)
{
    if (on == m_query.favouritesOnly)
        return;
    const Snapshot before = snapshot();
    m_query.favouritesOnly = on;
    commit(before);
}

void MusicBrowseController::setUnplayedOnly(bool on)
{
    if (on == m_query.unplayedOnly)
        return;
    const Snapshot before = snapshot();
    m_query.unplayedOnly = on;
    commit(before);
}

void MusicBrowseController::setArtistMode(const QString &mode)
{
    ArtistMode parsed;
    if (mode == QLatin1String("everyone"))
        parsed = ArtistMode::Everyone;
    else if (mode == QLatin1String("albumArtists"))
        parsed = ArtistMode::AlbumArtists;
    else
        return;
    if (parsed == m_query.artistMode)
        return;
    const Snapshot before = snapshot();
    m_query.artistMode = parsed;
    commit(before);
}

void MusicBrowseController::clearFilters()
{
    const Snapshot before = snapshot();
    m_query.genreIds.clear();
    m_query.decade = kDecadeAny;
    m_query.format = FormatFilter::Any;
    m_query.favouritesOnly = false;
    m_query.unplayedOnly = false;
    commit(before);
}

void MusicBrowseController::openGenre(const QString &genreId, const QString &genreName)
{
    if (genreId.isEmpty())
        return;
    const Snapshot before = snapshot();
    if (!genreName.isEmpty())
        m_genreNames.insert(genreId, genreName);
    // "Show me this genre": nothing else may hide part of it.
    m_query.genreIds = {genreId};
    m_query.decade = kDecadeAny;
    m_query.format = FormatFilter::Any;
    m_query.favouritesOnly = false;
    m_query.unplayedOnly = false;
    stateOf(Section::Albums).letter.clear();
    const bool sectionMoved = m_section != Section::Albums;
    m_section = Section::Albums;
    commit(before);
    if (sectionMoved)
        emit sectionChanged();
}

void MusicBrowseController::playFiltered()
{
    if (!m_query.libraryId.isEmpty())
        m_playback->playQuery(playScope(), scopeLabel());
}

void MusicBrowseController::shuffleFiltered()
{
    if (!m_query.libraryId.isEmpty())
        m_playback->shuffleQuery(playScope(), scopeLabel());
}

void MusicBrowseController::resetSessionState()
{
    resetForLibrary(QString());
    m_section = Section::Albums;
    ++m_collectGeneration;
    emit libraryChanged();
    emit sectionChanged();
    emit queryChanged();
    emit countsChanged();
}

// ── Fetching ────────────────────────────────────────────────────────────────

void MusicBrowseController::invalidate(Section section)
{
    SectionState &state = stateOf(section);
    state.stale = true;
    state.resultCount = -1;
    // reset() bumps the generation, so a page still in flight for the old
    // query lands on a dead generation and is dropped.
    m_lanes[slot(section)]->reset();
    clearModel(section);
}

void MusicBrowseController::clearModel(Section section)
{
    switch (section) {
    case Section::Albums: m_albums->clear(); break;
    case Section::Artists: m_artists->clear(); break;
    case Section::Songs: m_songs->clear(); break;
    case Section::Genres: m_genres->clear(); break;
    case Section::Playlists: m_playlists->clear(); break;
    }
}

void MusicBrowseController::ensureVisible()
{
    if (!m_query.libraryId.isEmpty() && stateOf(m_section).stale)
        fetch(m_section, 0);
}

void MusicBrowseController::loadMore()
{
    const int i = slot(m_section);
    if (m_query.libraryId.isEmpty() || m_lanes[i]->loading() || !m_models[i]->canLoadMore())
        return;
    fetch(m_section, m_models[i]->count());
}

void MusicBrowseController::retry(Section section)
{
    const MusicModelBase *model = m_models[slot(section)];
    if (model->count() == 0)
        fetch(section, 0);
    else if (model->canLoadMore())
        fetch(section, model->count());
}

void MusicBrowseController::fetch(Section section, int startIndex)
{
    if (m_query.libraryId.isEmpty())
        return;
    stateOf(section).stale = false;
    const MusicQuery query = queryFor(section);
    // Begin before asking: a cached repository future can resolve synchronously.
    const quint64 generation = m_lanes[slot(section)]->begin();
    const quint64 epoch = m_epoch;
    switch (section) {
    case Section::Albums:
        m_repository->browseAlbums(query, startIndex, kPageSize)
            .then(this, [this, generation, epoch, query, startIndex](Result<Page<Album>> result) {
                acceptPage(Section::Albums, generation, epoch, query, startIndex, result, m_albums);
            });
        break;
    case Section::Artists:
        m_repository->browseArtists(query, startIndex, kPageSize)
            .then(this, [this, generation, epoch, query, startIndex](Result<Page<Artist>> result) {
                acceptPage(Section::Artists, generation, epoch, query, startIndex, result, m_artists);
            });
        break;
    case Section::Songs:
        m_repository->browseTracks(query, startIndex, kPageSize)
            .then(this, [this, generation, epoch, query, startIndex](Result<Page<Track>> result) {
                acceptPage(Section::Songs, generation, epoch, query, startIndex, result, m_songs);
            });
        break;
    case Section::Genres:
        fetchGenres(generation, epoch, query, startIndex);
        break;
    case Section::Playlists:
        m_repository->browsePlaylists(query, startIndex, kPageSize)
            .then(this, [this, generation, epoch, query, startIndex](Result<Page<Playlist>> result) {
                acceptPage(Section::Playlists, generation, epoch, query, startIndex, result, m_playlists);
            });
        break;
    }
}

template<class T, class Model>
void MusicBrowseController::acceptPage(Section section, quint64 generation, quint64 epoch,
                                       const MusicQuery &query, int startIndex,
                                       const Result<Page<T>> &result, Model *model)
{
    MusicLane *lane = m_lanes[slot(section)];
    if (!lane->isCurrent(generation))
        return;
    if (epoch != m_epoch || query != queryFor(section) || startIndex != model->count()) {
        // A hidden section whose query moved while it was loading: settle the
        // lane and keep nothing. It refetches when it is shown.
        lane->succeed(generation);
        return;
    }
    if (!result.ok()) {
        lane->fail(generation, result.error);
        return;
    }
    const Page<T> &page = result.value;
    const int shown = startIndex + static_cast<int>(page.items.size());
    // Random is one sample, never paged: its model total is what arrived.
    const int total = query.sortKey == kRandom ? shown : page.totalRecordCount;
    if (startIndex == 0)
        model->setItems(page.items, total);
    else
        model->appendItems(page.items, total);

    SectionState &state = stateOf(section);
    state.resultCount = page.totalRecordCount;
    if (!narrowed(section))
        state.unfilteredCount = page.totalRecordCount;
    lane->succeed(generation);
    ensureCounts();
    emit countsChanged();
}

void MusicBrowseController::fetchGenres(quint64 generation, quint64 epoch, const MusicQuery &query, int startIndex)
{
    m_repository->allGenres(query.libraryId)
        .then(this, [this, generation, epoch, query, startIndex](Result<QList<GenreBin>> all) {
            MusicLane *lane = m_lanes[slot(Section::Genres)];
            if (!lane->isCurrent(generation))
                return;
            if (epoch != m_epoch || query != queryFor(Section::Genres) || startIndex != m_genres->count()) {
                lane->succeed(generation);
                return;
            }
            if (!all.ok()) {
                lane->fail(generation, all.error);
                return;
            }
            rememberGenreNames(all.value);
            const QList<GenreBin> sorted = sortGenres(all.value, query.sortKey, query.descending);
            const int total = static_cast<int>(sorted.size());
            // coverGenres never fails: a bin whose sample failed is drawn bare.
            m_repository->coverGenres(query.libraryId, sorted.mid(startIndex, kGenrePageSize))
                .then(this, [this, generation, epoch, query, startIndex, total](Result<QList<GenreBin>> covered) {
                    Page<GenreBin> page;
                    page.items = covered.value;
                    page.totalRecordCount = total;
                    page.startIndex = startIndex;
                    acceptPage(Section::Genres, generation, epoch, query, startIndex,
                               Result<Page<GenreBin>>::success(page), m_genres);
                });
        });
}

// The readout's left half when the view opened already narrowed (a restored
// route or openGenre): one Limit=1 request for the unnarrowed total.
void MusicBrowseController::ensureCounts()
{
    const Section section = m_section;
    SectionState &state = stateOf(section);
    if (m_query.libraryId.isEmpty() || !narrowed(section) || state.unfilteredCount >= 0 || state.countPending)
        return;
    const quint64 countGeneration = state.countGeneration;
    const quint64 epoch = m_epoch;
    const MusicQuery base = baseFor(section);
    auto accept = [this, section, countGeneration, epoch](const QString &error, int total) {
        acceptCount(section, countGeneration, epoch, error, total);
    };
    switch (section) {
    case Section::Albums:
        state.countPending = true;
        m_repository->browseAlbums(base, 0, 1).then(this, [accept](Result<Page<Album>> r) {
            accept(r.error, r.value.totalRecordCount);
        });
        break;
    case Section::Artists:
        state.countPending = true;
        m_repository->browseArtists(base, 0, 1).then(this, [accept](Result<Page<Artist>> r) {
            accept(r.error, r.value.totalRecordCount);
        });
        break;
    case Section::Songs:
        state.countPending = true;
        m_repository->browseTracks(base, 0, 1).then(this, [accept](Result<Page<Track>> r) {
            accept(r.error, r.value.totalRecordCount);
        });
        break;
    case Section::Genres:
    case Section::Playlists:
        break; // never narrowed
    }
}

void MusicBrowseController::acceptCount(Section section, quint64 countGeneration, quint64 epoch,
                                        const QString &error, int total)
{
    SectionState &state = stateOf(section);
    if (epoch != m_epoch || countGeneration != state.countGeneration)
        return;
    state.countPending = false;
    if (!error.isEmpty()) {
        // The readout degrades to the filtered count alone.
        qCWarning(logApp) << "music: unfiltered count failed" << error;
        return;
    }
    state.unfilteredCount = total;
    emit countsChanged();
}

// ── Genre options, album tracks ─────────────────────────────────────────────

void MusicBrowseController::rememberGenreNames(const QList<GenreBin> &genres)
{
    for (const GenreBin &genre : genres)
        m_genreNames.insert(genre.id, genre.name);
}

void MusicBrowseController::loadGenreOptions()
{
    if (m_query.libraryId.isEmpty() || m_genreOptionsLoading)
        return;
    m_genreOptionsLoading = true;
    m_genreOptionsFailed = false;
    emit genreOptionsChanged();
    const quint64 epoch = m_epoch;
    m_repository->allGenres(m_query.libraryId).then(this, [this, epoch](Result<QList<GenreBin>> result) {
        if (epoch != m_epoch)
            return;
        m_genreOptionsLoading = false;
        if (!result.ok()) {
            qCWarning(logApp) << "music: genre options failed" << result.error;
            m_genreOptionsFailed = true;
            emit genreOptionsChanged();
            return;
        }
        m_genreOptions = result.value;
        m_genreOptionsLoaded = true;
        rememberGenreNames(result.value);
        emit genreOptionsChanged();
        emit queryChanged(); // the pill and scope texts name genres
    });
}

void MusicBrowseController::ensureGenreOptions()
{
    if (!m_genreOptionsLoaded)
        loadGenreOptions();
}

void MusicBrowseController::collectAlbumTracks(const QString &albumId, const QString &name)
{
    if (albumId.isEmpty())
        return;
    const quint64 generation = ++m_collectGeneration;
    const quint64 epoch = m_epoch;
    m_repository->albumTracks(albumId).then(this, [this, generation, epoch, name](Result<QList<Track>> result) {
        if (generation != m_collectGeneration || epoch != m_epoch)
            return;
        if (!result.ok()) {
            emit actionFailed(tr("Couldn't read that album: %1").arg(result.error));
            return;
        }
        QStringList ids;
        for (const Track &track : std::as_const(result.value)) {
            if (!track.id.isEmpty())
                ids.append(track.id);
        }
        if (ids.isEmpty()) {
            emit actionFailed(tr("That album has no tracks to add."));
            return;
        }
        emit albumTracksCollected(name, ids);
    });
}

} // namespace strmqt::music
```

The header includes `MusicQueryTranslator.h` for `SortKey` (a plain header with no QtGui dependency).

Add to `strmqt_app` in `src/CMakeLists.txt`, next to the Phase 2 music controllers:

```cmake
    app/controllers/music/MusicBrowseController.h app/controllers/music/MusicBrowseController.cpp
```

- [x] **Step 5: Run the test to verify it passes**

Run: `cmake --build --preset dev --target tst_music_browse_controller && ctest --preset dev -R tst_music_browse_controller --output-on-failure`
Expected: PASS (12 tests).

- [x] **Step 6: Run the music suites together**

Run: `ctest --preset dev -R "tst_music" --output-on-failure`
Expected: PASS.

- [x] **Step 7: Commit**

```bash
git add src/app/controllers/music/MusicBrowseController.* src/CMakeLists.txt tests/integration/tst_music_browse_controller.cpp tests/CMakeLists.txt
git commit -m "feat(music): add the browse controller with per-section sort and lanes

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---
### Task 3: Application wiring and `MusicBrowseCtl`

**Files:**
- Modify: `src/app/Application.h` (forward declaration, accessor, member)
- Modify: `src/app/Application.cpp` (construction after the Phase 2 Home block; `teardownAuthenticatedSession`)
- Modify: `src/app/main.cpp` (the `MusicBrowseCtl` context property)

**Interfaces:**
- Consumes: `MusicBrowseController` (Task 2); `Application::musicRepository()`, `musicRelay()` and `musicPlayback()` (Phase 1); `MusicUserDataRelay::addModel`; the Phase 2 `m_musicHome` block.
- Produces:
  - `music::MusicBrowseController *Application::musicBrowse() const`
  - The QML context property `MusicBrowseCtl`.

This task only wires objects together, so it has no new unit test. The full suite and the self-test are its test. No page reads `MusicBrowseCtl` yet, so the self-test here only proves that start-up is clean.

- [x] **Step 1: Declare the member**

In `src/app/Application.h`, add to the `namespace music { … }` forward declarations (beside `class MusicHomeController;`):

```cpp
class MusicBrowseController;
```

Next to `musicHome()`:

```cpp
    music::MusicBrowseController *musicBrowse() const { return m_musicBrowse; }
```

Next to `m_musicHome`:

```cpp
    music::MusicBrowseController *m_musicBrowse = nullptr;
```

- [x] **Step 2: Construct, register the models, reset on sign-out**

In `src/app/Application.cpp`, add beside the `MusicHomeController.h` include:

```cpp
#include "controllers/music/MusicBrowseController.h"
```

Immediately after the Phase 2 block that ends with `m_musicRelay->addModel(model);` for `m_musicHome->models()`, add:

```cpp
    // Music Browse (Crate spec §5). Its five section models are patched in place
    // by the relay, so a favourite set on a sleeve anywhere shows here too.
    m_musicBrowse = new music::MusicBrowseController(m_musicRepository, m_musicPlayback, this);
    for (music::MusicModelBase *model : m_musicBrowse->models())
        m_musicRelay->addModel(model);
```

In `teardownAuthenticatedSession()`, directly after `m_musicHome->resetSessionState();`:

```cpp
    m_musicBrowse->resetSessionState();
```

- [x] **Step 3: Expose `MusicBrowseCtl`**

In `src/app/main.cpp`, add `#include "controllers/music/MusicBrowseController.h"` beside the `MusicHomeController.h` include. After the `MusicHomeCtl` line:

```cpp
    engine.rootContext()->setContextProperty(QStringLiteral("MusicBrowseCtl"), app.musicBrowse());
```

- [x] **Step 4: Build, test, self-test**

Run:

```bash
cmake --preset dev && cmake --build --preset dev && ctest --preset dev --output-on-failure
STRMQT_SELFTEST=1 QT_QPA_PLATFORM=offscreen QT_ASSUME_STDERR_HAS_CONSOLE=1 ./build/dev/strmqt; echo "exit=$?"
```

Expected: every test passes; the self-test prints no `selftest FAIL` line and ends with `exit=0`.

- [x] **Step 5: Commit**

```bash
git add src/app/Application.h src/app/Application.cpp src/app/main.cpp
git commit -m "$(cat <<'MSG'
feat(music): wire MusicBrowseController into the application

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
)"
```

---

### Task 4: `FilterPill`, `GenrePicker`, `CrateDividers`

**Files:**
- Create: `src/ui/music/FilterPill.qml`, `src/ui/music/GenrePicker.qml`, `src/ui/music/CrateDividers.qml`
- Modify: `src/ui/music/Music.cmake` (append the three files to its `qt_target_qml_sources(strmqt QML_FILES …)` list)

**Interfaces:**
- Consumes: `Theme` (including `crateKickerSize`, `crateKickerTracking`, `crateStripSize` from Phase 1), `FocusRing`, `StrmIcon`, `StrmSearchField`, `StrmButton`, `StrmScrollBar`.
- Produces, beyond the contract table:
  - `FilterPill`: `text`, `active`, `clearable` (default `active`), `toggle` (a ♡/Unplayed pill: filled when active), `iconName`, `accessibleName`; signals `activated()`, `cleared()`; `readonly property bool hovered`. Delete on a focused clearable pill emits `cleared()`. Backspace stays the shell's Back.
  - `GenrePicker`: `options` (`MusicBrowseCtl.genreOptions` rows), `loading`, `failed`, `opened`, `open()`, `close()`; signals `genresChosen(var ids)`, `dismissed()`, `retryRequested()`. The picked set is local until **Apply**, so browsing the list never refetches.
  - `CrateDividers`: `letters`, `currentLetter`; signal `letterChosen(string letter)`; `readonly property bool hovered`. One tab stop: Up/Down move a preview cursor, Return/Space/click choose.

These are QML-only, so there is no C++ unit test. Lint, build and the Task 5 self-test check them; Task 7 checks them visually.

- [x] **Step 1: Write `FilterPill.qml`**

```qml
import QtQuick
import StrmQt

// FilterPill — one Browse filter (Crate spec §5.2).
//
// Two kinds share one control. A menu pill (Genre, Decade, Format) opens a
// chooser on activation and, once set, names its value ("Decade: 70s") with a
// ✕ that clears it. A toggle pill (♡ Favourites, Unplayed) is filled while on.
//
// Controlled like StrmChip: it renders `active` and `text` and never changes
// them. The controller owns the query; the page routes both signals to it.
Item {
    id: pill

    property string text: ""
    property bool active: false
    property bool clearable: pill.active
    property bool toggle: false
    property string iconName: ""
    property string accessibleName: pill.text

    signal activated
    signal cleared

    readonly property bool hovered: hover.hovered
    readonly property bool pressed: tap.pressed
    readonly property bool filled: pill.toggle && pill.active

    readonly property color labelColor: {
        if (!pill.enabled)
            return Theme.textDisabled;
        if (pill.filled)
            return Theme.accentText;
        if (pill.active)
            return Theme.accentColor;
        if (pill.hovered || pill.activeFocus)
            return Theme.textPrimaryColor;
        return Theme.textSecondaryColor;
    }

    implicitHeight: Theme.scale(32)
    implicitWidth: row.implicitWidth + 2 * Theme.scale(12)
    activeFocusOnTab: pill.enabled

    Accessible.role: pill.toggle ? Accessible.CheckBox : Accessible.Button
    Accessible.name: pill.accessibleName
    Accessible.description: pill.clearable && !pill.toggle ? qsTr("Delete clears this filter") : ""
    Accessible.checkable: pill.toggle
    Accessible.checked: pill.toggle && pill.active
    Accessible.focusable: pill.enabled
    Accessible.focused: pill.activeFocus
    Accessible.pressed: pill.pressed
    Accessible.onPressAction: pill.activate()
    Accessible.onToggleAction: pill.activate()

    scale: !pill.enabled ? 1.0
         : pill.pressed ? Theme.pressScale
         : pill.activeFocus ? Theme.focusScale
         : pill.hovered ? Theme.hoverScale
         : 1.0

    Behavior on scale {
        NumberAnimation {
            duration: pill.activeFocus ? Theme.animFastMs : Theme.animInstant
            easing.type: pill.activeFocus ? Theme.easeStandard : Theme.easeInstant
        }
    }

    function activate(): void {
        if (pill.enabled)
            pill.activated();
    }

    Rectangle {
        id: bg

        anchors.fill: parent
        radius: Theme.radiusChip
        color: pill.filled && pill.enabled ? Theme.accentColor : "transparent"
        border.width: pill.filled ? 0 : 1
        border.color: pill.active && pill.enabled ? Theme.accentColor : Theme.hairline

        Behavior on color {
            ColorAnimation { duration: Theme.animFastMs; easing.type: Theme.easeStandard }
        }
    }

    Rectangle {
        anchors.fill: bg
        radius: bg.radius
        color: !pill.enabled ? "transparent"
             : pill.pressed ? Theme.pressTint
             : pill.hovered ? Theme.hoverTint
             : "transparent"
    }

    Row {
        id: row

        anchors.centerIn: parent
        spacing: Theme.scale(6)

        StrmIcon {
            anchors.verticalCenter: parent.verticalCenter
            visible: pill.iconName.length > 0
            name: pill.iconName
            color: pill.labelColor
            size: Theme.scale(14)
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: pill.text.length > 0
            text: pill.text
            color: pill.labelColor
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontSmall
            font.weight: pill.active ? Font.DemiBold : Font.Normal
        }

        // Its own hit area, so clearing a filter is never mistaken for opening
        // its chooser. Toggle pills clear by toggling, so they have none.
        Item {
            anchors.verticalCenter: parent.verticalCenter
            visible: pill.clearable && !pill.toggle
            width: Theme.scale(18)
            height: width

            StrmIcon {
                anchors.centerIn: parent
                name: "close"
                size: Theme.scale(12)
                color: clearHover.hovered ? Theme.textPrimaryColor : pill.labelColor
            }

            HoverHandler {
                id: clearHover
                enabled: pill.enabled
                cursorShape: Qt.PointingHandCursor
            }

            TapHandler {
                enabled: pill.enabled
                gesturePolicy: TapHandler.ReleaseWithinBounds
                onTapped: pill.cleared()
            }
        }
    }

    FocusRing {
        active: pill.activeFocus
        radius: Theme.radiusChip
    }

    HoverHandler {
        id: hover
        enabled: pill.enabled
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        id: tap
        enabled: pill.enabled
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: {
            pill.forceActiveFocus(Qt.MouseFocusReason);
            pill.activated();
        }
    }

    Keys.onReturnPressed: event => {
        if (!event.isAutoRepeat)
            pill.activate();
    }
    Keys.onEnterPressed: event => {
        if (!event.isAutoRepeat)
            pill.activate();
    }
    Keys.onSpacePressed: event => {
        if (!event.isAutoRepeat)
            pill.activate();
    }
    Keys.onDeletePressed: event => {
        event.accepted = pill.clearable && !pill.toggle;
        if (event.accepted)
            pill.cleared();
    }
}
```

Backspace is deliberately not bound: it is Back in the shell, and a pill must not swallow it.

- [x] **Step 2: Write `GenrePicker.qml`**

```qml
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import StrmQt

// GenrePicker — the Genre pill's chooser (Crate spec §5.2).
//
// An overlay and not a menu, for the reason PlaylistPicker is one: the
// measured library has 289 genres. Type to narrow, Up/Down to move, Return or
// a click to tick, Apply to commit. The ticks are local until Apply, so trying
// three genres costs one query, not six.
//
// It builds no strings: every row arrives from MusicBrowseCtl.genreOptions
// with its name, its count and its subtitle ("40 records").
FocusScope {
    id: picker

    property var options: []
    property bool loading: false
    property bool failed: false
    property bool opened: false
    property var picked: []
    property var rows: []

    signal genresChosen(var ids)
    signal dismissed
    signal retryRequested

    function open(): void {
        const selected = [];
        const source = picker.options || [];
        for (let i = 0; i < source.length; ++i) {
            if (source[i].selected === true)
                selected.push(String(source[i].id));
        }
        picker.picked = selected;
        picker.opened = true;
        field.text = "";
        picker.rebuild();
        field.forceActiveFocus(Qt.OtherFocusReason);
    }

    function close(): void {
        if (!picker.opened)
            return;
        picker.opened = false;
        picker.dismissed();
    }

    function rebuild(): void {
        const needle = field.text.trim().toLowerCase();
        const source = picker.options || [];
        const out = [];
        for (let i = 0; i < source.length; ++i) {
            if (needle.length === 0 || String(source[i].name).toLowerCase().indexOf(needle) >= 0)
                out.push(source[i]);
        }
        picker.rows = out;
        list.currentIndex = out.length > 0 ? Math.min(Math.max(list.currentIndex, 0), out.length - 1) : -1;
    }

    function isPicked(id): bool {
        return picker.picked.indexOf(String(id)) >= 0;
    }

    function toggle(index): void {
        if (index < 0 || index >= picker.rows.length)
            return;
        const id = String(picker.rows[index].id);
        const next = picker.picked.slice();
        const at = next.indexOf(id);
        if (at >= 0)
            next.splice(at, 1);
        else
            next.push(id);
        picker.picked = next;
    }

    function apply(): void {
        picker.genresChosen(picker.picked.slice());
        picker.close();
    }

    onOptionsChanged: {
        if (picker.opened)
            picker.rebuild();
    }

    anchors.fill: parent
    // `opened` as well as the opacity: open() focuses the field in the same
    // call, and an item that is still invisible then does not take focus.
    visible: picker.opened || picker.opacity > 0.01
    enabled: picker.opened
    opacity: picker.opened ? 1.0 : 0.0

    Behavior on opacity {
        NumberAnimation { duration: Theme.animFastMs; easing.type: Theme.easeStandard }
    }

    Keys.onEscapePressed: event => {
        picker.close();
        event.accepted = true;
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.scrimColor

        TapHandler {
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: picker.close()
        }
    }

    Rectangle {
        id: surface

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Math.round(parent.height * 0.12)
        width: Math.min(parent.width - Theme.pageMarginValue * 2, Theme.scale(560))
        height: head.height + list.height + hint.height + actions.height + Theme.spacingValue * 2
        radius: Theme.radiusPanel
        color: Theme.surfaceOverlay
        border.width: 1
        border.color: Theme.hairline

        TapHandler { gesturePolicy: TapHandler.ReleaseWithinBounds }

        Column {
            id: head

            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: Theme.spacingTight
            spacing: Theme.spacingTight

            Text {
                width: parent.width
                leftPadding: Theme.spacingTight
                topPadding: Theme.spacingTight
                text: picker.picked.length > 0
                      ? qsTr("Genres · %1 selected").arg(picker.picked.length)
                      : qsTr("Genres")
                color: Theme.textPrimaryColor
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            StrmSearchField {
                id: field

                width: parent.width
                implicitHeight: Theme.controlHeightLarge
                placeholderText: qsTr("Find a genre…")

                onTextEdited: picker.rebuild()
                onCleared: picker.rebuild()
                onEscapePressed: picker.close()

                KeyNavigation.tab: applyButton

                Keys.onUpPressed: {
                    if (list.count > 0)
                        list.currentIndex = Math.max(0, list.currentIndex - 1);
                }
                Keys.onDownPressed: {
                    if (list.count > 0)
                        list.currentIndex = Math.min(list.count - 1, list.currentIndex + 1);
                }
                Keys.onReturnPressed: event => {
                    if (!event.isAutoRepeat)
                        picker.toggle(list.currentIndex);
                }
                Keys.onEnterPressed: event => {
                    if (!event.isAutoRepeat)
                        picker.toggle(list.currentIndex);
                }
            }
        }

        ListView {
            id: list

            anchors.top: head.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: Theme.spacingTight
            height: Math.min(contentHeight, Theme.scale(400))
            clip: true
            model: picker.rows
            currentIndex: -1
            keyNavigationEnabled: false
            boundsBehavior: Flickable.StopAtBounds
            reuseItems: true

            ScrollBar.vertical: StrmScrollBar {}

            delegate: Item {
                id: row

                required property int index
                required property var modelData

                readonly property bool current: list.currentIndex === row.index
                readonly property bool ticked: picker.isPicked(row.modelData.id)

                width: list.width
                height: Theme.controlHeightLarge

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: Theme.scale(2)
                    radius: Theme.radiusChip
                    color: row.current ? Theme.hoverTint : "transparent"
                }

                Rectangle {
                    id: box

                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacingValue
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.scale(18)
                    height: width
                    radius: Theme.scale(3)
                    color: row.ticked ? Theme.accentColor : "transparent"
                    border.width: row.ticked ? 0 : 1
                    border.color: row.current ? Theme.textSecondaryColor : Theme.hairline

                    StrmIcon {
                        anchors.centerIn: parent
                        visible: row.ticked
                        name: "check"
                        size: Theme.scale(14)
                        color: Theme.accentText
                    }
                }

                Text {
                    anchors.left: box.right
                    anchors.leftMargin: Theme.spacingValue
                    anchors.right: count.left
                    anchors.rightMargin: Theme.spacingValue
                    anchors.verticalCenter: parent.verticalCenter
                    text: String(row.modelData.name)
                    color: row.ticked ? Theme.accentColor : Theme.textPrimaryColor
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontBodySize
                    elide: Text.ElideRight
                }

                Text {
                    id: count

                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingValue
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.modelData.subtitle !== undefined ? String(row.modelData.subtitle) : ""
                    color: Theme.textTertiary
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontCaption
                }

                // Hover previews the row and never takes the caret out of the field.
                HoverHandler {
                    id: rowHover
                    cursorShape: Qt.PointingHandCursor
                    onHoveredChanged: {
                        if (rowHover.hovered)
                            list.currentIndex = row.index;
                    }
                }

                ListView.onReused: {
                    if (rowHover.hovered)
                        list.currentIndex = row.index;
                }

                TapHandler {
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: picker.toggle(row.index)
                }
            }
        }

        Row {
            id: hint

            anchors.top: list.bottom
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingValue
            height: visible ? Theme.controlHeightLarge : 0
            visible: picker.rows.length === 0
            spacing: Theme.spacingValue

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: picker.loading ? qsTr("Loading genres…")
                    : picker.failed ? qsTr("Couldn't load genres.")
                    : (picker.options || []).length === 0 ? qsTr("This library has no genres.")
                    : qsTr("No genre matches.")
                color: Theme.textTertiary
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontSmall
            }

            StrmButton {
                anchors.verticalCenter: parent.verticalCenter
                visible: picker.failed && !picker.loading
                text: qsTr("Retry")
                iconName: "refresh"
                variant: "ghost"
                onClicked: picker.retryRequested()
            }
        }

        Row {
            id: actions

            anchors.top: hint.bottom
            anchors.topMargin: Theme.spacingTight
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingValue
            height: Theme.controlHeight
            spacing: Theme.spacingTight

            StrmButton {
                id: clearButton

                text: qsTr("Clear")
                variant: "ghost"
                enabled: picker.picked.length > 0
                onClicked: picker.picked = []

                KeyNavigation.right: applyButton
                KeyNavigation.up: field
            }

            StrmButton {
                id: applyButton

                text: qsTr("Apply")
                iconName: "check"
                variant: "primary"
                onClicked: picker.apply()

                KeyNavigation.left: clearButton
                KeyNavigation.up: field
                KeyNavigation.tab: field
            }
        }
    }
}
```

- [x] **Step 3: Write `CrateDividers.qml`**

```qml
pragma ComponentBehavior: Bound
import QtQuick
import StrmQt

// CrateDividers — the A–Z tabs down the right edge of a crate (Crate spec §5.1).
//
// One tab stop, like FilterBar's strip: 27 focusable letters would flood the
// focus chain. Up/Down move a preview cursor; only Return, Space or a click
// chooses. The chosen letter protrudes left in amber, the way a divider card
// stands proud of the records filed behind it.
//
// It stores nothing: `currentLetter` is the controller's, and a pick goes out
// as letterChosen() for the page to hand back (MusicBrowseCtl.toggleLetter).
FocusScope {
    id: dividers

    property var letters: []
    property string currentLetter: ""
    property int cursor: Math.max(0, dividers.letters.indexOf(dividers.currentLetter))

    signal letterChosen(string letter)

    readonly property bool hovered: hover.hovered
    readonly property real cellHeight: dividers.letters.length > 0
                                       ? Math.max(Theme.scale(12), Math.min(Theme.scale(22),
                                                  dividers.height / dividers.letters.length))
                                       : 0

    implicitWidth: Theme.scale(30)
    activeFocusOnTab: dividers.letters.length > 0

    Accessible.role: Accessible.List
    Accessible.name: qsTr("Jump to letter")
    Accessible.focusable: true
    Accessible.focused: dividers.activeFocus

    onCurrentLetterChanged: dividers.cursor = Math.max(0, dividers.letters.indexOf(dividers.currentLetter))

    function choose(index): void {
        if (index < 0 || index >= dividers.letters.length)
            return;
        dividers.cursor = index;
        dividers.letterChosen(dividers.letters[index]);
    }

    Column {
        id: column

        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter

        Repeater {
            model: dividers.letters

            delegate: Item {
                id: tab

                required property int index
                required property var modelData

                readonly property bool chosen: String(tab.modelData) === dividers.currentLetter
                readonly property bool previewed: dividers.activeFocus && dividers.cursor === tab.index

                width: dividers.width
                height: dividers.cellHeight

                Rectangle {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    height: parent.height - Theme.scale(2)
                    // The chosen divider stands proud of the rest.
                    width: tab.chosen ? parent.width : parent.width - Theme.scale(8)
                    radius: Theme.scale(2)
                    color: tab.chosen ? Theme.accentColor
                         : tab.previewed ? Theme.surfaceRaisedColor
                         : Theme.surfaceColor
                    border.width: tab.previewed && !tab.chosen ? 1 : 0
                    border.color: Theme.accentColor

                    Behavior on width {
                        NumberAnimation { duration: Theme.animFastMs; easing.type: Theme.easeStandard }
                    }

                    Text {
                        anchors.centerIn: parent
                        text: String(tab.modelData)
                        color: tab.chosen ? Theme.accentText
                             : tab.previewed ? Theme.textPrimaryColor
                             : Theme.textSecondaryColor
                        font.family: Theme.fontMono
                        font.pixelSize: Math.min(Theme.crateKickerSize, Math.round(dividers.cellHeight * 0.7))
                        font.weight: tab.chosen ? Font.Bold : Font.Normal
                    }
                }

                TapHandler {
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: {
                        dividers.forceActiveFocus(Qt.MouseFocusReason);
                        dividers.choose(tab.index);
                    }
                }
            }
        }
    }

    FocusRing {
        active: dividers.activeFocus
        radius: Theme.radiusChip
        inset: -Theme.scale(2)
    }

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }

    Keys.onUpPressed: event => {
        dividers.cursor = Math.max(0, dividers.cursor - 1);
        event.accepted = true;
    }
    Keys.onDownPressed: event => {
        dividers.cursor = Math.min(dividers.letters.length - 1, dividers.cursor + 1);
        event.accepted = true;
    }
    Keys.onReturnPressed: event => {
        if (!event.isAutoRepeat)
            dividers.choose(dividers.cursor);
    }
    Keys.onEnterPressed: event => {
        if (!event.isAutoRepeat)
            dividers.choose(dividers.cursor);
    }
    Keys.onSpacePressed: event => {
        if (!event.isAutoRepeat)
            dividers.choose(dividers.cursor);
    }
    // A typed letter chooses directly, as FilterBar's strip does.
    Keys.onPressed: event => {
        if (event.text.length !== 1 || event.modifiers & (Qt.ControlModifier | Qt.AltModifier))
            return;
        const upper = event.text.toUpperCase();
        const index = /^[0-9]$/.test(upper) ? 0 : dividers.letters.indexOf(upper);
        if (index >= 0) {
            dividers.choose(index);
            event.accepted = true;
        }
    }
}
```

`#` is first in `MusicBrowseCtl.letters`, so a typed digit chooses index 0.

- [x] **Step 4: Register the files**

In `src/ui/music/Music.cmake`, append to the `qt_target_qml_sources(strmqt QML_FILES` list, after the Phase 2 files:

```cmake
    ui/music/FilterPill.qml
    ui/music/GenrePicker.qml
    ui/music/CrateDividers.qml
```

- [x] **Step 5: Build and lint**

Run:

```bash
cmake --preset dev && cmake --build --preset dev
bash scripts/check-qmllint-baseline.sh build/dev
```

Expected: the build succeeds. The lint script reports no line in a fatal category (`is not a type`, `was not found`, `unavailable`, `incompatible-type`) for the three new files. These controls read no context property, so they should add no lint lines at all. If one does appear, fix it here rather than baselining it.

- [x] **Step 6: Commit**

```bash
git add src/ui/music/FilterPill.qml src/ui/music/GenrePicker.qml src/ui/music/CrateDividers.qml src/ui/music/Music.cmake
git commit -m "$(cat <<'MSG'
feat(music): add filter pills, the genre picker and crate dividers

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
)"
```

---

### Task 5: `MusicBrowsePage.qml`

**Files:**
- Create: `src/ui/pages/MusicBrowsePage.qml`
- Modify: `src/CMakeLists.txt` (add `ui/pages/MusicBrowsePage.qml` to the page list, after `ui/pages/MusicHomePage.qml`)
- Modify: `src/ui/Main.qml` (a `musicBrowseComponent` and a self-test entry only; routes are Task 6)

**Interfaces:**
- Consumes:
  - `MusicBrowseCtl` (Tasks 2–3), `MusicPlay`, `Actions`, `PlayerCtl`, `PlaylistCtl`, `App`
  - Phase 2 controls: `SectionStrip`, `CrateSleeve`, `CratePortrait`, `GenreBinTile`, `CoverCollage`; the `cardComponent` protocol on `StrmGrid`
  - Task 4 controls: `FilterPill`, `GenrePicker`, `CrateDividers`
  - Existing: `PageHeader`, `StrmGrid`, `TrackTable`, `TrackRow`, `SelectionBar`, `ItemMenu`, `PlaylistPicker`, `StrmMenu`, `StrmSelect`, `StrmButton`, `StrmIconButton`, `StrmSearchField`, `StrmPanel`, `StrmToastHost`, `LoadingState`, `EmptyState`, `MappedShortcut`, `ModelUtils`
- Produces: `MusicBrowsePage` with:
  - `property string libraryId`, `property string libraryName`, `property string initialSection` (the reconstruction properties)
  - `signal homeRequested()`: the strip's or a shoulder's **Home**; `Main.qml` routes it
  - `function cycleTab(step): bool` (always true: sections wrap through Home)
  - `function jumpLetter(step): bool` (`MusicBrowseCtl.jumpLetter`, else the view's `pageBy`)
  - One `navigationFocusKey` per section view: `musicBrowse-albums`, `musicBrowse-artists`, `musicBrowse-songs`, `musicBrowse-genres`, `musicBrowse-playlists`

Layout, top to bottom:
1. `PageHeader`: the library name.
2. `SectionStrip`: Home · Albums · Artists · Songs · Genres · Playlists.
3. The pill row:
   - left (clipped): Genre, Decade, Format, ♡ Favourites, Unplayed, **Clear all** (two or more filters); on Artists, *Album artists* / *Everyone*
   - right: the mono count readout, the sort select, the direction button, **▶ Play**, **⇄ Shuffle**
   - The pill row's left half, Play and Shuffle are hidden where `filtersAvailable` is false (contract note 7).
4. The Songs selection bar (Songs only).
5. One `Loader` over the five section views, with `CrateDividers` on its right while `letterStripVisible`.

This is a QML-only task, so there is no C++ unit test. The self-test constructs the page against the real context properties; lint checks it statically; Task 7 checks it visually.

- [x] **Step 1: Write the page**

`src/ui/pages/MusicBrowsePage.qml`:

```qml
pragma ComponentBehavior: Bound
import QtQuick
import StrmQt

// MusicBrowsePage — one music library read five ways (Crate spec §5).
//
// MusicBrowseCtl owns everything with a meaning: the section, the filters
// shared across Albums, Artists and Songs, the sort each section remembers,
// the letter, every count and every label. This page states intent and draws
// what it is told. Nothing here builds a query or a display string beyond a
// button caption.
//
// One Loader holds the visible section; the other four are Components, never
// live trees. Each keeps a navigation snapshot so switching back lands on the
// same record, and each has its own navigationFocusKey so Back/Forward restores
// focus exactly.
//
// Navigation contract: the page pushes nothing. Cards open through
// Actions.openDetails; Home is homeRequested(), which Main.qml routes.
FocusScope {
    id: page

    // ── Scope (set by Main.qml; the reconstruction properties) ─────────────
    property string libraryId: ""
    property string libraryName: ""
    property string initialSection: "albums"

    signal homeRequested

    // ── View state ─────────────────────────────────────────────────────────
    // Decoupled from MusicBrowseCtl.section so the old view can be snapshotted
    // before the Loader swaps it.
    property string loadedSection: "albums"
    property bool viewReady: false
    property var viewStates: ({})
    readonly property var activeView: viewLoader.item
    readonly property var lane: MusicBrowseCtl.currentLane
    readonly property bool laneLoading: page.lane !== null && page.lane.loading
    readonly property string laneError: page.lane !== null ? page.lane.error : ""
    readonly property bool songsShown: page.loadedSection === "songs"
    readonly property bool playlistsShown: page.loadedSection === "playlists"
    readonly property int shownCount: page.activeView && page.activeView.count !== undefined
                                      ? Number(page.activeView.count) : 0
    // The Playlists view always has its New playlist tile to stand on.
    readonly property bool contentFocusable: page.shownCount > 0 || page.playlistsShown

    readonly property string songRowFormat: qsTr("%1 · %2")
    readonly property int songRowHeight: Theme.scale(52)
    readonly property int songNumberColumn: Theme.scale(46)
    readonly property int songDurationColumn: Theme.scale(64)
    readonly property int songVerbsColumn: Theme.scale(72)
    readonly property int songArtistColumn:
        page.songsView() && page.songsView().showArtistColumn ? Theme.scale(220) : 0

    Component.onCompleted: {
        // Main.qml opens the controller before it pushes; this covers a page
        // constructed with a scope of its own. The self-test passes none, and
        // then nothing is fetched.
        if (page.libraryId.length > 0 && MusicBrowseCtl.libraryId !== page.libraryId)
            MusicBrowseCtl.open(page.libraryId, page.initialSection);
        page.loadedSection = MusicBrowseCtl.section;
        page.viewReady = true;
    }

    Connections {
        target: MusicBrowseCtl

        // The controller has already started the new section's lane; only
        // now may the Loader build delegates over its model.
        function onSectionChanged() {
            if (page.loadedSection === MusicBrowseCtl.section)
                return;
            page.captureActiveView();
            page.loadedSection = MusicBrowseCtl.section;
        }

        function onAlbumTracksCollected(subject, trackIds) {
            playlistPicker.show(subject, trackIds);
        }

        function onActionFailed(message) {
            browseToasts.show(message, "error");
        }
    }

    function captureActiveView(): void {
        const view = page.activeView;
        if (!view || typeof view.navigationFocusSnapshot !== "function")
            return;
        const next = Object.assign({}, page.viewStates);
        next[page.loadedSection] = view.navigationFocusSnapshot();
        page.viewStates = next;
    }

    function restoreActiveView(): void {
        const view = page.activeView;
        const state = page.viewStates[page.loadedSection];
        if (!view || !state || state.valid !== true
                || typeof view.restoreNavigationFocus !== "function")
            return;
        view.restoreNavigationFocus(String(state.identity), Number(state.index));
    }

    function focusContent(): void {
        if (page.contentFocusable && page.activeView)
            page.activeView.forceActiveFocus(Qt.TabFocusReason);
    }

    // ── Item helpers ───────────────────────────────────────────────────────
    function modelFor(key): var {
        return key === "artists" ? MusicBrowseCtl.artists
             : key === "songs" ? MusicBrowseCtl.songs
             : key === "genres" ? MusicBrowseCtl.genres
             : key === "playlists" ? MusicBrowseCtl.playlists
             : MusicBrowseCtl.albums;
    }

    function itemAt(index): var {
        const model = page.modelFor(page.loadedSection);
        if (!model || index < 0 || index >= model.count)
            return null;
        return model.get(index);
    }

    function idOf(item): string {
        return item && item.itemId !== undefined ? String(item.itemId) : "";
    }

    function nameOf(item): string {
        return item && item.name !== undefined ? String(item.name) : "";
    }

    function songsView(): var {
        return page.songsShown ? page.activeView : null;
    }

    function focusedItem(): var {
        const view = page.activeView;
        if (!view || view.currentIndex === undefined)
            return null;
        return page.itemAt(view.currentIndex);
    }

    function openItem(index): void {
        const item = page.itemAt(index);
        if (page.idOf(item).length === 0)
            return;
        if (page.loadedSection === "genres")
            MusicBrowseCtl.openGenre(page.idOf(item), page.nameOf(item));
        else
            Actions.openDetails(item);
    }

    // ▶ on a card. A record plays in disc order, an artist starts a radio seeded
    // on them, a playlist opens (its order is the playlist).
    function playItem(index): void {
        const item = page.itemAt(index);
        const id = page.idOf(item);
        if (id.length === 0)
            return;
        if (page.loadedSection === "albums")
            MusicPlay.playAlbum(id, page.nameOf(item));
        else if (page.loadedSection === "artists")
            MusicPlay.radio(id, page.nameOf(item));
        else if (page.loadedSection === "playlists")
            Actions.openDetails(item);
    }

    function playSongFrom(index): void {
        const model = MusicBrowseCtl.songs;
        if (index < 0 || index >= model.count)
            return;
        Actions.playAllFrom(ModelUtils.drain(model), index);
    }

    function fileSongSelection(): void {
        const table = page.songsView();
        if (!table)
            return;
        const ids = table.selectedIds();
        if (ids.length > 0)
            playlistPicker.show(page.libraryName.length > 0 ? page.libraryName : qsTr("Music"), ids);
    }

    readonly property string nowPlayingId: {
        const queue = PlayerCtl.queue;
        if (!queue || queue.currentIndex < 0)
            return "";
        const current = queue.currentItem();
        return current && current.itemId !== undefined ? String(current.itemId) : "";
    }

    // ── Shell hooks (Main.qml) ─────────────────────────────────────────────
    // LB/RB walk Home · Albums · … · Playlists and wrap, so the pair is never
    // a dead button.
    function cycleTab(step): bool {
        const key = MusicBrowseCtl.cycleSection(step);
        if (key === "home")
            page.homeRequested();
        return true;
    }

    // LT/RT step the crate dividers while they show (name sort) and page the
    // view otherwise.
    function jumpLetter(step): bool {
        if (MusicBrowseCtl.jumpLetter(step))
            return true;
        const view = page.activeView;
        return view !== null && typeof view.pageBy === "function" && view.pageBy(step) === true;
    }

    // ── The music input context ────────────────────────────────────────────
    MappedShortcut {
        actionId: "music.playPause"
        fallback: ["Space"]
        active: App.interactionContext === "music" && PlayerCtl.active
        onActivated: PlayerCtl.togglePause()
    }

    MappedShortcut {
        actionId: "music.shuffleAll"
        fallback: ["S"]
        active: App.interactionContext === "music" && MusicBrowseCtl.libraryId.length > 0
                && MusicBrowseCtl.filtersAvailable
        onActivated: MusicBrowseCtl.shuffleFiltered()
    }

    MappedShortcut {
        actionId: "music.favorite"
        fallback: ["L"]
        active: App.interactionContext === "music" && page.loadedSection !== "genres"
        onActivated: {
            const table = page.songsView();
            if (table && table.selectionCount > 0) {
                Actions.setFavoriteAll(table.selectedIds(), true);
                return;
            }
            const item = page.focusedItem();
            if (item)
                Actions.toggleFavorite(item);
        }
    }

    MappedShortcut {
        actionId: "music.instantMix"
        fallback: ["R"]
        active: App.interactionContext === "music"
                && page.loadedSection !== "playlists" && page.loadedSection !== "genres"
        onActivated: {
            const item = page.focusedItem();
            if (item)
                Actions.instantMix(item);
        }
    }

    // ── Header and sections ────────────────────────────────────────────────
    PageHeader {
        id: header

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: Theme.pageMarginValue
        anchors.rightMargin: Theme.pageMarginValue
        anchors.topMargin: Theme.spacingValue
        height: header.implicitHeight
        title: page.libraryName.length > 0 ? page.libraryName : qsTr("Music")
    }

    SectionStrip {
        id: strip

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        anchors.leftMargin: Theme.pageMarginValue
        anchors.rightMargin: Theme.pageMarginValue
        anchors.topMargin: Theme.spacingTight
        currentKey: MusicBrowseCtl.section
        // Empty or loading, the strip holds the keyboard (see the Loader).
        focus: !page.contentFocusable

        onSectionChosen: key => {
            if (key === "home")
                page.homeRequested();
            else
                MusicBrowseCtl.section = key;
        }

        Keys.onDownPressed: event => {
            if (pillBar.entryItem) {
                pillBar.entryItem.forceActiveFocus(Qt.TabFocusReason);
                event.accepted = true;
            }
        }
    }

    // ── Pill row ───────────────────────────────────────────────────────────
    // One scope that owns Left/Right across both halves, in reading order, so
    // no control needs a KeyNavigation pair that goes stale when a pill hides.
    FocusScope {
        id: pillBar

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: strip.bottom
        anchors.leftMargin: Theme.pageMarginValue
        anchors.rightMargin: Theme.pageMarginValue
        anchors.topMargin: Theme.spacingValue
        height: Theme.controlHeight

        readonly property var order: [genrePill, decadePill, formatPill, favouritesPill,
                                      unplayedPill, clearAllButton, albumArtistsPill,
                                      everyonePill, sortSelect, directionButton, playButton,
                                      shuffleButton]
        readonly property Item entryItem: {
            for (let i = 0; i < pillBar.order.length; ++i) {
                if (pillBar.reachable(pillBar.order[i]))
                    return pillBar.order[i];
            }
            return null;
        }

        function reachable(item): bool {
            return item !== null && item.visible && item.enabled;
        }

        function step(delta): bool {
            let at = -1;
            for (let i = 0; i < pillBar.order.length; ++i) {
                if (pillBar.order[i].activeFocus)
                    at = i;
            }
            if (at < 0)
                return false;
            for (let i = at + delta; i >= 0 && i < pillBar.order.length; i += delta) {
                if (pillBar.reachable(pillBar.order[i])) {
                    pillBar.order[i].forceActiveFocus(Qt.TabFocusReason);
                    return true;
                }
            }
            return true;
        }

        function popupUnder(menu, anchor): void {
            const p = anchor.mapToItem(null, 0, anchor.height + Theme.scale(4));
            menu.popupAt(p.x, p.y);
        }

        Keys.onLeftPressed: event => event.accepted = pillBar.step(-1)
        Keys.onRightPressed: event => event.accepted = pillBar.step(1)
        Keys.onUpPressed: event => {
            strip.forceActiveFocus(Qt.TabFocusReason);
            event.accepted = true;
        }
        Keys.onDownPressed: event => {
            page.focusContent();
            event.accepted = true;
        }

        Item {
            id: pillClip

            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: Math.max(0, parent.width - rightCluster.width - Theme.spacingValue)
            clip: true

            Row {
                id: pillRow

                anchors.verticalCenter: parent.verticalCenter
                // Keep the focused pill on screen when the row is wider than its clip.
                x: {
                    let focused = null;
                    for (let i = 0; i < pillBar.order.length; ++i) {
                        if (pillBar.order[i].activeFocus && pillBar.order[i].parent === pillRow)
                            focused = pillBar.order[i];
                    }
                    if (!focused)
                        return 0;
                    const overflow = focused.x + focused.width - pillClip.width;
                    return overflow > 0 ? -overflow : 0;
                }
                spacing: Theme.spacingTight

                FilterPill {
                    id: genrePill
                    visible: MusicBrowseCtl.filtersAvailable
                    text: MusicBrowseCtl.genrePillText
                    active: MusicBrowseCtl.genreIds.length > 0
                    onActivated: {
                        MusicBrowseCtl.ensureGenreOptions();
                        genrePicker.open();
                    }
                    onCleared: MusicBrowseCtl.clearGenres()
                }

                FilterPill {
                    id: decadePill
                    visible: MusicBrowseCtl.filtersAvailable && MusicBrowseCtl.decadeAvailable
                    text: MusicBrowseCtl.decadePillText
                    active: MusicBrowseCtl.decade !== 0
                    onActivated: pillBar.popupUnder(decadeMenu, decadePill)
                    onCleared: MusicBrowseCtl.decade = 0
                }

                FilterPill {
                    id: formatPill
                    visible: MusicBrowseCtl.filtersAvailable && MusicBrowseCtl.formatAvailable
                    text: MusicBrowseCtl.formatPillText
                    active: MusicBrowseCtl.format !== "any"
                    onActivated: pillBar.popupUnder(formatMenu, formatPill)
                    onCleared: MusicBrowseCtl.format = "any"
                }

                FilterPill {
                    id: favouritesPill
                    visible: MusicBrowseCtl.filtersAvailable
                    toggle: true
                    iconName: MusicBrowseCtl.favouritesOnly ? "heart-filled" : "heart"
                    text: qsTr("Favourites")
                    active: MusicBrowseCtl.favouritesOnly
                    onActivated: MusicBrowseCtl.favouritesOnly = !MusicBrowseCtl.favouritesOnly
                }

                FilterPill {
                    id: unplayedPill
                    visible: MusicBrowseCtl.filtersAvailable
                    toggle: true
                    text: qsTr("Unplayed")
                    active: MusicBrowseCtl.unplayedOnly
                    onActivated: MusicBrowseCtl.unplayedOnly = !MusicBrowseCtl.unplayedOnly
                }

                StrmButton {
                    id: clearAllButton
                    anchors.verticalCenter: parent.verticalCenter
                    visible: MusicBrowseCtl.filtersAvailable && MusicBrowseCtl.activeFilterCount >= 2
                    variant: "ghost"
                    iconName: "close"
                    text: qsTr("Clear all")
                    onClicked: MusicBrowseCtl.clearFilters()
                }

                // Which artists is a choice of endpoint, not a filter, so it
                // sits at the end of the row rather than among the pills.
                FilterPill {
                    id: albumArtistsPill
                    visible: MusicBrowseCtl.section === "artists"
                    toggle: true
                    text: qsTr("Album artists")
                    active: MusicBrowseCtl.artistMode === "albumArtists"
                    onActivated: MusicBrowseCtl.artistMode = "albumArtists"
                }

                FilterPill {
                    id: everyonePill
                    visible: MusicBrowseCtl.section === "artists"
                    toggle: true
                    text: qsTr("Everyone")
                    active: MusicBrowseCtl.artistMode === "everyone"
                    onActivated: MusicBrowseCtl.artistMode = "everyone"
                }
            }
        }

        Row {
            id: rightCluster

            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingTight

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: MusicBrowseCtl.countText
                color: Theme.textSecondaryColor
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontCaption
                font.letterSpacing: Theme.trackLabel * Theme.fontCaption
            }

            StrmSelect {
                id: sortSelect

                anchors.verticalCenter: parent.verticalCenter
                placeholder: qsTr("Sort")
                model: {
                    const out = [];
                    const source = MusicBrowseCtl.availableSorts;
                    for (let i = 0; i < source.length; ++i)
                        out.push({ "text": source[i].label, "value": source[i].key });
                    return out;
                }
                // Controlled: the controller answers every pick.
                currentIndex: {
                    const source = MusicBrowseCtl.availableSorts;
                    for (let i = 0; i < source.length; ++i) {
                        if (source[i].key === MusicBrowseCtl.sortKey)
                            return i;
                    }
                    return -1;
                }
                onActivated: index => {
                    const key = sortSelect.valueAt(index);
                    if (key !== undefined)
                        MusicBrowseCtl.sortKey = String(key);
                }
            }

            StrmIconButton {
                id: directionButton

                anchors.verticalCenter: parent.verticalCenter
                enabled: MusicBrowseCtl.sortKey !== "random"
                iconName: MusicBrowseCtl.sortDescending ? "chevron-down" : "chevron-up"
                tooltip: MusicBrowseCtl.sortDescending ? qsTr("Descending") : qsTr("Ascending")
                onClicked: MusicBrowseCtl.sortDescending = !MusicBrowseCtl.sortDescending
            }

            StrmButton {
                id: playButton

                anchors.verticalCenter: parent.verticalCenter
                visible: MusicBrowseCtl.filtersAvailable
                enabled: MusicBrowseCtl.libraryId.length > 0
                text: qsTr("Play")
                iconName: "play"
                variant: "primary"
                accessibleDescription: MusicBrowseCtl.scopeLabel
                onClicked: MusicBrowseCtl.playFiltered()
            }

            StrmButton {
                id: shuffleButton

                anchors.verticalCenter: parent.verticalCenter
                visible: MusicBrowseCtl.filtersAvailable
                enabled: MusicBrowseCtl.libraryId.length > 0
                text: qsTr("Shuffle")
                iconName: "shuffle"
                accessibleDescription: MusicBrowseCtl.scopeLabel
                onClicked: MusicBrowseCtl.shuffleFiltered()
            }
        }
    }

    StrmMenu {
        id: decadeMenu

        actions: {
            const out = [{ "text": qsTr("Any decade"), "checked": MusicBrowseCtl.decade === 0 },
                         { "separator": true }];
            const source = MusicBrowseCtl.decadeOptions;
            for (let i = 0; i < source.length; ++i)
                out.push({ "text": source[i].label, "checked": MusicBrowseCtl.decade === source[i].value });
            return out;
        }
        onTriggered: index => {
            if (index === 0)
                MusicBrowseCtl.decade = 0;
            else if (index >= 2)
                MusicBrowseCtl.decade = MusicBrowseCtl.decadeOptions[index - 2].value;
        }
    }

    StrmMenu {
        id: formatMenu

        actions: {
            const out = [];
            const source = MusicBrowseCtl.formatOptions;
            for (let i = 0; i < source.length; ++i)
                out.push({ "text": source[i].label, "checked": MusicBrowseCtl.format === source[i].key });
            return out;
        }
        onTriggered: index => MusicBrowseCtl.format = MusicBrowseCtl.formatOptions[index].key
    }

    // ── Songs selection ────────────────────────────────────────────────────
    SelectionBar {
        id: songSelection

        anchors.top: pillBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.pageMarginValue
        anchors.rightMargin: Theme.pageMarginValue
        count: page.songsView() ? page.songsView().selectionCount : 0

        onQueueRequested: Actions.addAllToQueue(page.songsView().selectedItems())
        onPlaylistRequested: page.fileSongSelection()
        onFavoriteRequested: Actions.setFavoriteAll(page.songsView().selectedIds(), true)
        onClearRequested: {
            const table = page.songsView();
            if (table) {
                table.clearSelection();
                table.forceActiveFocus(Qt.OtherFocusReason);
            }
        }
    }

    // ── Section views ──────────────────────────────────────────────────────
    Component {
        id: albumsComponent

        StrmGrid {
            id: albumsGrid

            navigationFocusKey: "musicBrowse-albums"
            navigationFocusFallbackItem: strip
            navigationFocusRefillActive: MusicBrowseCtl.albumsLane.loading
            gridModel: MusicBrowseCtl.albums
            customCardWidth: Theme.crateSleeveSize
            customCardHeight: Theme.crateSleeveSize + Theme.scale(56)
            emptyText: ""
            prefetchThreshold: 30
            focus: albumsGrid.count > 0
            KeyNavigation.up: pillBar.entryItem
            onNearEnd: MusicBrowseCtl.loadMore()
            onItemActivated: index => page.openItem(index)
            onItemPlayRequested: index => page.playItem(index)
            onMenuRequested: (index, mx, my) => musicMenu.popupForItem(page.itemAt(index), mx, my)

            cardComponent: Component {
                CrateSleeve {
                    id: sleeve

                    property var model: null
                    property int index: -1

                    size: Theme.crateSleeveSize
                    showCaption: true
                    coverUrl: sleeve.model && sleeve.model.coverUrl !== undefined ? String(sleeve.model.coverUrl) : ""
                    title: sleeve.model && sleeve.model.title !== undefined ? String(sleeve.model.title) : ""
                    subtitle: sleeve.model && sleeve.model.subtitle !== undefined ? String(sleeve.model.subtitle) : ""
                    badge: sleeve.model && sleeve.model.releaseBadge !== undefined ? String(sleeve.model.releaseBadge) : ""
                    hiRes: false
                }
            }
        }
    }

    Component {
        id: artistsComponent

        StrmGrid {
            id: artistsGrid

            navigationFocusKey: "musicBrowse-artists"
            navigationFocusFallbackItem: strip
            navigationFocusRefillActive: MusicBrowseCtl.artistsLane.loading
            gridModel: MusicBrowseCtl.artists
            customCardWidth: Theme.cratePortraitSize
            customCardHeight: Theme.cratePortraitSize + Theme.scale(52)
            emptyText: ""
            prefetchThreshold: 30
            focus: artistsGrid.count > 0
            KeyNavigation.up: pillBar.entryItem
            onNearEnd: MusicBrowseCtl.loadMore()
            onItemActivated: index => page.openItem(index)
            onItemPlayRequested: index => page.playItem(index)
            onMenuRequested: (index, mx, my) => musicMenu.popupForItem(page.itemAt(index), mx, my)

            cardComponent: Component {
                CratePortrait {
                    id: portrait

                    property var model: null
                    property int index: -1

                    size: Theme.cratePortraitSize
                    imageUrl: portrait.model && portrait.model.coverUrl !== undefined ? String(portrait.model.coverUrl) : ""
                    name: portrait.model && portrait.model.name !== undefined ? String(portrait.model.name) : ""
                    subtitle: portrait.model && portrait.model.subtitle !== undefined ? String(portrait.model.subtitle) : ""
                }
            }
        }
    }

    Component {
        id: songsComponent

        TrackTable {
            id: songsTable

            navigationFocusKey: "musicBrowse-songs"
            navigationFocusFallbackItem: strip
            navigationFocusRefillActive: MusicBrowseCtl.songsLane.loading
            focus: songsTable.count > 0
            model: MusicBrowseCtl.songs
            rowHeight: page.songRowHeight
            discGrouping: false
            artistRule: false
            alwaysShowArtist: true
            multiSelect: true
            prefetchThreshold: 30
            KeyNavigation.up: pillBar.entryItem
            onActivated: index => page.playSongFrom(index)
            onMenuRequested: (index, mx, my) => songMenu.popupForItemNoDetails(page.itemAt(index), mx, my)
            onNearEnd: MusicBrowseCtl.loadMore()

            delegate: TrackRow {
                id: songRow

                required property int index
                required property var model

                readonly property string trackId: songRow.model.itemId !== undefined ? String(songRow.model.itemId) : ""
                readonly property string albumText: songRow.model.albumTitle !== undefined ? String(songRow.model.albumTitle) : ""
                readonly property string formatText: songRow.model.formatBadge !== undefined ? String(songRow.model.formatBadge) : ""

                width: songsTable.width
                navigationFocusOwner: songsTable
                rowHeight: page.songRowHeight
                numberColumn: page.songNumberColumn
                durationColumn: page.songDurationColumn
                verbsColumn: page.songVerbsColumn
                artistColumn: page.songArtistColumn
                title: songRow.model.name !== undefined ? String(songRow.model.name) : ""
                // Album · FORMAT: the badge rides the secondary line until
                // Phase 4's CrateTrackTable gives it a column.
                secondary: songRow.formatText.length === 0 ? songRow.albumText
                         : songRow.albumText.length === 0 ? songRow.formatText
                         : page.songRowFormat.arg(songRow.albumText, songRow.formatText)
                artist: songsTable.shownArtistFor(songRow.model)
                durationText: songRow.model.durationText !== undefined ? String(songRow.model.durationText) : ""
                number: songRow.index + 1
                coverUrl: songRow.model.coverUrl !== undefined ? String(songRow.model.coverUrl) : ""
                showCover: true
                current: songsTable.currentIndex === songRow.index && songsTable.activeFocus
                selected: songsTable.isSelected(songRow.index)
                playing: songRow.trackId.length > 0 && songRow.trackId === page.nowPlayingId
                favorite: songRow.model.favorite === true
                showFavorite: true
                showMenu: true
                verbsRevealed: songRow.hovered || songRow.favorite
                onActivated: modifiers => {
                    songsTable.forceActiveFocus(Qt.MouseFocusReason);
                    songsTable.activateAt(songRow.index, modifiers);
                }
                onFavoriteToggled: {
                    const item = page.itemAt(songRow.index);
                    if (item)
                        Actions.toggleFavorite(item);
                }
                onMenuRequested: (sceneX, sceneY) =>
                    songMenu.popupForItemNoDetails(page.itemAt(songRow.index), sceneX, sceneY)
            }
        }
    }

    Component {
        id: genresComponent

        StrmGrid {
            id: genresGrid

            navigationFocusKey: "musicBrowse-genres"
            navigationFocusFallbackItem: strip
            navigationFocusRefillActive: MusicBrowseCtl.genresLane.loading
            gridModel: MusicBrowseCtl.genres
            customCardWidth: Theme.crateSleeveSize
            customCardHeight: Theme.crateSleeveSize
            emptyText: ""
            prefetchThreshold: 24
            focus: genresGrid.count > 0
            KeyNavigation.up: pillBar.entryItem
            onNearEnd: MusicBrowseCtl.loadMore()
            onItemActivated: index => page.openItem(index)

            cardComponent: Component {
                GenreBinTile {
                    id: bin

                    property var model: null
                    property int index: -1

                    size: Theme.crateSleeveSize
                    isAllBin: false
                    name: bin.model && bin.model.name !== undefined ? String(bin.model.name) : ""
                    subtitle: bin.model && bin.model.subtitle !== undefined ? String(bin.model.subtitle) : ""
                    covers: bin.model && bin.model.covers ? bin.model.covers : []
                }
            }
        }
    }

    Component {
        id: playlistsComponent

        // The New playlist tile is not a row of the model (contract note 10),
        // so this view is a scope of tile + grid that answers the same view
        // API the Loader's other items do.
        FocusScope {
            id: playlistsScope

            readonly property alias count: playlistsGrid.count
            readonly property alias currentIndex: playlistsGrid.currentIndex

            function navigationFocusSnapshot(): var { return playlistsGrid.navigationFocusSnapshot(); }
            function restoreNavigationFocus(identity, index): bool {
                return playlistsGrid.restoreNavigationFocus(identity, index);
            }
            function pageBy(step): bool { return playlistsGrid.pageBy(step); }

            Item {
                id: newTile

                anchors.left: parent.left
                anchors.top: parent.top
                anchors.leftMargin: Theme.pageMarginValue
                width: Theme.scale(220)
                height: Theme.controlHeightLarge
                activeFocusOnTab: true
                focus: playlistsGrid.count === 0

                Accessible.role: Accessible.Button
                Accessible.name: qsTr("New playlist")
                Accessible.onPressAction: createPrompt.show()

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusChip
                    color: newHover.hovered ? Theme.hoverTint : "transparent"
                    border.width: 1
                    border.color: newTile.activeFocus ? Theme.accentColor : Theme.hairline
                }

                Row {
                    anchors.centerIn: parent
                    spacing: Theme.spacingTight

                    StrmIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "plus"
                        color: Theme.accentColor
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("New playlist")
                        color: Theme.textPrimaryColor
                        font.family: Theme.fontBody
                        font.pixelSize: Theme.fontBodySize
                    }
                }

                FocusRing {
                    active: newTile.activeFocus
                    radius: Theme.radiusChip
                }

                HoverHandler {
                    id: newHover
                    cursorShape: Qt.PointingHandCursor
                }

                TapHandler {
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: createPrompt.show()
                }

                Keys.onReturnPressed: event => {
                    if (!event.isAutoRepeat)
                        createPrompt.show();
                }
                Keys.onEnterPressed: event => {
                    if (!event.isAutoRepeat)
                        createPrompt.show();
                }
                Keys.onUpPressed: event => {
                    if (pillBar.entryItem)
                        pillBar.entryItem.forceActiveFocus(Qt.TabFocusReason);
                    event.accepted = true;
                }
                KeyNavigation.down: playlistsGrid
            }

            Text {
                anchors.left: newTile.right
                anchors.leftMargin: Theme.spacingValue
                anchors.verticalCenter: newTile.verticalCenter
                visible: MusicBrowseCtl.playlistsLane.ready && MusicBrowseCtl.playlistsLane.empty
                text: qsTr("No music playlists yet. Make one here, or add a record to one from its menu.")
                color: Theme.textTertiary
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontSmall
            }

            StrmGrid {
                id: playlistsGrid

                anchors.top: newTile.bottom
                anchors.topMargin: Theme.spacingValue
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                navigationFocusKey: "musicBrowse-playlists"
                navigationFocusFallbackItem: newTile
                navigationFocusRefillActive: MusicBrowseCtl.playlistsLane.loading
                gridModel: MusicBrowseCtl.playlists
                customCardWidth: Theme.crateSleeveSize
                customCardHeight: Theme.crateSleeveSize + Theme.scale(52)
                emptyText: ""
                prefetchThreshold: 30
                focus: playlistsGrid.count > 0
                KeyNavigation.up: newTile
                onNearEnd: MusicBrowseCtl.loadMore()
                onItemActivated: index => page.openItem(index)
                onItemPlayRequested: index => page.playItem(index)
                onMenuRequested: (index, mx, my) => musicMenu.popupForItem(page.itemAt(index), mx, my)

                cardComponent: Component {
                    Item {
                        id: playlistCard

                        property var model: null
                        property int index: -1
                        property bool current: false
                        property bool hovered: false

                        signal activated
                        signal menuRequested(real x, real y)

                        width: Theme.crateSleeveSize
                        height: Theme.crateSleeveSize + Theme.scale(52)

                        CoverCollage {
                            id: collage

                            size: Theme.crateSleeveSize
                            radius: Theme.crateSleeveRadius
                            covers: playlistCard.model && playlistCard.model.coverUrl
                                    ? [String(playlistCard.model.coverUrl)] : []

                            FocusRing {
                                active: playlistCard.current
                                radius: Theme.crateSleeveRadius
                            }
                        }

                        Column {
                            anchors.top: collage.bottom
                            anchors.topMargin: Theme.scale(8)
                            width: parent.width
                            spacing: Theme.scale(2)

                            Text {
                                width: parent.width
                                text: playlistCard.model && playlistCard.model.name !== undefined ? String(playlistCard.model.name) : ""
                                color: playlistCard.current || playlistCard.hovered ? Theme.textPrimaryColor : Theme.textSecondaryColor
                                font.family: Theme.fontBody
                                font.pixelSize: Theme.fontBodySize
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }

                            Text {
                                width: parent.width
                                text: playlistCard.model && playlistCard.model.subtitle !== undefined ? String(playlistCard.model.subtitle) : ""
                                color: Theme.textTertiary
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.fontCaption
                                elide: Text.ElideRight
                            }
                        }

                        TapHandler {
                            gesturePolicy: TapHandler.ReleaseWithinBounds
                            onTapped: playlistCard.activated()
                        }

                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            gesturePolicy: TapHandler.ReleaseWithinBounds
                            onTapped: (eventPoint, button) => {
                                const p = playlistCard.mapToItem(null, eventPoint.position.x, eventPoint.position.y);
                                playlistCard.menuRequested(p.x, p.y);
                            }
                        }
                    }
                }
            }
        }
    }

    Loader {
        id: viewLoader

        anchors.top: page.songsShown ? songSelection.bottom : pillBar.bottom
        anchors.topMargin: Theme.spacingValue
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: dividers.visible ? dividers.left : parent.right
        anchors.leftMargin: page.songsShown ? Theme.pageMarginValue : 0
        anchors.rightMargin: page.songsShown ? Theme.spacingValue : 0
        anchors.bottomMargin: page.songsShown ? Theme.spacingValue : 0
        active: page.viewReady
        // A Loader is a focus scope. Its claim follows the strip's, so the
        // keyboard is never parked in an empty view (see MusicPage history).
        focus: page.contentFocusable
        sourceComponent: page.loadedSection === "artists" ? artistsComponent
                       : page.loadedSection === "songs" ? songsComponent
                       : page.loadedSection === "genres" ? genresComponent
                       : page.loadedSection === "playlists" ? playlistsComponent
                       : albumsComponent
        onLoaded: Qt.callLater(page.restoreActiveView)
    }

    CrateDividers {
        id: dividers

        anchors.right: parent.right
        anchors.top: viewLoader.top
        anchors.bottom: parent.bottom
        anchors.rightMargin: Theme.spacingTight
        anchors.bottomMargin: Theme.spacingValue
        visible: MusicBrowseCtl.letterStripVisible
        letters: MusicBrowseCtl.letters
        currentLetter: MusicBrowseCtl.letter
        onLetterChosen: letter => MusicBrowseCtl.toggleLetter(letter)
        Keys.onLeftPressed: event => {
            page.focusContent();
            event.accepted = true;
        }
    }

    // ── Page states ────────────────────────────────────────────────────────
    LoadingState {
        anchors.fill: viewLoader
        visible: page.laneLoading && page.shownCount === 0 && !page.playlistsShown
        shape: page.songsShown ? "list" : "grid"
    }

    EmptyState {
        anchors.fill: viewLoader
        visible: page.laneError.length > 0 && page.shownCount === 0
        severity: "error"
        iconName: "info"
        headline: qsTr("Couldn't load this section")
        body: page.laneError
        actionText: qsTr("Retry")
        actionIcon: "refresh"
        onActionTriggered: page.lane.retry()
    }

    EmptyState {
        id: unfilteredEmpty

        anchors.fill: viewLoader
        visible: page.lane !== null && page.lane.ready && page.lane.empty
                 && !MusicBrowseCtl.filtered && MusicBrowseCtl.letter.length === 0
                 && !page.playlistsShown
        iconName: "lib-music"
        headline: page.loadedSection === "artists" ? qsTr("No artists here")
                : page.loadedSection === "songs" ? qsTr("No songs here")
                : page.loadedSection === "genres" ? qsTr("No genres here")
                : qsTr("No records here")
        body: MusicBrowseCtl.section === "artists" && MusicBrowseCtl.artistMode === "albumArtists"
              ? qsTr("Nothing is filed under an album artist. Everyone who appears on a track is still listed.")
              : qsTr("Once your Emby server has scanned music into this library, it shows up here.")
        actionText: MusicBrowseCtl.section === "artists" && MusicBrowseCtl.artistMode === "albumArtists"
                    ? qsTr("Show everyone") : ""
        actionIcon: unfilteredEmpty.actionText.length > 0 ? "user" : ""
        onActionTriggered: MusicBrowseCtl.artistMode = "everyone"
    }

    // Narrowed to nothing: every active filter is repeated as its own way out.
    Column {
        id: noMatch

        anchors.centerIn: viewLoader
        width: Math.min(viewLoader.width - Theme.pageMarginValue * 2, Theme.scale(560))
        spacing: Theme.spacingValue
        visible: page.lane !== null && page.lane.ready && page.lane.empty
                 && (MusicBrowseCtl.filtered || MusicBrowseCtl.letter.length > 0)

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("No records match")
            color: Theme.textPrimaryColor
            font.family: Theme.fontDisplay
            font.pixelSize: Theme.fontHeading
            font.weight: Font.DemiBold
        }

        Flow {
            width: parent.width
            spacing: Theme.spacingTight

            StrmButton {
                visible: MusicBrowseCtl.genreIds.length > 0
                variant: "secondary"
                iconName: "close"
                text: MusicBrowseCtl.genrePillText
                onClicked: MusicBrowseCtl.clearGenres()
            }
            StrmButton {
                visible: MusicBrowseCtl.decadeAvailable && MusicBrowseCtl.decade !== 0
                variant: "secondary"
                iconName: "close"
                text: MusicBrowseCtl.decadePillText
                onClicked: MusicBrowseCtl.decade = 0
            }
            StrmButton {
                visible: MusicBrowseCtl.formatAvailable && MusicBrowseCtl.format !== "any"
                variant: "secondary"
                iconName: "close"
                text: MusicBrowseCtl.formatPillText
                onClicked: MusicBrowseCtl.format = "any"
            }
            StrmButton {
                visible: MusicBrowseCtl.favouritesOnly
                variant: "secondary"
                iconName: "close"
                text: qsTr("Favourites")
                onClicked: MusicBrowseCtl.favouritesOnly = false
            }
            StrmButton {
                visible: MusicBrowseCtl.unplayedOnly
                variant: "secondary"
                iconName: "close"
                text: qsTr("Unplayed")
                onClicked: MusicBrowseCtl.unplayedOnly = false
            }
            StrmButton {
                visible: MusicBrowseCtl.letter.length > 0
                variant: "secondary"
                iconName: "close"
                text: qsTr("Letter: %1").arg(MusicBrowseCtl.letter)
                onClicked: MusicBrowseCtl.letter = ""
            }
        }
    }

    // A page already on screen and the next one failed: say so in place.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.pageMarginValue
        height: pagingRow.implicitHeight + Theme.spacingValue * 2
        visible: page.laneError.length > 0 && page.shownCount > 0
        radius: Theme.radiusPanel
        color: Theme.surfaceOverlay
        border.width: 1
        border.color: Theme.hairline

        Row {
            id: pagingRow

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: Theme.spacingValue
            anchors.rightMargin: Theme.spacingValue
            spacing: Theme.spacingValue

            StrmIcon {
                anchors.verticalCenter: parent.verticalCenter
                name: "info"
                color: Theme.negative
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: pagingRow.width - Theme.iconSize - pagingRetry.width - pagingRow.spacing * 2
                text: qsTr("Couldn't load more: %1").arg(page.laneError)
                color: Theme.textSecondaryColor
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontSmall
                elide: Text.ElideRight
            }

            StrmButton {
                id: pagingRetry

                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Retry")
                iconName: "refresh"
                onClicked: page.lane.retry()
            }
        }
    }

    // ── Menus and overlays ─────────────────────────────────────────────────
    ItemMenu {
        id: songMenu

        allowAddToPlaylist: true
        onAddToPlaylistRequested: item => {
            if (page.idOf(item).length > 0)
                playlistPicker.show(page.nameOf(item), [page.idOf(item)]);
        }
    }

    ItemMenu {
        id: musicMenu

        profile: "musicBrowse"
        allowAddToPlaylist: true
        // A card's "Add to playlist": a playlist holds tracks, so the album's
        // tracks are collected first and arrive at onAlbumTracksCollected.
        onAddToPlaylistRequested: item => {
            if (page.idOf(item).length > 0)
                MusicBrowseCtl.collectAlbumTracks(page.idOf(item), page.nameOf(item));
        }
    }

    GenrePicker {
        id: genrePicker

        z: 700
        options: MusicBrowseCtl.genreOptions
        loading: MusicBrowseCtl.genreOptionsLoading
        failed: MusicBrowseCtl.genreOptionsFailed
        onGenresChosen: ids => MusicBrowseCtl.setGenres(ids)
        onRetryRequested: MusicBrowseCtl.ensureGenreOptions()
        onDismissed: genrePill.forceActiveFocus(Qt.OtherFocusReason)
    }

    PlaylistPicker {
        id: playlistPicker

        z: 800
        mediaType: "Audio"
        onDismissed: page.focusContent()
    }

    Item {
        id: createPrompt

        property bool opened: false

        function show(): void {
            nameField.text = "";
            createPrompt.opened = true;
            nameField.forceActiveFocus(Qt.OtherFocusReason);
        }

        function dismiss(): void {
            if (!createPrompt.opened)
                return;
            createPrompt.opened = false;
            page.focusContent();
        }

        function commit(): void {
            const name = nameField.text.trim();
            if (name.length === 0)
                return;
            playlistPicker.pending = true;
            PlaylistCtl.create(name, [], "Audio");
            createPrompt.dismiss();
        }

        anchors.fill: parent
        z: 750
        visible: createPrompt.opened || createPrompt.opacity > 0.01
        enabled: createPrompt.opened
        opacity: createPrompt.opened ? 1.0 : 0.0

        Behavior on opacity {
            NumberAnimation { duration: Theme.animFastMs; easing.type: Theme.easeStandard }
        }

        Rectangle {
            anchors.fill: parent
            color: Theme.scrimColor

            TapHandler {
                gesturePolicy: TapHandler.ReleaseWithinBounds
                onTapped: createPrompt.dismiss()
            }
        }

        StrmPanel {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: Math.round(parent.height * 0.18)
            width: Math.min(parent.width - Theme.pageMarginValue * 2, Theme.scale(480))
            elevation: 4
            padding: Theme.spacingLoose
            title: qsTr("New playlist")
            subtitle: qsTr("A music playlist. It starts empty; add records and tracks from their menus.")

            TapHandler { gesturePolicy: TapHandler.ReleaseWithinBounds }

            StrmSearchField {
                id: nameField

                width: parent.width
                implicitHeight: Theme.controlHeightLarge
                placeholderText: qsTr("Name")
                onAccepted: createPrompt.commit()
                onEscapePressed: createPrompt.dismiss()
                KeyNavigation.down: createButton
            }

            Row {
                spacing: Theme.spacingTight

                StrmButton {
                    id: createButton
                    text: qsTr("Create")
                    iconName: "plus"
                    variant: "primary"
                    enabled: nameField.text.trim().length > 0
                    onClicked: createPrompt.commit()
                    KeyNavigation.up: nameField
                    KeyNavigation.right: cancelButton
                }

                StrmButton {
                    id: cancelButton
                    text: qsTr("Cancel")
                    variant: "ghost"
                    onClicked: createPrompt.dismiss()
                    KeyNavigation.up: nameField
                    KeyNavigation.left: createButton
                }
            }
        }

        Keys.onEscapePressed: event => {
            createPrompt.dismiss();
            event.accepted = true;
        }
    }

    // `pending` tells this page's result apart from a playlist edited elsewhere.
    Connections {
        target: PlaylistCtl

        function onActionSucceeded(message) {
            if (!playlistPicker.pending)
                return;
            playlistPicker.pending = false;
            browseToasts.show(message, "success");
        }
        function onActionFailed(message) {
            if (!playlistPicker.pending)
                return;
            playlistPicker.pending = false;
            browseToasts.show(message, "error");
        }
    }

    StrmToastHost {
        id: browseToasts

        anchors.fill: parent
        z: 900
    }
}
```

Notes for the implementer:
- `MusicBrowseCtl.section = key`, `.sortKey = …`, `.decade = …` and the other assignments go through the controller's `WRITE` setters, which validate. The page never checks a value first.
- The `StrmSelect` `currentIndex` is a binding. That is allowed: StrmSelect never writes it (see its header). The controller's `queryChanged` moves it.
- If `StrmPanel` lays out its children with a Column (it does, per its header), the prompt needs no Column of its own.
- `CoverCollage` receives one cover because `PlaylistGridModel` carries only `coverUrl` (contract note 16).

- [x] **Step 2: Register the page and put it in the self-test**

In `src/CMakeLists.txt`, add after `ui/pages/MusicHomePage.qml`:

```cmake
        ui/pages/MusicBrowsePage.qml
```

In `src/ui/Main.qml`, add after the Phase 2 `musicHomeComponent`:

```qml
    Component {
        id: musicBrowseComponent

        MusicBrowsePage {
            id: browsePage

            objectName: "musicBrowsePage"
            onHomeRequested: root.openMusicHome(browsePage.libraryId, browsePage.libraryName)
        }
    }
```

In the self-test `pages` list, add `["musicBrowse", musicBrowseComponent]` after the `musicHome` entry. The `music` entry stays until Task 6.

- [x] **Step 3: Build, lint, self-test**

Run:

```bash
cmake --preset dev && cmake --build --preset dev
bash scripts/check-qmllint-baseline.sh build/dev
STRMQT_SELFTEST=1 QT_QPA_PLATFORM=offscreen QT_ASSUME_STDERR_HAS_CONSOLE=1 ./build/dev/strmqt; echo "exit=$?"
```

Expected:
- The build succeeds.
- The lint script reports new lines for `MusicBrowsePage.qml` only. Every one is `[unqualified]` or `[unresolved-type]` on a context property (`MusicBrowseCtl`, `MusicPlay`, `Actions`, `PlayerCtl`, `PlaylistCtl`, `App`), exactly like `MusicPage.qml`'s lines. No line is in a fatal category. Do not re-baseline yet; Task 7 does it once.
- The self-test prints `selftest ok   musicBrowse`, no `selftest FAIL` line, and ends with `exit=0`.

If a line names a Phase 2 control property this page sets (for example `showCaption` on `CrateSleeve`), the Phase 2 control is the authority: check the contract table in the index and correct the page, not the control.

- [x] **Step 4: Commit**

```bash
git add src/ui/pages/MusicBrowsePage.qml src/CMakeLists.txt src/ui/Main.qml
git commit -m "$(cat <<'MSG'
feat(music): add the Crate browse page

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
)"
```

---

### Task 6: The `musicBrowse` route, and retiring `MusicPage`

**Files:**
- Modify: `tests/unit/tst_navigation_history.cpp` (the probe, the music test, the ordering and item-policy pins, and a new structural slot)
- Modify: `tests/integration/tst_music_query.cpp` (drop `musicPageInstantiatesOnlyTheActiveTab`; its structural guarantee moves to the new slot)
- Modify: `src/ui/shell/BoundedNavigationStack.qml`
- Modify: `src/ui/Main.qml`
- Modify: `src/ui/shell/FilterBar.qml` (drop the music branches)
- Modify: `src/ui/pages/LibraryPage.qml`, `src/ui/controls/StrmSelect.qml` (comments that name `MusicPage` or the FilterBar genre select)
- Modify: `src/CMakeLists.txt` (drop `ui/pages/MusicPage.qml`)
- Delete: `src/ui/pages/MusicPage.qml`

**Interfaces:**
- Consumes: `MusicBrowseCtl.open`, `.restore`, `.openGenre`, `.section`, `.routeState` (Task 2); `MusicBrowsePage` and `musicBrowseComponent` (Task 5); from Phase 2, `root.openMusicHome`, `root.railKey`, the interim `root.openMusicSection` and the `musicHomeComponent` handlers.
- Produces:
  - `root.openMusicBrowse(libraryId, name, section)` and `root.openMusicGenre(libraryId, name, genreId, genreName)`, as in the index's route table
  - Route `{kind:"musicBrowse", id, name, key:"musicBrowse:"+id, title:name, tab:<section>, query:<routeState>}`
  - `BoundedNavigationStack`: `currentMusicBrowseSection`, `currentMusicBrowseState`, `musicBrowsePageComponent` (replacing `currentMusicTab` and `musicPageComponent`)
  - `interactionContext` is `"music"` on `musicBrowsePage` (and no longer names `musicPage`)

Order inside `Main.qml`: `openMusicBrowse` and `openMusicGenre` go directly before `function openSearch(): void {`, in that order. The test's `functionBody(name, next)` slices by that order: `openMusicBrowse` ends at `openMusicGenre` and `openMusicGenre` at `openSearch`. Both stay clear of the `openSeries → openFavorites` and `openLibrary → openPlaylists` slices other pins use.

- [ ] **Step 1: Port the navigation tests (they fail first)**

Save as `/tmp/w3e-nav-test.py` and run it from the repository root. It changes only `tests/unit/tst_navigation_history.cpp` and `tests/integration/tst_music_query.cpp`, and stops at the first anchor it cannot find.

```python
import pathlib, re, sys

def load(path):
    return pathlib.Path(path).read_text()

def swap(text, old, new, label):
    count = text.count(old)
    if count != 1:
        sys.exit(f"{label}: expected 1 match, found {count}")
    return text.replace(old, new)

def cut_lines(text, start, end, label, replacement=""):
    """Replace from the line holding `start` up to (not including) the line holding `end`."""
    s = text.find(start)
    if s < 0:
        sys.exit(f"{label}: start anchor missing")
    e = text.find(end, s)
    if e < 0:
        sys.exit(f"{label}: end anchor missing")
    s = text.rfind("\n", 0, s) + 1
    e = text.rfind("\n", 0, e) + 1
    return text[:s] + replacement + text[e:]

# ── tst_navigation_history.cpp ──────────────────────────────────────────────
path = "tests/unit/tst_navigation_history.cpp"
t = load(path)

t = swap(t, "    void restoresPerEntryMusicTab();\n",
         "    void restoresPerEntryMusicBrowseState();\n", "slot declaration")
t = swap(t, "    void itemPolicyIsCentralizedAcrossQmlSurfaces();\n",
         "    void itemPolicyIsCentralizedAcrossQmlSurfaces();\n"
         "    void musicBrowsePageInstantiatesOnlyTheActiveSection();\n", "new slot declaration")

t = swap(t, '    property string musicTab: "albums"\n',
         '    property string musicBrowseSection: "albums"\n'
         '    property string musicBrowseState: ""\n', "probe properties")

t = cut_lines(t, "    function pushMusic(tab): void {", "    function pushMusicHome(libraryId): void {",
              "probe helpers", '''    function pushMusicBrowse(section): void {
        root.musicBrowseSection = String(section);
        root.musicBrowseState = "";
        history.pushRoute({ "kind": "musicBrowse", "id": "music-1", "name": "Music",
                            "key": "musicBrowse:music-1", "title": "Music",
                            "tab": root.musicBrowseSection, "query": root.musicBrowseState });
    }
    function setMusicBrowseSection(section): void {
        root.musicBrowseSection = String(section);
        if (history.currentItem && history.currentItem.selectedSection !== undefined)
            history.currentItem.selectedSection = root.musicBrowseSection;
    }
    function setMusicBrowseState(state): void {
        root.musicBrowseState = String(state);
    }
    // The production Main.qml transaction: capture A, retarget the shared
    // controller to Albums in B, then construct B without re-snapshotting A.
    function openMusicBrowseFromMain(libraryId): void {
        history.rememberFocus();
        root.musicBrowseSection = "albums";
        root.musicBrowseState = "";
        history.pushRoute({ "kind": "musicBrowse", "id": String(libraryId), "name": "Music B",
                            "key": "musicBrowse:" + String(libraryId), "title": "Music B",
                            "tab": "albums", "query": "" },
                          undefined, true);
    }
''')

t = swap(t, '''        else if (route.kind === "music")
            root.musicTab = route.tab;
''', '''        else if (route.kind === "musicBrowse") {
            root.musicBrowseSection = route.tab;
            root.musicBrowseState = route.query;
        }
''', "probe prepareRoute")

t = cut_lines(t, "    component MusicProbe: FocusScope {", "    component MusicHomeProbe: FocusScope {",
              "probe component", '''    component MusicBrowseProbe: FocusScope {
        property string libraryId: ""
        property string libraryName: ""
        property string initialSection: "albums"
        property string selectedSection: initialSection
        property string controllerSectionAtCreation: ""
        objectName: "musicBrowse-" + selectedSection
        focus: true
        Component.onCompleted: controllerSectionAtCreation = root.musicBrowseSection
    }

''')

t = swap(t, "    Component { id: musicComponent; MusicProbe {} }\n",
         "    Component { id: musicBrowseComponent; MusicBrowseProbe {} }\n", "probe Component")
t = swap(t, "        currentMusicTab: root.musicTab\n",
         "        currentMusicBrowseSection: root.musicBrowseSection\n"
         "        currentMusicBrowseState: root.musicBrowseState\n", "probe stack binding")
t = swap(t, "        musicPageComponent: musicComponent\n",
         "        musicBrowsePageComponent: musicBrowseComponent\n", "probe stack component")

t = cut_lines(t, "void NavigationHistoryTest::restoresPerEntryMusicTab()",
              "void NavigationHistoryTest::reconstructsMusicHomeAfterEviction()",
              "music restore test", '''void NavigationHistoryTest::restoresPerEntryMusicBrowseState()
{
    const QString state = QStringLiteral(
        "{\\"v\\":1,\\"g\\":[\\"genre-1\\"],\\"d\\":1970,\\"f\\":\\"any\\",\\"fav\\":true}");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QQuickView view;
    const auto [root, history] = createHistoryProbe(dir, view);
    QVERIFY(root);
    QVERIFY(history);

    QVERIFY(invoke(root, "pushMusicBrowse", QStringLiteral("albums")));
    QVERIFY(invoke(root, "setMusicBrowseSection", QStringLiteral("songs")));
    QVERIFY(invoke(root, "setMusicBrowseState", state));
    QVERIFY(invoke(root, "pushRoute", 91));

    const QVariantMap retained = listProperty(history, "navTrail").at(1).toMap();
    QCOMPARE(retained.value(QStringLiteral("tab")).toString(), QStringLiteral("songs"));
    QCOMPARE(retained.value(QStringLiteral("query")).toString(), state);

    // Another scope moves the shared controller on; Back must put it back.
    QVERIFY(invoke(root, "setMusicBrowseState", QString()));
    QVERIFY(invoke(root, "goBack"));
    QTRY_COMPARE(currentItem(history)->property("selectedSection").toString(),
                 QStringLiteral("songs"));
    QTRY_COMPARE(root->property("musicBrowseState").toString(), state);
    QVERIFY(invoke(root, "goBack"));
    QVERIFY(invoke(root, "goForward"));
    QTRY_COMPARE(currentItem(history)->property("initialSection").toString(),
                 QStringLiteral("songs"));
    QTRY_COMPARE(currentItem(history)->property("selectedSection").toString(),
                 QStringLiteral("songs"));
    QTRY_COMPARE(root->property("musicBrowseState").toString(), state);
}

''')

# The Main.qml ordering pins: Phase 2 Task 8's music block (library, Home and
# the interim openMusicSection) is replaced as one block, up to the probe half.
t = cut_lines(t, "    // A music library lands on its Home. Home's section strip opens the music",
              "    QTemporaryDir dir;", "ordering pins", '''    // A music library lands on its Home; Home's strip and bins open Browse.
    const QByteArray libraryBody = functionBody("openLibrary", "openMusicHome");
    QVERIFY(!libraryBody.isEmpty());
    QVERIFY(libraryBody.contains("root.openMusicHome(libraryId, name)"));
    QVERIFY(!libraryBody.contains("MusicCtl."));

    const QByteArray homeBody = functionBody("openMusicHome", "openPlaylists");
    QVERIFY(!homeBody.isEmpty());
    const qsizetype homeCapture = homeBody.indexOf("root.capturePageDeparture");
    const qsizetype homeOpen = homeBody.indexOf("MusicHomeCtl.open");
    const qsizetype homePush = homeBody.indexOf("root.pushCapturedPage");
    QVERIFY(homeCapture >= 0);
    QVERIFY(homeOpen >= 0);
    QVERIFY(homePush >= 0);
    QVERIFY(homeCapture < homeOpen);
    QVERIFY(homeOpen < homePush);

    // Capture the departing route before the shared controller moves, and
    // push only once it has: the page is built against the new scope.
    const QByteArray browseBody = functionBody("openMusicBrowse", "openMusicGenre");
    QVERIFY(!browseBody.isEmpty());
    const qsizetype browseCapture = browseBody.indexOf("root.capturePageDeparture");
    const qsizetype browsePrepare = browseBody.lastIndexOf("MusicBrowseCtl.open(");
    const qsizetype browsePush = browseBody.indexOf("root.pushCapturedPage");
    QVERIFY(browseCapture >= 0);
    QVERIFY(browsePrepare >= 0);
    QVERIFY(browsePush >= 0);
    QVERIFY(browseCapture < browsePrepare);
    QVERIFY(browsePrepare < browsePush);
    QVERIFY(browseBody.contains("\\"query\\": MusicBrowseCtl.routeState"));

    const QByteArray genreBody = functionBody("openMusicGenre", "openSearch");
    QVERIFY(!genreBody.isEmpty());
    const qsizetype genreCapture = genreBody.indexOf("root.capturePageDeparture");
    const qsizetype genrePrepare = genreBody.lastIndexOf("MusicBrowseCtl.openGenre(");
    const qsizetype genrePush = genreBody.indexOf("root.pushCapturedPage");
    QVERIFY(genreCapture >= 0);
    QVERIFY(genrePrepare >= 0);
    QVERIFY(genrePush >= 0);
    QVERIFY(genreCapture < genrePrepare);
    QVERIFY(genrePrepare < genrePush);

    QFile browsePage(QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/pages/MusicBrowsePage.qml"));
    QVERIFY(browsePage.open(QIODevice::ReadOnly));
    const QByteArray browseSource = browsePage.readAll();
    QVERIFY(!browseSource.contains("MusicCtl."));
    for (const QByteArray &lane : {QByteArrayLiteral("albums"), QByteArrayLiteral("artists"),
                                   QByteArrayLiteral("songs"), QByteArrayLiteral("genres"),
                                   QByteArrayLiteral("playlists")}) {
        QVERIFY(browseSource.contains("navigationFocusRefillActive: MusicBrowseCtl." + lane
                                      + "Lane.loading"));
    }
    QVERIFY(!QFile::exists(QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/pages/MusicPage.qml")));

''')

t = swap(t, '''    QVERIFY(invoke(root, "pushMusic", QStringLiteral("albums")));
    QVERIFY(invoke(root, "setMusicTab", QStringLiteral("songs")));
    QVERIFY(invoke(root, "openMusicLibraryFromMain", QStringLiteral("music-2")));
    trail = listProperty(history, "navTrail");
    QCOMPARE(trail.size(), 3);
    QCOMPARE(trail.at(1).toMap().value(QStringLiteral("tab")).toString(),
             QStringLiteral("songs"));
    QCOMPARE(currentItem(history)->property("controllerTabAtCreation").toString(),
             QStringLiteral("albums"));
''', '''    const QString departingState = QStringLiteral("{\\"v\\":1,\\"fav\\":true}");
    QVERIFY(invoke(root, "pushMusicBrowse", QStringLiteral("albums")));
    QVERIFY(invoke(root, "setMusicBrowseSection", QStringLiteral("songs")));
    QVERIFY(invoke(root, "setMusicBrowseState", departingState));
    QVERIFY(invoke(root, "openMusicBrowseFromMain", QStringLiteral("music-2")));
    trail = listProperty(history, "navTrail");
    QCOMPARE(trail.size(), 3);
    QCOMPARE(trail.at(1).toMap().value(QStringLiteral("tab")).toString(),
             QStringLiteral("songs"));
    QCOMPARE(trail.at(1).toMap().value(QStringLiteral("query")).toString(), departingState);
    QCOMPARE(currentItem(history)->property("controllerSectionAtCreation").toString(),
             QStringLiteral("albums"));
''', "probe retarget block")

t = swap(t, '''    const QByteArray music = sourceFor(QStringLiteral("src/ui/pages/MusicPage.qml"));
''', '''    const QByteArray music = sourceFor(QStringLiteral("src/ui/pages/MusicBrowsePage.qml"));
    QVERIFY(!music.isEmpty());
''', "item policy source")
t = swap(t, '''    QVERIFY(music.contains("Actions.play(item)"));
    QVERIFY(!music.contains("MusicCtl.playAlbum("));
''', '''    QVERIFY(music.contains("MusicPlay.playAlbum("));
    QVERIFY(!music.contains("MusicCtl."));
''', "item policy play")

t = cut_lines(t, "void NavigationHistoryTest::searchTrackOwnerRestoresAcrossResultLifecycle()",
              "void NavigationHistoryTest::searchTrackOwnerRestoresAcrossResultLifecycle()",
              "structural slot anchor", '''// Only the visible section is a live tree: five Components, one Loader. The
// swap follows the controller's sectionChanged, after the departing view's
// cursor is captured, and a rebuilt view restores that cursor.
void NavigationHistoryTest::musicBrowsePageInstantiatesOnlyTheActiveSection()
{
    QFile file(QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/pages/MusicBrowsePage.qml"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray source = file.readAll();

    QCOMPARE(source.count("Loader {"), 1);
    for (const QByteArray &component : {QByteArrayLiteral("albumsComponent"),
                                        QByteArrayLiteral("artistsComponent"),
                                        QByteArrayLiteral("songsComponent"),
                                        QByteArrayLiteral("genresComponent"),
                                        QByteArrayLiteral("playlistsComponent")}) {
        QCOMPARE(source.count("id: " + component), 1);
    }
    const qsizetype handler = source.indexOf("function onSectionChanged()");
    const qsizetype capture = source.indexOf("page.captureActiveView();", handler);
    const qsizetype swapSection = source.indexOf("page.loadedSection = MusicBrowseCtl.section;", handler);
    QVERIFY(handler >= 0);
    QVERIFY(capture > handler);
    QVERIFY(swapSection > capture);
    QVERIFY(source.contains("onLoaded: Qt.callLater(page.restoreActiveView)"));
    QVERIFY(source.contains("view.restoreNavigationFocus(String(state.identity), Number(state.index))"));
}

''')

leftovers = [i + 1 for i, line in enumerate(t.splitlines())
             if "openMusicSection" in line or "MusicProbe" in line or "musicTab" in line]
pathlib.Path(path).write_text(t)
if leftovers:
    sys.exit(f"{path}: unexpected leftovers of the old music route on lines {leftovers}")

# ── tst_music_query.cpp ─────────────────────────────────────────────────────
path = "tests/integration/tst_music_query.cpp"
q = load(path)
q = swap(q, "    void musicPageInstantiatesOnlyTheActiveTab();\n", "", "music_query declaration")
q = cut_lines(q, "// The UI half of the lazy-lane contract is structural",
              "// The Songs tab must never be the open album's `tracks` model.", "music_query body")
pathlib.Path(path).write_text(q)
print("tests ported")
```

`cut_lines` with the same start and end anchor inserts the replacement before that line and removes nothing. The structural slot uses this.

The anchors follow Phase 2 Task 8's edits to this file. Phase 2's `MusicHomeProbe`, `pushMusicHome`, `musicHomeComponent` and `reconstructsMusicHomeAfterEviction` sit just after the music pieces this script replaces, and they are kept. Its `homeBody` pin now ends at `openPlaylists`, because `openMusicSection` is gone. If the final leftover check fires, the script has already written the file. Remove the listed old-route lines by hand.

Run:

```bash
python3 /tmp/w3e-nav-test.py
cmake --build --preset dev --target tst_navigation_history tst_music_query
ctest --preset dev -R "tst_navigation_history|tst_music_query" --output-on-failure
```

Expected:
- `tests ported`
- Both targets build.
- `tst_navigation_history` FAILS. The probe does not load (`Cannot assign to non-existent property "currentMusicBrowseSection"`), so every slot fails at `createHistoryProbe`. After Step 2, `productionRetargetOrderingRetainsDepartingScopes` still fails on `!browseBody.isEmpty()` until Step 3.
- `tst_music_query` passes.

- [ ] **Step 2: Retarget `BoundedNavigationStack.qml`**

Save as `/tmp/w3e-stack.py` and run it from the repository root:

```python
import pathlib, sys

path = pathlib.Path("src/ui/shell/BoundedNavigationStack.qml")
s = path.read_text()

def swap(old, new, label):
    global s
    if s.count(old) != 1:
        sys.exit(f"{label}: expected 1 match, found {s.count(old)}")
    s = s.replace(old, new)

swap('''    // MusicController is process-wide while each retained music route owns the
    // tab its semantic focus key belongs to.
    property string currentMusicTab: "albums"
''', '''    // MusicBrowseController is process-wide while each retained musicBrowse
    // route owns its section (the tab its semantic focus key belongs to) and
    // its query, as the controller's compact routeState.
    property string currentMusicBrowseSection: "albums"
    property string currentMusicBrowseState: ""
''', "current properties")
swap("    property Component musicPageComponent: null\n",
     "    property Component musicBrowsePageComponent: null\n", "component property")
swap('        case "music": return navigation.musicPageComponent;\n',
     '        case "musicBrowse": return navigation.musicBrowsePageComponent;\n', "componentFor")
swap('''        case "music": return { "libraryId": route.id, "libraryName": route.name,
                               "initialTab": route.tab };
''', '''        case "musicBrowse": return { "libraryId": route.id, "libraryName": route.name,
                                     "initialSection": route.tab };
''', "reconstructedProperties")
swap('''        else if (route.kind === "music")
            route.tab = navigation.boundedText(navigation.currentMusicTab, 16);
''', '''        else if (route.kind === "musicBrowse") {
            route.tab = navigation.boundedText(navigation.currentMusicBrowseSection, 16);
            route.query = navigation.boundedText(navigation.currentMusicBrowseState, 1024);
        }
''', "retainCurrentRouteState")
path.write_text(s)
print("stack retargeted")
```

```bash
python3 /tmp/w3e-stack.py
```

Expected: `stack retargeted`.

- [ ] **Step 3: Rewire `Main.qml`**

Save as `/tmp/w3e-main.py` and run it from the repository root. It edits `src/ui/Main.qml` only:

```python
import pathlib, re, sys

path = pathlib.Path("src/ui/Main.qml")
s = path.read_text()

def swap(old, new, label):
    global s
    if s.count(old) != 1:
        sys.exit(f"{label}: expected 1 match, found {s.count(old)}")
    s = s.replace(old, new)

# 1. The music input context follows the new page.
swap('stack.currentItem.objectName === "musicPage"',
     'stack.currentItem.objectName === "musicBrowsePage"', "interactionContext")

# 2. prepareRoute: the controller takes the retained section and query back.
swap('''        case "music":
            MusicCtl.setLibrary(route.id);
            MusicCtl.tab = route.tab;
            if (MusicCtl.tab === "artists" && MusicCtl.artists.count === 0)
                MusicCtl.loadArtists();
            else if (MusicCtl.tab === "songs" && MusicCtl.songs.count === 0)
                MusicCtl.loadSongs();
            else if (MusicCtl.tab === "playlists" && MusicCtl.playlists.count === 0)
                MusicCtl.loadPlaylists();
            else if (MusicCtl.tab === "albums" && MusicCtl.albums.count === 0)
                MusicCtl.loadAlbums();
            break;
''', '''        case "musicBrowse":
            MusicBrowseCtl.restore(route.id, route.tab, route.query);
            break;
''', "prepareRoute")

# 3. The Phase 2 interim route goes, with the comment block directly above it.
start = s.find("    function openMusicSection(")
if start < 0:
    sys.exit("openMusicSection: not found (Phase 2 interim route)")
end = s.find("\n    }\n", start)
if end < 0:
    sys.exit("openMusicSection: no closing brace")
end += len("\n    }\n")
lines_before = s[:start].split("\n")
while len(lines_before) >= 2 and lines_before[-2].lstrip().startswith("//"):
    lines_before.pop(-2)
head = "\n".join(lines_before)
tail = s[end:]
if head.endswith("\n\n") and tail.startswith("\n"):
    tail = tail[1:]
s = head + tail

# 4. Home's section and genre requests reach the new functions (Phase 2 Task 8).
swap('''            onSectionRequested: key =>
                root.openMusicSection(musicHomePage.libraryId, musicHomePage.libraryName, key, "")
            onGenreRequested: (genreId, genreName) =>
                root.openMusicSection(musicHomePage.libraryId, musicHomePage.libraryName, "albums", genreId)
''', '''            onSectionRequested: key =>
                root.openMusicBrowse(musicHomePage.libraryId, musicHomePage.libraryName, key)
            onGenreRequested: (genreId, genreName) =>
                root.openMusicGenre(musicHomePage.libraryId, musicHomePage.libraryName,
                                    genreId, genreName)
''', "Home handlers")
swap('''    // Home's key is not the library id: MusicPage keeps that key, so Home and a
    // section are two history entries rather than one replacing the other.
''', '''    // Home's key is not Browse's ("musicHome:" and "musicBrowse:" + id), so Home
    // and a section are two history entries rather than one replacing the other.
''', "openMusicHome comment")

# 5. The two routes, directly before openSearch.
swap('''    function openSearch(): void {
''', '''    // A music library's browse page (Crate spec §5). One history entry per
    // library: section switches on screen never push, and the entry retains
    // the section (tab) and the whole query (routeState) for Back/Forward.
    // The controller moves between the capture and the push, so the departing
    // route keeps its own scope and the new page is built on the new one.
    function openMusicBrowse(libraryId, name, section): void {
        const key = "musicBrowse:" + libraryId;
        if (root.currentKey === key) {
            MusicBrowseCtl.open(libraryId, section);
            root.focusCurrentPage();
            return;
        }
        root.capturePageDeparture();
        MusicBrowseCtl.open(libraryId, section);
        root.pushCapturedPage({ "kind": "musicBrowse", "id": libraryId, "name": name,
                                "key": key, "title": name,
                                "tab": MusicBrowseCtl.section,
                                "query": MusicBrowseCtl.routeState });
    }

    // A genre bin is a filter, not a page: the browse page on Albums with only
    // that genre set (openGenre replaces every other filter).
    function openMusicGenre(libraryId, name, genreId, genreName): void {
        const key = "musicBrowse:" + libraryId;
        if (root.currentKey === key) {
            MusicBrowseCtl.openGenre(genreId, genreName);
            root.focusCurrentPage();
            return;
        }
        root.capturePageDeparture();
        MusicBrowseCtl.open(libraryId, "albums");
        MusicBrowseCtl.openGenre(genreId, genreName);
        root.pushCapturedPage({ "kind": "musicBrowse", "id": libraryId, "name": name,
                                "key": key, "title": name,
                                "tab": MusicBrowseCtl.section,
                                "query": MusicBrowseCtl.routeState });
    }

    function openSearch(): void {
''', "openMusicBrowse/openMusicGenre")

# 6. The rail highlights the library on Browse too.
swap('''    // The nav rail's view of currentKey. A library's Music Home is that library
    // as far as the rail and the library cycle are concerned.
    readonly property string railKey: root.currentKey.startsWith("musicHome:")
                                      ? root.currentKey.substring("musicHome:".length)
                                      : root.currentKey
''', '''    // The nav rail's view of currentKey. A library's Music Home and Browse are
    // that library as far as the rail and the library cycle are concerned.
    readonly property string railKey: root.currentKey.replace(/^music(?:Home|Browse):/, "")
''', "railKey")

# 7. The stack follows the new controller.
swap("        currentMusicTab: MusicCtl.tab\n",
     "        currentMusicBrowseSection: MusicBrowseCtl.section\n"
     "        currentMusicBrowseState: MusicBrowseCtl.routeState\n", "stack section binding")
swap("        musicPageComponent: musicComponent\n",
     "        musicBrowsePageComponent: musicBrowseComponent\n", "stack component binding")

# 8. The self-test no longer builds MusicPage.
before = s
s = re.sub(r',\s*\["music", musicComponent\]', "", s, count=1)
if s == before:
    s = re.sub(r'\["music", musicComponent\],\s*', "", s, count=1)
if s == before:
    sys.exit("self-test: [\"music\", musicComponent] not found")

swap('''    Component {
        id: musicComponent
        MusicPage { objectName: "musicPage" }
    }

''', "", "musicComponent")

# 9. A comment that named the retired page.
swap("neither MusicPage nor\n    // ArtistPage carries",
     "neither AlbumPage nor\n    // ArtistPage carries", "MusicCtl toast comment")

for gone in ("openMusicSection", "musicComponent", "MusicPage", "currentMusicTab", '"music":'):
    if gone in s:
        sys.exit(f"Main.qml still mentions {gone}")
path.write_text(s)
print("main rewired")
```

```bash
python3 /tmp/w3e-main.py
```

Expected: `main rewired`.

The anchors are Phase 2 Task 8's exact text: the interim `openMusicSection` with its comment, the two `musicHomeComponent` handlers, `railKey` and `openMusicHome`'s comment. The script writes nothing until every edit has matched. If it stops, the message names the edit. Compare that spot with Phase 2 Task 8, make the numbered edits by hand, and meet the two `grep` checks below.

After the script, `Main.qml` must satisfy both of these:
- `grep -n "musicBrowse" src/ui/Main.qml` lists the context line, `case "musicBrowse"`, both functions, both Home handlers, `railKey`, the two stack bindings, the component binding, `musicBrowseComponent` and its self-test entry.
- `grep -n "MusicCtl" src/ui/Main.qml` lists only the `album`/`artist` routes and the `onActionFailed` toast connection. Phase 4 retires those.

- [ ] **Step 4: Remove the FilterBar music branches and retire `MusicPage`**

Save as `/tmp/w3e-filterbar.py` and run it from the repository root:

```python
import pathlib, sys

def swap(text, old, new, label):
    if text.count(old) != 1:
        sys.exit(f"{label}: expected 1 match, found {text.count(old)}")
    return text.replace(old, new)

def cut_lines(text, start, end, label, replacement=""):
    s = text.find(start)
    e = text.find(end, s + 1) if s >= 0 else -1
    if s < 0 or e < 0:
        sys.exit(f"{label}: anchor missing")
    s = text.rfind("\n", 0, s) + 1
    e = text.rfind("\n", 0, e) + 1
    return text[:s] + replacement + text[e:]

path = pathlib.Path("src/ui/shell/FilterBar.qml")
f = path.read_text()

f = swap(f, "genre, a person, Favorites, a different music tab) reset the query underneath\n",
         "genre, a person, Favorites) reset the query underneath\n", "header scope list")
f = cut_lines(f, "One bar, two controllers", "// Three rows of intent, in one bar:", "header controllers",
'''// ── The controller ─────────────────────────────────────────────────────────
// `controller` is LibraryCtl, passed in rather than named so the bindings read
// a shape, not a type (the UpdateBanner pattern): `sortBy`, `sortDescending`,
// `availableSorts`, `nameStartsWith`, `filtered`, `setSort()`,
// `setNameStartsWith()`, `clearFilters()`, and optionally `watchedFilter` +
// `setWatchedFilter()` for the Unwatched / Watched / Favorites row.
//
''')
f = swap(f, "//   watched chips / extras   what subset\n",
         "//   watched chips            what subset\n", "header rows")
f = cut_lines(f, "    // Page-supplied narrowing axes. See the header note for the shape.",
              "    // Where Down leaves the bar. The page points this at its content.", "extraFilters")
f = cut_lines(f, "leaves Up doing nothing there, which is what it has always done. The",
              "    // Where the grid should send Up:", "upTarget comment",
              "    // leaves Up doing nothing there, which is what it has always done.\n"
              "    property Item upTarget: null\n")
f = swap(f, '''    readonly property bool hasFavoritesToggle: bar.hasController
                                               && bar.controller.favoritesOnly !== undefined
''', "", "hasFavoritesToggle")
f = swap(f, '''    readonly property bool favoritesOn: bar.hasFavoritesToggle
                                        && bar.controller.favoritesOnly === true
''', "", "favoritesOn")
f = cut_lines(f, "    // The control the extras (and, with no extras, the clear button) chain back",
              "    // A–Z then \"#\"", "lastFixedControl", '''    // The control the clear button chains back to on Left: the last one of the
    // fixed set that is actually on screen.
    readonly property Item lastFixedControl: !bar.hasWatchedFilter ? directionButton
                                           : bar.compact ? watchedSelect
                                           : favoriteChip
    // Right out of that same set. Named once because several controls want it.
    readonly property Item clearOrNothing: clearButton.visible ? clearButton : null

''')
f = swap(f, '''    // controller.availableSorts is [{key, label}] and varies by library kind and
    // by music tab; StrmSelect wants [{text, value}].
''', '''    // controller.availableSorts is [{key, label}] and varies by library kind;
    // StrmSelect wants [{text, value}].
''', "sortModel comment")
f = swap(f, '''    //
    // ProductionYear and PlayCount are music's: a release-year sort means
    // newest records first, and "Most played" ascending is the least played.
''', "", "defaultDescending comment")
f = swap(f, '''            || key === "CommunityRating" || key === "CriticRating"
            || key === "ProductionYear" || key === "PlayCount";
''', '''            || key === "CommunityRating" || key === "CriticRating";
''', "defaultDescending keys")
f = cut_lines(f, " Extra filters ─", "    // The controller is the single source of truth", "toggledSelection")
f = swap(f, "    // or a music tab switch reset behind our back.\n",
         "    // reset behind our back.\n", "sync comment")
f = swap(f, '''        // MusicController raises tabChanged where LibraryController raises
        // scopeChanged, and neither has the other's signal.
        ignoreUnknownSignals: true
        function onQueryChanged() { bar.syncFromController(); }
        function onScopeChanged() { bar.syncFromController(); }
        function onTabChanged() { bar.syncFromController(); }
''', '''        function onQueryChanged() { bar.syncFromController(); }
        function onScopeChanged() { bar.syncFromController(); }
''', "Connections")
f = swap(f, '''                                     : bar.hasFavoritesToggle ? favoritesToggle
                                     : bar.firstExtraOrClear
''', '''                                     : bar.clearOrNothing
''', "direction right")
f = f.replace("KeyNavigation.right: bar.firstExtraOrClear\n", "KeyNavigation.right: bar.clearOrNothing\n")
f = cut_lines(f, "            // Music's whole answer to \"what subset\".",
              "            // Last in the row on purpose:", "favourites toggle and extras")
f = swap(f, '''                KeyNavigation.left: extraRepeater.count > 0
                                    ? extraRepeater.itemAt(extraRepeater.count - 1)
                                    : bar.lastFixedControl
''', '''                KeyNavigation.left: bar.lastFixedControl
''', "clear left")
f = cut_lines(f, "    // (The controllers choose the wire form: LibraryController sends",
              "    Item {\n        id: alphaHint", "alpha comment",
'''    // (LibraryController sends NameStartsWith, which matches the sort name.)
''')

for gone in ("usic", "extraFilter", "extraRepeater", "favoritesToggle", "hasFavoritesToggle",
             "firstExtraOrClear", "onTabChanged", "toggledSelection"):
    if gone in f:
        sys.exit(f"FilterBar.qml still mentions {gone}")
path.write_text(f)

path = pathlib.Path("src/ui/pages/LibraryPage.qml")
p = path.read_text()
p = cut_lines(p, "// The bar is shared with MusicPage now", "controller: LibraryCtl", "LibraryPage comment",
'''        // The bar names no controller of its own, so the page says which query
        // it governs. LibraryCtl publishes watchedFilter, so the bar renders the
        // three watched chips.
''')
path.write_text(p)

path = pathlib.Path("src/ui/controls/StrmSelect.qml")
c = path.read_text()
c = swap(c, '''    // changes: FilterBar's genre control is a Repeater delegate that goes away
    // with the filter set, and the sort control's options change per tab.
''', '''    // changes: a page swaps whole views under it, and a filter bar's sort
    // options change with the library.
''', "StrmSelect comment")
path.write_text(c)

path = pathlib.Path("src/CMakeLists.txt")
m = path.read_text()
m = swap(m, "        ui/pages/MusicPage.qml\n", "", "CMake MusicPage")
path.write_text(m)
print("filter bar trimmed")
```

Run:

```bash
python3 /tmp/w3e-filterbar.py
git rm src/ui/pages/MusicPage.qml
grep -rn "MusicPage\b\|musicPage\"\|currentMusicTab\|musicPageComponent\|openMusicSection" src tests
```

Expected:
- `filter bar trimmed`
- The `grep` lists only comments in `src/app/controllers/MusicController.{h,cpp}` and `tests/integration/tst_music_query.cpp`. Those belong to `MusicCtl`, which Phase 4 removes (contract note 13).

The `FilterBar` check for `"usic"` is deliberate: after this step the file names no music concept at all. If it trips on text the anchors did not cover, reword that text so it names no music concept.

- [ ] **Step 5: Build, test, self-test**

Run:

```bash
cmake --preset dev && cmake --build --preset dev
ctest --preset dev -R "tst_navigation_history|tst_music_query|tst_music_browse_controller" --output-on-failure
STRMQT_SELFTEST=1 QT_QPA_PLATFORM=offscreen QT_ASSUME_STDERR_HAS_CONSOLE=1 ./build/dev/strmqt; echo "exit=$?"
```

Expected:
- The build succeeds.
- All three tests PASS. `tst_navigation_history` includes `restoresPerEntryMusicBrowseState` and `musicBrowsePageInstantiatesOnlyTheActiveSection`.
- The self-test prints `selftest ok   musicBrowse` and no `music` line, and ends with `exit=0`.

- [ ] **Step 6: Commit**

```bash
git add -A src/ui/Main.qml src/ui/shell/BoundedNavigationStack.qml src/ui/shell/FilterBar.qml \
    src/ui/pages/LibraryPage.qml src/ui/controls/StrmSelect.qml src/CMakeLists.txt \
    src/ui/pages/MusicPage.qml tests/unit/tst_navigation_history.cpp \
    tests/integration/tst_music_query.cpp
git commit -m "$(cat <<'MSG'
refactor(music): route browse through musicBrowse and retire MusicPage

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
)"
rm -f /tmp/w3e-nav-test.py /tmp/w3e-stack.py /tmp/w3e-main.py /tmp/w3e-filterbar.py
```

---

### Task 7: Phase gate and visual check

**Files:**
- Modify: `config/qmllint-baseline.txt` (re-baseline once, after review)

**Interfaces:** consumes everything above. Produces a green tree, a reviewed lint baseline and a commit.

- [ ] **Step 1: Full build and test**

Run from a clean configure:

```bash
cmake --preset dev && cmake --build --preset dev
ctest --preset dev --output-on-failure
```

Expected: the build succeeds and every test passes, including `tst_music_browse_controller`, `tst_music_query`, `tst_navigation_history` and the Phase 1–2 music tests. Fix any failure in the task that owns the file, never by weakening an assertion.

- [ ] **Step 2: Review the lint delta, then re-baseline**

Run:

```bash
bash scripts/check-qmllint-baseline.sh build/dev; echo "exit=$?"
cmake --build build/dev --target strmqt_qmllint 2>&1 \
  | awk -v root="$PWD/" '/^Warning: /{h=$0; sub("^Warning: " root,"",h); sub(/:[0-9]+:[0-9]+:/,":",h); print h}' \
  | LC_ALL=C sort > /tmp/w3f-lint-now.txt
cut -f1 config/qmllint-baseline.txt | LC_ALL=C sort > /tmp/w3f-lint-base.txt
LC_ALL=C comm -13 /tmp/w3f-lint-base.txt /tmp/w3f-lint-now.txt
```

Expected:
- The script exits 1, reporting that the warning set changed. It must **not** print `qmllint found unresolvable QML types`. That fatal category is never baselined: fix the QML.
- In the `comm -13` output, every added line:
  - names `src/ui/pages/MusicBrowsePage.qml`, `src/ui/music/FilterPill.qml`, `src/ui/music/GenrePicker.qml`, `src/ui/music/CrateDividers.qml` or `src/ui/Main.qml`;
  - is `[unqualified]` or `[unresolved-type]` on a context property (`MusicBrowseCtl`, `MusicPlay`, `Actions`, `PlayerCtl`, `PlaylistCtl`, `App`).
- Lines removed (`comm -23`) for `MusicPage.qml` and `FilterBar.qml` are expected.

Fix any other added line (a missing id qualification, an unknown property, a binding loop) in the file that owns it, rebuild, and repeat this step. Then:

```bash
bash scripts/check-qmllint-baseline.sh build/dev --update
bash scripts/check-qmllint-baseline.sh build/dev; echo "exit=$?"
rm -f /tmp/w3f-lint-now.txt /tmp/w3f-lint-base.txt
```

Expected: `Updated …/config/qmllint-baseline.txt (N warnings).`, then `qmllint warning baseline matches (N warnings).` with `exit=0`.

- [ ] **Step 3: Self-test**

```bash
STRMQT_SELFTEST=1 QT_QPA_PLATFORM=offscreen QT_ASSUME_STDERR_HAS_CONSOLE=1 ./build/dev/strmqt 2>&1 | grep selftest; echo "exit=${PIPESTATUS[0]}"
```

Expected: `selftest ok   musicHome` and `selftest ok   musicBrowse`, no `music` entry, no `selftest FAIL`, and `exit=0`.

- [ ] **Step 4: Manual visual check against the mockup**

Open `docs/superpowers/specs/2026-09-16-music-crate-mockups/browse-structure.html` (option **A**) beside the app. Run `./build/dev/strmqt` signed in to a test server with a music library; the credentials come from `SecretsStore` and are never typed into a file. Check each item, and fix before committing:

1. **Frame:** the rail opens a music library on Home, and Home's **Albums** tab opens Browse:
   - The rail still highlights the library (`railKey`).
   - The strip shows Home · Albums · Artists · Songs · Genres · Playlists with the amber underline on Albums.
   - The pill row sits under it and the count readout is in mono on the right, e.g. `1,204 RECORDS · SORT: NAME ↑`.
2. **Filters:**
   - Genre opens the picker. Search narrows it, Space/Return toggles a row, **Apply** sets `Genre: Jazz ✕`, and the grid and count refetch (`1,204 → 38 RECORDS`).
   - Decade and Format open menus under their pills.
   - ♡ Favourites and Unplayed fill when on.
   - With two filters set, **Clear all** appears. ✕ on one pill clears only that filter.
3. **Sections:**
   - Switch to Artists: the filters persist, Decade hides, and *Album artists / Everyone* appear at the end. The sort is Artists' own; set Albums to Year first and confirm it survives the round trip.
   - Songs shows the table with the selection bar.
   - Genres shows bins. A bin lands on Albums with only that genre set.
   - Playlists shows **＋ New playlist** above the grid, and the pill row hides.
4. **Crate dividers:**
   - Sorted by Name, the dividers show on the right. Clicking **M** protrudes it in amber and narrows the grid.
   - LT/RT (or `[`/`]`) step letters.
   - Sorted by Year, the dividers hide and LT/RT page the grid.
5. **Keyboard and pad:**
   - LB/RB cycle sections, and from Playlists RB wraps to Home.
   - Down from the strip reaches the first pill, Down again the grid, and Up returns.
   - Left/Right walk the pills, skipping hidden ones.
   - `S` shuffles the filtered scope and names it in the queue source.
6. **History:**
   - With Songs selected, Jazz on and a card focused, open an album, then go Back: the section, filter and focused card return.
   - Forward and Back again after enough pages to evict the entry: they return by reconstruction.
7. **States:**
   - Disconnect the server mid-scroll: `Couldn't load more: …` with Retry, and the rows stay.
   - Filter to nothing: `No records match` with one clearing button per filter.
8. **Width:** at 400-logical-pixel width the pill row clips and scrolls to the focused pill, the right cluster stays whole, and nothing overlaps the dividers.

- [ ] **Step 5: Commit the baseline**

```bash
git add config/qmllint-baseline.txt
git commit -m "$(cat <<'MSG'
chore(music): re-baseline qmllint for the Crate browse page

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
)"
git status --short
```

Expected: `git status --short` shows no tracked changes from this phase. If Steps 1–4 changed code, commit those fixes first, each with a `fix(music): …` message naming what it fixed. The orchestrator also removes the parallel build directories: `rm -rf /tmp/w3a*`.

---

## Self-review

**Spec §5.1: Frame**
- Section strip with amber underline, one tab stop, shoulders cycle: Phase 2 `SectionStrip`, used by `MusicBrowsePage`. `cycleTab` → `MusicBrowseCtl.cycleSection`, wrapping through Home (Task 5).
- Pill row: pills left; `countText` in Plex Mono right; then sort, ▶ Play and ⇄ Shuffle.
  - Play is `playFiltered` → `MusicPlayback::playQuery`.
  - Shuffle is `shuffleFiltered` → `shuffleQuery`, sampled server-side (Tasks 1, 2, 5).
- A–Z crate dividers on the right edge, active letter protruding in amber, one tab stop:
  - Implemented by `CrateDividers` (Task 4).
  - Shown only for a Name sort (`letterStripVisible`); the triggers go through `jumpLetter`.
  - Sent as the `NameStartsWithOrGreater`/`NameLessThan` range by Phase 1's translator (`LetterRange`).

**Spec §5.2: Filters**
- Genre, a searchable overlay with checkboxes and counts: `GenrePicker`, with `genreOptions` rows `{id, name, count, subtitle, selected}`. Pill text `Genre: Jazz ✕` / `Genres: 3 ✕` comes from the controller (`genrePillText`).
- Decade, a menu with "Earlier": done, but **without counts** (note 6).
- Format, only if server-filterable: `formatAvailable`, with options from `MusicQueryTranslator::formatOptions`.
- ♡ Favourites and Unplayed toggles: `FilterPill` with `toggle: true`.
- ✕ clears one filter, and Clear all appears at two or more: `FilterPill.cleared`, `activeFilterCount >= 2`.
- A query change refetches the visible section and invalidates the others: `invalidate` resets hidden lanes (Task 2, tested).

**Spec §5.3: Sections**
- **Albums:** sleeve grid, caption "title / artist · year", badge only when not an Album (`releaseBadge`). Sorts per Task 2's table.
- **Artists:** round portraits, "N records", and the *Album artists / Everyone* switch at the end of the pill row.
- **Songs:** `TrackTable`, multi-select bar and type-to-jump as today. Feat. dimming and a separate format column wait for Phase 4 (notes 12, 22).
- **Genres:** bins with Size and Name sorts. A bin → Albums with only that genre (note 8).
- **Playlists:** "N tracks · runtime", **＋ New playlist** first (note 10). A single cover, not 2×2 (note 16).
- **State:**
  - Paging and per-section loading/error lanes.
  - Exact focus restore: live via `viewStates`, evicted via `navigationFocusKey` + `musicBrowse` route state (Task 6 tests).
- **Empty result:** "No records match" with a clearing button per active filter, letter included.

**Spec §8: Navigation, input and errors**
- **Routes:** `musicBrowse` carries section + query and replaces `music`, keyed `"musicBrowse:"+id`. History, forward stack and focus memory are pinned by `restoresPerEntryMusicBrowseState` and the ordering pins.
- **Input:**
  - Browse arms the `music` context (`interactionContext`).
  - Space plays/pauses; `S` shuffles the filtered scope (note 19); `L` favourites.
  - `cycleTab` and `jumpLetter` are implemented.
- **Errors:**
  - Lanes are independent per section.
  - Late replies are dropped by generation and epoch checks (Task 2 tests).
  - The paging error keeps rows on screen.
  - A session change calls `resetSessionState` (Task 3).

**The index contract**
- Every `MusicBrowseCtl` member the index lists exists with the listed type and meaning (Task 2 header). Additions are in note 3.
- Route functions and objects match the index's route table: `openMusicBrowse`, `openMusicGenre`, route `{kind:"musicBrowse", id, name, tab, key, title}` plus `query`, and objectName `musicBrowsePage`.
- Crate controls are used only through their Phase 2 APIs (`CrateSleeve`, `CratePortrait`, `GenreBinTile`, `CoverCollage`, `SectionStrip`) and the `cardComponent` protocol (note 15).
- Phase 2 Task 8's interim route, `railKey` and Home handlers are rewired by exact anchor (notes 14, 17).

**Removed, as the index's phase table requires**
- `MusicPage.qml`
- the FilterBar music branches (note 21)
- the `music` route: `prepareRoute`, `componentFor`, `reconstructedProperties`, `retainCurrentRouteState`, the self-test, `currentMusicTab`
- `tst_music_query::musicPageInstantiatesOnlyTheActiveTab` (moved, note 20)

`MusicCtl` itself stays for Phase 4 (note 13).

**Placeholder scan:** every step has complete code or an exact command with its expected output. The only hand edits are these, each with its target state stated:
- the fallback if a Phase 2 anchor has drifted
- lint fixes found in Task 7

**Known deviations:** notes 6 (no decade counts), 16 (single playlist cover), 18 (one discarded request when opening a genre from another page), 19 (`S` inert on Genres and Playlists), 22 (Songs row detail waits for Phase 4).
