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

    signal moved(real value)
    signal committed(real value)

    readonly property bool hovering: pointer.containsMouse
    readonly property bool dragging: pointer.pressed
    // Value under the pointer — what a chapter-thumbnail preview wants.
    readonly property real hoverValue: slider.valueAt(slider.pointerX)

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
        border.color: Theme.ground
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

        active: slider.previewComponent !== null && (slider.hovering || slider.dragging)
        sourceComponent: slider.previewComponent
        x: Math.max(0, Math.min(slider.width - preview.width, slider.pointerX - preview.width / 2))
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
        slider.nudge(-1);
        event.accepted = true;
    }
    Keys.onRightPressed: event => {
        slider.nudge(1);
        event.accepted = true;
    }
    Keys.onReleased: event => {
        if ((event.key === Qt.Key_Left || event.key === Qt.Key_Right) && !event.isAutoRepeat) {
            slider.emitCommitted();
            event.accepted = true;
        }
    }
}
