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
Item {
    id: select

    property var model: []
    property int currentIndex: -1
    property string placeholder: qsTr("Select…")

    signal activated(int index)

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

    implicitHeight: Theme.controlHeight
    implicitWidth: Math.max(Theme.scale(160),
                            labelText.implicitWidth + Theme.iconSize + Theme.spacingLoose)
    width: implicitWidth
    height: implicitHeight

    activeFocusOnTab: true

    function toggle() {
        if (menu.opened) {
            menu.close()
        } else {
            const p = select.mapToItem(null, 0, select.height + Theme.scale(4))
            menu.popupAt(p.x, p.y)
        }
    }

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
        text: select.currentIndex >= 0 ? select.currentText : select.placeholder
        color: select.currentIndex >= 0 ? Theme.textPrimaryColor : Theme.textTertiary
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
        actions: {
            const out = []
            const source = select.model || []
            for (let i = 0; i < source.length; ++i)
                out.push({ text: select.textAt(i), checked: i === select.currentIndex })
            return out
        }
        onTriggered: index => {
            select.currentIndex = index
            select.activated(index)
        }
    }
}
