import QtQuick
import StrmQt

// Square (or round) icon-only button: OSD transport, top-bar affordances, card
// overlay actions. Icon-only chrome is only honest if it teaches itself, so
// every instance with a `tooltip` grows one after a hover dwell, and the
// tooltip carries the keyboard shortcut too (ARCHITECTURE.md).
//
// `checked` is a display state, not a toggle: this control emits clicked() and
// lets the owner decide, so a binding to model data is never fought with.
Item {
    id: button

    property string iconName: ""
    property string tooltip: ""
    property string shortcut: ""
    // The tooltip is the normal accessible name. Call sites with a deliberately
    // different spoken label can override this without changing pointer copy.
    property string accessibleName: button.tooltip
    property string accessibleDescription: button.shortcut.length > 0
                                                   ? qsTr("Shortcut: %1").arg(button.shortcut)
                                                   : ""
    property bool checked: false
    property bool round: false
    // Additive to the shared contract: OSD and inline uses need a smaller box
    // than Theme.controlHeight without wrapping this control in another Item.
    property int size: Theme.controlHeight

    signal clicked

    readonly property bool hovered: hover.hovered
    readonly property bool pressed: tap.pressed

    readonly property color glyphColor: {
        if (!button.enabled)
            return Theme.textDisabled;
        if (button.checked)
            return Theme.accentColor;
        if (button.hovered || button.activeFocus)
            return Theme.textPrimaryColor;
        return Theme.textSecondaryColor;
    }

    implicitWidth: button.size
    implicitHeight: button.size
    activeFocusOnTab: button.enabled

    Accessible.role: Accessible.Button
    Accessible.name: button.accessibleName
    Accessible.description: button.accessibleDescription
    Accessible.focusable: button.enabled
    Accessible.focused: button.activeFocus
    Accessible.pressed: button.checked || button.pressed
    Accessible.onPressAction: button.activate()

    Component.onCompleted: {
        if (button.accessibleName.trim().length === 0)
            console.warn("StrmIconButton requires a tooltip or accessibleName", button.iconName);
    }

    scale: !button.enabled ? 1.0
         : button.pressed ? Theme.pressScale
         : button.activeFocus ? Theme.focusScale
         : button.hovered ? Theme.hoverScale
         : 1.0

    Behavior on scale {
        NumberAnimation {
            duration: button.activeFocus ? Theme.animFastMs : Theme.animInstant
            easing.type: button.activeFocus ? Theme.easeStandard : Theme.easeInstant
        }
    }

    function activate(): void {
        if (button.enabled)
            button.clicked();
    }

    Rectangle {
        id: bg

        anchors.fill: parent
        radius: button.round ? width / 2 : Theme.radiusChip
        color: button.checked && button.enabled ? Theme.accentMuted : "transparent"

        Behavior on color {
            ColorAnimation {
                duration: Theme.animInstant
                easing.type: Theme.easeInstant
            }
        }
    }

    Rectangle {
        anchors.fill: bg
        radius: bg.radius
        color: !button.enabled ? "transparent"
             : button.pressed ? Theme.pressTint
             : button.hovered ? Theme.hoverTint
             : "transparent"

        Behavior on color {
            ColorAnimation {
                duration: Theme.animInstant
                easing.type: Theme.easeInstant
            }
        }
    }

    StrmIcon {
        anchors.centerIn: parent
        name: button.iconName
        color: button.glyphColor
        size: Math.round(button.size * 0.5)
    }

    FocusRing {
        active: button.activeFocus
        radius: bg.radius
    }

    StrmTooltip {
        id: tip
        target: button
        text: button.tooltip
        shortcut: button.shortcut
    }

    HoverHandler {
        id: hover
        enabled: button.enabled
        cursorShape: Qt.PointingHandCursor
        onHoveredChanged: {
            if (hover.hovered)
                tip.requestShow();
            else
                tip.requestHide();
        }
    }

    TapHandler {
        id: tap
        enabled: button.enabled
        // Grab on press rather than the DragThreshold default: the default
        // holds only a passive grab until the drag threshold is crossed, so
        // the press keeps falling through to whatever is beneath — a rail
        // card under the hover chevrons, the card itself under the ✓/♥/⋯
        // overlay — and on release BOTH the button and the item below fire.
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: {
            tip.requestHide();
            button.forceActiveFocus(Qt.MouseFocusReason);
            button.clicked();
        }
    }

    Keys.onReturnPressed: event => {
        if (!event.isAutoRepeat)
            button.activate();
    }
    Keys.onEnterPressed: event => {
        if (!event.isAutoRepeat)
            button.activate();
    }
    Keys.onSpacePressed: event => {
        if (!event.isAutoRepeat)
            button.activate();
    }
}
