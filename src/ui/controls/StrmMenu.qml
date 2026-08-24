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

    Accessible.role: Accessible.Menu
    Accessible.name: qsTr("Actions")

    property var actions: []
    property int currentIndex: -1
    // Off for a menu of TOGGLES rather than of commands: a multi-select genre
    // filter that closed on every tick would make picking three genres three
    // trips through the same menu. Esc, a click outside, or the control that
    // opened it still close it, so there is no way to get stuck in one.
    property bool closeOnTrigger: true

    // ── When the control that opens the menu IS the menu's parent ──────────
    // On for StrmSelect and nothing else, and it fixes a bug the queue peek had
    // in the same shape (MiniPlayer.qml): a press outside a
    // CloseOnPressOutside popup closes it there and then — the exit transition
    // is prepared synchronously, so `opened` is already false — while the
    // control that opened it fires on RELEASE. The press shut the menu and the
    // release of that same click reopened it, so a select could open its own
    // dropdown but never close it; only Esc or a click elsewhere could.
    //
    // OutsideParent is the distinction QQuickComboBox draws for its own popup,
    // for exactly this reason. It is only correct where the popup's parent is
    // the opening control (or an item containing it) AND a press elsewhere on
    // that parent is not expected to dismiss the menu. For StrmSelect both hold:
    // `parent: select`, and the whole of that parent is the toggle. It is
    // therefore opt-in rather than the default — every other consumer parents
    // the menu to a page or a rail, where a press on the parent is a press
    // somewhere else entirely and must close.
    property bool toggledFromParent: false

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

    // ── How tall the menu wants to be, and how tall it may be ──────────────
    // The contentItem used to be a plain Column in a Popup: no ceiling and
    // nothing to scroll. Every consumer up to now had under twenty rows, so the
    // ceiling was never reached — then the genre filter arrived with the
    // measured library's 289 of them, which at Theme.controlHeight is a popup
    // roughly 11,000 px tall. popupAt() only flips it, so all but the first
    // screenful ran off the bottom of the window and could not be reached at
    // all.
    //
    // `naturalHeight` is summed from `actions` rather than read off the view's
    // contentHeight, and that is deliberate on two counts. It is exact, where a
    // virtualised ListView's contentHeight is an estimate until every delegate
    // has been built — a menu whose own height wobbled as it scrolled would be
    // worse than one that did not scroll. And it is arrived at exactly the way
    // the Column's implicitHeight was: the same rows, at the same heights,
    // added up in the same order. A menu that fits is therefore the same size
    // it has always been, to the pixel.
    readonly property real naturalHeight: {
        let total = 0
        const rows = menu.actions
        for (let i = 0; i < rows.length; ++i) {
            total += (rows[i] && rows[i].separator === true) ? Theme.spacingTight
                                                             : Theme.controlHeight
        }
        return total
    }

    // What the window has room for: its full height, less the margin popupAt()
    // keeps at both edges and this popup's own padding. A menu shorter than
    // this is never touched by it.
    readonly property real maxContentHeight: {
        const host = menu.parent
        const winH = host ? host.Window.height : 0
        if (winH <= 0)
            return menu.naturalHeight // no window to measure against yet
        return Math.max(Theme.controlHeight,
                        winH - Theme.spacingTight * 2 - menu.topPadding - menu.bottomPadding)
    }

    readonly property bool scrollable: menu.naturalHeight > menu.maxContentHeight

    // ── Width ──────────────────────────────────────────────────────────────
    // Measured off one hidden ruler, walked over the labels, rather than off a
    // hidden Text per action. QQuickText recomputes implicitWidth synchronously
    // once implicitWidth has been read, so a single item reports exactly what N
    // of them reported — the widest label, to the pixel — while a 289-row menu
    // that rebuilds its actions on every tick (closeOnTrigger: false) no longer
    // builds and throws away 289 text items each time.
    //
    // Assigned rather than bound: the loop reads ruler.implicitWidth, and a
    // binding that both writes the ruler and depends on it is a binding-loop
    // report waiting to happen. The two moments the answer can change are the
    // actions changing and the menu being shown, and both call this.
    function _remeasureWidth() {
        let widest = 0
        const rows = menu.actions
        for (let i = 0; i < rows.length; ++i) {
            const entry = rows[i]
            ruler.text = (entry && entry.text !== undefined) ? entry.text : ""
            widest = Math.max(widest, ruler.implicitWidth)
        }
        menu.contentWidth = Math.max(Theme.scale(200),
                                     widest + Theme.iconSize + Theme.spacingLoose)
    }

    onActionsChanged: menu._remeasureWidth()

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
    closePolicy: menu.toggledFromParent
                 ? (Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
                    | Popup.CloseOnReleaseOutsideParent)
                 : (Popup.CloseOnEscape | Popup.CloseOnPressOutside
                    | Popup.CloseOnReleaseOutside)

    // aboutToShow, not onOpened: the popup takes focus when its enter
    // transition finishes, so by `opened` the item worth remembering is already
    // the one we would be trying to restore it from.
    onAboutToShow: {
        // Catches a density change (Theme.scale) between one open and the next,
        // which the actions themselves would not report.
        menu._remeasureWidth()
        const host = menu.parent
        menu._focusEscrow = host ? host.Window.activeFocusItem : null
    }

    onOpened: menu.currentIndex = menu._step(-1, 1)
    // Walking a long menu with the arrow keys has to bring the row into view.
    // On a menu that fits this is a no-op — there is nowhere to scroll to — so
    // small menus behave exactly as they did.
    onCurrentIndexChanged: {
        if (menu.currentIndex >= 0)
            list.positionViewAtIndex(menu.currentIndex, ListView.Contain)
    }
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

    // A ListView, not a Column: it virtualises, so a 289-row genre filter builds
    // the dozen delegates on screen instead of 289. The height is clamped to
    // what the window has room for, and only then does it scroll. A menu that
    // fits gets implicitHeight === naturalHeight === what the Column reported,
    // is not interactive, and never moves under positionViewAtIndex(), so
    // nothing about a six-row menu changes.
    //
    // ── The model is the COUNT, and the delegate reads the row ─────────────
    // Not `model: menu.actions`, which is what a ListView normally wants.
    // `actions` is rebuilt as a brand-new array on every pick — StrmSelect
    // recomputes it from `isSelected(i)` — and assigning a ListView a new model
    // clears the view and regenerates it, which puts contentY back to 0. With
    // closeOnTrigger:false the menu deliberately stays open, so ticking a genre
    // near the bottom of the 289 threw the user back to row 0 for the next
    // pick, defeating the whole point of staying open.
    //
    // An int model does not change when the rows are re-made, only when there
    // are a different NUMBER of them — so a re-tick re-evaluates the delegates'
    // `action` binding in place (the tick appears, nothing moves) while a
    // genuinely different list still rebuilds and opens at the top.
    contentItem: ListView {
        id: list

        focus: true
        clip: true
        spacing: 0
        model: menu.actions.length
        implicitHeight: Math.min(menu.naturalHeight, menu.maxContentHeight)
        interactive: menu.scrollable
        boundsBehavior: Flickable.StopAtBounds
        // The menu owns Up/Down (they skip separators and disabled rows, which
        // ListView's own key handling knows nothing about) and drives the
        // highlight through menu.currentIndex.
        keyNavigationEnabled: false
        currentIndex: -1
        cacheBuffer: Theme.controlHeight * 4

        ScrollBar.vertical: StrmScrollBar {}

        // Off-screen text measurement — see _remeasureWidth(). Invisible, so it
        // draws nothing and costs no layout of its own; it is a child of the view
        // only because the menu needs somewhere to keep it.
        Text {
            id: ruler
            visible: false
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontBodySize
        }

        // Arrow keys are allowed to auto-repeat — holding Down should walk the
        // list. Only the activation keys below are auto-repeat guarded.
        Keys.onUpPressed: menu.currentIndex =
            menu._step(menu.currentIndex < 0 ? menu.actions.length : menu.currentIndex, -1)
        Keys.onDownPressed: menu.currentIndex = menu._step(menu.currentIndex, 1)
        // Guard isAutoRepeat: a held Return must not fire an action twice.
        Keys.onReturnPressed: event => { if (!event.isAutoRepeat) menu._activate(menu.currentIndex) }
        Keys.onEnterPressed: event => { if (!event.isAutoRepeat) menu._activate(menu.currentIndex) }

        delegate: Item {
            id: row

            required property int index

            // The row itself, looked up rather than delivered: the view's model
            // is a count (see above), so this is the binding that carries a
            // re-made `actions` into an already-built delegate. `|| ({})` keeps
            // the properties below reading like an action with nothing set,
            // for the frame in which the count has grown and the array has not.
            readonly property var action: menu.actions[row.index] || ({})

            readonly property bool isSeparator: row.action.separator === true
            readonly property bool rowEnabled: !row.isSeparator && row.action.enabled !== false
            readonly property bool destructive: row.action.destructive === true
            readonly property bool current: menu.currentIndex === row.index

            // The view's width, which the Popup sizes to availableWidth — one
            // step further out than before only because a delegate is not a
            // direct child of the popup any more.
            width: list.width
            height: row.isSeparator ? Theme.spacingTight : Theme.controlHeight

            Accessible.ignored: row.isSeparator
            Accessible.role: Accessible.MenuItem
            Accessible.name: row.action.text !== undefined ? String(row.action.text) : ""
            Accessible.checkable: row.action.checked !== undefined
            Accessible.checked: row.action.checked === true
            Accessible.focused: row.current
            Accessible.onPressAction: menu._activate(row.index)

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
                        && row.action.iconName !== undefined
                        && String(row.action.iconName).length > 0
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacingTight
                anchors.verticalCenter: parent.verticalCenter
                name: row.action.iconName !== undefined ? row.action.iconName : ""
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
                text: row.action.text !== undefined ? row.action.text : ""
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
                visible: !row.isSeparator && row.action.checked === true
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
