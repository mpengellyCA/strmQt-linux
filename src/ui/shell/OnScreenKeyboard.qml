pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import StrmQt

// OnScreenKeyboard — typing for a pad, a TV remote and the phone remote.
//
// It draws OnScreenKeyboardCtl (src/input/OnScreenKeyboard.h) and decides
// nothing. The text field keeps the focus the whole time: the controller takes
// the arrows, Select and Back while this is up and moves the highlighted key,
// and a click on a key is press(row, column). Nothing here takes focus, not
// even on a click, because the field has to keep it.
//
// In the window's overlay and above the sheets there (PlaylistPage's rename
// sheet is z 900), because the field being typed into may be in one of them or
// in a popup.
Item {
    id: keyboard

    parent: T.Overlay.overlay
    anchors.fill: parent
    z: 1000
    visible: OnScreenKeyboardCtl.open

    // One letter key; every row is ten of them wide.
    readonly property real unit: (panel.width - panel.padding * 2) / 10

    function label(kind, text): string {
        switch (kind) {
        case "shift":     return qsTr("Shift");
        case "symbols":   return OnScreenKeyboardCtl.symbols ? qsTr("abc") : qsTr("#+=");
        case "space":     return qsTr("Space");
        case "backspace": return qsTr("Delete");
        case "done":      return qsTr("Done");
        default:          return text;
        }
    }

    Rectangle {
        id: panel

        readonly property real padding: Theme.spacingValue

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacingLoose
        width: Math.min(parent.width - Theme.spacingValue * 2, Theme.scale(880))
        height: content.implicitHeight + panel.padding * 2
        radius: Theme.radiusPanel
        color: Theme.surfaceOverlay
        border.width: 1
        border.color: Theme.hairline

        // A click between the keys lands here and goes no further, never on
        // whatever is under the keyboard.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            onWheel: wheel => wheel.accepted = true
        }

        Column {
            id: content

            x: panel.padding
            y: panel.padding
            width: panel.width - panel.padding * 2
            spacing: Theme.spacingTight

            // What the field holds, since the keyboard may be covering it.
            Rectangle {
                width: parent.width
                height: Theme.controlHeightLarge
                radius: Theme.radiusChip
                color: Theme.surfaceColor
                border.width: 1
                border.color: Theme.hairline

                Text {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacingTight * 1.5
                    anchors.rightMargin: Theme.spacingTight * 1.5
                    verticalAlignment: Text.AlignVCenter
                    // The end of a long entry is where the typing is.
                    elide: Text.ElideLeft
                    textFormat: Text.PlainText
                    text: OnScreenKeyboardCtl.text
                    color: Theme.textPrimaryColor
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontBodyLarge
                }
            }

            Repeater {
                model: OnScreenKeyboardCtl.rows

                Row {
                    id: keyRow

                    required property var modelData
                    required property int index

                    Repeater {
                        model: keyRow.modelData

                        Item {
                            id: key

                            required property var modelData
                            required property int index

                            objectName: "key-" + keyRow.index + "-" + key.index
                            readonly property bool current: OnScreenKeyboardCtl.row === keyRow.index
                                                            && OnScreenKeyboardCtl.column === key.index
                            readonly property bool latched: key.modelData.kind === "shift"
                                                            && OnScreenKeyboardCtl.shifted

                            width: keyboard.unit * key.modelData.width
                            height: Theme.controlHeightLarge

                            Rectangle {
                                id: cap

                                anchors.fill: parent
                                anchors.margins: Theme.scale(3)
                                radius: Theme.radiusChip
                                color: key.latched ? Theme.accentMuted
                                     : hover.hovered || tap.pressed ? Theme.surfaceRaisedColor
                                     : Theme.surfaceColor

                                Text {
                                    anchors.centerIn: parent
                                    visible: key.modelData.kind !== "left"
                                             && key.modelData.kind !== "right"
                                    textFormat: Text.PlainText
                                    text: keyboard.label(key.modelData.kind, key.modelData.text)
                                    color: Theme.textPrimaryColor
                                    font.family: Theme.fontBody
                                    font.pixelSize: key.modelData.kind === "char" ? Theme.fontBodyLarge
                                                                                  : Theme.fontSmall
                                }

                                StrmIcon {
                                    anchors.centerIn: parent
                                    visible: key.modelData.kind === "left"
                                             || key.modelData.kind === "right"
                                    name: key.modelData.kind === "left" ? "chevron-left"
                                                                        : "chevron-right"
                                }
                            }

                            FocusRing {
                                active: key.current
                                anchors.fill: cap
                                radius: Theme.radiusChip
                            }

                            HoverHandler {
                                id: hover
                            }

                            TapHandler {
                                id: tap
                                onTapped: OnScreenKeyboardCtl.press(keyRow.index, key.index)
                            }
                        }
                    }
                }
            }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("Back closes the keyboard. Done finishes.")
                color: Theme.textTertiary
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontCaption
            }
        }
    }
}
