import QtQuick
import StrmQt

// A station (spec §4): a 2×2 collage with a wide-caps label. One press
// resolves and plays; the menu offers Play / Shuffle / Add to queue.
Item {
    id: tile

    property var covers: []
    property string label: ""
    property int size: Theme.crateSleeveSize
    property bool current: false
    property bool hovered: hover.hovered

    signal activated()
    signal menuRequested(real x, real y)

    implicitWidth: tile.size
    implicitHeight: tile.size + Theme.spacingTight + caption.implicitHeight
    width: tile.implicitWidth
    height: tile.implicitHeight

    Accessible.role: Accessible.Button
    Accessible.name: qsTr("Station: %1").arg(tile.label)
    Accessible.onPressAction: tile.activated()

    function requestMenuAt(px: real, py: real): void {
        const p = tile.mapToItem(null, px, py)
        tile.menuRequested(p.x, p.y)
    }

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        acceptedButtons: Qt.LeftButton
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: tile.activated()
        onLongPressed: tile.requestMenuAt(art.width / 2, art.height * 0.75)
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: eventPoint => tile.requestMenuAt(eventPoint.position.x, eventPoint.position.y)
    }

    Item {
        id: art

        width: tile.size
        height: tile.size
        scale: tile.current ? Theme.focusScale : (tile.hovered ? Theme.hoverScale : 1.0)

        Behavior on scale {
            NumberAnimation {
                duration: tile.current ? Theme.animFastMs : Theme.animInstant
                easing.type: tile.current ? Theme.easeStandard : Theme.easeInstant
            }
        }

        Rectangle {
            y: Theme.crateSleeveElevation.y * 0.25
            width: parent.width
            height: parent.height
            radius: Theme.crateSleeveRadius
            color: Theme.shadowColor
            opacity: Theme.crateSleeveElevation.opacity * 0.6
        }

        CoverCollage {
            covers: tile.covers
            size: tile.size
        }

        FocusRing {
            active: tile.current
            radius: Theme.crateSleeveRadius + Theme.scale(3)
            inset: -Theme.scale(3)
        }
    }

    CrateHeading {
        id: caption

        anchors.top: art.bottom
        anchors.topMargin: Theme.spacingTight
        width: tile.size
        pixelSize: Theme.crateStripSize
        text: tile.label
    }
}
