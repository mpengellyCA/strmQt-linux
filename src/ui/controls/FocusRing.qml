import QtQuick
import StrmQt

// The amber keyboard/gamepad focus ring (ARCHITECTURE.md).
//
// Purely visual: it declares no input handling at all, so dropping one into a
// control never changes that control's hit area or its focus chain.
//
// **Never a direct child of a Row, Column, Grid or Flow.** It anchors-fills its
// parent, and a positioner refuses fill anchors on its children: it warns "Row
// will not function" once, resets the anchor, and the ring stays 0×0 for the
// rest of its life — a control that can be focused and operated with nothing on
// screen to say so. Give the group a wrapper Item with real geometry outside the
// positioner and put the ring in that; StrmTabBar and LibraryPage's view-mode
// switch both show the shape.
//
// `active` must be driven by `activeFocus` and NEVER by hover. Hover and focus
// are separate states that can both be true at once; when they are, the ring is
// what wins visually. Anything that binds this to a HoverHandler is the bug this
// component exists to prevent.
Item {
    id: ring

    // Draw the ring. Bind to the owner's `activeFocus`.
    property bool active: false
    // Corner radius, matched to whatever the ring is framing.
    property int radius: Theme.radiusCardValue
    // Positive insets the ring inside the owner's bounds, negative outsets it.
    property int inset: 0

    anchors.fill: parent
    // Above the control's own background/content, below any popup.
    z: 2

    Rectangle {
        id: stroke

        anchors.fill: parent
        anchors.margins: ring.inset
        radius: ring.radius
        color: "transparent"
        border.color: Theme.accentColor
        border.width: Theme.focusRingWidth
        // Focus is a deliberate act and can afford to glide (ARCHITECTURE.md).
        opacity: ring.active ? 1.0 : 0.0
        visible: stroke.opacity > 0

        Behavior on opacity {
            NumberAnimation {
                duration: Theme.animFastMs
                easing.type: Theme.easeStandard
            }
        }
    }
}
