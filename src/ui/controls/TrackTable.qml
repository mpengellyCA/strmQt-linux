pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import StrmQt

// TrackTable — the list a set of TrackRows lives in (ARCHITECTURE.md).
//
// A ListView with three things added to it that every track surface in this app
// had been re-deriving on its own: the once-per-table decisions a row cannot
// make for itself, the keyboard behaviour a long list needs, and type-to-jump.
//
// ── One tab stop, not N (ARCHITECTURE.md §4) ───────────────────────────────
// This is a ListView, so it is a single item in the tab chain and it owns
// Up/Down itself. Page Up/Down, Home/End and type-to-jump are handled here too,
// and every verb TrackRow draws sets `activeFocusOnTab: false`. Tab leaves the
// table; it never walks it.
//
// ── Decisions made once, above the rows ────────────────────────────────────
// `showArtistColumn` and the disc banners are computed in one pass over the
// model whenever its count changes, never per delegate. Two reasons, and the
// second is the one that shows:
//
//  1. Recomputing per delegate re-marshals the whole model on every scrolled
//     pixel.
//  2. The artist column has to be present or absent for the *whole table* or
//     the columns stop lining up down the record. Printing "Opeth" on all six
//     tracks of an Opeth album is noise; printing it on a compilation is the
//     only way to read the record. So the question asked is "does any track
//     differ from the album's credit", asked once, and the answer sizes the
//     column for every row including the ones that agree.
//
// ── Extending the keys ─────────────────────────────────────────────────────
// A page that needs a key of its own connects `keyPressed` rather than
// declaring its own `Keys.onPressed`: an attached handler re-declared at the
// use site *replaces* this one, which would silently take Page Up, Home, End
// and type-to-jump away. Handlers for a specific key (`Keys.onDeletePressed`,
// `Keys.onUpPressed`) are separate signals and can still be declared outside.
//
// ── Where multi-select goes ────────────────────────────────────────────────
// Deferred until the batch verbs behind it exist (queue / add-to-playlist /
// favourite over a set), because a selection UI with nothing to do is dead
// code. The shape is here for it: `_moveCursor()` is the single funnel every
// cursor movement passes through, so Shift+Up/Down extends from an anchor
// there; `activateAt()` is the single activation path, so Ctrl+Click toggles
// there; and a row's selected state would be a `selected` property on TrackRow
// set from a `selectedRows` map on this component. Nothing else needs to move.
ListView {
    id: table

    // ── Table-level rules ──────────────────────────────────────────────────
    // Walk the model for disc boundaries. Off by default: only an album has
    // discs, and the pass is not free on a 500-row playlist.
    property bool discGrouping: false
    // Walk the model to decide whether the artist column is worth a column.
    property bool artistRule: false
    // The album's own credit, when the page already knows it. Empty falls back
    // to what the first track says, which is what an album page with no item
    // map has to do.
    property string albumArtist: ""
    property bool typeToJump: true
    // Which role type-to-jump matches against. "name" for a track table,
    // "label" where the model's display string is the composed one.
    property string jumpRole: "name"
    // Row height for the page-step calculation. The delegate's own height wins
    // for layout; this only has to be close enough to page by a screenful.
    property int rowHeight: Theme.scale(38)

    // ── Derived, once per model load ───────────────────────────────────────
    // Filled by the same pass the rules above need, so it is meaningful only
    // when the table walks its model at all — i.e. when one of them is on.
    property int totalRuntimeMs: 0
    property bool multiDisc: false
    property bool showArtistColumn: false
    // Row index → disc number, for the rows that begin a disc. Empty unless the
    // set actually has more than one.
    property var discStartAt: ({})
    property string derivedAlbumArtist: ""

    readonly property string albumArtistName: table.albumArtist.length > 0
                                              ? table.albumArtist : table.derivedAlbumArtist

    signal activated(int index)
    // Raised before this component's own key handling; accept the event to
    // claim the key.
    signal keyPressed(var event)

    // ── Reading a row ──────────────────────────────────────────────────────
    // MediaItemModel calls it get(); PlayQueue calls it itemAt(). Tolerant
    // rather than configurable, in the same spirit as the DTO mapper: a model
    // with neither simply has no table-level metadata and no type-to-jump.
    function rowAt(index) {
        const model = table.model
        if (!model || index < 0 || index >= table.count)
            return null
        if (typeof model.get === "function")
            return model.get(index)
        if (typeof model.itemAt === "function")
            return model.itemAt(index)
        return null
    }

    function artistLineOf(item): string {
        if (!item)
            return ""
        const names = item.artists
        if (names !== undefined && names !== null && names.length > 0)
            return Array.prototype.join.call(names, ", ")
        return (item.albumArtist !== undefined && item.albumArtist !== null)
                ? String(item.albumArtist) : ""
    }

    // The rule, applied to one row: the artist is shown only where the table
    // has decided there is a column at all AND this row's credit differs from
    // the album's.
    function shownArtistFor(item): string {
        if (!table.showArtistColumn)
            return ""
        const line = table.artistLineOf(item)
        return (line.length > 0 && line !== table.albumArtistName) ? line : ""
    }

    // > 0 when this row begins a numbered disc of a multi-disc set.
    function discFor(index): int {
        if (!table.multiDisc || table.discStartAt[index] === undefined)
            return -1
        return Number(table.discStartAt[index])
    }

    function rebuildMeta(): void {
        const count = table.count
        let total = 0
        let discs = []
        let starts = ({})
        let previousDisc = -32768
        let differs = false

        let credited = table.albumArtist
        if (credited.length === 0 && count > 0) {
            const first = table.rowAt(0)
            credited = (first && first.albumArtist !== undefined && first.albumArtist !== null)
                       ? String(first.albumArtist) : ""
        }

        if (table.discGrouping || table.artistRule) {
            for (let i = 0; i < count; ++i) {
                const entry = table.rowAt(i)
                if (!entry)
                    continue
                total += (entry.runtimeMs !== undefined) ? Number(entry.runtimeMs) : 0

                if (table.discGrouping) {
                    const disc = (entry.parentIndexNumber !== undefined)
                               ? Number(entry.parentIndexNumber) : -1
                    if (disc > 0 && discs.indexOf(disc) < 0)
                        discs.push(disc)
                    if (disc !== previousDisc) {
                        if (disc > 0)
                            starts[i] = disc
                        previousDisc = disc
                    }
                }

                if (table.artistRule) {
                    const line = table.artistLineOf(entry)
                    if (line.length > 0 && line !== credited)
                        differs = true
                }
            }
        }

        table.derivedAlbumArtist = credited
        table.totalRuntimeMs = total
        table.multiDisc = discs.length > 1
        table.discStartAt = discs.length > 1 ? starts : ({})
        table.showArtistColumn = differs
    }

    // ── Cursor ─────────────────────────────────────────────────────────────
    // The single funnel. Every movement below goes through it, which is what
    // makes a later Shift-extends-the-selection a change in one place.
    function _moveCursor(index): void {
        if (table.count === 0)
            return
        table.currentIndex = Math.max(0, Math.min(table.count - 1, index))
    }

    function _pageStep(): int {
        return Math.max(1, Math.floor(table.height / Math.max(1, table.rowHeight)))
    }

    // The single activation path.
    function activateAt(index): void {
        if (index < 0 || index >= table.count)
            return
        table.activated(index)
    }

    // ── Type-to-jump ───────────────────────────────────────────────────────
    // A 200-track box set is not navigable by arrow key. Prefix match on the
    // row's display role, from the row after the cursor on the first keystroke
    // and from the cursor itself once a word is being typed — so "so" lands on
    // the same row "s" did rather than skipping past it.
    property string _jumpBuffer: ""

    function _jump(text): bool {
        const needle = text.toLowerCase()
        const count = table.count
        if (needle.length === 0 || count === 0)
            return false
        const offset = text.length > 1 ? 0 : 1
        for (let step = 0; step < count; ++step) {
            const index = (table.currentIndex + offset + step) % count
            const entry = table.rowAt(index)
            if (!entry)
                continue
            const value = entry[table.jumpRole]
            if (value === undefined || value === null)
                continue
            if (String(value).toLowerCase().indexOf(needle) === 0) {
                table._moveCursor(index)
                table.positionViewAtIndex(index, ListView.Contain)
                return true
            }
        }
        return false
    }

    Timer {
        id: jumpReset
        interval: 900
        onTriggered: table._jumpBuffer = ""
    }

    // Not Component.onCompleted on the root: a use site that declares its own
    // would replace this one. A child's completes first and cannot be shadowed.
    QtObject {
        Component.onCompleted: table.rebuildMeta()
    }

    onCountChanged: table.rebuildMeta()
    onModelChanged: table.rebuildMeta()

    clip: true
    currentIndex: 0
    keyNavigationWraps: false
    highlightMoveDuration: Theme.animFastMs
    boundsBehavior: Flickable.StopAtBounds
    cacheBuffer: table.rowHeight * 12

    ScrollBar.vertical: StrmScrollBar {}

    Keys.onReturnPressed: event => {
        if (!event.isAutoRepeat)
            table.activateAt(table.currentIndex)
    }
    Keys.onEnterPressed: event => {
        if (!event.isAutoRepeat)
            table.activateAt(table.currentIndex)
    }

    Keys.onPressed: event => {
        // The page's own keys first, so a table verb can claim a key before the
        // navigation below sees it.
        table.keyPressed(event)
        if (event.accepted)
            return
        if (table.count === 0)
            return

        // A screen at a time, and the ends of a 300-track box set in one press.
        if (event.key === Qt.Key_PageDown) {
            table._moveCursor(table.currentIndex + table._pageStep())
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_PageUp) {
            table._moveCursor(table.currentIndex - table._pageStep())
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Home) {
            table._moveCursor(0)
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_End) {
            table._moveCursor(table.count - 1)
            event.accepted = true
            return
        }

        if (!table.typeToJump)
            return
        // Modified keys belong to whatever bound them; a bare space belongs to
        // play/pause until a word is already being typed.
        if (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
            return
        const typed = event.text
        if (typed.length !== 1)
            return
        const code = typed.charCodeAt(0)
        if (code < 0x20 || code === 0x7F)
            return
        if (typed === " " && table._jumpBuffer.length === 0)
            return

        const wanted = table._jumpBuffer + typed
        if (table._jump(wanted)) {
            table._jumpBuffer = wanted
            jumpReset.restart()
            event.accepted = true
        } else if (table._jumpBuffer.length > 0) {
            // A dead end mid-word still swallows the key: falling through to
            // "start a new search with this letter" is how a typo scrolls the
            // list somewhere unrelated.
            jumpReset.restart()
            event.accepted = true
        }
    }
}
