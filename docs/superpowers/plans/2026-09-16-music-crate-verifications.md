# Music Crate: Emby server measurements

Phase 1 Task 2 of the Crate plan. Measured against the live Emby 4.9.5 server on
2026-09-17, through `strmqt-cli get`, in the music library. Library totals are
recorded; ids, names and URLs are not. The constants live in
`src/server/emby/MusicServerCapabilities.h`.

Library size at measurement: 5,037 albums, 56,283 tracks, 289 music genres.

## V1a AudioCodecs filters Audio
- Emby 4.9.5, measured 2026-09-17
- Audio, no filter: 56,283; `AudioCodecs=flac`: 55,400; `AudioCodecs=mp3`: 516
- Verdict: honoured → `kAudioCodecsFiltersAudio = true`

## V1b AudioCodecs filters MusicAlbum
- Emby 4.9.5, measured 2026-09-17
- Albums, no filter: 5,037; `AudioCodecs=flac`: 0; `AudioCodecs=mp3`: 0
- The totals differ, but only because every album is dropped: 55,400 flac tracks
  belong to albums that the filter reports as having none. The parameter removes
  albums rather than filtering them, so it is not usable.
- Verdict: not honoured → `kAudioCodecsFiltersAlbums = false`

## V1c Hi-res filter
- Emby 4.9.5, measured 2026-09-17
- A random sample of 800 tracks held 134 hi-res streams (flac 24/44.1–192 kHz,
  DSD at 352.8 kHz), so a working filter would have lowered the total.
- Audio `AudioCodecs=flac`: 55,400. Adding any of `MinAudioBitDepth=24`,
  `MinBitDepth=24`, `MinSampleRate=88200`, `MinAudioSampleRate=88200`,
  `AudioBitDepths=24`, `BitDepth=24`, `MinAudioBitRate=2000000`,
  `MinBitrate=2000000`: 55,400 each (ignored)
- Verdict: no hi-res filter → `kHiResFilter = false`, `kHiResQueryKey = ""`,
  `kHiResQueryValue = ""`

## V2 Years accepts a decade list
- Emby 4.9.5, measured 2026-09-17
- Albums, no filter: 5,037; `Years=1970,…,1979`: 362; `Years=1975`: 21
- Verdict: honoured → `kYearsAcceptsDecade = true`

## V3 Release-type field
- Emby 4.9.5, measured 2026-09-17
- Album list items carry no `AlbumType`, `ReleaseType` or `ExtraType` key, even with
  `Fields=…,Tags,ExtraType`. `TagItems` is empty, and `Fields=Tags` over the first
  2,000 albums returned no tags at all.
- Verdict: none → `kReleaseTypeField = ""`; the release-type heuristic stands alone

## V4 Lyrics
- Emby 4.9.5, measured 2026-09-17
- `/Audio/{id}/Lyrics`: HTTP 500. `/Items/{id}/Lyrics`: HTTP 404.
- Lyrics are exposed as an embedded text subtitle stream on the track: in 200
  tracks with `Fields=MediaStreams`, 5 carried a stream with `"Type": "Subtitle"`,
  `"Codec": "text"`, `"Title": "Lyrics"`, `"IsTextSubtitleStream": true`,
  `"IsExternal": false`, `"SupportsExternalStream": true` (index 2 in each case).
- Working path: `/Items/{id}/{mediaSourceId}/Subtitles/{streamIndex}/Stream.js`
  (`/Videos/…` answers identically). The media source id is `MediaSources[0].Id`.
- Response shape: `{"TrackEvents": [{"Text": "…"}, …]}`. All five sampled tracks
  were **untimed**: no `StartPositionTicks`/`EndPositionTicks` on any event. Some
  tracks return one event whose `Text` joins lines with `"; "` (an empty line is
  `"; ; "`); others return one event per line (15 and 6 events).
- A timed lyrics file was not found in the sample; a timed event is expected to
  carry `StartPositionTicks` (ticks, 10,000 per ms), but that is unmeasured.
