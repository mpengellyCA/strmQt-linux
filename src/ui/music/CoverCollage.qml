pragma ComponentBehavior: Bound
import QtQuick
import StrmQt

// A square of covers: a 2×2 grid when there are four, else the first one whole.
// Stations draw one; nothing about it is interactive.
Item {
    id: collage

    property var covers: []
    property int size: Theme.crateSleeveSize
    property int radius: Theme.crateSleeveRadius

    readonly property int coverCount: collage.covers ? collage.covers.length : 0
    readonly property bool grid: collage.coverCount >= 4

    width: collage.size
    height: collage.size

    Rectangle {
        anchors.fill: parent
        radius: collage.radius
        color: Theme.surfaceRaisedColor
        clip: true

        StrmIcon {
            anchors.centerIn: parent
            name: "lib-music"
            size: Math.round(collage.size / 3)
            color: Theme.textTertiary
        }

        StrmImage {
            anchors.fill: parent
            visible: !collage.grid
            source: !collage.grid && collage.coverCount > 0 ? String(collage.covers[0]) : ""
            suppressWarnings: true
        }

        Grid {
            anchors.fill: parent
            columns: 2
            visible: collage.grid

            Repeater {
                model: collage.grid ? 4 : 0

                delegate: StrmImage {
                    id: quarter

                    required property int index

                    width: collage.size / 2
                    height: collage.size / 2
                    source: String(collage.covers[quarter.index])
                    suppressWarnings: true
                }
            }
        }
    }
}
