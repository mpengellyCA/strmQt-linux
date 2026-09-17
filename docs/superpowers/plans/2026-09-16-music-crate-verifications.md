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
