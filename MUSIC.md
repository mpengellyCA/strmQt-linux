# Music — plan to make it a first-class citizen

Date: 2026-08-23. Scope: `src/ui/pages/{Music,Album,Artist,Playlist}Page.qml`,
`src/ui/player/`, `src/ui/shell/{MiniPlayer,FilterBar}.qml`,
`src/app/controllers/{Music,Playlist,Player}Controller`, `src/app/models/MediaItemModel`,
`src/server/dto/MediaItem.h`, `src/server/emby/{EmbyClient,EmbyDtoMapper}`,
`src/platform/MprisPlayer`.

Method: static review against the cited code. Anything I could not confirm from the
tree is marked **[verify]** and is a measurement step, not an assumption.

## Completion disposition

The production milestone represented by this plan is complete as of 2026-08-23.
Phases 0-6 and 8 are implemented; Phase 7 delivered ReplayGain but its gapless
prototype did not become a shippable per-item-ticket design and remains an explicit
future playback milestone. The audit-driven lifecycle, session-isolation, responsive,
accessibility, MPRIS, and now-playing ownership repairs are also complete.

The following are accepted future scope, not unfinished acceptance criteria for this
milestone:

- gapless engine advance and optional crossfade;
- a full-screen lyrics pane, pending a verified server endpoint/shape;
- dedicated recently-played and most-played music surfaces;
- the explicitly deferred waveform scrubber.

This disposition supersedes unqualified future-tense language below when deciding
whether the music refactor is complete; the original plan remains intact as its design
and discovery record.

---

## 0. What is already there

The brief reads as if music has no integrated player. It has one, and the parts are
better than the framing suggests. Stating this first so the plan spends its effort on
the real gaps:

| Thing | Where | State |
|---|---|---|
| Docked now-playing bar | `src/ui/shell/MiniPlayer.qml` | Exists. Reserves its own strip (`reservedHeight`) so it displaces rather than covers. Transport, seekable scrubber, expand-to-full. |
| Full-screen audio view | `PlayerPage.audioMode` → `src/ui/player/NowPlayingPanel.qml` | Exists. Square cover, transport, shuffle/repeat, queue beside it, chrome that never auto-hides. |
| Browse while playing | `Main.qml:169` `minimizePlayer()` | Exists. Leaving the player page does not stop playback. |
| Album / Artist browsing | `MusicPage`, `AlbumPage`, `ArtistPage` | Exists, pages, virtualises, disc-aware track table. |
| Queue semantics | `src/app/PlayQueue.h` | Strong. Per-entry keys, shuffle that restores original order, repeat modes. |
| Playlists | `PlaylistController`, `PlaylistPage` | Exists, with reorder/rename/remove. Not music-aware. |

So this is not a build-it-from-nothing plan. It is: **fix one data bug that makes the
whole music surface look broken, give the library the query axes it never got, and
then raise the player from "works" to "the reason you open the app".**

---

## 1. The artwork bug — confirmed, and worse than described

**Verified.** `src/app/models/MediaItemModel.cpp:117-118`:

```cpp
case PosterUrlRole:
    return embyImageSource(item.id, QStringLiteral("Primary"), item.primaryImageTag);
```

Unconditional, for every item type. And `src/server/dto/MediaItem.h` has no
`albumPrimaryImageTag`, no `parentPrimaryImageItemId`, no `parentPrimaryImageTag` —
`EmbyDtoMapper::…` (line 130) parses `ImageTags.Primary` and nothing else on that axis.
There is a `thumbSource()` with a careful per-type precedence chain for 16:9 art; there
is no equivalent for square art.

For an `Audio` item that means:

- **A track with no embedded cover draws nothing.** `primaryImageTag` is empty, the
  provider gets a bad id, and the queue panel, the mini player, the now-playing hero
  and the album page's rows all render a hole. On a library ripped from CD this is
  most of it.
- **A track *with* embedded cover draws the wrong image.** Per-file embedded art is
  whatever the ripper wrote — frequently a 300 px scan, sometimes a different pressing,
  sometimes a back cover. It is used in preference to the album's curated cover
  because the album cover is never consulted at all.
- The queue looks like a ransom note when one record's tracks came from different
  sources, which is exactly the symptom in the brief.

### Fix

1. `MediaItem` gains `albumPrimaryImageTag`, `parentPrimaryImageItemId`,
   `parentPrimaryImageTag`. Mapper parses `AlbumPrimaryImageTag`, `AlbumId`,
   `ParentPrimaryImageItemId`, `ParentPrimaryImageTag`.
   **[verify]** whether Emby 4.9.5 sends `AlbumPrimaryImageTag` on `/Items` by default
   or needs it in `Fields`. `MusicController::openAlbum()` currently asks for
   `MediaSources` only (`MusicController.cpp:213`), so if it needs asking, that is the
   line to change.
