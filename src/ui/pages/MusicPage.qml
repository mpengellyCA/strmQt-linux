pragma ComponentBehavior: Bound
import QtQuick
import StrmQt

// MusicPage — a music library's front door (ARCHITECTURE.md).
//
// Three views of the same library, not one grid with a filter: **Albums**
// (5,037 on the target server), **Artists** (2,394 album artists, or 3,789 if
// you count everyone who appears on anything) and **Songs** (56,283). The first
// two are square art; the third is the shared TrackTable, because a song is a
// row and not a tile. All three page, and none ever tries to hold the whole
// library — StrmGrid and TrackTable both virtualise, and `nearEnd` pulls the
// next 100 before the user reaches the edge.
//
// Why a tab bar rather than three nav-rail destinations: they are three
// readings of one library, and the answer to "where is this record" is
// sometimes the album, sometimes the artist and sometimes the track. Switching
// must not refetch what is already loaded, so all three views exist at once and
// each keeps its own scroll position and its own keyboard cursor; only one is
// visible, so the hidden ones create no delegates.
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
// signal of its own. Opening a card is `Actions.openDetails(item)`, which
// Main.qml routes by item type — MusicAlbum to the album page, MusicArtist to
// the artist page, an Audio row to the album that holds it. One route, shared
// with every other page that opens a music item.
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

    // The controller's own vocabulary for the same three tabs. It owns a sort
    // per tab, so it has to be told which one is on screen; the tab bar stays
    // the source of truth for the INDEX and this is the one translation.
    readonly property string tabKey: page.songsTab ? "songs"
                                   : page.artistsTab ? "artists" : "albums"

    readonly property bool albumArtistMode: MusicCtl.artistMode === "albumArtists"

    readonly property int albumTotal: MusicCtl.albums.totalRecordCount
    readonly property int artistTotal: MusicCtl.artists.totalRecordCount
    readonly property int songTotal: MusicCtl.songs.totalRecordCount

    readonly property bool failed: MusicCtl.errorMessage.length > 0
    readonly property int shownCount: page.songsTab ? songsTable.count
                                    : page.albumsTab ? albumsGrid.count
                                    : artistsGrid.count
    readonly property bool isEmpty: page.shownCount === 0 && !MusicCtl.loading
    // Nothing below the tab bar can take focus while the first page is in
    // flight or the view is empty, so the tab bar holds it — a page that
    // arrives with the keyboard pointing at nothing is a page you cannot leave
    // without the mouse. The empty state's own action stays reachable by Tab.
    readonly property bool contentFocusable: page.shownCount > 0

    function formatCount(value) {
        return Number(value).toLocaleString(Qt.locale(), 'f', 0)
    }

    readonly property string headerSubtitle: {
        if (page.albumsTab)
            return page.albumTotal > 0 ? qsTr("%1 albums").arg(page.formatCount(page.albumTotal))
                                       : "";
        if (page.songsTab)
            return page.songTotal > 0 ? qsTr("%1 songs").arg(page.formatCount(page.songTotal))
                                      : "";
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
    // All three carry the `!MusicCtl.loading` guard, and it is not cosmetic on
    // any of them.
    //
    // For albums: Main.qml arms MusicCtl and calls loadAlbums() *before* pushing
    // this page, so without it the first request is always issued twice and the
    // first reply always thrown away by the controller's generation counter.
    //
    // For artists and songs: `MusicCtl.tab = …` ends in the controller's own
    // ensureCurrentTab(), which already issues the tab's first page when its
    // model is empty. The request is async, so `count` is still 0 on the very
    // next line — the guard the two used to have could not tell "nobody has
    // asked" from "somebody asked a microsecond ago". That cost a wasted 100-row
    // query against a 56,283-track library on every cold tab switch, and again
    // after every filter change, since a query change empties the other two
    // tabs. `loading` is true by then because fetchArtists()/fetchSongs() set it
    // before returning, which is exactly the distinction that was missing.
    function ensureAlbums() {
        if (page.scopeId.length > 0 && MusicCtl.albums.count === 0 && !MusicCtl.loading)
            MusicCtl.loadAlbums()
    }

    function ensureArtists() {
        if (page.scopeId.length > 0 && MusicCtl.artists.count === 0 && !MusicCtl.loading)
            MusicCtl.loadArtists()
    }

    function ensureSongs() {
        if (page.scopeId.length > 0 && MusicCtl.songs.count === 0 && !MusicCtl.loading)
            MusicCtl.loadSongs()
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
        MusicCtl.tab = page.tabKey
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

    // Play the Songs list from one row. The rows already loaded are the queue,
    // which is the same contract the album page's table has — a 56,283-row
    // queue is not what "play this song" means.
    function playSongFrom(index) {
        const model = MusicCtl.songs
        if (!model || index < 0 || index >= model.count)
            return
        const items = []
        for (let i = 0; i < model.count; ++i)
            items.push(model.get(i))
        Actions.playAllFrom(items, index)
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
        if (!ms || ms <= 0)
            return "–:––"
        const totalSeconds = Math.round(Number(ms) / 1000)
        const hours = Math.floor(totalSeconds / 3600)
        const minutes = Math.floor((totalSeconds / 60) % 60)
        const seconds = totalSeconds % 60
        const pad = v => (v < 10 ? "0" : "") + v
        return hours > 0 ? hours + ":" + pad(minutes) + ":" + pad(seconds)
                         : minutes + ":" + pad(seconds)
    }

    function idOf(item) {
        return (item && item.itemId !== undefined) ? String(item.itemId) : ""
    }

    // ── Playing an album from the grid ─────────────────────────────────────
    // One call, because "play this record" is a verb and lives in C++
    // (ARCHITECTURE.md rule 3). MusicController::playAlbum() fetches the
    // album's children into a scratch model of its own and hands ItemActions
    // the ordered items.
    //
    // This page used to do it by calling openAlbum() and watching the shared
    // `tracks` model fill behind a pending-id guard — which meant playing an
    // album navigated controller state, and would have fought the album page
    // the moment both were live.
    function playAlbum(item) {
        MusicCtl.playAlbum(page.idOf(item))
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
        // of. A verb wire-up rather than new machinery: ItemActions::shuffle()
        // pins SortBy=Random server-side, so the sample is drawn from the whole
        // library and not from the pages this grid happens to have loaded
        // (ARCHITECTURE.md rule 3 — shuffle exists once).
        //
        // It shuffles the LIBRARY, not the narrowed view: there is exactly one
        // implementation of shuffle and it takes a parent and a kind. Widening
        // it to carry genre and year is a change to a verb five other callers
        // share, so it is called for what it is rather than reimplemented here.
        StrmButton {
            id: shuffleAllButton

            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Shuffle")
            iconName: "shuffle"
            variant: "primary"
            enabled: page.scopeId.length > 0
            onClicked: Actions.shuffle(page.scopeId, "music")

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

        tabs: [{ text: qsTr("Albums") }, { text: qsTr("Artists") }, { text: qsTr("Songs") }]
        focus: !page.contentFocusable

        KeyNavigation.up: page.artistsTab ? albumArtistChip : shuffleAllButton
        KeyNavigation.down: filterBar

        onTabSelected: index => {
            // The controller keeps a sort per tab and fetches the tab's first
            // page if it has none, so telling it which tab is on screen is the
            // whole of the switch. The ensure* calls stay for the case it
            // cannot cover: a page that opened before anything was asked of the
            // controller at all.
            MusicCtl.tab = index === 2 ? "songs" : index === 1 ? "artists" : "albums"
            if (index === 1)
                page.ensureArtists()
            else if (index === 2)
                page.ensureSongs()
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
        downTarget: page.songsTab ? songsTable : page.artistsTab ? artistsGrid : albumsGrid
        upTarget: tabBar
    }

    // Both grids are built and only one is shown. Hiding a GridView releases
    // its delegates, so the cost of the other tab is its model — which the
    // controller owns either way — and the benefit is that switching back
    // returns to the same scroll offset and the same focused card.
    StrmGrid {
        id: albumsGrid

        anchors.top: filterBar.bottom
        anchors.topMargin: Theme.spacingTight
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        visible: page.albumsTab
        enabled: page.albumsTab

        gridModel: MusicCtl.albums
        cardVariant: "square"
        emptyText: ""
        prefetchThreshold: 30
        focus: page.albumsTab && albumsGrid.count > 0

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
        onMenuRequested: (index, mx, my) => musicMenu.popupFor("album", page.albumAt(index),
                                                               mx, my)
    }

    StrmGrid {
        id: artistsGrid

        anchors.top: filterBar.bottom
        anchors.topMargin: Theme.spacingTight
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        visible: page.artistsTab
        enabled: page.artistsTab

        gridModel: MusicCtl.artists
        cardVariant: "square"
        emptyText: ""
        prefetchThreshold: 30
        focus: page.artistsTab && artistsGrid.count > 0

        KeyNavigation.up: filterBar.entryItem

        onNearEnd: if (MusicCtl.canLoadMoreArtists) MusicCtl.loadMoreArtists()

        onItemActivated: index => page.requestArtist(page.artistAt(index))
        // An artist is not a parent the server will hand tracks back for, and
        // there is no ItemActions verb that queues by artist id (see the
        // hand-back note in this wave's report), so ▸ opens the discography —
        // where every playable thing an artist has actually lives — rather than
        // firing a query that would come back empty.
        onItemPlayRequested: index => page.requestArtist(page.artistAt(index))
        onItemFavoriteToggled: index => {
            const item = page.artistAt(index)
            if (item)
                Actions.toggleFavorite(item)
        }
        onMenuRequested: (index, mx, my) => musicMenu.popupFor("artist", page.artistAt(index),
                                                               mx, my)
    }

    // ── Songs ──────────────────────────────────────────────────────────────
    // The shared TrackTable (ARCHITECTURE.md), not a fourth inline delegate:
    // it was extracted in the previous phase precisely so this tab would not
    // become one. One tab stop, Up/Down and the page keys owned internally,
    // and type-to-jump — which a 56,283-row list needs more than any other
    // table in the app.
    //
    // Two things differ from an album's table and both are properties of the
    // shared component rather than a fork of it:
    //
    //  * `alwaysShowArtist` — an album's table earns its artist column by
    //    walking the model and asking "does any row differ from the album's
    //    credit". Here the answer is known in advance and the walk would repeat
    //    over a growing model on every page fetch.
    //  * `discGrouping: false` — a disc banner is a statement about one record.
    //
    // The album goes on the row's second line rather than into a column of its
    // own: TrackRow already draws a `secondary` line (it is the shape the queue
    // and playlist panes use), and a fifth column would leave the title nothing.
    //
    // The number column is the row's ORDINAL, not its track number: under a
    // name or date sort a column reading 11, 3, 7 down the page says nothing,
    // and TrackRow documents the slot as "track number or ordinal".
    readonly property int songRowHeight: Theme.scale(52)
    readonly property int songNumberColumn: Theme.scale(46)
    readonly property int songDurationColumn: Theme.scale(64)
    readonly property int songVerbsColumn: Theme.scale(72)
    readonly property int songArtistColumn: songsTable.showArtistColumn ? Theme.scale(220) : 0

    TrackTable {
        id: songsTable

        anchors.top: filterBar.bottom
        anchors.topMargin: Theme.spacingTight
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.pageMarginValue
        anchors.rightMargin: Theme.pageMarginValue
        anchors.bottomMargin: Theme.spacingValue
        visible: page.songsTab
        enabled: page.songsTab
        focus: page.songsTab && songsTable.count > 0

        model: MusicCtl.songs
        rowHeight: page.songRowHeight
        discGrouping: false
        artistRule: false
        alwaysShowArtist: true

        KeyNavigation.up: filterBar.entryItem

        onActivated: index => page.playSongFrom(index)

        // The same 30-row lead the grids use. TrackTable throttles nearEnd()
        // per loaded count, so this may call loadMore() unconditionally.
        prefetchThreshold: 30
        onNearEnd: if (MusicCtl.canLoadMoreSongs) MusicCtl.loadMoreSongs()

        delegate: TrackRow {
            id: songRow

            required property int index
            required property var model

            readonly property string trackId: songRow.model.itemId !== undefined
                                              ? String(songRow.model.itemId) : ""

            width: songsTable.width

            rowHeight: page.songRowHeight
            numberColumn: page.songNumberColumn
            durationColumn: page.songDurationColumn
            verbsColumn: page.songVerbsColumn
            artistColumn: page.songArtistColumn

            title: songRow.model.name !== undefined ? String(songRow.model.name) : ""
            // The album, as the second line. `posterUrl` straight from the
            // model — MediaItem::coverSource() already resolves a track to its
            // album's cover, so a rip with no embedded art still draws one.
            secondary: songRow.model.album !== undefined && songRow.model.album !== null
                       ? String(songRow.model.album) : ""
            artist: songsTable.shownArtistFor(songRow.model)
            durationText: page.formatDuration(songRow.model.runtimeMs)
            number: songRow.index + 1
            coverUrl: songRow.model.posterUrl !== undefined
                      ? String(songRow.model.posterUrl) : ""
            showCover: true

            current: songsTable.currentIndex === songRow.index && songsTable.activeFocus
            playing: songRow.trackId.length > 0 && songRow.trackId === page.nowPlayingId
            favorite: songRow.model.favorite === true
            showFavorite: true
            showMenu: true
            verbsRevealed: songRow.hovered || songRow.favorite

            onActivated: {
                songsTable.currentIndex = songRow.index
                songsTable.forceActiveFocus(Qt.MouseFocusReason)
                page.playSongFrom(songRow.index)
            }

            onFavoriteToggled: {
                const item = page.songAt(songRow.index)
                if (item)
                    Actions.toggleFavorite(item)
            }

            onMenuRequested: (sceneX, sceneY) => {
                songMenu.popupForItemNoDetails(page.songAt(songRow.index), sceneX, sceneY)
            }
        }
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
        // Back to the table the row was picked from, not to the top of the
        // page: an overlay that drops the keyboard somewhere else is the same
        // bug as one that never gives it back.
        onDismissed: {
            if (page.songsTab && songsTable.count > 0)
                songsTable.forceActiveFocus(Qt.OtherFocusReason)
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
    // Not ItemMenu: that list is built for playable leaves and containers it
    // knows about (Series / Season / BoxSet), and for a MusicAlbum it would
    // offer Play → Actions.play(albumId), which asks the player to stream a
    // folder. An album's verbs are the album page's verbs, and an artist's are
    // narrower still, so the list is stated here rather than widened there —
    // ItemMenu is not this wave's file to change.
    StrmMenu {
        id: musicMenu

        property string mode: "album"
        property var target: ({})
        property var verbs: []

        function popupFor(kind, item, sceneX, sceneY) {
            if (!item || !item.itemId)
                return
            const id = String(item.itemId)
            const favorite = Actions.isFavorite(id)
            const acts = []
            const vs = []
            function push(action, verb) {
                acts.push(action)
                vs.push(verb)
            }

            if (kind === "album") {
                push({ text: qsTr("Play"), iconName: "play" }, "play")
                push({ text: qsTr("Shuffle"), iconName: "shuffle" }, "shuffle")
                push({ separator: true }, "")
                push({ text: qsTr("Open album"), iconName: "lib-music" }, "open")
            } else {
                push({ text: qsTr("Open artist"), iconName: "user" }, "open")
            }
            push({ separator: true }, "")
            push({ text: favorite ? qsTr("Remove from favourites")
                                  : qsTr("Add to favourites"),
                   iconName: favorite ? "heart-filled" : "heart",
                   checked: favorite }, "favorite")

            musicMenu.mode = kind
            musicMenu.target = item
            musicMenu.verbs = vs
            musicMenu.actions = acts
            musicMenu.popupAt(sceneX, sceneY)
        }

        onTriggered: index => {
            const item = musicMenu.target
            const verb = (index >= 0 && index < musicMenu.verbs.length)
                       ? musicMenu.verbs[index] : ""
            if (!verb || !item || !item.itemId)
                return
            const id = String(item.itemId)
            switch (verb) {
            case "play":
                page.playAlbum(item)
                break
            case "shuffle":
                // Correct for an album exactly where playAll is not: SortBy is
                // Random, so nothing depends on the server's ordering.
                Actions.shuffle(id, "music")
                break
            case "open":
                if (musicMenu.mode === "album")
                    page.requestAlbum(item)
                else
                    page.requestArtist(item)
                break
            case "favorite":
                Actions.setFavorite(id, !Actions.isFavorite(id))
                break
            default:
                break
            }
        }
    }

    // ── Page states ────────────────────────────────────────────────────────
    LoadingState {
        anchors.top: filterBar.bottom
        anchors.topMargin: Theme.spacingValue
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        visible: MusicCtl.loading && page.shownCount === 0
        // A skeleton has to look like what is coming: rows for the song list,
        // tiles for the two grids.
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
        body: MusicCtl.errorMessage
        actionText: qsTr("Retry")
        actionIcon: "refresh"
        onActionTriggered: {
            if (page.songsTab)
                MusicCtl.loadSongs()
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
        iconName: MusicCtl.filtered ? "filter" : "lib-music"
        headline: MusicCtl.filtered
                  ? qsTr("Nothing matches these filters")
                  : page.songsTab ? qsTr("No songs here")
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
                text: qsTr("Couldn't load more: %1").arg(MusicCtl.errorMessage)
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
                    else if (page.artistsTab)
                        MusicCtl.loadMoreArtists()
                    else
                        MusicCtl.loadMoreAlbums()
                }
            }
        }
    }
}
