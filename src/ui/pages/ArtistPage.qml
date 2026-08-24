pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects
import StrmQt

// Artist page (ARCHITECTURE.md): the artist's art, their name, and their
// discography — newest first, because a discography reads as a career.
//
// ── Where the data comes from ──────────────────────────────────────────────
//
//  * `MusicCtl.openArtist(id, name)` fills `MusicCtl.artistAlbums` using
//    **AlbumArtistIds**. That distinction is the page: measured on this server,
//    `ArtistIds` for the same artist also matches 45 individual tracks off
//    compilations, which is a credit list and not a discography.
//  * The artist record itself — name, artwork, favourite state — is *not* a
//    fetch. It arrives as the model row the user clicked, handed in whole as
//    `artistItem`, so the hero draws on the first frame with no request and no
//    flash of an empty page. `artistId`/`artistName` are separate properties
//    with bindings into that map, so a caller that has only an id and a name
//    (a deep link, a restored history entry) can still push this page.
//
//  * `MusicCtl.artistTracks` is the artist's most-played, filled by the SAME
//    `openArtist()` call using **ArtistIds** — the opposite choice from the
//    discography above, and the right one here: top tracks should include what
//    someone guested on, which is exactly what a discography must exclude.
//
// ── The page had a stale claim in this comment, and it is worth recording ──
//
// Until this phase the note here said top tracks were impossible because the
// contract had no artist-scoped Audio query. It had one all along:
// `openArtist()` has issued the PlayCount-sorted `artistTracks` fetch since it
// was written, and **nothing rendered it** — the request went out and the reply
// was thrown away on every artist page open. The table below is what it was
// always for. Play and Shuffle for the whole artist are still absent for the
// original reason (`Actions.playAll/shuffle` address a ParentId and an artist
// is not one), but "Instant mix" now covers the same ground better: it is one
// request, the server picks, and it is what the artist grid's ▸ does too.
//
// ── Sharing one controller with two other pages ────────────────────────────
//
// `MusicCtl` is a single app-wide instance shared with the music library and
// the album page. Opening an album from the discography does not disturb
// `artistAlbums` (different model, different generation), but opening a
// *second* artist does. `scopeMine` is the controller's own answer to "whose
// discography is this" and the grid is shown only while the answer is this
// page's; `ensureScope()` re-arms it when the page comes back on screen. The
// same shape PersonPage uses against LibraryController, for the same reason.
//
// Focus model, as everywhere: hover never moves the keyboard's place. The grid
// is where the page starts — it is what the user came for — and Up out of it
// reaches the one hero control.
FocusScope {
    id: page
    objectName: "artistPage"

    // ── Input ──────────────────────────────────────────────────────────────
    // The whole model row for the artist, as MediaItemModel::get() produces it.
    // Optional: id and name alone are enough to draw and fetch.
    property var artistItem: ({})

    property string artistId: {
        const value = page.artistItem ? page.artistItem.itemId : undefined
        return (value === undefined || value === null) ? "" : String(value)
    }
    property string artistName: {
        const value = page.artistItem ? page.artistItem.name : undefined
        return (value === undefined || value === null) ? "" : String(value)
    }

    // ── Derived state ──────────────────────────────────────────────────────
    readonly property bool scopeMine: page.artistId.length > 0
                                      && MusicCtl.artistId === page.artistId

    // The controller's name wins once it is this artist's, so a push that
    // carried only an id still ends up with a title.
    readonly property string displayName: {
        if (page.scopeMine && MusicCtl.artistName.length > 0)
            return MusicCtl.artistName
        return page.artistName
    }

    // Artists have Primary art and nothing else, so there is no thumb/backdrop
    // ladder to walk. When the row carried a finished provider URL it is used;
    // otherwise one is built. The opaque provider namespace changes at every
    // identity boundary; reading it makes this binding follow that boundary.
    // An artist the server has no image for 404s, and the initials behind the
    // image are then what shows — which is the normal case, not an error.
    readonly property string artUrl: {
        const poster = page.artistItem ? page.artistItem.posterUrl : undefined
        if (poster !== undefined && poster !== null && String(poster).length > 0)
            return String(poster)
        if (page.artistId.length === 0 || Images.sourceNamespace.length === 0)
            return ""
        return Images.sourceFor(page.artistId, "Primary", "")
    }

    readonly property int albumCount: page.scopeMine
                                      ? MusicCtl.artistAlbums.totalRecordCount : 0
    readonly property int albumsLoaded: page.scopeMine ? MusicCtl.artistAlbums.count : 0

    // ── Top tracks ─────────────────────────────────────────────────────────
    // Same scope guard as the grid: MusicCtl is app-wide, and a list of another
    // artist's songs under this artist's name is worse than no list.
    readonly property int topTracksLoaded: page.scopeMine ? MusicCtl.artistTracks.count : 0
    readonly property bool hasTopTracks: page.topTracksLoaded > 0

    // How many rows the section is TALL, not how many were fetched. The page
    // has no scroll container — the discography grid is the thing that scrolls
    // — so a 50-row table anchored between the hero and the grid would push the
    // grid off the bottom of the window. Six rows is a section; the table
    // itself is a ListView and owns the rest internally, so nothing is lost.
    readonly property int topTracksRows: Math.min(6, page.topTracksLoaded)
    readonly property int topTrackRowHeight: Theme.scale(38)

    // `MusicCtl.loading` is raised by the album and artist *list* fetches and
    // not by openArtist(), so "is the discography still coming" has to be
    // tracked here: raised with the request, lowered by the first row, and
    // capped by a guard timer so an artist with nothing filed under them — or a
    // request that failed, which openArtist() reports nowhere — does not sit on
    // a skeleton forever.
    property bool discographyLoading: false

    readonly property bool discographyEmpty: page.artistId.length > 0
                                             && page.scopeMine
                                             && !page.discographyLoading
                                             && page.albumsLoaded === 0

    // Favourite is the one piece of user state this page writes. It is seeded
    // from the row that was handed in rather than from Actions.isFavorite():
    // MusicController's models are not registered with ItemActions, so that
    // call answers "false" for every album and artist on this server until
    // something has been toggled in this session. Reported; until then the
    // handed-in row is the honest source and the signal keeps it current.
    property bool artistFavorite: false

    readonly property int artSize: Theme.scale(200)

    // ── Loading ────────────────────────────────────────────────────────────
    function ensureScope(): void {
        if (page.artistId.length === 0 || page.scopeMine)
            return
        page.discographyLoading = true
        loadGuard.restart()
        MusicCtl.openArtist(page.artistId, page.artistName)
    }

    function syncFavorite(): void {
        const value = page.artistItem ? page.artistItem.favorite : undefined
        page.artistFavorite = value === true
    }

    function toggleFavorite(): void {
        if (page.artistId.length === 0)
            return
        Actions.setFavorite(page.artistId, !page.artistFavorite)
    }

    // ── Album verbs ────────────────────────────────────────────────────────
    // Always read back through the live model: the grid draws from it directly
    // and the verbs want the whole record, not a card's worth of it.
    function albumAt(index) {
        const model = MusicCtl.artistAlbums
        if (!model || index < 0 || index >= model.count)
            return null
        return model.get(index)
    }

    function openAlbum(index): void {
        const item = page.albumAt(index)
        if (item)
            Actions.openDetails(item)
    }

    // The same verb the music grid's ▸ calls, not Actions.playAll(id, "music"):
    // one implementation per verb (ARCHITECTURE.md rule 3), and the two used to
    // disagree — playAll sorts music by IndexNumber,SortName, which interleaves
    // the discs of a box set, while MusicCtl.playAlbum() takes the album's
    // children in the server's own disc-then-track order.
    function playAlbum(index): void {
        const item = page.albumAt(index)
        if (!item)
            return
        const id = item.itemId !== undefined ? String(item.itemId) : ""
        if (id.length > 0)
            MusicCtl.playAlbum(id)
    }

    function showMenu(index, sceneX, sceneY): void {
        itemMenu.popupForItem(page.albumAt(index), sceneX, sceneY)
    }

    // ── Top-track verbs ────────────────────────────────────────────────────
    function topTrackAt(index) {
        const model = MusicCtl.artistTracks
        if (!model || index < 0 || index >= model.count)
            return null
        return model.get(index)
    }

    // The loaded rows are the queue and the clicked row is where it starts —
    // the same contract the album page's table has.
    function playTopTrackFrom(index): void {
        const model = MusicCtl.artistTracks
        if (!model || index < 0 || index >= model.count)
            return
        const items = []
        for (let i = 0; i < model.count; ++i)
            items.push(model.get(i))
        Actions.playAllFrom(items, index)
    }

    function fileTopSelection(): void {
        const ids = topTracks.selectedIds()
        if (ids.length === 0)
            return
        playlistPicker.show(page.displayName, ids)
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

    Component.onCompleted: {
        page.syncFavorite()
        page.ensureScope()
    }

    // StackView hides a covered page and shows it again on pop. Coming back
    // from an album opened out of the discography is what this exists for.
    onVisibleChanged: { if (page.visible) page.ensureScope() }

    onArtistItemChanged: page.syncFavorite()

    Connections {
        target: Actions
        function onFavoriteChanged(itemId, favorite) {
            if (itemId === page.artistId)
                page.artistFavorite = favorite
        }
    }

    Connections {
        target: MusicCtl.artistAlbums
        function onCountChanged() {
            if (MusicCtl.artistAlbums.count > 0) {
                page.discographyLoading = false
                loadGuard.stop()
            }
        }
    }

    Timer {
        id: loadGuard
        interval: 6000
        onTriggered: page.discographyLoading = false
    }

    // ── The music input context (MUSIC.md §7) ──────────────────────────────
    // Three of the four. `music.shuffleAll` is deliberately absent: an artist
    // is not a ParentId the server will hand tracks back for, so there is
    // nothing honest for S to shuffle here, and "Instant mix" (R) is the verb
    // that does what a user pressing it would actually want. A key that quietly
    // did nothing would be worse than one that is simply not bound on this page.
    MappedShortcut {
        actionId: "music.playPause"
        fallback: ["Space"]
        active: App.interactionContext === "music" && PlayerCtl.active
        onActivated: PlayerCtl.togglePause()
    }

    MappedShortcut {
        actionId: "music.favorite"
        fallback: ["L"]
        active: App.interactionContext === "music" && page.artistId.length > 0
        onActivated: {
            if (page.hasTopTracks && topTracks.selectionCount > 0) {
                Actions.setFavoriteAll(topTracks.selectedIds(), true)
                return
            }
            // The artist, not an album: this page is about them, and the
            // hero button says the same thing.
            page.toggleFavorite()
        }
    }

    MappedShortcut {
        actionId: "music.instantMix"
        fallback: ["R"]
        active: App.interactionContext === "music" && page.artistId.length > 0
        onActivated: Actions.instantMix(page.artistId)
    }

    // ── Atmosphere (ARCHITECTURE.md) ────────────────────────────────────────
    // The artist's own art, blurred and desaturated behind the hero: an artist
    // has no backdrop on any server, so this is the only wash the page can
    // have, and it costs one already-cached image. Gated on Prefs so the
    // backdrop switch means the same thing on every page, and on `visible` so
    // an off wash stops the offscreen render rather than drawing it at zero.
    // The artist's colour under the blurred art below: one sampled, clamped
    // accent (MUSIC.md §4, Rule 2) rather than a second copy of the picture.
    // Lower in z so the photograph reads over it, not through it.
    CoverWash {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Math.round(page.height * 0.42)
        z: -2
        source: page.artUrl
    }

    Item {
        id: wash

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Math.round(page.height * 0.42)
        z: -1
        visible: Prefs.backdropEnabled && page.artUrl.length > 0 && wash.opacity > 0.001
        opacity: Prefs.backdropEnabled ? Prefs.backdropOpacity / 100 : 0

        Behavior on opacity {
            NumberAnimation { duration: Theme.animNormalMs; easing.type: Theme.easeStandard }
        }

        layer.enabled: wash.visible
        layer.effect: MultiEffect {
            autoPaddingEnabled: false
            blurEnabled: true
            blur: 1.0
            blurMax: 48
            saturation: -0.55
        }

        Image {
            anchors.fill: parent
            source: page.artUrl
            sourceSize.width: Theme.scale(640)
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            opacity: status === Image.Ready ? 1 : 0

            Behavior on opacity {
                NumberAnimation { duration: Theme.animSlow; easing.type: Theme.easeEmphasis }
            }
        }
    }

    // The wash has to end somewhere; a hard edge reads as a bug.
    Rectangle {
        anchors.fill: wash
        z: -1
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 0.45; color: "transparent" }
            GradientStop { position: 1.0; color: Theme.ground }
        }
    }

    // ── Inline pieces ──────────────────────────────────────────────────────
    // Page-local `component`s rather than files in controls/: both are shapes
    // this page needs in this form and no other page has. ARCHITECTURE.md is
    // about not re-inventing a *button* per page — the cards, buttons, grid and
    // menu on this page are all the shared ones.

    // A booth gear label over a value. Same shape as PersonPage's, and absent
    // facts render as nothing rather than as a label with a blank beside it.
    component Fact: Column {
        id: fact

        property string label: ""
        property string value: ""

        spacing: Theme.scale(2)
        visible: fact.value.length > 0

        Text {
            text: fact.label.toUpperCase()
            color: Theme.textTertiary
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontCaption
            font.letterSpacing: Theme.fontCaption * Theme.trackLabel
        }

        Text {
            text: fact.value
            color: Theme.textPrimaryColor
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontBodySize
        }
    }

    // The artist's art, square, with the missing-image case treated as the
    // normal case it is: most artists on a self-hosted server have no Primary
    // image at all. Initials on a tinted ground keep the hero's geometry
    // identical either way and the art crossfades in over them.
    component ArtistArt: Rectangle {
        id: art

        property string imageUrl: ""
        property string name: ""

        readonly property string initials: {
            const parts = art.name.trim().split(/\s+/).filter(p => p.length > 0)
            if (parts.length === 0)
                return ""
            if (parts.length === 1)
                return parts[0].charAt(0).toUpperCase()
            return String(parts[0].charAt(0)
                          + parts[parts.length - 1].charAt(0)).toUpperCase()
        }

        radius: Theme.radiusPanel
        color: Theme.surfaceColor
        border.width: 1
        border.color: Theme.hairline
        clip: true

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: Theme.surfaceRaisedColor }
                GradientStop { position: 1.0; color: Theme.surfaceColor }
            }

            Text {
                anchors.centerIn: parent
                text: art.initials
                visible: art.initials.length > 0
                color: Theme.textTertiary
                font.family: Theme.fontDisplay
                font.pixelSize: Math.round(art.height * 0.34)
                font.weight: Font.DemiBold
            }

            StrmIcon {
                anchors.centerIn: parent
                visible: art.initials.length === 0
                name: "lib-music"
                size: Math.round(art.height * 0.28)
                color: Theme.textTertiary
            }
        }

        Image {
            anchors.fill: parent
            source: art.imageUrl
            sourceSize.width: art.width
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            opacity: status === Image.Ready ? 1 : 0

            Behavior on opacity {
                NumberAnimation { duration: Theme.animNormalMs; easing.type: Theme.easeStandard }
            }
        }
    }

    // ── Hero ───────────────────────────────────────────────────────────────
    Item {
        id: hero

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: Theme.pageMarginValue
        anchors.leftMargin: Theme.pageMarginValue
        anchors.rightMargin: Theme.pageMarginValue
        height: Math.max(artFrame.height, heroText.implicitHeight)
        visible: page.artistId.length > 0

        ArtistArt {
            id: artFrame

            anchors.top: parent.top
            anchors.left: parent.left
            width: page.artSize
            height: page.artSize
            imageUrl: page.artUrl
            name: page.displayName
        }

        Column {
            id: heroText

            anchors.top: parent.top
            anchors.left: artFrame.right
            anchors.right: parent.right
            anchors.leftMargin: Theme.spacingLoose
            spacing: Theme.spacingValue

            Text {
                width: parent.width
                text: qsTr("Artist").toUpperCase()
                color: Theme.textTertiary
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontCaption
                font.letterSpacing: Theme.fontCaption * Theme.trackLabel
            }

            Text {
                width: parent.width
                text: page.displayName
                color: Theme.textPrimaryColor
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.fontDisplaySize
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                maximumLineCount: 2
                wrapMode: Text.WordWrap
            }

            Fact {
                label: qsTr("Releases")
                // The count is only true once the fetch has landed; before that
                // the row is absent rather than claiming zero.
                value: (page.albumCount > 0)
                       ? qsTr("%1 albums", "", page.albumCount).arg(page.albumCount)
                       : ""
            }

            // Two hero controls, and both are things this page can honestly
            // carry out: a favourite is an id and a boolean, and an instant mix
            // is one request against the artist's own id.
            Row {
                spacing: Theme.spacingTight

                StrmButton {
                    id: mixButton

                    // The artist as a station, and the same verb the artist
                    // grid's ▸ fires (ARCHITECTURE.md rule 3). Measured:
                    // /Items/{artistId}/InstantMix answers audio that is
                    // genuinely of this artist's world — see
                    // EmbyClient::instantMix().
                    text: qsTr("Instant mix")
                    iconName: "shuffle"
                    variant: "primary"
                    enabled: page.artistId.length > 0
                    onClicked: Actions.instantMix(page.artistId)

                    KeyNavigation.right: favButton
                    KeyNavigation.down: page.hasTopTracks ? topTracks : albumGrid
                }

                StrmButton {
                    id: favButton

                    text: page.artistFavorite ? qsTr("In favourites")
                                              : qsTr("Add to favourites")
                    iconName: page.artistFavorite ? "heart-filled" : "heart"
                    variant: page.artistFavorite ? "primary" : "secondary"
                    enabled: page.artistId.length > 0

                    KeyNavigation.left: mixButton
                    KeyNavigation.down: page.hasTopTracks ? topTracks : albumGrid
                    onClicked: page.toggleFavorite()
                }
            }
        }
    }

    // ── Top tracks ─────────────────────────────────────────────────────────
    // The list `openArtist()` has been fetching and discarding since it was
    // written (see the header). PlayCount-sorted, so the heading says so — on a
    // library nobody has played through StrmQt yet the order is arbitrary, and
    // labelling it "most played" is honest about what it is rather than about
    // what it looks like.
    Item {
        id: topHeading

        anchors.top: hero.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: page.hasTopTracks ? Theme.railGap : 0
        anchors.leftMargin: Theme.pageMarginValue
        anchors.rightMargin: Theme.pageMarginValue
        height: page.hasTopTracks ? topLabel.implicitHeight : 0
        visible: page.hasTopTracks

        Text {
            id: topLabel

            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Top tracks").toUpperCase()
            color: Theme.textTertiary
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontCaption
            font.letterSpacing: Theme.fontCaption * Theme.trackLabel
        }

        Text {
            anchors.left: topLabel.right
            anchors.leftMargin: Theme.spacingValue
            anchors.baseline: topLabel.baseline
            text: qsTr("most played")
            color: Theme.textSecondaryColor
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontCaption
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.bottomMargin: -Theme.spacingTight
            height: 1
            color: Theme.hairline
        }
    }

    SelectionBar {
        id: topSelection

        anchors.top: topHeading.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.pageMarginValue
        anchors.rightMargin: Theme.pageMarginValue

        count: page.hasTopTracks ? topTracks.selectionCount : 0

        onQueueRequested: Actions.addAllToQueue(topTracks.selectedItems())
        onPlaylistRequested: page.fileTopSelection()
        onFavoriteRequested: Actions.setFavoriteAll(topTracks.selectedIds(), true)
        onClearRequested: {
            topTracks.clearSelection()
            topTracks.forceActiveFocus(Qt.OtherFocusReason)
        }
    }

    readonly property int topArtistColumn: topTracks.showArtistColumn ? Theme.scale(200) : 0

    TrackTable {
        id: topTracks

        anchors.top: topSelection.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: page.hasTopTracks ? Theme.spacingValue : 0
        anchors.leftMargin: Theme.pageMarginValue
        anchors.rightMargin: Theme.pageMarginValue
        height: page.hasTopTracks ? page.topTracksRows * page.topTrackRowHeight : 0
        visible: page.hasTopTracks
        enabled: page.hasTopTracks

        model: MusicCtl.artistTracks
        rowHeight: page.topTrackRowHeight
        // No discs — these tracks come from every record the artist appears on
        // — and the artist column is unconditional for the same reason: a
        // guest credit is the point of an ArtistIds list, and asking "does any
        // row differ" would answer yes on almost every artist anyway.
        discGrouping: false
        artistRule: false
        alwaysShowArtist: true
        multiSelect: true

        KeyNavigation.up: mixButton
        KeyNavigation.down: albumGrid

        onActivated: index => page.playTopTrackFrom(index)

        delegate: TrackRow {
            id: topRow

            required property int index
            required property var model

            readonly property string trackId: topRow.model.itemId !== undefined
                                              ? String(topRow.model.itemId) : ""

            width: topTracks.width

            rowHeight: page.topTrackRowHeight
            artistColumn: page.topArtistColumn

            title: topRow.model.name !== undefined ? String(topRow.model.name) : ""
            // The album, on the second line: on a top-tracks list the record a
            // song came off is what tells two versions of it apart.
            secondary: topRow.model.album !== undefined && topRow.model.album !== null
                       ? String(topRow.model.album) : ""
            artist: topTracks.shownArtistFor(topRow.model)
            durationText: page.formatDuration(topRow.model.runtimeMs)
            // The ordinal, not the track number: this list is ordered by play
            // count, and a column reading 11, 3, 7 down a chart says nothing.
            number: topRow.index + 1

            current: topTracks.currentIndex === topRow.index && topTracks.activeFocus
            selected: topTracks.isSelected(topRow.index)
            playing: topRow.trackId.length > 0 && topRow.trackId === page.nowPlayingId
            favorite: topRow.model.favorite === true
            showFavorite: true
            showMenu: true
            verbsRevealed: topRow.hovered || topRow.favorite

            onActivated: modifiers => {
                topTracks.forceActiveFocus(Qt.MouseFocusReason)
                topTracks.activateAt(topRow.index, modifiers)
            }

            onFavoriteToggled: {
                const item = page.topTrackAt(topRow.index)
                if (item)
                    Actions.toggleFavorite(item)
            }

            onMenuRequested: (sceneX, sceneY) => {
                trackMenu.popupForItemNoDetails(page.topTrackAt(topRow.index), sceneX, sceneY)
            }
        }
    }

    // ── Discography ────────────────────────────────────────────────────────
    Item {
        id: albumHeading

        anchors.top: topTracks.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: Theme.railGap
        anchors.leftMargin: Theme.pageMarginValue
        anchors.rightMargin: Theme.pageMarginValue
        height: albumHeading.visible ? headingLabel.implicitHeight : 0
        visible: page.artistId.length > 0

        Text {
            id: headingLabel

            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Discography").toUpperCase()
            color: Theme.textTertiary
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontCaption
            font.letterSpacing: Theme.fontCaption * Theme.trackLabel
        }

        // Mono and tabular, like every other readout in this app: it is a
        // number, not a sentence.
        Text {
            anchors.left: headingLabel.right
            anchors.leftMargin: Theme.spacingValue
            anchors.baseline: headingLabel.baseline
            visible: page.albumCount > 0
            text: qsTr("newest first")
            color: Theme.textSecondaryColor
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontCaption
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.bottomMargin: -Theme.spacingTight
            height: 1
            color: Theme.hairline
        }
    }

    StrmGrid {
        id: albumGrid

        anchors.top: albumHeading.bottom
        anchors.topMargin: Theme.spacingValue
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom

        // Shown only while the shared controller is still scoped to this
        // artist: a grid quietly full of somebody else's records is worse than
        // no grid at all.
        visible: page.scopeMine && !page.discographyLoading && page.albumsLoaded > 0
        // Focus follows content, never visibility.
        focus: albumGrid.count > 0

        gridModel: MusicCtl.artistAlbums
        // Square, because a record sleeve is square. StrmCard already has the
        // variant; nothing here invents a shape.
        cardVariant: "square"
        emptyText: ""

        KeyNavigation.up: page.hasTopTracks ? topTracks : favButton

        onItemActivated: index => page.openAlbum(index)
        onItemPlayRequested: index => page.playAlbum(index)
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
        onMenuRequested: (index, mx, my) => page.showMenu(index, mx, my)
    }

    ItemMenu {
        id: itemMenu
        // The albums on this page are this artist's, so "Go to artist" would
        // navigate to where the user already is — the same call DetailsPage
        // makes about its own item.
        allowMusicNavigation: false
    }

    // The track menu the album page and the Songs tab already raise, minus
    // "Details": for a track the album page IS the details page.
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

    PlaylistPicker {
        id: playlistPicker

        z: 800
        // Everything filed from this page is a track, so a playlist made here
        // is an audio one — which is what puts it in the music library's
        // Playlists tab rather than in no library at all.
        mediaType: "Audio"
        onDismissed: {
            if (page.hasTopTracks)
                topTracks.forceActiveFocus(Qt.OtherFocusReason)
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
            artistToasts.show(message, "success")
        }
        function onActionFailed(message) {
            if (!playlistPicker.pending)
                return
            playlistPicker.pending = false
            artistToasts.show(message, "error")
        }
    }

    StrmToastHost {
        id: artistToasts

        anchors.fill: parent
        z: 900
    }

    // ── Page states ────────────────────────────────────────────────────────
    LoadingState {
        anchors.top: albumGrid.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        shape: "grid"
        active: page.artistId.length > 0 && page.discographyLoading
    }

    // An artist with no albums filed under them is a real state on a music
    // server: everything they appear on is filed under somebody else, or under
    // "Various Artists". Saying so beats an empty grid.
    EmptyState {
        anchors.top: albumGrid.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        visible: page.discographyEmpty
        iconName: "lib-music"
        headline: qsTr("No albums for %1").arg(page.displayName)
        body: qsTr("Nothing on this server is filed under this artist — their tracks may sit on compilations credited to someone else.")
        actionText: qsTr("Reload")
        actionIcon: "refresh"
        onActionTriggered: {
            page.discographyLoading = true
            loadGuard.restart()
            MusicCtl.openArtist(page.artistId, page.artistName)
        }
    }

    // Pushed with nothing to show. Reachable only from a broken link or from
    // the page-construction self-test, and it must not be a blank page either.
    EmptyState {
        id: noArtist

        anchors.fill: parent
        visible: page.artistId.length === 0
        focus: noArtist.visible
        iconName: "user"
        headline: qsTr("No artist selected")
        body: qsTr("Open an artist from search or from a music library.")
    }
}
