import QtQuick
import QtQuick.Controls.Basic
import StrmQt

// The application shell (ARCHITECTURE.md).
//
// This file was a bare StackView: no header, no rail, no back button, no user
// affordance. Search was `/`, settings was F2, back was Esc — none of it
// discoverable, none of it reachable with a mouse. Everything added here is the
// pointer's copy of what the keyboard could already do, plus the navigation
// history the keyboard never had either.
//
// Division of labour:
//   · pages request navigation through `Actions` (openDetails / openSeries) and
//     never push anything themselves;
//   · the chrome components emit what was chosen and never navigate;
//   · this file owns the stack, the history, and the focus memory.
//
// The one exception is HomePage's openLibrary signal: ItemActions has no library
// verb, so that signal is still handled here, exactly as before.
ApplicationWindow {
    id: root

    width: 1600
    height: 900
    minimumWidth: 960
    minimumHeight: 600
    visible: true
    title: "StrmQt"
    color: Theme.ground

    // ── Navigation history (ARCHITECTURE.md) ──────────────────────────────────
    // Retained records are compact scalar routes, never page Components, model
    // rows or controller closures. BoundedNavigationStack caps both those
    // descriptors and the live page graphs reconstructed from them.
    readonly property int navigationHistoryLimit: 40
    readonly property var navTrail: stack.navTrail
    readonly property var navForward: stack.navForward
    readonly property var focusMemory: stack.focusMemory

    readonly property bool playerOnTop: stack.currentItem !== null
                                        && stack.currentItem.objectName === "playerPage"

    // The overlays are z-stacked Items, not modal Popups, so nothing stops a
    // window shortcut from firing straight through one: with the shortcut sheet
    // up, Ctrl+K used to open the command palette on top of it and "/" pushed
    // Search *behind* it. Anything that opens a destination or a second overlay
    // stands down while one is showing.
    readonly property bool shortcutSheetOpened: shortcutSheetLoader.opened
    readonly property bool commandPaletteOpened: commandPaletteLoader.opened
    readonly property bool overlayOpen: root.shortcutSheetOpened || root.commandPaletteOpened
                                        || resumePrompt.visible
    readonly property bool chromeVisible: Session.authenticated && !root.playerOnTop

    function toggleShortcutSheet(): void {
        shortcutSheetLoader.toggle();
    }

    function toggleCommandPalette(): void {
        commandPaletteLoader.toggle();
    }
    // Interaction follows the visible surface, not whether media happens to be
    // playing underneath it. This single answer gates the shell shortcuts and
    // is published to Application for gamepad routing.
    readonly property string interactionContext: root.overlayOpen ? "overlay"
                                               : !Session.authenticated ? "login"
                                               : root.playerOnTop ? "player"
                                               : stack.currentItem !== null
                                                 && (stack.currentItem.objectName === "musicPage"
                                                     || stack.currentItem.objectName === "albumPage"
                                                     || stack.currentItem.objectName === "artistPage")
                                                 ? "music" : "browse"

    Binding {
        target: App
        property: "interactionContext"
        value: root.interactionContext
    }

    readonly property var currentEntry: stack.currentEntry
    // Destination key for the nav rail: "home", "favorites", "search",
    // "settings", "details", "series", or a library id.
    readonly property string currentKey: root.currentEntry !== null
                                         ? root.currentEntry.key : "home"
    readonly property string pageTitle: root.currentEntry !== null
                                        ? root.currentEntry.title : qsTr("Home")

    readonly property bool canGoBack: stack.canGoBack && !root.playerOnTop
    readonly property bool canGoForward: stack.canGoForward && !root.playerOnTop

    // ── Bindings come from InputMap, never from literals ───────────────────
    // MappedShortcut moved to src/ui/controls/ when music grew an input context
    // of its own (MUSIC.md §7): its keys belong to the pages that can carry them
    // out — "favourite what is selected" needs the row under the cursor, which
    // this file cannot see — and a second copy of the typable/chord split is
    // exactly the drift the control library exists to prevent.

    // ── Navigation ─────────────────────────────────────────────────────────
    function rememberFocus(): void {
        stack.rememberFocus();
    }

    // Focus is restored to the exact item the page was left on, not to the page
    // as a whole. That difference is what makes the app feel like it remembers
    // where you were instead of resetting you to the first card.
    function restoreFocusToPage(): void {
        stack.restoreFocusToCurrentPage();
    }

    // A pushed page must claim focus, exactly as a popped one does via
    // restoreFocusToPage(). Without this the item that triggered the push keeps
    // activeFocus: clicking a NavRail entry left the entry focused, which drew a
    // focus ring on it and — because NavRail.expanded is driven by "does anything
    // inside me have focus" — held the rail open until the user clicked elsewhere.
    // Every page root is a FocusScope, so this delegates to whatever the page
    // marked `focus: true` (SearchPage's field, a grid's first cell).
    function focusCurrentPage(): void {
        stack.focusCurrentPage();
    }

    function pushPage(route, initialProperties): void {
        // Player is a transient overlay, not a history entry. A remote route
        // can arrive while it is visible; remove that overlay before mutating
        // the bounded page cache so its token/page ordering stays exact.
        if (root.playerOnTop)
            stack.pop(StackView.Immediate);
        stack.pushRoute(route, initialProperties);
    }

    // Every route into the player goes through the same focus hand-off. The
    // player may already be showing (for example activeChanged followed by a
    // remote activation), in which case this simply reseats focus.
    function showPlayer(animateSleeve): void {
        if (root.playerOnTop) {
            Qt.callLater(root.focusCurrentPage);
            return;
        }
        root.rememberFocus();
        if (animateSleeve === true)
            root.liftSleeve();
        stack.push(playerComponent);
        Qt.callLater(root.focusCurrentPage);
    }

    // The plane belongs to whichever surface is showing the film: the player
    // page while it is up, the floating frame otherwise. Called whenever the
    // top of the stack changes, which covers both directions — and on the
    // page's own destruction, as a backstop.
    // Published by the player page while it exists, so nothing here has to
    // read a property off an untyped stack item.
    property Item playerVideoSlot: null

    // Leave the player WITHOUT stopping it, so playback continues under the
    // mini player. Until this existed there was no such path: Esc on the player
    // page called stop(), and canGoBack was forced false while it was on top —
    // so the docked bar was unreachable by construction.
    function minimizePlayer(): void {
        if (!root.playerOnTop)
            return;
        // The sleeve shrinks back into the bar (MUSIC.md §4). Both endpoints are
        // known right now — the hero is on screen and the bar's docked place is
        // computed rather than read — so this is one call, unlike the expand
        // below, which has to wait for a page that does not exist yet.
        root.startSleeveFlight(stack.currentItem.sleeveRect(root.contentItem),
                               stack.currentItem.sleeveRadius,
                               miniPlayer.dockedArtRect(root.contentItem),
                               miniPlayer.artRadius);
        stack.pop();
        Qt.callLater(root.restoreFocusToPage);
    }

    // ── The shared sleeve (MUSIC.md §4, "The signature") ────────────────────
    // The cover is one object that grows out of the bar and shrinks back, so it
    // lives HERE, above the StackView, where pushing and popping the player
    // page cannot destroy it.
    //
    // Both endpoints hide their own copy by BINDING to sleeveFlight.active, not
    // by being told to. There is no un-hide to miss, so an interrupted flight —
    // the opposite gesture, a track change, the player closing mid-air — cannot
    // strand an invisible cover anywhere.
    function startSleeveFlight(fromRect, fromRadius, toRect, toRadius): void {
        heroWatch.stop();
        if (!PlayerCtl.isAudio || !miniPlayer.active || toRect.width <= 0) {
            sleeveFlight.cancel();
            return;
        }
        // Interrupting a flight already in the air — expand, then immediately
        // minimise — turns the square around from wherever it has got to
        // instead of snapping it back to an endpoint it has already left.
        if (!sleeveFlight.active) {
            if (fromRect.width <= 0)
                return;
            sleeveFlight.place(fromRect, fromRadius);
        }
        sleeveFlight.flyTo(toRect, toRadius);
    }

    // Expand is the asymmetric half: the destination is inside a page that is
    // built by the push below, so the square lifts off the bar first and waits,
    // and `heroWatch` hands it its destination on the first frame the hero has
    // a geometry. If that never comes — a film, a page that failed to build —
    // the sleeve's own watchdog puts everything back.
    function liftSleeve(): void {
        heroWatch.stop();
        if (!PlayerCtl.isAudio || !miniPlayer.active) {
            sleeveFlight.cancel();
            return;
        }
        if (!sleeveFlight.active) {
            const from = miniPlayer.dockedArtRect(root.contentItem);
            if (from.width <= 0)
                return;
            sleeveFlight.place(from, miniPlayer.artRadius);
        }
        heroWatch.restart();
    }

    function goBack(): void {
        if (!root.canGoBack)
            return;
        stack.goBack();
    }

    function goForward(): void {
        if (!root.canGoForward)
            return;
        stack.goForward();
    }

    function goHome(): void {
        if (root.playerOnTop)
            stack.pop(StackView.Immediate);
        stack.goHome();
    }

    // Controllers are process-wide, while routes are not. Re-arm the scope of
    // a page restored from Back/Forward before its graph becomes interactive.
    // The switch replaces the captured per-entry closures formerly retained by
    // navigation history.
    function prepareRoute(route): void {
        switch (route.kind) {
        case "library":
            if (route.mode === "favorites")
                LibraryCtl.openFavorites();
            else if (route.mode === "genre")
                LibraryCtl.openGenre(route.id, route.name);
            else if (route.mode === "studio")
                LibraryCtl.openStudio(route.id, route.name);
            else if (route.mode === "collection")
                LibraryCtl.openCollection(route.id, route.name);
            else
                LibraryCtl.open(route.id, route.name, route.collectionType);
            break;
        case "person":
            LibraryCtl.openPerson(route.id, route.name);
            break;
        case "series":
            SeriesCtl.open(route.id, route.name);
            break;
        case "playlist":
            PlaylistCtl.refresh();
            if (route.id.length > 0)
                PlaylistCtl.open(route.id, route.name);
            break;
        case "music":
            MusicCtl.setLibrary(route.id);
            MusicCtl.loadAlbums();
            break;
        case "artist":
            MusicCtl.openArtist(route.id, route.name);
            break;
        case "album":
            MusicCtl.openAlbum(route.id, route.name);
            break;
        }
    }

    // Music kinds get their own pages. Without this an album card opened the
    // VIDEO details page, which has no notion of tracks.
    function openMusic(item): bool {
        const type = item.type !== undefined ? String(item.type) : "";
        if (type === "MusicArtist") {
            const key = "artist:" + item.itemId;
            if (root.currentKey === key) {
                root.focusCurrentPage();
                return true;
            }
            MusicCtl.openArtist(item.itemId, item.name);
            root.pushPage({ "kind": "artist", "id": item.itemId, "name": item.name,
                            "itemType": "MusicArtist", "key": key, "title": item.name },
                          { "artistItem": item });
            return true;
        }
        if (type === "MusicAlbum") {
            const key = "album:" + item.itemId;
            if (root.currentKey === key) {
                root.focusCurrentPage();
                return true;
            }
            MusicCtl.openAlbum(item.itemId, item.name);
            root.pushPage({ "kind": "album", "id": item.itemId, "name": item.name,
                            "itemType": "MusicAlbum", "key": key, "title": item.name },
                          { "albumItem": item });
            return true;
        }
        // A track opens the album it belongs to: there is no page for one song,
        // and the album is where its context lives.
        if (type === "Audio" && item.albumId !== undefined
                && String(item.albumId).length > 0) {
            return root.openMusic({ "itemId": item.albumId,
                                    "name": item.album !== undefined ? item.album : "",
                                    "type": "MusicAlbum" });
        }
        return false;
    }

    function openDetails(item): void {
        // A playlist has a page of its own and always did — it was simply
        // unreachable through this route, so a playlist card opened the VIDEO
        // details page, which is built around a backdrop, a cast and a similar
        // rail that a list of tracks has none of. The music library's Playlists
        // tab is the first surface to hand one to this verb.
        if (item && String(item.type) === "Playlist" && item.itemId !== undefined
                && String(item.itemId).length > 0) {
            root.openPlaylist(String(item.itemId),
                              item.name !== undefined ? String(item.name) : "");
            return;
        }
        // Music routes to its own pages; everything else is the details page.
        if (item && root.openMusic(item))
            return;
        const name = item && item.name ? String(item.name) : qsTr("Details");
        root.pushPage({ "kind": "details",
                        "id": item && item.itemId !== undefined ? String(item.itemId) : "",
                        "name": name,
                        "itemType": item && item.type !== undefined ? String(item.type) : "",
                        "key": "details", "title": name },
                      { "item": item });
    }

    function openSeries(seriesId, seriesName): void {
        SeriesCtl.open(seriesId, seriesName);
        root.pushPage({ "kind": "series", "id": seriesId, "name": seriesName,
                        "key": "series", "title": seriesName });
    }

    // Favorites is a filter across every library rather than a library of its
    // own, so it gets the same page and the same history record as a library —
    // only the controller call differs.
    // Re-choosing the destination already on screen is not a no-op: whatever
    // asked for it (a NavRail entry) is still holding focus and has to be
    // released, or the rail stays open and ringed.
    function openFavorites(): void {
        if (root.currentKey === "favorites") {
            root.focusCurrentPage();
            return;
        }
        LibraryCtl.openFavorites();
        root.pushPage({ "kind": "library", "mode": "favorites",
                        "key": "favorites", "title": qsTr("Favorites") });
    }

    function openLibrary(libraryId, name, collectionType): void {
        // A music library is albums AND artists, which is a different page from
        // the one-grid library view.
        if (collectionType === "music") {
            if (root.currentKey === libraryId) {
                root.focusCurrentPage();
                return;
            }
            MusicCtl.setLibrary(libraryId);
            MusicCtl.loadAlbums();
            root.pushPage({ "kind": "music", "id": libraryId, "name": name,
                            "collectionType": collectionType,
                            "key": libraryId, "title": name });
            return;
        }
        // Guarded like the others, and for a second reason: without it, clicking
        // the library already on screen pushed a duplicate page onto the history.
        if (root.currentKey === libraryId) {
            root.focusCurrentPage();
            return;
        }
        LibraryCtl.open(libraryId, name, collectionType);
        root.pushPage({ "kind": "library", "mode": "library",
                        "id": libraryId, "name": name,
                        "collectionType": collectionType,
                        "key": libraryId, "title": name });
    }

    function openPlaylists(): void {
        if (root.currentKey === "playlists") {
            root.focusCurrentPage();
            return;
        }
        PlaylistCtl.refresh();
        root.pushPage({ "kind": "playlist", "key": "playlists",
                        "title": qsTr("Playlists") });
    }

    // The same page, opened ON a playlist. It is one destination and keeps one
    // history key: arriving from a music card and arriving from the rail differ
    // only in whether anything is open in the right-hand pane, and giving the
    // two separate entries would make Back retrace a page the user never saw as
    // a different place.
    function openPlaylist(playlistId, name): void {
        PlaylistCtl.open(playlistId, name);
        if (root.currentKey === "playlists") {
            root.focusCurrentPage();
            return;
        }
        PlaylistCtl.refresh();
        root.pushPage({ "kind": "playlist", "id": playlistId, "name": name,
                        "key": "playlists", "title": qsTr("Playlists") });
    }

    function openSearch(): void {
        if (root.currentKey === "search") {
            root.focusCurrentPage();
            return;
        }
        root.pushPage({ "kind": "search", "key": "search", "title": qsTr("Search") });
    }

    function openSettings(): void {
        if (root.currentKey === "settings") {
            root.focusCurrentPage();
            return;
        }
        root.pushPage({ "kind": "settings", "key": "settings",
                        "title": qsTr("Settings") });
    }

    // A genre chip, a cast member or a studio: the same grid with one server-side
    // axis pinned, so it reuses the library page rather than growing one each.
    // The key is kind-scoped so "genre 8122" and "person 8122" are distinct
    // destinations — the two id spaces are unrelated and do overlap.
    function openBrowse(kind, id, name): void {
        const key = kind + ":" + id;
        if (root.currentKey === key) {
            root.focusCurrentPage();
            return;
        }
        // A person gets a page of their own — headshot, biography, filmography —
        // rather than the generic filtered grid the other scopes use.
        if (kind === "person") {
            LibraryCtl.openPerson(id, name);
            root.pushPage({ "kind": "person", "id": id, "name": name,
                            "key": key, "title": name },
                          { "personId": id, "personName": name });
            return;
        }

        if (kind === "genre")
            LibraryCtl.openGenre(id, name);
        else if (kind === "studio")
            LibraryCtl.openStudio(id, name);
        else if (kind === "collection")
            LibraryCtl.openCollection(id, name);
        root.pushPage({ "kind": "library", "mode": kind, "id": id, "name": name,
                        "key": key, "title": name });
    }

    // Available to pages as ApplicationWindow.window.notify(...) so a page does
    // not have to reach into the toast host itself.
    function notify(message, severity): void {
        toasts.show(message, severity !== undefined ? severity : "info");
    }

    // ── Window-level pointer routing (ARCHITECTURE.md) ───────────────────────
    Item {
        id: inputWatcher

        anchors.fill: parent
        z: -1

        // Non-blocking by default, so this only observes: pages keep every
        // hover they had.
        HoverHandler {
            onPointChanged: Input.noteInput("mouse")
        }

        // The two side buttons every mouse has had for twenty years.
        TapHandler {
            acceptedButtons: Qt.BackButton | Qt.ForwardButton
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: (eventPoint, button) => {
                Input.noteInput("mouse");
                if (button === Qt.BackButton)
                    root.goBack();
                else if (button === Qt.ForwardButton)
                    root.goForward();
            }
        }
    }

    BoundedNavigationStack {
        id: stack

        anchors.fill: parent
        anchors.leftMargin: root.chromeVisible ? navRail.collapsedWidth : 0
        anchors.topMargin: root.chromeVisible ? topBar.reservedHeight : 0
        // The mini player reserves its own strip rather than floating over the
        // page: without this the bar covers the last row of every grid, which
        // is exactly the row a user scrolls to.
        anchors.bottomMargin: miniPlayer.reservedHeight
        focus: true
        historyLimit: root.navigationHistoryLimit
        focusItem: root.activeFocusItem
        initialRoute: Session.authenticated
                      ? ({ "kind": "home", "key": "home", "title": qsTr("Home") })
                      : ({ "kind": "login", "key": "login", "title": qsTr("Sign in") })
        initialItem: Session.authenticated ? homeComponent : loginComponent

        loginPageComponent: loginComponent
        homePageComponent: homeComponent
        libraryPageComponent: libraryComponent
        personPageComponent: personComponent
        playlistPageComponent: playlistComponent
        artistPageComponent: artistComponent
        albumPageComponent: albumComponent
        musicPageComponent: musicComponent
        detailsPageComponent: detailsComponent
        seriesPageComponent: seriesComponent
        searchPageComponent: searchComponent
        settingsPageComponent: settingsComponent

        onPrepareRequested: route => root.prepareRoute(route)

        // Page-construction self-test (STRMQT_SELFTEST=1). See main.cpp: a
        // plain offscreen run only ever builds StackView's initialItem, so a
        // QML type error in any page reached by a push survives a clean
        // startup. This instantiates every page and reports.
        //
        // It runs on a delay rather than at completion so the session and the
        // controllers have settled; a page built against a half-initialised
        // controller fails for the wrong reason.
        Timer {
            running: typeof SelfTest !== "undefined" && SelfTest
            interval: 3500
            onTriggered: {
                const pages = [
                    ["login", loginComponent], ["home", homeComponent],
                    ["library", libraryComponent], ["details", detailsComponent],
                    ["series", seriesComponent], ["player", playerComponent],
                    ["search", searchComponent], ["settings", settingsComponent],
                    ["person", personComponent], ["playlist", playlistComponent],
                    ["artist", artistComponent], ["album", albumComponent],
                    ["music", musicComponent]
                ];
                let failures = 0;
                for (let i = 0; i < pages.length; ++i) {
                    const name = pages[i][0];
                    const comp = pages[i][1];
                    if (comp.status === Component.Error) {
                        console.warn("selftest FAIL " + name + ": " + comp.errorString());
                        ++failures;
                        continue;
                    }
                    const obj = comp.createObject(stack);
                    if (obj === null) {
                        console.warn("selftest FAIL " + name + ": createObject returned null");
                        ++failures;
                    } else {
                        console.log("selftest ok   " + name);
                        obj.destroy();
                    }
                }
                console.log("selftest: " + (pages.length - failures) + "/" + pages.length
                            + " pages constructed");

                // The video plane must follow the player page and come back
                // out of it, because it cannot be destroyed with the page:
                // freeing mpv's render context disables video for the loaded
                // file for good. When this handoff broke, every film played
                // black with a flash of picture on each transition — from one
                // stale read in a signal handler, which nothing here would
                // have noticed.
                stack.push(playerComponent);
                const seatedOnPage = root.playerVideoSlot !== null
                                     && videoPlane.parent === root.playerVideoSlot;
                stack.pop();
                const seatedInPip = videoPlane.parent === pipSlot;
                if (!seatedOnPage || !seatedInPip) {
                    console.warn("selftest FAIL video plane handoff: onPage=" + seatedOnPage
                                 + " backInPip=" + seatedInPip);
                    ++failures;
                } else {
                    console.log("selftest ok   video plane handoff");
                }

                // Startup-only overlays stay absent until their first verb,
                // then still expose the same toggle contract through Loader.
                if (shortcutSheetLoader.active || shortcutSheetLoader.item !== null
                    || commandPaletteLoader.active || commandPaletteLoader.item !== null) {
                    console.warn("selftest FAIL lazy overlays: instantiated at startup");
                    ++failures;
                } else {
                    root.toggleShortcutSheet();
                    root.toggleCommandPalette();
                    const shortcutReady = shortcutSheetLoader.active
                                          && shortcutSheetLoader.item !== null
                                          && shortcutSheetLoader.opened;
                    const paletteReady = commandPaletteLoader.active
                                         && commandPaletteLoader.item !== null
                                         && commandPaletteLoader.opened;
                    if (!shortcutReady || !paletteReady) {
                        console.warn("selftest FAIL lazy overlays: shortcut=" + shortcutReady
                                     + " palette=" + paletteReady);
                        ++failures;
                    } else {
                        console.log("selftest ok   lazy overlays");
                    }
                }
                Qt.exit(failures > 0 ? 1 : 0);
            }
        }

        // Back comes from the input map, not from a hardcoded Esc: rebinding it
        // in Settings has to move this too. Key events reach here only after the
        // focused page has declined them, so a page that wants Esc for itself
        // (the player does) still wins.
        Keys.onPressed: event => {
            Input.noteInput("keyboard");
            if (event.isAutoRepeat)
                return;
            const action = Input.actionForKey(event.key, event.modifiers, "browse");
            if (action === "nav.back" && root.canGoBack) {
                root.goBack();
                event.accepted = true;
            }
        }

        pushEnter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.animNormalMs }
        }
        pushExit: Transition {
            NumberAnimation { property: "opacity"; from: 1; to: 0.4; duration: Theme.animNormalMs }
        }
        popEnter: Transition {
            NumberAnimation { property: "opacity"; from: 0.4; to: 1; duration: Theme.animNormalMs }
        }
        popExit: Transition {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.animNormalMs }
        }
    }

    // Page-specific Back handling still goes through the same transaction as
    // the rail, mouse and input-map paths. Search uses this after its first Esc
    // has already cleared the query.
    Connections {
        target: stack.currentItem
        ignoreUnknownSignals: true
        function onBackRequested() { root.goBack(); }
    }

    // Above every piece of chrome: the sleeve travels over the rail and the bar,
    // not behind them. It declares no input handling, so nothing it passes over
    // becomes unclickable.
    // The endpoint rects above are asked for in THIS item's coordinates, which
    // is why every caller passes root.contentItem and not root. `root` is an
    // ApplicationWindow, not an Item: handed to a `target: Item` parameter it
    // coerces to null, mapToItem quietly falls back to scene coordinates, and
    // the two spaces coincide only while the window has no header or footer.
    // Adding either would have offset every endpoint by its height.
    // ── The video plane (one, for the life of the app) ──────────────────────
    // Same reason the sleeve below lives here rather than on a page, with a
    // harder consequence. Freeing mpv's render context disables video for the
    // file that is loaded, and mpv does not give it back: a recreated context
    // renders black, and neither vid=auto nor video-reload recovers it
    // (measured). A plane owned by the player page was therefore destroyed
    // every time the page was left, and coming back gave a black screen with
    // sound — which is exactly what leaving a film and returning to it did.
    //
    // So the plane is never destroyed. It is moved: into the page's slot while
    // the player is up, and into the floating frame below the rest of the
    // time. Reparenting and hiding are both safe — verified against Qt: one
    // renderer, never destroyed, across a reparent, a hidden item and a hidden
    // parent.
    //
    // Loaded by URL rather than as an inline Component: a build without libvlc
    // never registers the VlcVideo type, and an inline reference to it would
    // make this file unresolvable. engineName is CONSTANT, so it resolves once.
    Loader {
        id: videoPlane

        // A BINDING, not a handler. Reseating this from onCurrentItemChanged
        // read `root.playerOnTop` before that binding had been re-evaluated for
        // the same change, so the plane stayed in the hidden frame while the
        // player page was up: black video, and a flash of picture each time
        // playerOnTop flipped through a state where the frame was on screen.
        // A binding cannot be read too early — it re-evaluates when what it
        // depends on changes, in whatever order that takes.
        parent: (root.playerOnTop && root.playerVideoSlot !== null) ? root.playerVideoSlot
                                                                    : pipSlot
        anchors.fill: parent
        Component.onCompleted: setSource(
            PlayerCtl.backend.engineName === "vlc" ? "components/VlcVideoPlane.qml"
                                                   : "components/MpvVideoPlane.qml",
            { player: PlayerCtl.backend })
    }

    // ── Picture-in-picture ──────────────────────────────────────────────────
    // Where a film goes when you leave the player without stopping it. It sits
    // above the docked bar and moves with it, so the two read as one stack of
    // now-playing chrome rather than two things that happen to overlap.
    Item {
        id: pip

        readonly property int frameWidth:
            Math.round(Math.min(Theme.scale(360),
                                Math.max(Theme.scale(200), root.width * 0.22)))

        width: pip.frameWidth
        height: Math.round(pip.frameWidth * 9 / 16)
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingValue
        anchors.bottom: parent.bottom
        // The bar's reserved strip is animated, so this rides it up and down
        // instead of jumping when the bar arrives.
        anchors.bottomMargin: miniPlayer.reservedHeight + Theme.spacingValue
        // Below the bar and the rail, above the pages: it floats over content,
        // never over the chrome that controls it.
        z: 17
        visible: pip.shown || pip.reveal > 0.001
        enabled: pip.shown

        readonly property bool shown: Session.authenticated && PlayerCtl.active === true
                                      && PlayerCtl.isAudio !== true && !root.playerOnTop
        property real reveal: pip.shown ? 1 : 0

        Behavior on reveal {
            NumberAnimation {
                duration: Theme.animNormalMs
                easing.type: Theme.easeStandard
            }
        }

        opacity: pip.reveal
        scale: 0.94 + 0.06 * pip.reveal

        Accessible.role: Accessible.Button
        Accessible.name: qsTr("Picture in picture: %1").arg(PlayerCtl.title)
        Accessible.description: qsTr("Open the player")
        Accessible.onPressAction: root.showPlayer(false)

        // A rim rather than a real shadow. A MultiEffect draws its SOURCE as
        // well as the shadow, and with the picture inside that source the copy
        // landed offset from the original — two overlapping films, which is
        // invisible for the flat panels this idiom is borrowed from and
        // glaring here. Hiding the source empties its texture and takes the
        // shadow with it, and placing the copy by hand did not land either.
        // Two rounded rectangles, each a little larger and fainter, are worth
        // more than more machinery for a separation cue this size.
        Repeater {
            model: [{ grow: Theme.scale(6), alpha: 0.16 },
                    { grow: Theme.scale(3), alpha: 0.28 }]

            Rectangle {
                required property var modelData

                anchors.centerIn: parent
                width: parent.width + modelData.grow * 2
                height: parent.height + modelData.grow * 2
                radius: Theme.radiusCardValue + modelData.grow
                color: Qt.rgba(0, 0, 0, modelData.alpha)
            }
        }

        Rectangle {
            id: frame

            anchors.fill: parent
            radius: Theme.radiusCardValue
            color: "black" // a film's letterbox, not a themed surface
            border.width: 1
            border.color: pipHover.hovered ? Theme.accentColor : Theme.hairline
            clip: true

            Behavior on border.color {
                ColorAnimation {
                    duration: Theme.animFastMs
                    easing.type: Theme.easeStandard
                }
            }

            // The plane is parented here, and to the player page while that is
            // up. Nothing else may occupy it.
            Item {
                id: pipSlot

                anchors.fill: parent
            }

            // Says what a click does, without covering the picture until the
            // pointer is on it.
            Rectangle {
                anchors.fill: parent
                color: Theme.scrimColor
                opacity: pipHover.hovered ? 0.55 : 0
                visible: opacity > 0.001

                Behavior on opacity {
                    NumberAnimation {
                        duration: Theme.animFastMs
                        easing.type: Theme.easeStandard
                    }
                }

                StrmIcon {
                    anchors.centerIn: parent
                    name: "fullscreen"
                    size: Math.round(Theme.iconSize * 1.6)
                    color: Theme.textPrimaryColor
                }
            }
        }

        HoverHandler {
            id: pipHover

            cursorShape: Qt.PointingHandCursor
        }

        // A MouseArea, not a TapHandler: a handler takes a passive grab and
        // lets the press through to the grid behind, which is the bug the
        // docked bar had. This floats over content, so it must absorb.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            onClicked: root.showPlayer(false)
            onWheel: wheel => wheel.accepted = true
        }
    }

    SleeveFlight {
        id: sleeveFlight

        anchors.fill: parent
        z: 30
        // The same square both endpoints draw, so a track change mid-flight
        // swaps the picture and the flight carries on.
        source: miniPlayer.artUrl
    }

    // Waits for the full-screen hero to have a geometry, then hands the sleeve
    // its destination. Bounded: a handful of frames, after which the flight's
    // own watchdog puts both copies back rather than leaving one in the air.
    Timer {
        id: heroWatch

        property int attempts: 0

        interval: 16
        repeat: true
        onRunningChanged: {
            if (heroWatch.running)
                heroWatch.attempts = 0;
        }
        onTriggered: {
            ++heroWatch.attempts;
            const page = stack.currentItem;
            if (page === null || page.objectName !== "playerPage"
                || heroWatch.attempts > 12) {
                heroWatch.stop();
                sleeveFlight.cancel();
                return;
            }
            const target = page.sleeveRect(root.contentItem);
            if (target.width <= 0)
                return;
            heroWatch.stop();
            sleeveFlight.flyTo(target, page.sleeveRadius);
        }
    }

    // ── Chrome ─────────────────────────────────────────────────────────────
    // The rail expands over the page rather than displacing it, so only its
    // collapsed width is reserved above.
    NavRail {
        id: navRail

        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        z: 20
        visible: root.chromeVisible
        enabled: root.chromeVisible
        current: root.currentKey

        onHomeRequested: root.goHome()
        onLibrarySelected: (libraryId, name, collectionType) =>
            root.openLibrary(libraryId, name, collectionType)
        onSearchRequested: root.openSearch()
        onSettingsRequested: root.openSettings()
        onFavoritesRequested: root.openFavorites()
        onPlaylistsRequested: root.openPlaylists()
        onDismissed: root.focusCurrentPage()
    }

    // Docked now-playing bar (ARCHITECTURE.md). Sits above the pages and the top
    // bar but below the nav rail, so an expanded rail still reads over it.
    MiniPlayer {
        id: miniPlayer

        anchors.left: parent.left
        anchors.leftMargin: navRail.collapsedWidth
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        z: 18
        playerOnTop: root.playerOnTop
        sleeveInFlight: sleeveFlight.active

        onExpandRequested: {
            root.showPlayer(true)
        }
        // The bar's subline is a pair of links (MUSIC.md §4), and a link states
        // intent the same way a card does: the bar asks, this file routes. Both
        // go through openMusic(), so following "Lift Yr Skinny Fists" from the
        // bar lands on exactly the page an album card would have opened.
        onArtistRequested: (artistId, name) =>
            root.openMusic({ "itemId": artistId, "name": name, "type": "MusicArtist" })
        onAlbumRequested: (albumId, name) =>
            root.openMusic({ "itemId": albumId, "name": name, "type": "MusicAlbum" })
        onDismissed: root.focusCurrentPage()
    }

    TopBar {
        id: topBar

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.leftMargin: navRail.collapsedWidth
        anchors.right: parent.right
        z: 15
        visible: root.chromeVisible
        enabled: root.chromeVisible
        title: root.pageTitle
        canGoBack: root.canGoBack
        canGoForward: root.canGoForward
        userName: Session.username

        onBackRequested: root.goBack()
        onForwardRequested: root.goForward()
        onSettingsRequested: root.openSettings()
        onSignOutRequested: Session.logout()
        onSearchDismissed: root.restoreFocusToPage()
        // Typing here hands off to the search page, which owns the query from
        // then on: its field is bound to SearchCtl.query, so the caret lands in
        // a bigger field already holding what was typed.
        onSearchRequested: text => {
            SearchCtl.query = text;
            if (text.length > 0 && root.currentKey !== "search") {
                root.openSearch();
                topBar.clearSearch();
            }
        }
        onSearchSubmitted: text => {
            SearchCtl.query = text;
            root.openSearch();
            topBar.clearSearch();
        }
    }

    // ── Pages ──────────────────────────────────────────────────────────────
    // No page declares navigation signals any more; they call Actions.* and the
    // Connections block below does the pushing. HomePage.openLibrary is the one
    // survivor, because ItemActions has no library verb.
    Component {
        id: loginComponent
        LoginPage {}
    }

    Component {
        id: homeComponent

        HomePage {
            onOpenLibrary: (libraryId, name, collectionType) =>
                root.openLibrary(libraryId, name, collectionType)
        }
    }

    Component {
        id: libraryComponent
        LibraryPage {}
    }

    Component {
        id: personComponent
        PersonPage {}
    }

    Component {
        id: playlistComponent
        PlaylistPage {}
    }

    Component {
        id: artistComponent
        ArtistPage { objectName: "artistPage" }
    }

    Component {
        id: albumComponent
        AlbumPage { objectName: "albumPage" }
    }

    Component {
        id: musicComponent
        MusicPage { objectName: "musicPage" }
    }

    Component {
        id: detailsComponent
        DetailsPage {}
    }

    Component {
        id: seriesComponent
        SeriesPage {}
    }

    Component {
        id: playerComponent
        PlayerPage {
            onVideoSlotReady: slot => root.playerVideoSlot = slot
            // Before the page goes away with the plane still inside it. The
            // binding above has usually moved it already — playerOnTop goes
            // false when the pop starts — and this is the backstop for any
            // route that destroys the page without that happening first.
            onVideoSlotReleasing: root.playerVideoSlot = null
            sleeveInFlight: sleeveFlight.active
            onMinimizeRequested: root.minimizePlayer()
        }
    }

    Component {
        id: searchComponent
        SearchPage {}
    }

    Component {
        id: settingsComponent
        SettingsPage {}
    }

    // ── Shortcuts, all resolved through InputMap ───────────────────────────
    MappedShortcut {
        actionId: "library.search"
        fallback: ["/"]
        active: (root.interactionContext === "browse"
                 || root.interactionContext === "music")
                && root.currentKey !== "search"
        onActivated: root.openSearch()
    }

    MappedShortcut {
        actionId: "app.settings"
        fallback: ["F2"]
        active: root.interactionContext === "browse" || root.interactionContext === "music"
        onActivated: root.openSettings()
    }

    MappedShortcut {
        actionId: "app.fullscreen"
        fallback: ["F11", "F"]
        onActivated: root.visibility = root.visibility === Window.FullScreen
                     ? Window.Windowed : Window.FullScreen
    }

    // Not in the input map yet, so these carry their own defaults and pick up a
    // binding automatically the moment InputMap grows one.
    // A toggle has to be able to close what it opened. Opening either overlay
    // moves the interaction context to "overlay", so each of these stays armed
    // for its OWN sheet — and only its own, so that "?" typed into the command
    // palette's search box is text and not a shortcut.
    MappedShortcut {
        actionId: "app.shortcuts"
        fallback: ["?"]
        active: root.interactionContext === "browse" || root.interactionContext === "music"
                || (root.shortcutSheetOpened && !root.commandPaletteOpened
                    && !resumePrompt.visible)
        onActivated: root.toggleShortcutSheet()
    }

    MappedShortcut {
        actionId: "app.commandPalette"
        fallback: ["Ctrl+K"]
        active: root.interactionContext === "browse" || root.interactionContext === "music"
                || (root.commandPaletteOpened && !root.shortcutSheetOpened
                    && !resumePrompt.visible)
        onActivated: root.toggleCommandPalette()
    }

    // The docked bar takes focus by a click or by Tab, which is to say a
    // gamepad could not reach it at all: `MiniPlayer.focusTransport()` has
    // existed since the bar did and nothing called it. This is its caller, and
    // R3 on a pad resolves to the same action (GamepadManager, browse context).
    //
    // Armed on `miniPlayer.shown` rather than on PlayerCtl.active, because the
    // bar hides itself while the player page is on top — there the transport is
    // the page's own and the keyboard is already on it.
    MappedShortcut {
        actionId: "player.focusBar"
        fallback: ["N"]
        active: (root.interactionContext === "browse"
                 || root.interactionContext === "music") && miniPlayer.shown
        onActivated: miniPlayer.focusTransport()
    }

    // ── Destination cycling (gamepad shoulders, Ctrl+Tab) ───────────────────
    // Home, then every library, in the order the rail lists them. Favorites,
    // Search and Settings are deliberately absent: they are destinations you go
    // to, not ones you want to land on while flicking through your libraries.
    function cycleDestination(step): void {
        const libraries = HomeCtl.libraries;
        if (libraries === undefined || libraries === null)
            return;
        const keys = ["home"];
        const names = [qsTr("Home")];
        const types = [""];
        for (let i = 0; i < libraries.length; ++i) {
            keys.push(libraries[i].libraryId);
            names.push(libraries[i].name);
            types.push(libraries[i].collectionType);
        }
        if (keys.length < 2)
            return;

        // Somewhere off the cycle (a details page, settings): the first step
        // returns to Home rather than jumping to an arbitrary library.
        let index = keys.indexOf(root.currentKey);
        if (index < 0) {
            root.goHome();
            return;
        }
        index = (index + step + keys.length) % keys.length;
        if (keys[index] === "home")
            root.goHome();
        else
            root.openLibrary(keys[index], names[index], types[index]);
    }

    MappedShortcut {
        actionId: "nav.nextTab"
        fallback: ["Ctrl+Tab"]
        active: root.interactionContext === "browse" || root.interactionContext === "music"
        onActivated: root.cycleDestination(1)
    }

    MappedShortcut {
        actionId: "nav.previousTab"
        fallback: ["Ctrl+Shift+Tab"]
        active: root.interactionContext === "browse" || root.interactionContext === "music"
        onActivated: root.cycleDestination(-1)
    }

    // The rail expands on hover or focus, neither of which a gamepad has. This
    // is the pad's (and the keyboard's) way in: it pins the rail open AND moves
    // focus there, because an expanded rail nobody can reach is decoration.
    MappedShortcut {
        actionId: "app.toggleMenu"
        fallback: ["M"]
        active: root.interactionContext === "browse" || root.interactionContext === "music"
        onActivated: {
            if (navRail.pinned) {
                navRail.pinned = false;
                root.focusCurrentPage();
            } else {
                navRail.pinned = true;
                navRail.focusCurrent();
            }
        }
    }

    Connections {
        target: RemoteCtl

        function onNavigationRequested(destination) {
            if (destination === "home")
                root.goHome();
            else if (destination === "search")
                root.openSearch();
            else if (destination === "settings")
                root.openSettings();
            else if (destination === "back" && root.canGoBack)
                root.goBack();
        }
    }

    // A screenshot is written silently to disk; without this the user has no
    // idea whether the key did anything, or where the file went.
    Connections {
        target: PlayerCtl

        function onScreenshotSaved(path) {
            toasts.show(qsTr("Screenshot saved to %1").arg(path), "success");
        }

        function onScreenshotFailed(reason) {
            toasts.show(reason, "error");
        }
    }

    // Another Emby client sent this session a message. A toast is the only
    // place it can go, and dropping it silently would be worse.
    Connections {
        target: RemoteCtl

        function onMessageRequested(header, text) {
            const body = header.length > 0 && text.length > 0 ? header + " — " + text
                       : header.length > 0 ? header : text;
            if (body.length > 0)
                toasts.show(body, "info");
        }
    }

    // A refused version switch: the picker snaps back on its own, so without
    // this the control just looks like it does nothing.
    Connections {
        target: PlayerCtl

        function onSourceSwitchFailed(reason) {
            toasts.show(reason, "error");
        }
    }

    // ▸ on an album card is a one-shot verb, and neither MusicPage nor
    // ArtistPage carries a toast host of its own. Here, so both are covered by
    // one connection.
    Connections {
        target: MusicCtl

        function onActionFailed(message) {
            toasts.show(message, "error");
        }
    }

    // ── Controller wiring ──────────────────────────────────────────────────
    Connections {
        target: Actions

        function onDetailsRequested(item) {
            root.openDetails(item);
        }
        function onSeriesRequested(seriesId, seriesName) {
            root.openSeries(seriesId, seriesName);
        }
        function onBrowseRequested(kind, id, name) {
            root.openBrowse(kind, id, name);
        }
        function onActionFailed(message) {
            toasts.show(message, "error");
        }
        // Enqueuing has no visible effect until the current item ends, so the
        // toast is the only feedback that the verb did anything at all.
        function onQueueChanged() {
            toasts.show(qsTr("Added to queue"), "success");
        }
    }

    Connections {
        target: PlayerCtl

        function onActiveChanged() {
            if (PlayerCtl.active && !root.playerOnTop) {
                root.showPlayer(false);
            }
        }
        // A film chosen while a record is playing. The session was already
        // active, so onActiveChanged() never fired and the picture had nowhere
        // to go: the video played behind the library with only its sound, and
        // the only way to see it was to stop and start it again. The surface
        // is decided when the item is chosen, which is what this signal means.
        //
        // Audio is deliberately not here. A record started from a page opens
        // the player through onActiveChanged() as it always has, and a record
        // started while another one plays leaves the user where they are —
        // that is the whole point of the docked bar.
        function onItemStarted() {
            if (!PlayerCtl.isAudio && !root.playerOnTop)
                root.showPlayer(false);
        }
        function onStopped() {
            // Nothing is playing, so there is no sleeve to land: whatever was in
            // the air stops here rather than flying at a hero that is about to
            // be popped out from under it.
            heroWatch.stop();
            sleeveFlight.cancel();
            if (stack.currentItem && stack.currentItem.objectName === "playerPage") {
                stack.pop();
                Qt.callLater(root.restoreFocusToPage);
            }
        }
    }

    Connections {
        target: Session

        function onAuthenticatedChanged() {
            if (Session.authenticated) {
                stack.resetToRoute({ "kind": "home", "key": "home", "title": qsTr("Home") });
                HomeCtl.refresh();
            } else {
                stack.resetToRoute({ "kind": "login", "key": "login",
                                     "title": qsTr("Sign in") });
            }
        }
    }

    Component.onCompleted: {
        if (Session.authenticated) {
            HomeCtl.refresh();
            var crash = PlayerCtl.crashResumeInfo();
            if (crash.itemId) {
                resumePrompt.info = crash;
                resumePrompt.visible = true;
            }
        }
    }

    // ── Overlays ───────────────────────────────────────────────────────────
    Loader {
        id: shortcutSheetLoader
        objectName: "shortcutSheetLoader"
        readonly property ShortcutSheet overlay: item as ShortcutSheet
        readonly property bool opened: overlay !== null && overlay.opened

        function toggle(): void {
            active = true;
            overlay.toggle();
        }

        anchors.fill: parent
        z: 900
        active: false
        sourceComponent: ShortcutSheet {}
    }

    Connections {
        target: shortcutSheetLoader.overlay
        function onClosed() { root.restoreFocusToPage(); }
    }

    Loader {
        id: commandPaletteLoader
        objectName: "commandPaletteLoader"
        readonly property CommandPalette overlay: item as CommandPalette
        readonly property bool opened: overlay !== null && overlay.opened

        function toggle(): void {
            active = true;
            overlay.toggle();
        }

        anchors.fill: parent
        z: 910
        active: false
        sourceComponent: CommandPalette {}
    }

    Connections {
        target: commandPaletteLoader.overlay
        function onClosed() { root.restoreFocusToPage(); }
        function onLibraryChosen(libraryId, name, collectionType) {
            root.openLibrary(libraryId, name, collectionType);
        }
        function onItemChosen(item) { root.openDetails(item); }
        function onActionChosen(actionId) {
            if (actionId === "library.search")
                root.openSearch();
            else if (actionId === "app.settings")
                root.openSettings();
            else if (actionId === "app.fullscreen")
                root.visibility = root.visibility === Window.FullScreen
                                  ? Window.Windowed : Window.FullScreen;
        }
    }

    // Crash-resume prompt (PLAN §3.5): offered once after an unclean exit.
    Rectangle {
        id: resumePrompt

        property var info: ({})

        visible: false
        anchors.fill: parent
        color: Theme.scrimColor
        z: 950

        onVisibleChanged: if (visible) promptScope.forceActiveFocus()

        FocusScope {
            id: promptScope
            anchors.fill: parent

            Keys.onReturnPressed: event => { if (!event.isAutoRepeat) resumePrompt.accept(); }
            Keys.onEnterPressed: event => { if (!event.isAutoRepeat) resumePrompt.accept(); }
            Keys.onEscapePressed: resumePrompt.dismiss()

            Rectangle {
                anchors.centerIn: parent
                width: Theme.scale(560)
                height: prompt.height + Theme.pageMarginValue
                radius: Theme.radiusCardValue
                color: Theme.surfaceRaisedColor
                border.color: Theme.accentColor
                border.width: Theme.focusRingWidth

                Column {
                    id: prompt
                    anchors.centerIn: parent
                    width: parent.width - Theme.pageMarginValue
                    spacing: Theme.spacingValue

                    Text {
                        text: qsTr("Resume where you left off?")
                        color: Theme.textPrimaryColor
                        font.family: Theme.fontDisplay
                        font.pixelSize: Theme.fontTitle
                        font.weight: Font.DemiBold
                    }
                    Text {
                        width: parent.width
                        text: (resumePrompt.info.title || "")
                        color: Theme.textSecondaryColor
                        font.family: Theme.fontBody
                        font.pixelSize: Theme.fontBodySize
                        elide: Text.ElideRight
                    }

                    Row {
                        spacing: Theme.spacingTight

                        StrmButton {
                            text: qsTr("Resume")
                            variant: "primary"
                            iconName: "play"
                            onClicked: resumePrompt.accept()
                        }
                        StrmButton {
                            text: qsTr("Dismiss")
                            variant: "ghost"
                            onClicked: resumePrompt.dismiss()
                        }
                    }

                    Text {
                        text: qsTr("Enter — resume   ·   Esc — dismiss")
                        color: Theme.textTertiary
                        font.family: Theme.fontBody
                        font.pixelSize: Theme.fontSmall
                    }
                }
            }
        }

        function accept() {
            var info = resumePrompt.info;
            resumePrompt.visible = false;
            if (stack.currentItem)
                stack.currentItem.forceActiveFocus();
            PlayerCtl.playItem(info.itemId, info.title, info.positionMs);
            root.showPlayer(false);
        }
        function dismiss() {
            PlayerCtl.clearCrashResume();
            resumePrompt.visible = false;
            if (stack.currentItem)
                stack.currentItem.forceActiveFocus();
        }
    }

    // One toast surface for the whole app; pages reach it through
    // ApplicationWindow.window.notify().
    StrmToastHost {
        id: toasts

        anchors.fill: parent
        z: 1000
    }
}