2. `MediaItem::coverSource()` — sibling to `thumbSource()`, same `ImageRef` return,
   **type-dependent precedence**, because the inversion the brief asks for is correct
   for audio and wrong for everything else:
   - `Audio` / `AudioBook`: album Primary → parent Primary → **own Primary last**.
   - `MusicAlbum`: own Primary → parent (artist) Primary.
   - everything else: own Primary, i.e. today's behaviour, unchanged.
3. `PosterUrlRole` routes through `coverSource()`. One change, and every consumer —
   grid card, mini player, now-playing hero, queue row, MPRIS art — is fixed at once,
   because they all read `posterUrl`.
4. Keep an explicit `trackArtUrl` role **[optional]** so a future "this pressing has
   its own art" affordance has somewhere to live. Do not wire it to anything yet.

**Test:** extend the DTO mapping unit suite with an `Audio` fixture that has no
`ImageTags` and an `AlbumPrimaryImageTag`, asserting `coverSource()` resolves to the
album. That fixture is the regression guard; a screenshot is not.

This is Phase 0 and it lands before any visual work, because every mock of the new
player is a lie until covers actually appear.

---

## 2. The library has no query axes

**Verified.** `MusicController` fetches albums with `sortBy = "SortName"` ascending,
hardcoded (`MusicController.cpp:97`), artists likewise via a fixed endpoint pair. There
is no `setSort`, no `setNameStartsWith`, no genre, no year, no favourites filter.
`FilterBar.qml` — which has all of that — reads `LibraryCtl` directly in twenty-odd
bindings and cannot be pointed at anything else.

And `EmbyClient::genres()` (`EmbyClient.cpp:484`) is a **search typeahead**: `UserId`,
`Limit`, optional `SearchTerm`. No `ParentId`, no `IncludeItemTypes`, no paging. It
cannot enumerate a music library's genres. There is no `/MusicGenres` call in the
client at all.

There is also **no track-level view**. 56,283 tracks on the target server are reachable
only by walking into an album or an artist. Every serious music client has a Songs
list; this has none.

### Plan

**2.1 — `MusicController` grows a query, the same shape `LibraryController` already has.**
`sortBy`, `sortDescending`, `nameStartsWith`, `genreIds`, `yearFilters`,
`favoritesOnly`, plus `availableSorts` varying by tab. Sort sets, per tab:

| Albums | Artists | Songs |
|---|---|---|
| Sort name · Release year · Date added · Random · Community rating · Most played | Sort name · Random · Most played | Sort name · Album · Artist · Track number · Date added · Runtime · Play count · Random |

Seed direction the way `FilterBar.defaultDescendingFor()` already does: date-added and
year descend by default, names ascend.

**2.2 — A third tab: Songs.** `MusicController.tracks` is currently the *open album's*
tracks. It needs a separate `songs` model with its own generation counter, because
reusing `tracks` is what already forces the hack in §5. Query:
`IncludeItemTypes=Audio`, `Recursive=true`, paged at 100, fields
`MediaSources,Genres,ParentIndexNumber`.

**2.3 — `/MusicGenres`, properly.** New `EmbyClient::musicGenres(parentId, startIndex, limit)`
hitting `/MusicGenres` with `ParentId` and `UserId`.
**[verify]** that `/MusicGenres` honours `ParentId` on 4.9.5, and note the documented
trap from ARCHITECTURE.md §2: `/Genres` reports `TotalRecordCount = 0` while returning
rows, so page on the array's own size and assume `/MusicGenres` does the same until
measured otherwise.

**2.4 — Generalise `FilterBar` instead of forking it.** Today it is a `FocusScope` that
names `LibraryCtl` in every binding. Give it a `property QtObject controller` and read
through that, plus `property bool showAlphabet`, `property var extraFilters`. Then
`LibraryPage` passes `LibraryCtl` and `MusicPage` passes `MusicCtl` and there is one
filter bar in the app, not two that drift.

Two music-specific additions to it:
- **Genre** as a multi-select `StrmSelect` populated from `/MusicGenres`, not as chips
  — a music library has hundreds of genres and chips do not scale past about six.
- The alphabet strip keeps its existing "SORT NAME" hint. It is *more* right for music
  than for film: "The Beatles" files under B and users will look under T.

