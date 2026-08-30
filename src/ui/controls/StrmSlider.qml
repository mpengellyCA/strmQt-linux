// Bound: the marker Repeater's delegate reaches out to this file's ids, which
// is only well-defined (and only lint-clean) with bound component behaviour.
pragma ComponentBehavior: Bound

import QtQuick
import StrmQt

// The scrubber and the volume control (ARCHITECTURE.md).
//
// Two signals, because a seek is two different events: `moved` fires
// continuously while the user is dragging (update the time readout, move the
// preview) and `committed` fires once, on release (issue the actual seek). A
// player that seeks on every `moved` will thrash the demuxer.
//
// `value` is controlled by the owner. While the user is interacting, the
// control shows its own pending value so the knob never lags behind the
// pointer; it yields back to `value` the moment the owner reports a new one.
Item {
    id: slider

    property real from: 0
    property real to: 1
    property real value: 0
    // A second, dimmer fill behind the main one: how much is downloaded.
    property real buffered: 0
    // Values in [from, to] drawn as ticks over the track — chapter starts.
    property var markers: []
    property bool showKnobOnHoverOnly: true
    // Rendered above the track, centred on the cursor while hovering. Declare
    // it inline so it can reference this slider's id (`hoverValue`).
    property Component previewComponent: null
    // Keyboard/wheel increment.
    property real stepSize: (slider.to - slider.from) / 20
    property string accessibleName: qsTr("Slider")
    property string accessibleDescription: ""
    // QAccessibleQuickItem discovers these conventional names to provide its
    // value interface for a custom Item-backed slider.
    readonly property real minimumValue: slider.from
    readonly property real maximumValue: slider.to

    // Opt-in for scrubbers over playing media: focus alone must not let the
    // arrows move the value, or navigating onto the bar seeks by accident.
    // Return/Enter/Space (a gamepad's A lands here as Space) arms the control
    // — the "click on the bar" a key or pad user gives — and the same key,
    // Esc, or losing focus disarms it. Pointer scrubs are unaffected: a press
    // on the track is already that click.
    property bool armToScrub: false
    property bool armed: false

    signal moved(real value)
    signal committed(real value)

    readonly property bool hovering: pointer.containsMouse
    readonly property bool dragging: pointer.pressed
    // Value under the pointer — what a chapter-thumbnail preview wants.
    readonly property real hoverValue: slider.valueAt(slider.pointerX)
    // What a preview tooltip should show: the value under the pointer for a
    // hover or drag, but the control's own value when armed from the keyboard
    // — then the pointer is somewhere irrelevant and the knob is the cursor.
    readonly property real previewValue: (slider.armed && !slider.hovering && !slider.dragging)
                                         ? slider.shownValue : slider.hoverValue

    // ── internals ──────────────────────────────────────────────────────────
    // Bound rather than assigned from the handlers: MouseArea keeps mouseX
    // current for hover motion as well as drags, so the chapter preview tracks
    // the cursor without a press.
    readonly property real pointerX: pointer.mouseX
    property real pendingValue: 0
    // True while this control's own value is newer than the owner's.
    property bool pending: false

    readonly property real span: Math.max(1e-9, slider.to - slider.from)
    readonly property real shownValue: slider.pending ? slider.pendingValue : slider.value
    readonly property real fillPos: slider.normalised(slider.shownValue)
    readonly property real bufferedPos: slider.normalised(slider.buffered)
    readonly property int trackHeight: (slider.hovering || slider.dragging || slider.activeFocus)
                                       ? Theme.scale(6) : Theme.scale(4)

    function normalised(v: real): real {
        return Math.max(0, Math.min(1, (v - slider.from) / slider.span));
    }

    function valueAt(px: real): real {
        const t = slider.width <= 0 ? 0 : Math.max(0, Math.min(1, px / slider.width));
        return slider.from + t * slider.span;
    }

    function emitMoved(v: real): void {
        slider.pendingValue = Math.max(slider.from, Math.min(slider.to, v));
        slider.pending = true;
        slider.moved(slider.pendingValue);
    }

    function emitCommitted(): void {
        slider.committed(slider.shownValue);
    }

    function nudge(direction: int): void {
        slider.emitMoved(slider.shownValue + direction * slider.stepSize);
    }

    // The owner answered: stop showing our own guess. Not while dragging,
    // where an echoed value would fight the pointer.
    onValueChanged: {
        if (!slider.dragging)
            slider.pending = false;
    }

    implicitWidth: Theme.scale(200)
    implicitHeight: Theme.touchTarget
    activeFocusOnTab: slider.enabled

    Accessible.role: Accessible.Slider
    Accessible.name: slider.accessibleName
    Accessible.description: slider.accessibleDescription
    Accessible.focusable: slider.enabled
    Accessible.focused: slider.activeFocus
    Accessible.onIncreaseAction: slider.accessibleNudge(1)
    Accessible.onDecreaseAction: slider.accessibleNudge(-1)

    function accessibleNudge(direction: int): void {
        if (!slider.enabled)
            return;
        slider.nudge(direction);
        slider.emitCommitted();
    }

    Rectangle {
        id: track

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        height: slider.trackHeight
        radius: height / 2
        color: Theme.surfaceRaisedColor

        Behavior on height {
            NumberAnimation {
                duration: Theme.animInstant
                easing.type: Theme.easeInstant
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: parent.width * slider.bufferedPos
            radius: parent.radius
            color: Theme.accentMuted
            visible: slider.buffered > slider.from
        }

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: parent.width * slider.fillPos
            radius: parent.radius
            color: slider.enabled ? Theme.accentColor : Theme.accentMuted
        }

        // Chapter ticks, notched out of the track in the page colour so they
        // read against both the filled and unfilled halves.
        Repeater {
            model: slider.markers

            delegate: Rectangle {
                required property var modelData

                x: track.width * slider.normalised(modelData) - width / 2
                y: 0
                width: Theme.scale(2)
                height: track.height
                color: Theme.ground
                opacity: 0.9
                visible: modelData > slider.from && modelData < slider.to
            }
        }
    }

    Rectangle {
        id: knob

        x: track.width * slider.fillPos - width / 2
        anchors.verticalCenter: track.verticalCenter
        width: Theme.scale(14)
        height: width
        radius: width / 2
        color: Theme.accentColor
        border.width: 2
        // Armed reads as an accent ring around the knob: the visible half of
        // "the arrows now own this bar".
        border.color: slider.armed ? Theme.accentColor : Theme.ground
        visible: slider.enabled && knob.scale > 0.01
        // Grows into the pointer; absent entirely at rest when the owner asked
        // for a clean, unadorned progress line.
        scale: (slider.dragging || slider.hovering || slider.activeFocus) ? 1.0
             : (slider.showKnobOnHoverOnly ? 0.0 : 0.72)

        Behavior on scale {
            NumberAnimation {
                duration: Theme.animInstant
                easing.type: Theme.easeInstant
            }
        }
    }

    Loader {
        id: preview

        active: slider.previewComponent !== null
                && (slider.hovering || slider.dragging || slider.armed)
        sourceComponent: slider.previewComponent
        // Armed without a pointer on the bar: anchor to the knob, not to a
        // stale mouse position.
        x: {
            const centre = (slider.armed && !slider.hovering && !slider.dragging)
                         ? track.width * slider.fillPos : slider.pointerX;
            return Math.max(0, Math.min(slider.width - preview.width,
                                        centre - preview.width / 2));
        }
        y: -preview.height - Theme.spacingTight
    }

    FocusRing {
        active: slider.activeFocus
        radius: Theme.radiusChip
    }

    // One MouseArea rather than TapHandler + DragHandler. A scrubber needs a
    // press → move* → release state machine with exactly one commit at the end,
    // and the two-handler arrangement does not give that: the drag handler
    // re-activates per motion event, so a single scrub fires one `committed`
    // per pixel of travel — every one of them a seek. `preventStealing` is the
    // other half of the reason: it keeps a parent Flickable from taking the
    // press mid-scrub.
    MouseArea {
        id: pointer

        anchors.fill: parent
        enabled: slider.enabled
        hoverEnabled: true
        preventStealing: true
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.PointingHandCursor

        // Seek starts on press, not on release: a scrubber that waits for the
        // button to come up feels broken.
        onPressed: mouse => {
            slider.forceActiveFocus(Qt.MouseFocusReason);
            slider.emitMoved(slider.valueAt(mouse.x));
        }

        onPositionChanged: mouse => {
            if (pointer.pressed)
                slider.emitMoved(slider.valueAt(mouse.x));
        }

        // The one place a seek is actually issued.
        onReleased: slider.emitCommitted()

        onCanceled: slider.pending = false
    }

    WheelHandler {
        enabled: slider.enabled
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: event => {
            const delta = event.angleDelta.y !== 0 ? event.angleDelta.y : event.angleDelta.x;
            if (delta === 0)
                return;
            slider.nudge(delta > 0 ? 1 : -1);
            slider.emitCommitted();
        }
    }

    // Auto-repeat is wanted here (holding Left scrubs), so the isAutoRepeat
    // guard goes on the *release*: one committed seek per key press, however
    // many steps it travelled.
    Keys.onLeftPressed: event => {
        if (slider.armToScrub && !slider.armed) {
            // Swallowed, not propagated: the page binds plain Left to a 10 s
            // seek, and a bar the user has only focused must not let the key
            // fall through to it. Arming first is the whole point.
            event.accepted = true;
            return;
        }
        slider.nudge(-1);
        event.accepted = true;
    }
    Keys.onRightPressed: event => {
        if (slider.armToScrub && !slider.armed) {
            event.accepted = true;
            return;
        }
        slider.nudge(1);
        event.accepted = true;
    }
    Keys.onReleased: event => {
        if ((event.key === Qt.Key_Left || event.key === Qt.Key_Right) && !event.isAutoRepeat) {
            if (!slider.armToScrub || slider.armed)
                slider.emitCommitted();
            event.accepted = true;
        }
    }

    // The arm/disarm keys. Each declines the event when arming is off so the
    // page's own bindings (pause on Space, back on Esc) keep working for the
    // volume sliders and any other ungated use.
    Keys.onReturnPressed: event => {
        if (!slider.armToScrub)
            return;
        slider.armed = !slider.armed;
        event.accepted = true;
    }
    Keys.onEnterPressed: event => {
        if (!slider.armToScrub)
            return;
        slider.armed = !slider.armed;
        event.accepted = true;
    }
    Keys.onSpacePressed: event => {
        if (!slider.armToScrub)
            return;
        slider.armed = !slider.armed;
        event.accepted = true;
    }
    Keys.onEscapePressed: event => {
        if (slider.armed) {
            slider.armed = false;
            event.accepted = true;
        }
    }

    // Focus leaving is a disarm: an armed bar nobody can see is a seek trap.
    onActiveFocusChanged: {
        if (!slider.activeFocus)
            slider.armed = false;
    }
}
