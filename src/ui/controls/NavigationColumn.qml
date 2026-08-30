pragma Singleton
import QtQuick

// NavigationColumn — the screen column the user's eye is in (ARCHITECTURE.md §4).
//
// A page of horizontal shelves has one cursor per shelf, and each shelf
// remembers where it was left. Walking down such a page therefore moved the
// selection sideways on every step: Down out of card 3 of Continue Watching
// landed on whatever card Next Up was last parked on — usually its far end,
// because that is where a previous sweep had stopped. The eye follows the ring,
// so the page read as jumping about rather than as a list being walked.
//
// The fix is the one every ten-foot interface uses: a vertical move keeps the
// COLUMN. The shelf being left publishes the x of the card under the ring, and
// the shelf being entered puts its cursor on whichever of its own cards sits
// nearest that x. Each shelf still keeps its own scroll offset — nothing
// scrolls sideways to satisfy this — so the move is purely "the card below the
// one I was on".
//
// ── Why a singleton, and why it disarms itself ─────────────────────────────
// The two shelves involved are siblings that do not know about each other, and
// on three different pages they are held by three different parents (a vertical
// ListView on Home, a KeyNavigation chain on Details and Search). A value
// passed between them through their common ancestor would have to be plumbed
// through every one of those; this is the same value, stated once.
//
// It is armed ONLY by an Up/Down keypress and disarms itself on the next turn
// of the event loop, which is the whole of its safety. Focus moves for any
// other reason — a click, Tab, the back-stack restoring an item by identity —
// happen with nothing armed and are left exactly as they were. The generation
// counter is what makes a second arm cancel the first one's pending disarm.
QtObject {
    id: column

    // Scene x of the card the vertical move started from, or -1.
    property real sceneX: -1
    // True only between an Up/Down keypress and the end of that event turn.
    property bool armed: false

    property int _generation: 0

    function arm(x): void {
        if (typeof x !== "number" || !isFinite(x) || x < 0) {
            column.disarm();
            return;
        }
        column.sceneX = x;
        column.armed = true;
        const generation = ++column._generation;
        // The focus move a key triggers is synchronous, so the shelf being
        // entered reads this before the loop turns. Anything later is not that
        // move and must not see it.
        Qt.callLater(function () {
            if (generation === column._generation)
                column.armed = false;
        });
    }

    function disarm(): void {
        ++column._generation;
        column.armed = false;
    }

    // Arm from the current item of a horizontal view. A view with no current
    // item (an empty shelf) disarms instead of publishing a stale column.
    function noteFrom(view): void {
        if (!view || view.currentItem === null || view.currentItem === undefined) {
            column.disarm();
            return;
        }
        const item = view.currentItem;
        const point = item.mapToItem(null, item.width / 2, 0);
        column.arm(point.x);
    }

    // Only for a key that is a vertical move; everything else disarms, so a
    // Left/Right sweep cannot leave a column armed for the next unrelated
    // focus change.
    function noteFromKey(view, key): void {
        if (key === Qt.Key_Up || key === Qt.Key_Down)
            column.noteFrom(view);
    }

    // Put a horizontal view's cursor on the item nearest the armed column.
    // Returns false — and changes nothing — when nothing is armed, which is
    // every focus move that is not a vertical step.
    function applyTo(view): bool {
        if (!column.armed || !view || view.count === undefined || view.count <= 0)
            return false;
        const index = column.indexNear(view, column.sceneX);
        column.disarm();
        if (index < 0 || index === view.currentIndex)
            return false;
        view.currentIndex = index;
        // Contain, never Center: the shelf keeps the scroll offset it had. This
        // only matters for a shelf short enough that the column falls past its
        // end, where the clamp below lands on an item that may be off-screen.
        view.positionViewAtIndex(index, ListView.Contain);
        return true;
    }

    // indexAt first — it is exact, and the chip strips have variable-width
    // cells that no arithmetic can place. It returns -1 for a point in the
    // spacing between two cells or past the last one, and the uniform-step
    // estimate below covers both; a column past the end clamps to the last
    // item, which is the honest nearest answer rather than a wrap.
    function indexNear(view, sceneX): int {
        const local = view.mapFromItem(null, sceneX, 0).x;
        const contentPos = local + view.contentX;
        let index = view.indexAt(contentPos, view.contentY + view.height / 2);
        if (index < 0) {
            const span = view.contentWidth - view.leftMargin - view.rightMargin;
            const step = (span + view.spacing) / view.count;
            index = step > 0 ? Math.floor((contentPos - view.originX) / step) : 0;
        }
        return Math.max(0, Math.min(view.count - 1, index));
    }
}
