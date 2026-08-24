# StrmQt top-to-bottom code audit

Date: 2026-08-23

Reviewed revision: `15c1d9b` (`fix(music,input): review corrections to multi-select and the music context`)

Scope: application architecture, server/controller boundaries, playback lifecycle, audio/video transitions, shell and input state, QML controls and layouts, platform integration, security, tests, packaging, documentation, and the completed-state claim for `MUSIC.md`.

This was a read-only review of the product code. The only file created by the audit is this report. The pre-existing untracked `MUSIC.md` was treated as the design and acceptance record for the last major refactor and was not changed.

## Executive summary

The music refactor is substantial and, in most of its core scope, well executed. The artwork model, music query axes, Songs and audio-playlist surfaces, shared track controls, music-shaped mini/full player, sampled wash, sleeve transition, MPRIS metadata, ReplayGain, instant mix, multi-select, and music input context are present and tested. The UI also has unusually disciplined focus/hover separation, single-tab-stop composites, controlled selection components, and generation guards in many content controllers.

The main defects are not primarily visual. They come from four state concepts being conflated:

1. the item the user most recently requested;
2. the queue's current cursor;
3. the item and load the backend is actually presenting; and
4. the player surface the user is currently interacting with.

`PlayerController::active` and `isAudio` are asked to stand in for several of those concepts. During a delayed or failed audio/video handoff, the queue and QML can publish the incoming item while the backend is still producing events, audio, frames, duration, tracks, and errors for the outgoing item. At the most severe edge, failure to resolve the next item marks the controller inactive and removes the player UI without stopping the old backend, leaving ghost playback that the normal Stop command can no longer stop.

The review found **2 critical, 7 high, 12 medium, and 2 low-priority findings**. The recommended first change is not another local boolean or QML guard. It is an explicit playback lifecycle with separate pending and committed identities, plus load and authenticated-session epochs carried through asynchronous work.

## Validation performed

- `cmake --build --preset dev -j2`: passed; the existing build was current.
- `ctest --preset dev --output-on-failure`: **29/29 passed** in 23.38 seconds when local loopback binding was permitted. An initial restricted-sandbox run caused 11 mock-server suites to fail at TCP bind; that was an environment failure, not a project failure.
- `cmake --build build/dev --target strmqt_qmllint`: exited successfully, but emitted roughly 2,700 lines of warnings. The fatal checks pass; the warning volume is itself addressed in AUD-17.
- `STRMQT_SELFTEST=1 QT_QPA_PLATFORM=offscreen .../strmqt`: exited successfully and constructed all 13 pages. It also exposed an unfinished `kscreen-doctor` child process at shutdown; see AUD-18.
- No live Emby server, physical gamepad, screen reader, VLC runtime, or sustained real playback session was available. Findings depending on those environments are clearly distinguished from directly provable state errors.

## Remediation record

Repairs completed on 2026-08-23. The findings below are preserved as the review record;
this table records their disposition in the repaired tree.

| Findings | Disposition | Repair evidence |
|---|---|---|
| AUD-01, 02, 05, 06 | Repaired | Load epochs reject stale backend events; replacement quiesces the old engine; pending presentation is cleared coherently; mpv, VLC, and the fake share reset semantics; termination reasons preserve failure resume data and establish Idle invariants (`93af1c1`, `e3f9c83`). |
| AUD-03, 04 | Repaired | Playback-intent and authenticated-session epochs enforce last-intent-wins and reject/abort old work. Logout/server changes stop playback, clear queue/models/MPRIS/caches, and session-scope retained search/playback/library data (`7511cfd`, `a442a37`, `955075f`). |
| AUD-07, 08, 09 | Repaired | The visible shell publishes `login | browse | music | player | overlay`; gamepad state is per SDL device; live/power policy composes foreground and committed media kind; UserData membership reconciles; all player entry uses focus-safe `showPlayer()` (`d9cdb95`). |
| AUD-10, 11, 12 | Repaired | Recovery is serialized by incident token, observable track/settings state follows backend readback, A-B and QML player transients reset at their owner boundaries, and favourite overrides reset on session changes (`7cee217`, `3719ef8`). |
| AUD-13 | Repaired | `MpvVideoItem` transfers render state through `synchronize()`, avoids GUI-object dereferences from `render()`, and tears callbacks down explicitly; an offscreen lifecycle test covers detach-before-owner-destruction (`54aa54c`, `91128bb`). |
| AUD-14, 15 | Repaired | Base controls and important player rows publish accessible roles, names, states, actions, and value interfaces. Player surfaces have compact width/height breakpoints and overflow behavior, with a 960x600 minimum window and an executable accessibility test (`de49863`, `e9f2f19`, `459ce6c`, `4b3b92d`). |
| AUD-16 | Repaired | Architecture now states the honest Emby-specific server seam; now-playing presentation is shared and structured; token fallback is session-only; URL validation and explicit insecure-HTTP policy are enforced; image byte/pixel ceilings and auth cache partitioning are tested (`a442a37`, `47e3a88`, `0c48fd6`). |
| AUD-17 | Repaired | Transition tests now cover stale loads, failure/stop boundaries, recovery bursts, stale user intents, session retirement, query membership, multi-device input, accessibility, image ceilings, and renderer teardown. CI compares the complete normalized qmllint warning set to a reviewed baseline (`93af1c1`, `7511cfd`, `91128bb`, `d9cdb95`, `459ce6c`, `0c48fd6`). |
| AUD-18, 19 | Repaired | HDR probing is idempotent and bounded; release/capability docs agree; informative small text has stronger contrast and reduced motion is a persisted policy (`1b053ee`, `2b9b49e`, `396e613`). |
| AUD-20 | Repaired | Manual releases validate and check out the requested immutable tag/commit, reject moved tags, and use a pinned, checksummed AppImage tool (`0511164`, `fc30c1e`). |
| AUD-21, 22, 23 | Repaired | Playlists page to completion; MPRIS capabilities/volume/registration rollback match implementation; CMake requests direct Qt modules and the Flatpak mirror uses a valid archive/hash (`5c9d138`, `1f324c4`, `0511164`). |

Final integration validation after all repairs:

- warnings-as-errors configure and full build: passed;
- `ctest`: **33/33 passed**;
- offscreen page construction: **13/13 passed**;
- focused QML accessibility interaction test: passed;
- qmllint: **859 normalized existing warnings**, exactly matching the reviewed baseline;
- release workflow and Flatpak YAML parsing/checksum validation: passed.

