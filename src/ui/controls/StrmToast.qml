import QtQuick
import StrmQt

// StrmToast — one transient message (ARCHITECTURE.md).
//
// A toast is a *notification*, not a dialog: it never takes focus and never
// blocks. The only focusable things on it are its optional action and its
// dismiss button, and both are reachable with the mouse; keyboard users get the
// same information from the surface that raised it.
Item {
    id: toast

    property string text: ""
    property string severity: "info"   // "info" | "success" | "warning" | "error"
    property string actionText: ""

    signal actionTriggered()
    // Additive to the brief: the host needs a way to hear the ✕, and a toast
    // that cannot be dismissed early is an annoyance rather than a message.
    signal dismissRequested()

    readonly property bool hovered: hover.hovered

    readonly property color severityColor: severity === "success" ? Theme.positive
                                         : severity === "warning" ? Theme.warningColor
                                         : severity === "error" ? Theme.negative
                                         : Theme.textSecondaryColor

    readonly property string severityIcon: severity === "success" ? "check"
                                         : severity === "error" ? "close"
                                         : "info"

    // Sized from TextMetrics rather than from the live Text item: binding the
    // toast's width to a wrapped Text's implicitWidth, while that Text's own
    // width is anchored inside the toast, is exactly the shape of a binding
    // loop. TextMetrics measures the unwrapped string independently.
    TextMetrics {
        id: metrics
        font: messageText.font
        text: toast.text
    }

    readonly property int chromeWidth: Theme.iconSize + Theme.scale(96)
                                       + (toast.actionText.length > 0 ? Theme.scale(88) : 0)

    implicitWidth: Math.min(Theme.scale(460),
                            Math.max(Theme.scale(280), metrics.width + chromeWidth))
    implicitHeight: Math.max(Theme.controlHeightLarge,
                             messageText.contentHeight + Theme.spacingValue * 1.5)
    width: implicitWidth
    height: implicitHeight

    HoverHandler {
        id: hover
    }

    Rectangle {
        id: surface
        anchors.fill: parent
        radius: Theme.radiusPanel
        color: Theme.surfaceOverlay
        border.width: 1
        border.color: Theme.hairline

        // Severity is carried by a leading bar rather than by tinting the whole
        // surface: an error toast must still be readable, and a red panel is
        // not.
        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: Theme.scale(3)
            radius: width / 2
            color: toast.severityColor
        }
    }

    StrmIcon {
        id: severityGlyph
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacingValue
        anchors.verticalCenter: parent.verticalCenter
        name: toast.severityIcon
        size: Theme.iconSize
        color: toast.severityColor
    }

    Text {
        id: messageText
        anchors.left: severityGlyph.right
        anchors.leftMargin: Theme.spacingTight * 1.5
        anchors.right: toast.actionText.length > 0 ? actionButton.left : dismissButton.left
        anchors.rightMargin: Theme.spacingTight
        anchors.verticalCenter: parent.verticalCenter
        text: toast.text
        color: Theme.textPrimaryColor
        font.family: Theme.fontBody
        font.pixelSize: Theme.fontSmall
        wrapMode: Text.WordWrap
        maximumLineCount: 3
        elide: Text.ElideRight
    }

    StrmButton {
        id: actionButton
        anchors.right: dismissButton.left
        anchors.rightMargin: Theme.spacingTight / 2
        anchors.verticalCenter: parent.verticalCenter
        visible: toast.actionText.length > 0
        text: toast.actionText
        variant: "ghost"
        onClicked: toast.actionTriggered()
    }

    StrmIconButton {
        id: dismissButton
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingTight / 2
        anchors.verticalCenter: parent.verticalCenter
        iconName: "close"
        round: true
        tooltip: qsTr("Dismiss")
        onClicked: toast.dismissRequested()
    }
}
