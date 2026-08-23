import QtQuick
import QtQuick.Controls.Basic
import StrmQt

// Hover tooltip for icon-only controls (ARCHITECTURE.md). Carries the keyboard
// shortcut as well as the label, which is how an icon-only UI stays teachable
// instead of merely compact.
//
// It is a Popup, so it renders in the window overlay and is never clipped by a
// rail, a grid delegate, or a Flickable.
//
// Usage: give it a `target`, then call `requestShow()` when the pointer enters
// that target and `requestHide()` when it leaves. The delay lives here, so no
// caller has to own a Timer.
Popup {
    id: tip

    property string text: ""
    property string shortcut: ""
    property Item target: null
    // Hover dwell before showing, ms.
    property int delay: 500
    // Distance between the target's edge and the panel.
    property int gap: Theme.spacingTight

    // Set at open time rather than bound: mapToItem() is not a bindable
    // expression, so re-evaluating it on every geometry change would be a lie.
    property bool below: false

    parent: tip.target

    x: tip.target ? Math.round((tip.target.width - tip.width) / 2) : 0
    y: tip.below ? (tip.target ? tip.target.height + tip.gap : 0)
                 : -tip.height - tip.gap

    topPadding: Theme.scale(6)
    bottomPadding: Theme.scale(6)
    leftPadding: Theme.spacingTight
    rightPadding: Theme.spacingTight

    // Sized off the content explicitly rather than through Popup's implicit
    // content sizing, which settles a frame late and clipped the trailing
    // shortcut glyph.
    implicitWidth: row.implicitWidth + tip.leftPadding + tip.rightPadding
    implicitHeight: row.implicitHeight + tip.topPadding + tip.bottomPadding

    // A tooltip is decoration, never a focus trap or a click target.
    modal: false
    dim: false
    focus: false
    closePolicy: Popup.NoAutoClose

    // Start the dwell timer. Safe to call repeatedly.
    function requestShow(): void {
        if (!tip.target || tip.text.length === 0)
            return;
        dwell.restart();
    }

    // Cancel a pending show and dismiss an open one.
    function requestHide(): void {
        dwell.stop();
        tip.close();
    }

    function showNow(): void {
        if (!tip.target || tip.text.length === 0)
            return;
        // Flip below the target when there is no room above it in the scene.
        const scenePos = tip.target.mapToItem(null, 0, 0);
        tip.below = scenePos.y < (tip.implicitHeight + tip.gap);
        tip.open();
    }

    Timer {
        id: dwell
        interval: tip.delay
        repeat: false
        onTriggered: tip.showNow()
    }

    background: Rectangle {
        color: Theme.surfaceOverlay
        radius: Theme.radiusChip
        border.width: 1
        border.color: Theme.hairline
    }

    contentItem: Row {
        id: row

        spacing: Theme.spacingTight

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: tip.text
            color: Theme.textPrimaryColor
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontSmall
        }

        // Shortcuts are gear labels, not prose: mono, quiet, tabular.
        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: tip.shortcut.length > 0
            text: tip.shortcut
            color: Theme.textTertiary
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontCaption
        }
    }

    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0.0
            to: 1.0
            duration: Theme.animFastMs
            easing.type: Theme.easeStandard
        }
    }

    exit: Transition {
        NumberAnimation {
            property: "opacity"
            from: 1.0
            to: 0.0
            duration: Theme.animInstant
            easing.type: Theme.easeInstant
        }
    }
}
