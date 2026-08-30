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
// and type-to-jump away. The same goes for `Keys.onShortcutOverride`, without
// which type-to-jump never sees a letter that is also a shortcut. Handlers for
// a specific key (`Keys.onDeletePressed`, `Keys.onUpPressed`) are separate
// signals and can still be declared outside.
//
// ── Multi-select ───────────────────────────────────────────────────────────
// Off by default (`multiSelect`), because a table whose page offers no batch
// verb would gain a selection nobody could do anything with. Where it is on it
// went exactly where the shape said it would: `_moveCursor()` is the single
// funnel every cursor movement passes through, so Shift+Up/Down extends from an
// anchor there; `activateAt()` is the single activation path, so Ctrl+Click and
// Shift+Click are decided there; and `selectedRows` drives `selected` on
// TrackRow.
//
// **The set is keyed by ROW INDEX, and that is what makes clearing it a rule
// rather than a judgement call.** An index means nothing once the rows behind
// it change, so:
//
//  · a model RESET clears it. `MediaItemModel::setItems()` is the one signal
//    that means "these are different rows" whatever the count says, and it is
//    what every refill goes through — a sort change, a genre filter, a letter,
//    opening a second album. So a filter change drops the selection, which is
//    also the honest answer on its own terms: the rows the user picked are not
//    on screen any more, and silently queueing them later would be a batch verb
//    acting on something invisible.
//  · a paged APPEND does not. loadMore() inserts rows after the ones already
//    there, every existing index still names the row it named, and a selection
//    made at row 4 must survive scrolling to row 400.
//
// Ctrl+Click TOGGLES and does not activate: a click that both added a row to
// the set and started playing it would make building a selection impossible.
ListView {
    id: table

    property string navigationFocusKey: ""
    property Item navigationFocusFallbackItem: null
    property bool navigationFocusRefillActive: false
    readonly property string navigationFocusKind: "tracks"
    readonly property bool navigationFocusRestorePending: navigationFocus.pending
    property bool _navigationFocusWriting: false
    property bool _navigationFocusPrefetchSuppressed: false
    property int _navigationFocusWriteGeneration: 0

    function navigationFocusSnapshot(): var { return navigationFocus.snapshot() }
    function restoreNavigationFocus(identity, index): bool {
        return navigationFocus.restore(identity, index)
    }
    function cancelNavigationFocusRestore(): void { navigationFocus.cancel() }
    function _cancelNavigationFocusForUser(): void {
        if (!table._navigationFocusWriting)
            navigationFocus.cancel()
    }
    function _applyNavigationFocus(index): void {
        table._navigationFocusWriting = true
        table._navigationFocusPrefetchSuppressed = true
        const generation = ++table._navigationFocusWriteGeneration
        if (index >= 0) {
            table.currentIndex = index
            table.positionViewAtIndex(index, ListView.Contain)
        }
        table.forceActiveFocus(Qt.OtherFocusReason)
        table._navigationFocusWriting = false
        Qt.callLater(() => {
            if (generation === table._navigationFocusWriteGeneration)
                table._navigationFocusPrefetchSuppressed = false
        })
    }
    function _applyNavigationFallback(): void {
        if (table.navigationFocusFallbackItem
                && table.navigationFocusFallbackItem.visible
                && table.navigationFocusFallbackItem.enabled) {
            table.navigationFocusFallbackItem.forceActiveFocus(Qt.OtherFocusReason)
            return
        }
        let candidate = table
        for (let step = 0; step < 256; ++step) {
            candidate = candidate.nextItemInFocusChain(true)
            if (!candidate || candidate === table)
                return
            let cursor = candidate
            while (cursor && cursor !== table)
                cursor = cursor.parent
            if (cursor !== table) {
                candidate.forceActiveFocus(Qt.OtherFocusReason)
                return
            }
        }
    }

    NavigationFocusRestorer {
        id: navigationFocus
        model: table.model
        count: table.count
        currentIndex: table.currentIndex
        refillActive: table.navigationFocusRefillActive
        onFocusRequested: index => table._applyNavigationFocus(index)
        onFallbackRequested: table._applyNavigationFallback()
    }

    Connections {
        target: table
        function onActiveFocusChanged() {
            if (!table.activeFocus)
                table._cancelNavigationFocusForUser()
        }
    }

    // ── Table-level rules ──────────────────────────────────────────────────
    // Walk the model for disc boundaries. Off by default: only an album has
    // discs, and the pass is not free on a 500-row playlist.
    property bool discGrouping: false
    // Walk the model to decide whether the artist column is worth a column.
    property bool artistRule: false
    // Skip that question and always give the artist a column. For a table whose
    // rows come from ONE record the rule above is the right answer — printing
    // "Opeth" six times is noise. For a library-wide song list the answer is
    // known in advance (every row may differ), and asking anyway would walk a
    // growing model on every page fetch to learn something already true.
    property bool alwaysShowArtist: false
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

    // ── Paging ─────────────────────────────────────────────────────────────
    // How close to the end counts as "nearly there". 0 — the default — means
    // this table never pages, which is right for every caller that fetches its
    // whole list in one request (an album, a playlist, a queue). The Songs tab
    // sets it, because 56,283 rows are not one request.
    //
    // The mirror of StrmGrid's prefetch, deliberately: the two conditions that
    // mean "fetch now" are the keyboard cursor closing on the end OR the
    // viewport showing the last rows, and a pointer user who never moves the
    // cursor depends entirely on the second. It fires at most once per loaded
    // count, so a handler may call loadMore() unconditionally — contentY moves
    // on every scrolled pixel and an unthrottled signal is a request storm.
    property int prefetchThreshold: 0

    // ── Selection ──────────────────────────────────────────────────────────
    property bool multiSelect: false
    // Row index → true. REPLACED rather than mutated on every change: a mutated
    // object notifies nothing and every row on screen would keep its old state.
    property var selectedRows: ({})
    // Where a Shift range is measured from. Set by every plain move and every
    // Ctrl+Click; never by an extending move, which is what makes Shift+Down
    // three times select three rows instead of one.
    property int selectionAnchor: -1
    property int selectionCount: 0
    // True only for the instant _moveCursor() spends inside an extending move,
    // so onCurrentIndexChanged can tell "the user walked somewhere" from "a
    // Shift range is being drawn".
    property bool _extending: false

    function isSelected(index): bool {
        return table.selectedRows[index] === true
    }

    function clearSelection(): void {
        if (table.selectionCount === 0 && table.selectionAnchor < 0)
            return
        table.selectedRows = ({})
        table.selectionCount = 0
        table.selectionAnchor = -1
    }

    function _applySelection(next): void {
        let count = 0
        for (const key in next) {
            if (next[key] === true)
                ++count
        }
        table.selectedRows = next
        table.selectionCount = count
    }

    function toggleSelection(index): void {
        if (!table.multiSelect || index < 0 || index >= table.count)
            return
        const next = Object.assign({}, table.selectedRows)
        if (next[index] === true)
            delete next[index]
        else
            next[index] = true
        table._applySelection(next)
        table.selectionAnchor = index
    }

    // Replaces the set with one contiguous run. A Shift range is a statement
    // about where the anchor and the cursor are, not an addition to whatever
    // was picked before it — the alternative accumulates runs the user cannot
    // see the boundaries of.
    function selectRange(from, to): void {
        if (!table.multiSelect || table.count === 0)
            return
        const lo = Math.max(0, Math.min(from, to))
        const hi = Math.min(table.count - 1, Math.max(from, to))
        const next = ({})
        for (let i = lo; i <= hi; ++i)
            next[i] = true
        table._applySelection(next)
    }

    function selectAll(): void {
        if (!table.multiSelect || table.count === 0)
            return
        table.selectRange(0, table.count - 1)
        table.selectionAnchor = 0
    }

    // The selected rows' item ids, in table order. The batch verbs' whole
    // contract: PlaylistPicker.show(subject, ids) takes exactly this, which is
    // why nothing about the picker or the playlist controller had to change.
    function selectedIds(): var {
        const out = []
        for (let i = 0; i < table.count; ++i) {
            if (!table.isSelected(i))
                continue
            const entry = table.rowAt(i)
            if (entry && entry.itemId !== undefined && entry.itemId !== null)
                out.push(String(entry.itemId))
        }
        return out
    }

    // The selected rows as item maps, for the verbs that want the whole record
    // (queueing needs the title and the type, not just an id).
    function selectedItems(): var {
        const out = []
        for (let i = 0; i < table.count; ++i) {
            if (!table.isSelected(i))
                continue
            const entry = table.rowAt(i)
            if (entry)
                out.push(entry)
        }
        return out
    }

    signal activated(int index)
    // The row's context menu, raised from the keyboard rather than the pointer:
    // TrackRow draws the same menu behind a right-click and a ⋯ button, and
    // neither exists on a pad. `x`/`y` are SCENE coordinates, the same as
    // TrackRow.menuRequested, so a page hands both to one handler.
    signal menuRequested(int index, real x, real y)
    // Raised before this component's own key handling; accept the event to
    // claim the key.
    signal keyPressed(var event)
    signal nearEnd()

    property int _lastNearEndCount: -1

    function _checkNearEnd(): void {
        if (navigationFocus.pending || table._navigationFocusPrefetchSuppressed
                || table.prefetchThreshold <= 0
                || table.count <= 0
                || table.count === table._lastNearEndCount)
            return
        const cursorNear = table.currentIndex >= 0
                           && table.count - table.currentIndex < table.prefetchThreshold
        const lastVisible = table.indexAt(table.contentX + 1,
                                          table.contentY + table.height - 1)
        const viewportNear = table.atYEnd
                             || (lastVisible >= 0
                                 && table.count - lastVisible < table.prefetchThreshold)
        if (cursorNear || viewportNear) {
            table._lastNearEndCount = table.count
            table.nearEnd()
        }
    }

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
    // the album's — unless the table was told the column is unconditional, in
    // which case suppressing the rows that happen to match the FIRST row's
    // credit would blank an arbitrary subset of a library-wide list.
    function shownArtistFor(item): string {
        if (!table.showArtistColumn)
            return ""
        const line = table.artistLineOf(item)
        if (line.length === 0)
            return ""
        return (table.alwaysShowArtist || line !== table.albumArtistName) ? line : ""
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
        table.showArtistColumn = differs || table.alwaysShowArtist
    }

    // ── Cursor ─────────────────────────────────────────────────────────────
    // The single funnel. Every movement below goes through it, which is what
    // made Shift-extends-the-selection a change in one place.
    //
    // `extend` is the Shift half: the cursor moves and the selection becomes
    // the run between the anchor and where it landed.
    //
    // Re-planting the anchor on a PLAIN move is NOT done here, and that is the
    // one thing in this component that could not be: plain Up/Down are
    // ListView's own key handling and never reach this function, so an anchor
    // moved only here would stay where the last Shift range began and the next
    // Shift+Down would select back to it across everything the user had walked
    // past in between. It is done in onCurrentIndexChanged instead, which every
    // move goes through whoever made it — this one, ListView's arrows,
    // type-to-jump, or a page assigning currentIndex directly.
    function _moveCursor(index, extend): void {
        if (table.count === 0)
            return
        const target = Math.max(0, Math.min(table.count - 1, index))
        const extending = extend === true && table.multiSelect
        if (extending && table.selectionAnchor < 0)
            table.selectionAnchor = table.currentIndex
        table._extending = extending
        table.currentIndex = target
        table._extending = false
        if (extending)
            table.selectRange(table.selectionAnchor, target)
    }

    // The context menu for the row under the ring. False when there is no row,
    // so the caller can leave the key unaccepted.
    function requestMenuForCurrent(): bool {
        const item = table.currentItem
        if (table.currentIndex < 0 || !item)
            return false
        table._cancelNavigationFocusForUser()
        const point = item.mapToItem(null, item.width / 2, item.height)
        table.menuRequested(table.currentIndex, point.x, point.y)
        return true
    }

    // A screenful, for a caller that is not a keypress — the pad's triggers on
    // a page with no alphabet strip to jump by.
    function pageBy(step): bool {
        if (table.count <= 0)
            return false
        const target = Math.max(0, Math.min(table.count - 1,
                                            table.currentIndex + step * table._pageStep()))
        if (target === table.currentIndex)
            return false
        table._moveCursor(target, false)
        table.forceActiveFocus(Qt.OtherFocusReason)
        return true
    }

    function _pageStep(): int {
        return Math.max(1, Math.floor(table.height / Math.max(1, table.rowHeight)))
    }

    // The single activation path, and the one place Ctrl+Click and Shift+Click
    // are decided. `modifiers` is Qt::KeyboardModifiers; omitted means none,
    // which is what every keyboard activation passes.
    function activateAt(index, modifiers): void {
        table._cancelNavigationFocusForUser()
        if (index < 0 || index >= table.count)
            return
        const mods = (modifiers === undefined || modifiers === null) ? Qt.NoModifier : modifiers
        if (table.multiSelect && (mods & Qt.ControlModifier)) {
            // Toggle, and do NOT activate: a click that both added the row to
            // the set and started playing it could never build a selection.
            table.currentIndex = index
            table.toggleSelection(index)
            return
        }
        if (table.multiSelect && (mods & Qt.ShiftModifier)) {
            table._moveCursor(index, true)
            return
        }
        // A plain activation is also "I am done with that set". Leaving it
        // standing would let the next batch verb act on rows the user believes
        // they have moved on from.
        table.clearSelection()
        table._moveCursor(index)
        table.activated(index)
    }

    // ── Type-to-jump ───────────────────────────────────────────────────────
    // A 200-track box set is not navigable by arrow key. Prefix match on the
    // row's display role, from the row after the cursor on the first keystroke
    // and from the cursor itself once a word is being typed — so "so" lands on
    // the same row "s" did rather than skipping past it.
    property string _jumpBuffer: ""

    // Is this keystroke typing rather than a shortcut? A printable character
    // with no Ctrl/Alt/Meta — a modified key belongs to whatever bound it — and
    // a bare space only once a word is already being typed, because until then
    // space belongs to play/pause. Asked by both `Keys.onPressed` and
    // `Keys.onShortcutOverride` so the key set they claim cannot drift apart.
    function _consumesAsTyping(event): bool {
        if (!table.typeToJump || table.count === 0)
            return false
        if (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
            return false
        const typed = event.text
        if (typed.length !== 1)
            return false
        const code = typed.charCodeAt(0)
        if (code < 0x20 || code === 0x7F)
            return false
        return typed !== " " || table._jumpBuffer.length > 0
    }

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

    onCountChanged: {
        table.rebuildMeta()
        table._checkNearEnd()
        navigationFocus.noteProgress()
    }
    onModelChanged: {
        // A different list is a different paging cursor: without this the
        // throttle still holds the previous model's count and the first page of
        // the new one never asks for a second.
        table._lastNearEndCount = -1
        navigationFocus.cancel()
        table.clearSelection()
        table.rebuildMeta()
    }

    // …and a different list is very often the SAME model object. MusicCtl.songs
    // is a CONSTANT Q_PROPERTY, so `onModelChanged` above fires once in the life
    // of the page and never again, while a filter or a sort change refills that
    // one model in place. The refilled list is frequently exactly a page long —
    // the same 100 rows the throttle last fired at — and then `count ===
    // _lastNearEndCount` short-circuits forever and the bottom of the filtered
    // list never loads page 2.
    //
    // MediaItemModel::setItems() is a model RESET, which is the one signal that
    // means "these are different rows" whatever the count says.
    Connections {
        target: Qt.isQtObject(table.model) ? table.model : null
        ignoreUnknownSignals: true
        function onModelReset() {
            table._lastNearEndCount = -1
            // …and the selection goes with it. See the header: the set is keyed
            // by row index, and a reset is the one signal that means the rows
            // behind those indices are different rows.
            table.clearSelection()
            // …and so does the derived metadata, for exactly the same reason.
            // onCountChanged is the only other trigger for rebuildMeta(), so a
            // setItems() landing on the SAME row count left showArtistColumn,
            // multiDisc, discStartAt, derivedAlbumArtist and totalRuntimeMs
            // describing the rows that were just replaced. The rule the throttle
            // and the selection follow has to be the whole rule.
            table.rebuildMeta()
            Qt.callLater(navigationFocus.noteProgress)
        }
    }
    onCurrentIndexChanged: {
        table._checkNearEnd()
        // Every plain move re-plants the anchor — see _moveCursor().
        if (!table._extending)
            table.selectionAnchor = table.currentIndex
    }
    onContentYChanged: table._checkNearEnd()

    clip: true
    currentIndex: 0
    keyNavigationWraps: false
    highlightMoveDuration: Theme.animFastMs
    boundsBehavior: Flickable.StopAtBounds
    cacheBuffer: table.rowHeight * 12
    reuseItems: true

    ScrollBar.vertical: StrmScrollBar {}

    // ── Letters have to beat the shortcuts ─────────────────────────────────
    // Qt matches a QML `Shortcut` in QShortcutMap *before* the key is ever
    // delivered as a key press, and this app binds a lot of bare letters:
    // "M" pins the nav rail, "F" full-screens, "/" searches and "?" opens the
    // shortcut sheet from Main.qml, and "K" "A" "C" "I" "L" "S" are player
    // actions that are live whenever the queue panel is — which is exactly
    // when a track table has focus. Without this, typing "m" on an album page
    // moved focus to the nav rail instead of landing on "Master of Puppets".
    //
    // ShortcutOverride is delivered to the focus item before the shortcut
    // fires, and accepting it suppresses that shortcut for that one keypress
    // only. So the claim is kept as narrow as it can be: only while this table
    // holds focus, and only for the keys `Keys.onPressed` below would treat as
    // typing. Chords are never touched, and every shortcut works again the
    // instant focus is anywhere else.
    Keys.onShortcutOverride: event => {
        if (table.activeFocus && table._consumesAsTyping(event))
            event.accepted = true
    }

    Keys.onReturnPressed: event => {
        table._cancelNavigationFocusForUser()
        if (!event.isAutoRepeat)
            table.activateAt(table.currentIndex, event.modifiers)
    }
    Keys.onEnterPressed: event => {
        table._cancelNavigationFocusForUser()
        if (!event.isAutoRepeat)
            table.activateAt(table.currentIndex, event.modifiers)
    }

    Keys.onPressed: event => {
        table._cancelNavigationFocusForUser()
        // The page's own keys first, so a table verb can claim a key before the
        // navigation below sees it.
        table.keyPressed(event)
        if (event.accepted)
            return
        // After the page's own keys, so a surface that already answers Menu
        // (the playlist member list, whose menu is a different list) keeps it.
        if (event.key === Qt.Key_Menu && !event.isAutoRepeat) {
            event.accepted = table.requestMenuForCurrent()
            return
        }
        if (table.count === 0)
            return

        // ── Selection keys ─────────────────────────────────────────────────
        // Up/Down are ListView's own and are deliberately left to it — but only
        // unmodified. Shift+Up/Down are claimed here, before the view sees
        // them, so the move and the range it implies happen together in
        // _moveCursor() rather than as a move the selection has to chase.
        const extend = table.multiSelect && (event.modifiers & Qt.ShiftModifier) !== 0
        if (extend && event.key === Qt.Key_Down) {
            table._moveCursor(table.currentIndex + 1, true)
            event.accepted = true
            return
        }
        if (extend && event.key === Qt.Key_Up) {
            table._moveCursor(table.currentIndex - 1, true)
            event.accepted = true
            return
        }
        if (table.multiSelect && event.key === Qt.Key_A
                && (event.modifiers & Qt.ControlModifier)) {
            table.selectAll()
            event.accepted = true
            return
        }
        // Only while there IS a selection: Esc is Back everywhere in this app
        // and swallowing it on an empty set would trap the user in the table.
        if (table.multiSelect && event.key === Qt.Key_Escape && table.selectionCount > 0) {
            table.clearSelection()
            event.accepted = true
            return
        }

        // A screen at a time, and the ends of a 300-track box set in one press.
        if (event.key === Qt.Key_PageDown) {
            table._moveCursor(table.currentIndex + table._pageStep(), extend)
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_PageUp) {
            table._moveCursor(table.currentIndex - table._pageStep(), extend)
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Home) {
            table._moveCursor(0, extend)
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_End) {
            table._moveCursor(table.count - 1, extend)
            event.accepted = true
            return
        }

        if (!table._consumesAsTyping(event))
            return

        const wanted = table._jumpBuffer + event.text
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