Manual coverage still requires the corresponding hardware/services: sustained real
mpv/VLC playback, a physical multi-controller setup, a live KWallet migration/denial,
a screen reader across complete pages, and an actual GitHub/Flatpak release run.

## Priority map

| ID | Severity | Area | Finding |
|---|---|---|---|
| AUD-01 | Critical | Playback lifecycle | An outgoing backend load remains authoritative during replacement and can become unstoppable ghost playback on failure |
| AUD-02 | High | Audio/video UI | Queue intent and presented media are published as a split-brain snapshot |
| AUD-03 | High | User intent | Slow Play All, shuffle, or instant-mix requests can overwrite a newer playback choice |
| AUD-04 | Critical | Session boundary | Logout/server changes do not invalidate old asynchronous work, allowing cross-user/server state and writes |
| AUD-05 | High | Backend contract | Per-load frame, timeline, buffering, track, and statistics state is not reset consistently |
| AUD-06 | High | Failure semantics | Failure termination erases crash-resume state and does not establish clean idle invariants |
| AUD-07 | High | Input/shell state | Gamepad and global shortcuts use playback activity instead of the visible interaction surface |
| AUD-08 | High | Live state | Suspension causes overwrite each other and UserData invalidation is not reconciled into query membership |
| AUD-09 | Medium | Focus | Automatic player entry bypasses the shell's focus-transfer contract |
| AUD-10 | Medium | Recovery | Concurrent recovery requests and watchdog actions can start competing loads |
| AUD-11 | Medium | Tracks/settings | Asynchronous backend readback is not the source of truth for track and playback-setting state |
| AUD-12 | Medium | Transient UI state | OSD panels, stats, A-B loop points, and favorite overrides outlive their valid item/session |
| AUD-13 | Medium | Rendering | The mpv framebuffer renderer crosses Qt Quick's synchronization boundary with raw GUI-thread objects |
| AUD-14 | Medium | Accessibility | Most custom controls do not publish roles, names, values, or accessible actions |
| AUD-15 | Medium | Responsive design | Player layouts have no declared minimum geometry or compact/overflow mode |
| AUD-16 | Medium | Architecture/security | The server abstraction is nominal, presentation logic is duplicated, and plaintext token fallback is silent |
| AUD-17 | Medium | Quality gates | Tests miss state-transition diagonals and QML lint warning volume hides regressions |
| AUD-18 | Low | Platform lifecycle | HDR probing has no timeout and leaves a process running during the self-test |
| AUD-19 | Low | Documentation/design polish | Release documentation and `MUSIC.md` completion status have drifted; contrast and motion need accessibility policy |
| AUD-20 | High | Release automation | Manual release dispatch ignores its required tag when selecting the source revision |
| AUD-21 | Medium | Data completeness | Playlists are silently truncated to 500 members |
| AUD-22 | Medium | Desktop integration | MPRIS advertises capabilities and volume behavior it does not implement accurately |
| AUD-23 | Medium | Build portability | CMake links Qt modules it never requests from `find_package` |

## Findings

### AUD-01 — Outgoing backend events survive item replacement and failed handoff can create ghost playback

**Severity: Critical**

`PlayerController::startItem()` calls `closeCurrentSession()`, but that function only stops progress reporting and sends a stop report; it does not stop or quiesce the engine. The controller then changes `m_itemId`, queue-facing metadata, generation, busy/active state, and starts an asynchronous PlaybackInfo request while the prior backend load is still alive (`src/app/controllers/PlayerController.cpp:315-400`, `1272-1282`).

The generation protects the HTTP continuation, not backend events. `hasTicket()` does not verify that `m_ticketItemId == m_itemId` (`src/app/controllers/PlayerController.h:334-358`), and state, error, end, position, and duration callbacks do not carry a load identity (`src/app/controllers/PlayerController.cpp:95-106`, `536-554`, `735-798`). Neither `PlayerBackend` nor the mpv/VLC event surfaces attach a controller epoch to their signals (`src/playback/PlayerBackend.h:169-189`).

This produces several concrete failure modes:

- a late End event from audio A can advance the queue past pending video B;
- a late Error can recover or demote using stale ticket A while controller identity says B;
- a late Playing event can report B started while A is still on screen;
- late position/duration events can corrupt B's progress and resume state;
- if B's PlaybackInfo or playable-source selection fails, `finishSession()` marks the controller inactive and Main pops the player, but the old backend is never stopped (`PlayerController.cpp:383-387`, `511-517`, `1284-1293`; `src/ui/Main.qml:993-1002`). `stop()` then returns early because `active == false` (`PlayerController.cpp:1025-1033`), so the ghost playback cannot be recovered through the normal UI.

**Recommendation:** make load identity first-class. A robust sequence is:

```text
user intent N
    -> resolve pending item N
    -> stop/quiesce presented load N-1
    -> load N with epoch N
    -> commit presented snapshot N only on matching backend-ready event
    -> ignore every backend signal whose epoch is not N
```

The epoch can be passed to `PlayerBackend::load()` and echoed by every state/error/end/timeline signal, or represented by a per-load session object whose destruction disconnects old events. Immediately invalidate the old ticket/source identity when replacement begins. Independently enforce the invariant `!active => backend is Idle` so every termination path is safe even before the fuller refactor lands.

**Required tests:** delayed B PlaybackInfo while injecting late A Playing/Error/End/position/duration events; raw URL to server item; failed PlaybackInfo and no-playable-source handoffs; Stop during resolution; assertion that inactive always means an idle backend.

### AUD-02 — Audio/video mode switches publish a non-atomic, split-brain snapshot

**Severity: High**

The queue cursor moves before `startItem()` resolves the incoming ticket (`PlayerController.cpp:912-947`). `isAudio` recomputes from that new queue entry immediately (`PlayerController.cpp:72-93`, `1350-1373`), and `PlayerPage` swaps between `NowPlayingPanel` and the video plane as soon as it changes (`src/ui/pages/PlayerPage.qml:70-92`, `137-149`, `199-235`).

At the same time, duration, position, selected source, streams, tracks, buffering, and render output still come from the outgoing backend or retained ticket (`PlayerController.cpp:227-295`, `345-400`). The loading overlay is informational; item-scoped controls remain enabled. A slow video-to-audio handoff can therefore show the new sleeve/title with the prior movie's two-hour position, source and track list while the movie frame or audio continues. The reverse direction can expose an old audio duration and technical readout on a video surface. A seek or source selection during this window acts on the outgoing engine while controller identity already names the incoming item.

