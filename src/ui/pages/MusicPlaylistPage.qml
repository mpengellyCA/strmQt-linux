pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import StrmQt

// MusicPlaylistPage — an audio playlist as a Crate record (spec §6.3).
//
// The collage and the summary are PlaylistCtl's, derived from the members in
// C++ (up to four distinct covers in member order, "31 tracks · 2 h 4 min").
// Nothing here re-derives either: the controller counts the rows and picks the
// covers, and QML binds.
//
// The member table, its edit verbs and every guard on them are PlaylistPage's,
// moved as-is: the controller owns order and identity, and this page only asks.
// Video playlists keep PlaylistPage. Main routes here only from the music
// context.
//
// ── Why a plain TrackTable and not CrateTrackTable ─────────────────────────
// CrateTrackTable is a TrackTable with the Crate row built in, and it is the
// right row everywhere the model is a TrackListModel. This page's model is
// PlaylistCtl.items, a MediaItemModel, whose roles are `name`, `label`,
// `subtitle`, `posterUrl`, `runtimeMs` and `favorite` — not the
// `displayTitle` / `featuredText` / `artistText` / `durationText` /
// `differsFromAlbumArtist` / `coverUrl` / `favourite` set the Crate row reads.
// Bound to these rows a Crate row would draw blank. The Crate row is also
// closed by design — its own header says never to set `delegate` on it — and a
// playlist row carries three edit verbs (up, down, remove) that no other track
// surface has. So the table is a TrackTable and the row is declared here.
//
// ── Entries, not items ─────────────────────────────────────────────────────
// Every write verb addresses a PLAYLIST ENTRY, never an item id: the same track
// may sit in one playlist twice. The controller does that mapping; this page
// hands it row numbers and nothing else.
//
// ── Where the keyboard goes when something disappears ──────────────────────
// Qt clears active focus when the focused item is DISABLED — it becomes this
// FocusScope, which is not a keyboard target — but not when the item merely
// goes `visible: false`, which leaves the keyboard standing on something
// nobody can see. Both happen on this page as a matter of course: the
// selection bar hides and disables the instant a removal empties the
// selection, Play and Shuffle disable when the last member goes, and the error
// state's Retry button hides the moment a reload succeeds. `focusParked` below
// sees all three, and `recoverFocus()` decides where the keyboard lands.
FocusScope {
    id: page

    objectName: "musicPlaylistPage"

    // ── Contract ───────────────────────────────────────────────────────────
    property string playlistId: ""
    property string playlistName: ""

    // The open playlist was deleted; the shell goes back.
    signal backRequested()

    readonly property int memberCount: PlaylistCtl.items.count
    readonly property bool failed: PlaylistCtl.errorMessage.length > 0
    readonly property bool narrow: page.width < Theme.scale(1000)
    readonly property bool isActivePage: page.StackView.status === StackView.Active
                                         || page.StackView.view === null
    readonly property int collageSize: page.narrow ? Theme.scale(160) : Theme.scale(340)

    // `visible`, not `isActivePage`: pop() detaches the popped item, which makes
    // `isActivePage`'s `StackView.view === null` clause report a popped page as
    // active. PlaylistCtl is shared with PlaylistPage too, so a stale reopen
    // here can retarget the generic page's pane as well as this one.
    // See MusicArtistPage.ensureOpen for the measurements behind the choice.
    function ensureOpen(): void {
        if (!page.visible)
            return
        if (page.playlistId.length > 0 && PlaylistCtl.currentId !== page.playlistId)
            PlaylistCtl.open(page.playlistId, page.playlistName)
    }

    Component.onCompleted: Qt.callLater(page.ensureOpen)
    // PlaylistCtl is shared with PlaylistPage and with any other playlist
    // route in history: take it back when this page shows again.
    onVisibleChanged: { if (page.visible) Qt.callLater(page.ensureOpen) }

    // Two stops: the verbs and the table. A stop that cannot take the keyboard
    // is left out rather than swallowing the gesture — Play is disabled on an
    // empty playlist, and an empty table has no row to stand on.
    function cycleTab(step): bool {
        const stops = []
        if (playButton.enabled)
            stops.push(playButton)
        else if (renameButton.enabled)
            stops.push(renameButton)
        if (page.memberCount > 0)
            stops.push(memberList)
        if (stops.length === 0)
            return false
        const at = memberList.activeFocus ? Math.max(0, stops.indexOf(memberList)) : 0
        const next = ((at + step) % stops.length + stops.length) % stops.length
        stops[next].forceActiveFocus(Qt.TabFocusReason)
        return true
    }

    function showMemberMenu(row, sceneX, sceneY): void {
        const item = PlaylistCtl.itemAt(row)
        if (!item)
            return
        memberList.currentIndex = row
        itemMenu.popupForItem(item, sceneX, sceneY)
    }

    function formatDuration(ms): string {
        return NowPlayingInfo.formatDuration(ms, "")
    }

    // ── Focus recovery ─────────────────────────────────────────────────────
    // True when the keyboard is on nothing usable: on this FocusScope itself
    // (where Qt parks it after disabling the focused item), on nothing at all,
    // or on an item that has hidden out from under it. Reading the focused
    // item's own `visible` is what makes the binding re-evaluate for that last
    // case, which emits no other signal.
    readonly property bool focusParked: {
        const focused = page.Window.activeFocusItem
        return focused === null || focused === page || focused.visible === false
    }

    onFocusParkedChanged: {
        if (page.focusParked)
            Qt.callLater(page.recoverIfStranded)
    }

    onMemberCountChanged: Qt.callLater(page.recoverIfStranded)

    // The page being given the keyboard is its own case: a FocusScope opened on
    // an empty playlist has no `focus: true` child to forward to, so it parks
    // the keyboard on itself and `focusParked` never CHANGES — it was already
    // true when nothing in the window was focused. Measured, not assumed: an
    // offscreen probe of this page opened on an empty list left the keyboard on
    // the scope until this trigger existed.
    onActiveFocusChanged: Qt.callLater(page.recoverIfStranded)

    // Nearest-useful-thing first: the member table when it has rows, then the
    // leftmost header verb that is actually enabled. When the playlist is empty
    // that is Rename rather than Delete — landing the keyboard on a destructive
    // verb the user did not ask for is how the wrong thing gets deleted. If
    // nothing on the page can take it (no playlist open at all), the scope
    // keeps it and Tab still leaves.
    function recoverFocus(): void {
        if (page.memberCount > 0) {
            memberList.forceActiveFocus(Qt.OtherFocusReason)
            return
        }
        if (playButton.enabled) {
            playButton.forceActiveFocus(Qt.OtherFocusReason)
            return
        }
        if (renameButton.enabled)
            renameButton.forceActiveFocus(Qt.OtherFocusReason)
    }

    function recoverIfStranded(): void {
        // Mid-refill the answer is not knowable yet — the rows are on their way
        // and the table takes the keyboard back itself when they land — so every
        // trigger fires again on the falling edge of `loading`.
        if (PlaylistCtl.loading || !page.activeFocus || !page.focusParked)
            return
        page.recoverFocus()
    }

    // ── Controller intents ─────────────────────────────────────────────────
    Connections {
        target: PlaylistCtl

        function onActionSucceeded(message) {
            if (page.isActivePage)
                toasts.show(message, "success")
        }
        function onActionFailed(message) {
            if (page.isActivePage)
                toasts.show(message, "error")
        }
        function onActionWarning(message) {
            if (page.isActivePage)
                toasts.show(message, "warning")
        }
        // Every removal, move and reload ends here. This is the edge on which
        // the page can finally answer "where is the keyboard now".
        function onLoadingChanged() {
            if (!PlaylistCtl.loading)
                Qt.callLater(page.recoverIfStranded)
        }
        // StackView may retain a covered playlist page. Only the page whose
        // gesture raised the intent forwards it, or the queue would be built
        // twice.
        function onPlayItemsRequested(items, startIndex) {
            if (page.StackView.status === StackView.Active)
                Actions.playAllFrom(items, startIndex)
        }
        function onQueueItemsRequested(items) {
            if (page.StackView.status === StackView.Active)
                Actions.addAllToQueue(items)
        }
        function onMoveFocusRequested(row) {
            if (page.StackView.status !== StackView.Active)
                return
            // The model reset has completed, but ListView lays out the new
            // delegates on its next turn. Follow the authoritative entry after
            // that turn.
            Qt.callLater(() => {
                if (page.StackView.status !== StackView.Active)
                    return
                memberList.currentIndex = row
                memberList.positionViewAtIndex(row, ListView.Contain)
            })
        }
        function onCurrentRemoved() {
            if (page.StackView.status === StackView.Active)
                page.backRequested()
        }
    }

    // ── The music input context (spec §8) ──────────────────────────────────
    MappedShortcut {
        actionId: "music.playPause"
        fallback: ["Space"]
        active: App.interactionContext === "music" && page.isActivePage && PlayerCtl.active
        onActivated: PlayerCtl.togglePause()
    }

    MappedShortcut {
        actionId: "music.shuffleAll"
        fallback: ["S"]
        active: App.interactionContext === "music" && page.isActivePage && page.memberCount > 0
        onActivated: PlaylistCtl.requestShuffle()
    }

    MappedShortcut {
        actionId: "music.favorite"
        fallback: ["L"]
        active: App.interactionContext === "music" && page.isActivePage && page.memberCount > 0
        onActivated: {
            if (memberList.selectionCount > 0) {
                Actions.setFavoriteAll(PlaylistCtl.selectedItemIds(memberList.selectedRows), true)
                return
            }
            const item = PlaylistCtl.itemAt(memberList.currentIndex)
            if (item)
                Actions.toggleFavorite(item)
        }
    }

    MappedShortcut {
        actionId: "music.instantMix"
        fallback: ["R"]
        active: App.interactionContext === "music" && page.isActivePage && page.memberCount > 0
        onActivated: {
            const item = PlaylistCtl.itemAt(Math.max(0, memberList.currentIndex))
            if (item && item.itemId !== undefined)
                MusicPlay.radio(String(item.itemId), String(item.name))
        }
    }

    // ── Atmosphere ─────────────────────────────────────────────────────────
    CoverWash {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Math.round(page.height * 0.5)
        z: -1
        source: PlaylistCtl.currentCovers.length > 0 ? PlaylistCtl.currentCovers[0] : ""
    }

    // ── Left column: collage and summary ───────────────────────────────────
    Column {
        id: collageColumn

        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: Theme.pageMarginValue
        anchors.topMargin: Theme.spacingLoose
        width: page.narrow ? 0 : page.collageSize
        visible: !page.narrow
        spacing: Theme.spacingValue

        CoverCollage {
            covers: PlaylistCtl.currentCovers
            size: page.collageSize
            radius: Theme.radiusCardValue
        }

        Text {
            width: parent.width
            visible: text.length > 0
            text: PlaylistCtl.currentSummary
            color: Theme.textSecondaryColor
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontSmall
            elide: Text.ElideRight
        }
    }

    // ── Right column ───────────────────────────────────────────────────────
    Item {
        id: mainColumn

        anchors.left: page.narrow ? parent.left : collageColumn.right
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

            CoverCollage {
                id: narrowCollage

                visible: page.narrow
                covers: PlaylistCtl.currentCovers
                size: page.collageSize
                radius: Theme.radiusCardValue
            }

            Column {
                width: header.width - (narrowCollage.visible ? narrowCollage.width + header.spacing : 0)
                spacing: Theme.spacingTight

                // Narrow drops the left column, so the summary rides the kicker
                // rather than disappearing with the collage it sat under.
                CrateKicker {
                    width: parent.width
                    text: page.narrow && PlaylistCtl.currentSummary.length > 0
                          ? qsTr("Playlist · %1").arg(PlaylistCtl.currentSummary)
                          : qsTr("Playlist")
                }

                CrateHeading {
                    width: parent.width
                    text: PlaylistCtl.currentName.length > 0 ? PlaylistCtl.currentName
                                                             : page.playlistName
                    pixelSize: Theme.crateHeroAlbum
                }

                Item {
                    width: 1
                    height: Theme.spacingTight
                }

                Flow {
                    width: parent.width
                    spacing: Theme.spacingTight

                    StrmButton {
                        id: playButton

                        text: qsTr("Play")
                        iconName: "play"
                        variant: "primary"
                        enabled: page.memberCount > 0
                        onClicked: PlaylistCtl.requestPlayFrom(0)

                        KeyNavigation.right: shuffleButton
                        KeyNavigation.down: memberList
                    }

                    StrmButton {
                        id: shuffleButton

                        text: qsTr("Shuffle")
                        iconName: "shuffle"
                        enabled: page.memberCount > 0
                        onClicked: PlaylistCtl.requestShuffle()

                        KeyNavigation.left: playButton
                        KeyNavigation.right: renameButton
                        KeyNavigation.down: memberList
                    }

                    StrmIconButton {
                        id: renameButton

                        iconName: "edit"
                        tooltip: qsTr("Rename this playlist")
                        enabled: PlaylistCtl.currentId.length > 0
                        onClicked: {
                            renameSheet.seed = PlaylistCtl.currentName
                            renameSheet.open()
                        }

                        KeyNavigation.left: shuffleButton
                        KeyNavigation.right: reloadButton
                        KeyNavigation.down: memberList
                    }

                    StrmIconButton {
                        id: reloadButton

                        iconName: "refresh"
                        tooltip: qsTr("Reload from the server")
                        enabled: PlaylistCtl.currentId.length > 0
                        onClicked: PlaylistCtl.reload()

                        KeyNavigation.left: renameButton
                        KeyNavigation.right: deleteButton
                        KeyNavigation.down: memberList
                    }

                    StrmIconButton {
                        id: deleteButton

                        iconName: "trash"
                        tooltip: qsTr("Delete this playlist")
                        // Deleting is irreversible on the server, so it asks.
                        // The controller deliberately does not: confirmation
                        // belongs where the user is, not in a verb.
                        enabled: PlaylistCtl.currentId.length > 0
                        onClicked: confirmDelete.open()

                        KeyNavigation.left: reloadButton
                        KeyNavigation.down: memberList
                    }
                }
            }
        }

        // What a multi-selection of members can be done to (spec §7). Zero
        // height while nothing is picked, so the table does not move under the
        // cursor the instant a selection is made — and `enabled` follows
        // `visible` inside the bar, which is why a removal that empties the
        // selection clears the keyboard rather than hiding it.
        SelectionBar {
            id: memberSelection

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: header.bottom
            anchors.topMargin: Theme.spacingLoose

            count: memberList.selectionCount
            allowPlaylist: false
            allowRemove: true

            onQueueRequested: PlaylistCtl.requestQueueSelection(memberList.selectedRows)
            onFavoriteRequested: Actions.setFavoriteAll(
                                     PlaylistCtl.selectedItemIds(memberList.selectedRows), true)
            onRemoveRequested: PlaylistCtl.removeSelection(memberList.selectedRows)
            onClearRequested: {
                memberList.clearSelection()
                memberList.forceActiveFocus(Qt.OtherFocusReason)
            }
            onVisibleChanged: Qt.callLater(page.recoverIfStranded)
        }

        TrackTable {
            id: memberList

            navigationFocusKey: "music-playlist-members"
            navigationFocusFallbackItem: playButton
            navigationFocusRefillActive: PlaylistCtl.loading

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: memberSelection.bottom
            anchors.bottom: parent.bottom
            anchors.topMargin: Theme.spacingTight
            clip: true
            focus: page.memberCount > 0
            // Ruling P4-R23. A table with no rows is a fourth shape of stranded
            // focus that a `visible`/null/scope predicate cannot see: the page
            // fades the table with `opacity` rather than hiding it (below), so an
            // empty table is a real, visible, focusable item that answers no key.
            // It was reachable — Rename, Reload and Delete stay enabled on an
            // empty playlist and all three name this table as their
            // `KeyNavigation.down` stop, and KeyNavigation declines only DISABLED
            // or invisible targets. Disabling it is better than widening the
            // predicate twice over: KeyNavigation now refuses the target outright
            // instead of landing on it and being rescued a turn later, and if
            // focus does arrive some other way, Qt clears it onto this
            // FocusScope, which is shape 2 and already seen.
            enabled: page.memberCount > 0
            model: PlaylistCtl.items
            rowHeight: Theme.scale(56)
            multiSelect: true
            // A playlist is not a record: no disc grouping, no artist rule, and
            // the composed label rather than the bare name for type-to-jump,
            // because an audio playlist can still hold a video the user filed
            // there.
            jumpRole: "label"
            // Opacity, not visibility: an invisible view drops active focus
            // and a reload would eject the keyboard on every move.
            opacity: PlaylistCtl.loading ? 0.0 : 1.0

            Behavior on opacity {
                NumberAnimation { duration: Theme.animFastMs; easing.type: Theme.easeStandard }
            }

            KeyNavigation.up: playButton

            onActivated: index => PlaylistCtl.requestPlayFrom(index)

            // The edit verbs on the keyboard, handed to the table rather than
            // declared as this view's own Keys.onPressed — re-declaring that
            // attached handler here would replace the table's and take Page
            // Up/Down, Home/End and type-to-jump with it.
            //
            // Alt+Up/Alt+Down rather than plain arrows, which have to keep
            // meaning "move the cursor"; Delete is the platform's remove key,
            // auto-repeat guarded so a held key cannot walk down the playlist
            // deleting it.
            onKeyPressed: event => {
                if (memberList.count === 0)
                    return
                const row = memberList.currentIndex
                if (event.modifiers & Qt.AltModifier) {
                    if (event.key === Qt.Key_Up && !event.isAutoRepeat) {
                        PlaylistCtl.moveRow(row, -1)
                        event.accepted = true
                    } else if (event.key === Qt.Key_Down && !event.isAutoRepeat) {
                        PlaylistCtl.moveRow(row, 1)
                        event.accepted = true
                    }
                    return
                }
                if (event.key === Qt.Key_Delete && !event.isAutoRepeat) {
                    // A selection wins over the cursor: with rows picked,
                    // Delete obviously means those rows.
                    if (memberList.selectionCount > 0)
                        PlaylistCtl.removeSelection(memberList.selectedRows)
                    else
                        PlaylistCtl.removeRow(row)
                    event.accepted = true
                } else if (event.key === Qt.Key_Menu && !event.isAutoRepeat) {
                    const item = memberList.currentItem
                    if (item) {
                        const p = item.mapToItem(null, Theme.spacingValue, item.height)
                        page.showMemberMenu(row, p.x, p.y)
                    }
                    event.accepted = true
                }
            }

            onMenuRequested: (index, mx, my) => page.showMemberMenu(index, mx, my)

            // The shared row configured for a playlist: an ordinal rather than
            // a track number, because a row's position is the playlist's fact
            // and not the record's; a cover, because a playlist mixes records
            // and the art is how you find your place; and the three edit verbs
            // declared inline, which is what TrackRow's default property is
            // for.
            //
            // `activeFocusOnTab: false` throughout, here and on TrackRow's
            // built-in pair: the table is one tab stop and owns the arrow keys,
            // and five focusable buttons per row would make Tab walk the
            // playlist instead of leaving it. The keyboard reaches all of them
            // through the table's key handler above.
            delegate: TrackRow {
                id: memberRow

                required property int index
                required property var model

                width: memberList.width
                navigationFocusOwner: memberList

                rowHeight: Theme.scale(56)
                surfaceTopMargin: Theme.scale(2)
                surfaceBottomMargin: Theme.scale(2)
                surfaceRightMargin: Theme.spacingTight
                // Tabular, so the column does not jitter between 9 and 10.
                numberColumn: Theme.scale(42)
                // Five verbs wide, so the title never runs under them.
                verbsColumn: Theme.scale(176)
                number: memberRow.index + 1
                hoverPlayGlyph: false
                showCover: true
                coverSize: Theme.scale(40)

                // Poster first, still second: an audio playlist can still hold
                // an episode the user filed there, and only the latter has a
                // thumb.
                coverUrl: {
                    const poster = memberRow.model.posterUrl !== undefined
                                 ? String(memberRow.model.posterUrl) : ""
                    if (poster.length > 0)
                        return poster
                    return memberRow.model.thumbUrl !== undefined
                           ? String(memberRow.model.thumbUrl) : ""
                }
                title: memberRow.model.name !== undefined ? String(memberRow.model.name) : ""
                secondary: memberRow.model.subtitle !== undefined
                           ? String(memberRow.model.subtitle) : ""
                durationText: page.formatDuration(memberRow.model.runtimeMs)
                favorite: memberRow.model.favorite === true
                played: memberRow.model.played === true

                current: memberRow.ListView.isCurrentItem && memberList.activeFocus
                selected: memberList.isSelected(memberRow.index)
                // Deliberate: MediaItemModel rows carry no queue identity, and a
                // track that appears in a playlist twice would light up on both
                // rows. The now-playing marker belongs to surfaces whose rows
                // are the queue's rows.
                playing: false
                showFavorite: true
                showMenu: true
                // Both may be true at once, and the verbs appear for either: the
                // pointer needs them under the cursor, the keyboard needs them
                // on the row it is standing on.
                verbsRevealed: memberRow.hovered || memberRow.current || memberRow.favorite

                // Through activateAt(), not straight to requestPlayFrom(): that
                // is the one place Ctrl+Click and Shift+Click are decided, and
                // it moves the cursor itself.
                onActivated: modifiers => {
                    memberList.forceActiveFocus(Qt.MouseFocusReason)
                    memberList.activateAt(memberRow.index, modifiers)
                }
                onFavoriteToggled: {
                    const item = PlaylistCtl.itemAt(memberRow.index)
                    if (item)
                        Actions.toggleFavorite(item)
                }
                onMenuRequested: (sceneX, sceneY) => page.showMemberMenu(memberRow.index,
                                                                        sceneX, sceneY)

                StrmIconButton {
                    iconName: "chevron-up"
                    round: true
                    size: Theme.scale(28)
                    activeFocusOnTab: false
                    enabled: memberRow.index > 0
                    tooltip: qsTr("Move up")
                    shortcut: "Alt+Up"
                    onClicked: {
                        memberList.cancelNavigationFocusRestore()
                        PlaylistCtl.moveRow(memberRow.index, -1)
                    }
                }

                StrmIconButton {
                    iconName: "chevron-down"
                    round: true
                    size: Theme.scale(28)
                    activeFocusOnTab: false
                    enabled: memberRow.index < page.memberCount - 1
                    tooltip: qsTr("Move down")
                    shortcut: "Alt+Down"
                    onClicked: {
                        memberList.cancelNavigationFocusRestore()
                        PlaylistCtl.moveRow(memberRow.index, 1)
                    }
                }

                StrmIconButton {
                    iconName: "trash"
                    round: true
                    size: Theme.scale(28)
                    activeFocusOnTab: false
                    tooltip: qsTr("Remove from this playlist")
                    shortcut: "Del"
                    onClicked: {
                        memberList.cancelNavigationFocusRestore()
                        PlaylistCtl.removeRow(memberRow.index)
                    }
                }
            }
        }

        // Swallows clicks aimed at rows that are being replaced underneath.
        MouseArea {
            anchors.fill: memberList
            visible: PlaylistCtl.loading
            acceptedButtons: Qt.AllButtons
        }

        LoadingState {
            anchors.fill: memberList
            shape: "list"
            active: PlaylistCtl.loading
            margins: 0
        }

        EmptyState {
            id: emptyMembers

            anchors.fill: memberList
            visible: !PlaylistCtl.loading && page.memberCount === 0 && !page.failed
            iconName: "playlist"
            headline: qsTr("This playlist is empty")
            body: qsTr("Use “Add to playlist” on any album or track to put it here.")
        }

        EmptyState {
            id: failedMembers

            anchors.fill: memberList
            visible: !PlaylistCtl.loading && page.memberCount === 0 && page.failed
            severity: "error"
            iconName: "info"
            headline: qsTr("Couldn't load this playlist")
            body: PlaylistCtl.errorMessage
            actionText: qsTr("Try again")
            actionIcon: "refresh"
            onActionTriggered: PlaylistCtl.reload()
            // Retry hides this state the moment the reload starts, with the
            // keyboard still standing on that button.
            onVisibleChanged: Qt.callLater(page.recoverIfStranded)
        }
    }

    // ── Context menu ───────────────────────────────────────────────────────
    // The shared list, plus the one verb that only exists inside a playlist.
    // "Go to album" and "Go to artist" are on because this is a music surface
    // whose shell reaches both pages, and a track that cannot be followed back
    // to its record is a dead end.
    ItemMenu {
        id: itemMenu

        allowMusicNavigation: true
        allowRemoveFromPlaylist: true
        onRemoveFromPlaylistRequested: item => PlaylistCtl.removeItem(item)
    }

    StrmToastHost {
        id: toasts

        anchors.fill: parent
        z: 200
    }

    // ── Rename / delete ────────────────────────────────────────────────────
    StrmPanel {
        id: renameSheet

        property string seed: ""

        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(Theme.scale(460), page.width - Theme.spacingLoose * 2)
        visible: false
        z: 900
        title: qsTr("Rename playlist")

        function open() {
            visible = true
            renameField.text = renameSheet.seed
            renameField.forceActiveFocus()
        }
        // Closing hides the field the keyboard is standing on, so the landing
        // is named rather than left to whatever Qt does with an invisible
        // focus item.
        function close() {
            visible = false
            if (renameButton.enabled)
                renameButton.forceActiveFocus(Qt.OtherFocusReason)
            else
                page.recoverFocus()
        }
        function commit() {
            const wanted = renameField.text.trim()
            if (wanted.length === 0 || wanted === renameSheet.seed) {
                renameSheet.close()
                return
            }
            PlaylistCtl.rename(PlaylistCtl.currentId, wanted)
            renameSheet.close()
        }

        Column {
            width: parent.width
            spacing: Theme.spacingValue

            StrmSearchField {
                id: renameField

                width: parent.width
                placeholderText: qsTr("Playlist name")
                onAccepted: renameSheet.commit()
                // The field's own signal, not Keys.onEscapePressed: an attached
                // handler re-declared here would REPLACE the one inside
                // StrmSearchField rather than add to it.
                onEscapePressed: renameSheet.close()
            }

            Row {
                anchors.right: parent.right
                spacing: Theme.spacingTight

                StrmButton {
                    text: qsTr("Cancel")
                    variant: "ghost"
                    onClicked: renameSheet.close()
                }

                StrmButton {
                    text: qsTr("Rename")
                    variant: "primary"
                    enabled: renameField.text.trim().length > 0
                             && renameField.text.trim() !== renameSheet.seed
                    onClicked: renameSheet.commit()
                }
            }
        }
    }

    StrmPanel {
        id: confirmDelete

        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(Theme.scale(440), page.width - Theme.spacingLoose * 2)
        visible: false
        z: 900
        title: qsTr("Delete this playlist?")

        function open() {
            visible = true
            cancelDelete.forceActiveFocus()
        }
        function close() {
            visible = false
            page.recoverFocus()
        }

        Column {
            width: parent.width
            spacing: Theme.spacingValue

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                // Named, because "are you sure" without saying what is being
                // deleted is how the wrong thing gets deleted.
                text: qsTr("\"%1\" will be removed from the server. The tracks themselves are not deleted.")
                          .arg(PlaylistCtl.currentName)
                color: Theme.textSecondaryColor
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontBodySize
            }

            Row {
                anchors.right: parent.right
                spacing: Theme.spacingTight

                StrmButton {
                    id: cancelDelete

                    text: qsTr("Cancel")
                    variant: "ghost"
                    onClicked: confirmDelete.close()
                    KeyNavigation.right: confirmDeleteButton
                }

                StrmButton {
                    id: confirmDeleteButton

                    text: qsTr("Delete")
                    destructive: true
                    onClicked: {
                        PlaylistCtl.remove(PlaylistCtl.currentId)
                        confirmDelete.close()
                    }
                    KeyNavigation.left: cancelDelete
                }
            }
        }
    }
}
