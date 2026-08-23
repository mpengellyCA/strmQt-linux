import QtQuick
import QtQuick.Controls.Basic
import StrmQt

// Search input for the top bar, the search page and the command palette.
//
// `text`, `placeholderText`, `textEdited()` and `accepted()` are TextField's
// own API and are deliberately not redeclared here — they already mean exactly
// what the contract asks for. `cleared()` and `escapePressed()` are added,
// because "the user emptied the box" and "the user wants out" are decisions the
// owning page has to make (usually: drop the query, pop the page).
//
// The focus ring is the field's own border rather than a FocusRing overlay: a
// ring floating around a text box reads as a second, empty control.
TextField {
    id: field

    property bool showClear: true

    signal cleared
    signal escapePressed

    readonly property int glyphInset: Theme.spacingTight + Theme.scale(2)
    readonly property bool clearVisible: field.showClear && field.text.length > 0

    implicitHeight: Theme.controlHeight
    implicitWidth: Theme.scale(280)

    leftPadding: field.glyphInset + Theme.iconSize + Theme.spacingTight
    rightPadding: field.clearVisible ? clearButton.width + Theme.spacingTight : Theme.spacingValue
    topPadding: 0
    bottomPadding: 0

    color: Theme.textPrimaryColor
    placeholderTextColor: Theme.textTertiary
    selectionColor: Theme.accentColor
    selectedTextColor: Theme.accentText
    font.family: Theme.fontBody
    font.pixelSize: Theme.fontBodySize
    verticalAlignment: TextInput.AlignVCenter
    hoverEnabled: true
    selectByMouse: true

    background: Rectangle {
        radius: Theme.radiusChip
        color: Theme.surfaceColor
        border.width: field.activeFocus ? Theme.focusRingWidth : 1
        border.color: field.activeFocus ? Theme.accentColor
                    : field.hovered ? Theme.textTertiary
                    : Theme.hairline

        Behavior on border.color {
            ColorAnimation {
                duration: Theme.animFastMs
                easing.type: Theme.easeStandard
            }
        }
    }

    StrmIcon {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: field.glyphInset
        name: "search"
        color: field.activeFocus ? Theme.accentColor : Theme.textTertiary
    }

    // Only there when there is something to clear; an always-on × is noise.
    StrmIconButton {
        id: clearButton

        anchors.verticalCenter: parent.verticalCenter
        anchors.right: parent.right
        anchors.rightMargin: Theme.scale(4)
        visible: field.clearVisible
        iconName: "close"
        round: true
        size: Theme.scale(26)
        onClicked: {
            field.clear();
            field.cleared();
            field.forceActiveFocus(Qt.MouseFocusReason);
        }
    }

    // A text field's pointer is a caret, not a hand; the clear button carries
    // its own PointingHandCursor.
    HoverHandler {
        cursorShape: Qt.IBeamCursor
    }

    Keys.onEscapePressed: event => {
        field.escapePressed();
        event.accepted = true;
    }
}
