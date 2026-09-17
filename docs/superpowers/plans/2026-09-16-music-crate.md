# Music "Crate" Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rebuild StrmQt's music surface as a purpose-built "Crate" experience. That means a Music Home, a Browse page, Album/Artist/Playlist pages and an "Out of the sleeve" player, fed by a music-specific DTO and composition layer instead of `MediaItem` and `MusicController`.

**Architecture:** Music data takes this path:
- `EmbyClient::getJson` → `EmbyMusicMapper` → music DTOs (`src/server/dto/music/`).
- `MusicRepository` composes, fans out and caches (`src/app/music/`).
- Typed list models → per-surface controllers → QML pages.

Playback still goes through `ItemActions`/`PlayQueue` by converting tracks with `music::toMediaItem()`. One `MusicPlayback` object owns every music play verb.

**Tech Stack:** C++20, Qt 6.8+ (6.11 target): Core, Network, Qml, Quick; libmpv; Emby 4.9.5 REST; Qt Test with the in-repo `MockEmbyServer`.

**Spec:** `docs/superpowers/specs/2026-09-16-music-crate-design.md` (mockups in `docs/superpowers/specs/2026-09-16-music-crate-mockups/`). Read the spec before starting any phase. This plan argues from it.

## How this plan is split

| File | Phase | Lands | Removes |
|---|---|---|---|
| [phase 1](2026-09-16-music-crate-phase1-data.md) | 1: Data layer | `getJson`, server verifications, music DTOs, `EmbyMusicMapper`, `MusicRepository`, models, user-data relay, `PlayQueue.sourceLabel`, `MusicPlayback`, Crate tokens, app wiring | — |
| [phase 2](2026-09-16-music-crate-phase2-home.md) | 2: Home | Crate controls, custom cards in `StrmRail`/`StrmGrid`, `MusicHomeController`, `MusicHomePage`, `musicHome` route | — |
| [phase 3](2026-09-16-music-crate-phase3-browse.md) | 3: Browse | `MusicBrowseController`, pills, genre picker, crate dividers, `MusicBrowsePage`, `musicBrowse` route | `MusicPage.qml`, the music branches of `FilterBar.qml`, the `music` route |
| [phase 4](2026-09-16-music-crate-phase4-pages.md) | 4: Album / Artist / Playlist | `AlbumController`, `ArtistController`, `MusicAlbumPage`, `MusicArtistPage`, `MusicPlaylistPage` | `AlbumPage.qml`, `ArtistPage.qml`, `MusicController`, `tst_music_query` |
| [phase 5](2026-09-16-music-crate-phase5-player.md) | 5: Player | `animateRecord` setting, `NowPlayingMusicController`, `RecordStage`, `MusicPlayerPanel`, docked audio bar | `NowPlayingPanel.qml`, unused music roles |
| [phase 6](2026-09-16-music-crate-phase6-docs.md) | 6: Docs and final gate | ARCHITECTURE.md, MUSIC.md superseded, qmllint baseline | — |

Server measurements from Phase 1 Task 2 are recorded in
`docs/superpowers/plans/2026-09-16-music-crate-verifications.md`. Several later tasks
branch on those outcomes, and each branch is written out in full where it occurs.

## Cross-phase handoffs

Each phase file opens with **Contract notes**. Where they differ from this index, the notes win, for that phase and every later one. The notes that reach across phases:

- **Phase 1 additions made later:**
  - Phase 2 adds `MusicPlayback::shuffleStation`/`queueStation`.
  - Phase 3 adds `MusicRepository::coverGenres` and `MusicPlayback::playQuery`.
  - Phase 4 adds `MusicRepository::artistTracks` and `MusicPlayback::shuffleArtist`.
- **Routes:**
  - Phase 2 wires Home to an interim `root.openMusicSection`, which Phase 3 deletes and replaces with `openMusicBrowse`/`openMusicGenre`.
  - `railKey` strips `musicHome:` (Phase 2) and `musicBrowse:` (Phase 3).
- **`MusicCtl`:**
  - Phase 3 removes its browse consumers.
  - Phase 4 deletes `MusicController`, `AlbumPage`, `ArtistPage` and `tst_music_query`.
  - Phase 3 first moves the active-section test to `tst_navigation_history`.
- **Songs:** keeps `TrackTable` (bound to `MusicBrowseCtl`) until Phase 4's `CrateTrackTable`.
- **Playlists:** Phase 4 refreshes `MusicBrowseCtl.playlistsLane` after `playlistsMutated`.
- **Player (Phase 5):**
  - `RecordStage` uses `recordState`, not `state`.
  - The `animateRecord` setting lives under Appearance.