- Verdict: available (text) → `kLyricsAvailable = true`

## V5 Similar artists
- Emby 4.9.5, measured 2026-09-17
- `/Artists/{id}/Similar UserId={uid} Limit=8`: 5 items, all `MusicArtist`
- `/Items/{id}/Similar UserId={uid} Limit=8`: the same 5 `MusicArtist` items
- Verdict: works → `kSimilarArtists = true`, `kSimilarArtistsPath = "/Artists/{id}/Similar"`

## V6 MinDateCreated filters MusicAlbum
- Emby 4.9.5, measured 2026-09-17
- Newest album `DateCreated` is 2023-05-13, oldest 2015-10-14, so recent windows are
  legitimately empty. Albums, no filter: 5,037; `MinDateCreated` 2000-01-01: 5,037;
  2020-01-01: 4,634; 2023-05-13T04:00Z: 108; 2025-01-01, 30 days ago, 7 days ago: 0
- Verdict: honoured → `kMinDateCreated = true`

## V7 Album PlayCount / DatePlayed sort
- Emby 4.9.5, measured 2026-09-17
- `MusicAlbum SortBy=PlayCount SortOrder=Descending Limit=5`: every `UserData.PlayCount`
  is 0, with and without `Fields=UserDataPlayCount,UserDataLastPlayedDate`.
- The album of the most recently played track, fetched on its own, has
  `PlayCount: 0`, `Played: false` and no `LastPlayedDate`. Emby keeps no play data on
  albums, so `SortBy=DatePlayed` on albums is not meaningful either.
- Verdict: not meaningful → `kAlbumPlayCountSort = false`

## V8 MusicGenres item counts
- Emby 4.9.5, measured 2026-09-17
- `/MusicGenres UserId={uid} ParentId={lib} Limit=3 Fields=ItemCounts`: items carry
  only `BackdropImageTags, Id, ImageTags, Name, ServerId, Type, UserData`; no
  `AlbumCount`, `ChildCount` or `SongCount` (also not with `Fields=ChildCount`).
- `/MusicGenres` with `Limit=1000` returned 289 rows and `TotalRecordCount` 289.
- Verdict: no counts → `kGenreItemCounts = false`

## V9 MediaStreams on list queries
- Emby 4.9.5, measured 2026-09-17
- Audio list with `Fields=MediaStreams`: every item has top-level `MediaStreams`
  with an `Audio` stream (`Codec`, `BitDepth`, `SampleRate`, `BitRate`, `Channels`),
  and also `MediaSources`.
- Verdict: present → `kMediaStreamsOnLists = true`

## V10 Album runtime on list queries
- Emby 4.9.5, measured 2026-09-17
- Album list items carry non-zero `RunTimeTicks` (e.g. 2,126,800,000 = 3 min 33 s) and
  `ChildCount` (the track count); `CumulativeRunTimeTicks` is absent.
- Verdict: present → `kAlbumRuntimeOnLists = true`

## V11 `/Artists` item counts (open question, not yet measured)
- Not measured against the live server. No entry above (V1-V10) covers `/Artists` or
  `/Artists/AlbumArtists`; this is recorded during visual-fix-1
  (.superpowers/sdd/2026-09-16-music-crate/visual-fix-1-report.md), which fixed
  `EmbyClient::artistParams()` never forwarding `query.fields` (so `/Artists` and
  `/Artists/AlbumArtists` asked for no fields at all, regardless of what
  `MusicQueryTranslator` requested).
- That fix makes the *request* correct — `Fields=ItemCounts,DateCreated` now reaches
  the wire for `musicArtists()`/`albumArtists()`. It does **not** establish that Emby
  answers with `AlbumCount`: V8 measured that the sibling `/MusicGenres` endpoint
  drops `AlbumCount`/`ChildCount`/`SongCount` entirely even when `Fields=ItemCounts`
  (and `Fields=ChildCount`) is sent, which is exactly why
  `MusicRepository::genreCountsFromAlbums()` derives genre counts by walking albums
  client-side instead of trusting the field.
