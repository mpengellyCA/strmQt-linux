import QtQuick
import StrmQt

// PageHeader — the title block every page opens with (ARCHITECTURE.md).
//
// Deliberately tiny: a title, an optional subtitle (usually a count or a year
// range), and a right-hand slot for whatever controls that page owns. It exists
// so seven pages do not invent seven slightly different heading treatments —
// the thing that makes an app read as assembled rather than designed.
//
//   PageHeader {
//       title: LibraryCtl.title
//       subtitle: qsTr("%1 items").arg(LibraryCtl.model.count)
//       StrmSelect { ... }      // default slot → right-hand actions
//       StrmIconButton { ... }
//   }
//
// Children land in the actions row, right-aligned and vertically centred; the
// header takes its height from whichever side is taller, so a page with tall
// controls does not need to know the text metrics.
Item {
    id: header

    property string title: ""
    property string subtitle: ""
    // Gap between the title block and the actions, and between actions.
    property int spacing: Theme.spacingValue

    default property alias actions: actionRow.data

    implicitWidth: titleBlock.implicitWidth + header.spacing + actionRow.implicitWidth
    implicitHeight: Math.max(titleBlock.implicitHeight, actionRow.implicitHeight)

    Column {
        id: titleBlock

        anchors.left: parent.left
        anchors.right: actionRow.left
        anchors.rightMargin: header.spacing
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.scale(2)

        Text {
            width: parent.width
            text: header.title
            visible: header.title.length > 0
            color: Theme.textPrimaryColor
            font.family: Theme.fontDisplay
            font.pixelSize: Theme.fontHeading
            font.weight: Font.DemiBold
            elide: Text.ElideRight
            maximumLineCount: 1
        }

        Text {
            width: parent.width
            text: header.subtitle
            visible: header.subtitle.length > 0
            color: Theme.textSecondaryColor
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontBodySize
            elide: Text.ElideRight
            maximumLineCount: 1
        }
    }

    Row {
        id: actionRow

        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: header.spacing
    }
}