- **qmllint baseline:** Phase 5 Task 9 updates it once, and Phase 6 Task 3 re-checks it. `--update` may only remove lines.

## Global Constraints

- C++20, Qt 6. CMake requires Qt 6.8 and the target machine runs 6.11. No new third-party dependencies.
- **Layers:** `src/server/**` and `src/core/**` never include QtGui. Music DTOs and `EmbyMusicMapper` go in `strmqt_core`; the repository, models, controllers and `MusicPlayback` go in `strmqt_app`.
- **Emby access:** `MusicRepository` is the only music unit that holds an `EmbyClient`. QML never reshapes data: display strings (durations, "N records", badges) are formatted in C++.
- **JSON parsing is tolerant:** unknown fields are ignored and missing fields defaulted. A malformed audio stream yields `AudioFormat{}`, which shows no badge.
- **Async:** every async method returns `QFuture<Result<T>>`, resolved on the caller's thread (`.then(this, …)`). Every controller guards replies with a generation counter and drops superseded replies.
- **Partial results:** a composed method succeeds when its *core* request succeeds. Failed secondary parts come back empty and are logged with `qCWarning`.
- **Cache:** per account, in memory. TTLs: Home shelves and genre bins 5 min, sleeves and album tracks 10 min, cover samples for the session. Cleared on `EmbyClient::identityChanged`. No on-disk catalogue.
- **Emby quirks (measured, ARCHITECTURE.md §2):**
  - A letter is sent as `NameStartsWithOrGreater=L&NameLessThan=L+1`. "#" is `NameLessThan=A`; "Z" has no upper bound.
  - `/MusicGenres` honours `ParentId`; page on the returned array's size.
  - `SortBy=Random` cannot page, so it is always one page.
  - `InstantMix` does not page and returns duplicates, so de-duplicate by id.
  - Playlists are scoped with `ParentId`.
- **Visual:** Crate is a *dialect* of Projection Booth and adds no colours. The cover wash keeps its clamp (saturation ≤ 0.55, luminance 0.10–0.22, opacity ≤ 22%).
- **Focus and input:**
  - Hover is not focus. Focus draws the amber ring.
  - A shelf, strip or list is one tab stop. Vertical moves keep the column (`NavigationColumn`).
  - `Main.qml` alone owns navigation, and QML states intent while C++ decides.
- **Security:** no credentials, tokens or server URLs in the repo or in test fixtures. Use the fixture ids already in the tests: user `a1b2c3d4e5f60718293a4b5c6d7e8f90`, token `not-a-real-token-fixture-only`, music library `1868998`.
- **Build and test:** `cmake --preset dev`, `cmake --build --preset dev`, `ctest --preset dev`. Warnings are errors. A phase ends only when all three pass, `bash scripts/check-qmllint-baseline.sh build/dev` reports no *new* warnings, and `STRMQT_SELFTEST=1 QT_QPA_PLATFORM=offscreen QT_ASSUME_STDERR_HAS_CONSOLE=1 ./build/dev/strmqt` exits 0.
- **Git:**
  - One conventional commit per task (`feat(music): …`, `test(music): …`, `refactor(music): …`, `docs: …`). End each message with `Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>`.
  - Never push, amend, rebase or force.
  - The release is held for this whole refactor, so old music code may be deleted as soon as a phase replaces it.

## Shared vocabulary (names every phase relies on)

A task implementer sees only their own task. Every cross-task name is fixed here.
Do not rename them.

**DTOs:** `namespace strmqt::music`. Files: `src/server/dto/music/MusicTypes.h`, `MusicQuery.h`, `MusicMediaItem.h/.cpp`.

