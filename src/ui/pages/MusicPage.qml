pragma ComponentBehavior: Bound
import QtQuick
import StrmQt

// MusicPage — a music library's front door (ARCHITECTURE.md).
//
// Two views of the same library, not one grid with a filter: **Albums**
// (5,037 on the target server) and **Artists** (2,394 album artists, or 3,789
// if you count everyone who appears on anything). Both are square art, both
// page, and neither ever tries to hold the whole library — StrmGrid virtualises
// the rows and `nearEnd` pulls the next 100 before the user reaches the edge.
//
// Why a tab bar rather than two nav-rail destinations: they are two readings of
// one library, and the answer to "where is this record" is sometimes the album
// and sometimes the artist. Switching must not refetch, so both grids exist at
// once and each keeps its own scroll position and its own keyboard cursor; only
// one is visible, so the hidden one creates no delegates.
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

    readonly property bool albumArtistMode: MusicCtl.artistMode === "albumArtists"

    readonly property int albumTotal: MusicCtl.albums.totalRecordCount
    readonly property int artistTotal: MusicCtl.artists.totalRecordCount

    readonly property bool failed: MusicCtl.errorMessage.length > 0
    readonly property int shownCount: page.albumsTab ? albumsGrid.count : artistsGrid.count
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
    // The loading guard is for albums only, and it is not cosmetic: Main.qml
    // arms MusicCtl and calls loadAlbums() *before* pushing this page, so
    // without it the first request is always issued twice and the first reply
    // always thrown away by the controller's generation counter.
    function ensureAlbums() {
        if (page.scopeId.length > 0 && MusicCtl.albums.count === 0 && !MusicCtl.loading)
            MusicCtl.loadAlbums()
    }

    function ensureArtists() {
        if (page.scopeId.length > 0 && MusicCtl.artists.count === 0)
            MusicCtl.loadArtists()
    }

    Component.onCompleted: {
        if (page.libraryId.length > 0)
            MusicCtl.setLibrary(page.libraryId)
        page.ensureAlbums()
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

    function idOf(item) {
        return (item && item.itemId !== undefined) ? String(item.itemId) : ""
    }

    function nameOf(item) {
        return (item && item.name !== undefined) ? String(item.name) : ""
    }

    // ── Playing an album from the grid ─────────────────────────────────────
    // An album is a container, and Actions.playAll(albumId, "music") is the
    // wrong verb for it: playAll fixes SortBy=SortName, which queues a record
    // in alphabetical order by track title. The only ordering that is right is
    // the server's own disc/track order, which is what MusicCtl.openAlbum()
    // fetches — so "play this album" is load-its-tracks-then-queue-them, and
    // the queue verb is playAllFrom(), the same one the album page uses.
    //
    // The pending id is matched against MusicCtl.albumId before anything is
    // queued, so clicking ▸ on two albums in quick succession plays the second
    // one and never the first one's tracks under the second one's name.
    property string pendingPlayAlbumId: ""

    function playAlbum(item) {
        const id = page.idOf(item)
        if (id.length === 0)
            return
        page.pendingPlayAlbumId = id
        MusicCtl.openAlbum(id, page.nameOf(item))
    }

    function requestAlbum(item) {
        if (page.idOf(item).length === 0)
            return
        // Navigating supersedes any queued auto-play: the album page is about
        // to reuse the very same tracks model.
        page.pendingPlayAlbumId = ""
        Actions.openDetails(item)
    }

    function requestArtist(item) {
        if (page.idOf(item).length === 0)
            return
        Actions.openDetails(item)
    }

    Connections {
        target: MusicCtl.tracks

        function onCountChanged() {
            if (page.pendingPlayAlbumId.length === 0)
                return
            if (MusicCtl.albumId !== page.pendingPlayAlbumId || MusicCtl.tracks.count === 0)
                return
            const items = []
            for (let i = 0; i < MusicCtl.tracks.count; ++i)
                items.push(MusicCtl.tracks.get(i))
            page.pendingPlayAlbumId = ""
            Actions.playAllFrom(items, 0)
        }
    }

    Connections {
        target: MusicCtl

        // The open album moved somewhere this page did not send it: whatever
        // was waiting to be played is no longer what will arrive.
        function onAlbumChanged() {
            if (MusicCtl.albumId !== page.pendingPlayAlbumId)
                page.pendingPlayAlbumId = ""
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

        tabs: [{ text: qsTr("Albums") }, { text: qsTr("Artists") }]
        focus: !page.contentFocusable

        KeyNavigation.up: page.artistsTab ? albumArtistChip : null
        KeyNavigation.down: page.albumsTab ? albumsGrid : artistsGrid

        onTabSelected: index => {
            if (index === 1)
                page.ensureArtists()
        }
    }

    // Both grids are built and only one is shown. Hiding a GridView releases
    // its delegates, so the cost of the other tab is its model — which the
    // controller owns either way — and the benefit is that switching back
    // returns to the same scroll offset and the same focused card.
    StrmGrid {
        id: albumsGrid

        anchors.top: tabBar.bottom
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

        KeyNavigation.up: tabBar

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

        anchors.top: tabBar.bottom
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

        KeyNavigation.up: tabBar

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
        anchors.top: tabBar.bottom
        anchors.topMargin: Theme.spacingValue
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        visible: MusicCtl.loading && page.shownCount === 0
        shape: "grid"
    }

    EmptyState {
        anchors.top: tabBar.bottom
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
            if (page.albumsTab)
                MusicCtl.loadAlbums()
            else
                MusicCtl.loadArtists()
        }
    }

    EmptyState {
        anchors.top: tabBar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        visible: page.isEmpty && !page.failed
        iconName: "lib-music"
        headline: page.albumsTab ? qsTr("No albums here")
                                 : qsTr("No artists here")
        // The artists view has an action because it has a second answer: the
        // other endpoint. The albums view genuinely has nothing to offer but a
        // rescan on the server, so it says so instead of pretending.
        body: page.albumsTab
              ? qsTr("Once your Emby server has scanned some music into this library, "
                     + "the albums show up here.")
              : (page.albumArtistMode
                 ? qsTr("Nothing is filed under an album artist in this library. "
                        + "Everyone who appears on a track is still listed.")
                 : qsTr("No artist appears on anything in this library."))
        actionText: (!page.albumsTab && page.albumArtistMode) ? qsTr("Show all artists") : ""
        actionIcon: (!page.albumsTab && page.albumArtistMode) ? "user" : ""
        onActionTriggered: MusicCtl.artistMode = "artists"
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
                    if (page.albumsTab)
                        MusicCtl.loadMoreAlbums()
                    else
                        MusicCtl.loadMoreArtists()
                }
            }
        }
    }
}
