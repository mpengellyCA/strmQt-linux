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
    visible: true
    title: "StrmQt"
    color: Theme.ground

    // ── Navigation history (ARCHITECTURE.md) ──────────────────────────────────
    // StackView gives us back and nothing else. `navTrail` mirrors the pushed
    // pages so a popped page can be pushed again — a forward stack needs the
    // component and its properties, and StackView.pop() hands back neither.
    //
    // Records are { component, props, prepare, key, title }. `prepare` re-arms
    // whichever controller backs the page before it is pushed again, so going
    // forward into a library shows that library and not whatever was opened
    // since.
    property var navTrail: []
    property var navForward: []
    // Depth → the item that had focus when that depth was left behind.
    property var focusMemory: ({})

    readonly property bool playerOnTop: stack.currentItem !== null
                                        && stack.currentItem.objectName === "playerPage"

    // The overlays are z-stacked Items, not modal Popups, so nothing stops a
    // window shortcut from firing straight through one: with the shortcut sheet
    // up, Ctrl+K used to open the command palette on top of it and "/" pushed
    // Search *behind* it. Anything that opens a destination or a second overlay
    // stands down while one is showing.
    readonly property bool overlayOpen: shortcutSheet.opened || commandPalette.opened
                                        || resumePrompt.visible
    readonly property bool chromeVisible: Session.authenticated && !root.playerOnTop

    readonly property var currentEntry: root.navTrail.length > 0
                                        ? root.navTrail[root.navTrail.length - 1] : null
    // Destination key for the nav rail: "home", "favorites", "search",
    // "settings", "details", "series", or a library id.
    readonly property string currentKey: root.currentEntry !== null
                                         ? root.currentEntry.key : "home"
    readonly property string pageTitle: root.currentEntry !== null
                                        ? root.currentEntry.title : qsTr("Home")

    readonly property bool canGoBack: stack.depth > 1 && !root.playerOnTop
    readonly property bool canGoForward: root.navForward.length > 0 && !root.playerOnTop

    // True while a text input owns focus — letter shortcuts must not fire then.
    readonly property bool editingText: activeFocusItem !== null
                                        && activeFocusItem.cursorPosition !== undefined

    // ── Bindings come from InputMap, never from literals ───────────────────
    // Input.actions is a notifying property, so reading it here is what makes
    // every sequence below update live when a binding changes.
    readonly property var keymap: {
        const list = Input.actions;
        const map = ({});
        for (let i = 0; i < list.length; ++i)
            map[list[i].actionId] = list[i].sequences;
        return map;
    }

    function sequencesFor(actionId, fallback) {
        const found = root.keymap[actionId];
        return (found !== undefined && found.length > 0) ? found : fallback;
    }

    // A one-character sequence ("/", "F", "?") is something you can also type
    // into a text field, so it has to be suppressed while one has focus. A
    // chord or a function key never can be, and must keep working.
    function typableSequences(actionId, fallback) {
        return root.sequencesFor(actionId, fallback).filter(s => s.length === 1);
    }

    function chordSequences(actionId, fallback) {
        return root.sequencesFor(actionId, fallback).filter(s => s.length !== 1);
    }

    // A shortcut defined by an InputMap action id rather than by a key string:
    // the chord half always fires, the single-character half stands down while
    // a text field has focus.
    component MappedShortcut: Item {
        id: mapped

        property string actionId: ""
        property var fallback: []
        property bool active: true

        signal activated

        Shortcut {
            sequences: root.chordSequences(mapped.actionId, mapped.fallback)
            enabled: mapped.active
            onActivated: {
                Input.noteInput("keyboard");
                mapped.activated();
            }
        }

        Shortcut {
            sequences: root.typableSequences(mapped.actionId, mapped.fallback)
            enabled: mapped.active && !root.editingText
            onActivated: {
                Input.noteInput("keyboard");
                mapped.activated();
            }
        }
    }

    // ── Navigation ─────────────────────────────────────────────────────────
    function rememberFocus(): void {
        const item = root.activeFocusItem;
        if (item)
            root.focusMemory[stack.depth] = item;
    }

    // Focus is restored to the exact item the page was left on, not to the page
    // as a whole. That difference is what makes the app feel like it remembers
    // where you were instead of resetting you to the first card.
    function restoreFocusToPage(): void {
        const remembered = root.focusMemory[stack.depth];
        delete root.focusMemory[stack.depth];
        if (remembered) {
            try {
                if (remembered.visible && remembered.enabled) {
                    remembered.forceActiveFocus(Qt.OtherFocusReason);
                    return;
                }
            } catch (err) {
                // The remembered item was destroyed with its page; fall back.
            }
        }
        if (stack.currentItem)
            stack.currentItem.forceActiveFocus(Qt.OtherFocusReason);
    }

    // A pushed page must claim focus, exactly as a popped one does via
    // restoreFocusToPage(). Without this the item that triggered the push keeps
    // activeFocus: clicking a NavRail entry left the entry focused, which drew a
    // focus ring on it and — because NavRail.expanded is driven by "does anything
    // inside me have focus" — held the rail open until the user clicked elsewhere.
    // Every page root is a FocusScope, so this delegates to whatever the page
    // marked `focus: true` (SearchPage's field, a grid's first cell).
    function focusCurrentPage(): void {
        if (stack.currentItem)
            stack.currentItem.forceActiveFocus(Qt.OtherFocusReason);
    }

    function pushPage(component, props, prepare, key, pageTitle): void {
        root.rememberFocus();
        // Any new push abandons the forward branch, the way a browser does.
        root.navForward = [];
        root.navTrail = root.navTrail.concat([{
            "component": component,
            "props": props !== undefined && props !== null ? props : ({}),
            "prepare": prepare !== undefined ? prepare : null,
            "key": key,
            "title": pageTitle
        }]);
        stack.push(component, props !== undefined && props !== null ? props : ({}));
        Qt.callLater(root.focusCurrentPage);
    }

    // Leave the player WITHOUT stopping it, so playback continues under the
    // mini player. Until this existed there was no such path: Esc on the player
    // page called stop(), and canGoBack was forced false while it was on top —
    // so the docked bar was unreachable by construction.
    function minimizePlayer(): void {
        if (!root.playerOnTop)
            return;
        stack.pop();
        Qt.callLater(root.restoreFocusToPage);
    }

    function goBack(): void {
        if (!root.canGoBack)
            return;
        if (root.navTrail.length > 0) {
            const entry = root.navTrail[root.navTrail.length - 1];
            root.navTrail = root.navTrail.slice(0, -1);
            root.navForward = [entry].concat(root.navForward);
        }
        stack.pop();
        // Re-arm whatever backs the page we are returning TO. Controllers are
        // single instances shared across pages, so popping alone leaves them
        // scoped to the page just left: going back from a person to a library
        // showed the library page with the person's filmography still in it.
        const restored = root.navTrail.length > 0
                       ? root.navTrail[root.navTrail.length - 1] : null;
        if (restored && restored.prepare)
            restored.prepare();
        Qt.callLater(root.restoreFocusToPage);
    }

    function goForward(): void {
        if (!root.canGoForward)
            return;
        const entry = root.navForward[0];
        root.navForward = root.navForward.slice(1);
        root.rememberFocus();
        root.navTrail = root.navTrail.concat([entry]);
        if (entry.prepare)
            entry.prepare();
        stack.push(entry.component, entry.props);
        Qt.callLater(root.focusCurrentPage);
    }

    function goHome(): void {
        if (stack.depth > 1) {
            root.rememberFocus();
            // Unwinding to Home is still history: everything left behind stays
            // reachable with Forward, newest first.
            root.navForward = root.navTrail.slice().reverse().concat(root.navForward);
            root.navTrail = [];
            while (stack.depth > 1)
                stack.pop();
        }
        Qt.callLater(root.restoreFocusToPage);
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
            root.pushPage(artistComponent, { "artistItem": item },
                          () => MusicCtl.openArtist(item.itemId, item.name),
                          key, item.name);
            return true;
        }
        if (type === "MusicAlbum") {
            const key = "album:" + item.itemId;
            if (root.currentKey === key) {
                root.focusCurrentPage();
                return true;
            }
            MusicCtl.openAlbum(item.itemId, item.name);
            root.pushPage(albumComponent, { "albumItem": item },
                          () => MusicCtl.openAlbum(item.itemId, item.name),
                          key, item.name);
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
        // Music routes to its own pages; everything else is the details page.
        if (item && root.openMusic(item))
            return;
        root.pushPage(detailsComponent, { "item": item }, null, "details",
                      item && item.name ? item.name : qsTr("Details"));
    }

    function openSeries(seriesId, seriesName): void {
        SeriesCtl.open(seriesId, seriesName);
        root.pushPage(seriesComponent, ({}),
                      () => SeriesCtl.open(seriesId, seriesName),
                      "series", seriesName);
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
        root.pushPage(libraryComponent, ({}),
                      () => LibraryCtl.openFavorites(),
                      "favorites", qsTr("Favorites"));
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
            const arm = () => {
                MusicCtl.setLibrary(libraryId);
                MusicCtl.loadAlbums();
            };
            root.pushPage(musicComponent, ({}), arm, libraryId, name);
            return;
        }
        // Guarded like the others, and for a second reason: without it, clicking
        // the library already on screen pushed a duplicate page onto the history.
        if (root.currentKey === libraryId) {
            root.focusCurrentPage();
            return;
        }
        LibraryCtl.open(libraryId, name, collectionType);
        root.pushPage(libraryComponent, ({}),
                      () => LibraryCtl.open(libraryId, name, collectionType),
                      libraryId, name);
    }

    function openPlaylists(): void {
        if (root.currentKey === "playlists") {
            root.focusCurrentPage();
            return;
        }
        PlaylistCtl.refresh();
        root.pushPage(playlistComponent, ({}), () => PlaylistCtl.refresh(),
                      "playlists", qsTr("Playlists"));
    }

    function openSearch(): void {
        if (root.currentKey === "search") {
            root.focusCurrentPage();
            return;
        }
        root.pushPage(searchComponent, ({}), null, "search", qsTr("Search"));
    }

    function openSettings(): void {
        if (root.currentKey === "settings") {
            root.focusCurrentPage();
            return;
        }
        root.pushPage(settingsComponent, ({}), null, "settings", qsTr("Settings"));
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
            root.pushPage(personComponent, { "personId": id, "personName": name },
                          () => LibraryCtl.openPerson(id, name), key, name);
            return;
        }

        const run = () => {
            if (kind === "genre")
                LibraryCtl.openGenre(id, name);
            else if (kind === "person")
                LibraryCtl.openPerson(id, name);
            else if (kind === "studio")
                LibraryCtl.openStudio(id, name);
            else if (kind === "collection")
                LibraryCtl.openCollection(id, name);
        };
        run();
        root.pushPage(libraryComponent, ({}), run, key, name);
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

    StackView {
        id: stack

        anchors.fill: parent
        anchors.leftMargin: root.chromeVisible ? navRail.collapsedWidth : 0
        anchors.topMargin: root.chromeVisible ? topBar.reservedHeight : 0
        // The mini player reserves its own strip rather than floating over the
        // page: without this the bar covers the last row of every grid, which
        // is exactly the row a user scrolls to.
        anchors.bottomMargin: miniPlayer.reservedHeight
        focus: true
        initialItem: Session.authenticated ? homeComponent : loginComponent

        // focusMemory is keyed by depth, and only the depth being restored ever
        // deleted its own entry. Abandon a branch — go back three pages, then
        // walk a different way — and the entries above the new depth still hold
        // references to items that were destroyed with their pages, so the next
        // restore at that depth reads a dead object and throws (masked by the
        // try/catch in restoreFocusToPage). Anything deeper than the stack is by
        // definition gone, so this is where it gets dropped.
        onDepthChanged: {
            for (const key in root.focusMemory) {
                if (Number(key) > stack.depth)
                    delete root.focusMemory[key];
            }
        }

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

        onExpandRequested: {
            if (!root.playerOnTop) {
                root.rememberFocus();
                stack.push(playerComponent);
            }
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
        ArtistPage {}
    }

    Component {
        id: albumComponent
        AlbumPage {}
    }

    Component {
        id: musicComponent
        MusicPage {}
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
        active: Session.authenticated && !PlayerCtl.active && !root.overlayOpen
                && root.currentKey !== "search"
        onActivated: root.openSearch()
    }

    MappedShortcut {
        actionId: "app.settings"
        fallback: ["F2"]
        active: Session.authenticated && !PlayerCtl.active && !root.overlayOpen
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
    MappedShortcut {
        actionId: "app.shortcuts"
        fallback: ["?"]
        active: Session.authenticated && !root.playerOnTop
                && !commandPalette.opened && !resumePrompt.visible
        onActivated: shortcutSheet.toggle()
    }

    MappedShortcut {
        actionId: "app.commandPalette"
        fallback: ["Ctrl+K"]
        active: Session.authenticated && !root.playerOnTop
                && !shortcutSheet.opened && !resumePrompt.visible
        onActivated: commandPalette.toggle()
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
        active: Session.authenticated && !root.playerOnTop && !root.overlayOpen
        onActivated: root.cycleDestination(1)
    }

    MappedShortcut {
        actionId: "nav.previousTab"
        fallback: ["Ctrl+Shift+Tab"]
        active: Session.authenticated && !root.playerOnTop && !root.overlayOpen
        onActivated: root.cycleDestination(-1)
    }

    // The rail expands on hover or focus, neither of which a gamepad has. This
    // is the pad's (and the keyboard's) way in: it pins the rail open AND moves
    // focus there, because an expanded rail nobody can reach is decoration.
    MappedShortcut {
        actionId: "app.toggleMenu"
        fallback: ["M"]
        active: Session.authenticated && !root.playerOnTop && !root.overlayOpen
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
            if (PlayerCtl.active && stack.currentItem.objectName !== "playerPage") {
                root.rememberFocus();
                stack.push(playerComponent);
            }
        }
        function onStopped() {
            if (stack.currentItem && stack.currentItem.objectName === "playerPage") {
                stack.pop();
                Qt.callLater(root.restoreFocusToPage);
            }
        }
    }

    Connections {
        target: Session

        function onAuthenticatedChanged() {
            stack.clear();
            root.navTrail = [];
            root.navForward = [];
            root.focusMemory = ({});
            if (Session.authenticated) {
                stack.push(homeComponent);
                HomeCtl.refresh();
            } else {
                stack.push(loginComponent);
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
    ShortcutSheet {
        id: shortcutSheet

        anchors.fill: parent
        z: 900

        onClosed: root.restoreFocusToPage()
    }

    CommandPalette {
        id: commandPalette

        anchors.fill: parent
        z: 910

        onClosed: root.restoreFocusToPage()
        onLibraryChosen: (libraryId, name, collectionType) =>
            root.openLibrary(libraryId, name, collectionType)
        onItemChosen: item => root.openDetails(item)
        onActionChosen: actionId => {
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