The existing queue test even documents part of this intermediate split by asserting the queue cursor changes while controller title/duration remain stale (`tests/integration/tst_queue_playback.cpp:541-584`). That makes the behavior intentional at one layer but unsafe for consumers that need one coherent now-playing record.

**Recommendation:** publish a controller-owned immutable `PresentedMedia` snapshot containing item id, generation, media kind, metadata, timeline, ticket/source, tracks, and backend state. Keep a separate `PendingMedia`/intent while resolving. QML and MPRIS should bind to the committed snapshot, not directly combine queue intent with backend observation. If retaining the old media during prefetch is desired for continuity, keep its complete UI and controls coherent until the new load commits. If it is not, stop the old engine and clear/notify all per-item fields before showing the new loading state.

**Required tests:** audio-to-video and video-to-audio under delayed PlaybackInfo, from both full player and minimized states; assert art/title/media kind/timeline/source/tracks/MPRIS position all belong to the same item at every observation point.

### AUD-03 — Slow asynchronous queue builders can override newer user intent

**Severity: High**

Playing a leaf item reaches `PlayerController::playItem()` immediately, but container Play All, shuffle, collection, series shuffle, and instant mix first perform asynchronous server requests (`src/app/ItemActions.cpp:200-233`, `372-430`, `484-514`). Their continuations have no request generation or cancellation and unconditionally replace the queue when they complete. `ItemActions` has generations for optimistic user-state mutations but no playback-intent token (`src/app/ItemActions.h:154-189`).

Reproduction: start a slow Play All on an audio artist, immediately start a movie, and wait for the artist query. The older request completes last and switches the queue and UI back to audio. The reverse order produces the inverse switch. This is a direct last-completion-wins state reversal and can look like the player spontaneously choosing the wrong surface.

**Recommendation:** increment a playback-intent generation for every interrupting playback command, including direct leaf play. Capture it in every asynchronous queue builder and ignore stale completions. Centralizing the command epoch in `PlayerController` would keep remote, MPRIS, item-action, and QML commands under one last-user-intent-wins policy.

**Required tests:** delay Items, InstantMix, collection, and Episodes responses; issue an intervening leaf or second-container play; assert the newest selection remains current and the stale completion emits no queue replacement/toast.

### AUD-04 — Authentication is not an isolation boundary for asynchronous work or playback

**Severity: Critical**

`SessionController::login()` has no request epoch or cancellation. Its late completion always persists the returned user/token and marks the app authenticated (`src/app/controllers/SessionController.cpp:75-102`). `setServerUrl()` retargets the one shared client, while `logout()` only overwrites credentials/authentication (`SessionController.cpp:30-35`, `105-110`). `EmbyClient::setSession()` invalidates no outstanding requests (`src/server/emby/EmbyClient.cpp:69-73`). A login reply can therefore re-authenticate after Logout, and controller requests issued for one server/user can populate models after the application has moved to another.

Some chained operations can cross the server boundary more seriously. `renameItem()` fetches an object, then its late callback invokes a POST through the client's *current* base URL/token (`EmbyClient.cpp:803-845`). If the shared client was retargeted in between and item ids collide, an old-server response can cause a write to the newly selected server. PlaybackInfo parsing also combines a response with current base URL/session fields (`EmbyClient.cpp:694-721`) rather than an immutable request context.

Separately, logout does not stop or clear the player. Main clears its stack and pushes Login (`src/ui/Main.qml:1009-1019`), while MiniPlayer is shown from `PlayerCtl.active && !playerOnTop` and has no authentication guard (`src/ui/shell/MiniPlayer.qml:88-94`). Signing out during minimized playback can continue media, render the prior user's metadata and controls over Login, keep MPRIS active, and cause progress/recovery behavior after credentials were cleared.

Other application-lifetime data is not consistently reset or namespaced. Home's genre-rail pipeline is one-shot and its nested requests lack retarget generations (`src/app/controllers/HomeController.cpp:283-350`); a late old-session reply can fill the new session and prevent a correct refetch. Recent search history uses one process-wide QSettings key and is neither cleared nor namespaced by server/user (`src/app/controllers/SearchController.cpp:10-15`, `43-50`, `128-147`). Library display state, remembered source/track choices, and crash-resume are also device-global (`src/core/Settings.cpp:351-425`, `477-502`), so colliding server item ids can apply one account's subtitle/source preference to another. The image/network cache is shared and image URLs do not encode the authenticated user; no auth-change hook partitions or clears it (`src/app/EmbyImageProvider.cpp:81-100`).

**Recommendation:** introduce a monotonically increasing authenticated-session epoch that changes on login attempt, logout, user switch, and server URL change. Capture it in every controller/client operation and discard replies from older epochs. Abort network replies where possible and carry immutable base URL/token/device context through chained operations. Make session teardown an application-level transaction: stop and clear playback/queue, cancel pending playback intents and requests, reset all user-scoped models/caches/MPRIS/transient UI, then clear authentication. Namespace retained preferences such as search history by stable server+user identity or clear them under an explicit privacy policy. Defensively gate player chrome and shortcuts on authentication, but do not rely on that visual guard as the actual stop.

**Required tests:** logout/server change during login, rename fetch-to-POST, model loads, genre rails, playback resolution and recovery; switch user while a queue builder is in flight; assert the late login cannot re-authenticate, no cross-server write occurs, backend/queue/models/MPRIS are cleared, no old metadata/searches appear, and no post-logout progress request is sent.

### AUD-05 — Playback backends do not share a complete per-load reset contract

**Severity: High**

mpv `load()` resets only part of its cache and does not consistently reset/notify position, duration, tracks, buffering, decoder, and statistics (`src/playback/mpv/MpvPlayer.cpp:161-181`). Its `stop()` also clears only a subset (`MpvPlayer.cpp:191-211`). VLC `load()` retains duration, buffering, front/back frame, and `m_sawFirstFrame`; `stop()` leaves several observable values and does not notify all resets (`src/playback/vlc/VlcPlayer.cpp:190-228`).

Because PlayerPage makes a video plane visible based on queue-derived media kind before the new frame is ready, VLC video A -> audio B -> video C can expose A's retained frame until C decodes. Stale buffering can keep “Buffering” visible or change watchdog behavior on the next item. The fake backend is synchronous and resets differently, so current controller tests cannot expose this contract drift.

