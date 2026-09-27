import QtQuick
import StrmQt

// A clip rectangle with room for the focus ring past its edges.
//
// A view that sets `clip: true` clips at its own bounds, and FocusRing draws
// OUTSIDE the item it frames (a card's ring sits Theme.focusRingWidth beyond
// the art, and a focused card is raised by Theme.focusScale on top of that).
// So whatever sits flush against the view's edge — the top row of a grid, the
// first card of a gutterless shelf, the first control in a scrolling form —
// had its ring sliced off exactly where the user starts.
//
// Moving the content away from the edge is not the fix: it shifts the layout
// off the page margin, and Qt's positionViewAtIndex() ignores a view's
// top/leftMargin anyway, so every "put the cursor back" path would land the
// first row flush against the edge again. Instead the view stops clipping and
// sits inside this item, which keeps the view's geometry and clips the given
// number of pixels further out on each side. Layout and scrolling are exactly
// what they were; the only visible difference is that scrolled content runs
// that far past the edge before it is cut, which is the room the ring needed.
//
// Usage: give this item the view's geometry (its anchors, x/y, width/height,
// visible), and fill it with the view — either nested, or, to leave a long
// view where it is declared, by handing the view over through `parent`:
//
//     FocusClip { id: viewClip; anchors.fill: parent }
//     GridView {
//         id: view
//         parent: viewClip.contentItem
//         anchors.fill: parent        // no clip: true
//     }
//
// Siblings that anchored to the view anchor to this item instead: it has the
// view's geometry, and the view is no longer their sibling.
//
// Along the axis a view scrolls, keep the outset small (Theme.focusRingOutset,
// or Theme.focusHeadroom() of a raised card less the slack its cell already
// has): that is how far content bleeds. Across it, nothing scrolls into the
// extra room, so it can be as generous as the surroundings allow.
Item {
    id: frame

    property int leftOutset: Theme.focusRingOutset
    property int rightOutset: Theme.focusRingOutset
    property int topOutset: Theme.focusRingOutset
    property int bottomOutset: Theme.focusRingOutset

    // Where the clipped view lives: the same geometry as this item.
    readonly property alias contentItem: inner
    default property alias content: inner.data

    Item {
        id: clipper

        anchors.fill: parent
        anchors.leftMargin: -frame.leftOutset
        anchors.rightMargin: -frame.rightOutset
        anchors.topMargin: -frame.topOutset
        anchors.bottomMargin: -frame.bottomOutset
        clip: true

        Item {
            id: inner

            anchors.fill: parent
            anchors.leftMargin: frame.leftOutset
            anchors.rightMargin: frame.rightOutset
            anchors.topMargin: frame.topOutset
            anchors.bottomMargin: frame.bottomOutset
        }
    }
}
