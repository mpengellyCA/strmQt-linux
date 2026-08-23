import QtQuick
import StrmQt

// StrmToastHost — the window-level notification surface (ARCHITECTURE.md).
//
// Placed ONCE, near the top of the window's visual stack:
//
//   StrmToastHost { id: toasts; anchors.fill: parent; z: 1000 }
//   ...
//   toasts.show(qsTr("Marked as watched"), "success")
//
// Behaviour:
//   · queued, one message visible at a time — a stack of five toasts is noise,
//     and the failure mode we actually have is a burst of identical errors;
//   · anchored bottom-right, entering with a short rise + fade;
//   · auto-dismisses after ~4 s, ~7 s for errors, because an error the user
//     missed is an error they will hit again;
//   · the dismiss timer pauses while the pointer is over the toast, so reading
//     a long message or reaching for its action never races the clock.
//
// The host itself is input-transparent except where the toast actually is: it
// fills the window so it can position against the corner, but it must never
// intercept clicks meant for the page underneath.
Item {
    id: host

    // Queue of { text, severity, actionText } records.
    property var queue: []
    property var current: null

    readonly property bool showing: current !== null

    // Emitted when the visible toast's action is used; the payload is the
    // record that was passed to show(), so a caller can tell which one fired.
    signal actionTriggered(var record)

    function show(text, severity, actionText) {
        const record = {
            "text": text !== undefined ? String(text) : "",
            "severity": severity !== undefined && severity !== null ? String(severity) : "info",
            "actionText": actionText !== undefined && actionText !== null ? String(actionText) : ""
        }
        // Collapse an immediate repeat rather than queueing it twice: a retry
        // loop emitting the same failure should read as one message.
        if (host.current !== null && host.current.text === record.text
                && host.current.severity === record.severity) {
            dismissTimer.restart()
            return
        }
        const next = host.queue.slice()
        next.push(record)
        host.queue = next
        if (host.current === null)
            host._advance()
    }

    function dismiss() {
        dismissTimer.stop()
        host.current = null
        host._advance()
    }

    function clear() {
        dismissTimer.stop()
        host.queue = []
        host.current = null
    }

    function _advance() {
        if (host.current !== null || host.queue.length === 0)
            return
        const next = host.queue.slice()
        host.current = next.shift()
        host.queue = next
        dismissTimer.interval = host.current.severity === "error"
                                ? 7000 : 4000
        dismissTimer.restart()
    }

    Timer {
        id: dismissTimer
        repeat: false
        // Reading time is not running time: the countdown stops while the
        // pointer rests on the toast, and starts over when it leaves — giving
        // the full interval back is friendlier than resuming a nearly-expired
        // one under the cursor the user just moved away.
        running: false
        onTriggered: host.dismiss()
    }

    // Re-arm the countdown when the pointer leaves the toast.
    Connections {
        target: toast
        function onHoveredChanged() {
            if (host.current === null)
                return
            if (toast.hovered)
                dismissTimer.stop()
            else
                dismissTimer.restart()
        }
    }

    StrmToast {
        id: toast

        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: Theme.pageMarginValue
        anchors.bottomMargin: Theme.pageMarginValue

        text: host.current !== null ? host.current.text : ""
        severity: host.current !== null ? host.current.severity : "info"
        actionText: host.current !== null ? host.current.actionText : ""

        opacity: host.showing ? 1 : 0
        visible: opacity > 0.01
        enabled: host.showing
        transform: Translate {
            y: host.showing ? 0 : Theme.spacingLoose

            Behavior on y {
                NumberAnimation { duration: Theme.animNormalMs; easing.type: Theme.easeEmphasis }
            }
        }

        Behavior on opacity {
            NumberAnimation { duration: Theme.animFastMs; easing.type: Theme.easeStandard }
        }

        onActionTriggered: {
            const record = host.current
            host.dismiss()
            if (record !== null)
                host.actionTriggered(record)
        }
        onDismissRequested: host.dismiss()
    }
}
