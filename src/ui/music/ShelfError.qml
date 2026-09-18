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
            id: retryButton

            anchors.verticalCenter: parent.verticalCenter
            focus: true
            // Ruling P3-R2, and the quietest instance of it in the crate: a
            // consumer hides this whole line when the error clears
            // (CrateShelf's `visible: shelf.showError`), which is exactly what a
            // successful Retry does while the keyboard is still on the button.
            // StrmButton ties activeFocusOnTab to `interactive`, which `visible`
            // never touches, so the binding does not merely get refused — it
            // never re-evaluates at all, and the stop stays DECLARED on a hidden
            // button with no Qt warning to show for it. Tab traversal skips the
            // hidden line either way; what this corrects is the property value,
            // read directly by NavRail.qml:43 and :98.
            activeFocusOnTab: retryButton.visible || retryButton.activeFocus
            text: qsTr("Retry")
            iconName: "refresh"
            variant: "ghost"
            onClicked: shelfError.retry()
        }
    }
}