**Recommendation:** define an explicit observable reset contract for `load()` and `stop()` covering state, position, duration, buffered amount, buffering flag, tracks/selections, frame, decoder/statistics, and errors. Every changed value must emit its notification. Gate video-plane visibility on a matching video load reaching a ready/first-frame state rather than item type alone.

**Required tests:** backend contract suite applied to mpv and VLC adapters where feasible; VLC retained-frame regression; loading a new item after buffering; stop/load notification assertions.

### AUD-06 — Failure termination erases recovery data and leaves dirty controller fields

**Severity: High**

`finishSession()` unconditionally clears crash-resume data, even when reached from PlaybackInfo failure, no playable source, fatal playback failure, or next-episode lookup failure (`PlayerController.cpp:383-387`, `511-517`, `735-769`, `850-859`, `1284-1293`). A temporary 5xx while accepting a crash resume can therefore erase the only saved recovery point. A fatal midstream error also discards the periodically persisted position.

The same method stops timers and marks inactive, but does not consistently clear `busy`, `m_started`, `m_reporting`, ticket/source/presentation state, or the backend itself. Stop during ticket loading can leave `busy == true` while inactive. This makes `finishSession()` a notification shortcut rather than a state invariant. There is also an asymmetric durability edge: writing crash-resume explicitly calls `QSettings::sync()`, while clearing it does not (`src/core/Settings.cpp:477-502`), so sudden process/power loss immediately after a clean stop can leave a stale prompt for the next launch.

**Recommendation:** model termination reason explicitly: clean EOF, explicit user stop, replacement, authentication teardown, and failure. Preserve or update resume data on failure; clear it only after clean/explicit policy says to. Route all exits through one idempotent transition that establishes a documented Idle snapshot and backend state.

**Required tests:** failed crash-resume retention, fatal midstream retention, explicit stop clearing policy, stop while resolving/loading, and complete idle-field assertions.

### AUD-07 — Input context follows playback activity instead of the visible player surface

**Severity: High**

Main intentionally supports minimizing the player while playback continues (`src/ui/Main.qml:129-146`). The mini player's own contract says it exists while the player page is not on top (`src/ui/shell/MiniPlayer.qml:88-94`). Despite that, Application changes gamepad context solely from `PlayerController::active` (`src/app/Application.cpp:149-159`).

In player context, A toggles pause instead of selecting a focused card, shoulders seek instead of cycling libraries, Start toggles the invisible OSD, Back/View stops instead of browsing back, and R3 has no action (`src/input/GamepadManager.cpp:127-170`). The input catalogue explicitly places `player.focusBar` in browse context because the bar exists when the player page is not on top (`src/input/InputMap.cpp:463-478`), so the wiring contradicts its own contract.

Keyboard gating has the same mismatch: Search and Settings are disabled with `!PlayerCtl.active`, whereas other browse shortcuts correctly use `!root.playerOnTop` (`src/ui/Main.qml:782-795`, `804-890`). After minimizing audio or video, the browse page is visible and pointer-operable, but `/` and F2 do nothing.

Multi-controller state is also global. Held buttons and dominant axes are not keyed by SDL device, and button/axis handling ignores the event's `which` field (`src/input/GamepadManager.cpp:173-230`; `src/input/GamepadManager.h:107-122`). One of two controllers can overwrite the other's hold; disconnecting either releases both controllers' synthetic keys.

**Recommendation:** introduce one shell-owned interaction context derived from visible surface and modal state, e.g. `login | browse | music | player | overlay`. QML should publish that context to the input dispatcher; playback lifecycle must not infer it. Gate all global shortcuts through the same context service.

**Required tests:** active/minimized audio and video with keyboard and gamepad mappings; player on top; overlay open; mini-player focus; context changes while a button is held.

### AUD-08 — Live-update policy loses suspension causes and does not reconcile UserData invalidation

**Severity: High**

Live updates are suspended by two separate callbacks writing one boolean. `activeChanged` writes `player.active`; `applicationStateChanged` writes `!foreground || player.active` (`src/app/Application.cpp:147-168`). Event ordering therefore matters. If playback stops while the app remains in the background, `activeChanged` can unsuspend polling and call `refreshNow()` even though the window is still backgrounded.

The comment and architecture rationale are about avoiding contention with the video decoder, but all active audio sessions suspend updates too. Application does not listen for `isAudioChanged`, so an audio/video queue boundary that keeps `active == true` never re-evaluates suspension. Screen inhibition similarly uses only active/paused, describes every item as “Playing video,” and does not react to `isAudioChanged` (`Application.cpp:195-208`). Hours of music can therefore prevent display blanking, while a mid-session media-kind transition is ignored.

The content side of live updates is also incomplete. `LiveUpdateService` publishes played, favorite, positionTicks, and playCount patches and a debounced `userDataInvalidated` signal (`src/app/controllers/LiveUpdateService.cpp:243-264`, `319-329`). Production controllers do not consume `userDataInvalidated`; Home and Library patch only played/favorite in place. That cannot remove an item from Favorites, Continue Watching, Next Up, or a filtered grid when membership changes, and it does not keep resume position/play count coherent. Polling fallback emits both library and UserData invalidations, but a paged Library grid deliberately avoids a full reconciliation, so remote changes can remain stale indefinitely without WebSocket delivery.

**Recommendation:** use one recomputation function for each suspension/power policy, called from every input signal. For polling, compute from application foreground plus `active && !isAudio` if decoder contention is the real reason. For power, define separate display and sleep policies. Separately, make UserData reconciliation query-aware: patch all advertised fields, remove/add rows when filter membership changes, and schedule targeted reloads where local membership cannot be decided. Test WebSocket and polling paths against first and later pages.

### AUD-09 — Player entry routes bypass the shell's focus-transfer invariant

**Severity: Medium**

`Main.pushPage()` documents and implements deferred focus transfer after a StackView push (`src/ui/Main.qml:102-127`). Automatic playback pushes and mini-player expansion call `stack.push(playerComponent)` directly without `Qt.callLater(focusCurrentPage)` (`Main.qml:650-655`, `984-991`).

The triggering card or mini child can retain active focus underneath the new page. PlayerPage's key routing then may not receive seek, volume, stop, or structural navigation until another pointer/focus action. A hidden page may retain a visible focus-related state.

**Recommendation:** create one `showPlayer()` shell verb that remembers focus, pushes or raises the player, starts the sleeve animation where appropriate, and transfers focus after construction. Use it for activeChanged, mini expansion, resume, remote activation, and any future entry route. Gate page-owned shortcuts on the page being current/visible as an additional invariant.

