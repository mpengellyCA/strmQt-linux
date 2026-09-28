# StrmQt — Architecture

A native Emby client for KDE Plasma: C++20, Qt 6 / Qt Quick, libmpv.

This document describes how the application is built and why it is built that way.
It is the only design document in the repository; the milestone plans and build
journals it replaces have been removed.

---

## 1. Shape of the program

```
                    ┌──────────────────────────────┐
   Qt Quick  ─────► │  ui/   pages · controls ·    │
   (QML)            │        shell · player        │
                    └──────────────┬───────────────┘
                        context properties
                    ┌──────────────▼───────────────┐
                    │  app/  controllers · models  │  QObject / QAbstractListModel
                    │        ItemActions · queue   │
                    └───────┬──────────────┬───────┘
                            │              │
              ┌─────────────▼───┐   ┌──────▼──────────────┐
              │ server/         │   │ playback/           │
              │  EmbyClient     │   │  PlayerBackend      │
              │  EmbyWebSocket  │   │  MpvPlayer · Vlc    │
              │  DTOs + mapper  │   └─────────────────────┘
              └─────────────────┘
                    ┌──────────────────────────────┐
                    │ core/ Settings · Result · Log│
                    │ platform/ SecretsStore ·     │
                    │   MprisPlayer · PowerInhibit │
                    │ input/ InputMap · Gamepad    │
                    └──────────────────────────────┘
```

Four rules hold this together:

1. **QML states intent; C++ decides.** A page calls `Actions.play(item)` or
   `LibraryCtl.setSort(...)`. It never builds a query, never pushes a page, and
   never talks to the network.
2. **`Main.qml` alone owns navigation.** Pages emit or call verbs; `ItemActions`
   turns them into signals; `Main.qml` pushes and pops. This is why a page can be
   reached from a rail, a search result, a context menu and a remote command
   without any of them knowing about each other.
3. **One implementation per verb.** Play, shuffle, queue, mark-played and
   favourite exist once, in `ItemActions`, shared by cards, menus, the details
   page, the player and the remote-control service.
4. **Playback engines are interfaces.** `PlayerBackend` has mpv and VLC
   implementations. The server side is not equivalently pluggable today:
   controllers depend on `EmbyClient`, while backend-neutral DTOs and mapping
   keep Emby wire formats out of QML. Supporting another media service requires
   introducing a substantive service interface and adapting the controllers;
   it is not a drop-in backend addition.

Music has a domain layer of its own between the client and the pages. Emby's
item payloads are reshaped once, in C++, into music value types (`src/server/dto/music/`:
`Track`, `Album`, `AlbumSleeve`, `ArtistProfile`, `GenreBin`, `Station`, …) by
`EmbyMusicMapper`, which derives the format badge, featured artists and release type.
`MusicRepository` (`src/app/music/`) is the only music unit that holds an `EmbyClient`:
it composes several requests into one answer (a sleeve is the album and its tracks,
fetched in parallel, then more by the artist), degrades a failed *secondary* request
to an empty part, and caches per account in memory. Typed list models and one controller per
surface sit above it, so a music page binds display-ready roles ("48 min",
"FLAC 24/96", "12 records") and never reshapes data itself. `MusicPlayback` owns every
music play verb and hands `ItemActions` ordinary queue maps, so the queue and the
verbs never learn about music types.

### Threading and async

Application and controller state is affine to the Qt event-loop thread. Work
that can block or consume a frame budget is dispatched in bounded tasks and
returns value types before that state is touched again:

- Network calls return `QFuture<Result<T>>` resolved on the caller's thread.
- `Result<T>` is an explicit success/error union — no exceptions cross an API
  boundary.
- Image decoding and legacy secret-file `QSettings`/`QFile` work run on bounded
  Qt thread-pool tasks. Their GUI-thread owners receive only copied result
  values through lifetime-guarded watchers.
- libmpv runs its own threads internally and is marshalled back through
  `QMetaObject::invokeMethod`.

**Generation counters guard every async reply.** A controller increments a counter
when a new request supersedes an older one, and a reply whose generation no longer
matches is dropped. Without this a slow first reply lands after a newer one and
shows results for a query the user has already replaced. Every controller that can
be re-targeted (`Library`, `Search`, `Details`, `Series`, `Music`, `Playlist`,
`Player`) does this.

### Supported Qt versions and compatibility shims

StrmQt builds on Qt 6.4 and up, in two tiers. **Full** (Qt 6.8+) is today's
app, unchanged. **Compat** (Qt 6.4–6.7) keeps every feature and every page; it
only changes how a few things look, because Qt 6.8 is where `MultiEffect` (the
effect type the design language is built on) and a run of font APIs land. The
switch is single, at 6.8, because no distro this release targets ships
anything in 6.5–6.7 — a Qt in that gap is a developer's own upgrade, not a
package. Condensed, what compat loses:

| Surface | Full tier | Compat tier |
|---|---|---|
| Icon tint, shadows, masks, backdrop wash | `MultiEffect` | `Qt5Compat.GraphicalEffects` |
| Tabular figures, Crate display width | `font.features`/`font.variableAxes` | not set — invisible on the bundled faces, or narrower headings |
| `Loader`/list header-footer hosting a bound component | the real `Loader`/component | a `createObject()` host (6.4's object creator refuses a bound component outside its creation context) |
| QML type annotations | enforced and coerced (6.7+) | ignored by the interpreter |

**Choosing a tier is one build decision, never a runtime check.** The CMake
cache variable `STRMQT_QML_TIER` (`auto`/`full`/`compat`, resolved by
`cmake/StrmQtQmlTier.cmake`) picks `full` on Qt ≥ 6.8 and `compat` otherwise; an
explicit `full` fails configure below Qt 6.7. `src/ui/shims/full` and
`src/ui/shims/compat` hold the same file names — `StrmTint`, `StrmShadow`, `StrmMask`, `StrmBackdropBlur`,
`BoundLoader`, `BoundViewSlot`, `TabularText`, `TabularMetrics`,
`CrateDisplayText` — and only one directory joins the `StrmQt` module, so the
type name (the file's basename) resolves identically everywhere. `BoundLoader`
and `BoundViewSlot` exist because a `Loader` or a `ListView` header/footer that
hosts a `pragma ComponentBehavior: Bound` component cannot be built directly on
6.4; their compat versions `createObject()` it and reproduce the host's sizing
and focus scope. No QML outside `src/ui/shims` imports `QtQuick.Effects` or
`Qt5Compat.GraphicalEffects` — callers write `StrmTint { … }` and never see
which module answered. The resolved tier is written to
a build-directory file, `strmqt-qml-tier.txt`, which `packaging/debian/rules`
reads to compute the effects-module dependency, and logged once at startup
(`src/app/Application.cpp`, beside the version line).

**Three C++ sites are guarded by `QT_VERSION_CHECK` rather than a compatibility
header**, because each stands alone: `src/app/main.cpp` falls back from
`QQmlApplicationEngine::loadFromModule` (Qt 6.5) to `addImportPath`/`load` on a
fixed URL, which works on every Qt because `RESOURCE_PREFIX`
(`src/CMakeLists.txt`) pins the module under `qrc:/qt/qml` — only the
*default* from 6.5 on. `InputMap::trigger` (`src/input/InputMap.cpp`) cannot
assume `qReturnArg` (Qt 6.5), so `invokeAction` runs through a helper that
reads the handler's meta-object and **shapes the call from the signature it
reports** — `(QString, bool) -> bool` or `(QVariant, QVariant) -> QVariant` —
because a QML handler's typed-vs-untyped signature depends on the Qt it runs
on, and guessing the shape would silently drop the handler on some Qt
versions. `MusicRepository::ready` (`src/app/music/MusicRepository.cpp`) uses
`QtFuture::makeReadyFuture` below Qt 6.6 and the un-deprecated
`makeReadyValueFuture` from 6.6, so the full-tier build's `-Werror` never sees
the deprecation.

**The build floor is CMake 3.25**, Debian 12's version. The bundled SDL3 fetch
(`cmake/StrmQtSdl3.cmake`) is the one place that would want `FetchContent`'s
`EXCLUDE_FROM_ALL` (3.28) to keep SDL3 out of the install tree; it sets
`SDL_INSTALL=OFF` instead, which does the same job on 3.25.

**The binary imports one `Qt_6_PRIVATE_API` symbol,
`QQmlPrivate::compositeMetaType`,** and carries `qmlcachegen`'s ahead-of-time
code, valid only for the Qt it was generated against — why every native
package is built inside, and pinned to, one distro release's Qt (§9) rather
than shipped once for a whole distro family.

Qt 6.4 behaviour no review can settle was checked by running the full suite
and the page self-test in Ubuntu 24.04 and Debian 12 containers from the first
porting task onward, plus the `floor` CI job that keeps doing it on every
push. The sweep's findings — the bound-component loss behind
`BoundLoader`/`BoundViewSlot`, the mpv console-switch rename, the pre-6.5
`Overlay` import — are recorded under "Task 7: Qt 6.4 behaviour sweep" in
`docs/superpowers/plans/2026-09-27-distro-compat-075-verifications.md`.

---

## 2. Server layer

`EmbyClient` is a thin, direct REST client over QtNetwork — no SDK, no wrapper
library. `EmbyDtoMapper` turns JSON into DTOs and is deliberately tolerant: unknown
fields are ignored, absent fields take defaults, and mixed or negative index numbers
parse without special-casing.

`EmbyWebSocket` carries live updates. `LiveUpdateService` sits above it and falls
back to polling when the socket is unavailable, suspends while the video decoder is
running, suspends while the window is unfocused, and refreshes immediately on
playback stop and on regaining focus.

### Emby behaviour worth knowing

Every item here was measured against a live Emby 4.9.5.0 server, and most of them
overturned a reasonable reading of the API. They are the reason parts of this
codebase look the way they do.

| Behaviour | Consequence |
|---|---|
| The server closes a WebSocket after ~30 s of client silence | A keep-alive is mandatory, not an optimisation. A healthy socket is otherwise silent indefinitely, so protocol ping/pong is the only usable liveness probe. |
| An unparseable client frame causes an immediate disconnect | Every outbound message is built as JSON, never concatenated. |
| `UserDataChanged` carries the whole record | Model rows are patched in place instead of refetched — including playback position, or a watched state updates while the progress bar stays stale. |
| **`ContainsItemId` is silently ignored** | It returns *every* BoxSet rather than failing. `ListItemIds` is the only query that answers "which collections contain this item"; nothing on the item payload does. |
| **`AdjacentTo` is silently ignored** | On `/Shows/{id}/Episodes` it returns the entire series. `StartItemId` returns the list from an episode onward, so "the next episode" is the second row — and it crosses season boundaries. |
| `/Persons` and `/Genres` report `TotalRecordCount = 0` while returning rows | Anything paging on that count renders an empty list. Callers use the array's own size. |
| **A playlist carries no media type anywhere on the wire** | Absent from the `/Items` list payload, absent from the item detail payload, and `Fields=MediaType` does not add it. Nothing on a playlist says whether it holds music or films. |
| **`MediaTypes` discards `IncludeItemTypes`** | Asked together with `IncludeItemTypes=Playlist` it returns the whole library — 204,528 albums and tracks — and `Audio` and `Video` answer identically. It is not a filter that fails; it is a filter that deletes the constraint beside it. |
| `ParentId` filters playlists by media type even though they live outside every library | A playlist created through `POST /Playlists` is stored under `data/userplaylists`, not in a library folder, yet the music library's id returns the audio ones and the movie library's the video ones. This is the only way to ask for "this library's playlists", and it is one request — the music library's Playlists tab needs no per-playlist probe. A playlist with **no members** belongs to no library at all, whatever `MediaType` it was created with. |
| Image *enhancers* composite decorations into the bytes served | An episode still came back as a 640×438 PNG of a television set with the still inside its bezel, 375 KB, versus a 400×225 JPEG at 48 KB. `EnableImageEnhancers=false` on every image request. |
| `PlaybackInfo` on a folder is an HTTP 500 | Series, BoxSet, MusicArtist and MusicAlbum all fail with `Unable to cast … to IHasMediaSources`. "Play" on a container plays its *contents*. |
| A DirectPlay profile that omits a container causes a lossy transcode | Not an error — a silently worse stream. The audio container list covers what ffmpeg decodes, including DSD (`dsf`/`dff`), and there is an audio TranscodingProfile so an unsupported container has a defined fallback rather than an improvised one. |
| There is no rename endpoint | Renaming is read-modify-write through `UpdateItem`: fetch the whole item, change `Name`, post it back, and drop `SortName`/`ForcedSortName` or it keeps filing under the old name. |
| Emby files a person's birth date under `PremiereDate` and birthplace under `ProductionLocations` | A person is an ordinary item to Emby. That vocabulary is the server's, not a mistake to correct. |
| **`/Items/{id}/InstantMix` serves a track, an album *and* an artist** | There is one instant-mix verb, not three. `/Artists/InstantMix` adds nothing: it wants an `Id`, not a name — `Name=angela` is an HTTP 500, "Unrecognized Guid format." — and answers the same shape. |
| **InstantMix does not page, and its `TotalRecordCount` is the array's own size** | `StartIndex=5` answers a *fresh* randomised set, not the sixth row onward. One mix is one request; a second page would be a second station. |
| A track seed comes back as InstantMix row 0 and does not count against `Limit` | `Limit=50` returns 51 rows. Right for "play this, then things like it", and no special case is needed. An album or artist seed is not itself audio and returns exactly `Limit`. |
| **InstantMix rows are not distinct** | 500 asked for came back as 493 unique ids. `PlayQueue` keys entries rather than ids, so the repeats would survive to the queue panel; callers de-duplicate. |
| `AudioCodecs` filters tracks, but on `MusicAlbum` it drops every album (4.9.5) | `AudioCodecs=flac` leaves 55,400 of 56,283 tracks and 0 of 5,037 albums. The Format filter applies to Songs only; `MusicQueryTranslator` never sends `AudioCodecs` for albums. |
| No bit-depth or sample-rate filter exists | `MinBitDepth`, `MinAudioBitDepth`, `MinSampleRate` and five similar names are all ignored while hi-res tracks exist. "Hi-res only" is not offered. |
| `Years` takes a comma-separated decade | `Years=1970,…,1979` is one request, so the Decade pill needs no premiere-date range. "Earlier" is the exception: a single `MaxPremiereDate` bound at the end of 1949. |
| Nothing on the wire says EP, single or compilation | No `AlbumType`/`ReleaseType`, and album tags are empty. The release type is classified client-side. |
| **Lyrics are an embedded text subtitle stream, not a lyrics endpoint** | `/Audio/{id}/Lyrics` is a 500 and `/Items/{id}/Lyrics` a 404. A track with lyrics has a `Subtitle` stream titled `Lyrics`; `/Items/{id}/{mediaSourceId}/Subtitles/{index}/Stream.js` returns `TrackEvents[].Text`, untimed in every sample. |
| `/Artists/{id}/Similar` returns similar artists | The artist page's Similar row is one request. |
| `MinDateCreated` filters albums | "N added this week" is one `Limit=0` request. |
| **Albums keep no play data** | An album whose tracks were just played still reports `PlayCount: 0`, `Played: false` and no `LastPlayedDate`, so `SortBy=PlayCount`/`DatePlayed` on `MusicAlbum` is meaningless. Album play history is derived from tracks. |
| `/MusicGenres` returns no item counts | `Fields=ItemCounts` adds nothing. A genre bin's record count is derived client-side by walking the library's albums and counting their genres. |
| **List queries hide track play counts and last-played dates** | An `Audio` list sorted by `DatePlayed` is correctly ordered, but each row reports `PlayCount: 0` and no `LastPlayedDate`. `Fields=UserDataPlayCount,UserDataLastPlayedDate` makes the list carry the real values. |
| Audio lists carry `MediaStreams` and album lists carry `RunTimeTicks` | Format badges and album runtimes need no per-item fetch. |
| `GenreItems[].Id` is a JSON number | Artist ids on the same item are strings. Id parsing accepts both. |

---

## 3. Playback

### The stream ladder

`PlaybackInfo` returns one or more **media sources** (versions of the same item),
each with its own ordered ladder of delivery methods: DirectPlay → DirectStream →
Transcode.

The ladder never mixes sources. Demoting a rung degrades *the version the user
chose*; it does not silently swap to a different cut with a different runtime.

`PlayerController` owns recovery: a watchdog for stalls, playback-ticket refresh,
and crash resume. Engines report clean EOF separately from fatal errors; the
controller never guesses that an error is EOF from the playhead position. Position
is reported to the server on a timer and at start/stop so watch state survives.

### Queue

`PlayQueue` is the session object that shuffle, auto-advance, Up Next, prev/next
and "play all" are all built on. **Queue identity is a per-entry key, not an item
id** — the same episode or track can legitimately appear twice, and an id-keyed
queue corrupts on the second occurrence.

Auto-advance has a precedence rule:

- A queue of more than one item was built deliberately ("play all", a shuffle, a
  manual add). Its end is a deliberate end.
- A single-item queue means one thing was played directly. If it was an episode
  and the preference is on, the series continues.

A queue also remembers **where it came from**: `PlayQueue::sourceLabel` ("Sunburned
Almanac", "Station · Heavy rotation", "Radio · Björk") is set by the play verb that
filled it and shown as "Playing from" in the music player. Remembering is only safe
because it is forgotten eagerly: replacing or clearing the queue, or adding anything
to it, drops the label, while reordering and removing keep it, and a film that
interrupts a record gives it back with the rest of the snapshot. Where no verb named
the source, the player falls back to `contextLabel`, which is derived from what the
queue holds.

### Skip and fast-forward

Next and previous follow one rule, owned by `PlayerController::skipForward` /
`skipBack` and shared by the remote's ⏭/⏮, the pad, MPRIS and the web remote's
Next/Previous: a video with chapters steps by chapter, then through the queue;
music and chapterless video step by queue entry. Holding ⏭ fast-forwards. Tap-or-hold
is a pure state machine (`playback/FastForwardRamp.h`): past 400 ms a press is a hold, at 2× doubling
each second to 32×; a tap skips on its release, a hold's release does not, and a
press within 150 ms of the last release is a stuttering remote and ignored. The
engine plays as fast as it can (mpv tops out at 4×, reported by
`PlayerBackend::maximumPlaybackSpeed()`) and the rest is made up by seeking ahead.
A Transcode stream never seeks ahead — each far seek restarts Emby's ffmpeg job — so
there the hold is capped at the engine's own speed. The watchdog stands down during
a hold, `playbackSpeed` keeps reporting the user's speed throughout, and the release
restores it.

### Engines

`MpvPlayer` uses the raw libmpv C API and the render API for embedding. Hardware
decoding is verified live per release. `VlcPlayer` exists as an escape hatch behind
`STRMQT_WITH_VLC` and has a known video-output defect.

**The video plane belongs to the shell, not to the player page.** Freeing mpv's
render context disables video for the file that is loaded, and mpv does not give
it back: a recreated context renders black, and neither `vid=auto` nor
`video-reload` recovers it (measured against libmpv 2.5). A plane owned by the
player page was therefore destroyed every time the page was left, so returning
to a film gave a black screen with sound. `Main.qml` owns one plane for the life
of the app and moves it between the page's slot and the picture-in-picture frame
that floats above the docked bar; reparenting and hiding both keep the renderer
alive, which is what makes that safe.

---

## 4. User interface

### Design language

The interface is called *Projection Booth*: a warm near-black ground (`#0C0B0A`)
with amber accent (`#F0A02A`). The accent is not KDE blue, Emby green, Jellyfin
purple or Plex orange — it sits outside the hue range of most poster art, so a
focus ring is never lost against the artwork it frames. Emby green, Jellyfin purple
and Breeze blue ship as alternates.

`Theme.qml` is the single token source: colour roles, a type scale over three
bundled typefaces (Archivo display, Public Sans body, IBM Plex Mono data), spacing,
radii, motion durations and easings, and a **density multiplier** (compact /
comfortable / TV) that scales the whole interface. Fonts and icons are compiled
into the binary, so a sandboxed artifact renders identically to a native build
without depending on the host's font set. [`docs/BRANDING.md`](docs/BRANDING.md)
covers the brand mark and verifies every token, weight and colour it documents
against this file. Every tint, shadow, mask and blur — `StrmIcon`'s tint
included — goes through the StrmTint shim and its siblings rather than an
effect type named directly, so the same QML builds on Qt 6.4 ("Supported Qt
versions and compatibility shims", §1).

Music speaks a dialect of it called *Crate*: the same ground, accent and typefaces,
set louder. Archivo is pushed wide and heavy for headings and hero titles, data sits
in Plex Mono, and an album is a square sleeve with a deep shadow and no card behind
it. Crate adds tokens (`Theme.crate*`) and no colours; the cover wash keeps its clamp.

### Hover is not focus

This is the rule the control library exists to enforce.

- **Hover** is where the pointer is. It tints, it raises, it shows a tooltip.
- **Focus** is where the keyboard is. It draws the amber ring, and only it does.

They can be true at once, and when they are, focus wins visually. **Hover must
never move keyboard focus** — a pointer passing over a rail must not steal the
place a gamepad user was holding. `FocusRing` is a purely visual component that
declares no input handling, precisely so it cannot be wired to a hover state.

### Controls

Everything is built from `src/ui/controls/` — buttons, cards, rails, grids, menus,
selects, sliders, chips, tooltips, toasts, skeletons. No page defines its own
button. Every control is simultaneously pointer- and focus-driven; retrofitting
pointer support onto a keyboard-only control is how the prototype ended up with two
`MouseArea`s in the entire application.

Two conventions worth knowing:

- **A list or strip is one tab stop, not N.** The alphabet bar, the view-mode
  switch and the season tabs each own Left/Right internally rather than putting 27
  dead stops in the tab chain.
- **Images are requested at the size they are drawn.** `sourceSize` is in *logical*
  pixels, so it is multiplied by `Screen.devicePixelRatio` and by the card's own
  scale. Without that, every card asks the server for a fraction of the pixels it
  then draws.
- **A clipping view leaves room for the focus ring.** `FocusRing` draws outside the
  item it frames, so a view with `clip: true` sliced the ring off whatever sat flush
  against its edge — the first card, the top row. Such views stop clipping and sit in
  a `FocusClip`, which keeps their geometry and clips `Theme.focusRingOutset` further
  out (or `Theme.focusHeadroom()` for a raised card). Padding the content instead does
  not work: `positionViewAtIndex()` ignores a view's margins.

### Moving about with a D-pad

Three rules, and each of them replaced something that read as the selection
jumping about rather than being walked.

- **A vertical step keeps the column.** A page of horizontal shelves has one
  cursor per shelf, and each shelf remembers where it was left — so Down out of
  card 3 landed on whatever card the next shelf was last parked on, usually its
  far end. The shelf being left publishes the x of the card under the ring and
  the shelf being entered puts its cursor on whichever of its own cards sits
  nearest it (`NavigationColumn`). Each shelf keeps its own scroll offset;
  nothing scrolls sideways to satisfy this. It is armed by an Up/Down keypress
  and disarms itself on the next turn of the event loop, so a click, a Tab or a
  back-stack restore never sees it.

- **The edges of a row are edges, not wraps.** `GridView`'s own Left/Right walk
  the model, so Left in the first column landed on the *last* card of the row
  above. Both edges are claimed before the view sees them.

- **The edges of a page reach the chrome.** Left off the left-hand edge opens and
  focuses the destination rail; Up off the top reaches the header. Both are
  fallbacks on the `StackView` (plus one in `StrmGrid`, which has to claim Left
  itself to stop the wrap), so they only fire once every control between the
  keyboard and the shell has declined the key — a grid mid-row, an armed
  scrubber, a filter chain and the two-pane playlist all answer first. Both are
  auto-repeat guarded: the rail hands the keyboard back on its own Left, so a
  held direction would otherwise swap the two surfaces several times a second.

This is what makes the shell reachable without a pointer at all. The rail expands
on hover or focus and a gamepad has neither; the header carries Back, Forward and
the only route to signing out, and every one of them was a pointer or a Tab away.

### Pages

`Main.qml` owns a `StackView`, the navigation history (including a forward stack,
which `StackView` does not provide), and focus memory. It restores the exact
loaded item by stable identity after Back/Forward; if that item is absent when
the controller's bounded refill finishes, it uses the nearest eligible row (or
the page's normal empty-state focus) without fetching extra pages.

Pages: Login · Home · Library · Details · Series · Person · Playlist · Search ·
Settings · Player, and for music: Music Home · Music Browse · Album · Artist ·
Music Playlist.

A music library opens on **Music Home**, a page of independently loading shelves
(pick up where you left off, recently played, new in the crate, stations, genre
bins, artists you play, forgotten favourites, pull one out). Each shelf is its own
lane: it has its own skeleton, hides when empty and collapses to one Retry line when
it fails, while the others stay live. **Music Browse** is the same library read
five ways (Albums · Artists · Songs · Genres · Playlists) under a section strip,
sharing one set of filter pills and keeping a sort per section. Its A–Z crate
dividers send a letter as a `NameStartsWithOrGreater`/`NameLessThan` pair, and
choosing a genre bin is a filter, not a page. Its Playlists section is the user's
*audio* playlists; the nav rail's Playlists destination is still all of them,
because a picker raised from a film has to keep offering film lists.

---

## 5. Input

`InputMap` is the single source of truth for every binding. It feeds the shortcut
sheet, the remapping UI and every page, and it is what makes a rebind take effect
everywhere at once — including on the gamepad.

**Actions are invoked by id; keys are one way to reach them.** A gamepad button,
a web remote tap and a command palette pick all name an *action id* and call
`InputMap::trigger()`, which asks the registered handlers newest first until one
answers (`invokeAction(actionId, autoRepeat) -> bool`). Commands — fullscreen,
settings, stop, volume — are answered by the QML that owns them: `MappedShortcut`
and the player page register themselves, and answer only while they are live. No
key is synthesised, so a command works with the window in the background and
whatever the key is bound to.

The navigation actions (`InputMap::isNavigationAction`: arrows, Select, Back,
paging, the context menu) are the exception, because their meaning belongs to the
focused control. `NavigationKeyHandler`, registered first and so asked last,
delivers the key currently bound to the action to the focused item. A rebind moves
the pad and the phone with it, and a gamepad hint shown in the UI is the binding
that actually fires. A navigation action with a gamepad hint must resolve to
exactly one key — asserted in tests, because a hint without a working binding is
worse than no hint. Keys posted to a background window still land because
`WindowFocusKeeper` stops Qt clearing the focus chain on deactivation.

The layout targets Xbox 360 / Xbox One, which is the PC standard and the layout
SDL's own gamepad abstraction is modelled on. Mapping is context-dependent, so one
pair of shoulders seeks ±60 s during playback and changes section while browsing.
The triggers seek ±10 s in the player, the left stick click (L3) toggles between
the full player and the docked bar, and the right stick's vertical axis is
volume there — the one player control a pad cannot reach by focus.

**While browsing, the shoulders and the triggers divide the work of moving
about.** The shoulders change what *section* is on screen: the page's own tab bar
where it has one — the music section strip, a season, a settings section — and the
previous/next library where it has not. The triggers move *through* the list on
it: a letter on the alphabet strip where the page has one, which is the only sane
way across a 1300-item library from a pad, and a screenful of whatever holds the
keyboard where it has not. Both resolve through the shell, which is the only
thing that knows which page is up (`Main.qml` `cycleSection()` / `jumpLetter()`);
a page opts in by exposing `cycleTab(step)` or `jumpLetter(step)`, and nothing in
the shell has to learn about a page that grows tabs later.

**A held A opens the item menu.** Every verb a card draws under a pointer — play,
mark watched, favourite, add to a playlist, go to the series — lived behind a
right-click or a hover `⋯`, so it did not exist for a keyboard or a pad at all.
It has the Menu key (`nav.contextMenu`) and, on a pad, a half-second hold of A;
tapping A still selects, committed on the release so the two can be told apart.
The rule is a pure state machine in `input/GamepadDecision.h`, because what makes
it subtle is an ordering — a release arriving after the hold has already fired
must not *also* select — and orderings are what a device cannot be made to
reproduce on demand.

**There are three contexts, not two.** Browse, player, and *music* — Music Home,
Music Browse, an album, an artist and an audio playlist page. Two actions only conflict when their
contexts overlap, and that is the whole reason music has one: Space, `S` and `L`
are each already bound in browse or in player, and only a non-overlapping
context lets a music page mean play/pause, shuffle-this-library and favourite by
them. Music pages arm their own shortcuts on their own `visible`, so "the music
context is live" is "a music page is the one on screen" and nothing tracks it
centrally. A page-owned `Shortcut` must be gated that way: `Shortcut` is
window-scoped and a `StackView` keeps covered pages alive.

One coexistence rule follows from it. `TrackTable` claims single printable
characters through `Keys.onShortcutOverride` while it holds focus and
type-to-jump is on, so `s` typed into a track table jumps to a song rather than
shuffling the library — which is right, the user is typing. Space is exempt at
the table until a word is already being typed, so play/pause still works from a
track list.

**A TV remote is a keyboard with odd keys, rewritten at the window.** Bluetooth
remotes send keys no page answers, so three event filters on the window translate
them before the focused item sees them. `RemoteOkKeyFilter` turns OK
(`Qt::Key_Select`, or keysym XF86OK, which Qt 6.11 delivers with key 0) into Return.
`RemoteBackKeyFilter` turns any non-typable key bound to `nav.back` — `Qt::Key_Back`
by default — into its primary binding, Esc, because menus, panels and `Popup` answer
Esc by name. `SkipKeyFilter` catches `player.skipForward` / `skipBack` while something
plays, as a filter rather than a `Shortcut` because a `Shortcut` never sees the release
that tells a tap from a hold. The rest are ordinary actions: `app.home` (Home Page) is
global, and `library.open1`…`9` and `library.favorites` (the digits) are browse-only,
so a stray digit cannot leave a film; a focused alphabet strip claims digits through
`Keys.onShortcutOverride`, as `TrackTable` does for letters. For the next unfamiliar
remote, `QT_LOGGING_RULES="strmqt.input.keys.debug=true"` installs `KeyEventLogger`
on the application, which logs every key with its scan code and keysym.

**The stick is one control, not two axes.** Only the dominant axis acts, and it
must lead by a margin; a true diagonal moves nothing, because the user has not said
which way they mean. Vertical starts at a higher threshold than horizontal, since a
hand rolls up-down more easily than left-right. Whichever axis owns the stick keeps
it until released. The decision is a pure function in `input/StickDecision.h` with
its own tests — the interesting cases are all diagonals and cannot be checked by
reading.

---

## 6. Remote control

Another Emby client — a phone app, Emby Web, a second StrmQt — can drive this one.

This is two halves and shipping one is worse than shipping neither. Handling
commands without declaring capabilities means no client offers this one as a
target; declaring capabilities without handling them means it appears as a target
that silently does nothing. **The declared command list is generated from the same
place the handlers live**, and only commands with a real verb behind them are
declared.

Capabilities are re-sent on every reconnect: a resumed socket is a new session to
the server, so a single announcement at startup is forgotten the first time the
network blinks.

---

## 7. Testing

```bash
ctest --preset dev                     # 75 suites
cmake --build <dir> --target strmqt_qmllint
STRMQT_SELFTEST=1 QT_QPA_PLATFORM=offscreen ./strmqt
```

- **Unit** — DTO mapping, settings, the stream ladder, queue behaviour, the input
  map, the stick decision table, and `tst_secret_backends` — the probe order,
  the wire formats and the unavailable/refused split (§8) — over a fake D-Bus
  transport (`src/platform/secrets/DBusTransport.h`) rather than a real bus.
- **Integration** — controllers and the client against a local `QTcpServer` mock
  replaying recorded fixtures. No network, no display, no session bus.

The distro matrix runs in containers, not on the developer's own Qt:
`scripts/ci/local.sh` and `scripts/ci/check.sh` build, test and self-test one
distro's image (Arch and, since 0.7.5, Ubuntu 24.04/26.04, Debian 12/13 and
Fedora 43/44) so a developer reproduces a container failure without waiting on
CI. `.github/workflows/ci.yml` runs the Arch build on every push, plus a
`floor` job (Ubuntu 24.04, Qt 6.4.2, the compat tier) and a `debian-13` job
(Qt 6.8.2, the oldest full-tier Qt) — the two ends of the supported range.
`.github/workflows/packages.yml` goes further: it builds every `.deb`, `.rpm`
and the AppImage, then installs each into a fresh container of its own release
and reruns the page self-test there, so a package is proven installable, not
just buildable.

One lesson from porting `tst_navigation_history` to Qt 6.8: **stage a QML
module once per run, not once per test**, because Qt 6.8's type loader
remembers where it first resolved a module URI across engines, and a per-test
copy left later tests resolving `StrmQt` against an already-deleted directory.

Three more things about this gate are not obvious and were each learned from a
defect that reached a user:

1. **`strmqt_qmllint` exits 0 on warnings.** The exit code is not the gate. CI
   normalizes warning locations and compares the complete warning stream with a
   checked-in fingerprint, so a new warning fails even when qmllint succeeds.
   Intentional warning-set changes require regenerating and reviewing that
   baseline; type-resolution failures remain visible in the raw job log.
2. **A plain offscreen run proves far less than it looks like.** `StackView` only
   ever constructs its `initialItem`, so a QML type error in any page reached by a
   push survives a clean startup. `STRMQT_SELFTEST=1` constructs every page and
   exits non-zero if any fails. For a packaged build, run it *inside* the artifact:
   a successful build proves the compiler was happy, not that the packaged QML
   module resolves.
3. **`QT_ASSUME_STDERR_HAS_CONSOLE=1` is required** for offscreen runs, or QML
   diagnostics vanish under redirected stderr and an empty log reads as a pass.

A note on `strings`: it does not find `QStringLiteral` data, which is UTF-16
(`strings -el` does), and it does not reach inside an AppImage or a Flatpak. Two
false negatives in this repository came from that.

---

## 8. Configuration and secrets

Settings live in `QSettings` (INI under `~/.config/StrmQt/`). **There is no default
server address**: the artifacts are distributable, so a baked-in host would be both
a privacy leak and wrong for every user but one. The login screen asks.

Access tokens go to a system keyring, one per saved account, keyed
`emby/<sha256(server+user)>/accessToken`. There are three backends
(`src/platform/secrets/`), tried in a fixed order — **KWallet 6, then
KWallet 5, then the freedesktop Secret Service** — because a Plasma user's
existing tokens are already in KWallet, and asking a second keyring after a
KDE one answers would just duplicate them. `SecretsStore::chooseSecretBackends`
builds the candidate list from the session bus's owned and activatable names:
an *owned* wallet counts on any desktop, but an *activatable* one counts only
when `XDG_CURRENT_DESKTOP` says KDE — activating `kwalletd` on GNOME or a
window manager would greet the user with a "create a wallet" prompt for a
wallet nothing else on their desktop uses. Plasma 6 aliases `org.kde.kwalletd5`
onto `kwalletd6`'s own D-Bus unique name; when the store sees the two service
names share an owner it drops KWallet 5 from the candidate list rather than
asking the same daemon twice.

Each backend answers `Ok`, `Unavailable`, `Refused` or `Failed`. **Unavailable**
moves to the next backend (no wallet found, or it vanished between prepare and
open); **Refused** — the user dismissed an unlock prompt — goes straight to the
vault file without trying another keyring, on the same reasoning as the KWallet
order: a refusal is a choice, not an outage. Interactive calls (opening a
wallet, waiting on a prompt) carry a five-minute timeout; a timeout or a D-Bus
`NoReply` is **Failed**, which behaves like a refusal — the vault, with a
warning — rather than cascading to the next keyring, since a keyring that
cannot answer in five minutes is not one to keep waiting on.

If every keyring is unavailable, refused or failed, tokens fall back to a vault
file (`secrets.ini` under the app data location, `0600` owner-only) — lower
security, and the login screen says so while that mode is active; a vault
written while the keyring was down is migrated into it and scrubbed the next
time the keyring opens. Settings → Server shows a **Credentials** row naming
whichever backend is in use (`SecretsStore::backendName`, "KWallet", "KWallet
5", "Secret Service" or "vault file"), so a support conversation does not have
to guess.

The Secret Service backend stores one item per key, with attributes
`xdg:schema=ca.mikesdev.StrmQt.Secret` and `strmqt-key=<that key>` — the
server address and username appear in the keyring only as their hash, never in
the clear. It opens its session with the Secret Service's `plain` algorithm,
which puts the secret on the session bus unencrypted; that is the same
exposure KWallet's own string-based write already has, and the bus is
per-user, but it is a documented trade-off rather than an oversight — a
negotiated DH-AES session is follow-up work, not this release's.

Accounts are listed in a server-tagged registry (`accounts/registry`) backing
the login screen's profile picker: **switch user** keeps the profile and
token, **sign out** forgets both. No credential is ever written to the repo,
and TLS certificate errors are fatal in release builds.

---

## 9. Packaging

Five artifacts, all built from a clean checkout:

- **Arch package** — `install()` rules put two binaries, a desktop entry, AppStream
  metainfo and six icon sizes into `/usr`. Nothing else is installed: the QML module
  is compiled into the executable, so there is no qmldir or `.qml` to ship.
- **Flatpak** — `org.kde.Platform` 6.11 plus libmpv, libplacebo, libass and uchardet
  built from source. FFmpeg comes from the runtime.
- **`.deb`** (`packaging/debian/`) — one binary package, built inside and for
  exactly one release's container (`scripts/ci/package-deb.sh`, `dpkg-buildpackage`),
  targeting Ubuntu 24.04/26.04 and Debian 12/13. `packaging/debian/rules`'
  `override_dh_gencontrol` computes three dependencies no ELF `NEEDED` entry
  can express: `QtAbi` (pins the upstream Qt the binary was built against,
  since it imports a private symbol — §1), `QmlEffects` (the tier's effect
  module, read from `strmqt-qml-tier.txt`) and `SvgPlugin` (whichever package
  ships the Qt SVG image plugin, `libqsvg.so`, there).
- **`.rpm`** (`packaging/rpm/strmqt.spec`) — one package per Fedora release
  (43, 44; `scripts/ci/package-rpm.sh`, `rpmbuild`), which pins
  `Requires: qt6-qtdeclarative = %{_qt6_version}` to the exact Qt it was built
  against, for the same private-symbol reason. Fedora rebases Qt inside a
  release, so that pin holds the update back on a system that has StrmQt
  installed until a StrmQt rebuilt against the new Qt exists.
- **AppImage** (`packaging/appimage/`) — built in an `ubuntu:24.04` container
  with aqt-installed Qt 6.11.3, Qt's own qt-cmake deploy step plus patchelf.
  linuxdeploy is deliberately *not* used: its excludelist omits libva,
  libvulkan and libpulse, which are exactly the libraries whose bundling
  breaks hardware decode. The build checks, on every run, that nothing bundled
  imports a `GLIBC_` symbol newer than 2.39 or a `GLIBCXX_` symbol newer than
  3.4.33 — Ubuntu 24.04's floor — and fails otherwise; its type2 runtime is
  pinned by sha256. Release CI proves that floor by running the finished
  AppImage's self-test in bare `ubuntu:24.04`, `debian:trixie` and `fedora:43`
  containers (`.github/workflows/packages.yml`), not just trusting the check.

SDL3 (§5's only gamepad path) is bundled statically, from a pinned,
hash-checked `FetchContent` download, wherever the distro does not package
it — Ubuntu 24.04, Debian 12, and the AppImage. `packaging/debian/rules` turns
`STRMQT_BUNDLE_SDL3` on by probing `pkg-config --exists sdl3` in the target
container; the AppImage always turns it on, since aqt's Qt carries no SDL3
either. Fedora and Debian 13/Ubuntu 26.04 use the distro's own package.

The rule for what an AppImage may bundle: *pure-userspace codec and render code,
never anything that talks to a kernel device, the display server, the audio server
or the font database.* It is enforced three ways, including a hard assertion in the
build script that greps the AppDir for forbidden sonames — denylists rot, and the
assertion catches the rot.

---

## 10. Known limitations

- **Zero-copy VAAPI is unreachable**: `MpvVideoItem::ensureContext()` does not pass
  `MPV_RENDER_PARAM_WL_DISPLAY`, so mpv finds no native display. Hardware decode
  still works via `vulkan-copy`; this costs a copy, not the feature.
- **VLC video output** is broken (`STRMQT_WITH_VLC` builds only). Suspected forced
  `RV32` chroma with no fallback.
- **No gapless audio advance.** mpv gapless wants a playlist handed to the engine,
  not per-item loads.
- **Forgotten favourites is not least-recently-played.** The Music Home shelf asks
  for favourite albums sorted by `DatePlayed`, but Emby keeps no play date on an
  album, so the order is not yet the one the shelf's name promises.
- Chapter thumbnails, PiP and drag-to-reorder are unimplemented; each needs a verb or an
  id grammar the current interfaces do not have.
- **The AppImage's floor is glibc 2.39** (Ubuntu 24.04, Debian 13+, Fedora 40+),
  checked on every build against the bundled libmpv/ffmpeg closure and proven
  in CI on bare hosts (§9). **Debian 12 is not covered**; its users take the
  `.deb` or the Flatpak.
- **Compat-tier visuals are an approximation, not a copy.** `Qt5Compat.GraphicalEffects`
  gives Qt 6.4–6.7 the same shadows, masks and blurs `MultiEffect` gives 6.8+,
  but not pixel-identically (§1) — this is accepted, not a defect to chase.
- **A Fedora StrmQt rebuild is required after Fedora rebases Qt.** The `.rpm`'s
  exact-Qt `Requires` (§9) holds the Qt package back until that happens, which
  is a hold on the user's Qt updates, not a silent breakage.
- **The Secret Service backend's session is unencrypted on the session bus**
  (`plain`, §8) — the same exposure KWallet's own API already has. A
  negotiated DH-AES session is follow-up work.
- **Season badges go stale outside the loaded season.** `SeriesController`'s seasons
  model is not registered with `ItemActions` and nothing recomputes its unplayed
  counts, so marking an episode watched updates the badge only for the season
  currently on screen. A real fix needs the server's `UserData.UnplayedItemCount`
  refetched per season, not a UI hook.
- **Held volume on a gamepad is unclamped.** Held horizontal seeks in the player
  are floored at 250 ms, but the right stick's volume runs the fast ladder — at
  its 38 ms floor, 5% per step sweeps the whole range in well under a second.
  Horizontal repeat in player context is floored everywhere, including the OSD
  button row, because `GamepadManager` can see the action and the context but
  not which control has focus.
- **`Component.onDestruction` cannot read a delegate's `index`** — it has already
  been reset to -1 by then. Any cleanup keyed on index there is dead code; track
  the delegate's identity instead (`StrmRail`, `StrmGrid`, `HomePage` do).