**2.5 — Shuffle-the-whole-scope.** A header button that queues the current filtered
scope in random order. `Actions.shuffle(scopeId, "music")` already pins `SortBy=Random`
server-side, so this is a verb wire-up, not new machinery. It is the single most-used
button in every music app and StrmQt does not have it.

---

## 3. Audio playlists

`PlaylistController` lists every playlist the user has, mixed. `createPlaylist()`
accepts a `mediaType` (`EmbyClient.cpp:682`) and **nothing in the app ever passes
`"Audio"`**. `MusicPage` has no playlist surface at all.

- Fourth tab on `MusicPage`: **Playlists**, scoped to audio.
  **[verify]** whether Emby exposes playlist media type on the list payload; if it does
  not, filter by asking `/Playlists/{id}/Items` for one item and reading its type, cached
  — ugly, but the alternative is showing a user's video playlists inside their music
  library.
- Every "Add to playlist" path from a music context creates with `MediaType=Audio`.
- New-playlist-from-selection on the Songs tab and the album track table.
- No precondition fix needed: `PlaylistController` already runs `m_listGeneration` and
  `m_itemsGeneration` as separate counters (`PlaylistController.cpp:39,77`), so the
  list fetch and the open-playlist fetch no longer strand each other. Music is about to
  become the heaviest user of that controller, so re-check the split holds under a
  create-while-open sequence once the music paths land — but there is nothing to fix
  going in.

---

## 4. The design: "Sleeve", a music dialect of Projection Booth

Not a new aesthetic. `Theme.qml` is the single token source and this plan adds no
literal colour to any page. What music gets is a *dialect*: three rules that apply
only where audio is playing or being browsed.

**Rule 1 — the square is the unit.** Video is 16:9 and letterboxed into black; music
is 1:1 and fills. Every music surface is built on the sleeve: grid cards
(`cardVariant: "square"`, already correct), the now-playing hero, and — the change —
the docked bar, which currently prefers `thumbUrl` (16:9) and falls back to the poster
(`MiniPlayer.qml:104-117`). That is exactly backwards for a track. In audio mode the
bar takes the square at full bar height, flush left, no rounding on the outer edge.

**Rule 2 — the sleeve lights the room.** A single accent colour sampled from the cover,
clamped, washed behind the now-playing view and behind the album/artist page header as
a soft vertical gradient into `Theme.ground`. This is where a music app earns its
atmosphere, and it costs one C++ helper: `EmbyImageFetcher` already decodes a `QImage`
(`EmbyImageProvider.cpp:70`), so compute a dominant colour there, cache it by image id,
expose it as a `coverTint` role.

The clamp is the whole design decision, and it is a hard constraint, not a taste call:
saturation ≤ 0.55, luminance ∈ [0.10, 0.22], and the wash never exceeds 22% opacity.
Projection-booth amber exists because it sits outside the hue range of poster art so a
focus ring is never lost against it (ARCHITECTURE.md §4). An unclamped wash would put
the ring back inside that range and undo the reason the accent was chosen. If a sampled
tint cannot meet the clamp, it falls back to `Theme.surfaceColor` — no wash is always
better than an illegible one.

**Rule 3 — numerals are mono, and they line up.** Track number, duration, disc marker,
bitrate, sample rate: `Theme.fontMono` with tabular figures, right-aligned, so a column
reads straight down a box set. `AlbumPage` already does this; the shared row in §5
makes it universal.

### The signature: one continuous sleeve

The thing worth remembering, and the thing Emby's own players do not do: **the cover is
one object that grows from the bar into the full-screen view and shrinks back.** Not a
cross-fade between two screens — the same square, animated between two rectangles, with
the surrounding chrome fading around it.

Mechanically: a shared `Item` living in `Main.qml` above the `StackView`, whose geometry
is animated between the bar's art frame and `NowPlayingPanel`'s hero on expand/minimise,
with both endpoints hiding their own copy for the duration. `Theme.animSlow` (420 ms) and
`Theme.easeEmphasis`, which is what that easing token is for.

It is the one place in this plan where I would spend real animation budget, because it
is the transition the user performs most and the one that tells them the music never
stopped.

### Bar states

The bar is one component with two modes, chosen on the same `audioMode` predicate
`PlayerPage.qml:104` already computes — lift that into a `PlayerCtl.isAudio` property
so the bar and the page share one answer instead of each deriving it.

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ │  2px playhead, full bleed
│ ┌────┐  Threnody               ⏮  ⏯  ⏭   ⇄ ↻      1:12 / −2:48   ♡  ☰  ⌃ │
│ │ ▤▤ │  Godspeed You! …  ·  Lift Yr Skyy                                    │
│ └────┘                                                                      │
└──────────────────────────────────────────────────────────────────────────────┘
   square    title / artist · album      transport   shuffle    time    fav queue expand