### AUD-10 — Recovery and watchdog paths can create concurrent loads

**Severity: Medium**

Every backend error while started can schedule another delayed ticket refresh; there is no recovery-in-flight guard or recovery epoch (`PlayerController.cpp:623-662`, `760-763`). Multiple callbacks share the same item generation, so all remain valid and can replace the ticket and call `startAttempt()`. The watchdog is not clearly suspended during recovery and can reload or demote in parallel (`PlayerController.cpp:562-620`).

**Recommendation:** add an explicit Recovering state and one attempt token per incident. Stop progress/watchdog actions during backoff, invalidate older recovery callbacks whenever a new attempt begins, and resume only on the matching backend-ready event. Test error bursts and watchdog ticks during a delayed recovery.

### AUD-11 — Track and playback-setting state is emitted before asynchronous backend readback

**Severity: Medium**

Controller track selection/cycling immediately calls `rememberCurrentTracks()` (`PlayerController.cpp:1071-1097`). The real mpv backend intentionally does not optimistically change its selected id; observers refresh it later (`src/playback/mpv/MpvPlayer.cpp:262-283`, `580-610`). Persistence can therefore store the track being left rather than the new selection. The synchronous fake masks this behavior.

mpv emits `PlayerBackend::trackChanged`, and PlayerOsd listens to `PlayerCtl.trackChanged`, but PlayerController never forwards that signal (`MpvPlayer.cpp:608`; `src/ui/player/PlayerOsd.qml:588-594`; controller constructor connections at `PlayerController.cpp:34-149`). The expected track-change toast does not reach QML.

Playback speed and audio/subtitle delay have the same source-of-truth issue: controller setters emit immediately, but backend-originated changes are not forwarded (`PlayerController.cpp:404-426`; `src/ui/player/PlaybackSettingsPanel.qml:49-60`). A per-file reset, mpv script, or rejected setter can leave QML bindings stale.

**Recommendation:** treat setters as requests and drive observable controller state from backend readback notifications. Persist track choices only after the matching selection is confirmed, while distinguishing restoration from user intent. Add deferred-selection and signal-forwarding tests.

### AUD-12 — Transient item and session state survives beyond its ownership boundary

**Severity: Medium**

Several state values have longer lifetimes than the media or session they describe:

- `PlayerOsd.panelKey`, `panelTab`, and `statsVisible` live for the PlayerPage lifetime and are not reset on video-to-video item replacement (`src/ui/player/PlayerOsd.qml:28-48`). Track and chapter panels can expose outgoing rows during the next ticket delay.
- On audio/video change, PlayerPage closes a panel but does not clear stats (`src/ui/pages/PlayerPage.qml:82-92`). Ctrl+I remains active in audio mode and toggles invisible state; that overlay can unexpectedly appear when the queue returns to video (`PlayerPage.qml:385-389`; `PlayerOsd.qml:168-183`).
- A-B loop fields are initialized only once and are changed only by mark/clear (`src/app/controllers/PlayerController.h:368-369`; `PlayerController.cpp:1106-1138`). New items, raw URLs, Stop, and session finish do not clear them. An A point set on item A becomes the A point when marking B on item B; a completed A-B pair makes the first press on B clear instead of mark.
- `MiniPlayer.favoriteOverrides` is application-lifetime and never reset on server/user changes (`src/ui/shell/MiniPlayer.qml:282-309`), so an item id reused or refreshed under another session can inherit stale local presentation state.

**Recommendation:** classify transient state by owner: load, item, player page, playback session, authenticated server/user, or application. Reset it at that owner's boundary or key it by the full owner identity. Video-only shortcuts should be disabled in audio mode unless they have a visible audio equivalent. Queue panels may deliberately persist; item-specific track/chapter/source panels should close or be keyed by playback generation.

### AUD-13 — MpvVideoItem does not use Qt Quick's renderer synchronization boundary safely

**Severity: Medium**

The render-thread `MpvRenderer` retains and reads a raw `MpvVideoItem *` and reaches player/window state through it (`src/playback/mpv/MpvVideoItem.cpp:29-32`, `56-73`, `94-95`). `setPlayerObject()` mutates `m_player` on the GUI thread (`MpvVideoItem.cpp:114-122`), and the update callback also queues through the raw item pointer. No `QQuickFramebufferObject::Renderer::synchronize()` step copies immutable render data or establishes lifetime rules.

This is a data-race/use-after-free risk during player reassignment, scene teardown, or rapid PlayerPage construction/destruction. It is especially relevant once audio/video transitions and render-plane lifecycle are stressed.

**Recommendation:** implement `synchronize()` to transfer a lifetime-safe render handle/state snapshot, avoid dereferencing GUI-thread QQuickItem/window/player members from `render()`, and clear mpv callbacks before either side is destroyed. Exercise repeated page create/destroy and media-kind switching under the threaded render loop and TSAN where possible.

### AUD-14 — Custom controls are mostly absent from the accessibility contract

**Severity: Medium**

The reusable controls are plain `Item`s with custom pointer/key handling but generally no `Accessible.role`, `name`, state, value, or action. Representative examples are `StrmButton.qml:13-22,55-77,181-204`, `StrmIconButton.qml:11-23,38-57,122-143`, and `StrmSlider.qml:18-36,89-91,194-250`. Only a few settings-specific elements currently publish accessibility semantics (`src/ui/settings/SettingsSections.qml:186-189`, `461-463`). Mini-player links and custom player list rows have the same issue.

Keyboard accessibility is thoughtfully implemented, but a screen reader cannot infer that these Items are buttons/sliders, announce disabled/busy/checked/current state, expose a useful name, or invoke standard press/increment/decrement actions.

**Recommendation:** add semantics once in the base controls: role, accessible name/description, enabled/busy/pressed/checked/value/min/max state, press action, and slider increment/decrement actions. Require names at icon-only call sites. Add accessibility-tree smoke tests for Login, a browse page, the mini player, and both audio/video player surfaces.

### AUD-15 — Player layouts do not define a supported compact geometry

**Severity: Medium**

Main declares a default window size but no minimum dimensions (`src/ui/Main.qml:21-28`). The video OSD places two large fixed button groups on opposing edges without an overflow/adaptive variant (`src/ui/player/OsdButtonRow.qml:91-411`). NowPlaying switches mainly on width and allocates a fixed proportion of height to the hero while its control column has substantial fixed content (`src/ui/player/NowPlayingPanel.qml:336-647`). MiniPlayer similarly composes fixed left/center/right clusters (`src/ui/shell/MiniPlayer.qml:598-607`, `785-992`).

