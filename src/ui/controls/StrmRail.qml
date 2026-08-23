pragma ComponentBehavior: Bound
import QtQuick
import StrmQt

// StrmRail — one horizontal shelf of StrmCards (ARCHITECTURE.md).
//
// Replaces `components/MediaRail.qml`. The keyboard behaviour of that file was
// the good part of the prototype and is preserved verbatim: no wrapping,
// ApplyRange highlighting so the focused card is never jammed against the
// screen edge, and isAutoRepeat guards on activation. What is new is that the
// mouse now exists here at all: hover chevrons, edge fades, and — the one that
// actually bit users — explicit wheel routing.
FocusScope {
    id: rail

    property string title: ""
    property var railModel: null
    property string cardVariant: "poster"
    property bool showMore: false
    property string emptyText: qsTr("Nothing here yet")

    signal itemActivated(int index)
    signal itemPlayRequested(int index)
    // Additive to the brief, and the reason the overlay actions are not dead
    // buttons: a card's ✓/♥ have to reach the page that owns the model.
    signal itemPlayedToggled(int index)
    signal itemFavoriteToggled(int index)
    signal menuRequested(int index, real x, real y)
    signal moreRequested()

    readonly property alias currentIndex: list.currentIndex

    // Which card the pointer is over, or -1. Published separately from
    // currentIndex because hover and focus are separate states: a page driving
    // a backdrop wash wants to follow the pointer without the pointer ever
    // moving the keyboard's place. A card only clears this if it is still the
    // one that set it, and "still the one" is the delegate *object* rather than
    // its index — see cell.setHovered().
    readonly property int hoveredIndex: rail._hoveredIndex
    property int _hoveredIndex: -1
    property Item _hoverOwner: null
    readonly property alias count: list.count
    readonly property bool hovered: railHover.hovered

    // A single hidden card is the one source of truth for "how big is a card of
    // this variant", instead of a copy of the variant→size table living in the
    // rail, the grid and the card all at once.
    StrmCard {
        id: metrics
        visible: false
        enabled: false
        variant: rail.cardVariant
    }

    readonly property int cardWidth: metrics.implicitWidth
    readonly property int cardHeight: metrics.implicitHeight
    // Headroom so a focused card's Theme.focusScale raise is not clipped by the
    // list's own clip rectangle.
    readonly property int rowPadding: Math.ceil(cardHeight * (Theme.focusScale - 1) / 2)
                                      + Theme.spacingTight

    width: parent ? parent.width : implicitWidth
    implicitWidth: Theme.scale(800)
    height: headingRow.height + Theme.spacingValue + list.height

    // ── Scrolling ──────────────────────────────────────────────────────────
    function _clampX(x) {
        const minX = list.originX
        const maxX = Math.max(minX, list.originX + list.contentWidth - list.width)
        return Math.max(minX, Math.min(maxX, x))
    }

    // Immediate (wheel): the pointer expects 1:1 tracking, so no animation.
    function scrollBy(dx) {
        scrollAnim.stop()
        list.contentX = rail._clampX(list.contentX + dx)
    }

    // Animated (chevrons): roughly one viewport, minus half a card so the
    // card that was at the edge stays visible as an anchor.
    function scrollPage(direction) {
        const step = Math.max(rail.cardWidth, list.width - rail.cardWidth * 0.5)
        scrollAnim.stop()
        scrollAnim.from = list.contentX
        scrollAnim.to = rail._clampX(list.contentX + direction * step)
        scrollAnim.start()
    }

    NumberAnimation {
        id: scrollAnim
        target: list
        property: "contentX"
        duration: Theme.animNormalMs
        easing.type: Theme.easeEmphasis
        onFinished: list.returnToBounds()
    }

    function activateCurrent() {
        if (list.currentIndex >= 0)
            rail.itemActivated(list.currentIndex)
    }

    HoverHandler {
        id: railHover
        // No cursorShape here: the rail's background is not clickable, only the
        // cards and the chevrons on it are, and each of those sets its own.
    }

    // ── Heading ────────────────────────────────────────────────────────────
    Item {
        id: headingRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: Theme.pageMarginValue
        anchors.rightMargin: Theme.pageMarginValue
        height: heading.implicitHeight

        Text {
            id: heading
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: rail.title
            color: Theme.textPrimaryColor
            font.family: Theme.fontDisplay
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
        }

        // "See all →" is a hover affordance, but it is also shown whenever the
        // rail holds keyboard focus — an affordance only a mouse can discover
        // is the bug this milestone exists to fix.
        StrmButton {
            id: moreButton
            anchors.left: heading.right
            anchors.leftMargin: Theme.spacingValue
            anchors.verticalCenter: parent.verticalCenter
            visible: rail.showMore && opacity > 0.01
            opacity: (rail.hovered || rail.activeFocus) ? 1 : 0
            text: qsTr("See all")
            iconName: "chevron-right"
            variant: "ghost"
            onClicked: rail.moreRequested()

            Behavior on opacity {
                NumberAnimation { duration: Theme.animInstant; easing.type: Theme.easeInstant }
            }
        }
    }

    // ── The shelf ──────────────────────────────────────────────────────────
    ListView {
        id: list

        anchors.top: headingRow.bottom
        anchors.topMargin: Theme.spacingValue
        anchors.left: parent.left
        anchors.right: parent.right
        height: rail.cardHeight + rail.rowPadding * 2

        orientation: ListView.Horizontal
        spacing: Theme.spacingValue
        leftMargin: Theme.pageMarginValue
        rightMargin: Theme.pageMarginValue
        focus: true
        clip: true
        model: rail.railModel
        boundsBehavior: Flickable.StopAtBounds

        // Preserved from MediaRail: no wrapping, and ApplyRange so the focused
        // card is never pinned to the screen edge.
        keyNavigationWraps: false
        highlightMoveDuration: Theme.animFastMs
        preferredHighlightBegin: Theme.pageMarginValue
        preferredHighlightEnd: width / 2
        highlightRangeMode: ListView.ApplyRange
        cacheBuffer: rail.cardWidth * 4

        // A6, part 1. WheelHandler filters by axis *before* it grabs: with
        // orientation Qt.Horizontal it declines any event whose angleDelta.x is
        // zero, so a plain (vertical) mouse wheel is never taken here and falls
        // through to the enclosing page Flickable. That is the whole bug — a
        // horizontal rail must not eat the page's scroll gesture.
        WheelHandler {
            orientation: Qt.Horizontal
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: event => rail.scrollBy(-event.angleDelta.x)
        }

        // A6, part 2 (belt and braces). Flickable::wheelEvent only accepts an
        // axis it can actually flick; pinning flickableDirection to horizontal
        // means the ListView itself also leaves vertical wheel unaccepted
        // instead of silently swallowing it while scrolling nothing.
        flickableDirection: Flickable.HorizontalFlick

        delegate: FocusScope {
            id: cell

            required property int index
            required property var model

            width: rail.cardWidth
            height: list.height

            // A FocusScope rather than a plain Item, so that focus landing
            // *inside* a card is observable at all: a card's ✓/♥/⋯ are real
            // buttons and take focus on tap, and a plain Item never reports
            // activeFocus for a focused descendant. Without this the cursor
            // stayed on whichever card the keyboard last visited, so the next
            // arrow key moved from a place the user had already left — and the
            // page's own vertical cursor, which follows this one, stayed on the
            // wrong rail. Focus only: hover still never moves the cursor.
            onActiveFocusChanged: {
                if (cell.activeFocus)
                    list.currentIndex = cell.index
            }

            // Hover ownership is the delegate itself, not its index: `index` is
            // already reset to -1 by the time Component.onDestruction runs, so
            // an index comparison there can never match, and a card removed
            // from under a resting pointer left the published index pointing at
            // a row that no longer exists — with nothing left alive to clear it.
            function setHovered(on) {
                if (on) {
                    rail._hoveredIndex = cell.index
                    rail._hoverOwner = cell
                } else if (rail._hoverOwner === cell) {
                    rail._hoverOwner = null
                    rail._hoveredIndex = -1
                }
            }

            Component.onDestruction: cell.setHovered(false)

            // A row removed above this card renumbers it without the pointer
            // moving, so the published index has to follow it.
            onIndexChanged: {
                if (rail._hoverOwner === cell && cell.index >= 0)
                    rail._hoveredIndex = cell.index
            }

            StrmCard {
                id: cardItem
                anchors.centerIn: parent
                variant: rail.cardVariant
                // A wide card asks for wide art. An episode's "poster" is a
                // 16:9 still, so drawing it in a 2:3 frame crops it to a
                // fragment of the frame it came from — which is what
                // Continue Watching looked like. thumbUrl is empty when the
                // item has nothing suitable, and then the poster is still
                // the honest answer.
                imageUrl: {
                    const wide = rail.cardVariant === "still" || rail.cardVariant === "backdrop";
                    const thumb = cell.model.thumbUrl !== undefined ? cell.model.thumbUrl : "";
                    if (wide && thumb.length > 0)
                        return thumb;
                    return cell.model.posterUrl !== undefined ? cell.model.posterUrl : "";
                }
                label: cell.model.label !== undefined ? cell.model.label
                     : (cell.model.name !== undefined ? cell.model.name : "")
                sublabel: cell.model.subtitle !== undefined ? cell.model.subtitle : ""
                progress: cell.model.progress !== undefined ? cell.model.progress : 0
                played: cell.model.played === true
                favorite: cell.model.favorite === true
                unplayedCount: cell.model.unplayedCount !== undefined
                               ? cell.model.unplayedCount : 0
                // Focus, not hover: the card's own HoverHandler owns the other
                // half of the pair and the two never touch.
                highlighted: cell.ListView.isCurrentItem && list.activeFocus

                onHoveredChanged: cell.setHovered(hovered)

                onActivated: {
                    // A click makes this card the keyboard's place too, so a
                    // subsequent arrow key continues from where the user
                    // clicked. This is a *commit*, not a hover.
                    list.currentIndex = cell.index
                    list.forceActiveFocus(Qt.MouseFocusReason)
                    rail.itemActivated(cell.index)
                }
                onPlayRequested: rail.itemPlayRequested(cell.index)
                onPlayedToggled: rail.itemPlayedToggled(cell.index)
                onFavoriteToggled: rail.itemFavoriteToggled(cell.index)
                onMenuRequested: (mx, my) => rail.menuRequested(cell.index, mx, my)
            }
        }

        // Guard isAutoRepeat: a held/stuck Return must not machine-gun activations.
        Keys.onReturnPressed: event => { if (!event.isAutoRepeat) rail.activateCurrent() }
        Keys.onEnterPressed: event => { if (!event.isAutoRepeat) rail.activateCurrent() }
    }

    // A6, part 3. The vertical axis cannot be filtered statically the way the
    // horizontal one can, because whether a vertical wheel belongs to the rail
    // depends on a runtime modifier (Shift). X11/Wayland do not translate
    // Shift+wheel into a horizontal event the way macOS does, so it arrives as
    // an ordinary vertical wheel and someone has to look at it.
    //
    // This is a MouseArea rather than a second WheelHandler on purpose:
    // `WheelEvent.accepted = false` is the documented, long-stable way to hand
    // an event back for further propagation, and handing it back is the entire
    // point. Everything that is not Shift-modified falls through from here to
    // the ListView's horizontal WheelHandler (true horizontal wheel) and then
    // past the non-vertically-flickable ListView to the page.
    MouseArea {
        anchors.fill: list
        z: 1
        acceptedButtons: Qt.NoButton   // presses pass straight through to the cards
        hoverEnabled: false            // so does hover
        onWheel: wheel => {
            if (!(wheel.modifiers & Qt.ShiftModifier) || wheel.angleDelta.y === 0) {
                wheel.accepted = false
                return
            }
            wheel.accepted = true
            rail.scrollBy(-wheel.angleDelta.y)
        }
    }

    // ── Edge fades (ARCHITECTURE.md) ────────────────────────────────────────
    // The shelf reads as continuing into the page margin rather than being
    // guillotined by the clip rectangle.
    Rectangle {
        anchors.left: list.left
        anchors.top: list.top
        anchors.bottom: list.bottom
        width: Theme.pageMarginValue
        z: 2
        visible: !list.atXBeginning
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Theme.ground }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }

    Rectangle {
        anchors.right: list.right
        anchors.top: list.top
        anchors.bottom: list.bottom
        width: Theme.pageMarginValue
        z: 2
        visible: !list.atXEnd
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 1.0; color: Theme.ground }
        }
    }

    // ── Hover chevrons (ARCHITECTURE.md) ──────────────────────────────────────
    StrmIconButton {
        id: leftChevron
        anchors.left: list.left
        anchors.leftMargin: Theme.spacingTight
        anchors.verticalCenter: list.verticalCenter
        z: 3
        iconName: "chevron-left"
        round: true
        tooltip: qsTr("Scroll left")
        enabled: !list.atXBeginning
        visible: opacity > 0.01
        opacity: (rail.hovered && !list.atXBeginning && list.count > 0) ? 1 : 0
        onClicked: rail.scrollPage(-1)

        Behavior on opacity {
            NumberAnimation { duration: Theme.animInstant; easing.type: Theme.easeInstant }
        }
    }

    StrmIconButton {
        id: rightChevron
        anchors.right: list.right
        anchors.rightMargin: Theme.spacingTight
        anchors.verticalCenter: list.verticalCenter
        z: 3
        iconName: "chevron-right"
        round: true
        tooltip: qsTr("Scroll right")
        enabled: !list.atXEnd
        visible: opacity > 0.01
        opacity: (rail.hovered && !list.atXEnd && list.count > 0) ? 1 : 0
        onClicked: rail.scrollPage(1)

        Behavior on opacity {
            NumberAnimation { duration: Theme.animInstant; easing.type: Theme.easeInstant }
        }
    }

    // ── Empty state ────────────────────────────────────────────────────────
    Text {
        anchors.centerIn: list
        visible: list.count === 0 && rail.emptyText.length > 0
        text: rail.emptyText
        color: Theme.textTertiary
        font.family: Theme.fontBody
        font.pixelSize: Theme.fontBodySize
    }
}
