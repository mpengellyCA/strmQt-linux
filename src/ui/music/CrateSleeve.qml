import QtQuick
import StrmQt

// An album as a record shop shows it (spec §2): a square cover with a deep
// shadow and a caption under it. No card surface.
//
// Focus lives with whoever holds the sleeve. In a rail that is the cell, which
// drives `current`. Hover lifts the art a little, and never sets `current`.
Item {
    id: sleeve

    property string coverUrl: ""
    property string title: ""
    property string subtitle: ""
    property string badge: ""
    property bool hiRes: false
    property int size: Theme.crateSleeveSize
    property bool current: false
    property bool showCaption: true
    property bool hovered: hover.hovered

    signal activated()
    signal playRequested()
    signal menuRequested(real x, real y)

    readonly property real lift: sleeve.current ? Theme.focusScale
                                                : (sleeve.hovered ? Theme.hoverScale : 1.0)

    implicitWidth: sleeve.size
    implicitHeight: sleeve.size + (sleeve.showCaption ? Theme.spacingTight + caption.implicitHeight : 0)
    width: sleeve.implicitWidth
    height: sleeve.implicitHeight

    Accessible.role: Accessible.Button
    Accessible.name: sleeve.subtitle.length > 0 ? sleeve.title + ", " + sleeve.subtitle : sleeve.title
    Accessible.onPressAction: sleeve.activated()

    function requestMenuAt(px: real, py: real): void {
        const p = sleeve.mapToItem(null, px, py)
        sleeve.menuRequested(p.x, p.y)
    }

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        acceptedButtons: Qt.LeftButton
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: sleeve.activated()
        // A held press is the touch and remote path to the menu.
        onLongPressed: sleeve.requestMenuAt(art.width / 2, art.height * 0.75)
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: eventPoint => sleeve.requestMenuAt(eventPoint.position.x, eventPoint.position.y)
    }

    Item {
        id: art

        width: sleeve.size
        height: sleeve.size
        scale: sleeve.lift

        Behavior on scale {
            NumberAnimation {
                duration: sleeve.current ? Theme.animFastMs : Theme.animInstant
                easing.type: sleeve.current ? Theme.easeStandard : Theme.easeInstant
            }
        }

        // The deep shadow, as two offset plates. A MultiEffect per sleeve would
        // break batching across a shelf of twenty.
        Rectangle {
            x: -Theme.scale(2)
            y: Theme.crateSleeveElevation.y * 0.6
            width: parent.width + Theme.scale(4)
            height: parent.height
            radius: Theme.crateSleeveRadius + Theme.scale(4)
            color: Theme.shadowColor
            opacity: Theme.crateSleeveElevation.opacity * 0.3
        }

        Rectangle {
            y: Theme.crateSleeveElevation.y * 0.25
            width: parent.width
            height: parent.height
            radius: Theme.crateSleeveRadius
            color: Theme.shadowColor
            opacity: Theme.crateSleeveElevation.opacity * 0.6
        }

        Rectangle {
            id: cover

            anchors.fill: parent
            radius: Theme.crateSleeveRadius
            color: Theme.surfaceRaisedColor
            clip: true

            StrmIcon {
                anchors.centerIn: parent
                visible: image.status !== Image.Ready
                name: "lib-music"
                size: Math.round(sleeve.size / 3)
                color: Theme.textTertiary
            }

            StrmImage {
                id: image
                anchors.fill: parent
                source: sleeve.coverUrl
            }

            StrmIconButton {
                anchors.centerIn: parent
                // Shown for focus as well as hover: an affordance only a mouse can
                // find is the bug the controls library exists to prevent.
                opacity: (sleeve.hovered || sleeve.current) ? 1 : 0
                visible: opacity > 0.01
                iconName: "play"
                round: true
                tooltip: qsTr("Play")
                onClicked: sleeve.playRequested()

                Behavior on opacity {
                    NumberAnimation { duration: Theme.animInstant; easing.type: Theme.easeInstant }
                }
            }
        }

        FocusRing {
            active: sleeve.current
            radius: Theme.crateSleeveRadius + Theme.scale(3)
            inset: -Theme.scale(3)
        }
    }

    Column {
        id: caption

        visible: sleeve.showCaption
        anchors.top: art.bottom
        anchors.topMargin: Theme.spacingTight
        width: sleeve.size
        spacing: Theme.scale(2)

        Text {
            width: parent.width
            text: sleeve.title
            color: Theme.textPrimaryColor
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontSmall
            font.weight: Font.DemiBold
            textFormat: Text.PlainText
            elide: Text.ElideRight
            maximumLineCount: 1
        }

        Row {
            id: subtitleRow

            width: parent.width
            spacing: Theme.spacingTight

            Text {
                width: subtitleRow.width - (badgeItem.visible ? badgeItem.width + subtitleRow.spacing : 0)
                anchors.verticalCenter: parent.verticalCenter
                text: sleeve.subtitle
                color: Theme.textSecondaryColor
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontCaption
                textFormat: Text.PlainText
                elide: Text.ElideRight
                maximumLineCount: 1
            }

            CrateBadge {
                id: badgeItem
                anchors.verticalCenter: parent.verticalCenter
                text: sleeve.badge
                hiRes: sleeve.hiRes
            }
        }
    }
}
