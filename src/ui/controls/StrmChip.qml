import QtQuick
import StrmQt

// Pill used for filters, seasons, genres and codec tags (ARCHITECTURE.md).
//
// `checked` is a *controlled* property: the chip renders it and emits
// toggled(), it never flips itself. Filter state belongs to the page or the
// query object, and a chip that mutates its own `checked` would silently break
// `checked: model.selected`-style bindings the first time it is clicked.
//
//   StrmChip { text: "Unwatched"; checked: q.unwatched; onToggled: q.unwatched = !q.unwatched }
Item {
    id: chip

    property string text: ""
    property string iconName: ""
    property bool checked: false
    property bool closable: false
    property string accessibleName: chip.text
    property string accessibleDescription: chip.closable ? qsTr("Can be removed") : ""

    signal toggled
    signal closed

    readonly property bool hovered: hover.hovered
    readonly property bool pressed: tap.pressed

    readonly property color labelColor: {
        if (!chip.enabled)
            return Theme.textDisabled;
        if (chip.checked)
            return Theme.accentText;
        if (chip.hovered || chip.activeFocus)
            return Theme.textPrimaryColor;
        return Theme.textSecondaryColor;
    }

    implicitHeight: Theme.scale(32)
    implicitWidth: row.implicitWidth + 2 * Theme.spacingValue
    activeFocusOnTab: chip.enabled

    Accessible.role: Accessible.CheckBox
    Accessible.name: chip.accessibleName
    Accessible.description: chip.accessibleDescription
    Accessible.checkable: true
    Accessible.checked: chip.checked
    Accessible.focusable: chip.enabled
    Accessible.focused: chip.activeFocus
    Accessible.pressed: chip.pressed
    Accessible.onPressAction: chip.activate()
    Accessible.onToggleAction: chip.activate()

    scale: !chip.enabled ? 1.0
         : chip.pressed ? Theme.pressScale
         : chip.activeFocus ? Theme.focusScale
         : chip.hovered ? Theme.hoverScale
         : 1.0

    Behavior on scale {
        NumberAnimation {
            duration: chip.activeFocus ? Theme.animFastMs : Theme.animInstant
            easing.type: chip.activeFocus ? Theme.easeStandard : Theme.easeInstant
        }
    }

    function activate(): void {
        if (chip.enabled)
            chip.toggled();
    }

    Rectangle {
        id: bg

        anchors.fill: parent
        radius: Theme.radiusPill
        color: chip.checked && chip.enabled ? Theme.accentColor : Theme.surfaceColor
        border.width: chip.checked ? 0 : 1
        border.color: Theme.hairline

        Behavior on color {
            ColorAnimation {
                duration: Theme.animFastMs
                easing.type: Theme.easeStandard
            }
        }
    }

    Rectangle {
        anchors.fill: bg
        radius: bg.radius
        color: !chip.enabled ? "transparent"
             : chip.pressed ? Theme.pressTint
             : chip.hovered ? Theme.hoverTint
             : "transparent"

        Behavior on color {
            ColorAnimation {
                duration: Theme.animInstant
                easing.type: Theme.easeInstant
            }
        }
    }

    Row {
        id: row

        anchors.centerIn: parent
        spacing: Theme.scale(6)

        StrmIcon {
            anchors.verticalCenter: parent.verticalCenter
            visible: chip.iconName.length > 0
            name: chip.iconName
            color: chip.labelColor
            size: Theme.scale(15)
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: chip.text.length > 0
            text: chip.text
            color: chip.labelColor
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontSmall

            Behavior on color {
                ColorAnimation {
                    duration: Theme.animInstant
                    easing.type: Theme.easeInstant
                }
            }
        }

        // Its own hit area, so dismissing a chip is never mistaken for
        // toggling it.
        Item {
            id: closeSlot

            anchors.verticalCenter: parent.verticalCenter
            visible: chip.closable
            width: Theme.scale(18)
            height: width

            StrmIcon {
                anchors.centerIn: parent
                name: "close"
                size: Theme.scale(13)
                color: closeHover.hovered ? Theme.negative : chip.labelColor
            }

            HoverHandler {
                id: closeHover
                enabled: chip.enabled
                cursorShape: Qt.PointingHandCursor
            }

            TapHandler {
                enabled: chip.enabled
                // Grab on press so the chip's own tap handler never also fires.
                gesturePolicy: TapHandler.ReleaseWithinBounds
                onTapped: chip.closed()
            }
        }
    }

    FocusRing {
        active: chip.activeFocus
        radius: Theme.radiusPill
    }

    HoverHandler {
        id: hover
        enabled: chip.enabled
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        id: tap
        enabled: chip.enabled
        // Grab on press so a click never falls through to items beneath (see
        // StrmIconButton for the full rationale).
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: {
            chip.forceActiveFocus(Qt.MouseFocusReason);
            chip.toggled();
        }
    }

    Keys.onReturnPressed: event => {
        if (!event.isAutoRepeat)
            chip.activate();
    }
    Keys.onEnterPressed: event => {
        if (!event.isAutoRepeat)
            chip.activate();
    }
    Keys.onSpacePressed: event => {
        if (!event.isAutoRepeat)
            chip.activate();
    }
}