Narrow windows, short landscape windows, large font scaling, and TV overscan can make groups collide or leave negative/insufficient art space.

**Recommendation:** explicitly declare the minimum supported geometry and add compact breakpoints based on both available width and height. Move secondary OSD actions into overflow, use Layouts with priority/maximum widths, and provide a scrollable/compact NowPlaying control arrangement. Test at minimum size, 200% UI scale, long translated strings, and 720p TV-safe bounds.

### AUD-16 — Architectural seams and credential/presentation ownership do not match the stated design

**Severity: Medium**

Three design debts stand out:

1. `MediaServerBackend` is an empty nominal interface (`src/server/MediaServerBackend.h:7-20`), while controllers and actions depend directly on `emby::EmbyClient *` throughout. The playback backend seam is real; the promised drop-in server-backend boundary is not. Adding Jellyfin/Plex would currently touch nearly every controller.
2. MiniPlayer and NowPlayingPanel independently derive art, title/subtitle, artist/album, queue/favorite state, time formatting, and technical labels from queue/controller fields. This duplicated QML presentation logic increases the chance that audio/video handoffs or metadata fixes land on one surface only. Video episode context still parses a display title using the literal separator `" — "` rather than consuming structured now-playing metadata (`src/ui/shell/MiniPlayer.qml:195-259`; similar derivation in `src/ui/player/PlayerOsd.qml`).
3. When KWallet is unavailable or access is rejected, the token silently falls back to plaintext `secrets.ini` using QSettings (`src/platform/SecretsStore.cpp:47-77`, `84-109`, `112-139`). The warning is only in logs; the user is not asked to accept plaintext storage, offered session-only storage, or shown the current security mode.

The plaintext fallback is also contradicted by AppStream's claim that credentials are “never in plain text on disk” (`packaging/appstream/ca.mikesdev.StrmQt.metainfo.xml:20-26`) and Login's claim that “credentials are never stored” (`src/ui/pages/LoginPage.qml:173-178`). The password is not stored, but the access token is a credential and may be stored in plaintext. The fallback does not explicitly enforce owner-only permissions, fallback deletion reports success without checking sync/status, and Logout ignores deletion failure. Server URL validation checks essentially only for empty input; unsupported schemes, embedded credentials, missing hosts, and non-loopback plaintext HTTP are not given an explicit policy (`src/app/controllers/SessionController.cpp:75-83`; `src/server/emby/EmbyWebSocket.cpp:86-100`). Image fetch similarly reads and decodes complete server replies without a response-byte or decoded-pixel ceiling (`src/app/EmbyImageProvider.cpp:102-180`), exposing the client to memory exhaustion from a compromised server or decompression bomb.

**Recommendation:** either complete a server-service interface covering the operations controllers actually use, or narrow the documentation's modular-backend claim. Introduce a shared, structured NowPlaying presentation model while keeping layout-specific QML separate. Make credential storage mode visible and consensual, provide a session-only choice, enforce `0600` fallback storage and verified deletion, and migrate/delete plaintext when a wallet later becomes available. Validate http/https URLs with a host, reject embedded credentials/unsupported schemes, require an explicit warning for non-loopback HTTP, and impose encoded-byte and decoded-pixel limits on images before allocation.

### AUD-17 — Existing tests do not exercise the transition matrix, and lint noise weakens the gate

**Severity: Medium**

The 29 suites provide good coverage of DTO mapping, query construction, queue semantics, controllers, MPRIS metadata, input-map catalogues, and happy-path playback. They use a synchronous fake backend for controller tests and include no Qt Quick Test suite that drives Main, PlayerPage, MiniPlayer, or actual OSD focus/state transitions (`tests/CMakeLists.txt:55-130`). Page self-test construction proves components instantiate, not that state and focus remain coherent.

The missing cases closely match the defects above:

- delayed audio-to-video and video-to-audio handoff;
- stale backend events after replacement or Stop;
- failed next-item ghost playback;
- competing asynchronous playback intents;
- active/top/minimized/overlay input contexts;
- logout during each playback lifecycle state;
- OSD/A-B-loop reset boundaries;
- backend reset and notification contracts;
- asynchronous track selection and recovery bursts;
- VLC retained frames and mpv render-thread teardown.

The QML lint target exits zero but produces a very large warning stream, dominated by unqualified accesses plus accepted dynamic-property warnings such as player-page sleeve geometry in Main. A gate that tolerates thousands of warnings makes a new warning hard to notice even when it belongs to a documented “worth reading” category.

**Recommendation:** add a focused transition matrix rather than broad screenshot tests. Build a controllable asynchronous backend fake that attaches load ids and can inject delayed events. Add Qt Quick interaction tests for shell focus and visibility. For QML lint, remove/suppress known warnings intentionally, introduce typed page contracts where possible, record a warning baseline, and fail on new warnings even if upstream qmllint still exits zero.

### AUD-18 — HDR probing has no bounded lifetime

**Severity: Low**

`HdrSupport::probe()` starts `kscreen-doctor --json` and waits only for finished/error; it has no timeout or already-probing guard (`src/platform/HdrSupport.cpp:14-51`). The offscreen page self-test exited with `QProcess: Destroyed while process ("kscreen-doctor") is still running.` A hung utility can keep the probe unresolved for the entire application lifetime, and repeated calls would create multiple processes.

**Recommendation:** make the probe idempotent, add a short timeout, terminate/kill and delete safely, and either skip platform probes in the construction self-test or have self-test teardown await/cancel them.

### AUD-19 — Documentation and completion records have drifted; motion/contrast policy is incomplete

**Severity: Low**

The release-facing documents disagree with the tree:

- CMake declares 0.3.0, README says 0.2.0 (`CMakeLists.txt:3`; `README.md:21`).
- README says 24/24 tests; the suite now has 29 (`README.md:31`).
- README says a default server URL is compiled in, while Settings/architecture intentionally require explicit configuration (`README.md:114-115`; `src/core/Settings.cpp:48-54`).
- README and ARCHITECTURE list MPRIS Next/Previous as unimplemented, but Application wires both to PlayerController (`README.md:150-151`; `ARCHITECTURE.md:352-353`; `src/app/Application.cpp:245-255`).
- The AppImage build notes still describe VLC-disabled PlayerPage construction as a release blocker, although PlayerPage now loads the VLC plane conditionally and release CI self-tests the VLC-disabled artifact (`packaging/appimage/README.md:147-164`; `.github/workflows/release.yml:108-121`).

