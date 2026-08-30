pragma ComponentBehavior: Bound
import QtQuick
import StrmQt

// MusicPage — a music library's front door (ARCHITECTURE.md).
//
// Four views of the same library, not one grid with a filter: **Albums**
// (5,037 on the target server), **Artists** (2,394 album artists, or 3,789 if
// you count everyone who appears on anything), **Songs** (56,283) and
// **Playlists** (1,564). Three of them are square art; Songs is the shared
// TrackTable, because a song is a row and not a tile. All four page, and none
// ever tries to hold the whole library — StrmGrid and TrackTable both
// virtualise, and `nearEnd` pulls the next 100 before the user reaches the edge.
//
// Why a tab bar rather than four nav-rail destinations: they are four
// readings of one library, and the answer to "where is this record" is
// sometimes the album, sometimes the artist and sometimes the track. Switching
// must not retain four delegate trees: one Loader owns the active view and the
// other three are lightweight Components. A bounded navigation snapshot per
// tab restores its cursor when the Loader switches back.
//
// The Playlists tab is the user's AUDIO playlists and nothing else. The nav
// rail's own Playlists destination is still every playlist they have — a music
// library that lists someone's film playlists is the thing this tab exists to
// avoid, and a global playlist page that hid them would be a different bug.
//
// ── Filtering (ARCHITECTURE.md) ────────────────────────────────────────────
// The bar under the tabs is the SAME FilterBar the library page uses, pointed
// at MusicCtl. Sort is per tab and filters are shared across them, which the
// controller owns; this page only says which controls to add — a Genre
// multi-select fed by /MusicGenres, because 289 genres is not a row of chips.
//
// Nothing here decides what a filter means. `extraFilterActivated` hands the
// new selection straight to the controller.
//
// Navigation contract: this page pushes nothing and declares no navigation
// signal of its own. Opening a card is `Actions.openDetails(item)`, whose
// centralized policy emits a normalized Album, Artist or Details route. One
// route contract is shared with every other page that opens a music item.
FocusScope {
    id: page

    // ── Scope ──────────────────────────────────────────────────────────────
    // Set by Main.qml at push time. Empty means "every music library", which is
    // what MusicController::setLibrary("") already means; the page then shows
    // the generic title rather than inventing one.
    // Optional: Main.qml scopes MusicCtl itself before pushing, so the page is
    // usable with neither set. They exist so the page can also be pushed with
    // its own scope, and so the header can name the library.
    property string libraryId: ""
    property string libraryName: ""
    property string initialTab: "albums"

    // What the lists are actually scoped to. The page property wins when it is
    // set; otherwise the controller's own scope is the answer, which is the
    // case when Main.qml armed MusicCtl before the push. Empty on both means no
    // library at all (the self-test), and then nothing is fetched.
    readonly property string scopeId: page.libraryId.length > 0 ? page.libraryId
                                                                : MusicCtl.libraryId

    // ── View state ─────────────────────────────────────────────────────────
    // StrmTabBar assigns its own currentIndex on a commit, so it is the source
    // of truth and this page reads it rather than binding it — a binding here
    // would be broken by the first click anyway.
    readonly property int currentTab: tabBar.currentIndex
    readonly property bool albumsTab: page.currentTab === 0
    readonly property bool artistsTab: page.currentTab === 1
    readonly property bool songsTab: page.currentTab === 2
    readonly property bool playlistsTab: page.currentTab === 3
    // Decoupled from tabBar.currentIndex so onTabSelected can snapshot the old
    // Loader item before changing its sourceComponent.
    property int loadedTab: 0
    property bool viewReady: false
    readonly property var activeView: tabViewLoader.item
    property var albumsViewState: ({ "valid": false, "identity": "", "index": -1 })
    property var artistsViewState: ({ "valid": false, "identity": "", "index": -1 })
    property var songsViewState: ({ "valid": false, "identity": "", "index": -1 })
    property var playlistsViewState: ({ "valid": false, "identity": "", "index": -1 })

    // Each tab owns a separate request lane. Aggregate loading answers whether
    // anything in the controller is busy; it is not a lifecycle signal for the
    // visible view because a hidden tab may still be finishing its own page.
    readonly property bool currentTabLoading: page.playlistsTab ? MusicCtl.playlistsLoading
                                              : page.songsTab ? MusicCtl.songsLoading
                                              : page.artistsTab ? MusicCtl.artistsLoading
                                              : MusicCtl.albumsLoading
    readonly property string currentTabErrorMessage:
        page.playlistsTab ? MusicCtl.playlistsErrorMessage
        : page.songsTab ? MusicCtl.songsErrorMessage
        : page.artistsTab ? MusicCtl.artistsErrorMessage
        : MusicCtl.albumsErrorMessage

    // The controller's own vocabulary for the same four tabs. It owns a sort
    // per tab, so it has to be told which one is on screen; the tab bar stays
    // the source of truth for the INDEX and this is the one translation.
    readonly property string tabKey: page.playlistsTab ? "playlists"
                                   : page.songsTab ? "songs"
                                   : page.artistsTab ? "artists" : "albums"

    function tabIndex(key): int {
        return key === "artists" ? 1 : key === "songs" ? 2
             : key === "playlists" ? 3 : 0
    }

    readonly property bool albumArtistMode: MusicCtl.artistMode === "albumArtists"

    readonly property int albumTotal: MusicCtl.albums.totalRecordCount
    readonly property int artistTotal: MusicCtl.artists.totalRecordCount
    readonly property int songTotal: MusicCtl.songs.totalRecordCount
    readonly property int playlistTotal: MusicCtl.playlists.totalRecordCount

    readonly property bool failed: page.currentTabErrorMessage.length > 0
    readonly property int shownCount: page.activeView && page.activeView.count !== undefined
                                      ? Number(page.activeView.count) : 0
    readonly property bool isEmpty: page.shownCount === 0 && !page.currentTabLoading
    // Nothing below the tab bar can take focus while the first page is in
    // flight or the view is empty, so the tab bar holds it — a page that
    // arrives with the keyboard pointing at nothing is a page you cannot leave
    // without the mouse. The empty state's own action stays reachable by Tab.
    readonly property bool contentFocusable: page.shownCount > 0

    function formatCount(value) {
        return Number(value).toLocaleString(Qt.locale(), 'f', 0)
    }

    function viewState(index): var {
        return index === 1 ? page.artistsViewState
             : index === 2 ? page.songsViewState
             : index === 3 ? page.playlistsViewState : page.albumsViewState
    }

    function setViewState(index, state): void {
        if (index === 1)
            page.artistsViewState = state
        else if (index === 2)
            page.songsViewState = state
        else if (index === 3)
            page.playlistsViewState = state
        else
            page.albumsViewState = state
    }

    function captureActiveView(): void {
        const view = page.activeView
        if (!view || typeof view.navigationFocusSnapshot !== "function")
            return
        page.setViewState(page.loadedTab, view.navigationFocusSnapshot())
    }

    function restoreActiveView(): void {
        const view = page.activeView
        const state = page.viewState(page.loadedTab)
        if (!view || !state || state.valid !== true
                || typeof view.restoreNavigationFocus !== "function")
            return
        view.restoreNavigationFocus(String(state.identity), Number(state.index))
    }

    function songsView(): var {
        return page.loadedTab === 2 ? page.activeView : null
    }

    readonly property string headerSubtitle: {
        if (page.albumsTab)
            return page.albumTotal > 0 ? qsTr("%1 albums").arg(page.formatCount(page.albumTotal))
                                       : "";
        if (page.songsTab)
            return page.songTotal > 0 ? qsTr("%1 songs").arg(page.formatCount(page.songTotal))
                                      : "";
        if (page.playlistsTab)
            return page.playlistTotal > 0
                    ? qsTr("%1 playlists").arg(page.formatCount(page.playlistTotal)) : "";
        if (page.artistTotal <= 0)
            return "";
        return page.albumArtistMode
                ? qsTr("%1 album artists").arg(page.formatCount(page.artistTotal))
                : qsTr("%1 artists").arg(page.formatCount(page.artistTotal));
    }

    // ── Loading ────────────────────────────────────────────────────────────
    // Albums are fetched when the page opens; artists only when the tab is
    // first chosen. Both lists are several thousand items long and a user who
    // only ever browses albums should not pay for the artist query.
    //
    // Guarded on the scope so a page constructed with none (the self-test)
    // issues no request at all, and on `count` so returning to a library
    // already loaded does not re-fetch it.
    // Each ensure uses its own lane's loading flag. For artists and songs,
    // `MusicCtl.tab = …` ends in the controller's own
    // ensureCurrentTab(), which already issues the tab's first page when its
    // model is empty. The request is async, so `count` is still 0 on the very
    // next line — the guard the two used to have could not tell "nobody has
    // asked" from "somebody asked a microsecond ago". That cost a wasted 100-row
    // query against a 56,283-track library on every cold tab switch. A hidden
    // lane is deliberately ignored here: it neither owns nor admits work for
    // the visible tab.
    function ensureAlbums() {
        if (page.scopeId.length > 0 && MusicCtl.albums.count === 0
                && !MusicCtl.albumsLoading)
            MusicCtl.loadAlbums()
    }

    function ensureArtists() {
        if (page.scopeId.length > 0 && MusicCtl.artists.count === 0
                && !MusicCtl.artistsLoading)
            MusicCtl.loadArtists()
    }

    function ensureSongs() {
        if (page.scopeId.length > 0 && MusicCtl.songs.count === 0
                && !MusicCtl.songsLoading)
            MusicCtl.loadSongs()
    }

    // The scope guard is load-bearing here and not merely an optimisation: the
    // library id is what makes this list AUDIO playlists (see
    // MusicController::loadPlaylists), so with no scope there is nothing to ask
    // for. The controller refuses the same way; this just avoids the round trip.
    function ensurePlaylists() {
        if (page.scopeId.length > 0 && MusicCtl.playlists.count === 0
                && !MusicCtl.playlistsLoading)
            MusicCtl.loadPlaylists()
    }

    // /MusicGenres for the filter bar's Genre select, scoped by ParentId —
    // measured: the music library answers 289 genres and every other library
    // answers none.
    //
    // Called on every tab switch as well as at open, because the controller's
    // loadGenres() is a resume rather than a one-shot: a walk that answered page
    // 0 and then failed picks up from the page that failed, and one that reached
    // the end of the list turns every later call away. This is the app's only
    // retry surface for a half-loaded genre list.
    function ensureGenres() {
        if (page.scopeId.length > 0)
            MusicCtl.loadGenres()
    }

    Component.onCompleted: {
        if (page.libraryId.length > 0)
            MusicCtl.setLibrary(page.libraryId)
        tabBar.currentIndex = page.tabIndex(page.initialTab)
        page.loadedTab = tabBar.currentIndex
        MusicCtl.tab = page.tabKey
        page.viewReady = true
        if (page.artistsTab)
            page.ensureArtists()
        else if (page.songsTab)
            page.ensureSongs()
        else if (page.playlistsTab)
            page.ensurePlaylists()
        else
            page.ensureAlbums()
        page.ensureGenres()
    }

    // ── Item helpers ───────────────────────────────────────────────────────
    function albumAt(index) {
        const model = MusicCtl.albums
        if (!model || index < 0 || index >= model.count)
            return null
        return model.get(index)
    }

    function artistAt(index) {
        const model = MusicCtl.artists
        if (!model || index < 0 || index >= model.count)
            return null
        return model.get(index)
    }

    function songAt(index) {
        const model = MusicCtl.songs
        if (!model || index < 0 || index >= model.count)
            return null
        return model.get(index)
    }

    function playlistAt(index) {
        const model = MusicCtl.playlists
        if (!model || index < 0 || index >= model.count)
            return null
        return model.get(index)
    }

    // Play the Songs list from one row. The rows already loaded are the queue,
    // which is the same contract the album page's table has — a 56,283-row
    // queue is not what "play this song" means.
    function playSongFrom(index) {
        const model = MusicCtl.songs
        if (!model || index < 0 || index >= model.count)
            return
        Actions.playAllFrom(ModelUtils.drain(model), index)
    }

    // ── Batch verbs (MUSIC.md §7) ──────────────────────────────────────────
    // The picker takes a list of ids and always did, so this is the same call
    // the row's own "Add to playlist" makes with one id in it. The subject is
    // the library, because a selection spanning forty records has no other
    // honest name to offer the "create from these" row.
    function fileSongSelection() {
        const table = page.songsView()
        if (!table)
            return
        const ids = table.selectedIds()
        if (ids.length === 0)
            return
        playlistPicker.show(page.libraryName.length > 0 ? page.libraryName : qsTr("Music"), ids)
    }

    // ── What is playing right now ──────────────────────────────────────────
    // Reading queue.currentIndex is what makes this re-evaluate: it is a
    // notifying property and currentItem() is a plain lookup that would never
    // update on its own.
    readonly property string nowPlayingId: {
        const queue = PlayerCtl.queue
        if (!queue || queue.currentIndex < 0)
            return ""
        const current = queue.currentItem()
        return (current && current.itemId !== undefined) ? String(current.itemId) : ""
    }

    function formatDuration(ms) {
        return NowPlayingInfo.formatDuration(ms, "–:––")
    }

    function idOf(item) {
        return (item && item.itemId !== undefined) ? String(item.itemId) : ""
    }

    // ── Playing an album from the grid ─────────────────────────────────────
    // One call, because "play this record" is a semantic verb and lives in
    // ItemActions (ARCHITECTURE.md rule 3). Its album policy delegates to
    // MusicController::playAlbum(), which fetches the server's ordered children
    // into a scratch model and hands the resulting leaves back.
    //
    // This page used to do it by calling openAlbum() and watching the shared
    // `tracks` model fill behind a pending-id guard — which meant playing an
    // album navigated controller state, and would have fought the album page
    // the moment both were live.
    function playAlbum(item) {
        Actions.play(item)
    }

    function requestAlbum(item) {
        if (page.idOf(item).length === 0)
            return
        Actions.openDetails(item)
    }

    function requestArtist(item) {
        if (page.idOf(item).length === 0)
            return
        Actions.openDetails(item)
    }

    // A playlist opens the playlist page, through the same one route every
    // other card uses: Main.qml routes by item type and this page pushes
    // nothing itself.
    function requestPlaylist(item) {
        if (page.idOf(item).length === 0)
            return
        Actions.openDetails(item)
    }

    // ── The music input context (MUSIC.md §7) ──────────────────────────────
    // Space, S, L and R mean something here that they mean nowhere else, which
    // is why `InputMap` grew a third context rather than these being global.
    // They live on the page and not in Main.qml because two of them need the
    // row under the cursor, which only this page can see.
    //
    // The shell context is load-bearing: a Shortcut is window-scoped and a
    // StackView keeps a covered page alive. It also stands down for shell
    // overlays layered above an otherwise-visible music page.
    //
    // Coexistence with type-to-jump: TrackTable claims single printable
    // characters while it has focus and typeToJump is on, so on the Songs tab
    // "s" and "l" jump to a song instead of firing these — which is right, the
    // user is typing. Space is exempt at the table until a word is already
    // being typed, so play/pause works from the track list too.
    // What the keyboard is standing on, whichever view is showing.
    function focusedItem() {
        const view = page.activeView
        if (!view || view.currentIndex === undefined)
            return null
        if (page.loadedTab === 2)
            return page.songAt(view.currentIndex)
        if (page.loadedTab === 1)
            return page.artistAt(view.currentIndex)
        if (page.loadedTab === 3)
            return page.playlistAt(view.currentIndex)
        return page.albumAt(view.currentIndex)
    }

    MappedShortcut {
        actionId: "music.playPause"
        fallback: ["Space"]
        // Only while something is loaded: with no queue, Space is Select and
        // the grid's own key handling has to keep it.
        active: App.interactionContext === "music" && PlayerCtl.active
        onActivated: PlayerCtl.togglePause()
    }

    MappedShortcut {
        actionId: "music.shuffleAll"
        fallback: ["S"]
        active: App.interactionContext === "music" && page.scopeId.length > 0
        // The same call the header button makes, for the same reason it makes
        // it: shuffle exists once and takes the library AS FILTERED — the
        // letter, genres and favourites narrowing the view are the sample.
        onActivated: MusicCtl.shuffleFiltered()
    }

    MappedShortcut {
        actionId: "music.favorite"
        fallback: ["L"]
        active: App.interactionContext === "music"
        onActivated: {
            // A selection wins over the cursor: if the user has picked rows,
            // "favourite" is obviously about those rows.
            const table = page.songsView()
            if (table && table.selectionCount > 0) {
                Actions.setFavoriteAll(table.selectedIds(), true)
                return
            }
            const item = page.focusedItem()
            if (item)
                Actions.toggleFavorite(item)
        }
    }

    MappedShortcut {
        actionId: "music.instantMix"
        fallback: ["R"]
        // Not on the Playlists tab: a playlist's order IS the playlist, and
        // /Items/{playlistId}/InstantMix is a query nobody has measured.
        active: App.interactionContext === "music" && !page.playlistsTab
        onActivated: {
            const item = page.focusedItem()
            if (item)
                Actions.instantMix(item)
        }
    }

    // ── Header ─────────────────────────────────────────────────────────────
    PageHeader {
        id: header

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: Theme.pageMarginValue
        anchors.rightMargin: Theme.pageMarginValue
        anchors.topMargin: Theme.spacingValue
        height: header.implicitHeight

        title: page.libraryName.length > 0 ? page.libraryName : qsTr("Music")
        subtitle: page.headerSubtitle

        // ── Which artists? (a real choice, not a filter) ───────────────────
        // /Artists and /Artists/AlbumArtists are different endpoints returning
        // different lists — 3,789 people who appear on something against the
        // 2,394 an album is actually filed under. Two chips rather than a
        // dropdown: the alternative is worth seeing without opening anything,
        // and a chip is controlled (it renders `checked`, it never flips
        // itself), so the value stays owned by the controller.
        StrmChip {
            id: albumArtistChip

            anchors.verticalCenter: parent.verticalCenter
            visible: page.artistsTab
            // No glyph on either chip: the icon set has `user` (one person)
            // and nothing for "several people", and drawing the same single
            // figure on both halves of a binary choice says nothing. See this
            // wave's report for the icon that would earn its place here.
            text: qsTr("Album artists")
            checked: page.albumArtistMode
            onToggled: MusicCtl.artistMode = "albumArtists"

            KeyNavigation.right: allArtistChip
            KeyNavigation.down: tabBar
        }

        StrmChip {
            id: allArtistChip

            anchors.verticalCenter: parent.verticalCenter
            visible: page.artistsTab
            text: qsTr("All artists")
            checked: !page.albumArtistMode
            onToggled: MusicCtl.artistMode = "artists"

            KeyNavigation.left: albumArtistChip
            KeyNavigation.right: shuffleAllButton
            KeyNavigation.down: tabBar
        }

        // ── Shuffle everything ─────────────────────────────────────────────
        // The most-used button in every music app, and the one this had none
        // of. It shuffles the library AS NARROWED: the genre, letter and
        // favourites filters on the filter bar constrain the sample, so
        // "shuffle the jazz" is a filter plus this button. The query is built
        // by MusicController (which owns the filter state) and the Random
        // sample by ItemActions::shuffleFiltered (ARCHITECTURE.md rule 3 —
        // shuffle exists once).
        StrmButton {
            id: shuffleAllButton

            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Shuffle")
            iconName: "shuffle"
            variant: "primary"
            enabled: page.scopeId.length > 0
            onClicked: MusicCtl.shuffleFiltered()

            KeyNavigation.left: page.artistsTab ? allArtistChip : null
            KeyNavigation.down: tabBar
        }
    }

    // ── Albums / Artists ───────────────────────────────────────────────────
    StrmTabBar {
        id: tabBar

        anchors.left: parent.left
        anchors.top: header.bottom
        anchors.leftMargin: Theme.pageMarginValue
        anchors.topMargin: Theme.spacingTight

        tabs: [{ text: qsTr("Albums") }, { text: qsTr("Artists") }, { text: qsTr("Songs") },
               { text: qsTr("Playlists") }]
        focus: !page.contentFocusable

        KeyNavigation.up: page.artistsTab ? albumArtistChip : shuffleAllButton
        KeyNavigation.down: filterBar

        onTabSelected: index => {
            page.captureActiveView()
            // The controller keeps a sort per tab and fetches the tab's first
            // page if it has none, so telling it which tab is on screen is the
            // whole of the switch. The ensure* calls stay for the case it
            // cannot cover: a page that opened before anything was asked of the
            // controller at all.
            MusicCtl.tab = index === 3 ? "playlists" : index === 2 ? "songs"
                         : index === 1 ? "artists" : "albums"
            // MusicCtl clears/starts a dirty lane synchronously. Only after
            // that contract is established may the Loader construct delegates
            // for the newly active model.
            page.loadedTab = index
            if (index === 1)
                page.ensureArtists()
            else if (index === 2)
                page.ensureSongs()
            else if (index === 3)
                page.ensurePlaylists()
            else
                page.ensureAlbums()
            page.ensureGenres()
        }
    }

    // ── Sort / filter / alphabet ───────────────────────────────────────────
    // The library page's bar, pointed at MusicCtl. Below the tabs rather than
    // above them because the sort set is a property of the tab: "Track number"
    // is meaningless for an artist and "Release year" for a song, so the bar
    // has to read as belonging to the view it governs.
    //
    // The alphabet strip keeps its "SORT NAME" hint, and it is more right here
    // than on the film page: "The Beatles" files under B, and a user who does
    // not know that will look under T.
    FilterBar {
        id: filterBar

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: tabBar.bottom
        anchors.leftMargin: Theme.pageMarginValue
        anchors.rightMargin: Theme.pageMarginValue
        anchors.topMargin: Theme.spacingTight
        height: filterBar.implicitHeight

        controller: MusicCtl

        // One page-supplied axis: genre. A multi-select and not chips — the
        // measured library has 289 of them.
        //
        // The label doubles as the failure surface. FilterBar disables a select
        // with no options, and a control that is greyed out for reasons only the
        // log knows is the same dead end as an empty list with no explanation:
        // the walk failed, and saying so is the difference between "this library
        // has no genres" and "ask again".
        extraFilters: [{
            key: "genre",
            label: (MusicCtl.genresFailed && MusicCtl.genreOptions.length === 0)
                   ? qsTr("Genre unavailable") : qsTr("Genre"),
            options: MusicCtl.genreOptions,
            selected: MusicCtl.genreIds
        }]

        onExtraFilterActivated: (key, values) => {
            if (key === "genre")
                MusicCtl.setGenreIds(values)
        }

        // Down out of the bar lands in whichever view it is narrowing, and Up
        // lands back on the tabs — which is where Up out of a grid reached
        // before there was a bar between them.
        downTarget: page.activeView
        upTarget: tabBar
    }

    // The four views below are Components, not four live views. tabViewLoader
    // owns exactly one of them; changing sourceComponent destroys the old
    // delegate tree before constructing the next one.
    readonly property int songRowHeight: Theme.scale(52)
    readonly property int songNumberColumn: Theme.scale(46)
    readonly property int songDurationColumn: Theme.scale(64)
    readonly property int songVerbsColumn: Theme.scale(72)
    readonly property int songArtistColumn:
        page.songsView() && page.songsView().showArtistColumn ? Theme.scale(220) : 0

    SelectionBar {
        id: songSelection

        anchors.top: filterBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.pageMarginValue
        anchors.rightMargin: Theme.pageMarginValue

        count: page.songsView() ? page.songsView().selectionCount : 0

        onQueueRequested: Actions.addAllToQueue(page.songsView().selectedItems())
        onPlaylistRequested: page.fileSongSelection()
        onFavoriteRequested: Actions.setFavoriteAll(page.songsView().selectedIds(), true)
        onClearRequested: {
            const table = page.songsView()
            if (table) {
                table.clearSelection()
                table.forceActiveFocus(Qt.OtherFocusReason)
            }
        }
    }

    Component {
        id: albumsViewComponent

        StrmGrid {
            id: albumsGrid
            navigationFocusKey: "music-albums"
            navigationFocusFallbackItem: tabBar
            navigationFocusRefillActive: MusicCtl.albumsLoading
            gridModel: MusicCtl.albums
            cardVariant: "square"
            emptyText: ""
            prefetchThreshold: 30
            focus: albumsGrid.count > 0
            KeyNavigation.up: filterBar.entryItem
            onNearEnd: if (MusicCtl.canLoadMoreAlbums) MusicCtl.loadMoreAlbums()
            onItemActivated: index => page.requestAlbum(page.albumAt(index))
            onItemPlayRequested: index => page.playAlbum(page.albumAt(index))
            onItemPlayedToggled: index => {
                const item = page.albumAt(index)
                if (item)
                    Actions.togglePlayed(item)
            }
            onItemFavoriteToggled: index => {
                const item = page.albumAt(index)
                if (item)
                    Actions.toggleFavorite(item)
            }
            onMenuRequested: (index, mx, my) =>
                musicMenu.popupForItem(page.albumAt(index), mx, my)
        }
    }

    Component {
        id: artistsViewComponent

        StrmGrid {
            id: artistsGrid
            navigationFocusKey: "music-artists"
            navigationFocusFallbackItem: tabBar
            navigationFocusRefillActive: MusicCtl.artistsLoading
            gridModel: MusicCtl.artists
            cardVariant: "square"
            emptyText: ""
            prefetchThreshold: 30
            focus: artistsGrid.count > 0
            KeyNavigation.up: filterBar.entryItem
            onNearEnd: if (MusicCtl.canLoadMoreArtists) MusicCtl.loadMoreArtists()
            onItemActivated: index => page.requestArtist(page.artistAt(index))
            onItemPlayRequested: index => Actions.instantMix(page.artistAt(index))
            onItemFavoriteToggled: index => {
                const item = page.artistAt(index)
                if (item)
                    Actions.toggleFavorite(item)
            }
            onMenuRequested: (index, mx, my) =>
                musicMenu.popupForItem(page.artistAt(index), mx, my)
        }
    }

    Component {
        id: playlistsViewComponent

        StrmGrid {
            id: playlistsGrid
            navigationFocusKey: "music-playlists"
            navigationFocusFallbackItem: tabBar
            navigationFocusRefillActive: MusicCtl.playlistsLoading
            gridModel: MusicCtl.playlists
            cardVariant: "square"
            emptyText: ""
            prefetchThreshold: 30
            focus: playlistsGrid.count > 0
            KeyNavigation.up: filterBar.entryItem
            onNearEnd: if (MusicCtl.canLoadMorePlaylists) MusicCtl.loadMorePlaylists()
            onItemActivated: index => page.requestPlaylist(page.playlistAt(index))
            onItemPlayRequested: index => page.requestPlaylist(page.playlistAt(index))
            onItemFavoriteToggled: index => {
                const item = page.playlistAt(index)
                if (item)
                    Actions.toggleFavorite(item)
            }
            onMenuRequested: (index, mx, my) =>
                musicMenu.popupForItem(page.playlistAt(index), mx, my)
        }
    }

    Component {
        id: songsViewComponent

        TrackTable {
            id: songsTable
            navigationFocusKey: "music-songs"
            navigationFocusFallbackItem: tabBar
            navigationFocusRefillActive: MusicCtl.songsLoading
            focus: songsTable.count > 0
            model: MusicCtl.songs
            rowHeight: page.songRowHeight
            discGrouping: false
            artistRule: false
            alwaysShowArtist: true
            multiSelect: true
            KeyNavigation.up: filterBar.entryItem
            onActivated: index => page.playSongFrom(index)
            prefetchThreshold: 30
            onNearEnd: if (MusicCtl.canLoadMoreSongs) MusicCtl.loadMoreSongs()

            delegate: TrackRow {
                id: songRow
                required property int index
                required property var model
                readonly property string trackId: songRow.model.itemId !== undefined
                                                  ? String(songRow.model.itemId) : ""
                width: songsTable.width
                navigationFocusOwner: songsTable
                rowHeight: page.songRowHeight
                numberColumn: page.songNumberColumn
                durationColumn: page.songDurationColumn
                verbsColumn: page.songVerbsColumn
                artistColumn: page.songArtistColumn
                title: songRow.model.name !== undefined ? String(songRow.model.name) : ""
                secondary: songRow.model.album !== undefined && songRow.model.album !== null
                           ? String(songRow.model.album) : ""
                artist: songsTable.shownArtistFor(songRow.model)
                durationText: page.formatDuration(songRow.model.runtimeMs)
                number: songRow.index + 1
                coverUrl: songRow.model.posterUrl !== undefined
                          ? String(songRow.model.posterUrl) : ""
                showCover: true
                current: songsTable.currentIndex === songRow.index && songsTable.activeFocus
                selected: songsTable.isSelected(songRow.index)
                playing: songRow.trackId.length > 0 && songRow.trackId === page.nowPlayingId
                favorite: songRow.model.favorite === true
                showFavorite: true
                showMenu: true
                verbsRevealed: songRow.hovered || songRow.favorite
                onActivated: modifiers => {
                    songsTable.forceActiveFocus(Qt.MouseFocusReason)
                    songsTable.activateAt(songRow.index, modifiers)
                }
                onFavoriteToggled: {
                    const item = page.songAt(songRow.index)
                    if (item)
                        Actions.toggleFavorite(item)
                }
                onMenuRequested: (sceneX, sceneY) =>
                    songMenu.popupForItemNoDetails(page.songAt(songRow.index), sceneX, sceneY)
            }
        }
    }

    Loader {
        id: tabViewLoader

        anchors.top: page.songsTab ? songSelection.bottom : filterBar.bottom
        anchors.topMargin: Theme.spacingTight
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: page.songsTab ? Theme.pageMarginValue : 0
        anchors.rightMargin: page.songsTab ? Theme.pageMarginValue : 0
        anchors.bottomMargin: page.songsTab ? Theme.spacingValue : 0
        active: page.viewReady
        // A Loader is a focus scope: the grid inside it can only receive
        // activeFocus while the LOADER holds focus in the page's scope. A
        // constant `focus: true` loses that claim to the tab bar's
        // `focus: !contentFocusable` — one focus child per scope — and when
        // content then arrived, the grid's `focus: count > 0` landed in a
        // scope that had no focus and the page went dead to arrows. Mirroring
        // the tab bar's condition hands the claim over in step with it:
        // empty or loading, the tab bar holds the keyboard; loaded, the view
        // does.
        focus: page.contentFocusable
        sourceComponent: page.loadedTab === 1 ? artistsViewComponent
                       : page.loadedTab === 2 ? songsViewComponent
                       : page.loadedTab === 3 ? playlistsViewComponent
                                              : albumsViewComponent
        onLoaded: Qt.callLater(page.restoreActiveView)
    }

    // The shared item menu, minus "Details": for a track the album page IS the
    // details page, exactly as on AlbumPage — and "exactly as on AlbumPage"
    // has to include the playlist row. Right-clicking a track here and
    // right-clicking the same track on its album page were offering different
    // verbs for no reason anyone could state.
    ItemMenu {
        id: songMenu

        allowAddToPlaylist: true
        onAddToPlaylistRequested: item => {
            const id = (item && item.itemId !== undefined) ? String(item.itemId) : ""
            const name = (item && item.name !== undefined) ? String(item.name) : ""
            if (id.length > 0)
                playlistPicker.show(name, [id])
        }
    }

    // ── Add to playlist ────────────────────────────────────────────────────
    // The same panel the album page raises, now a registered control rather
    // than an inline component copied per page.
    PlaylistPicker {
        id: playlistPicker

        z: 800
        // Only tracks are filed from this page, so a playlist created here is
        // an audio one — which is what lands it in the tab beside this picker
        // rather than in no library at all.
        mediaType: "Audio"
        // Back to the table the row was picked from, not to the top of the
        // page: an overlay that drops the keyboard somewhere else is the same
        // bug as one that never gives it back.
        onDismissed: {
            const table = page.songsView()
            if (table && table.count > 0)
                table.forceActiveFocus(Qt.OtherFocusReason)
        }
    }

    // An album card's "Add to playlist" (MUSIC.md §3's carried-over gap). The
    // grid only has an album ID, and a playlist holds tracks — so the round trip
    // is the controller's and this is where it lands.
    Connections {
        target: MusicCtl

        function onAlbumTracksCollected(subject, trackIds) {
            playlistPicker.show(subject, trackIds)
        }
    }

    // `pending` is what tells this page's toast apart from a playlist edited on
    // some other surface: PlaylistCtl's results are global.
    Connections {
        target: PlaylistCtl

        function onActionSucceeded(message) {
            if (!playlistPicker.pending)
                return
            playlistPicker.pending = false
            musicToasts.show(message, "success")
        }
        function onActionFailed(message) {
            if (!playlistPicker.pending)
                return
            playlistPicker.pending = false
            musicToasts.show(message, "error")
        }
    }

    StrmToastHost {
        id: musicToasts

        anchors.fill: parent
        z: 900
    }

    // ── Context menu ───────────────────────────────────────────────────────
    // This browse profile is intentionally narrower than the generic menu,
    // while its kinds, order and targets remain ItemActions policy.
    ItemMenu {
        id: musicMenu

        profile: "musicBrowse"
        allowAddToPlaylist: true
        onAddToPlaylistRequested: item => {
            const id = page.idOf(item)
            const name = item && item.name !== undefined ? String(item.name) : ""
            if (id.length > 0)
                MusicCtl.collectAlbumTracks(id, name)
        }
    }

    // ── Page states ────────────────────────────────────────────────────────
    LoadingState {
        anchors.top: filterBar.bottom
        anchors.topMargin: Theme.spacingValue
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        visible: page.currentTabLoading && page.shownCount === 0
        // A skeleton has to look like what is coming: rows for the song list,
        // tiles for the three grids.
        shape: page.songsTab ? "list" : "grid"
    }

    EmptyState {
        anchors.top: filterBar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        visible: page.isEmpty && page.failed
        severity: "error"
        iconName: "info"
        headline: qsTr("Couldn't load this music library")
        body: page.currentTabErrorMessage
        actionText: qsTr("Retry")
        actionIcon: "refresh"
        onActionTriggered: {
            if (page.songsTab)
                MusicCtl.loadSongs()
            else if (page.playlistsTab)
                MusicCtl.loadPlaylists()
            else if (page.artistsTab)
                MusicCtl.loadArtists()
            else
                MusicCtl.loadAlbums()
        }
    }

    EmptyState {
        anchors.top: filterBar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        visible: page.isEmpty && !page.failed
        // Empty-because-there-is-nothing and empty-because-the-filters-took-it
        // -all are different dead ends, and only the second has a way out. The
        // library page draws exactly this distinction; now that music filters,
        // it needs it too — narrowed to nothing with no visible undo is the
        // worst state this page can be in.
        iconName: MusicCtl.filtered ? "filter"
                : page.playlistsTab ? "playlist" : "lib-music"
        headline: MusicCtl.filtered
                  ? qsTr("Nothing matches these filters")
                  : page.songsTab ? qsTr("No songs here")
                  : page.playlistsTab ? qsTr("No music playlists yet")
                  : page.albumsTab ? qsTr("No albums here")
                  : qsTr("No artists here")
        // The artists view has a second answer even unfiltered: the other
        // endpoint. The other two genuinely have nothing to offer but a rescan
        // on the server, so they say so instead of pretending.
        body: MusicCtl.filtered
              ? qsTr("No music in this library is left once these filters are applied.")
              : page.albumsTab
              ? qsTr("Once your Emby server has scanned some music into this library, "
                     + "the albums show up here.")
              : page.songsTab
              ? qsTr("Once your Emby server has scanned some music into this library, "
                     + "the tracks show up here.")
              // Not "you have no playlists": the user may have plenty, all of
              // them film. This list is the ones the server files as audio, and
              // saying which is the difference between an empty shelf and a
              // broken page.
              : page.playlistsTab
              ? qsTr("Playlists you make from an album or a track appear here. "
                     + "Your video playlists stay under Playlists in the sidebar.")
              : (page.albumArtistMode
                 ? qsTr("Nothing is filed under an album artist in this library. "
                        + "Everyone who appears on a track is still listed.")
                 : qsTr("No artist appears on anything in this library."))
        actionText: MusicCtl.filtered ? qsTr("Clear filters")
                  : (page.artistsTab && page.albumArtistMode) ? qsTr("Show all artists")
                  : ""
        actionIcon: MusicCtl.filtered ? "close"
                  : (page.artistsTab && page.albumArtistMode) ? "user"
                  : ""
        onActionTriggered: {
            if (MusicCtl.filtered)
                MusicCtl.clearFilters()
            else
                MusicCtl.artistMode = "artists"
        }
    }

    // A page of results already on screen and the *next* page failed: say so in
    // place rather than throwing away everything the user has scrolled past.
    Rectangle {
        id: pagingError

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.pageMarginValue
        height: pagingRow.implicitHeight + Theme.spacingValue * 2
        visible: page.failed && page.shownCount > 0
        radius: Theme.radiusPanel
        color: Theme.surfaceOverlay
        border.width: 1
        border.color: Theme.hairline

        Row {
            id: pagingRow

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: Theme.spacingValue
            anchors.rightMargin: Theme.spacingValue
            spacing: Theme.spacingValue

            StrmIcon {
                anchors.verticalCenter: parent.verticalCenter
                name: "info"
                color: Theme.negative
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: pagingRow.width - Theme.iconSize - pagingRetry.width
                       - pagingRow.spacing * 2
                text: qsTr("Couldn't load more: %1").arg(page.currentTabErrorMessage)
                color: Theme.textSecondaryColor
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontSmall
                elide: Text.ElideRight
                maximumLineCount: 1
            }

            StrmButton {
                id: pagingRetry

                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Retry")
                iconName: "refresh"
                variant: "secondary"
                onClicked: {
                    if (page.songsTab)
                        MusicCtl.loadMoreSongs()
                    else if (page.playlistsTab)
                        MusicCtl.loadMorePlaylists()
                    else if (page.artistsTab)
                        MusicCtl.loadMoreArtists()
                    else
                        MusicCtl.loadMoreAlbums()
                }
            }
        }
    }
}
