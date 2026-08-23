import QtQuick
import StrmQt

// StrmSelect — a dropdown shaped like a StrmButton that opens a StrmMenu
// (ARCHITECTURE.md). Used for sort order, quality caps, track pickers.
//
// `model` accepts either a list of plain strings or a list of
// { text, value } objects; `valueAt(i)` returns the value for the latter and
// the string itself for the former, so callers never have to branch.
//
// Shaped like StrmButton rather than *being* one: a select needs its own
// trailing chevron and its own pressed/open state, and inheriting a button's
// content item to bolt those on would be more coupling than it is worth.
//
// `currentIndex` is *controlled*: the select renders the index its owner hands
// it and never writes it. That is the single-owner rule in ARCHITECTURE.md, and
// it is not a purity argument — a JS write here destroys the owner's binding,
// after which the label and the thing it names are two variables that agree
// only until one of them moves. Both cases that move it are ones this app
// actually has: a remembered version resolving after the details payload lands,
// and PlayerController refusing an unplayable source so the picker has to snap
// back to what is really playing.
//
// The contract that buys: **every consumer must route `activated` to whatever
// owns the index**, directly or through a controller round-trip. A handler that
// does not leaves a select that never changes — visible on the first click,
// which is the failure worth having.
Item {
    id: select

    property var model: []
    // Owned by the consumer. See the note above before assigning it from here.
    property int currentIndex: -1
    property string placeholder: qsTr("Select…")

    // ── Multi-select (ARCHITECTURE.md) ─────────────────────────────────────
    // A music library has 289 genres (measured), which is a set no row of chips
    // can render and no single-pick select can express. In this mode the label
    // names the selection rather than one row, every matching row is ticked,
    // and the menu stays open so picking three genres is one trip.
    //
    // `selectedValues` is controlled exactly as `currentIndex` is: the select
    // renders what it is handed and never writes it. `activated(index)` still
    // reports the row that was hit — the owner decides what toggling it means,
    // which is what keeps the set of ids owned by the controller.
    property bool multiSelect: false
    property var selectedValues: []

    signal activated(int index)

    function isSelected(i): bool {
        if (!select.multiSelect)
            return i === select.currentIndex
        const value = select.valueAt(i)
        if (value === undefined || !select.selectedValues)
            return false
        return Array.prototype.indexOf.call(select.selectedValues, value) >= 0
    }

    readonly property bool opened: menu.opened
    readonly property bool hovered: hover.hovered

    function textAt(i) {
        if (!select.model || i < 0 || i >= select.model.length)
            return ""
        const entry = select.model[i]
        return (entry !== null && typeof entry === "object" && entry.text !== undefined)
                ? entry.text : String(entry)
    }

    function valueAt(i) {
        if (!select.model || i < 0 || i >= select.model.length)
            return undefined
        const entry = select.model[i]
        return (entry !== null && typeof entry === "object" && entry.value !== undefined)
                ? entry.value : entry
    }

    readonly property string currentText: textAt(currentIndex)
    readonly property var currentValue: valueAt(currentIndex)

    readonly property int selectedCount: (select.multiSelect && select.selectedValues)
                                         ? select.selectedValues.length : 0

    // What the closed control says. One pick names itself — a filter reading
    // "Doom Metal" is the whole point of it — and beyond that a count, because
    // three genre names do not fit and a truncated list reads as a bug.
    readonly property string _multiLabel: {
        if (select.selectedCount === 0)
            return select.placeholder
        if (select.selectedCount === 1) {
            const source = select.model || []
            for (let i = 0; i < source.length; ++i) {
                if (select.isSelected(i))
                    return select.textAt(i)
            }
        }
        return qsTr("%1 selected").arg(select.selectedCount)
    }

    implicitHeight: Theme.controlHeight
    implicitWidth: Math.max(Theme.scale(160),
                            labelText.implicitWidth + Theme.iconSize + Theme.spacingLoose)
    width: implicitWidth
    height: implicitHeight

    activeFocusOnTab: true

    // A control could not close its own dropdown before `toggledFromParent`
    // existed: the press closed the menu and the release of that same click
    // reopened it. See StrmMenu.toggledFromParent for the whole diagnosis — it
    // is the bug the queue peek had, in the same shape.
    function toggle() {
        if (menu.opened) {
            menu.close()
        } else {
            const p = select.mapToItem(null, 0, select.height + Theme.scale(4))
            menu.popupAt(p.x, p.y)
        }
    }

    // Hiding a popup's PARENT does not close the popup — it renders in the
    // window's overlay layer and simply keeps floating there, with only Esc to
    // find it (measured on Qt 6.11 for the queue peek, and it is the same
    // popup class here). A select disappears whenever the view around it
    // changes: FilterBar's genre control is a Repeater delegate that goes away
    // with the filter set, and the sort control's options change per tab.
    onVisibleChanged: { if (!select.visible) menu.close() }
    onEnabledChanged: { if (!select.enabled) menu.close() }

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        id: tap
        acceptedButtons: Qt.LeftButton
        gesturePolicy: TapHandler.ReleaseWithinBounds
        // Opening a menu with the mouse is a commit, so focus does move here —
        // unlike plain hover, which never steals focus.
        onTapped: {
            select.forceActiveFocus(Qt.MouseFocusReason)
            select.toggle()
        }
    }

    // Space has to beat the shortcuts, the same way TrackTable's letters do:
    // `music.playPause` binds it and a window-scoped Shortcut is matched in
    // QShortcutMap before the key reaches this item, so on a music page with a
    // queue loaded the sort and genre dropdowns could not be opened from the
    // keyboard at all. ShortcutOverride is delivered to the focus item first
    // and accepting it suppresses that shortcut for that one press — claimed
    // only while this control holds focus, and only for the key it answers.
    Keys.onShortcutOverride: event => {
        if (select.activeFocus && event.key === Qt.Key_Space)
            event.accepted = true
    }

    Keys.onReturnPressed: event => { if (!event.isAutoRepeat) select.toggle() }
    Keys.onEnterPressed: event => { if (!event.isAutoRepeat) select.toggle() }
    Keys.onSpacePressed: event => { if (!event.isAutoRepeat) select.toggle() }

    Rectangle {
        id: surface
        anchors.fill: parent
        radius: Theme.radiusChip
        color: tap.pressed ? Theme.pressTint
             : (select.hovered || select.opened) ? Theme.surfaceRaisedColor
             : Theme.surfaceColor
        border.width: 1
        border.color: select.opened ? Theme.accentColor : Theme.hairline

        Behavior on color {
            ColorAnimation { duration: Theme.animInstant; easing.type: Theme.easeInstant }
        }
    }

    Text {
        id: labelText
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacingTight * 1.5
        anchors.right: chevron.left
        anchors.rightMargin: Theme.spacingTight
        anchors.verticalCenter: parent.verticalCenter
        text: select.multiSelect ? select._multiLabel
            : select.currentIndex >= 0 ? select.currentText
            : select.placeholder
        color: (select.multiSelect ? select.selectedCount > 0 : select.currentIndex >= 0)
               ? Theme.textPrimaryColor : Theme.textTertiary
        font.family: Theme.fontBody
        font.pixelSize: Theme.fontBodySize
        elide: Text.ElideRight
    }

    // Flips to point back at the control while the menu is open, so the
    // affordance reads as "this closes again" rather than as decoration.
    StrmIcon {
        id: chevron
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingTight
        anchors.verticalCenter: parent.verticalCenter
        name: "chevron-down"
        size: Theme.iconSize
        color: Theme.textSecondaryColor
        rotation: select.opened ? 180 : 0

        Behavior on rotation {
            NumberAnimation { duration: Theme.animFastMs; easing.type: Theme.easeStandard }
        }
    }

    FocusRing {
        active: select.activeFocus
        anchors.fill: surface
        radius: Theme.radiusChip
        inset: -Theme.focusRingWidth
    }

    StrmMenu {
        id: menu
        parent: select
        // The proviso StrmMenu.toggledFromParent states, met here: the popup's
        // parent is the opening control itself, and the whole of that parent IS
        // the toggle — there is no part of it a press should dismiss the menu
        // from. A press anywhere off the control still closes it.
        toggledFromParent: true
        // Toggles stay open, commands close. See StrmMenu.closeOnTrigger.
        closeOnTrigger: !select.multiSelect
        actions: {
            const out = []
            const source = select.model || []
            for (let i = 0; i < source.length; ++i)
                out.push({ text: select.textAt(i), checked: select.isSelected(i) })
            return out
        }
        // Asks, and does not decide: the owner writes the value back and the
        // binding carries it home. Assigning select.currentIndex here would
        // drop that binding on the user's very first pick.
        onTriggered: index => select.activated(index)
    }
}