```

Additions over today's bar: shuffle and repeat (they are queue state and belong where
the queue is controlled), favourite (music favouriting happens per-track, constantly,
and walking to a page to do it is the friction), a queue peek popover, and the
artist/album line as *links* rather than dead text. `Stop` leaves the audio bar — for
music, stop is pause plus forgetting where you were, and nobody wants it; it stays in
the video bar where it belongs.

The title split currently parses `" — "` out of `PlayerCtl.title` to reconstruct
episode context (`MiniPlayer.qml:130`). For music that is guesswork over a string that
was already structured — read `artists`, `album`, `name` off the queue entry directly.

### Full-screen: `NowPlayingPanel`, raised

Keep the two-pane structure; it is right. Add, in priority order:

1. **The wash** (Rule 2) behind everything, and the cover with a real drop shadow
   (`Theme.elevation4`) so the sleeve sits *on* the surface rather than in it.
2. **Lyrics** as a third pane, time-synced when the server has them.
   **[verify]** Emby 4.9.5's lyrics endpoint — `/Audio/{id}/Lyrics` — and whether it
   returns timestamps or plain text. Plain text still earns the pane; unsynced lyrics
   beat no lyrics.
3. **Up-next-in-context**: the queue pane already lists what is coming; label the
   source ("from *Lift Yr Skinny Fists*", "artist radio") so a queue's provenance is
   visible.
4. **Technical readout** on the sleeve's underside, mono, small: `FLAC 24/96 · 2,304
   kbps · Direct play`. `PlayerCtl` already publishes stream method and the source's
   streams. This is the detail that tells an audiophile the app respects them, and it
   is nearly free.
5. **Waveform-shaped scrubber** — deferred, and named here only to say it is deferred:
   it needs peak data the server does not serve.

---

## 5. One track row, not four

**Verified.** `AlbumPage.qml` (1,311 lines), `PlaylistPage.qml` (1,496), and
`QueuePanel.qml` each define their own track row delegate inline; `ArtistPage`'s top
tracks is a fourth. They will not gain the artwork fix, the now-playing indicator, or a
music context menu at the same rate, and they already differ.

Extract `src/ui/controls/TrackRow.qml`: index or now-playing indicator, optional cover
thumb, title, artist (shown only when it differs from the album's — `AlbumPage`'s rule,
decided once per table so columns stay aligned), duration, favourite, overflow menu.
Then extract `TrackTable.qml` for disc grouping, multi-select, and type-to-jump.

Both are single tab stops that own Up/Down internally, per the existing convention
("a list or strip is one tab stop, not N", ARCHITECTURE.md §4).

This also removes the `MusicPage.playAlbum` hack. Today, pressing ▸ on an album card
calls `MusicCtl.openAlbum()` and waits on `tracks.onCountChanged` behind a
`pendingPlayAlbumId` guard (`MusicPage.qml:127-186`) — it hijacks the shared tracks
model as a side channel, which means playing an album *navigates controller state* and
will fight the album page the moment both are live. Replace with a real verb:
`MusicController::playAlbum(albumId)` that fetches into a scratch list and hands
`ItemActions` the ordered items. One implementation per verb, which is rule 3 of the
architecture.

---

## 6. Desktop integration and the audio path

These are the items that separate a music player from a video player that can play
music, and two of them are the most visible defects on the target platform.

**6.1 — MPRIS metadata is nearly empty.** `MprisPlayer::metadata()`
(`MprisPlayer.cpp:156`) publishes `mpris:trackid`, `mpris:length`, `xesam:title`,
`xesam:artist`. On Plasma that means the panel applet, the lock screen and the media
notification show a title and a blank square. Add `mpris:artUrl` (a `file://` to a
cached cover — the disk cache in `EmbyImageProvider` already has the bytes, it needs a
path handed out), `xesam:album`, `xesam:albumArtist`, `xesam:trackNumber`,
`xesam:useCount`, `xesam:userRating`. Implement `Next`/`Previous`: verified still
empty stubs with `canGoNext()`/`canGoPrevious()` hardcoded to `false`
(`MprisPlayer.cpp:80-81,95-96`), so Plasma's applet renders both buttons dead.
`PlayQueue` has had `advance()`/`goBack()` all along — this is a wire-up.

