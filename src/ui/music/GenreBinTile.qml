pragma ComponentBehavior: Bound
import QtQuick
import StrmQt

// A genre as a record bin (spec §4): three covers fanned from the stack, the
// name in wide caps, "N records" under it. The all-genres bin ends in an arrow
// instead of a count.
Item {
    id: tile

    property string name: ""
    property string subtitle: ""
    property var covers: []
    property int size: Theme.crateSleeveSize
    property bool current: false
    property bool isAllBin: false
    property bool hovered: hover.hovered

    readonly property Item allArrow: arrow

    signal activated()

    implicitWidth: tile.size
    implicitHeight: stack.height + Theme.spacingTight + label.implicitHeight + Theme.scale(2) + meta.height
    width: tile.implicitWidth
    height: tile.implicitHeight

    Accessible.role: Accessible.Button
    Accessible.name: tile.subtitle.length > 0 ? tile.name + ", " + tile.subtitle : tile.name
    Accessible.onPressAction: tile.activated()

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        acceptedButtons: Qt.LeftButton
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: tile.activated()
    }

    Item {
        id: stack

        width: tile.size
        height: Math.round(tile.size * 0.8)
        scale: tile.current ? Theme.focusScale : (tile.hovered ? Theme.hoverScale : 1.0)

        Behavior on scale {
            NumberAnimation {
                duration: tile.current ? Theme.animFastMs : Theme.animInstant
                easing.type: tile.current ? Theme.easeStandard : Theme.easeInstant
            }
        }

        Repeater {
            model: 3

            // index 0 is the back of the stack, 2 the front cover.
            delegate: Rectangle {
                id: bin

                required property int index

                readonly property int depth: 2 - bin.index
                readonly property string url: tile.covers && tile.covers.length > bin.depth
                                              ? String(tile.covers[bin.depth]) : ""

                width: Math.round(tile.size * 0.62)
                height: bin.width
                x: (stack.width - bin.width) / 2
                   + (bin.depth === 1 ? -tile.size * 0.16 : bin.depth === 2 ? tile.size * 0.16 : 0)
                y: stack.height - bin.height - (bin.depth === 0 ? 0 : tile.size * 0.05)
                z: bin.index
                rotation: bin.depth === 1 ? -9 : bin.depth === 2 ? 9 : 0
                antialiasing: true
                radius: Theme.crateSleeveRadius
                color: Theme.surfaceRaisedColor
                border.width: 1
                border.color: Theme.hairline

                StrmImage {
                    anchors.fill: parent
                    anchors.margins: 1
                    source: bin.url
                    suppressWarnings: true
                }
            }
        }

        FocusRing {
            active: tile.current
            radius: Theme.radiusCardValue
            inset: -Theme.scale(3)
        }
    }

    CrateHeading {
        id: label

        anchors.top: stack.bottom
        anchors.topMargin: Theme.spacingTight
        width: tile.size
        pixelSize: Theme.crateStripSize
        text: tile.name
    }

    Item {
        id: meta

        anchors.top: label.bottom
        anchors.topMargin: Theme.scale(2)
        width: tile.size
        height: Math.max(kicker.implicitHeight, arrow.size)

        CrateKicker {
            id: kicker
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            visible: !tile.isAllBin && tile.subtitle.length > 0
            text: tile.subtitle
        }

        StrmIcon {
            id: arrow
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            visible: tile.isAllBin
            name: "arrow-right"
            size: Theme.scale(18)
            color: Theme.accentColor
        }
    }
}
