import QtQuick
import QtQuick.Templates as T
import StrmQt

// PairingCodeOverlay — the code a phone types to pair with the web remote.
//
// A phone that is not paired asks for a code (Pair); the server makes one up
// and this shows it, over everything, the film included, until the phone sends
// it back, it runs out (WebRemoteCtl.pairingSeconds) or Back dismisses it. It
// takes no focus and blocks nothing: whatever was being used goes on being
// used underneath. A StrmLight's code is never shown here; the StrmLight shows
// its own (WebRemoteServer.h, "Pairing").
//
// In the window's overlay, above the on-screen keyboard (z 1000).
Item {
    id: overlay

    readonly property string code: WebRemoteCtl ? WebRemoteCtl.pairingCode : ""
    property int secondsLeft: 0

    parent: T.Overlay.overlay
    anchors.fill: parent
    z: 1100
    visible: overlay.code.length > 0

    onCodeChanged: overlay.secondsLeft = WebRemoteCtl ? WebRemoteCtl.pairingSeconds : 0

    Timer {
        interval: 1000
        repeat: true
        running: overlay.visible && overlay.secondsLeft > 0
        onTriggered: overlay.secondsLeft -= 1
    }

    // Back from a keyboard, a remote, a pad or the phone takes it down.
    MappedShortcut {
        actionId: "nav.back"
        active: overlay.visible
        onActivated: WebRemoteCtl.cancelPairing()
    }

    Rectangle {
        id: card

        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.spacingValue * 2, Theme.scale(520))
        height: content.implicitHeight + Theme.spacingLoose * 2
        radius: Theme.radiusPanel
        color: Theme.surfaceOverlay
        border.width: 1
        border.color: Theme.hairline

        // A click on the card goes no further.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
        }

        Column {
            id: content

            anchors.centerIn: parent
            width: parent.width - Theme.spacingLoose * 2
            spacing: Theme.spacingValue

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: qsTr("Enter this code on your phone")
                color: Theme.textPrimaryColor
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.fontTitle
            }

            Text {
                objectName: "pairingCode"
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                textFormat: Text.PlainText
                text: overlay.code.split("").join(" ")
                color: Theme.accentColor
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontHero
                font.weight: Font.Bold
            }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: qsTr("It works once and goes in %1:%2. Back dismisses it.")
                      .arg(Math.floor(overlay.secondsLeft / 60))
                      .arg(String(overlay.secondsLeft % 60).padStart(2, "0"))
                color: Theme.textSecondaryColor
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontSmall
            }
        }
    }
}