The design tokens also lack an accessibility policy. `Theme.textTertiary` is used for small informative time/technical text with roughly 3.3-3.5:1 contrast on common backgrounds, below the usual 4.5:1 target for small text (`src/ui/Theme.qml:49-53`, `80-83`; `MiniPlayer.qml:921-928`). Motion durations are unconditional, including the signature 420 ms sleeve transition (`Theme.qml:168-180`; `src/ui/shell/SleeveFlight.qml:129-168`).

**Recommendation:** derive version/capability information from one source or use a release checklist. Reserve tertiary color for nonessential/decorative content or raise its contrast for small information. Add a reduced-motion preference/platform mapping that collapses most transitions and substitutes a short crossfade or instant swap for the sleeve.

### AUD-20 — Manual release dispatch can build the wrong revision

**Severity: High**

The release workflow declares a required `tag` input for `workflow_dispatch`, but checkout does not use that input (`.github/workflows/release.yml:3-10`, `28-30`, `100`). Later shell logic chooses `GITHUB_REF_NAME` before the input (`release.yml:49-55`, `156-158`). `GITHUB_REF_NAME` is populated for manual runs, commonly with the selected branch, so the required tag can be ignored while artifacts are built from that branch. Version consistency checks may catch some runs, but the source-selection contract itself is wrong and creates a release provenance risk.

The AppImage job also downloads and executes `appimagetool` from the mutable `continuous` release without a checksum or signature (`release.yml:102-111`). Replacement or compromise of that upstream asset would directly affect shipped binaries.

**Recommendation:** select the release ref explicitly by event type, put the validated result in an environment value, and pass that exact ref to every `actions/checkout`. Avoid interpolating an unvalidated workflow input directly into shell. Pin appimagetool to an immutable release and verify its SHA-256/signature. Add a dry-run/action test covering tag-push and manual-dispatch resolution, and publish the resolved commit SHA and tool hashes with release artifacts.

### AUD-21 — Playlist membership is silently truncated

**Severity: Medium**

`PlaylistController` caps member fetches at 500 and reloads only one page (`src/app/controllers/PlaylistController.cpp:9-18`, `108-131`). There is no member pagination or “partial results” state. A playlist with more than 500 entries therefore looks complete while display, playback, reorder, remove, and other mutations operate on an incomplete local set.

**Recommendation:** page until the advertised total/short page, or expose progressive loading with a clear completeness property and disable whole-list operations until complete. Test 499, 500, 501, and multi-page playlists, including reorder/removal across a page boundary.

### AUD-22 — MPRIS capability advertisement is not truthful

**Severity: Medium**

MPRIS reports Volume as a constant `1.0` and ignores writes, while CanSeek/CanPlay/CanPause are effectively unconditional (`src/platform/MprisPlayer.cpp:92-106`). README advertises remote volume behavior, while InputMap itself notes volume actions are awaiting a controller verb (`README.md:8-10`; `src/input/InputMap.cpp:448-461`). Desktop clients can render enabled controls that do nothing or offer actions in invalid states.

Registration fallback also lacks ownership bookkeeping: when the primary service name is occupied, a fallback name may be registered, but the object does not retain that actual name and teardown unregisters only the primary (`MprisPlayer.cpp:135-166`). Partial object-registration failure can leave the fallback service owned until process exit.

**Recommendation:** wire volume and capability properties to actual PlayerController/backend state, including change notifications, or stop advertising unsupported behavior. Store and unregister the exact service name and roll back every partial registration step. Test idle/loading/playing/paused capability maps, volume round-trip, name collision, and object-registration failure.

### AUD-23 — Direct Qt build dependencies are not all discovered

**Severity: Medium**

Top-level CMake requests Qt Core, Gui, Quick, QuickControls2, Test, and WebSockets, but targets directly link `Qt6::Network`, `Qt6::DBus`, and `Qt6::OpenGL` without listing those components in `find_package` (`CMakeLists.txt:16`; `src/CMakeLists.txt:28`, `169`). A developer installation may happen to make those imported targets available transitively, while a clean/minimal Qt installation can fail configuration or generation.

The Flatpak manifest's libass primary source is a `.tar.gz` while its fallback mirror is a `.tar.xz` under the same SHA-256 (`packaging/flatpak/ca.mikesdev.StrmQt.yml:120-125`). Differently compressed archives are not byte-identical, so the mirror cannot pass the declared checksum if it is needed.

**Recommendation:** request every directly linked Qt component explicitly and add a clean dependency-image configure/build job so transitive availability cannot hide missing declarations. Use a byte-identical libass mirror or give independently described sources their correct hashes.

## `MUSIC.md` completion assessment

`MUSIC.md` is best read as a design plan rather than a completed checklist: it mixes required work, verified discovery, experiments, optional items, and explicit deferrals. Against the current tree, its core refactor is largely complete, but the document is **not fully complete if every unqualified planned item is release acceptance**.

| Plan area | Assessment | Evidence/notes |
|---|---|---|
| Phase 0: music artwork precedence and DTO data | Implemented | `MediaItem::coverSource`, mapping, queue preservation, and DTO/queue tests exist |
| Phase 1: shared TrackRow/TrackTable and direct `playAlbum` verb | Implemented | Shared controls and dedicated controller/action tests exist |
| Phase 2: Albums/Artists/Songs query axes, MusicGenres, generalized filter | Implemented | `MusicController` query properties and extensive `tst_music_query` coverage exist |
| Phase 3: audio-scoped playlists | Implemented | Music playlist tab/scoping and create behavior are tested |
| Phase 4: music mini player and single `PlayerCtl.isAudio` answer | Implemented, with lifecycle defects | The surfaces share `isAudio`, but AUD-01/AUD-02/AUD-07 show the answer is attached to the wrong transition/interaction boundaries |
| Phase 5: sampled wash and continuous sleeve | Implemented | CoverTint service/tests and SleeveFlight exist |
| Phase 6: rich MPRIS metadata and Next/Previous | Implemented | Metadata tests and command wiring exist; docs are stale |
| Phase 7: gapless prototype and ReplayGain | Partially implemented | ReplayGain is implemented/tested; no gapless advance exists and it remains a documented limitation |
| Phase 8: instant mix, multi-select, music input context | Implemented in core scope | Endpoints/actions, selection controls, and input catalogue/tests exist |
| Full-screen lyrics pane | Not implemented | No lyrics endpoint, DTO/controller, or pane exists |
| Recently played / most played music surfaces | Partially/not implemented | Artist popularity exists, but no dedicated recently-played music surface matching §7 was found |
| Queue provenance label | Implemented | `PlayQueue::contextLabel` and NowPlaying binding/tests exist |
| Waveform scrubber | Explicitly deferred | Not a completion defect under the plan's own wording |
| Crossfade | Explicitly optional and dependent on gapless | Not a required completion defect unless product scope is expanded |

