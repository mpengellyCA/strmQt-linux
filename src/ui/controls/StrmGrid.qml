pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import StrmQt

// StrmGrid — the paged library grid (ARCHITECTURE.md).
//
// Mirrors StrmRail's signal surface so a page can drive a rail and a grid with
// the same handlers. Unlike the rail, a grid scrolls vertically, so the wheel
// needs no special routing here — the vertical axis genuinely belongs to it.
//
// ── View modes (ARCHITECTURE.md) ──────────────────────────────────────────────
// `viewMode` is the page-facing knob: "poster" (2:3 art), "wide" (16:9 art) and
// "list" (one row per item). All three are the *same* GridView — one model, one
// currentIndex, one prefetch path, one scroll position — because switching the
// shape of a library must not refetch it and must not lose where the user was.
// "list" is that GridView with a single column and a row-shaped delegate, which
// is why Page Up/Down, Home/End, focus wrapping and paging all keep working
// without a second implementation of any of them.
//
// `cardScale` is the card-size control. It multiplies the metrics card and the
// list row height; the poster/still delegate is a StrmCard carried at that
// scale, so hover and focus raises still compose on top of it.
FocusScope {
    id: grid

    property var gridModel: null
    property string cardVariant: "poster"
    // "poster" | "wide" | "list". Pages that never offer a choice leave this
    // alone and keep driving `cardVariant` as before.
    property string viewMode: "poster"
    // 1.0 is the Theme's native card size; the library's size control walks a
    // small set of steps either side of it.
    property real cardScale: 1.0
    // 0 derives the column count from the available width.
    property int cellsAcross: 0
    property string emptyText: qsTr("Nothing here yet")
    // Route-stable owner id used by bounded navigation history. Page call
    // sites provide it; visual/BFS order is deliberately irrelevant.
    property string navigationFocusKey: ""
    // How close to the end of loaded content counts as "near", in items.
    property int prefetchThreshold: 30

    signal itemActivated(int index)
    signal itemPlayRequested(int index)
    signal itemPlayedToggled(int index)
    signal itemFavoriteToggled(int index)
    signal menuRequested(int index, real x, real y)
    // Raised when the user is approaching the end of what is loaded, so the
    // page can fetch the next page before they hit the edge.
    signal nearEnd()

    readonly property alias currentIndex: view.currentIndex
    readonly property string navigationFocusKind: "grid"
    readonly property bool navigationFocusRestorePending: navigationFocus.pending
    property bool _navigationFocusWriting: false
    property bool _navigationFocusPrefetchSuppressed: false

    function navigationFocusSnapshot(): var { return navigationFocus.snapshot() }
    function restoreNavigationFocus(identity, index): bool {
        return navigationFocus.restore(identity, index)
    }
    function cancelNavigationFocusRestore(): void { navigationFocus.cancel() }
    function _cancelNavigationFocusForUser(): void {
        if (!grid._navigationFocusWriting)
            navigationFocus.cancel()
    }
    function _applyNavigationFocus(index): void {
        grid._navigationFocusWriting = true
        grid._navigationFocusPrefetchSuppressed = true
        if (index >= 0) {
            view.currentIndex = index
            view.positionViewAtIndex(index, GridView.Contain)
        }
        view.forceActiveFocus(Qt.OtherFocusReason)
        grid._navigationFocusWriting = false
        Qt.callLater(() => { grid._navigationFocusPrefetchSuppressed = false })
    }

    // See StrmRail.hoveredIndex: hover is published separately from focus, and
    // its owner is the delegate object rather than an index.
    readonly property int hoveredIndex: grid._hoveredIndex
    property int _hoveredIndex: -1
    property Item _hoverOwner: null
    readonly property alias count: view.count

    // "wide" is StrmCard's 16:9 variant; there is no fifth card shape here.
    readonly property string effectiveVariant: grid.viewMode === "wide" ? "still"
                                                                        : grid.cardVariant
    readonly property bool listMode: grid.viewMode === "list"

    // Same single-source-of-truth sizing probe as StrmRail.
    StrmCard {
        id: metrics
        visible: false
        enabled: false
        variant: grid.effectiveVariant
    }

    readonly property int cardWidth: Math.round(metrics.implicitWidth * grid.cardScale)
    readonly property int cardHeight: Math.round(metrics.implicitHeight * grid.cardScale)
    // One list row: art on the left at 16:9, then the labels, then the actions.
    readonly property int rowHeight: Math.round(Theme.scale(76) * grid.cardScale)
    readonly property int columns: grid.listMode
        ? 1
        : (cellsAcross > 0
           ? cellsAcross
           : Math.max(1, Math.floor(view.width / (cardWidth + Theme.spacingValue))))

    function activateCurrent() {
        grid._cancelNavigationFocusForUser()
        if (view.currentIndex >= 0)
            grid.itemActivated(view.currentIndex)
    }

    // ── Keeping the user's place across a mode or size change ──────────────
    // Cell geometry changes under the viewport, so contentY stops meaning what
    // it meant a frame ago. Remember the item at the top-left *before* the
    // relayout and put it back afterwards: the library is not refetched, the
    // model is untouched, and currentIndex — the focused item — never moves.
    property int _anchorIndex: -1

    function _rememberAnchor() {
        if (view.count <= 0)
            return
        let anchor = view.indexAt(view.contentX + 1, view.contentY + 1)
        if (anchor < 0)
            anchor = view.currentIndex
        grid._anchorIndex = anchor
        Qt.callLater(grid._restoreAnchor)
    }

    function _restoreAnchor() {
        if (grid._anchorIndex < 0 || grid._anchorIndex >= view.count)
            return
        // forceLayout() first, and it is not optional: cellWidth/cellHeight are
        // already the new values by now, but the GridView only re-places its
        // items in the polish phase. Without this, positionViewAtIndex() maps
        // the anchor through the *old* geometry and lands somewhere else.
        view.forceLayout()
        view.positionViewAtIndex(grid._anchorIndex, GridView.Beginning)
        grid._anchorIndex = -1
    }

    onViewModeChanged: grid._rememberAnchor()
    onCardScaleChanged: grid._rememberAnchor()

    // The two conditions that mean "prefetch now": the keyboard cursor is close
    // to the end, or the viewport is showing the last rows. Either is enough —
    // a mouse user who never moves currentIndex still has to trigger paging.
    //
    // Fires at most once per loaded-item count, so a page handler can call
    // loadMore() unconditionally: contentY changes on every scrolled pixel and
    // an unthrottled signal would be a request storm.
    property int _lastNearEndCount: -1

    // The throttle is keyed on the loaded count, so it has to be told when the
    // rows are replaced rather than added to. MusicCtl.albums and .artists are
    // CONSTANT Q_PROPERTIES — the model object never changes — and a filter
    // change clears and refills them in place. A refilled list that happens to
    // be the same length as the old one would otherwise sit at
    // `count === _lastNearEndCount` and never page again.
    //
    // MediaItemModel::setItems() is a model RESET, which is the signal that
    // means "different rows" independently of how many there are.
    Connections {
        target: Qt.isQtObject(grid.gridModel) ? grid.gridModel : null
        ignoreUnknownSignals: true
        function onModelReset() {
            grid._lastNearEndCount = -1
            Qt.callLater(navigationFocus.retry)
        }
    }

    NavigationFocusRestorer {
        id: navigationFocus
        model: grid.gridModel
        count: view.count
        currentIndex: view.currentIndex
        onFocusRequested: index => grid._applyNavigationFocus(index)
    }

    Connections {
        target: grid
        function onActiveFocusChanged() {
            if (!grid.activeFocus)
                grid._cancelNavigationFocusForUser()
        }
    }

    onCountChanged: navigationFocus.retry()
    onGridModelChanged: navigationFocus.cancel()

    function _checkNearEnd() {
        if (grid._navigationFocusPrefetchSuppressed || view.count <= 0
                || view.count === grid._lastNearEndCount)
            return
        const cursorNear = view.currentIndex >= 0
                           && view.count - view.currentIndex < grid.prefetchThreshold
        const lastVisible = view.indexAt(view.contentX + view.width / 2,
                                         view.contentY + view.height - 1)
        const viewportNear = view.atYEnd
                             || (lastVisible >= 0
                                 && view.count - lastVisible < grid.prefetchThreshold)
        if (cursorNear || viewportNear) {
            grid._lastNearEndCount = view.count
            grid.nearEnd()
        }
    }

    GridView {
        id: view

        anchors.fill: parent
        anchors.leftMargin: Theme.pageMarginValue
        anchors.rightMargin: Theme.pageMarginValue
        focus: true
        clip: true
        model: grid.gridModel

        cellWidth: grid.listMode
                   ? Math.max(1, width)
                   : (grid.columns > 0 ? Math.floor(width / grid.columns)
                                       : grid.cardWidth + Theme.spacingValue)
        cellHeight: grid.listMode ? grid.rowHeight + Theme.spacingTight
                                  : grid.cardHeight + Theme.spacingValue
        cacheBuffer: cellHeight * 3
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds

        keyNavigationWraps: false
        highlightMoveDuration: Theme.animFastMs
        preferredHighlightBegin: 0
        preferredHighlightEnd: height - cellHeight
        highlightRangeMode: GridView.ApplyRange

        ScrollBar.vertical: StrmScrollBar {}

        // A FocusScope rather than a plain Item, for the same reason StrmRail's
        // delegate is one: a card's ✓/♥/⋯ are real buttons that take focus on
        // tap, and a plain Item never reports activeFocus for a focused
        // descendant. Without this, tapping a verb on cell 7 left currentIndex
        // on cell 3 — the focus ring drew on a card the keyboard had left, and
        // the next arrow key moved from there. Focus only: hover still never
        // moves the cursor.
        delegate: FocusScope {
            id: cell

            required property int index
            required property var model

            width: view.cellWidth
            height: view.cellHeight

            onActiveFocusChanged: {
                if (cell.activeFocus) {
                    if (cell.index !== view.currentIndex)
                        grid._cancelNavigationFocusForUser()
                    view.currentIndex = cell.index
                }
            }

            readonly property bool current: cell.GridView.isCurrentItem && view.activeFocus
            // A wide card asks for wide art. An episode's "poster" is a 16:9
            // still, so drawing it in a 2:3 frame crops it to a fragment of the
            // frame it came from — which is what Continue Watching looked like.
            // thumbUrl is empty when the item has nothing suitable, and then the
            // poster is still the honest answer.
            readonly property string wideArt: {
                const thumb = cell.model.thumbUrl !== undefined ? cell.model.thumbUrl : "";
                if (thumb.length > 0)
                    return thumb;
                return cell.model.posterUrl !== undefined ? cell.model.posterUrl : "";
            }
            readonly property string posterArt: cell.model.posterUrl !== undefined
                                                ? cell.model.posterUrl : ""
            readonly property string title: cell.model.label !== undefined
                ? cell.model.label
                : (cell.model.name !== undefined ? cell.model.name : "")
            readonly property string subtitle: cell.model.subtitle !== undefined
                                               ? cell.model.subtitle : ""
            readonly property real itemProgress: cell.model.progress !== undefined
                                                 ? cell.model.progress : 0
            readonly property bool itemPlayed: cell.model.played === true
            readonly property bool itemFavorite: cell.model.favorite === true
            readonly property int itemUnplayed: cell.model.unplayedCount !== undefined
                                                ? cell.model.unplayedCount : 0

            // Ownership is the delegate itself, not its index: `index` is
            // already reset to -1 by the time Component.onDestruction runs, so
            // an index comparison there can never match, and a card removed
            // from under a resting pointer left the published index pointing at
            // a row that no longer exists — with nothing left alive to clear it.
            function setHovered(on) {
                if (on) {
                    grid._hoveredIndex = cell.index
                    grid._hoverOwner = cell
                } else if (grid._hoverOwner === cell) {
                    grid._hoverOwner = null
                    grid._hoveredIndex = -1
                }
            }

            Component.onDestruction: cell.setHovered(false)

            // A pooled delegate stays alive, so Component.onDestruction is not
            // enough once GridView reuse is enabled. Never let the published
            // pointer-hover index retain the identity this cell used to have.
            GridView.onPooled: cell.setHovered(false)
            GridView.onReused: cell.setHovered((!grid.listMode && cardItem.hovered)
                                               || (grid.listMode && rowHover.hovered))

            // A row removed above this card renumbers it without the pointer
            // moving, so the published index has to follow it.
            onIndexChanged: {
                if (grid._hoverOwner === cell && cell.index >= 0)
                    grid._hoveredIndex = cell.index
            }

            // Click and Return both mean "open", and both take the keyboard
            // cursor with them so the two input models stay in agreement.
            function open() {
                grid._cancelNavigationFocusForUser()
                view.currentIndex = cell.index
                view.forceActiveFocus(Qt.MouseFocusReason)
                grid.itemActivated(cell.index)
            }

            // ── Poster / wide ──────────────────────────────────────────────
            StrmCard {
                id: cardItem
                anchors.centerIn: parent
                visible: !grid.listMode
                enabled: !grid.listMode
                // The size control rides on top of the card's own hover and
                // focus raises, which multiply with it rather than replace it.
                scale: grid.cardScale
                variant: grid.effectiveVariant
                // Empty in list mode so a hidden card never pulls artwork the
                // user is not being shown.
                imageUrl: {
                    if (grid.listMode)
                        return "";
                    const wide = grid.effectiveVariant === "still"
                                 || grid.effectiveVariant === "backdrop";
                    return wide ? cell.wideArt : cell.posterArt;
                }
                label: cell.title
                sublabel: cell.subtitle
                progress: cell.itemProgress
                played: cell.itemPlayed
                favorite: cell.itemFavorite
                unplayedCount: cell.itemUnplayed
                highlighted: cell.current

                onHoveredChanged: cell.setHovered(hovered)

                onActivated: cell.open()
                onPlayRequested: grid.itemPlayRequested(cell.index)
                onPlayedToggled: grid.itemPlayedToggled(cell.index)
                onFavoriteToggled: grid.itemFavoriteToggled(cell.index)
                onMenuRequested: (mx, my) => grid.menuRequested(cell.index, mx, my)
            }

            // ── List row ───────────────────────────────────────────────────
            // Same verbs, same hover-vs-focus rule (ARCHITECTURE.md): the row
            // background follows the pointer, the amber ring follows the
            // keyboard, and pointing at a row never moves focus onto it.
            Rectangle {
                id: row

                visible: grid.listMode
                enabled: grid.listMode
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                // Room for the focus ring, which is drawn outside the row and
                // would otherwise be clipped against the viewport edges.
                anchors.margins: Theme.focusRingWidth
                height: grid.rowHeight - 2 * Theme.focusRingWidth
                radius: Theme.radiusCardValue
                color: rowHover.hovered ? Theme.surfaceRaisedColor : Theme.surfaceColor
                border.width: 1
                border.color: (rowHover.hovered || cell.current) ? Theme.hairline : "transparent"

                Behavior on color {
                    ColorAnimation { duration: Theme.animInstant; easing.type: Theme.easeInstant }
                }

                HoverHandler {
                    id: rowHover
                    cursorShape: Qt.PointingHandCursor
                    onHoveredChanged: cell.setHovered(rowHover.hovered)
                }

                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: cell.open()
                }

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: eventPoint => {
                        const p = row.mapToItem(null, eventPoint.position.x,
                                                eventPoint.position.y)
                        grid.menuRequested(cell.index, p.x, p.y)
                    }
                }

                Rectangle {
                    id: rowArt

                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.margins: Theme.spacingTight
                    width: Math.round(height * 16 / 9)
                    radius: Theme.radiusChip
                    color: Theme.ground
                    clip: true

                    Image {
                        anchors.fill: parent
                        source: grid.listMode ? cell.wideArt : ""
                        sourceSize.width: Math.round(rowArt.width * Screen.devicePixelRatio)
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

                    // Resume position, exactly where StrmCard draws it.
                    Rectangle {
                        visible: cell.itemProgress > 0.01 && !cell.itemPlayed
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: Theme.scale(3)
                        color: Theme.accentMuted

                        Rectangle {
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: parent.width * Math.max(0, Math.min(cell.itemProgress, 1))
                            color: Theme.accentColor
                        }
                    }
                }

                Column {
                    anchors.left: rowArt.right
                    anchors.leftMargin: Theme.spacingValue
                    anchors.right: rowActions.left
                    anchors.rightMargin: Theme.spacingValue
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.scale(2)

                    Text {
                        width: parent.width
                        text: cell.title
                        color: (rowHover.hovered || cell.current) ? Theme.textPrimaryColor
                                                                  : Theme.textSecondaryColor
                        font.family: Theme.fontBody
                        font.pixelSize: Theme.fontBodySize
                        elide: Text.ElideRight
                        maximumLineCount: 1

                        Behavior on color {
                            ColorAnimation {
                                duration: Theme.animInstant
                                easing.type: Theme.easeInstant
                            }
                        }
                    }

                    Text {
                        width: parent.width
                        visible: cell.subtitle.length > 0
                        text: cell.subtitle
                        color: Theme.textTertiary
                        font.family: Theme.fontBody
                        font.pixelSize: Theme.fontSmall
                        elide: Text.ElideRight
                        maximumLineCount: 1
                    }
                }

                // State first, then the pointer-only verbs. The indicators stay
                // put when the actions fade in, so nothing jumps under the
                // cursor on hover.
                Row {
                    id: rowActions

                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingValue
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spacingTight

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: cell.itemUnplayed > 0 && !cell.itemPlayed
                        width: Math.max(Theme.scale(24),
                                        rowCount.implicitWidth + Theme.spacingTight * 1.5)
                        height: Theme.scale(24)
                        radius: height / 2
                        color: Theme.accentColor

                        Text {
                            id: rowCount
                            anchors.centerIn: parent
                            text: cell.itemUnplayed
                            color: Theme.accentText
                            font.family: Theme.fontBody
                            font.pixelSize: Theme.fontCaption
                            font.bold: true
                        }
                    }

                    StrmIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: cell.itemPlayed
                        name: "check"
                        size: Theme.iconSize
                        color: Theme.positive
                    }

                    // Always laid out, only faded: if the verbs appeared and
                    // disappeared the labels beside them would reflow every
                    // time the pointer crossed a row.
                    Item {
                        width: rowVerbs.implicitWidth
                        height: rowVerbs.implicitHeight
                        anchors.verticalCenter: parent.verticalCenter
                        opacity: rowHover.hovered ? 1 : 0
                        enabled: opacity > 0.01

                        Behavior on opacity {
                            NumberAnimation {
                                duration: Theme.animInstant
                                easing.type: Theme.easeInstant
                            }
                        }

                        Row {
                            id: rowVerbs
                            spacing: Theme.scale(2)

                            StrmIconButton {
                                iconName: "play"
                                size: Theme.scale(32)
                                tooltip: qsTr("Play")
                                onClicked: grid.itemPlayRequested(cell.index)
                            }

                            StrmIconButton {
                                iconName: "check"
                                size: Theme.scale(32)
                                checked: cell.itemPlayed
                                tooltip: cell.itemPlayed ? qsTr("Mark unwatched")
                                                         : qsTr("Mark watched")
                                onClicked: grid.itemPlayedToggled(cell.index)
                            }

                            StrmIconButton {
                                iconName: cell.itemFavorite ? "heart-filled" : "heart"
                                size: Theme.scale(32)
                                checked: cell.itemFavorite
                                tooltip: cell.itemFavorite ? qsTr("Remove from favourites")
                                                           : qsTr("Add to favourites")
                                onClicked: grid.itemFavoriteToggled(cell.index)
                            }

                            StrmIconButton {
                                id: rowMore
                                iconName: "more-horizontal"
                                size: Theme.scale(32)
                                tooltip: qsTr("More…")
                                onClicked: {
                                    const p = rowMore.mapToItem(null, rowMore.width / 2,
                                                                rowMore.height)
                                    grid.menuRequested(cell.index, p.x, p.y)
                                }
                            }
                        }
                    }
                }

                FocusRing {
                    active: cell.current
                    anchors.fill: parent
                    radius: Theme.radiusCardValue
                    inset: -Theme.focusRingWidth
                }
            }
        }

        onCurrentIndexChanged: grid._checkNearEnd()
        onContentYChanged: grid._checkNearEnd()
        onCountChanged: grid._checkNearEnd()

        // Guard isAutoRepeat: a held/stuck Return must not machine-gun activations.
        Keys.onReturnPressed: event => { if (!event.isAutoRepeat) grid.activateCurrent() }
        Keys.onEnterPressed: event => { if (!event.isAutoRepeat) grid.activateCurrent() }

        // A screen at a time. GridView does not do this itself, so before now
        // the only way down a 1300-item library was one row per keypress — and
        // the gamepad's triggers were mapped to a jump that did not exist.
        function _pageRows() {
            const rows = Math.floor(view.height / Math.max(1, view.cellHeight));
            return Math.max(1, rows) * Math.max(1, grid.columns);
        }

        Keys.onPressed: event => {
            grid._cancelNavigationFocusForUser()
            if (view.count === 0)
                return;
            if (event.key === Qt.Key_PageDown) {
                view.currentIndex = Math.min(view.count - 1, view.currentIndex + view._pageRows());
                event.accepted = true;
            } else if (event.key === Qt.Key_PageUp) {
                view.currentIndex = Math.max(0, view.currentIndex - view._pageRows());
                event.accepted = true;
            } else if (event.key === Qt.Key_Home) {
                view.currentIndex = 0;
                event.accepted = true;
            } else if (event.key === Qt.Key_End) {
                view.currentIndex = view.count - 1;
                event.accepted = true;
            }
        }
    }

    Text {
        anchors.centerIn: parent
        visible: view.count === 0 && grid.emptyText.length > 0
        text: grid.emptyText
        color: Theme.textTertiary
        font.family: Theme.fontBody
        font.pixelSize: Theme.fontTitle
    }
}
