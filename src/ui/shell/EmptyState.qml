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
            id: actionButton

            anchors.horizontalCenter: parent.horizontalCenter
            visible: empty.actionText.length > 0
            // Ruling P3-R2: most EmptyState instances leave actionText empty,
            // so without this override the action button still DECLARES a Tab
            // stop where nothing is drawn for it. Measured at Qt 6.11.2: Tab
            // traversal skips an invisible item whatever activeFocusOnTab
            // says, so the override corrects the property value, which
            // NavRail.qml:43 and :98 read directly.
            //
            // `|| activeFocus` covers the case the bare form cannot. `visible`
            // is EFFECTIVE visibility, so it goes false the moment the whole
            // empty state hides — which is exactly what a successful Retry
            // does, while the keyboard is still standing on the Retry. Qt
            // refuses to clear activeFocusOnTab on the active focus item,
            // warns, and keeps the old value; `visible` never changes again, so
            // the binding would never get a second chance. Measured on a real
            // window: one warning, and a stop left declared on an invisible
            // button for the rest of the page's life. The page still has to
            // move the keyboard off it — Qt does not clear focus on hide — but
            // this keeps the property honest for whoever reads it.
            activeFocusOnTab: actionButton.visible || actionButton.activeFocus
            text: empty.actionText
            iconName: empty.actionIcon
            variant: "primary"
            onClicked: empty.actionTriggered()
        }
    }
}
