import QtQuick
import StrmQt

// A failed shelf collapses to this one line (spec §4). The other shelves stay
// live, so this never claims the page and never takes focus on its own.
FocusScope {
    id: shelfError

    property string message: ""

    signal retry()

    implicitWidth: row.implicitWidth
    implicitHeight: row.implicitHeight
    height: implicitHeight

    Row {
        id: row

        spacing: Theme.spacingValue

        StrmIcon {
            anchors.verticalCenter: parent.verticalCenter
            name: "info"
            size: Theme.scale(18)
            color: Theme.textSecondaryColor
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: shelfError.message
            color: Theme.textSecondaryColor
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontSmall
            textFormat: Text.PlainText
            Accessible.role: Accessible.AlertMessage
            Accessible.name: shelfError.message
        }

        StrmButton {
            anchors.verticalCenter: parent.verticalCenter
            focus: true
            text: qsTr("Retry")
            iconName: "refresh"
            variant: "ghost"
            onClicked: shelfError.retry()
        }
    }
}
