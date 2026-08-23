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
4. **Backends are interfaces.** `PlayerBackend` has an mpv and a VLC
   implementation; the server layer is written so a second media server can sit
   beside Emby.

### Threading and async

There are no worker threads. Everything is single-threaded Qt event-loop code:

- Network calls return `QFuture<Result<T>>` resolved on the caller's thread.
- `Result<T>` is an explicit success/error union — no exceptions cross an API
  boundary.
- libmpv runs its own threads internally and is marshalled back through
  `QMetaObject::invokeMethod`.

**Generation counters guard every async reply.** A controller increments a counter
when a new request supersedes an older one, and a reply whose generation no longer
matches is dropped. Without this a slow first reply lands after a newer one and
shows results for a query the user has already replaced. Every controller that can
be re-targeted (`Library`, `Search`, `Details`, `Series`, `Music`, `Playlist`,
`Player`) does this.

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

---

## 3. Playback

### The stream ladder

`PlaybackInfo` returns one or more **media sources** (versions of the same item),
each with its own ordered ladder of delivery methods: DirectPlay → DirectStream →
Transcode.

The ladder never mixes sources. Demoting a rung degrades *the version the user
chose*; it does not silently swap to a different cut with a different runtime.

`PlayerController` owns recovery: a watchdog for stalls, a broken-tail detector,
playback-ticket refresh, and crash resume. Position is reported to the server on a
timer and at start/stop so watch state survives.

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

### Engines

`MpvPlayer` uses the raw libmpv C API and the render API for embedding. Hardware
decoding is verified live per release. `VlcPlayer` exists as an escape hatch behind
`STRMQT_WITH_VLC` and has a known video-output defect.

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
without depending on the host's font set.

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

### Pages

`Main.qml` owns a `StackView`, the navigation history (including a forward stack,
which `StackView` does not provide), and focus memory — focus is restored to the
exact item a page was left on, not to the page as a whole.

Pages: Login · Home · Library · Details · Series · Person · Playlist · Music ·
Album · Artist · Search · Settings · Player.

`MusicPage` is four readings of one library — Albums, Artists, Songs, Playlists
— sharing one filter set and keeping a sort per tab. Its Playlists tab is the
user's *audio* playlists; the nav rail's Playlists destination is still all of
them, because a picker raised from a film has to keep offering film lists.

---

## 5. Input

`InputMap` is the single source of truth for every binding. It feeds the shortcut
sheet, the remapping UI and every page, and it is what makes a rebind take effect
everywhere at once — including on the gamepad.

**Gamepad buttons carry no hardcoded keys.** A button resolves to an *action id*,
which `InputMap` turns into whatever that action is currently bound to. The pad
then synthesises that key. Consequences: a rebind moves the pad with it, and a
gamepad hint shown in the UI is the binding that actually fires. An action with a
gamepad hint must resolve to exactly one key — asserted in tests, because a hint
without a working binding is worse than no hint.

The layout targets Xbox 360 / Xbox One, which is the PC standard and the layout
SDL's own gamepad abstraction is modelled on. Mapping is context-dependent, so one
pair of shoulders changes library while browsing and seeks during playback.

**There are three contexts, not two.** Browse, player, and *music* — the music
library, an album and an artist page. Two actions only conflict when their
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
ctest --preset dev                     # 28 suites
cmake --build <dir> --target strmqt_qmllint
STRMQT_SELFTEST=1 QT_QPA_PLATFORM=offscreen ./strmqt
```

- **Unit** — DTO mapping, settings, the stream ladder, queue behaviour, the input
  map, the stick decision table.
- **Integration** — controllers and the client against a local `QTcpServer` mock
  replaying recorded fixtures. No network, no display, no session bus.

Three things about this gate are not obvious and were each learned from a defect
that reached a user:

1. **`strmqt_qmllint` exits 0 on warnings.** The exit code is not the gate; the
   per-category grep for `is not a type` / `was not found` / `unavailable` /
   `incompatible-type` is. `missing-property` catches invented properties and is
   worth reading too.
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

The Emby access token goes to KWallet via `kwalletd6` over D-Bus, with a plaintext
fallback when the wallet is unreachable. No credential is ever written to the repo,
and TLS certificate errors are fatal in release builds.

---

## 9. Packaging

Three artifacts, all built from a clean checkout:

- **Arch package** — `install()` rules put two binaries, a desktop entry, AppStream
  metainfo and six icon sizes into `/usr`. Nothing else is installed: the QML module
  is compiled into the executable, so there is no qmldir or `.qml` to ship.
- **Flatpak** — `org.kde.Platform` 6.11 plus libmpv, libplacebo, libass and uchardet
  built from source. FFmpeg comes from the runtime.
- **AppImage** — Qt's own deploy script plus patchelf. linuxdeploy is deliberately
  *not* used: its excludelist omits libva, libvulkan and libpulse, which are exactly
  the libraries whose bundling breaks hardware decode.

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
- MPRIS Next/Previous, chapter thumbnails, PiP and drag-to-reorder are unimplemented;
  each needs a verb or an id grammar the current interfaces do not have.
- The AppImage's glibc floor is set by the bundled FFmpeg, not by this code. It runs
  on current Arch and little else.
- **Season badges go stale outside the loaded season.** `SeriesController`'s seasons
  model is not registered with `ItemActions` and nothing recomputes its unplayed
  counts, so marking an episode watched updates the badge only for the season
  currently on screen. A real fix needs the server's `UserData.UnplayedItemCount`
  refetched per season, not a UI hook.
- **Held volume on a gamepad is unclamped.** Held horizontal seeks are floored at
  250 ms, but Up/Down still run the fast ladder at 5% per step. Horizontal repeat
  in player context is floored everywhere, including the OSD button row, because
  `GamepadManager` can see the action and the context but not which control has
  focus.
- **`Component.onDestruction` cannot read a delegate's `index`** — it has already
  been reset to -1 by then. Any cleanup keyed on index there is dead code; track
  the delegate's identity instead (`StrmRail`, `StrmGrid`, `HomePage` do).
