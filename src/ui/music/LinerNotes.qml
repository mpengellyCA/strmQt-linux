pragma ComponentBehavior: Bound
import QtQuick
import StrmQt

// LinerNotes — the facts on the back of a sleeve (Crate spec §6.1).
//
// Rows come finished from AlbumCtl.linerNotes: {label, value, links}. Empty
// rows are already left out, so nothing here decides what is worth showing. A
// row with links (Genre) draws them as chips that open the genre. Any other
// row is a plain value.
//
// ── Anchor this, or set its width ──────────────────────────────────────────
// A Column takes its width from its children and every row here takes its
// width from this Column, so a consumer that sets NEITHER `width` NOR left and
// right anchors converges at width 0 *and* height 0 — with no binding loop, no
// runtime warning and no qmllint warning. The panel is simply not there and the
// Column above it closes up as if the notes had never been declared. The
// default `width` below is the guard rail: an unanchored consumer gets a narrow
// but legible panel instead of an invisible one. It is not a substitute for
// anchoring.
//
// ── Keyboard (ARCHITECTURE.md §4) ──────────────────────────────────────────
// A row of genre chips is a row of destinations, and a destination the pointer
// can reach and the keyboard cannot is a bug this app has shipped three times.
// So the link row follows the contract DetailsPage.qml's `ChipStrip` sets: ONE
// tab stop for the whole row rather than one per chip, the row owns Left/Right
// and Return, and each chip shows the cursor through LinkChip's `highlighted`
// and never takes focus itself. The container is a Flow rather than ChipStrip's
// horizontal ListView because liner notes wrap down a narrow panel instead of
// scrolling sideways; the focus contract is the same one.
Column {
    id: notes

    property var rows: []

    signal linkActivated(string id, string name)

    // See "Anchor this, or set its width" above. A Column's own implicit width
    // is its widest child, which here is a child sized from this Column — so
    // without a width of its own the fixed point is zero and the panel
    // disappears silently.
    //
    // This is a default `width` and not an `implicitWidth` because a positioner
    // re-declares implicitWidth/implicitHeight READ-ONLY (QQuickImplicitSizeItem),
    // so `implicitWidth: …` here is a *runtime* "Invalid property assignment"
    // that makes the whole type unavailable — and, because it is a runtime
    // error rather than a compile error, neither the build nor qmllint says a
    // word about it. Anchoring or setting `width` at the use site overrides
    // this, which is what every real consumer should do.
    width: Theme.scale(320)

    spacing: Theme.spacingValue

    Repeater {
        model: notes.rows

        delegate: Column {
            id: noteRow

            required property var modelData

            readonly property var links: noteRow.modelData.links ? noteRow.modelData.links : []

            width: notes.width
            spacing: Theme.scale(4)

            CrateKicker {
                text: String(noteRow.modelData.label)
            }

            Text {
                width: parent.width
                visible: noteRow.links.length === 0
                text: String(noteRow.modelData.value)
                color: Theme.textPrimaryColor
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontBodySize
                wrapMode: Text.WordWrap
                maximumLineCount: 3
                elide: Text.ElideRight
            }

            Flow {
                id: chipFlow

                // Which chip the keyboard is on. Never driven by hover: hover
                // previews, the ring follows the keyboard.
                property int currentLink: 0

                width: parent.width
                visible: noteRow.links.length > 0
                spacing: Theme.spacingTight

                // The row's single tab stop, tied to `visible` rather than left
                // at a bare `true`: Qt keeps an invisible item out of the focus
                // chain, but saying so here is what stops a later edit from
                // leaving a hidden row in the Tab order.
                //
                // `|| activeFocus` is not belt-and-braces, it is required, and
                // it was measured: QQuickItem refuses to clear activeFocusOnTab
                // on the item that currently HOLDS focus and prints "Cannot set
                // activeFocusOnTab to false once item is the active focus item"
                // — which is exactly the moment a panel hides under the
                // keyboard. Holding the flag true for that instant lets
                // releaseFocus() below hand the keyboard on first; the binding
                // then re-evaluates to false on its own, because activeFocus has
                // gone with it.
                activeFocusOnTab: chipFlow.visible || chipFlow.activeFocus

                function moveCursor(step: int): void {
                    const count = noteRow.links.length
                    if (count === 0)
                        return
                    chipFlow.currentLink = Math.max(0, Math.min(count - 1,
                                                                chipFlow.currentLink + step))
                }

                function activateCurrent(): void {
                    const links = noteRow.links
                    if (chipFlow.currentLink < 0 || chipFlow.currentLink >= links.length)
                        return
                    const link = links[chipFlow.currentLink]
                    const id = (link && link.id !== undefined) ? String(link.id) : ""
                    // An id-less genre is not a link (LinkChip renders it as
                    // plain text), so Return does nothing rather than opening
                    // an empty destination.
                    if (id.length === 0)
                        return
                    notes.linkActivated(id, (link.name !== undefined) ? String(link.name) : "")
                }

                // Qt clears active focus on `enabled: false` but NOT on
                // `visible: false`, and this row's visibility follows the whole
                // panel's as well as its own link count — a page that hides its
                // liner notes while a chip row holds the keyboard would strand
                // it in an invisible item. Hand the keyboard on instead.
                // Deferred, because the sibling bindings that decide where
                // focus should land have not settled when this fires.
                function releaseFocus(): void {
                    if (chipFlow.visible || !chipFlow.activeFocus)
                        return
                    const next = chipFlow.nextItemInFocusChain(true)
                    if (next && next !== chipFlow)
                        next.forceActiveFocus(Qt.OtherFocusReason)
                }

                onVisibleChanged: {
                    if (!chipFlow.visible && chipFlow.activeFocus)
                        Qt.callLater(chipFlow.releaseFocus)
                }

                // Clamped, not wrapped, and accepted at both ends — the same
                // thing ChipStrip's `keyNavigationWraps: false` ListView does,
                // so Left/Right feel identical on both surfaces.
                Keys.onLeftPressed: event => {
                    chipFlow.moveCursor(-1)
                    event.accepted = true
                }
                Keys.onRightPressed: event => {
                    chipFlow.moveCursor(1)
                    event.accepted = true
                }
                // Guarded against auto-repeat like every other activation path
                // in this app.
                Keys.onReturnPressed: event => {
                    if (!event.isAutoRepeat)
                        chipFlow.activateCurrent()
                }
                Keys.onEnterPressed: event => {
                    if (!event.isAutoRepeat)
                        chipFlow.activateCurrent()
                }

                Repeater {
                    model: noteRow.links

                    delegate: LinkChip {
                        id: chip

                        required property int index
                        required property var modelData

                        readonly property string linkId: (chip.modelData
                                                          && chip.modelData.id !== undefined)
                                                         ? String(chip.modelData.id) : ""

                        label: String(chip.modelData.name)
                        iconName: "lib-music"
                        // No id (an older payload) → plain text, not a dead pill
                        // the cursor can land on and Return cannot open.
                        // LinkChip.qml records the rule.
                        linked: chip.linkId.length > 0
                        highlighted: chip.index === chipFlow.currentLink && chipFlow.activeFocus

                        // A click commits AND makes this chip the keyboard's
                        // place, so a following arrow key continues from here.
                        onActivated: {
                            chipFlow.currentLink = chip.index
                            chipFlow.forceActiveFocus(Qt.MouseFocusReason)
                            notes.linkActivated(chip.linkId, String(chip.modelData.name))
                        }
                    }
                }
            }
        }
    }
}
