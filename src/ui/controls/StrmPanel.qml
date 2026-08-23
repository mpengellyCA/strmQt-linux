import QtQuick
import QtQuick.Effects
import StrmQt

// StrmPanel — the elevated surface behind OSD panels, dialogs and settings
// sections (ARCHITECTURE.md). Elevation is a shadow + surface pair, never a
// "slightly lighter colour" guess (ARCHITECTURE.md).
//
// Children are laid out in a column inside the panel's padding:
//
//   StrmPanel {
//       title: qsTr("Playback")
//       StrmSwitch { text: qsTr("Direct play") }
//       StrmSwitch { text: qsTr("Hardware decode") }
//   }
//
// The shadow is a MultiEffect over the background rectangle. It is drawn BEHIND
// the still-visible background rather than the usual "hide the source" trick:
// a hidden source renders nothing into its layer in some paint paths, and a
// panel that silently disappears is a far worse failure than one extra opaque
// draw of a rectangle that is instanced a handful of times per screen.
Item {
    id: panel

    property string title: ""
    property string subtitle: ""
    property int padding: Theme.spacingValue
    property int elevation: 2                    // 1–4 → Theme.elevation1..4

    default property alias content: body.data

    readonly property var _shadow: elevation >= 4 ? Theme.elevation4
                                 : elevation === 3 ? Theme.elevation3
                                 : elevation === 1 ? Theme.elevation1
                                 : Theme.elevation2

    implicitWidth: Math.max(header.implicitWidth, body.implicitWidth) + padding * 2
    implicitHeight: (header.visible ? header.height + Theme.spacingValue : 0)
                    + body.implicitHeight + padding * 2

    MultiEffect {
        anchors.fill: background
        source: background
        autoPaddingEnabled: true
        shadowEnabled: true
        shadowColor: "#000000"
        shadowBlur: panel._shadow.blur
        shadowVerticalOffset: panel._shadow.y
        shadowOpacity: panel._shadow.opacity
    }

    Rectangle {
        id: background
        anchors.fill: parent
        // layer.enabled makes the rectangle a texture provider so MultiEffect
        // above can sample it.
        layer.enabled: true
        radius: Theme.radiusPanel
        color: Theme.surfaceOverlay
        border.width: 1
        border.color: Theme.hairline
    }

    Column {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: panel.padding
        spacing: Theme.scale(2)
        visible: panel.title.length > 0 || panel.subtitle.length > 0

        Text {
            width: parent.width
            visible: panel.title.length > 0
            text: panel.title
            color: Theme.textPrimaryColor
            font.family: Theme.fontDisplay
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        Text {
            width: parent.width
            visible: panel.subtitle.length > 0
            text: panel.subtitle
            color: Theme.textSecondaryColor
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.WordWrap
        }
    }

    Column {
        id: body
        anchors.top: header.visible ? header.bottom : parent.top
        anchors.topMargin: header.visible ? Theme.spacingValue : panel.padding
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: panel.padding
        anchors.rightMargin: panel.padding
        spacing: Theme.spacingTight
    }
}