- Open question: does `/Artists` with `Fields=ItemCounts` return `AlbumCount` on Emby
  4.9.5? Unknown from this seat.
- If the answer turns out to be no, the remedy is a client-side derived artist count
  modelled on `genreCountsFromAlbums()` — a separate task with its own design, not
  attempted here.
- Verdict: unmeasured

## Other observations that later tasks depend on

These are not V-items, but they were measured in the same session and change what a
correct request or parser looks like.

### List queries hide track play data unless asked for
- An Audio list sorted `SortBy=DatePlayed SortOrder=Descending` returns rows whose
  `UserData` is `{IsFavorite, PlayCount: 0, PlaybackPositionTicks, Played: true}`:
  **`PlayCount` is 0 and `LastPlayedDate` is absent**.
- Fetching the same rows one by one (`/Users/{uid}/Items/{id}`) gives
  `LastPlayedDate` values in descending order and `PlayCount` 1, 1, 1, 1, 3, 2.
  So the server sorts correctly; it just omits the values from list payloads.
- Adding `Fields=UserDataPlayCount,UserDataLastPlayedDate` to the list query returns
  the real `PlayCount` and `LastPlayedDate`.
- `SortBy=PlayCount` on Audio is honoured (per-item counts 9, 8, 7, 7, 6, 6).
- Played audio in the library at measurement: 138 tracks. Favourite albums: 0.

### Stream details seen in the sample
- Codecs: `flac` (16/44.1, 16/48, 24/44.1, 24/48, 24/88.2, 24/96, 24/192),
  `mp3` (`BitDepth` absent), `dsd_lsbf_planar` (DSF: `BitDepth` 8, `SampleRate`
  352,800), and a malformed `flac` with no `BitDepth` and `SampleRate` 0.
- Some tracks carry an `EmbeddedImage` stream (`mjpeg`) beside the audio stream.

### Genre ids are numbers
- `GenreItems[].Id` is a JSON **number** on album items (`{"Id": <number>,
  "Name": …}`), while `AlbumArtists[].Id` and `ArtistItems[].Id` are strings.

# Phase 4 gate: what the Crate pages leave uncovered

Phase 4 Task 11, measured 2026-09-18 against `/tmp/w4gate` (a clean Debug build with
`STRMQT_WERROR=ON`) at the gate commit. Every entry below was produced by mutating the
named line and running the whole suite — 61 tests — not by reading the code and
guessing. A gap with a passing suite behind it is a measurement; the mutation and its
result are recorded so the next reader does not have to repeat it.

## P4-U1 `MusicBrowseController::m_epoch` has no cover anywhere
- Deleting `++m_epoch` from `resetForLibrary()` (`MusicBrowseController.cpp:493`) passes
  all 61 tests.
- Not because the epoch is idle, but because four independent guards each drop the same
  stale reply: the lane generation, the epoch, `query != queryFor(section)`, and
  `startIndex != model->count()` (`MusicBrowseController.cpp:895`). Any one of them
  suffices for every case a fixture can build, so no fixture can isolate the epoch.
- Covering it needs a reply that survives the other three: the same query and the same
  `startIndex` under a *different* library. That is the fixture to write, not another
  assertion on the existing ones.

## P4-U2 "Random is one page" is unpinned
- `MusicBrowseController.cpp:908`, `const int total = query.sortKey == kRandom ? shown
  : page.totalRecordCount;`. Mutating it to always use `page.totalRecordCount` passes
  all 61.
- The visible failure would be a random section that advertises 5,037 rows and refills
  forever against a sample of 100.

## P4-U3 The genre walk's paging and its hard stop are untested
- `MusicRepository.cpp:600–614`: the walk pages on the returned array's own size
  (`items.size() == pageSize`) and stops after 20 pages (`startIndex / pageSize < 19`).
