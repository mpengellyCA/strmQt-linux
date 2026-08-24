pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
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

    readonly property bool scopeMine: page.albumId.length > 0
                                      && MusicCtl.albumId === page.albumId
    readonly property int trackCount: page.scopeMine && MusicCtl.tracks.count > 0
                                      ? MusicCtl.tracks.count
                                      : ((page.albumItem && page.albumItem.childCount !== undefined)
                                         ? Number(page.albumItem.childCount) : 0)

    readonly property bool hasTracks: page.scopeMine && MusicCtl.tracks.count > 0

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
    onAlbumIdChanged: page.syncFavorite()

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
    // The header item is a navigation snapshot rather than a live model row,
    // so it retains one scalar. Track rows use their registered model directly.
    property bool albumFavorite: false

    function syncFavorite() {
        page.albumFavorite = page.albumId.length > 0
                && page.albumItem && page.albumItem.favorite === true
    }

    Connections {
        target: Actions
        function onFavoriteChanged(itemId, favorite) {
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

    // ── Batch verbs (MUSIC.md §7) ──────────────────────────────────────────
    // The selection's ids go to the SAME picker call the header button makes
    // with page.trackIds(); the picker, PlaylistController and the client all
    // took a list of ids from the day they were written, so none of them
    // changed for this.
    function fileSelection() {
        const ids = trackList.selectedIds()
        if (ids.length === 0)
            return
        playlistPicker.show(page.albumName, ids)
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
    // A detail request is owned by the shared controller and carries both a
    // real in-flight state and a real failure. Scope the state to this album so
    // a covered page never displays another album's rows while StackView is
    // transitioning between them.
    readonly property bool awaitingTracks: page.scopeMine && MusicCtl.loading
                                           && !page.hasTracks

    // ── The music input context (MUSIC.md §7) ──────────────────────────────
    // The same four keys the music library binds, answered for a record.
    // The shell's music context keeps them off covered pages and modal overlays;
    // Shortcut is window-scoped even when its owning page is underneath one.
    MappedShortcut {
        actionId: "music.playPause"
        fallback: ["Space"]
        active: App.interactionContext === "music" && PlayerCtl.active
        onActivated: PlayerCtl.togglePause()
    }

    MappedShortcut {
        actionId: "music.shuffleAll"
        fallback: ["S"]
        active: App.interactionContext === "music" && page.albumId.length > 0
        // THIS record, not the library: on an album page the scope the user
        // means is the one they are looking at, and it is the same call the
        // header's Shuffle button makes.
        onActivated: Actions.shuffle(page.albumId, "music")
    }

    MappedShortcut {
        actionId: "music.favorite"
        fallback: ["L"]
        active: App.interactionContext === "music"
        onActivated: {
            if (trackList.selectionCount > 0) {
                Actions.setFavoriteAll(trackList.selectedIds(), true)
                return
            }
            const item = page.trackAt(trackList.currentIndex)
            if (item)
                Actions.toggleFavorite(item)
        }
    }

    MappedShortcut {
        actionId: "music.instantMix"
        fallback: ["R"]
        active: App.interactionContext === "music" && page.hasTracks
        onActivated: {
            // The track under the cursor, not the album: a mix from one song is
            // sharper than a mix from a whole record, and the seed comes back
            // as the queue's first row (EmbyClient::instantMix).
            const item = page.trackAt(trackList.currentIndex)
            if (item)
                Actions.instantMix(item)
        }
    }

    // ── Atmosphere (MUSIC.md §4, Rule 2) ───────────────────────────────────
    // The record's own colour behind its header, sampled from the sleeve on
    // screen and clamped in C++ before it reaches here. Gated on Prefs inside
    // CoverWash, the same switch every other decorative background obeys.
    CoverWash {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Math.round(page.height * 0.42)
        z: -1
        source: page.coverUrl
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
                    //
                    // This is also "new playlist from this album": the picker's
                    // first row creates the name typed into it, out of exactly
                    // these ids, as an audio playlist. A multi-TRACK selection
                    // raises the same picker with a shorter list of ids — see
                    // page.fileSelection() and SelectionBar.
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

    // ── What the selection can be done to (MUSIC.md §7) ────────────────────
    // Between the headings and the rows, so the verbs are next to the thing
    // they act on. Zero-height while nothing is picked, so the table does not
    // move under the cursor the instant a selection is made.
    SelectionBar {
        id: selectionBar

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: tableHead.bottom
        anchors.leftMargin: Theme.pageMarginValue
        anchors.rightMargin: Theme.pageMarginValue

        count: trackList.selectionCount

        onQueueRequested: Actions.addAllToQueue(trackList.selectedItems())
        onPlaylistRequested: page.fileSelection()
        onFavoriteRequested: Actions.setFavoriteAll(trackList.selectedIds(), true)
        onClearRequested: {
            trackList.clearSelection()
            trackList.forceActiveFocus(Qt.OtherFocusReason)
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
        anchors.top: selectionBar.bottom
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
        multiSelect: true
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
            selected: trackList.isSelected(trackRow.index)
            playing: trackRow.trackId.length > 0 && trackRow.trackId === page.nowPlayingId
            favorite: trackRow.model.favorite === true
            showFavorite: true
            showMenu: true
            // A favourited row keeps its filled heart on show; the rest of the
            // verbs belong to the pointer.
            verbsRevealed: trackRow.hovered || trackRow.favorite

            // Through activateAt(), not straight to playFrom(): that is the one
            // place Ctrl+Click and Shift+Click are decided, and it moves the
            // cursor itself.
            onActivated: modifiers => {
                trackList.forceActiveFocus(Qt.MouseFocusReason)
                trackList.activateAt(trackRow.index, modifiers)
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
    // The shared control, not the inline `component` this used to declare:
    // it had already been copied into a second page, and the Songs tab needed
    // a third. Same API, same behaviour — src/ui/controls/PlaylistPicker.qml.
    PlaylistPicker {
        id: playlistPicker

        z: 800
        // Everything this page can file is a track, so a playlist made here is
        // an audio playlist — which is what puts it in the music library's
        // Playlists tab rather than in nothing at all.
        mediaType: "Audio"
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
        visible: page.albumId.length === 0
                 || (page.scopeMine && !MusicCtl.loading && !page.hasTracks)
        iconName: "lib-music"
        headline: page.albumId.length === 0
                  ? qsTr("No album open")
                  : (MusicCtl.errorMessage.length > 0
                     ? qsTr("Could not load tracks") : qsTr("No tracks came back"))
        body: page.albumId.length > 0
              ? (MusicCtl.errorMessage.length > 0
                 ? MusicCtl.errorMessage
                 : qsTr("The server returned no tracks for this album."))
              : qsTr("Open an album from the music library to see its tracks.")
        actionText: page.albumId.length > 0 ? qsTr("Try again") : ""
        actionIcon: page.albumId.length > 0 ? "refresh" : ""
        onActionTriggered: MusicCtl.openAlbum(page.albumId, page.albumName)
    }
}
