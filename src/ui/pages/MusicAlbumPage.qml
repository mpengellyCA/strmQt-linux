pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import StrmQt

// MusicAlbumPage — the back of a sleeve (Crate spec §6.1).
//
// Everything on it is AlbumCtl's: the kicker, the badges, the sides, the liner
// notes and the More-by shelf are composed in C++ from one albumSleeve call, so
// this file only lays them out. Play verbs go through AlbumCtl, which hands the
// queue its source label ("Sunburned Almanac", "Shuffle · …", "Radio · …").
//
// The page pushes nothing itself. The artist credit uses Actions.openArtist,
// a More-by sleeve uses Actions.openAlbum, and a genre link is raised to the
// shell as musicGenreRequested, where Main decides which library it opens in.
//
// ── Layout ─────────────────────────────────────────────────────────────────
// Wide: the sleeve and its liner notes stand in a column of their own on the
// left, the header and the record fill the right. Narrow (< 1000): the left
// column folds away, a smaller sleeve joins the header and the liner notes move
// into the table's footer, above More-by. The More-by shelf IS the table's
// footer rather than a band under it, so it scrolls with the record instead of
// taking height away from the thing the page exists for.
FocusScope {
    id: page

    // ── Contract ───────────────────────────────────────────────────────────
    // The album's item map as the route carried it. Only itemId and name are
    // read: the sleeve comes from AlbumCtl, so a stale map can never show an
    // old cover or year. A page with no map (the self-test) shows whatever
    // AlbumCtl holds.
    property var albumItem: ({})

    signal musicGenreRequested(string genreId, string genreName)

    readonly property string albumId: page.albumItem && page.albumItem.itemId !== undefined
                                      ? String(page.albumItem.itemId) : AlbumCtl.albumId
    readonly property string albumName: page.albumItem && page.albumItem.name !== undefined
                                        ? String(page.albumItem.name) : AlbumCtl.title
    readonly property bool hasTracks: AlbumCtl.albumId === page.albumId && AlbumCtl.tracks.count > 0
    // ── Where the header folds (rulings P4-R25 and P4-R32) ───────────────────
    //
    // The rule, not the number: **the two-column layout may not open narrower
    // than the narrowest text column the folded layout ever has to show.** The
    // threshold is whatever width satisfies that, so it is computed here rather
    // than written down — `Theme.scale()` moves with `Prefs.densityMode`
    // (compact 0.9, normal 1.0, tv 1.15) and a pre-scaled literal would hold the
    // guarantee at one density and quietly lose it at the others.
    //
    // Two facts drive it. First, each layout spends a fixed amount of the page on
    // everything that is *not* the text column, so the column is `width - chrome`
    // in both. Unfolded chrome is the larger by a full sleeve, which means
    // crossing this threshold in either direction always costs the reader that
    // difference: the step cannot be removed by moving the number, only
    // relocated. Second, there is a floor — the shell cannot produce a page
    // narrower than `floorWidth` — so the folded layout has a worst case, and
    // that worst case is the bar the unfolded layout has to clear on its first
    // pixel. Hence `threshold = (floorWidth - foldedChrome) + unfoldedChrome`,
    // which makes the unfolded layout open at exactly the folded floor and only
    // widen from there.
    //
    // At density 1.0 this evaluates to 1064. It was 1000 before R25, where the
    // unfolded layout opened 64px below the floor; a pre-scaled `Theme.scale(1064)`
    // was 96px below it at compact density, which is what R32 corrects.
    //
    // The column this protects — the header text beside the sleeve — is not the
    // widest thing on the page. The track table below it is full width, and it
    // gives up the sleeve's extra width at the same threshold (804 -> 580 at
    // density 1.0). That is a real step, and it is the one this derivation does
    // not remove: the guarantee is that the narrowest column never opens below
    // the width it already survives folded, not that nothing changes width.
    readonly property int narrowSleeveSize: Theme.scale(200)
    readonly property int wideSleeveSize: Theme.scale(340)
    // `Theme.pageMarginValue * 2` is the page margin on both sides; the middle
    // term is the sleeve, and the last is the gap the sleeve is held off the text
    // by — `header.spacing` folded, `mainColumn.anchors.leftMargin` unfolded.
    readonly property int foldedChrome: Theme.pageMarginValue * 2 + page.narrowSleeveSize
                                       + Theme.spacingLoose
    readonly property int unfoldedChrome: Theme.pageMarginValue * 2 + page.wideSleeveSize
                                          + Theme.spacingLoose * 2
    // `src/ui/Main.qml` sets `minimumWidth: 960` on the window and its StackView
    // gives up `NavRail.collapsedWidth` on the left. The 960 is the one unscaled
    // constant in the derivation — a window minimum is a fact about the display,
    // not about density — while the rail's width is scaled, so the floor is not
    // the same page width at every density (900 normal, 906 compact, 891 tv).
    // Mirroring the rail's width here couples this page to the shell's geometry;
    // if a third page needs the same number it belongs in Theme.
    readonly property int floorWidth: 960 - (Theme.touchTarget + Theme.spacingValue)
    readonly property int foldedFloorText: page.floorWidth - page.foldedChrome
    readonly property bool narrow: page.width < page.foldedFloorText + page.unfoldedChrome
    readonly property bool isActivePage: page.StackView.status === StackView.Active
                                         || page.StackView.view === null
    readonly property int sleeveSize: page.narrow ? page.narrowSleeveSize : page.wideSleeveSize

    // The More-by shelf lives in the track table's footer, and ListView types
    // `footerItem` as a bare QQuickItem — reaching the rail through it is an
    // untyped lookup qmllint is right to flag. The footer publishes the rail
    // here instead, so every reader has a real Item to work with.
    property Item moreByShelf: null

    Accessible.role: Accessible.Pane
    Accessible.name: page.albumName

    // `visible`, not `isActivePage`: pop() detaches the popped item, which makes
    // `isActivePage`'s `StackView.view === null` clause report a popped page as
    // active. Both callers below defer, so without this a page just navigated
    // away from reopens its own album over the one the user went back to.
    // See MusicArtistPage.ensureOpen for the measurements behind the choice.
    function ensureOpen(): void {
        if (!page.visible)
            return
        if (page.albumId.length > 0 && AlbumCtl.albumId !== page.albumId)
            AlbumCtl.open(page.albumId, page.albumName)
    }

    Component.onCompleted: Qt.callLater(page.ensureOpen)
    // A covered album page becomes visible again after a second album was
    // opened on top of it: take the shared controller back.
    onVisibleChanged: { if (page.visible) Qt.callLater(page.ensureOpen) }

    // ── Stranded focus (ARCHITECTURE.md §4, MusicBrowsePage's shape) ───────
    // Qt clears active focus on `enabled: false` but NOT on `visible: false`,
    // and half the controls here hide rather than disable: the whole left
    // column at the fold, the liner notes that move with it, the table and the
    // More-by shelf when the album has nothing in them. Rather than hang a hook
    // on each one, this watches the *focused item's* own visibility, so it
    // re-evaluates exactly when focus can strand and covers controls added
    // later too. Deferred, because the handler runs before the sibling bindings
    // that decide where focus should land have caught up.
    function isStranded(): bool {
        const focused = page.Window.activeFocusItem
        return page.activeFocus && focused !== null && focused.visible === false
    }

    // Two landings, nearest context first: the record itself where there is
    // one, and the Play button, which is on screen for the whole life of the
    // page. One of the two always exists, so this never leaves focus nowhere.
    function recoverStranded(): void {
        if (trackTable.visible) {
            // focusRows, not forceActiveFocus: the "More by" shelf lives in this
            // table's footer, so it is a sibling INSIDE the same FocusScope, and
            // once it holds the scope's focus a bare forceActiveFocus on the
            // view is a measured no-op. focusRows lands on the current row.
            trackTable.focusRows(Qt.OtherFocusReason)
            return
        }
        playButton.forceActiveFocus(Qt.OtherFocusReason)
    }

    function recoverIfStranded(): void {
        if (page.isStranded())
            page.recoverStranded()
    }

    readonly property bool focusedItemVisible: {
        const focused = page.Window.activeFocusItem
        return focused === null || focused.visible === true
    }

    onFocusedItemVisibleChanged: {
        if (!page.focusedItemVisible)
            Qt.callLater(page.recoverIfStranded)
    }

    // The pad's shoulders (Main.qml cycleSection). An album has no tab bar, so
    // they walk the page's three regions: the verbs, the tracks and the shelf
    // under them.
    function cycleTab(step): bool {
        const stops = [playButton, trackTable]
        if (page.moreByShelf !== null && page.moreByShelf.visible)
            stops.push(page.moreByShelf)
        let at = 0
        for (let i = 0; i < stops.length; ++i) {
            if (stops[i].activeFocus)
                at = i
        }
        const next = stops[(at + step + stops.length) % stops.length]
        next.forceActiveFocus(Qt.TabFocusReason)
        // Bring the region into view. The shelf lives in the table's FOOTER, and
        // positionViewAtIndex(count - 1, ListView.End) puts the last *row* at the
        // bottom of the viewport, leaving the footer below it unrevealed —
        // positionViewAtBeginning() and positionViewAtEnd() are the only two
        // calls that account for a header or a footer. So the shelf branch uses
        // positionViewAtEnd(), and only the row branch needs the `count > 0`
        // guard (`count - 1` is -1 on an empty album).
        if (next === trackTable) {
            if (trackTable.count > 0)
                trackTable.positionViewAtIndex(Math.max(0, trackTable.currentIndex),
                                               ListView.Contain)
        } else if (next !== playButton) {
            trackTable.positionViewAtEnd()
        }
        return true
    }

    function fileSelection(): void {
        const ids = trackTable.selectedIds()
        if (ids.length > 0)
            playlistPicker.show(AlbumCtl.title, ids)
    }

    function toggleFavouriteInScope(): void {
        if (trackTable.selectionCount > 0) {
            Actions.setFavoriteAll(trackTable.selectedIds(), true)
            return
        }
        if (trackTable.activeFocus) {
            const track = trackTable.rowAt(trackTable.currentIndex)
            if (track) {
                Actions.toggleFavorite(track)
                return
            }
        }
        if (AlbumCtl.albumId.length > 0)
            Actions.toggleFavorite(AlbumCtl.albumItem)
    }

    // ── The music input context (spec §8) ──────────────────────────────────
    // Window-scoped shortcuts, so each one is gated on this page being the
    // one on top: a covered album must not answer S for the album above it.
    MappedShortcut {
        actionId: "music.playPause"
        fallback: ["Space"]
        active: App.interactionContext === "music" && page.isActivePage && PlayerCtl.active
        onActivated: PlayerCtl.togglePause()
    }

    MappedShortcut {
        actionId: "music.shuffleAll"
        fallback: ["S"]
        active: App.interactionContext === "music" && page.isActivePage && page.hasTracks
        onActivated: AlbumCtl.shuffle()
    }

    MappedShortcut {
        actionId: "music.favorite"
        fallback: ["L"]
        active: App.interactionContext === "music" && page.isActivePage
        onActivated: page.toggleFavouriteInScope()
    }

    // Phase 4 note 12: R is the ALBUM radio here, the same verb as the ◎
    // button. Seeding a mix from the track under the cursor stays in the
    // track's ⋯ menu, where the subject is unambiguous.
    MappedShortcut {
        actionId: "music.instantMix"
        fallback: ["R"]
        active: App.interactionContext === "music" && page.isActivePage
                && AlbumCtl.albumId.length > 0
        onActivated: AlbumCtl.radio()
    }

    // ── Atmosphere ─────────────────────────────────────────────────────────
    CoverWash {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Math.round(page.height * 0.5)
        z: -1
        source: AlbumCtl.coverUrl
    }

    // ── Left column: the sleeve and its notes ──────────────────────────────
    // The liner notes' link chips sit flush on the column's left edge and grow
    // on focus, so the column clips with room for them (FocusClip). Nothing
    // scrolls sideways into that room, so it can take a whole raise.
    FocusClip {
        id: sleeveColumnClip

        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.pageMarginValue
        anchors.topMargin: Theme.spacingLoose
        width: page.narrow ? 0 : page.sleeveSize
        visible: !page.narrow
        leftOutset: Theme.focusHeadroom(page.sleeveSize)
        rightOutset: Theme.focusHeadroom(page.sleeveSize)
    }

    Flickable {
        id: sleeveColumn

        parent: sleeveColumnClip.contentItem
        anchors.fill: parent
        contentHeight: sleeveStack.implicitHeight + Theme.spacingLoose
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: sleeveStack

            width: sleeveColumn.width
            spacing: Theme.spacingLoose

            CrateSleeve {
                size: page.sleeveSize
                coverUrl: AlbumCtl.coverUrl
                title: AlbumCtl.title
                showCaption: false
                badge: ""
                hiRes: false
            }

            // LinerNotes is a positioner: it cannot state an implicitWidth, so
            // a consumer that sets neither width nor anchors collapses it to
            // nothing without a warning. Width it here.
            LinerNotes {
                width: sleeveStack.width
                rows: AlbumCtl.linerNotes
                onLinkActivated: (id, name) => page.musicGenreRequested(id, name)
            }
        }
    }

    // ── Right column: header, selection, tracks ────────────────────────────
    Item {
        id: mainColumn

        anchors.left: page.narrow ? parent.left : sleeveColumnClip.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.leftMargin: page.narrow ? Theme.pageMarginValue : Theme.spacingLoose * 2
        anchors.rightMargin: Theme.pageMarginValue
        anchors.topMargin: Theme.spacingLoose

        Row {
            id: header

            anchors.left: parent.left
            anchors.right: parent.right
            spacing: Theme.spacingLoose

            CrateSleeve {
                id: narrowSleeve

                // The folded layout's own sleeve: the left column is gone, so the
                // record still has to be shown next to its title. Not a Tab stop,
                // so hiding it strands nothing.
                visible: page.narrow
                size: page.sleeveSize
                coverUrl: AlbumCtl.coverUrl
                title: AlbumCtl.title
                showCaption: false
                badge: ""
                hiRes: false
            }

            Column {
                id: headerText

                width: header.width - (page.narrow ? page.sleeveSize + header.spacing : 0)
                spacing: Theme.spacingTight

                CrateKicker {
                    id: kickerLine

                    width: headerText.width
                    text: AlbumCtl.kicker
                    visible: kickerLine.text.length > 0
                }

                CrateHeading {
                    width: headerText.width
                    text: AlbumCtl.title.length > 0 ? AlbumCtl.title : page.albumName
                    pixelSize: Theme.crateHeroAlbum
                }

                // One tab stop wrapping a LinkChip, the shape DetailsPage uses
                // for a credit: the chip draws the state, the wrapper owns the
                // keyboard. `activeFocusOnTab` is tied to `visible` as well as
                // to the id — Qt drops focus on `enabled: false` but never on
                // `visible: false`, so a credit that hides while holding the
                // keyboard would strand it — and `|| activeFocus` is there
                // because Qt will not let the flag be cleared on the item that
                // holds focus at that very moment. See the same pair on the
                // track table below.
                Item {
                    id: artistLink

                    width: artistChip.implicitWidth
                    height: artistChip.implicitHeight
                    visible: AlbumCtl.artist.length > 0
                    activeFocusOnTab: (artistLink.visible && AlbumCtl.artistId.length > 0)
                                      || artistLink.activeFocus
                    onVisibleChanged: Qt.callLater(page.recoverIfStranded)

                    KeyNavigation.down: playButton
                    Keys.onReturnPressed: event => { if (!event.isAutoRepeat) artistChip.activate() }
                    Keys.onEnterPressed: event => { if (!event.isAutoRepeat) artistChip.activate() }

                    LinkChip {
                        id: artistChip

                        anchors.fill: parent
                        label: AlbumCtl.artist
                        iconName: "user"
                        linked: AlbumCtl.artistId.length > 0
                        highlighted: artistLink.activeFocus
                        onActivated: Actions.openArtist(AlbumCtl.artistId, AlbumCtl.artist)
                    }
                }

                Flow {
                    id: badgeRow

                    width: headerText.width
                    spacing: Theme.spacingTight

                    // One decision, not two: AlbumCtl.isHiRes is the dominant
                    // format's own flag, so the badge's text and its colour
                    // come from the same place.
                    CrateBadge {
                        visible: AlbumCtl.formatBadge.length > 0
                        text: AlbumCtl.formatBadge
                        hiRes: AlbumCtl.isHiRes
                    }

                    CrateBadge {
                        visible: AlbumCtl.discBadge.length > 0
                        text: AlbumCtl.discBadge
                        hiRes: false
                    }

                    Text {
                        height: Theme.scale(24)
                        verticalAlignment: Text.AlignVCenter
                        visible: AlbumCtl.trackSummary.length > 0
                        text: AlbumCtl.trackSummary
                        color: Theme.textSecondaryColor
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontSmall
                        textFormat: Text.PlainText
                    }
                }

                Item {
                    width: 1
                    height: Theme.spacingTight
                }

                Flow {
                    id: verbRow

                    width: headerText.width
                    spacing: Theme.spacingTight

                    StrmButton {
                        id: playButton

                        text: qsTr("Play")
                        iconName: "play"
                        variant: "primary"
                        enabled: page.hasTracks
                        onClicked: AlbumCtl.play(0)

                        KeyNavigation.up: artistLink
                        KeyNavigation.right: shuffleButton
                        KeyNavigation.down: trackTable
                    }

                    StrmButton {
                        id: shuffleButton

                        text: qsTr("Shuffle")
                        iconName: "shuffle"
                        enabled: page.hasTracks
                        onClicked: AlbumCtl.shuffle()

                        KeyNavigation.up: artistLink
                        KeyNavigation.left: playButton
                        KeyNavigation.right: radioButton
                        KeyNavigation.down: trackTable
                    }

                    StrmButton {
                        id: radioButton

                        text: qsTr("Album radio")
                        iconName: "audio-track"
                        enabled: AlbumCtl.albumId.length > 0
                        onClicked: AlbumCtl.radio()

                        KeyNavigation.up: artistLink
                        KeyNavigation.left: shuffleButton
                        KeyNavigation.right: playlistButton
                        KeyNavigation.down: trackTable
                    }

                    StrmButton {
                        id: playlistButton

                        text: qsTr("Playlist")
                        iconName: "plus"
                        enabled: page.hasTracks
                        onClicked: playlistPicker.show(AlbumCtl.title, AlbumCtl.trackIds)

                        KeyNavigation.up: artistLink
                        KeyNavigation.left: radioButton
                        KeyNavigation.right: favouriteButton
                        KeyNavigation.down: trackTable
                    }

                    StrmIconButton {
                        id: favouriteButton

                        iconName: AlbumCtl.favourite ? "heart-filled" : "heart"
                        checked: AlbumCtl.favourite
                        enabled: AlbumCtl.albumId.length > 0
                        tooltip: AlbumCtl.favourite ? qsTr("Remove from favourites")
                                                    : qsTr("Add to favourites")
                        shortcut: "L"
                        onClicked: Actions.toggleFavorite(AlbumCtl.albumItem)

                        KeyNavigation.up: artistLink
                        KeyNavigation.left: playlistButton
                        KeyNavigation.down: trackTable
                    }
                }
            }
        }

        SelectionBar {
            id: selectionBar

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: header.bottom
            anchors.topMargin: Theme.spacingLoose

            count: trackTable.selectionCount
            onQueueRequested: Actions.addAllToQueue(trackTable.selectedItems())
            onPlaylistRequested: page.fileSelection()
            onFavoriteRequested: Actions.setFavoriteAll(trackTable.selectedIds(), true)
            onClearRequested: {
                trackTable.clearSelection()
                // See recoverStranded: forceActiveFocus does not take the
                // keyboard back off the footer shelf.
                trackTable.focusRows(Qt.OtherFocusReason)
            }
        }

        // Configured, never re-delegated: CrateTrackTable's Crate row is the
        // whole point of the type, and a `delegate:` declared here would
        // replace it silently.
        // The table's clip (FocusClip). Its rows keep their ring inside, but the
        // More-by shelf in its footer is gutterless, and its first sleeve's
        // ring reaches past the table's left edge by a whole focus raise.
        FocusClip {
            id: trackTableClip

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: selectionBar.bottom
            anchors.bottom: parent.bottom
            anchors.topMargin: Theme.spacingTight
            leftOutset: Theme.focusHeadroom(Theme.crateSleeveSize)
            rightOutset: Theme.focusHeadroom(Theme.crateSleeveSize)
        }

        CrateTrackTable {
            id: trackTable

            navigationFocusKey: "album-tracks"
            navigationFocusFallbackItem: playButton
            navigationFocusRefillActive: AlbumCtl.loading

            parent: trackTableClip.contentItem
            anchors.fill: parent
            // TrackTable clips itself; FocusClip above is the clip now.
            clip: false
            focus: page.hasTracks
            visible: page.hasTracks
            // A list is one tab stop (ARCHITECTURE.md §4), and tied to
            // `visible` because this one hides on an empty or failed album.
            // `|| activeFocus` is not redundant: Qt refuses to clear
            // activeFocusOnTab on the item that currently HOLDS focus, warns,
            // and keeps the old value — permanently, since `visible` will not
            // change again. Staying true for that one instant lets
            // recoverIfStranded hand the keyboard on, after which activeFocus
            // has gone and the binding settles to false on its own.
            // NOT about staying in the Tab order: measured at 6.11.2, Tab and
            // Backtab skip an invisible item whatever activeFocusOnTab says.
            // What the stale `true` corrupts is NavRail.qml:43 and :98, which
            // read child.activeFocusOnTab directly to build their own sets.
            activeFocusOnTab: trackTable.visible || trackTable.activeFocus
            onVisibleChanged: Qt.callLater(page.recoverIfStranded)

            model: AlbumCtl.tracks
            discs: AlbumCtl.discs
            artistColumnShown: AlbumCtl.showArtistColumn

            KeyNavigation.up: playButton
            // KeyNavigation's default priority is AfterItem, so the ListView keeps
            // Up/Down for the row cursor and these only fire at the ends.
            //
            // There is deliberately no `KeyNavigation.down` here. Down at the last
            // row already reaches the More-by shelf, because Qt wires a pair's
            // reverse direction implicitly: the shelf's own `KeyNavigation.up:
            // trackTable` is what gives this table its `down`, and the footer
            // re-declares it whenever the view rebuilds one. A line here was tried
            // twice on two different justifications and both were measured false —
            // the second claimed Qt holds these targets as raw pointers, but
            // Qt 6.11.2 stores them as QPointer<QQuickItem> (qquickitem_p.h:844-849)
            // and a destroyed shelf self-nulls.

            onActivated: index => AlbumCtl.play(index)
            onMenuRequested: (index, mx, my) =>
                trackMenu.popupForItemNoDetails(trackTable.rowAt(index), mx, my)

            footer: Column {
                id: footerColumn

                // The footer outlives every model reset, but not the page: a
                // dangling Item in `page.moreByShelf` would be read by cycleTab
                // after the view had let go of it.
                Component.onCompleted: page.moreByShelf = moreByRail
                Component.onDestruction: {
                    if (page.moreByShelf === moreByRail)
                        page.moreByShelf = null
                }

                width: trackTable.width
                topPadding: Theme.spacingLoose * 2
                spacing: Theme.spacingLoose

                LinerNotes {
                    width: footerColumn.width
                    visible: page.narrow
                    rows: AlbumCtl.linerNotes
                    onLinkActivated: (id, name) => page.musicGenreRequested(id, name)
                }

                // `gutter: 0` because this column already sits inside
                // mainColumn's page margin; without it the rail would keep a
                // second 48px gutter of its own and its first sleeve would
                // start well right of the track numbers above it.
                //
                // Ruling P4-R18: this was a wrapper that pulled the rail left by
                // a margin and inflated its width. The arithmetic aligned, but
                // translating a control out of its enclosing clip rectangle
                // clipped both hover chevrons and both edge fades out of
                // existence, and binding the wrapper's `visible` to the rail's
                // own latched the shelf off for good. The gutter belongs to the
                // control, so it now lives there.
                StrmRail {
                    id: moreByRail

                    width: footerColumn.width
                    gutter: 0
                    visible: AlbumCtl.moreBy.count > 0
                    title: AlbumCtl.moreByTitle
                    railModel: AlbumCtl.moreBy
                    navigationFocusKey: "album-more-by"
                    customCardWidth: Theme.crateSleeveSize
                    customCardHeight: Theme.crateSleeveSize + Theme.scale(52)
                    // A shelf is one tab stop, and it hides with its model.
                    // `|| activeFocus` is required, not belt-and-braces: Qt
                    // refuses to clear activeFocusOnTab on the item that holds
                    // focus — it warns and keeps the old value, permanently,
                    // since `visible` does not change again. Holding it true for
                    // that instant lets the recovery below hand the keyboard on
                    // first. Not a Tab-order fix: Tab and Backtab already skip
                    // an invisible item. The stale `true` matters because
                    // NavRail.qml:43 and :98 read activeFocusOnTab directly.
                    activeFocusOnTab: moreByRail.visible || moreByRail.activeFocus
                    onVisibleChanged: Qt.callLater(page.recoverIfStranded)

                    // The shelf is the table's footer, so focus arriving here by
                    // Tab or by a shoulder lands below the fold on any album
                    // whose tracks overflow the viewport: neither
                    // forceActiveFocus nor Qt's Tab traversal scrolls an
                    // enclosing Flickable to a newly focused child. An amber
                    // ring nobody can see is the failure ARCHITECTURE.md §4
                    // exists to prevent, so reveal it on the way in.
                    onActiveFocusChanged: {
                        if (moreByRail.activeFocus)
                            trackTable.positionViewAtEnd()
                    }

                    // KEEP this binding: it is what supplies the TABLE's Down.
                    // Qt wires a KeyNavigation pair's reverse direction
                    // implicitly, so the table declares no KeyNavigation.down —
                    // retarget or delete this and Down at the last row dies
                    // silently.
                    KeyNavigation.up: trackTable

                    // ...but on its own it does not work, for the same reason
                    // route 2 did not: KeyNavigation calls setFocus on the
                    // table, and the shelf is a sibling inside the table's own
                    // FocusScope, so the scope's focus never leaves the shelf.
                    // An explicit Keys handler runs BEFORE the attached
                    // KeyNavigation and, by accepting the event, stops it; the
                    // binding above survives for the pairing while this does the
                    // actual move. Measured in both directions.
                    Keys.onUpPressed: event => {
                        trackTable.focusRows(Qt.BacktabFocusReason)
                        event.accepted = true
                    }

                    cardComponent: Component {
                        CrateSleeve {
                            id: moreBySleeve

                            property var model: null
                            property int index: -1

                            size: Theme.crateSleeveSize
                            showCaption: true
                            coverUrl: moreBySleeve.model && moreBySleeve.model.coverUrl !== undefined
                                      ? String(moreBySleeve.model.coverUrl) : ""
                            title: moreBySleeve.model && moreBySleeve.model.title !== undefined
                                   ? String(moreBySleeve.model.title) : ""
                            subtitle: moreBySleeve.model && moreBySleeve.model.subtitle !== undefined
                                      ? String(moreBySleeve.model.subtitle) : ""
                            badge: moreBySleeve.model && moreBySleeve.model.releaseBadge !== undefined
                                   ? String(moreBySleeve.model.releaseBadge) : ""
                            hiRes: false
                        }
                    }

                    onItemActivated: index => {
                        const album = AlbumCtl.moreBy.get(index)
                        if (album)
                            Actions.openAlbum(String(album.itemId), String(album.title))
                    }
                    onItemPlayRequested: index => {
                        const album = AlbumCtl.moreBy.get(index)
                        if (album)
                            MusicPlay.playAlbum(String(album.itemId), String(album.title), 0)
                    }
                    onMenuRequested: (index, mx, my) =>
                        albumMenu.popupForItem(AlbumCtl.moreBy.get(index), mx, my)
                }
            }
        }

        LoadingState {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: selectionBar.bottom
            anchors.bottom: parent.bottom
            visible: AlbumCtl.loading && !page.hasTracks
            shape: "list"
            margins: 0
        }

        EmptyState {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: selectionBar.bottom
            anchors.bottom: parent.bottom
            visible: !AlbumCtl.loading && !page.hasTracks
            iconName: "lib-music"
            severity: AlbumCtl.error.length > 0 ? "error" : "info"
            headline: page.albumId.length === 0 ? qsTr("No album open")
                    : AlbumCtl.error.length > 0 ? qsTr("Couldn't load this album")
                    : qsTr("No tracks came back")
            body: AlbumCtl.error.length > 0 ? AlbumCtl.error
                : page.albumId.length === 0 ? qsTr("Open an album from your music library.")
                : qsTr("The server returned no tracks for this album.")
            actionText: page.albumId.length > 0 ? qsTr("Try again") : ""
            actionIcon: page.albumId.length > 0 ? "refresh" : ""
            onActionTriggered: AlbumCtl.retry()
        }
    }

    // ── Menus and overlays ─────────────────────────────────────────────────
    // For a track the album page is its details page, so the menu leaves
    // Details out. Instant mix from one track lives here, and nowhere else on
    // this page.
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

    ItemMenu {
        id: albumMenu
    }

    PlaylistPicker {
        id: playlistPicker

        z: 800
        mediaType: "Audio"
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
}