| Name | Kind |
|---|---|
| `NamedRef { QString id, name; }`, `using ArtistRef = NamedRef; using GenreRef = NamedRef;` | struct |
| `AudioFormat { codec, bitDepth, sampleRate, bitrate, channels, badge, isLossless, isHiRes }` | struct |
| `enum class ReleaseType { Album, EP, Single, Compilation }`, `QString releaseTypeName(ReleaseType)` | enum + fn |
| `Track { id, title, artists, albumArtists, albumId, albumTitle, discNumber, trackNumber, runtimeMs, positionMs, favourite, played, playCount, lastPlayed, dateAdded, format, coverRef, playlistItemId, featured, displayTitle, differsFromAlbumArtist }` | struct |
| `Album { id, title, albumArtists, year, premiereDate, genres, studios, dateAdded, trackCount, runtimeMs, favourite, playCount, lastPlayed, coverRef, releaseType, releaseTypeFromServer, discCount, formatSummary }` | struct |
| `Disc { number, runtimeMs, tracks }`, `AlbumSleeve { album, discs, moreByArtist }` | struct |
| `Artist { id, name, coverRef, backdropRef, albumCount, trackCount, favourite }` | struct |
| `ArtistProfile { artist, albums, epsAndSingles, appearsOn, topTracks, similar }` | struct |
| `GenreBin { id, name, recordCount, covers }` | struct |
| `enum class StationKind { HeavyRotation, Favourites, DeepCuts, MoreLike, ShuffleAll }`, `Station { kind, label, seedId, covers }` | enum + struct |
| `ContinueListening { album, resumeTrack, resumeIndex, progress, isValid() }` | struct |
| `Playlist { id, name, trackCount, runtimeMs, dateAdded, coverRef }` | struct |
| `template<class T> struct Page { QList<T> items; int totalRecordCount; int startIndex; }` | struct |
| `using ImageRef = MediaItem::ImageRef` (`itemId, imageType, tag`) | alias |
| `enum class Section { Albums, Artists, Songs, Genres, Playlists }`, `enum class FormatFilter { Any, Lossless, Lossy, HiRes }`, `enum class ArtistMode { AlbumArtists, Everyone }`, `kDecadeAny = 0`, `kDecadeEarlier = -1`, `MusicQuery { libraryId, section, sortKey, descending, letter, genreIds, decade, format, favouritesOnly, unplayedOnly, artistMode; hasFilters() }` | query |
| `MediaItem toMediaItem(const Track &)`, `toMediaItem(const Album &)`, `toMediaItem(const Artist &)` | fns (spec §3.6's `Track::toMediaItem()` as free overloads) |

**Mapper:** `namespace strmqt::emby`, `src/server/emby/EmbyMusicMapper.h/.cpp`:
`deriveAudioFormat`, `parseAudioFormat`, `FeaturedSplit`/`splitFeatured`, `parseEmbyDate`, `releaseTypeFromTag`, `parseTrack(s)`, `parseAlbum(s)`, `parseArtist(s)`, `parseGenreBin`, `parsePlaylist(s)`, `ReleaseEvidence`, `classifyRelease(const ReleaseEvidence &)`, `groupDiscs`, `refineAlbumFromTracks`, `dominantFormat`.

**Server capabilities:** `namespace strmqt::emby::caps`, `src/server/emby/MusicServerCapabilities.h`, written from the Task 2 measurements: `kAudioCodecsFiltersAudio`, `kAudioCodecsFiltersAlbums`, `kHiResFilter`, `kHiResQueryKey`, `kHiResQueryValue`, `kYearsAcceptsDecade`, `kReleaseTypeField`, `kLyricsAvailable`, `kSimilarArtists`, `kSimilarArtistsPath`, `kMinDateCreated`, `kAlbumPlayCountSort`, `kGenreItemCounts`, `kMediaStreamsOnLists`, `kAlbumRuntimeOnLists`. Later phases branch on these constants at compile time, never on a runtime probe.

**Client additions:**
- `EmbyClient::getJson(const QString &path, const QUrlQuery &query, RequestHandle *handle = nullptr)`. `{uid}` in the path or in any query value becomes the session's user id.
- `static QUrlQuery EmbyClient::itemsParams(const ItemsQuery &)` and `static QUrlQuery EmbyClient::artistParams(const QString &userId, const ItemsQuery &)` are public.
- `ItemsQuery` gains `years`, `audioCodecs`, `minDateCreated`, `minPremiereDate`, `maxPremiereDate`, `ids`.
- `strmqt-cli get PATH [Key=Value…]` prints the JSON reply; used by Task 2.
- Tests: `MockEmbyServer::addQueryRoute(method, path, QList<QPair<QString,QString>> required, status, body)`.

**App layer:** `namespace strmqt::music`, `src/app/music/`:
- `TtlCache<T>`, `Fanout`.
- `MusicQueryTranslator`: `SortKey`, `LetterRange`, `toItemsQuery`, `sortKeysFor`, `sortFields`, `letterRange`, `codecsFor`, `formatFilterable`, `formatOptions`.
- `MusicFormat.h`: `formatDuration`, `formatRuntime`, `formatRecordCount`, `formatTrackCount`, `joinNames`, `coverUrl`.
- `MusicRepository` with `enum class Freshness { Listening, Favourites, Everything }` and `struct NewAlbums { QList<Album> albums; int addedThisWeek = -1; }`.
  - Methods: `albumTracks`, `albumSleeve`, `artistProfile`, `continueListening`, `recentAlbums`, `newAlbums`, `allGenres`, `genreBins`, `topArtists`, `forgottenFavourites`, `randomAlbums`, static `stations`, `resolveStation`, `browseAlbums`, `browseArtists`, `browseTracks`, `browsePlaylists`, `sampleTracks`, `markStale`, `noteUserDataChanged`, `clear`.
  - The exact signatures are in Phase 1 Tasks 10–12.
- `UserDataPatch { optional favourite, played, playCount, positionMs }`, `MusicUserDataRelay` (`addModel`, `bind`, `apply`).
- `MusicPlayback`: `playTracks`, `playAlbum`, `shuffleAlbum`, `radio`, `playStation`, `playStationTile`, `shuffleQuery`, static `toMaps`.

**Models:** `src/app/music/models/`:
- `MusicModelBase`, which provides `count`, `totalRecordCount`, `canLoadMore`, `get(row)`, `indexOfNavigationIdentity("i:"+id | "p:"+playlistItemId)`, `idAt` and `applyUserData`.
- `MusicListModel<T>`, which provides `setItems`, `appendItems`, `clear`, `items` and `at`.
- `AlbumGridModel`, `ArtistGridModel`, `PlaylistGridModel`, `TrackListModel` (serves every `MediaItemModel` role plus music roles; `mediaMaps()`), `GenreBinModel`, `StationModel`.

`static QVariantMap MediaItemModel::mapForItem(const MediaItem &)` backs every `get()`, so ItemMenu and ItemActions receive the maps they already understand.
The spec's `ShelfModel` is dropped. Every Home lane publishes its own typed model, and stations get `StationModel`.

**Controllers:** `src/app/controllers/music/`. Each is exposed to QML under the context property in brackets:
- `MusicHomeController` (`MusicHomeCtl`)
- `MusicBrowseController` (`MusicBrowseCtl`)
- `AlbumController` (`AlbumCtl`)
- `ArtistController` (`ArtistCtl`)
- `NowPlayingMusicController` (`NowPlayingMusicCtl`)

`MusicPlayback` is exposed as `MusicPlay`.

**Queue:** `PlayQueue::sourceLabel` (`Q_PROPERTY … NOTIFY sourceLabelChanged`) and `setSourceLabel`.
`ItemActions::playAllFromIfCurrent(items, startIndex, generation, const QString &sourceLabel = QString())`.

**Routes** (`Main.qml` / `BoundedNavigationStack.qml`):
- `musicHome`: `id` = library id, key `"musicHome:"+id`.
- `musicBrowse`: `id` = library id, `tab` = section name `albums|artists|songs|genres|playlists`, key `"musicBrowse:"+id`.
- `album` and `artist`: kinds and keys unchanged.
- `playlist` with `mode: "audio"`: the audio playlist page.

Page `objectName`s: `musicHomePage`, `musicBrowsePage`, `albumPage`, `artistPage`, `musicPlaylistPage`.

**User-data seam (deliberate deviation from spec §3.6.3, recorded here):**
- `ItemActions::registerModel` is *not* generalised, because its internals depend on `MediaItemModel::items()`.
- Music models implement `applyUserData(itemId, UserDataPatch)`.
- `MusicUserDataRelay` (in Application) feeds them from `ItemActions::favoriteChanged`/`playedChanged` and `LiveUpdateService::userDataPatched`, and tells `MusicRepository` to invalidate.
- The observable behaviour is the spec's: favourites and play counts patch in place everywhere.

## Phase 2–5 contract (controllers, routes, controls)

Fixed here so the UI phases can be written and run independently. A phase file may *add* members; it must not rename or re-type anything listed.

### Lanes: `src/app/controllers/music/MusicLane.h/.cpp` (Phase 2)

`class MusicLane : QObject` is one independently loading shelf or section.
- Properties:
  - `Q_PROPERTY(QObject *model READ model CONSTANT)`
  - `Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)`
  - `Q_PROPERTY(QString error READ error NOTIFY stateChanged)`
  - `Q_PROPERTY(bool empty READ empty NOTIFY stateChanged)`: true when not loading, no error, and the model has no rows
  - `Q_PROPERTY(bool ready READ ready NOTIFY stateChanged)`: has loaded at least once
- `Q_INVOKABLE void retry()` emits `retryRequested()`.
- C++ side:
  - `MusicLane(MusicModelBase *model, QObject *parent)`
  - `quint64 begin()` sets loading, clears the error, bumps and returns the generation
  - `bool isCurrent(quint64) const`
  - `void succeed(quint64)`, `void fail(quint64, const QString &error)`

  A superseded generation is ignored.

### Controllers: `src/app/controllers/music/`

Every controller is constructed with `(music::MusicRepository *repository, music::MusicPlayback *playback, QObject *parent)`. Application registers every model it owns with `musicRelay()->addModel(...)` and exposes the controller under the context property in brackets.

**`MusicHomeController` (`MusicHomeCtl`, Phase 2):**
- `libraryId` (read-only, NOTIFY), `Q_INVOKABLE open(libraryId)`, `Q_INVOKABLE refreshStale()`
  - Home calls `refreshStale()` when it becomes visible; within the TTL it costs nothing.
- `hero`: `QVariantMap`, NOTIFY `heroChanged`.
  - Keys: `mode` (`"resume"` | `"pullOne"` | `""`), `albumId`, `title`, `artist`, `artistId`, `year`, `summary` ("artist · 1997 · 12 tracks · 48 min"), `coverUrl`, `progress` (0–1), `resumeIndex`, `resumeLabel` ("Resume track 4"), `favourite`, `albumItem` (a `MediaItemModel::mapForItem` map, for menus, `Actions` and `root.openAlbum`).
- `heroLane` (its model is `heroRecent`, an `AlbumGridModel` holding the ≤ 3 recent albums beside the hero).
- Shelf lanes, in shelf order:
  - `recentLane` (`AlbumGridModel`)
  - `newLane` (`AlbumGridModel`), plus `addedThisWeekText` (`""` when hidden)
  - `stationLane` (`StationModel`)
  - `genreLane` (`GenreBinModel`), plus `allGenresText` ("All 42 genres")
  - `artistLane` (`ArtistGridModel`)
  - `forgottenLane` (`AlbumGridModel`)
  - `pullLane` (`AlbumGridModel`)
- Invokables:
  - `resumeHero()`, `shuffleHero()`, `anotherOne()` (a new pull-one-out hero)
  - `reshuffle()` (refills `pullLane`)
  - `playStation(int row)`
  - `cycleSection(int step)`, which returns the section key the strip should open

**`MusicBrowseController` (`MusicBrowseCtl`, Phase 3):**
- `libraryId`, `Q_INVOKABLE open(libraryId, section)`
- `section` (`"albums" | "artists" | "songs" | "genres" | "playlists"`, READ WRITE NOTIFY)
- Models and lanes:
  - `albums` / `albumsLane` (`AlbumGridModel`)
  - `artists` / `artistsLane` (`ArtistGridModel`)
  - `songs` / `songsLane` (`TrackListModel`)
  - `genres` / `genresLane` (`GenreBinModel`)
  - `playlists` / `playlistsLane` (`PlaylistGridModel`)
  - `currentLane`
  - `Q_INVOKABLE loadMore()`, for the current section
- Query (per-section sort memory; the filters are shared):
  - `sortKey` (RW), `sortDescending` (RW), `availableSorts` (`QVariantList` of `{key, label}`), `sortLabel`
  - `letter` (RW; `""` = none), `letters` (`["#","A",…,"Z"]`), `letterStripVisible`
  - `genreIds` (`QStringList`), `genrePillText`, `decade` (int; `0` any, `-1` earlier), `decadePillText`, `format` (`"any" | "lossless" | "lossy" | "hires"`), `formatPillText`, `formatAvailable`
  - `favouritesOnly` (RW), `unplayedOnly` (RW), `artistMode` (`"albumArtists" | "everyone"`, RW)
  - `filtered`, `activeFilterCount`, `resultCount`, `unfilteredCount`, `countText` ("1,204 → 38 RECORDS · SORT: YEAR ↓")
- Options:
  - `genreOptions` (`QVariantList` of `{id, name, count, selected}`)
  - `decadeOptions` (`{value, label}`)
  - `formatOptions` (`{key, label}`)
- Invokables: `setGenres(QStringList)`, `toggleGenre(id)`, `clearGenres()`, `setDecade(int)`, `setFormat(key)`, `clearFilters()`, `openGenre(genreId, genreName)` (switches to Albums with only that genre set), `playFiltered()`, `shuffleFiltered()`, `jumpLetter(int step)` (returns bool), `cycleSection(int step)` (returns the new section key, or `"home"`).

**`AlbumController` (`AlbumCtl`, Phase 4):**
- `Q_INVOKABLE open(albumId, name)`, `albumId`, `loading`, `error`, `Q_INVOKABLE retry()`
- Display: `title`, `artist`, `artistId`, `kicker` ("EP · 1997"), `coverUrl`, `formatBadge`, `isHiRes`, `discCount`, `discBadge` ("2 DISCS" or `""`), `trackSummary` ("12 tracks · 48 min"), `favourite`
- `linerNotes`: `QVariantList` of `{label, value, links: [{id, name}]}`, empty rows omitted
- `albumItem` (a map), `tracks` (`TrackListModel`), `moreBy` (`AlbumGridModel`), `moreByTitle` ("More by X")
- `play(int fromIndex)`, `shuffle()`, `radio()`

**`ArtistController` (`ArtistCtl`, Phase 4):**
- `Q_INVOKABLE open(artistId, name, libraryId = "")`, `artistId`, `loading`, `error`, `retry()`
- Display: `name`, `kicker` ("Artist · 14 records · 212 tracks"), `coverUrl`, `backdropUrl`, `favourite`, `artistItem`
- Models: `albums`, `epsAndSingles`, `appearsOn` (`AlbumGridModel`), `topTracks` (`TrackListModel`), `similar` (`ArtistGridModel`)
- `tabs`: `QVariantList` of `{key, label, count}`, non-empty tabs only
- `shuffle()`, `radio()`
- Phase 4 adds `MusicRepository::artistTracks(const QString &artistId, int limit)` (random, `ArtistIds`) and `MusicPlayback::shuffleArtist(artistId, name)` (label `"Shuffle · " + name`).

**`NowPlayingMusicController` (`NowPlayingMusicCtl`, Phase 5):**
- `active` (the current queue item is audio), `title`, `artist`, `artistId`, `album`, `albumId`, `coverUrl`, `favourite`, `readout` ("FLAC 24/96 · DIRECT PLAY")
- `sourceLabel` (the queue's `sourceLabel`, else its `contextLabel`)
- `recordState` (`"playing" | "paused" | "buffering" | "stopped"`)
- `albumTracks` (`TrackListModel`), `currentAlbumRow`
- `lyricsAvailable`, plus `lyrics` (`QVariantList` of `{timeMs, text}`; `timeMs` = −1 for untimed), only when `caps::kLyricsAvailable`
- Signal `albumChanging()`, emitted before a different album's track becomes current, which drives the record's slide in and out
- `Q_INVOKABLE playAlbumFrom(int row)`

### Routes (`Main.qml`, `BoundedNavigationStack.qml`)

| Function | Route | Page (`objectName`) |
|---|---|---|
| `root.openMusicHome(libraryId, name)` | `{kind:"musicHome", id, name, key:"musicHome:"+id, title:name}` | `MusicHomePage` (`musicHomePage`) |
| `root.openMusicBrowse(libraryId, name, section)` | `{kind:"musicBrowse", id, name, tab:section, key:"musicBrowse:"+id, title:name}` | `MusicBrowsePage` (`musicBrowsePage`) |
| `root.openMusicGenre(libraryId, name, genreId, genreName)` | `musicBrowse` with `tab:"albums"`; calls `MusicBrowseCtl.openGenre` first | same |
| `root.openAlbum(item)` / `root.openArtist(item)` | unchanged kinds and keys; `prepareRoute` calls `AlbumCtl.open` / `ArtistCtl.open` | `MusicAlbumPage` (`albumPage`) / `MusicArtistPage` (`artistPage`) |
| `root.openMusicPlaylist(id, name)` | `{kind:"playlist", mode:"audio", id, name, key:"musicPlaylist:"+id, title:name}` | `MusicPlaylistPage` (`musicPlaylistPage`) |

- `openLibrary` sends `collectionType === "music"` to `openMusicHome` (Phase 2).
- `interactionContext` is `"music"` for the objectNames `musicHomePage`, `musicBrowsePage`, `albumPage`, `artistPage` and `musicPlaylistPage`.
- Music pages implement `cycleTab(step)`. `MusicBrowsePage` also implements `jumpLetter(step)`.
- Every new page is added to the self-test page list.

### Crate controls: `src/ui/music/` (Phase 2 unless noted; module `StrmQt`, listed in `src/ui/music/Music.cmake`)

| Control | API |
|---|---|
| `CrateSleeve` | `coverUrl`, `title`, `subtitle`, `badge`, `hiRes`, `size`, `current` (focus-ring owner), `showCaption`; signals `activated()`, `playRequested()`, `menuRequested(real x, real y)` |
| `CratePortrait` | `imageUrl`, `name`, `subtitle`, `size`, `current`; signals `activated()`, `menuRequested(real x, real y)` |
| `CrateBadge` | `text`, `hiRes` |
| `CrateKicker` | `text`, `color` (default `Theme.textSecondaryColor`) |
| `CrateHeading` | `text`, `pixelSize` (default `Theme.crateShelfHeading`): wide Archivo caps |
| `CoverCollage` | `covers` (list of URLs, 1–4), `size`, `radius` |
| `StationTile` | `covers`, `label`, `size`, `current`; signals `activated()`, `menuRequested(real x, real y)` |
| `GenreBinTile` | `name`, `subtitle`, `covers`, `size`, `current`, `isAllBin`; signals `activated()` |
| `SectionStrip` | `currentKey`, `keys` (default `["home","albums","artists","songs","genres","playlists"]`); signal `sectionChosen(string key)`; `function cycle(step): bool` |
| `CrateShelf` | `title`, `kicker`, `lane` (`MusicLane`), `delegate` (`Component`), `actionText`, `navigationFocusKey`; signals `actionTriggered()`, `itemActivated(int index)`, `itemPlayRequested(int index)`, `menuRequested(int index, real x, real y)`. Hidden when `lane.empty`; shows a skeleton row while loading and `ShelfError` on error |
| `ShelfError` | `message`; signal `retry()` |
| `FilterPill` (P3) | `text`, `active`, `clearable`; signals `activated()`, `cleared()` |
| `GenrePicker` (P3) | an overlay: `options`, `open()`, `close()`; signal `genresChosen(var ids)` |
| `CrateDividers` (P3) | `letters`, `currentLetter`; signal `letterChosen(string letter)` |
| `LinerNotes` (P4) | `rows` (`AlbumCtl.linerNotes`); signal `linkActivated(string id, string name)` |
| `RecordStage` (P5) | `coverUrl`, `state` (`recordState`), `animate` (`Prefs.animateRecord`), `spinning` (derived); `function changeAlbum(newCoverUrl)` |
| `MusicPlayerPanel` (P5) | the tabs Up next / Album / Lyrics bound to `NowPlayingMusicCtl` and `PlayerCtl.queue` |

`StrmRail` and `StrmGrid` gain `property Component cardComponent: null` (Phase 2). When it is set, the delegate loads it instead of `StrmCard` and passes `model`, `index`, `current` and `hovered`. Focus, navigation memory and paging are unchanged.

## File structure (complete)

```
src/server/dto/ItemsQuery.h                         (modify: years, audioCodecs, date bounds)
src/server/emby/EmbyClient.h/.cpp                   (modify: getJson, new ItemsQuery axes)
src/cli/main.cpp                                    (modify: `get` probe command)
src/server/dto/music/MusicTypes.h                   (new: value types)
src/server/dto/music/MusicQuery.h                   (new)
src/server/dto/music/MusicMediaItem.h/.cpp          (new: toMediaItem overloads)
src/server/emby/EmbyMusicMapper.h/.cpp              (new)
src/server/emby/MusicServerCapabilities.h           (new: measured constants, Task 2)
docs/superpowers/plans/2026-09-16-music-crate-verifications.md (new: Task 2 record)
src/app/music/TtlCache.h                            (new, header-only)
src/app/music/Fanout.h                              (new, header-only)
src/app/music/MusicQueryTranslator.h/.cpp           (new)
src/app/music/MusicRepository.h/.cpp                (new)
src/app/music/UserDataPatch.h                       (new)
src/app/music/MusicUserDataRelay.h/.cpp             (new)
src/app/music/MusicPlayback.h/.cpp                  (new)
src/app/music/models/MusicModelBase.h/.cpp          (new)
src/app/music/models/MusicListModel.h               (new, header-only template)
src/app/music/models/PlaylistGridModel.h/.cpp       (new)
src/app/music/models/AlbumGridModel.h/.cpp          (new)
src/app/music/models/ArtistGridModel.h/.cpp         (new)
src/app/music/models/TrackListModel.h/.cpp          (new)
src/app/music/models/GenreBinModel.h/.cpp           (new)
src/app/music/models/StationModel.h/.cpp            (new)
src/app/music/MusicFormat.h/.cpp                    (new: duration/count strings)
src/app/controllers/music/MusicLane.h/.cpp                (phase 2)
src/app/controllers/music/MusicHomeController.h/.cpp      (phase 2)
src/app/controllers/music/MusicBrowseController.h/.cpp    (phase 3)
src/app/controllers/music/AlbumController.h/.cpp          (phase 4)
src/app/controllers/music/ArtistController.h/.cpp         (phase 4)
src/app/controllers/music/NowPlayingMusicController.h/.cpp (phase 5)
src/app/models/MediaItemModel.h/.cpp                (modify: static mapForItem)
src/app/PlayQueue.h/.cpp                            (modify: sourceLabel)
src/app/ItemActions.h/.cpp                          (modify: label param)
src/app/Application.h/.cpp, src/app/main.cpp        (modify: wiring, context properties)
src/core/Settings.h/.cpp                            (phase 5: animateRecord)
src/ui/Theme.qml                                    (modify: crate tokens)
src/ui/music/Music.cmake                            (new: QML fragment for music UI)
src/ui/music/CrateSleeve.qml, CratePortrait.qml, CrateBadge.qml, CrateKicker.qml, CrateHeading.qml,
             CoverCollage.qml, StationTile.qml, GenreBinTile.qml, SectionStrip.qml,
             CrateShelf.qml, ShelfError.qml                       (phase 2)
src/ui/music/FilterPill.qml, GenrePicker.qml, CrateDividers.qml   (phase 3)
src/ui/music/LinerNotes.qml, CrateTrackTable.qml                  (phase 4)
src/ui/music/RecordStage.qml, MusicPlayerPanel.qml                (phase 5)
src/ui/pages/MusicHomePage.qml (2), MusicBrowsePage.qml (3), MusicAlbumPage.qml,
             MusicArtistPage.qml, MusicPlaylistPage.qml (4)
src/ui/controls/StrmRail.qml, StrmGrid.qml          (phase 2: cardComponent override)
src/ui/shell/BoundedNavigationStack.qml, src/ui/Main.qml (phases 2–4: routes)
src/ui/shell/MiniPlayer.qml, src/ui/pages/PlayerPage.qml (phase 5)
tests/mocks/MockEmbyServer.h/.cpp                   (modify: addQueryRoute)
tests/mocks/emby/music/*.json                       (new fixtures)
tests/unit/tst_music_mapper.cpp, tst_music_models.cpp, tst_music_query_translator.cpp,
             tst_music_cache.cpp, tst_music_user_data_relay.cpp, tst_crate_tokens.cpp,
             tst_play_queue.cpp (extended)
tests/integration/tst_music_repository.cpp, tst_music_playback.cpp, tst_item_actions_queue.cpp and tst_emby_client.cpp (extended),
             tst_music_home_controller.cpp, tst_music_browse_controller.cpp,
             tst_album_artist_controllers.cpp, tst_now_playing_music.cpp
```

## Execution notes for the orchestrator

- **Order:** phases run in order, and so do the tasks inside a phase. Some tasks within a phase are independent and may run in parallel waves; each phase file lists its safe waves at the top.
- **Parallel builds:**
  - Each parallel agent builds in its own directory: `cmake -S . -B /tmp/w<wave><agent> --preset dev` with `TMPDIR=/tmp/w<wave><agent>/tmp`.
  - Check `df -h /tmp` before a wave.
  - At the wave's integration gate, the orchestrator runs `rm -rf /tmp/w<wave>*` in the same step as the commit (AGENTS.md).
- **Branch outcomes:** Phase 1 Task 2 needs the live server. If there is no stored session (`./build/dev/strmqt-cli libraries` prints `error: not logged in. Run: strmqt-cli login --user NAME`), stop and ask the user to run `! ./build/dev/strmqt-cli login --user <their name>`. Do not guess outcomes. Never write the server URL, user name or token into any file.
- **Commits in parallel waves:** agents share one working tree, so in a multi-agent wave they do not commit. The orchestrator builds and tests the integrated tree at the gate, commits each task separately in task order with that task's message, and removes `/tmp/w<wave>*` in the same step as the last commit.
- **Visual check:** after Phases 2–5, ask the user to look at the running app. QML polish is verified manually and noted in the commit (AGENTS.md).
