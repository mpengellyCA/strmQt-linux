# Music: "Crate" redesign

Date: 2026-09-16. Status: approved design, pending implementation plan.
Supersedes `MUSIC.md` as the plan of record for music.

Mockups (static HTML, open in a browser) live beside this file in
`2026-09-16-music-crate-mockups/`. Where a mockup shows several options, the chosen
one is named below.

---

## 1. Why

Music works, but it is built from the film library's parts. It is a `MediaItem` —
the struct that also carries `seriesName`, `officialRating` and `status`. `MusicPage`
is `LibraryPage`'s layout (header → tab bar → shared `FilterBar` → `StrmGrid` of
`StrmCard`) with square cards. The player is the video OSD's skeleton with an audio
mode. There is no landing page: opening a music library drops you on an A–Z album
grid.

The goal is a music surface **designed first**, with the data **adapted to the
design** by a dedicated layer. Reshaping responses and composing several requests
into what one screen draws is that layer's job, never QML's.

### Out of scope

- Gapless playback, crossfade and waveform scrubbing (they remain future playback work).
- A persistent local catalogue or listening-history database. Every screen asks Emby
  live, through a short-lived per-session cache.
- Any change to the film/TV pages, the video player, the playback engines, or the
  remote-control protocol.
- A pluggable non-Emby music backend. `MusicRepository` is the only Emby-aware music
  unit, so it is the future seam, but no interface is introduced now.

---

## 2. Visual direction: Crate

Chosen from `music-direction.html`, option **A: Crate**. A record shop: covers
lead, the type is loud, and you browse by flicking through bins.

Crate is a **dialect of Projection Booth, not a new theme**. It adds no colour.
The ground (`#0C0B0A`), surfaces, amber accent, and the three bundled typefaces are
unchanged. `Theme.qml` gains music tokens only:

| Token group | Value |
|---|---|
| Crate display | Archivo, width ≈ 118–120%, weight 800–850, uppercase, tracking −0.02em. For section strip, shelf headings, hero titles, genre bins. |
| Crate hero sizes | Hero title ≈ 52 px (Home), 58–60 px (album, player), 84 px (artist), all scaled by the density multiplier. |
| Body and data | Public Sans for body text; IBM Plex Mono with tabular figures for durations, counts, formats and kickers. |
| Sleeve | Square cover, 3 px radius, deep drop shadow (`elevation4`-class). **No card surface** behind an album: a cover and its caption. |
| Kicker | Plex Mono, 11 px, letter-spacing 0.16–0.18em, uppercase, `textSecondary`. |
| Badge | Plex Mono 10.5 px, 1 px `#4a443d` border, 3 px radius; the hi-res variant is amber. |

Rules carried over unchanged: hover is not focus, focus draws the amber ring, and the
**cover wash keeps its existing clamp** (saturation ≤ 0.55, luminance 0.10–0.22,
opacity ≤ 22%) so the ring never loses contrast.

---

## 3. Architecture: the music domain layer

```
QML pages (Home · Browse · Album · Artist · Player)
        │  properties / invokables
        ▼
Controllers   MusicHomeController · MusicBrowseController · AlbumController · ArtistController
        │  own generation counters, expose models
        ▼
Models        AlbumGridModel · ArtistGridModel · TrackListModel · GenreBinModel · ShelfModel
        │  built from music DTOs
        ▼
MusicRepository   composition, fan-out/merge, per-session cache      (src/app/music/)
        │
        ▼
EmbyClient ──► EmbyMusicMapper ──► music DTOs                        (src/server/…)
```

### 3.1 DTOs — `src/server/dto/music/`

Value types. Every derived field is computed **once, by the mapper**.