**6.2 — Gapless.** Verified: `MpvPlayer.cpp:177` issues `loadfile <url> replace` per
item and nothing ever appends, so every track boundary tears down and re-opens the
engine's stream. mpv gapless wants a playlist handed to the engine, not per-item loads. This is the single biggest audio-quality gap and it is
genuinely hard, because `PlayerController` owns per-item tickets, position reporting and
recovery. Proposal: hand mpv the *next* item only, via `loadfile … append`, once the
current track passes 80% and the queue's next entry has a resolved ticket — a one-deep
lookahead rather than a full playlist. It preserves per-item ticket handling and
recovery, and it fixes live albums and DJ mixes, which is where the seam is unbearable.
Prototype before committing: **[verify]** that mpv's `gapless-audio=yes` actually closes
the seam with a one-deep append under transcode as well as direct play.

**6.3 — Volume normalisation.** ReplayGain tags via mpv's `af=replaygain` or
`replaygain=track|album`. A settings toggle: off / track / album. Cheap, and it is the
difference between a shuffled library being listenable and not.

**6.4 — Crossfade.** Optional, 0–12 s. Depends on 6.2. Lower priority than it looks —
gapless matters more, and crossfade over gapless is wrong for albums.

---

## 7. Beyond Emby's own players

The brief asks for a higher bar. These are the items where clearing it is realistic:

- **Instant mix / artist radio.** `/Items/{id}/InstantMix` and `/Artists/InstantMix`
  **[verify]**, surfaced as ▸ on an artist card — which today just opens the artist page
  because there is no verb that queues by artist (`MusicPage.qml:395`). This turns the
  artist grid from a directory into something you can play.
- **A real "recently played" and "most played" for music**, not the generic Home rails.
- **Type-to-find inside a track table** — 200-track box sets need it.
- **Multi-select** in the Songs tab and every track table, with queue / add-to-playlist /
  favourite as batch verbs. Emby Web does not have this and it is the thing power users
  ask for first.
- **A music context for the input map.** `InputMap` is contextual already (browse /
  player). Music deserves a third: `/` for search-in-library, `S` shuffle-all,
  `L` favourite, space for play/pause **while browsing** — which today does nothing,
  because space belongs to the player context and the user is on a grid.
- **Gamepad**: the docked bar needs to be reachable. `MiniPlayer.focusTransport()`
  exists and `Main.qml` calls it from nowhere obvious — give it a binding.

---

## 8. Order of work

Each phase is shippable on its own and leaves the tree green.

| Phase | Content | Why here |
|---|---|---|
| **0** | §1 artwork (`MediaItem`, mapper, `coverSource()`, `PosterUrlRole`) + DTO test | Everything visual is a lie until covers resolve. Smallest change, largest visible delta. |
| **1** | §5 `TrackRow`/`TrackTable` extraction; kill the `playAlbum` hack | Consolidate before adding, or every later change lands four times. |
| **2** | §2 `MusicController` query axes, Songs tab, `/MusicGenres`, `FilterBar` generalisation | The library ask, in full. Independent of the player work. |
| **3** | §3 audio playlists | Depends on 2's tab structure. |
| **4** | §4 the bar: audio mode, square art, shuffle/repeat/favourite, queue peek, `PlayerCtl.isAudio` | The integrated-player ask. Depends on 0 for art. |
| **5** | §4 the wash + the continuous sleeve transition; `NowPlayingPanel` raised | The signature. Depends on 4. |
| **6** | §6.1 MPRIS metadata + Next/Previous | Small, high visibility on Plasma. Could move earlier if desired — it has no dependency on 1–5. |
| **7** | §6.2 gapless prototype, §6.3 ReplayGain | Hardest, and the one with a real chance of not working as designed. Prototype gates it. |
| **8** | §7 instant mix, multi-select, music input context | The bar-clearing items, once the foundation holds. |

### What I would cut if the plan has to be shorter

Phases 0, 1, 2 and 4. That is the artwork fixed, the duplication removed, the library
filterable, and the bar made music-shaped — the four things the brief actually asks for.
5, 6 and 7 are what make it better than Emby; 8 is what makes it better than most
dedicated music clients.

### Risks

- **§6.2 gapless may not be reachable** without restructuring `PlayerController`'s
  per-item ticket ownership. Prototype first; if the one-deep append does not close the
  seam, say so and stop rather than half-landing it.
- **§4's sampled tint** is the one place this plan can make the app *worse* — an
  unclamped wash breaks the focus-ring guarantee the whole accent choice rests on. The
  clamp is non-negotiable and belongs in a test, not a review comment.
- **`FilterBar` generalisation touches `LibraryPage`**, which is not a music file. It is
  still the right call — the alternative is a second filter bar that drifts — but it
  needs the library page's own regression pass, not just music's.
