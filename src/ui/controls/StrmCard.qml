import QtQuick
import QtQuick.Window
import StrmQt

// StrmCard — the one media tile in the app (ARCHITECTURE.md).
//
// Replaces `components/PosterCard.qml`, which had no pointer input at all and
// conflated hover with focus. The cardinal rule here (ARCHITECTURE.md):
//
//   hover  → HoverHandler,  Theme.hoverScale, Theme.animInstant, follows the
//            cursor, fades the overlay actions in, brightens the label.
//   focus  → `highlighted` (driven by the containing view from
//            `ListView.isCurrentItem && view.activeFocus`), amber FocusRing,
//            Theme.focusScale, Theme.animFastMs, sticky.
//
// They are independent booleans and both may be true at once: the ring still
// draws and the larger (focus) scale wins. Hovering NEVER calls
// forceActiveFocus() — moving the mouse must not steal the keyboard's place.
//
// Elevation note: ARCHITECTURE.md describes elevation as a shadow+surface pair.
// The surface half (surfaceColor → surfaceRaisedColor plus a brightening
// hairline) is done here; the shadow half is deliberately NOT a per-delegate
// MultiEffect, because that is one offscreen render target per card and it
// breaks scene-graph batching across a whole grid. StrmPanel/StrmMenu, which
// exist one at a time, carry the real shadows.
Item {
    id: card

    // ── Contract ───────────────────────────────────────────────────────────
    // "poster" 2:3 (default) · "still" 16:9 · "square" 1:1 · "backdrop" wide
    property string variant: "poster"
    property string imageUrl: ""
    property string label: ""
    property string sublabel: ""
    property real progress: 0            // 0–1, resume position
    property bool played: false
    property bool favorite: false        // additive: lets the ♥ render its state
    property int unplayedCount: 0
    property string badgeText: ""        // free-form corner badge ("4K", "HDR", …)
    property bool highlighted: false     // keyboard/gamepad focus, view-driven
    property bool showOverlayActions: true

    signal activated()
    signal playRequested()
    signal playedToggled()
    signal favoriteToggled()
    // x/y are SCENE coordinates (mapToItem(null, …)) so a StrmMenu anchored
    // anywhere in the window can be placed without knowing the card's ancestry.
    signal menuRequested(real x, real y)

    // ── Derived geometry ───────────────────────────────────────────────────
    readonly property int mediaWidth: variant === "still" ? Theme.stillWidth
                                    : variant === "backdrop" ? Theme.scale(420)
                                    : Theme.posterWidthValue
    readonly property int mediaHeight: variant === "still" ? Theme.stillHeight
                                     : variant === "backdrop" ? Math.round(Theme.scale(420) * 9 / 16)
                                     : variant === "square" ? Theme.posterWidthValue
                                     : Theme.posterHeightValue
    // The label block is a fixed reservation rather than content-driven, so
    // every card in a rail or grid row is exactly the same height whether or
    // not this particular item happens to have a sublabel.
    readonly property int labelBlockHeight: Theme.spacingTight
                                            + Math.round(Theme.fontSmall * 1.45)
                                            + Math.round(Theme.fontCaption * 1.45)

    readonly property bool hovered: hover.hovered
    readonly property bool raised: hovered || highlighted

    implicitWidth: mediaWidth
    implicitHeight: mediaHeight + labelBlockHeight
    width: implicitWidth
    height: implicitHeight

    Accessible.role: Accessible.ListItem
    Accessible.name: card.label
    Accessible.description: card.sublabel
    Accessible.selectable: true
    Accessible.selected: card.highlighted
    Accessible.focused: card.highlighted
    Accessible.onPressAction: card.activate()

    function activate() { card.activated() }

    // Scene-space menu request from an arbitrary child point.
    function requestMenuAt(item, px, py) {
        const p = item.mapToItem(null, px, py)
        card.menuRequested(p.x, p.y)
    }

    // ── Pointer ────────────────────────────────────────────────────────────
    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        acceptedButtons: Qt.LeftButton
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: card.activated()
    }

    // Right-click anywhere on the card opens the context menu (ARCHITECTURE.md).
    TapHandler {
        id: rightTap
        acceptedButtons: Qt.RightButton
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: (eventPoint) => card.requestMenuAt(card, eventPoint.position.x,
                                                     eventPoint.position.y)
    }

    // Guard isAutoRepeat: a held/stuck Return must not machine-gun activations.
    // Only reached when the card itself holds focus; inside a view the view
    // owns the keys and drives `highlighted`.
    Keys.onReturnPressed: event => { if (!event.isAutoRepeat) card.activated() }
    Keys.onEnterPressed: event => { if (!event.isAutoRepeat) card.activated() }

    // ── Visual ─────────────────────────────────────────────────────────────
    Item {
        id: visual
        anchors.fill: parent
        transformOrigin: Item.Center
        // Focus wins when both are true: it is the larger raise and the slower,
        // deliberate curve.
        scale: card.highlighted ? Theme.focusScale
             : card.hovered ? Theme.hoverScale : 1.0

        Behavior on scale {
            NumberAnimation {
                duration: card.highlighted ? Theme.animFastMs : Theme.animInstant
                easing.type: card.highlighted ? Theme.easeStandard : Theme.easeInstant
            }
        }

        Rectangle {
            id: frame
            width: card.mediaWidth
            height: card.mediaHeight
            radius: Theme.radiusCardValue
            clip: true
            color: card.raised ? Theme.surfaceRaisedColor : Theme.surfaceColor
            border.width: 1
            border.color: card.raised ? Theme.hairline : "transparent"

            Behavior on color {
                ColorAnimation { duration: Theme.animInstant; easing.type: Theme.easeInstant }
            }

            Image {
                id: art
                anchors.fill: parent
                source: card.imageUrl
                // Never full-res: the provider downscales server-side to the
                // width we actually draw. Height is left unset so the aspect
                // ratio is preserved during decode.
                //
                // mediaWidth is LOGICAL pixels, so on any scaled display this
                // asked the server for half (or a third) of the pixels it then
                // drew, and every card came back soft. Episode stills showed it
                // worst because they are photographic rather than flat poster art.
                //
                // card.scale is how the size control resizes a card, so it has
                // to be part of the request too: at the larger steps the card
                // draws bigger than mediaWidth and would otherwise be handed a
                // small image to upscale.
                sourceSize.width: Math.round(card.mediaWidth * Math.max(1, card.scale)
                                             * Screen.devicePixelRatio)
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                cache: true
                opacity: status === Image.Ready ? 1 : 0

                Behavior on opacity {
                    NumberAnimation { duration: Theme.animNormalMs; easing.type: Theme.easeStandard }
                }
            }

            // Placeholder while loading / when the item has no artwork.
            Text {
                anchors.centerIn: parent
                width: parent.width - Theme.spacingLoose
                visible: art.status !== Image.Ready
                text: card.label
                color: Theme.textSecondaryColor
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontBodySize
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                maximumLineCount: 4
                elide: Text.ElideRight
            }

            // ── Indicators (preserved from PosterCard) ─────────────────────
            Rectangle {
                id: countBadge
                visible: card.unplayedCount > 0 && !card.played
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: Theme.spacingTight
                width: Math.max(Theme.scale(24), countText.implicitWidth + Theme.spacingTight * 1.5)
                height: Theme.scale(24)
                radius: height / 2
                color: Theme.accentColor

                Text {
                    id: countText
                    anchors.centerIn: parent
                    text: card.unplayedCount
                    color: Theme.accentText
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontCaption
                    font.bold: true
                }
            }

            Rectangle {
                visible: card.played
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: Theme.spacingTight
                width: Theme.scale(24)
                height: Theme.scale(24)
                radius: height / 2
                color: Theme.positive

                StrmIcon {
                    anchors.centerIn: parent
                    name: "check"
                    size: Theme.scale(15)
                    color: Theme.accentText
                }
            }

            // Free-form badge ("4K", "HDR", …), top-left so it never collides
            // with the played / unplayed indicators.
            Rectangle {
                visible: card.badgeText.length > 0
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.margins: Theme.spacingTight
                width: badgeLabel.implicitWidth + Theme.spacingTight
                height: Theme.scale(20)
                radius: Theme.radiusChip
                color: Theme.scrimColor

                Text {
                    id: badgeLabel
                    anchors.centerIn: parent
                    text: card.badgeText
                    color: Theme.textPrimaryColor
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontCaption
                }
            }

            // Resume progress bar.
            Rectangle {
                visible: card.progress > 0.01 && !card.played
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: Theme.scale(4)
                color: Theme.accentMuted

                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: parent.width * Math.max(0, Math.min(card.progress, 1))
                    color: Theme.accentColor
                }
            }

            // ── Hover overlay (ARCHITECTURE.md) ───────────────────────────────
            // Pointer-only: it exists to give the mouse the actions the
            // keyboard reaches through the context menu, so it tracks `hovered`
            // and NOT `highlighted`.
            Item {
                id: overlay
                anchors.fill: parent
                opacity: (card.showOverlayActions && card.hovered) ? 1 : 0
                visible: opacity > 0.01
                enabled: visible

                Behavior on opacity {
                    NumberAnimation { duration: Theme.animInstant; easing.type: Theme.easeInstant }
                }

                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: Theme.veil }
                        GradientStop { position: 0.45; color: Theme.veil }
                        GradientStop { position: 1.0; color: Theme.scrimColor }
                    }
                }

                StrmIconButton {
                    id: playButton
                    anchors.centerIn: parent
                    iconName: "play"
                    round: true
                    tooltip: qsTr("Play")
                    onClicked: card.playRequested()
                }

                Row {
                    anchors.bottom: parent.bottom
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottomMargin: card.progress > 0.01 && !card.played
                                          ? Theme.spacingTight + Theme.scale(4)
                                          : Theme.spacingTight
                    spacing: Theme.spacingTight

                    StrmIconButton {
                        iconName: "check"
                        round: true
                        checked: card.played
                        tooltip: card.played ? qsTr("Mark unwatched") : qsTr("Mark watched")
                        onClicked: card.playedToggled()
                    }

                    StrmIconButton {
                        iconName: card.favorite ? "heart-filled" : "heart"
                        round: true
                        checked: card.favorite
                        tooltip: card.favorite ? qsTr("Remove from favourites")
                                               : qsTr("Add to favourites")
                        onClicked: card.favoriteToggled()
                    }

                    StrmIconButton {
                        id: moreButton
                        iconName: "more-horizontal"
                        round: true
                        tooltip: qsTr("More…")
                        onClicked: card.requestMenuAt(moreButton, moreButton.width / 2,
                                                      moreButton.height)
                    }
                }
            }
        }

        // Amber ring, drawn outside the frame so it never covers artwork.
        FocusRing {
            active: card.highlighted
            anchors.fill: frame
            radius: Theme.radiusCardValue
            inset: -Theme.focusRingWidth
        }

        // ── Labels ─────────────────────────────────────────────────────────
        Text {
            id: labelText
            anchors.top: frame.bottom
            anchors.topMargin: Theme.spacingTight
            width: card.mediaWidth
            text: card.label
            // Both hover and focus brighten the label; neither owns it.
            color: card.raised ? Theme.textPrimaryColor : Theme.textSecondaryColor
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontSmall
            elide: Text.ElideRight
            maximumLineCount: 1

            Behavior on color {
                ColorAnimation { duration: Theme.animInstant; easing.type: Theme.easeInstant }
            }
        }

        Text {
            anchors.top: labelText.bottom
            width: card.mediaWidth
            visible: card.sublabel.length > 0
            text: card.sublabel
            color: Theme.textTertiary
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontCaption
            elide: Text.ElideRight
            maximumLineCount: 1
        }
    }
}
