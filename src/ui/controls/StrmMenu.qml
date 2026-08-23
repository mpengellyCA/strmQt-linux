pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import StrmQt

// StrmMenu — context menu / action sheet (ARCHITECTURE.md).
//
// Built on Popup so it renders in the window's overlay layer: a menu opened
// from a card inside a clipped, scrolling rail must not be cut off by that
// rail, and it must close when the user clicks anywhere else.
//
// `actions` is a plain array of objects; every field is optional:
//   { text, iconName, enabled, checked, separator, destructive }
//
// Fully keyboard-driven: Up/Down move (skipping separators and disabled rows),
// Return/Enter activate, Esc closes. Hover previews the row under the cursor by
// moving `currentIndex` — which is a preview, not a commit, exactly as with
// cards: pointer and keyboard drive the same highlight but only a click or a
// Return commits.
Popup {
    id: menu

    property var actions: []
    property int currentIndex: -1
    // Off for a menu of TOGGLES rather than of commands: a multi-select genre
    // filter that closed on every tick would make picking three genres three
    // trips through the same menu. Esc, a click outside, or the control that
    // opened it still close it, so there is no way to get stuck in one.
    property bool closeOnTrigger: true

    signal triggered(int index)

    // ── Focus escrow (ARCHITECTURE.md) ─────────────────────────────────────
    // `focus: true` below hands the keyboard to the popup, and QQC2 does not
    // hand it back: closing a menu left nothing focused, so the arrow keys were
    // dead until the user clicked something. Every other overlay in the app
    // already returns focus explicitly — ShortcutSheet and CommandPalette
    // through Main.qml's restoreFocusToPage(), the playlist picker to the
    // button that opened it — and a menu is the one that cannot delegate it,
    // because it is raised from cards, rails, grids, the top bar and every
    // StrmSelect. So it remembers the item itself rather than making six
    // callers remember it.
    //
    // Typed as Item, not stashed in a JS map like Main.qml's focus memory, so
    // QML nulls it for us if the page it belonged to is destroyed while the
    // menu is open. What remains to guard is an item that outlived its page but
    // is no longer reachable — hence the visible/enabled test on restore.
    property Item _focusEscrow: null

    // ── Placement ──────────────────────────────────────────────────────────
    // x/y arrive in SCENE coordinates (what StrmCard.menuRequested emits), and
    // are flipped rather than clamped when the menu would run off the window:
    // a menu that overlaps its anchor is worse than one that opens upward.
    function popupAt(sceneX, sceneY) {
        const host = menu.parent
        if (!host)
            return
        const winW = host.Window.width
        const winH = host.Window.height
        let sx = sceneX
        let sy = sceneY
        if (winW > 0 && sx + menu.width > winW - Theme.spacingTight)
            sx = Math.max(Theme.spacingTight, sceneX - menu.width)
        if (winH > 0 && sy + menu.height > winH - Theme.spacingTight)
            sy = Math.max(Theme.spacingTight, sceneY - menu.height)
        const p = host.mapFromItem(null, sx, sy)
        menu.x = p.x
        menu.y = p.y
        menu.open()
    }

    function _isSelectable(i) {
        if (i < 0 || i >= menu.actions.length)
            return false
        const a = menu.actions[i]
        return !a.separator && a.enabled !== false
    }

    function _step(from, delta) {
        let i = from + delta
        while (i >= 0 && i < menu.actions.length) {
            if (menu._isSelectable(i))
                return i
            i += delta
        }
        return menu._isSelectable(from) ? from : -1
    }

    function _activate(i) {
        if (!menu._isSelectable(i))
            return
        if (menu.closeOnTrigger)
            menu.close()
        menu.triggered(i)
    }

    // Give the keyboard back to wherever it was when the menu went up, in the
    // defensive shape Main.qml's restoreFocusToPage() uses: a remembered item
    // is only worth focusing while it is still on screen and still enabled.
    function _restoreFocus() {
        // Re-opened while the restore was queued: the escrow now belongs to
        // that open and its own close will return it. Tested before the escrow
        // is read, or this call clears the new one; and yanking focus out of a
        // menu the user is looking at is a worse bug than the one being fixed.
        if (menu.visible)
            return
        const remembered = menu._focusEscrow
        menu._focusEscrow = null
        const host = menu.parent
        if (!remembered || !host)
            return
        // Never override a REAL claim on the keyboard. A press outside closes
        // the menu and also lands on whatever was underneath, and a row that
        // pushes a page has already focused that page by the time the exit
        // transition ends. What QQC2 leaves behind is nothing focused, or bare
        // window content with no focus chain under it — those two, and only
        // those two, are this menu's to repair.
        //
        // "Bare window content" is two different items here: Main.qml is an
        // ApplicationWindow, and QQC2 hands focus to its contentItem — a child
        // of the window's own contentItem, not the same object. Testing only
        // the window root made this return early in exactly the case it exists
        // for. For a plain Window the two are the same item and the second
        // test is a no-op.
        const current = host.Window.activeFocusItem
        const win = host.Window.window
        const bare = current === host.Window.contentItem
                     || (win && current === win.contentItem)
        if (current && !bare)
            return
        if (remembered.visible && remembered.enabled)
            remembered.forceActiveFocus(Qt.OtherFocusReason)
    }

    // ── Popup configuration ────────────────────────────────────────────────
    padding: Theme.spacingTight / 2
    modal: false
    dim: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside | Popup.CloseOnReleaseOutside

    contentWidth: Math.max(Theme.scale(200),
                           metrics.implicitWidth + Theme.iconSize + Theme.spacingLoose)

    // aboutToShow, not onOpened: the popup takes focus when its enter
    // transition finishes, so by `opened` the item worth remembering is already
    // the one we would be trying to restore it from.
    onAboutToShow: {
        const host = menu.parent
        menu._focusEscrow = host ? host.Window.activeFocusItem : null
    }

    onOpened: menu.currentIndex = menu._step(-1, 1)
    // Deferred, the way Main.qml defers its own focus restores: `closed` fires
    // inside the popup's own teardown, which is still settling focus, and a
    // forceActiveFocus() made in the middle of that is simply overwritten.
    onClosed: {
        menu.currentIndex = -1
        Qt.callLater(menu._restoreFocus)
    }

    enter: Transition {
        NumberAnimation {
            property: "opacity"; from: 0; to: 1
            duration: Theme.animFastMs; easing.type: Theme.easeStandard
        }
    }
    exit: Transition {
        NumberAnimation {
            property: "opacity"; from: 1; to: 0
            duration: Theme.animInstant; easing.type: Theme.easeInstant
        }
    }

    background: Rectangle {
        radius: Theme.radiusPanel
        color: Theme.surfaceOverlay
        border.width: 1
        border.color: Theme.hairline
    }

    contentItem: Column {
        id: column
        focus: true
        spacing: 0

        // Arrow keys are allowed to auto-repeat — holding Down should walk the
        // list. Only the activation keys below are auto-repeat guarded.
        Keys.onUpPressed: menu.currentIndex =
            menu._step(menu.currentIndex < 0 ? menu.actions.length : menu.currentIndex, -1)
        Keys.onDownPressed: menu.currentIndex = menu._step(menu.currentIndex, 1)
        // Guard isAutoRepeat: a held Return must not fire an action twice.
        Keys.onReturnPressed: event => { if (!event.isAutoRepeat) menu._activate(menu.currentIndex) }
        Keys.onEnterPressed: event => { if (!event.isAutoRepeat) menu._activate(menu.currentIndex) }

        // Off-screen text measurement. Positioners skip invisible children, so
        // this never occupies a row; it exists only so `contentWidth` can be
        // derived from the widest label WITHOUT the child-width ⇄ menu-width
        // binding loop that measuring the visible rows would create.
        Column {
            id: metrics
            visible: false

            Repeater {
                model: menu.actions
                delegate: Text {
                    required property var modelData
                    text: modelData.text !== undefined ? modelData.text : ""
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontBodySize
                }
            }
        }

        Repeater {
            model: menu.actions

            delegate: Item {
                id: row

                required property var modelData
                required property int index

                readonly property bool isSeparator: row.modelData.separator === true
                readonly property bool rowEnabled: !row.isSeparator && row.modelData.enabled !== false
                readonly property bool destructive: row.modelData.destructive === true
                readonly property bool current: menu.currentIndex === row.index

                width: menu.availableWidth
                height: row.isSeparator ? Theme.spacingTight : Theme.controlHeight

                // Separator: a hairline with breathing room, not a row.
                Rectangle {
                    visible: row.isSeparator
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: Theme.spacingTight
                    anchors.rightMargin: Theme.spacingTight
                    height: 1
                    color: Theme.hairline
                }

                Rectangle {
                    visible: !row.isSeparator
                    anchors.fill: parent
                    radius: Theme.radiusChip
                    color: row.current && row.rowEnabled ? Theme.hoverTint : "transparent"

                    Behavior on color {
                        ColorAnimation { duration: Theme.animInstant; easing.type: Theme.easeInstant }
                    }
                }

                StrmIcon {
                    id: rowIcon
                    visible: !row.isSeparator
                            && row.modelData.iconName !== undefined
                            && String(row.modelData.iconName).length > 0
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacingTight
                    anchors.verticalCenter: parent.verticalCenter
                    name: row.modelData.iconName !== undefined ? row.modelData.iconName : ""
                    size: Theme.iconSize
                    color: !row.rowEnabled ? Theme.textDisabled
                         : row.destructive ? Theme.negative
                         : Theme.textSecondaryColor
                }

                Text {
                    visible: !row.isSeparator
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacingTight * 2 + Theme.iconSize
                    anchors.right: checkMark.left
                    anchors.rightMargin: Theme.spacingTight
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.modelData.text !== undefined ? row.modelData.text : ""
                    color: !row.rowEnabled ? Theme.textDisabled
                         : row.destructive ? Theme.negative
                         : Theme.textPrimaryColor
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontBodySize
                    elide: Text.ElideRight
                }

                StrmIcon {
                    id: checkMark
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingTight
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !row.isSeparator && row.modelData.checked === true
                    name: "check"
                    size: Theme.iconSize
                    color: Theme.accentColor
                }

                HoverHandler {
                    enabled: row.rowEnabled
                    cursorShape: Qt.PointingHandCursor
                    // Preview only — hover moves the highlight, it never commits.
                    onHoveredChanged: if (hovered) menu.currentIndex = row.index
                }

                TapHandler {
                    enabled: row.rowEnabled
                    acceptedButtons: Qt.LeftButton
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: menu._activate(row.index)
                }
            }
        }
    }
}