The immediate implication is to close the state/lifecycle defects before declaring the music refactor production-complete. Lyrics, gapless, and recently played should then be either implemented, explicitly moved to a future milestone, or marked as accepted deferrals in `MUSIC.md`. Leaving the plan as prose with no final status makes “completed” ambiguous and has already allowed README/ARCHITECTURE capability lists to drift.

## Recommended remediation order

1. **Close the two critical boundaries.** Fix ghost playback immediately; add an authenticated-session epoch/reset transaction; and define Idle/Resolving/Loading/Playing/Paused/Recovering/Failed with pending versus presented identity and a per-load epoch.
2. **Make handoff atomic.** Bind QML and MPRIS to a coherent presented snapshot; reset backend observable state per load; reject stale events.
3. **Enforce last-user-intent-wins.** Add generations to all async queue builders and cancel them at session boundaries.
4. **Close session and app-state boundaries.** Stop/clear on logout, compose live-update and power policies from all causes, and reset item/session transients.
5. **Unify shell interaction state.** Drive gamepad and shortcuts from visible shell context; route every player entry through one focus-safe verb.
6. **Harden recovery/readback.** Serialize recovery, forward backend notifications, and persist confirmed track selections.
7. **Add the transition test matrix.** Use delayed requests and epoch-aware fake events, then add QML focus/visibility tests and backend reset contract tests.
8. **Address design/system debt.** Accessibility semantics, compact layouts, render-thread synchronization, now-playing presentation model, credential UX, and lint baseline.
9. **Harden release and reconcile scope.** Before the next release, fix dispatch ref selection and pin appimagetool. Decide the status of lyrics/gapless/recently played, then update version, test count, configuration, and MPRIS documentation together.

## Follow-up review of the repairs — 2026-08-23

A second pass re-ran every validation claimed above (build with warnings as
errors, 33/33 tests, 859-warning qmllint baseline, 13/13 offscreen page
construction) and reviewed the repair diff. All of those claims held. Eight
defects in the repairs themselves were found and fixed; each carries a
regression test unless noted.

| ID | Severity | Finding | Fix |
|---|---|---|---|
| FUP-01 | High | AUD-13 replaced the renderer's item update with `QQuickWindow::update()`. That schedules a frame but never sets the framebuffer node's render-pending flag, so mpv's frame notifications stopped reaching `render()` and video would freeze on its last drawn frame. Measured against Qt 6.11: 40 window updates produced 0 renders, 40 item updates produced 40. | Redraw requests go through a shared `MpvUpdateBridge` that posts the item update to the GUI thread. `render()` still never dereferences a GUI object, and the bridge outlives the render context. `tst_mpv_video_item` fails against the old behaviour. |
| FUP-02 | Medium | `VlcPlayer::m_loadId` was written on the GUI thread and read from libvlc's event thread as a plain `quint64`. | `std::atomic<LoadId>`. |
| FUP-03 | Medium | Gating the overlay shortcuts on the interaction context left `?` and `Ctrl+K` unable to close the sheet they opened, because opening one makes the context `overlay`. | Each shortcut stays armed for its own overlay, and only its own. |
| FUP-04 | Medium | `writeSecret()` returned false when the wallet write succeeded but the legacy plaintext copy could not be deleted, so login logged "could not persist access token" about a token safely in the wallet. | The write result reports the write; the leftover copy is reported separately and names the file. The plaintext branch now also removes a secret whose permissions it could not prove. |
| FUP-05 | Low | The pending track id was cleared on the first `tracksChanged` regardless of content, so an unrelated republication before the readback lost the user's selection. | A request survives until the engine reports it, and is retired by the next load, stop, or session boundary. |
| FUP-06 | Low | `setBaseUrl()`/`setSession()` retired every in-flight request even when the identity did not change. Making them no-ops revealed that the unconditional bump was load-bearing: it is what drops a login reply that lands after logout. | The boundary is explicit. `retireOutstandingRequests()` is called by `SessionController::beginSessionBoundary()`, and the setters only retire work on a real identity change. |
| FUP-07 | Low | Session-scoped settings keys shipped with no migration, so an upgrade silently lost view modes, remembered tracks/versions, and the resume point. | `migrateLegacySessionData()` adopts the pre-scoping keys into the first session that can own them, exactly once. |
| FUP-08 | Low | Home reconciled membership with three queries on every user-data invalidation. Audio playback keeps live updates running, so a progress report cadence became a refetch cadence. | A floor between snapshots: the first invalidation is immediate, anything behind it collapses into one deferred refresh. |

The qmllint gate also now checks the fatal type-error categories independently of
the baseline, so an unresolvable QML type can never be accepted by `--update`,
and a wholesale set change is called out as a probable toolchain change.

Still manual, unchanged from above: sustained real mpv/VLC playback (FUP-01 is
covered by an executable test but not by a real decode), a physical
multi-controller setup, a live KWallet migration/denial, a screen reader across
complete pages, and an actual release run.

## Design decisions worth preserving

- One controller-owned `isAudio` answer is the right direction; it needs to describe committed presentation rather than speculative queue intent.
- Keeping the video rendering surface constructed across in-page audio/video changes avoids expensive context churn.
- The explicit audio-mode focus reseat and OSD panel close show good awareness of hidden-focus failure modes.
- Hover and keyboard focus are consistently distinct; hover does not steal focus.
- Composite controls tend to be one tab stop and own their internal arrow-key navigation.
- `StrmSelect` follows a controlled-value pattern that avoids breaking bindings.
- The sleeve endpoints bind visibility to flight state rather than relying on paired imperative hide/show calls.
- Generation counters already used by content controllers are a sound pattern and should be extended to playback intent and backend load identity.
- The tests around cover tint, DTO artwork precedence, music queries, queue semantics, and MPRIS metadata are specific and valuable regression guards.

The project does not need a wholesale visual rewrite. Its design language is coherent and distinctive. The highest return comes from making ownership and transition boundaries as disciplined as the visual and input components already are.
