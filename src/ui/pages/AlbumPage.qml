pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import StrmQt

// AlbumPage — one record (ARCHITECTURE.md).
//
// A square cover, who made it, when, how long it runs, and then the thing the
// page actually exists for: the track list. That list is a **table**, not a
// grid of cards — dense rows, one line each, numbers and durations in mono so
// the columns line up down the whole record.
//
// Three decisions worth stating, because each one is a place this could
// plausibly have gone wrong:
//
//  1. **Order is the server's.** MusicController fetches an album's children
//     without a sort key precisely because Emby already returns them in disc
//     then track order, so nothing here re-sorts. That is also why "Play" is
//     `Actions.playAllFrom(tracks, 0)` and not `Actions.playAll(albumId)` —
//     playAll pins SortBy=SortName, which would queue a record alphabetically
//     by track title.
//  2. **The per-track artist appears only when it differs from the album's.**
//     Printing "Opeth" on all six tracks of an Opeth album is noise; printing
//     it on a compilation is the only way to read the record. The column is
//     decided once for the whole table, so it never appears and disappears
//     row by row and the columns stay aligned.
//  3. **Disc numbers only when there is more than one disc.** Most albums are
//     single-disc and `ParentIndexNumber` is frequently absent altogether, so a
//     "Disc 1" banner above every record would be pure furniture.
//
// (2) and (3) are `TrackTable`'s now — this page was where they were worked
// out, and lifting them there is what stopped the queue panel and the playlist
// pane from having to work them out again.
//
// Navigation contract: the page pushes nothing. Opening the album artist goes
// through `Actions.openDetails()`, which Main.qml routes by item type — a
// MusicArtist lands on the artist page, exactly as an album card does from the
// music library. No page-local navigation signal, and no second route to the
// same destination.
FocusScope {
    id: page

    // ── Contract ───────────────────────────────────────────────────────────
    // The album's own item map, as MediaItemModel::get() produces it, handed
    // over by Main.qml at push time. MusicCtl publishes only the open album's
    // id and name, so the cover, the year and the album artist have to travel
    // with the item — the same way DetailsPage receives `item`.
    //
    // Everything below degrades to the controller's id/name when the map is
    // absent, so a page constructed with no properties (the self-test) is
    // still a valid page rather than a crash.
    property var albumItem: ({})

    function mapString(key) {
        return (page.albumItem && page.albumItem[key] !== undefined && page.albumItem[key] !== null)
                ? String(page.albumItem[key]) : ""
    }

    readonly property string albumId: page.mapString("itemId").length > 0
                                      ? page.mapString("itemId") : MusicCtl.albumId
    readonly property string albumName: page.mapString("name").length > 0
                                        ? page.mapString("name") : MusicCtl.albumName
    readonly property string coverUrl: page.mapString("posterUrl")
    readonly property int albumYear: (page.albumItem && page.albumItem.year !== undefined)
                                     ? Number(page.albumItem.year) : 0
    // The map's album artist, before the fallback to what the tracks say.
    readonly property string mapAlbumArtist: page.mapString("albumArtist")
    // ArtistItems ids, in the same order as the names. Present on an album only
    // when the server sent them; without one the credit is text, not a link,
    // and LinkChip renders exactly that (`linked: false`) rather than a pill
    // that looks clickable and does nothing.
    readonly property var albumArtistIds: (page.albumItem && page.albumItem.artistIds !== undefined)
                                          ? page.albumItem.artistIds : []
    readonly property string albumArtistId: page.albumArtistIds.length > 0
                                            ? String(page.albumArtistIds[0]) : ""

    readonly property int trackCount: MusicCtl.tracks.count > 0
                                      ? MusicCtl.tracks.count
                                      : ((page.albumItem && page.albumItem.childCount !== undefined)
                                         ? Number(page.albumItem.childCount) : 0)

    readonly property bool hasTracks: MusicCtl.tracks.count > 0

    // ── Derived once per track load ────────────────────────────────────────
    // The one pass over the loaded tracks — running time, multi-disc, where the
    // discs start, and whether the artist column is worth a column at all —
    // now lives in TrackTable, which does it once per model load rather than
    // per delegate. The album's credit still starts here, because only this
    // page has the album's own item map; the table falls back to what the first
    // track says when the map carried no album artist.
    readonly property string albumArtistName: trackList.albumArtistName

    Component.onCompleted: page.syncFavorite()

    // Likewise: an album page reused for a second album re-reads both.
    onAlbumIdChanged: {
        page.syncFavorite()
        page.trackFetchTimedOut = false
    }

    // ── Formatting ─────────────────────────────────────────────────────────
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

    function formatTotal(ms) {
        if (!ms || ms <= 0)
            return ""
        const minutes = Math.round(Number(ms) / 60000)
        return minutes >= 60 ? qsTr("%1 h %2 min").arg(Math.floor(minutes / 60))
                                                  .arg(minutes % 60)
                             : qsTr("%1 min").arg(minutes)
    }

    // ── Favourites ─────────────────────────────────────────────────────────
    // MusicController's models are NOT registered with ItemActions (see this
    // wave's hand-back note), which has two consequences this page has to work
    // around and neither of which it can fix from QML:
    //
    //   · Actions.isFavorite() knows nothing about a track or an album until
    //     something in *this session* changes it, so the model role — what the
    //     server actually said — is the baseline, not that lookup;
    //   · the optimistic patch ItemActions applies after a toggle never reaches
    //     MusicCtl.tracks, so the role goes stale the moment it is toggled.
    //
    // So: role for the baseline, this overlay for what has changed since. The
    // map is REPLACED rather than mutated because a mutated object notifies
    // nothing and every heart on screen would keep its old state.
    //
    // Registering the four music models in Application.cpp removes the need for
    // all of it, and the overlay then simply agrees with the role.
    property var favoriteOverrides: ({})
    property bool albumFavorite: false

    function favoriteOf(itemId, fallback) {
        if (itemId.length > 0 && page.favoriteOverrides[itemId] !== undefined)
            return page.favoriteOverrides[itemId] === true
        return fallback === true
    }

    function syncFavorite() {
        page.albumFavorite = page.albumId.length > 0
                && page.favoriteOf(page.albumId,
                                   page.albumItem && page.albumItem.favorite === true)
    }

    Connections {
        target: Actions
        function onFavoriteChanged(itemId, favorite) {
            const next = Object.assign({}, page.favoriteOverrides)
            next[itemId] = favorite
            page.favoriteOverrides = next
            if (itemId === page.albumId)
                page.albumFavorite = favorite
        }
    }

    // ── Track verbs ────────────────────────────────────────────────────────
    function trackItems() {
        const model = MusicCtl.tracks
        const out = []
        for (let i = 0; i < model.count; ++i)
            out.push(model.get(i))
        return out
    }

    function trackIds() {
        const model = MusicCtl.tracks
        const out = []
        for (let i = 0; i < model.count; ++i) {
            const entry = model.get(i)
            if (entry.itemId !== undefined)
                out.push(String(entry.itemId))
        }
        return out
    }

    function trackAt(index) {
        const model = MusicCtl.tracks
        if (!model || index < 0 || index >= model.count)
            return null
        return model.get(index)
    }

    // The whole point of the table: the album is the queue, and the clicked row
    // is where it starts.
    function playFrom(index) {
        if (index < 0 || index >= MusicCtl.tracks.count)
            return
        Actions.playAllFrom(page.trackItems(), index)
    }

    // ── What is playing right now ──────────────────────────────────────────
    // Reading queue.currentIndex is what makes this re-evaluate: it is a
    // notifying property and currentItem() is a plain lookup that would never
    // update on its own.
    readonly property string nowPlayingId: {
        const queue = PlayerCtl.queue;
        if (!queue || queue.currentIndex < 0)
            return "";
        const current = queue.currentItem();
        return (current && current.itemId !== undefined) ? String(current.itemId) : "";
    }

    // ── Track fetch state ──────────────────────────────────────────────────
    // MusicController::openAlbum() reports neither `loading` nor
    // `errorMessage` (see this wave's hand-back note), so this page cannot ask
    // whether the fetch is in flight or has failed — it can only observe that
    // no tracks have arrived. A skeleton that never resolves is the worst of
    // the available answers, so after a grace period the page offers the
    // request again instead of shimmering forever.
    property bool trackFetchTimedOut: false

    readonly property bool awaitingTracks: page.albumId.length > 0 && !page.hasTracks
                                           && !page.trackFetchTimedOut

    Timer {
        running: page.albumId.length > 0 && !page.hasTracks
        interval: 12000
        onTriggered: page.trackFetchTimedOut = true
    }

    Connections {
        target: MusicCtl
        function onAlbumChanged() { page.trackFetchTimedOut = false }
    }

    // ── Hero ───────────────────────────────────────────────────────────────
    Item {
        id: hero

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: Theme.pageMarginValue
        anchors.rightMargin: Theme.pageMarginValue
        anchors.topMargin: Theme.spacingLoose
        height: Math.max(cover.height, meta.implicitHeight)

        Rectangle {
            id: cover

            anchors.left: parent.left
            anchors.top: parent.top
            width: Theme.scale(212)
            height: width
            radius: Theme.radiusCardValue
            color: Theme.surfaceColor
            border.width: 1
            border.color: Theme.hairline
            clip: true

            Image {
                anchors.fill: parent
                anchors.margins: 1
                source: page.coverUrl
                sourceSize.width: Math.round(cover.width * Screen.devicePixelRatio)
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                cache: true
                opacity: status === Image.Ready ? 1 : 0

                Behavior on opacity {
                    NumberAnimation {
                        duration: Theme.animNormalMs
                        easing.type: Theme.easeStandard
                    }
                }
            }

            // Records have covers; when this one does not, the placeholder says
            // "music" rather than leaving a hole the size of a sleeve.
            StrmIcon {
                anchors.centerIn: parent
                visible: page.coverUrl.length === 0
                name: "lib-music"
                size: Theme.scale(56)
                color: Theme.textTertiary
            }
        }

        Column {
            id: meta

            anchors.left: cover.right
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: Theme.spacingLoose
            spacing: Theme.spacingTight

            Text {
                width: parent.width
                text: page.albumName.length > 0 ? page.albumName : qsTr("Album")
                color: Theme.textPrimaryColor
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.fontHeading
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                maximumLineCount: 2
                wrapMode: Text.WordWrap
            }

            // ── The credit ─────────────────────────────────────────────────
            // A LinkChip, because the album artist is a destination, not a
            // filter — and it is wrapped in a focusable Item because LinkChip
            // is designed for a view that owns the arrow keys and this one has
            // no such view. Hover still only previews; the ring follows the
            // keyboard.
            Item {
                id: artistLink

                width: artistChip.implicitWidth
                height: artistChip.implicitHeight
                visible: page.albumArtistName.length > 0
                activeFocusOnTab: page.albumArtistId.length > 0

                KeyNavigation.down: playButton

                Keys.onReturnPressed: event => {
                    if (!event.isAutoRepeat)
                        artistChip.activated()
                }
                Keys.onEnterPressed: event => {
                    if (!event.isAutoRepeat)
                        artistChip.activated()
                }

                LinkChip {
                    id: artistChip

                    anchors.fill: parent
                    label: page.albumArtistName
                    iconName: "user"
                    linked: page.albumArtistId.length > 0
                    highlighted: artistLink.activeFocus
                    // A synthesized map rather than a fetched one: Main.qml's
                    // music router reads only the type, the id and the name,
                    // and those are exactly what an album's credit carries.
                    onActivated: Actions.openDetails({
                        "itemId": page.albumArtistId,
                        "name": page.albumArtistName,
                        "type": "MusicArtist"
                    })
                }
            }

            // Year · tracks · running time. The running time is a readout, so
            // it is set in mono like every other number the app reports
            // (ARCHITECTURE.md).
            Row {
                spacing: Theme.spacingTight

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: text.length > 0
                    text: {
                        const parts = [];
                        if (page.albumYear > 0)
                            parts.push(String(page.albumYear));
                        if (page.trackCount > 0)
                            parts.push(qsTr("%1 tracks").arg(page.trackCount));
                        return parts.join("  ·  ");
                    }
                    color: Theme.textSecondaryColor
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontBodySize
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: trackList.totalRuntimeMs > 0
                    text: "·"
                    color: Theme.textTertiary
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontBodySize
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: trackList.totalRuntimeMs > 0
                    text: page.formatTotal(trackList.totalRuntimeMs)
                    color: Theme.textSecondaryColor
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSmall
                }
            }

            Item {
                width: 1
                height: Theme.spacingTight
            }

            Row {
                spacing: Theme.spacingTight

                StrmButton {
                    id: playButton

                    text: qsTr("Play")
                    iconName: "play"
                    variant: "primary"
                    enabled: page.hasTracks
                    onClicked: page.playFrom(0)

                    KeyNavigation.up: artistLink
                    KeyNavigation.right: shuffleButton
                    KeyNavigation.down: trackList
                }

                StrmButton {
                    id: shuffleButton

                    text: qsTr("Shuffle")
                    iconName: "shuffle"
                    // Correct with only an id: SortBy=Random means nothing
                    // depends on the order the server would otherwise return.
                    enabled: page.albumId.length > 0
                    onClicked: Actions.shuffle(page.albumId, "music")

                    KeyNavigation.up: artistLink
                    KeyNavigation.left: playButton
                    KeyNavigation.right: playlistButton
                    KeyNavigation.down: trackList
                }

                StrmButton {
                    id: playlistButton

                    text: qsTr("Add to playlist")
                    iconName: "playlist"
                    enabled: page.hasTracks
                    // The album's TRACKS, not the album: what a playlist holds
                    // is playable items, and expanding a container id is the
                    // server's business rather than something to assume.
                    onClicked: playlistPicker.show(page.albumName, page.trackIds())

                    KeyNavigation.up: artistLink
                    KeyNavigation.left: shuffleButton
                    KeyNavigation.right: favoriteButton
                    KeyNavigation.down: trackList
                }

                StrmIconButton {
                    id: favoriteButton

                    anchors.verticalCenter: parent.verticalCenter
                    iconName: page.albumFavorite ? "heart-filled" : "heart"
                    checked: page.albumFavorite
                    enabled: page.albumId.length > 0
                    tooltip: page.albumFavorite ? qsTr("Remove from favourites")
                                                : qsTr("Add to favourites")
                    onClicked: Actions.setFavorite(page.albumId, !page.albumFavorite)

                    KeyNavigation.up: artistLink
                    KeyNavigation.left: playlistButton
                    KeyNavigation.down: trackList
                }
            }
        }
    }

    // ── Column metrics ─────────────────────────────────────────────────────
    // One set of numbers for the header strip and every row, so the table
    // cannot drift out of alignment with its own headings.
    readonly property int rowHeight: Theme.scale(38)
    readonly property int discHeaderHeight: Theme.scale(32)
    readonly property int numberColumn: Theme.scale(46)
    readonly property int durationColumn: Theme.scale(64)
    readonly property int verbsColumn: Theme.scale(72)
    readonly property int artistColumn: trackList.showArtistColumn ? Theme.scale(220) : 0

    // ── Table head ─────────────────────────────────────────────────────────
    // A gear label, in mono and letterspaced, exactly as the library's size
    // control labels itself: it names the columns without competing with the
    // album title above it.
    Item {
        id: tableHead

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: hero.bottom
        anchors.leftMargin: Theme.pageMarginValue
        anchors.rightMargin: Theme.pageMarginValue
        anchors.topMargin: Theme.spacingLoose
        height: page.hasTracks ? Theme.scale(26) : 0
        visible: page.hasTracks

        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: page.numberColumn - Theme.spacingTight
            horizontalAlignment: Text.AlignRight
            text: "#"
            color: Theme.textTertiary
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontCaption
            font.letterSpacing: Theme.fontCaption * Theme.trackLabel
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: page.numberColumn + Theme.spacingValue
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("TITLE")
            color: Theme.textTertiary
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontCaption
            font.letterSpacing: Theme.fontCaption * Theme.trackLabel
        }

        Text {
            anchors.right: parent.right
            anchors.rightMargin: page.durationColumn + page.verbsColumn + Theme.spacingValue
            anchors.verticalCenter: parent.verticalCenter
            visible: trackList.showArtistColumn
            width: page.artistColumn
            text: qsTr("ARTIST")
            color: Theme.textTertiary
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontCaption
            font.letterSpacing: Theme.fontCaption * Theme.trackLabel
            elide: Text.ElideRight
        }

        Text {
            anchors.right: parent.right
            anchors.rightMargin: page.verbsColumn
            anchors.verticalCenter: parent.verticalCenter
            width: page.durationColumn
            horizontalAlignment: Text.AlignRight
            // A clock glyph would need a column of its own; the mono label is
            // the same width as the values under it.
            text: qsTr("TIME")
            color: Theme.textTertiary
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontCaption
            font.letterSpacing: Theme.fontCaption * Theme.trackLabel
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.hairline
        }
    }

    // ── The tracks ─────────────────────────────────────────────────────────
    // The shared table (ARCHITECTURE.md): one tab stop, Up/Down and the page
    // keys owned internally, disc grouping and the artist-column rule decided
    // once above the rows, and type-to-jump for a box set that arrow keys
    // cannot navigate.
    TrackTable {
        id: trackList

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: tableHead.bottom
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.pageMarginValue
        anchors.rightMargin: Theme.pageMarginValue
        anchors.topMargin: Theme.spacingTight
        anchors.bottomMargin: Theme.spacingValue
        focus: page.hasTracks
        visible: page.hasTracks
        model: MusicCtl.tracks
        rowHeight: page.rowHeight

        discGrouping: true
        artistRule: true
        // The album's own credit, when the item map carried one. Empty means
        // "ask the first track", which is what the table then does.
        albumArtist: page.mapAlbumArtist

        KeyNavigation.up: playButton

        onActivated: index => page.playFrom(index)

        delegate: TrackRow {
            id: trackRow

            required property int index
            required property var model

            readonly property string trackId: trackRow.model.itemId !== undefined
                                              ? String(trackRow.model.itemId) : ""

            width: trackList.width

            rowHeight: page.rowHeight
            discHeaderHeight: page.discHeaderHeight
            numberColumn: page.numberColumn
            durationColumn: page.durationColumn
            verbsColumn: page.verbsColumn
            artistColumn: page.artistColumn

            title: trackRow.model.name !== undefined ? String(trackRow.model.name) : ""
            // The rule: only when it differs from the album's own credit, and
            // only where the table decided there is a column at all.
            artist: trackList.shownArtistFor(trackRow.model)
            durationText: page.formatDuration(trackRow.model.runtimeMs)
            number: trackRow.model.indexNumber !== undefined
                    ? Number(trackRow.model.indexNumber) : -1
            discNumber: trackList.discFor(trackRow.index)

            current: trackList.currentIndex === trackRow.index && trackList.activeFocus
            playing: trackRow.trackId.length > 0 && trackRow.trackId === page.nowPlayingId
            favorite: page.favoriteOf(trackRow.trackId, trackRow.model.favorite === true)
            showFavorite: true
            showMenu: true
            // A favourited row keeps its filled heart on show; the rest of the
            // verbs belong to the pointer.
            verbsRevealed: trackRow.hovered || trackRow.favorite

            onActivated: {
                trackList.currentIndex = trackRow.index
                trackList.forceActiveFocus(Qt.MouseFocusReason)
                page.playFrom(trackRow.index)
            }

            onFavoriteToggled: {
                const item = page.trackAt(trackRow.index)
                if (item)
                    Actions.toggleFavorite(item)
            }

            onMenuRequested: (sceneX, sceneY) => {
                trackMenu.popupForItemNoDetails(page.trackAt(trackRow.index), sceneX, sceneY)
            }
        }
    }

    // ── Menus and overlays ─────────────────────────────────────────────────
    // The shared item menu, minus "Details": for a track, the album page IS the
    // details page, and the generic one is built around a backdrop, a cast and
    // a similar-items rail that a three-minute song has none of.
    ItemMenu {
        id: trackMenu

        allowAddToPlaylist: true
        onAddToPlaylistRequested: item => {
            const id = (item && item.itemId !== undefined) ? String(item.itemId) : ""
            const name = (item && item.name !== undefined) ? String(item.name) : ""
            if (id.length > 0)
                playlistPicker.show(name, [id])
        }
    }

    // ── Add to playlist ────────────────────────────────────────────────────
    // The same type-and-pick panel DetailsPage uses, because this server has
    // ~1,500 playlists and a menu with 1,500 rows is a scrollbar with words on
    // it. Inline for the reason that file states: a QML file that is not
    // registered in CMake is invisible to the module, and registering one is
    // not this page's to do. Generalised on one axis only — it files a LIST of
    // ids, so the same panel serves one track and a whole record.
    component PlaylistPicker: FocusScope {
        id: picker

        property string subject: ""
        property var targetIds: []
        property bool opened: false
        property var records: []
        property var rows: []
        // A write of this page's is in flight, so PlaylistCtl's global results
        // can be told apart from an edit made somewhere else.
        property bool pending: false

        signal dismissed

        function show(subject, ids): void {
            if (!ids || ids.length === 0)
                return
            picker.subject = subject ? subject : ""
            picker.targetIds = ids
            picker.opened = true
            pickerField.text = ""
            if (PlaylistCtl.playlists.count === 0)
                PlaylistCtl.refresh()
            else
                picker.rebuildRecords()
            pickerField.forceActiveFocus(Qt.OtherFocusReason)
        }

        function dismiss(): void {
            if (!picker.opened)
                return
            picker.opened = false
            picker.dismissed()
        }

        function rebuildRecords(): void {
            const model = PlaylistCtl.playlists
            const out = []
            for (let i = 0; i < model.count; ++i) {
                const entry = model.get(i)
                const name = entry.name !== undefined ? String(entry.name) : ""
                out.push({
                    "create": false,
                    "id": entry.itemId !== undefined ? String(entry.itemId) : "",
                    "name": name,
                    "lower": name.toLowerCase()
                })
            }
            picker.records = out
            picker.rebuild()
        }

        function rebuild(): void {
            const typed = pickerField.text.trim()
            const needle = typed.toLowerCase()
            const source = picker.records
            const out = []
            let exact = false
            for (let i = 0; i < source.length; ++i) {
                if (source[i].lower === needle)
                    exact = true
                if (needle.length === 0 || source[i].lower.indexOf(needle) >= 0)
                    out.push(source[i])
            }
            if (typed.length > 0 && !exact)
                out.unshift({ "create": true, "id": "", "name": typed, "lower": needle })
            picker.rows = out
            pickerList.currentIndex = out.length > 0 ? 0 : -1
        }

        function activate(index): void {
            if (index < 0 || index >= picker.rows.length || picker.targetIds.length === 0)
                return
            const row = picker.rows[index]
            picker.pending = true
            if (row.create)
                PlaylistCtl.create(row.name, picker.targetIds)
            else
                PlaylistCtl.addItems(row.id, picker.targetIds)
            picker.dismiss()
        }

        anchors.fill: parent
        // `opened` as well as the animated opacity: forceActiveFocus() runs in
        // the same call as show(), and an item that is still invisible at that
        // moment does not take focus.
        visible: picker.opened || picker.opacity > 0.01
        enabled: picker.opened
        opacity: picker.opened ? 1.0 : 0.0

        Behavior on opacity {
            NumberAnimation { duration: Theme.animFastMs; easing.type: Theme.easeStandard }
        }

        Connections {
            target: PlaylistCtl.playlists
            function onCountChanged() {
                if (picker.opened)
                    picker.rebuildRecords()
            }
        }

        Rectangle {
            anchors.fill: parent
            color: Theme.scrimColor

            TapHandler {
                gesturePolicy: TapHandler.ReleaseWithinBounds
                onTapped: picker.dismiss()
            }
        }

        Rectangle {
            id: pickerSurface

            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: Math.round(parent.height * 0.14)
            width: Math.min(parent.width - Theme.pageMarginValue * 2, Theme.scale(620))
            height: pickerHead.height + pickerList.height + pickerHint.height
                    + Theme.spacingValue
            radius: Theme.radiusPanel
            color: Theme.surfaceOverlay
            border.width: 1
            border.color: Theme.hairline

            // Keeps clicks inside the panel off the scrim behind it.
            TapHandler { gesturePolicy: TapHandler.ReleaseWithinBounds }

            Column {
                id: pickerHead

                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: Theme.spacingTight
                spacing: Theme.spacingTight

                Text {
                    width: parent.width
                    leftPadding: Theme.spacingTight
                    topPadding: Theme.spacingTight
                    text: picker.subject.length > 0
                          ? qsTr("Add “%1” to…").arg(picker.subject)
                          : qsTr("Add to playlist")
                    color: Theme.textPrimaryColor
                    font.family: Theme.fontDisplay
                    font.pixelSize: Theme.fontTitle
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }

                StrmSearchField {
                    id: pickerField

                    width: parent.width
                    implicitHeight: Theme.controlHeightLarge
                    placeholderText: qsTr("Find a playlist, or type a new name…")

                    onTextEdited: picker.rebuild()
                    onCleared: picker.rebuild()
                    onEscapePressed: picker.dismiss()
                    onAccepted: picker.activate(pickerList.currentIndex)

                    Keys.onUpPressed: {
                        if (pickerList.count > 0)
                            pickerList.currentIndex = Math.max(0, pickerList.currentIndex - 1)
                    }
                    Keys.onDownPressed: {
                        if (pickerList.count > 0)
                            pickerList.currentIndex = Math.min(pickerList.count - 1,
                                                               pickerList.currentIndex + 1)
                    }
                    Keys.onReturnPressed: event => {
                        if (!event.isAutoRepeat)
                            picker.activate(pickerList.currentIndex)
                    }
                    Keys.onEnterPressed: event => {
                        if (!event.isAutoRepeat)
                            picker.activate(pickerList.currentIndex)
                    }
                }
            }

            ListView {
                id: pickerList

                anchors.top: pickerHead.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: Theme.spacingTight
                anchors.rightMargin: Theme.spacingTight
                anchors.topMargin: Theme.spacingTight
                height: Math.min(contentHeight, Theme.scale(380))
                clip: true
                model: picker.rows
                currentIndex: -1
                keyNavigationEnabled: false
                highlightMoveDuration: Theme.animFastMs
                boundsBehavior: Flickable.StopAtBounds
                cacheBuffer: Theme.controlHeightLarge * 6

                ScrollBar.vertical: StrmScrollBar {}

                delegate: Item {
                    id: pickerRow

                    required property int index
                    required property var modelData

                    readonly property bool current: pickerList.currentIndex === pickerRow.index

                    width: pickerList.width
                    height: Theme.controlHeightLarge

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: Theme.scale(2)
                        radius: Theme.radiusChip
                        color: pickerRow.current ? Theme.hoverTint : "transparent"

                        Behavior on color {
                            ColorAnimation {
                                duration: Theme.animInstant
                                easing.type: Theme.easeInstant
                            }
                        }
                    }

                    StrmIcon {
                        id: pickerGlyph

                        anchors.left: parent.left
                        anchors.leftMargin: Theme.spacingValue
                        anchors.verticalCenter: parent.verticalCenter
                        name: pickerRow.modelData.create ? "plus" : "playlist"
                        color: pickerRow.current ? Theme.accentColor : Theme.textTertiary
                    }

                    Text {
                        anchors.left: pickerGlyph.right
                        anchors.leftMargin: Theme.spacingValue
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.spacingValue
                        anchors.verticalCenter: parent.verticalCenter
                        text: pickerRow.modelData.create
                              ? qsTr("Create “%1”").arg(pickerRow.modelData.name)
                              : pickerRow.modelData.name
                        color: Theme.textPrimaryColor
                        font.family: Theme.fontBody
                        font.pixelSize: Theme.fontBodySize
                        elide: Text.ElideRight
                        maximumLineCount: 1
                    }

                    // Hover previews the row; it never commits, and it never
                    // takes the caret out of the field being typed in.
                    HoverHandler {
                        cursorShape: Qt.PointingHandCursor
                        onHoveredChanged: {
                            if (hovered)
                                pickerList.currentIndex = pickerRow.index
                        }
                    }

                    TapHandler {
                        gesturePolicy: TapHandler.ReleaseWithinBounds
                        onTapped: picker.activate(pickerRow.index)
                    }
                }
            }

            Text {
                id: pickerHint

                anchors.top: pickerList.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: Theme.spacingValue
                anchors.rightMargin: Theme.spacingValue
                height: pickerHint.visible ? Theme.controlHeightLarge : Theme.spacingTight
                verticalAlignment: Text.AlignVCenter
                visible: picker.rows.length === 0
                text: picker.records.length === 0
                      ? qsTr("You have no playlists yet — type a name to make one.")
                      : qsTr("Nothing matches. Keep typing to create a new playlist.")
                color: Theme.textTertiary
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }
        }
    }

    PlaylistPicker {
        id: playlistPicker

        z: 800
        // Focus goes back to the button that opened it: a dismissed overlay
        // that drops the keyboard at the top of the page is the same bug as one
        // that never gives it back.
        onDismissed: playlistButton.forceActiveFocus(Qt.OtherFocusReason)
    }

    Connections {
        target: PlaylistCtl

        function onActionSucceeded(message) {
            if (!playlistPicker.pending)
                return
            playlistPicker.pending = false
            albumToasts.show(message, "success")
        }
        function onActionFailed(message) {
            if (!playlistPicker.pending)
                return
            playlistPicker.pending = false
            albumToasts.show(message, "error")
        }
    }

    StrmToastHost {
        id: albumToasts

        anchors.fill: parent
        z: 900
    }

    // ── Page states ────────────────────────────────────────────────────────
    LoadingState {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: tableHead.bottom
        anchors.bottom: parent.bottom
        visible: page.awaitingTracks
        shape: "list"
    }

    EmptyState {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: tableHead.bottom
        anchors.bottom: parent.bottom
        visible: !page.hasTracks && !page.awaitingTracks
        iconName: "lib-music"
        headline: page.albumId.length > 0 ? qsTr("No tracks came back")
                                          : qsTr("No album open")
        body: page.albumId.length > 0
              ? qsTr("The server returned nothing for this album. It may still be "
                     + "scanning, or the request may have failed.")
              : qsTr("Open an album from the music library to see its tracks.")
        actionText: page.albumId.length > 0 ? qsTr("Try again") : ""
        actionIcon: page.albumId.length > 0 ? "refresh" : ""
        onActionTriggered: {
            page.trackFetchTimedOut = false
            MusicCtl.openAlbum(page.albumId, page.albumName)
        }
    }
}