- No fixture returns a full genre page, so neither the paging nor the stop is ever
  entered. The failure mode of a missing stop is a hung app rather than a wrong list,
  which is why this is written down instead of shrugged off.

## P4-U4 `acceptPage`'s commit-before-settle ordering is unpinned
- No test observes a lane's `stateChanged` while reading the model, so nothing notices
  if the lane settles before the model is committed — the window in which a page would
  read `loading == false` against an empty model.

## P4-U5 The `ensureOpen` visibility guard has no regression test
- Reverting `if (!page.visible)` to `if (!page.isActivePage)` on the three Crate pages
  passes 61/61.
- Covering it needs a fixture that drives a real `StackView` pop with undrained
  `Qt.callLater` queues. The probe recipe that measured it: copy
  `BoundedNavigationStack`, mirror the page's `isActivePage` and `ensureOpen`, push A,
  push B, `goBack()` all in one JS turn, and read which `ensureOpen` fired last.

## P4-U6 `navigationFocusKey "album-tracks"` restore is uncovered
- The key is set and the album page is pushed and popped in tests, but no test asserts
  the focused row comes back on a Back into the album.

## P4-U7 The library-less genre link — now pinned by shape, not by behaviour
- `root.musicLibraryId()` (`Main.qml:529`) is empty until Music Home or Browse has been
  visited, so an album or artist opened from Search or a deep link early has no music
  library and `openMusicGenreFromPage` falls back to `Actions.browseGenre`. That
  fallback is the design, not a bug.
- Closed this far at the gate: `tst_navigation_history`'s
  `productionRetargetOrderingRetainsDepartingScopes` now pins both branches of
  `openMusicGenreFromPage` and their order. Proved to bite — deleting the `else` fails
  the test on `fromPageFallback > fromPageFilter`.
- Still uncovered: that the fallback *fires*. Like every source pin in that test it
  cannot tell production code from a comment, and no test builds the shell with both
  music controllers unscoped.

## P4-U8 `EmbyImageFetcher::sourceFor`'s empty-tag permission is dead capability
- `sourceFor` rejects only an empty `itemId` or `imageType` (`EmbyImageProvider.cpp:287`),
  so an empty `tag` is allowed. The deleted `ArtistPage.qml:94` was its only caller with
  an empty tag; the one remaining caller, `PersonCard.qml:49`, guards on
  `card.imageTag.length > 0`.
- Removing the permission would be a behaviour change with no caller asking for it, so
  it was correctly left alone. It is unexercised capability, not a bug.

## P4-U9 The Application wiring no test can see — partly closed by P4-R11
- No test constructs `strmqt::Application` (verified: nothing under `tests/` includes
  `Application.h`; the tests that need the album/artist favourite relay make the same
  connection themselves). Deleting either `ItemActions::favoriteChanged` connect
  (`Application.cpp:195,204`) or either `resetSessionState()` call
  (`Application.cpp:344,345`) therefore leaves the suite and the page self-test green.
- Closed at the gate (ruling P4-R11): the `STRMQT_SELFTEST` run, which already builds
  the real object graph, now asserts that the `AlbumCtl` and `ArtistCtl` context
  properties are objects and that both `favoriteChanged` connects exist, and exits 1
  naming the missing one. Proved to bite twice: deleting the album connect gives
  `selftest FAIL music wiring: ItemActions::favoriteChanged is not connected to
  AlbumController::noteFavourite` and exit 1; deleting the `ArtistCtl` context property
  gives `context property ArtistCtl is not an object` and exit 1.
- Still uncovered: the two `resetSessionState()` calls. Nothing observes a session
  boundary on the album or artist controller, and the self-test has no session to end.

## P4-U10 The baseline and the prose disagreed about the deleted pages
- `config/qmllint-baseline.txt` still held 89 records for `AlbumPage.qml` and
  `ArtistPage.qml`; the gate's re-baseline removed them (1,125 → 1,253 records:
  −100 / +228, every addition an unqualified read of a context property).
- `MUSIC.md` prose still names the deleted pages. Phase 6 owns the docs.