| DTO | Fields | Derived |
|---|---|---|
| `ArtistRef` | id, name | — |
| `AudioFormat` | codec, bitDepth, sampleRate, bitrate, channels | `badge` ("FLAC 24/96", "MP3 320"), `isLossless`, `isHiRes` (lossless and (bitDepth > 16 or sampleRate > 48 kHz)) |
| `Track` | id, title, `artists[]`, `albumArtists[]`, albumId, albumTitle, discNumber, trackNumber, runtimeMs, favourite, playCount, lastPlayed, dateAdded, `AudioFormat`, `coverRef` | `featured[]` (split from "feat." / "ft." in the title *or* extra track artists beyond the album artist), `displayTitle` (title without the feat. suffix), `differsFromAlbumArtist` |
| `Album` | id, title, `albumArtists[]`, year, premiereDate, `genres[]` (id+name), studios, dateAdded, trackCount, runtimeMs, favourite, playCount, lastPlayed, `coverRef` | `releaseType` (Album / EP / Single / Compilation, see §3.2), `discCount`, `formatSummary` (the album's dominant `AudioFormat`) |
| `Disc` | number, runtimeMs, `tracks[]` | — |
| `AlbumSleeve` | `Album`, `discs[]`, `moreByArtist[]` (Album) | composed |
| `Artist` | id, name, `coverRef`, backdropRef, albumCount, trackCount, favourite | — |
| `ArtistProfile` | `Artist`, `albums[]`, `epsAndSingles[]`, `appearsOn[]`, `topTracks[]` (5), `similar[]` (Artist) | composed and grouped |
| `GenreBin` | id, name, recordCount, `covers[]` (≤ 3 ImageRefs) | composed |
| `Station` | kind (`HeavyRotation`, `Favourites`, `DeepCuts`, `MoreLike`, `ShuffleAll`), label, seedId, `covers[]` (4) | resolved to tracks only when played |
| `ContinueListening` | `Album`, resume `Track`, resume track index, progress 0–1 through the album | composed |
| `MusicQuery` | section, sortBy, descending, letter, genreIds, decade (0 = any), format (`Any`/`Lossless`/`Lossy`/`HiRes`), favouritesOnly, unplayedOnly, artistMode | — |

`coverRef` reuses `MediaItem::ImageRef` semantics and the existing precedence
(album Primary → parent Primary → own Primary for tracks).

### 3.2 `EmbyMusicMapper` — `src/server/emby/`

Parses Emby JSON **directly** into music DTOs, so it can read fields `MediaItem` never
kept: the audio `MediaStream`'s `BitDepth`/`SampleRate`/`BitRate`/`Channels`,
`DateCreated`, `UserData.LastPlayedDate`, `AlbumArtists`, `ArtistItems`,
`GenreItems`, `Studios`. It uses the tolerant rules of `EmbyDtoMapper`: unknown
fields are ignored, missing fields defaulted, a malformed stream yields
`AudioFormat{}` (no badge).

`releaseType` precedence:
1. A server-sent album-type tag, if Emby 4.9.5 sends one (verify, §9).
2. Compilation when the album artist is "Various Artists" (case-insensitive) or the
   album's tracks have ≥ 4 distinct primary artists none of which is the album artist.
3. Single: ≤ 3 tracks and runtime < 20 min.
4. EP: ≤ 7 tracks and runtime < 35 min.
5. Otherwise Album.

The heuristic lives in one function with a unit test per branch.

### 3.3 `MusicRepository` — `src/app/music/`

The only music unit that holds an `EmbyClient`. Each method returns
`QFuture<Result<T>>` resolved on the caller's thread.

| Method | Composition |
|---|---|
| `continueListening()` | Last played `Audio` (SortBy=DatePlayed desc, Limit 1) → its album + album tracks, in parallel once the id is known. |
| `recentAlbums(limit=20)` | Recently played `Audio` (Limit 200) → grouped by album in play order, deduplicated → album records for the first `limit`. |
| `newAlbums(limit=20)` | `MusicAlbum` SortBy=DateCreated desc; plus the count added in the last 7 days (one `Limit=0` request with a `MinDateCreated` filter). |
| `genreBins(limit)` | `/MusicGenres` for the library (page on the array's own size) sorted by record count → for each of the first `limit`, 3 album covers (`GenreIds`, SortBy=Random, Limit 3). Samples are cached per genre. |
| `topArtists(limit=20)` | Recently played `Audio` (Limit 500) → counted by primary artist → artist records. |
| `forgottenFavourites(limit=20)` | Favourite `MusicAlbum`, SortBy=DatePlayed ascending. |
| `randomAlbums(limit=20)` | `MusicAlbum` SortBy=Random. Never cached. |
| `stations()` | Station descriptors with collage covers (from shelves already fetched where possible). |
| `resolveStation(Station)` | HeavyRotation: `Audio` SortBy=PlayCount desc, Limit 200, then shuffled client-side. Favourites: favourite `Audio`, Random, 200. DeepCuts: `IsUnplayed` `Audio`, Random, 200. MoreLike: `/Items/{seed}/InstantMix`, deduplicated. ShuffleAll: `Audio` Random, 200. |
| `browseAlbums/Artists/Tracks(MusicQuery, startIndex)` | One paged request, filters translated per §5.2. |
| `albumSleeve(id)` | Album + tracks (with `MediaSources`) + "more by" (album artist's other albums, Limit 12) in parallel. |
| `artistProfile(id)` | Artist + album-artist albums + all albums the artist appears on + top tracks (`ArtistIds`, SortBy=PlayCount) + `/Items/{id}/Similar`, in parallel; grouped into albums / EPs & singles / appears-on client-side. |
| `albumTracks(id)` | Tracks only (used by play verbs and the player's Album tab; cached). |

**Cache.** An in-memory cache per account: Home shelves and genre bins (TTL 5 min),
`albumTracks`/`albumSleeve` (TTL 10 min), cover samples (for the session). Cleared on
session or account change. Invalidated by `markStale(kind)` — playback start marks
`recentAlbums`, `continueListening` and `topArtists` stale.

**Partial results.** A composed method resolves as success when its *core* request
succeeds (the album, the artist, the genre list). Secondary parts that fail are
returned empty and logged: a missing "more by", missing covers, or a missing
similar-artists row. Only a core failure is a `Result` error.

### 3.4 Models — `src/app/music/models/`

`AlbumGridModel`, `ArtistGridModel`, `TrackListModel`, `GenreBinModel`,
`ShelfModel` (a heterogeneous shelf: a kind plus one of the grid models). Role names
follow what the design draws: `coverUrl`, `title`, `subtitle`, `year`,
`releaseType`, `formatBadge`, `isHiRes`, `discNumber`, `trackNumber`,
`durationText`, `featuredText`, `favourite`, `playCount`, `recordCount`, and so on.
Display strings (durations, "N records") are formatted in C++.

Each model provides `get(row)`, `indexOfNavigationIdentity(id)`, and paging
(`canLoadMore`) with the same contracts as `MediaItemModel`, so
`NavigationFocusRestorer` and `StrmGrid` keep working.

### 3.5 Controllers — `src/app/controllers/music/`

| Controller | Publishes |
|---|---|
| `MusicHomeController` | `libraryId`; one shelf lane per shelf (`model`, `loading`, `error`, `retry()`); `continueListening` (a map); `reshuffle()`; `playStation(kind)` |
| `MusicBrowseController` | `section`; per-section model, loading, error and `canLoadMore`; the query (`sortBy`, `sortDescending`, `availableSorts`, `letter`, `genreIds`, `decade`, `format`, `favouritesOnly`, `unplayedOnly`, `artistMode`, `filtered`, `resultCount`, `unfilteredCount`); `genreOptions`, `decadeOptions`, `formatOptions` (with counts where the server gives them); `playFiltered()`, `shuffleFiltered()`, `clearFilters()` |
| `AlbumController` | `sleeve` properties, `tracks` model, `moreBy` model, detail loading and error; `play(fromIndex)`, `shuffle()`, `radio()` |
| `ArtistController` | profile properties, `albums`, `epsAndSingles`, `appearsOn`, `topTracks`, `similar` models; `shuffle()`, `radio()` |

Every controller keeps the generation-counter rule: a superseded reply is dropped.

### 3.6 Seams with the rest of the app

1. **Playback.** `Track::toMediaItem()` is the single conversion. Controllers hand
   converted items to `ItemActions::playAllFrom` / `addAllToQueue` / `playNext`.
   `ItemActions` and `PlayQueue` do not learn about music DTOs.
2. **Queue provenance.** `PlayQueue` gains `sourceLabel` (a string, `NOTIFY`), set by
   the play verbs ("Sunburned Almanac", "Station · Heavy rotation",
   "Shuffle · Jazz · 1970s"). Empty for film/TV.
3. **Live user data.** `ItemActions::registerModel` generalises to a small
   `UserDataSink` interface (`updateUserData(id, played, favourite, position,
   playCount)`). `MediaItemModel` and all music models implement it, so
   `UserDataChanged` patches favourites and play counts in place everywhere.
4. **Navigation verbs.** `ItemActions::openAlbum/openArtist` keep their signatures.
   Search, menus, MPRIS and the web remote reach the new pages unchanged.

---

## 4. Music Home

Chosen shelves: the suggested set plus *Artists you play*. It is the landing page for
a music library.

Top of every music screen: the **section strip**:
`HOME · ALBUMS · ARTISTS · SONGS · GENRES · PLAYLISTS` (§5.1).

Shelves, in order:

1. **Hero: "Pick up where you left off."**
   - **Content:** a 210 px sleeve with the cover wash, the album title in Crate hero
     type, "artist · year · N tracks · runtime", and a progress line through the album.
   - **Actions:** **Resume track N**, **Shuffle album**, **♡**.
   - **Beside it:** "Recently played", a compact list of the three albums played before it.
   - **No history:** a random album, kicker "Pull one out", actions **Play** and
     **Another one ↻**.
2. **Recently played:** a row of album covers, deduplicated, up to 20.
3. **New in the crate:** albums by date added, with the kicker "N added this week"
   (hidden when 0).
4. **Stations:** five tiles, each a 2×2 cover collage with a wide-caps label: Heavy
   rotation, Favourites, Deep cuts, More like *[top artist]*, Shuffle all. One press
   resolves and plays. The menu offers Play / Shuffle / Add to queue.
5. **Dig by genre:** the ten largest genres as bins (wide-caps name, "N records",
   three covers fanned from the stack), then an **"All N genres →"** bin that opens the
   Genres section.
6. **Artists you play:** round portraits, ordered by recent play count.
7. **Forgotten favourites:** favourite albums, least recently played first.
8. **Pull one out:** random albums; **↻** in the heading reshuffles.

**Behaviour**

- **Loading:** each shelf is an independent lane with its own skeleton in its real
  shape (square or round).
- **Empty:** a shelf with no content is hidden, heading included.
- **Error:** a failing shelf collapses to one line with **Retry**; the other shelves
  stay live.
- **Freshness:** returning within the cache TTL is instant; stale shelves refetch
  when Home becomes visible.
- **Moving about:** each shelf is one tab stop and follows the `NavigationColumn`
  rules (Down keeps the column, Left at the edge opens the rail). A held A or the Menu
  key opens the item menu.

---

## 5. Browse

Chosen from `browse-structure.html`, option **A: Section strip + pills + crate dividers**.

### 5.1 Frame

- **Section strip:** Crate display tabs with an amber inset underline on the active
  one. One tab stop that owns Left/Right. The shoulders cycle sections from anywhere
  (`cycleSection`).
- **Pill row:** the filter pills (left); `"<unfiltered> → <filtered> RECORDS · SORT:
  <label> ↓"` in Plex Mono (right); then the **sort** control, **▶ Play** and
  **⇄ Shuffle** (server-sampled across the whole filtered scope).
- **A–Z crate dividers:** a vertical strip on the right edge; the active letter
  protrudes in amber. One tab stop; the triggers call `jumpLetter`. Visible only when
  sorted by name. Letters match the **sort** name and are sent as the
  `NameStartsWithOrGreater`/`NameLessThan` range pair.

### 5.2 Filters

Filters are shared across Albums, Artists and Songs. **Sort is per section.**

| Pill | Control | Set state | Query |
|---|---|---|---|
| Genre | A searchable overlay picker with checkboxes and counts | `Genre: Jazz ✕` / `Genres: 3 ✕` | `GenreIds` |
| Decade | A menu (1950s–2020s, "Earlier") with counts | `Decade: 70s ✕` | `Years=1970,…,1979` (verify §9) |
| Format | Lossless / Lossy (+ Hi-res only if server-filterable) | `Format: Lossless ✕` | `AudioCodecs` (verify §9); otherwise the pill is not shown |
| ♡ Favourites | Toggle | filled | `Filters=IsFavorite` |
| Unplayed | Toggle | filled | `Filters=IsUnplayed` |

- ✕ on a pill clears that filter. With two or more filters set, **Clear all** appears.
- A query change refetches the visible section and invalidates the others.

### 5.3 Sections

| Section | Content | Sorts |
|---|---|---|
| Albums | Sleeve grid; caption "title / artist · year"; a release-type badge only when not an Album | Name, Artist, Year, Date added, Most played, Random |
| Artists | Round portraits; "N records"; an *Album artists / Everyone* switch at the end of the pill row | Name, Most played, Date added, Random |
| Songs | `TrackTable`: # · Title (+ dim feat.) · Artist · Album · Format badge · Duration · ♡; multi-select bar; type-to-jump | Name, Artist, Album, Date added, Most played, Duration, Random |
| Genres | A large grid of genre bins | Size, Name |
| Playlists | Audio playlists as 2×2 collages, "N tracks · runtime"; a **＋ New playlist** tile first | Name, Date added |

Selecting a genre bin switches to **Albums with that genre pill set**. It is a filter,
not a separate page.

- **State:** paging, per-section loading and error, and exact focus restore on
  Back/Forward behave as today.
- **Empty result:** "No records match", with each active filter repeated as a
  clearing button.

---

## 6. Album, artist and playlist pages

From `album-artist.html` (approved as shown).

### 6.1 Album: the back of a sleeve

- **Left column:** a 340 px sleeve, then **liner notes** (Released · Genre links ·
  Label (Studios) · Format · "Added N ago · played N×"). Empty rows are omitted.
  Genre links open Browse → Albums with that genre pill.
- **Header:**
  - A kicker with the release type and year, the title in Crate hero type, and the
    album artist as a link.
  - Badges for format (amber when hi-res), "N DISCS" when discCount > 1, and
    "N tracks · runtime".
- **Actions:** ▶ Play · ⇄ Shuffle · ◎ Album radio (instant mix) · ＋ Playlist · ♡.
- **Tracklist** (`TrackTable`):
  - **Grouping:** "Side A / Side B / …" headings with "Disc N · runtime", only when
    discCount > 1.
  - **Artist column:** shown only for rows where `differsFromAlbumArtist`.
  - **Rows:** featured artists dim after the title; the now-playing row is amber;
    multi-select and type-to-jump are kept.
- **More by *artist*:** a cover row, hidden when empty.
- **Data:** everything comes from `AlbumSleeve`.

### 6.2 Artist: poster and filed releases

- **Poster header:** the backdrop (fallback: the portrait with the cover wash), a
  kicker "Artist · N records · N tracks", and the name in 84 px Crate display.
- **Actions:** ⇄ Shuffle artist · ◎ Artist radio · ♡.
- **Main column:** tabs **Albums / EPs & Singles / Appears on** with counts, newest
  first. Empty tabs are hidden.
- **Right column:**
  - **Most played:** five tracks with covers; guest appearances are labelled.
  - **Similar artists:** round portraits, hidden when empty.
- **Data:** everything comes from `ArtistProfile`.

### 6.3 Playlist (audio)

- **Left column:** a 2×2 collage and "N tracks · runtime · updated".
- **Header:** the title in Crate hero type; Play · Shuffle · Rename · Delete.
- **Table:** a track table with cover thumbnails.
- **Behaviour:** reorder, remove and rename keep today's `PlaylistController`
  behaviour. Video playlists keep the existing `PlaylistPage`.

---

## 7. Player

Chosen from `player.html`, option **B: Out of the sleeve**.

### 7.1 Docked bar (audio)

- **Layout:** a 72 px square cover flush left · title (link to album) · "artist ·
  album" (links).
- **Controls:** ⇄ ⏮ ⏯ ⏭ ↻ · mono "elapsed / total" · ♡ · queue peek · stop · expand.
- **Playhead:** a 2 px amber line on the top edge.
- **Detail:** while playing, a thin slice of record shows past the cover's right edge
  and retracts on pause.
- **Video:** the bar is unchanged for video.

### 7.2 Full player (audio)

**Stage (left)**

- **Sleeve and record:** a 420 px sleeve with the record behind it. The record's
  centre label is the cover, circle-cropped.
- **Below the stage:**
  - The title in Crate display, and the artist and album links.
  - Transport ⇄ ⏮ ⏯ ⏭ ↻ ♡ ＋ ⋯. The ⋯ menu holds Go to album, Go to artist,
    Radio and Stop.
  - A scrubber with mono times.
  - The readout "FLAC 24/96 · DIRECT PLAY", from the playback ticket and stream
    method.

| Playback state | Record |
|---|---|
| Playing | slid ≈ 45% out, rotating at 33⅓ rpm (1.8 s per turn) |
| Paused | decelerates to rest over ≈ 600 ms, stays out |
| Buffering | holds; the readout pulses "BUFFERING" |
| Next track, same album | keeps spinning |
| Next track, different album | slides in → sleeve cross-fades to new cover → slides out |
| Stopped / queue ended | slides fully into the sleeve |

**Side panel (right, 440 px): tabs**

- **Up next:** "Playing from · *sourceLabel*"; reorder, remove and jump, as today's
  queue.
- **Album:** the current album's tracklist (`albumTracks`, cached), with the playing
  track highlighted. Selecting a row plays the album from that row.
- **Lyrics:** shown only if the server provides lyrics (§9). Timed lyrics follow the
  playhead; plain text is shown static. The tab is hidden when there are none.

**Motion and performance**

- **Sleeve flight:** the existing continuous-sleeve flight lands on the stage sleeve;
  the record slides out after it lands.
- **Spin:** one rotating layer, running only while the window is visible and the
  player page is current. It is stopped, not just hidden, when the page is left.
- **Setting:** *Settings → Interface → Animate record* (default on). Off gives a
  static, slid-out record.
- **Wash:** it keeps the existing clamp.
- **Focus:** the stage and the side panel are two focus zones joined by Left/Right.
  All existing player shortcuts and gamepad mappings are kept.

---

## 8. Navigation, input and errors

**Routes and input**

- **Routes:** `Main.qml` gains `musicHome`, `musicBrowse` (section + query),
  `album` and `artist` in place of `music`. Each keeps history, forward stack and
  focus memory keyed by stable identity.
- **Input:** the three input contexts stay. The music context is armed by Home,
  Browse, Album and Artist. Space plays or pauses, `S` shuffles the current view, `L`
  favourites. The browse page implements `cycleSection(step)` and `jumpLetter(step)`;
  Home implements `cycleSection` only.

**Errors**

- **Parsing:** tolerant (§3.2).
- **Partial composition:** degrades by hiding the failed part (§3.3). Only a core
  failure shows the page's error state with Retry.
- **Lanes:** every Home shelf and browse section is an independent loading and error
  lane.
- **Session change:** clears the repository cache; generation counters drop late
  replies.

---

## 9. Server behaviour to verify

Measure each item against the live Emby 4.9.5 server during Phase 1. Record the
outcome in the ARCHITECTURE.md §2 table.

1. Does `AudioCodecs` filter `/Users/{uid}/Items`?
   - **Yes:** the Format pill is a filter.
   - **No:** format is badge-only and the pill is not shown.
   - **Also check:** whether any bit-depth or sample-rate filter exists (it decides
     "Hi-res only").
2. Does `Years=` accept ten comma-separated years in one request?
   - **No:** Decade falls back to `MinPremiereDate`/`MaxPremiereDate`.
3. Is an album or release type field sent for EPs and singles?
   - **No:** the §3.2 heuristic stands alone.
4. Does `/Audio/{id}/Lyrics` (or the lyrics stream on the media source) exist, and
   are the lyrics timed?
   - **No:** the Lyrics tab is not built.
5. Does `/Items/{id}/Similar` return artists for a `MusicArtist` id?
   - **No:** Similar artists is omitted.
6. Does `MinDateCreated` filter `MusicAlbum` (for "N added this week")?
   - **No:** the kicker is omitted.
7. Does `SortBy=PlayCount` / `DatePlayed` order `MusicAlbum` by *album* plays?
   - **No:** Most played for albums is derived by aggregating tracks, or dropped from
     the sort list.

---

## 10. Testing

Qt Test, recorded fixtures under `tests/mocks/emby`, no network.

- **`EmbyMusicMapper`:**
  - Format badge and class for FLAC 16/44, FLAC 24/96, ALAC, MP3 320, DSF, and a
    missing stream.
  - Feat. splitting (title suffix and extra artists).
  - Every `releaseType` branch.
  - Disc grouping including missing disc numbers.
  - Cover precedence.
  - Tolerance of absent and malformed fields.
- **`MusicRepository`** against the `QTcpServer` mock:
  - Parallel composition of the sleeve and the profile.
  - Partial failure (a secondary request 500s → success with an empty part) versus a
    core failure → error.
  - Recently played deduplication and ordering.
  - Genre-bin sampling and the sample cache.
  - Cache TTL, `markStale`, and clearing on session change.
  - Station resolution, including instant-mix deduplication.
- **Models:** roles, paging, `indexOfNavigationIdentity`, `UserDataSink` patches.
- **Controllers:**
  - `MusicQuery` → request translation (letter range pair, decade, format,
    filters).
  - Per-section sort memory; a query change invalidates hidden sections.
  - Stale replies are dropped.
  - Per-shelf lanes.
- **`PlayQueue`:** `sourceLabel` set by each play verb, and cleared for film/TV.
- **QML:** `strmqt_qmllint` with the fingerprint baseline updated deliberately;
  `STRMQT_SELFTEST=1` constructs every new page. Visual polish is verified manually
  and noted in commits.

---

## 11. Delivery

One spec, one implementation plan, phased. **The next release waits for the whole
refactor**, so old music code may be deleted as soon as it gets in the way rather than
kept alive in parallel.

| Phase | Lands | Removes |
|---|---|---|
| 1 | Music DTOs, `EmbyMusicMapper`, `MusicRepository`, models, `UserDataSink`, `PlayQueue.sourceLabel`, §9 verifications, Crate tokens in `Theme.qml` | — |
| 2 | `MusicHomeController`, Music Home page, `musicHome` route, section strip component | — |
| 3 | `MusicBrowseController`, browse page (strip, pills, genre picker, crate dividers, five sections) | `MusicPage.qml`, music branches of `FilterBar.qml` |
| 4 | `AlbumController`, `ArtistController`, album, artist and audio-playlist pages | old `AlbumPage.qml`, `ArtistPage.qml`, `MusicController` |
| 5 | Audio docked bar, "Out of the sleeve" player, record animation, Animate record setting | audio half of `NowPlayingPanel.qml`; music-only roles left unused on `MediaItemModel` |
| 6 | Docs: ARCHITECTURE.md §2 table, §4 pages, §5 input; `MUSIC.md` marked superseded; qmllint baseline | — |

Each phase builds, passes `ctest --preset dev`, and passes the selftest.
