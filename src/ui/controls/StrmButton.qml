import QtQuick
import StrmQt

// The one button in the app (ARCHITECTURE.md). Pointer- and focus-driven
// from the first line: hover and focus are separate states, both can be true at
// once, and hover never steals keyboard focus.
//
//   variant "primary"    accent fill, accentText label
//   variant "secondary"  surface fill with a hairline border (default)
//   variant "ghost"      transparent until hovered or focused
//
// `destructive` swaps the accent for Theme.negative in every variant.
Item {
    id: button

    property string text: ""
    property string iconName: ""
    property string variant: "secondary"
    property bool busy: false
    property bool destructive: false
    property string accessibleName: button.text
    property string accessibleDescription: ""

    signal clicked

    // Pointer hover. Deliberately distinct from activeFocus.
    readonly property bool hovered: hover.hovered
    readonly property bool pressed: tap.pressed
    // Busy is "working", not "broken": it blocks input without greying out.
    readonly property bool interactive: button.enabled && !button.busy

    readonly property int hPad: Theme.spacingValue

    readonly property color accentTone: button.destructive ? Theme.negative : Theme.accentColor

    readonly property color fillColor: {
        if (button.variant === "primary")
            return button.enabled ? button.accentTone : Theme.accentMuted;
        if (button.variant === "ghost")
            return "transparent";
        return Theme.surfaceColor;
    }

    readonly property color labelColor: {
        if (!button.enabled)
            return Theme.textDisabled;
        if (button.variant === "primary")
            return Theme.accentText;
        if (button.destructive)
            return Theme.negative;
        // A ghost button reads as chrome until you point at it.
        if (button.variant === "ghost" && !button.hovered && !button.activeFocus)
            return Theme.textSecondaryColor;
        return Theme.textPrimaryColor;
    }

    implicitHeight: Theme.controlHeight
    implicitWidth: row.implicitWidth + 2 * button.hPad
    activeFocusOnTab: button.interactive

    Accessible.role: Accessible.Button
    Accessible.name: button.accessibleName
    Accessible.description: button.busy ? qsTr("Working") : button.accessibleDescription
    Accessible.focusable: button.interactive
    Accessible.focused: button.activeFocus
    Accessible.pressed: button.pressed
    Accessible.onPressAction: button.activate()

    // Press < hover < focus. When hover and focus are both true the larger
    // (focus) scale applies, and the ring below draws on top of it.
    scale: !button.interactive ? 1.0
         : button.pressed ? Theme.pressScale
         : button.activeFocus ? Theme.focusScale
         : button.hovered ? Theme.hoverScale
         : 1.0

    Behavior on scale {
        NumberAnimation {
            // Hover tracks the cursor (instant); focus glides (fast).
            duration: button.activeFocus ? Theme.animFastMs : Theme.animInstant
            easing.type: button.activeFocus ? Theme.easeStandard : Theme.easeInstant
        }
    }

    function activate(): void {
        if (button.interactive)
            button.clicked();
    }

    Rectangle {
        id: bg

        anchors.fill: parent
        radius: Theme.radiusChip
        color: button.fillColor
        border.width: button.variant === "secondary" ? 1 : 0
        border.color: button.destructive ? Theme.negative : Theme.hairline

        Behavior on color {
            ColorAnimation {
                duration: Theme.animInstant
                easing.type: Theme.easeInstant
            }
        }
    }

    // Hover/press wash, layered over whatever fill the variant chose so all
    // three variants brighten identically.
    Rectangle {
        anchors.fill: bg
        radius: bg.radius
        color: !button.interactive ? "transparent"
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

    Row {
        id: row

        anchors.centerIn: parent
        spacing: Theme.spacingTight
        opacity: button.busy ? 0.0 : 1.0

        Behavior on opacity {
            NumberAnimation {
                duration: Theme.animInstant
                easing.type: Theme.easeInstant
            }
        }

        StrmIcon {
            anchors.verticalCenter: parent.verticalCenter
            visible: button.iconName.length > 0
            name: button.iconName
            color: button.labelColor
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: button.text.length > 0
            text: button.text
            color: button.labelColor
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontBodySize

            Behavior on color {
                ColorAnimation {
                    duration: Theme.animInstant
                    easing.type: Theme.easeInstant
                }
            }
        }
    }

    StrmIcon {
        anchors.centerIn: parent
        visible: button.busy
        name: "refresh"
        color: button.labelColor

        RotationAnimation on rotation {
            running: button.busy && !Theme.reducedMotion
            from: 0
            to: 360
            duration: Theme.animAmbient
            loops: Animation.Infinite
        }
    }

    FocusRing {
        active: button.activeFocus
        radius: Theme.radiusChip
    }

    HoverHandler {
        id: hover
        enabled: button.interactive
        cursorShape: Qt.PointingHandCursor
        // No forceActiveFocus() here, ever: the pointer must not move the
        // keyboard's idea of where it is.
    }

    TapHandler {
        id: tap
        enabled: button.interactive
        onTapped: {
            // Clicking *is* a deliberate act, so it may take focus.
            button.forceActiveFocus(Qt.MouseFocusReason);
            button.clicked();
        }
    }

    // Every activation path emits the same signal. isAutoRepeat guards a held
    // Return from machine-gunning the action.
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
