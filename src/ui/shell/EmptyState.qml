import QtQuick
import StrmQt

// EmptyState — what a page shows when it has nothing (ARCHITECTURE.md).
//
// The rule this component exists to enforce: an empty state offers an action,
// it does not state a fact. "No results" is a dead end; "No results for
// 'blade' — Clear search" is a way out. `actionText` is therefore part of the
// normal shape of this component, not a decoration on it.
//
//   EmptyState {
//       iconName: "search"
//       headline: qsTr("No results for “%1”").arg(SearchCtl.query)
//       body: qsTr("Try a shorter query, or search a different library.")
//       actionText: qsTr("Clear search")
//       onActionTriggered: SearchCtl.query = ""
//   }
//
// `severity: "error"` swaps the icon tint to Theme.negative so a failed load and
// an empty shelf do not read identically.
Item {
    id: empty

    property string iconName: "info"
    property string headline: ""
    property string body: ""
    property string actionText: ""
    property string actionIcon: ""
    // "info" | "error"
    property string severity: "info"

    signal actionTriggered

    readonly property color glyphColor: empty.severity === "error"
                                        ? Theme.negative : Theme.textTertiary

    implicitWidth: Theme.scale(420)
    implicitHeight: column.implicitHeight

    Column {
        id: column

        anchors.centerIn: parent
        width: Math.min(parent.width, Theme.scale(420))
        spacing: Theme.spacingValue

        StrmIcon {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: empty.iconName.length > 0
            name: empty.iconName
            size: Theme.scale(48)
            color: empty.glyphColor
        }

        Text {
            width: parent.width
            visible: empty.headline.length > 0
            text: empty.headline
            color: Theme.textPrimaryColor
            font.family: Theme.fontDisplay
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        Text {
            width: parent.width
            visible: empty.body.length > 0
            text: empty.body
            color: Theme.textSecondaryColor
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontBodySize
            lineHeight: Theme.lineNormal
            lineHeightMode: Text.ProportionalHeight
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        StrmButton {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: empty.actionText.length > 0
            text: empty.actionText
            iconName: empty.actionIcon
            variant: "primary"
            onClicked: empty.actionTriggered()
        }
    }
}
