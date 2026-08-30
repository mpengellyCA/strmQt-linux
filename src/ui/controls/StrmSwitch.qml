import QtQuick
import StrmQt

// Settings toggle. Like StrmChip, `checked` is controlled by the owner and this
// control only reports intent:
//
//   StrmSwitch { text: "Ken Burns"; checked: Settings.kenBurns
//                onToggled: Settings.kenBurns = !Settings.kenBurns }
Item {
    id: control

    property bool checked: false
    property string text: ""
    property string accessibleName: control.text
    property string accessibleDescription: ""

    signal toggled

    readonly property bool hovered: hover.hovered
    readonly property bool pressed: tap.pressed

    implicitHeight: Theme.controlHeight
    implicitWidth: track.width + (label.visible ? Theme.spacingTight + label.implicitWidth : 0)
    activeFocusOnTab: control.enabled

    Accessible.role: Accessible.CheckBox
    Accessible.name: control.accessibleName
    Accessible.description: control.accessibleDescription
    Accessible.checkable: true
    Accessible.checked: control.checked
    Accessible.focusable: control.enabled
    Accessible.focused: control.activeFocus
    Accessible.pressed: control.pressed
    Accessible.onPressAction: control.activate()
    Accessible.onToggleAction: control.activate()

    function activate(): void {
        if (control.enabled)
            control.toggled();
    }

    Rectangle {
        id: track

        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        width: Theme.scale(44)
        height: Theme.scale(24)
        radius: height / 2
        color: !control.enabled ? Theme.surfaceColor
             : control.checked ? Theme.accentColor
             : Theme.surfaceRaisedColor
        border.width: control.checked ? 0 : 1
        border.color: Theme.hairline

        Behavior on color {
            ColorAnimation {
                duration: Theme.animFastMs
                easing.type: Theme.easeStandard
            }
        }

        // Hover wash on the track only: the knob keeps its own colour so the
        // on/off read never gets muddier as the pointer moves over it.
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: !control.enabled ? "transparent"
                 : control.pressed ? Theme.pressTint
                 : control.hovered ? Theme.hoverTint
                 : "transparent"

            Behavior on color {
                ColorAnimation {
                    duration: Theme.animInstant
                    easing.type: Theme.easeInstant
                }
            }
        }

        Rectangle {
            id: knob

            readonly property int pad: Theme.scale(3)

            y: knob.pad
            x: control.checked ? track.width - width - knob.pad : knob.pad
            width: track.height - 2 * knob.pad
            height: width
            radius: width / 2
            color: !control.enabled ? Theme.textDisabled
                 : control.checked ? Theme.accentText
                 : Theme.textSecondaryColor

            Behavior on x {
                NumberAnimation {
                    duration: Theme.animFastMs
                    easing.type: Theme.easeStandard
                }
            }

            Behavior on color {
                ColorAnimation {
                    duration: Theme.animFastMs
                    easing.type: Theme.easeStandard
                }
            }
        }
    }

    Text {
        id: label

        anchors.verticalCenter: parent.verticalCenter
        anchors.left: track.right
        anchors.leftMargin: Theme.spacingTight
        visible: control.text.length > 0
        text: control.text
        color: !control.enabled ? Theme.textDisabled
             : (control.hovered || control.activeFocus) ? Theme.textPrimaryColor
             : Theme.textSecondaryColor
        font.family: Theme.fontBody
        font.pixelSize: Theme.fontBodySize

        Behavior on color {
            ColorAnimation {
                duration: Theme.animInstant
                easing.type: Theme.easeInstant
            }
        }
    }

    FocusRing {
        active: control.activeFocus
        radius: Theme.radiusPill
    }

    HoverHandler {
        id: hover
        enabled: control.enabled
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        id: tap
        enabled: control.enabled
        // Grab on press so a click never falls through to items beneath (see
        // StrmIconButton for the full rationale).
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: {
            control.forceActiveFocus(Qt.MouseFocusReason);
            control.toggled();
        }
    }

    Keys.onReturnPressed: event => {
        if (!event.isAutoRepeat)
            control.activate();
    }
    Keys.onEnterPressed: event => {
        if (!event.isAutoRepeat)
            control.activate();
    }
    Keys.onSpacePressed: event => {
        if (!event.isAutoRepeat)
            control.activate();
    }
}
