import QtQuick
import StrmQt

// FilterPill — one Browse filter (Crate spec §5.2).
//
// Two kinds share one control. A menu pill (Genre, Decade, Format) opens a
// chooser on activation and, once set, names its value ("Decade: 70s") with a
// ✕ that clears it. A toggle pill (♡ Favourites, Unplayed) is filled while on.
//
// Controlled like StrmChip: it renders `active` and `text` and never changes
// them. The controller owns the query; the page routes both signals to it.
Item {
    id: pill

    property string text: ""
    property bool active: false
    property bool clearable: pill.active
    property bool toggle: false
    property string iconName: ""
    property string accessibleName: pill.text

    signal activated
    signal cleared

    readonly property bool hovered: hover.hovered
    readonly property bool pressed: tap.pressed
    readonly property bool filled: pill.toggle && pill.active

    readonly property color labelColor: {
        if (!pill.enabled)
            return Theme.textDisabled;
        if (pill.filled)
            return Theme.accentText;
        if (pill.active)
            return Theme.accentColor;
        if (pill.hovered || pill.activeFocus)
            return Theme.textPrimaryColor;
        return Theme.textSecondaryColor;
    }

    implicitHeight: Theme.scale(32)
    implicitWidth: row.implicitWidth + 2 * Theme.scale(12)
    activeFocusOnTab: pill.enabled

    Accessible.role: pill.toggle ? Accessible.CheckBox : Accessible.Button
    Accessible.name: pill.accessibleName
    Accessible.description: pill.clearable && !pill.toggle ? qsTr("Delete clears this filter") : ""
    Accessible.checkable: pill.toggle
    Accessible.checked: pill.toggle && pill.active
    Accessible.focusable: pill.enabled
    Accessible.focused: pill.activeFocus
    Accessible.pressed: pill.pressed
    Accessible.onPressAction: pill.activate()
    Accessible.onToggleAction: pill.activate()

    scale: !pill.enabled ? 1.0
         : pill.pressed ? Theme.pressScale
         : pill.activeFocus ? Theme.focusScale
         : pill.hovered ? Theme.hoverScale
         : 1.0

    Behavior on scale {
        NumberAnimation {
            duration: pill.activeFocus ? Theme.animFastMs : Theme.animInstant
            easing.type: pill.activeFocus ? Theme.easeStandard : Theme.easeInstant
        }
    }

    function activate(): void {
        if (pill.enabled)
            pill.activated();
    }

    Rectangle {
        id: bg

        anchors.fill: parent
        radius: Theme.radiusChip
        color: pill.filled && pill.enabled ? Theme.accentColor : "transparent"
        border.width: pill.filled ? 0 : 1
        border.color: pill.active && pill.enabled ? Theme.accentColor : Theme.hairline

        Behavior on color {
            ColorAnimation { duration: Theme.animFastMs; easing.type: Theme.easeStandard }
        }
    }

    Rectangle {
        anchors.fill: bg
        radius: bg.radius
        color: !pill.enabled ? "transparent"
             : pill.pressed ? Theme.pressTint
             : pill.hovered ? Theme.hoverTint
             : "transparent"
    }

    Row {
        id: row

        anchors.centerIn: parent
        spacing: Theme.scale(6)

        StrmIcon {
            anchors.verticalCenter: parent.verticalCenter
            visible: pill.iconName.length > 0
            name: pill.iconName
            color: pill.labelColor
            size: Theme.scale(14)
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: pill.text.length > 0
            text: pill.text
            color: pill.labelColor
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontSmall
            font.weight: pill.active ? Font.DemiBold : Font.Normal
        }

        // Its own hit area, so clearing a filter is never mistaken for opening
        // its chooser. Toggle pills clear by toggling, so they have none.
        //
        // Ruling P2-R2: this affordance is only ever visible while the pill is
        // clearable, so it never gets a Tab stop of its own — `objectName` and
        // the explicit `activeFocusOnTab: false` let a test pin that down the
        // way `tst_card_component.cpp`'s `railChevronsAreNotTabStops` does for
        // StrmRail's hover chevrons.
        Item {
            id: clearHit
            objectName: "filterPillClear"

            anchors.verticalCenter: parent.verticalCenter
            visible: pill.clearable && !pill.toggle
            activeFocusOnTab: false
            width: Theme.scale(18)
            height: width

            StrmIcon {
                anchors.centerIn: parent
                name: "close"
                size: Theme.scale(12)
                color: clearHover.hovered ? Theme.textPrimaryColor : pill.labelColor
            }

            HoverHandler {
                id: clearHover
                enabled: pill.enabled
                cursorShape: Qt.PointingHandCursor
            }

            TapHandler {
                enabled: pill.enabled
                gesturePolicy: TapHandler.ReleaseWithinBounds
                onTapped: pill.cleared()
            }
        }
    }

    FocusRing {
        objectName: "filterPillFocusRing"
        active: pill.activeFocus
        radius: Theme.radiusChip
    }

    HoverHandler {
        id: hover
        enabled: pill.enabled
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        id: tap
        enabled: pill.enabled
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: {
            pill.forceActiveFocus(Qt.MouseFocusReason);
            pill.activated();
        }
    }

    // Backspace is deliberately not bound: it is Back in the shell, and a
    // pill must not swallow it.
    Keys.onReturnPressed: event => {
        if (!event.isAutoRepeat)
            pill.activate();
    }
    Keys.onEnterPressed: event => {
        if (!event.isAutoRepeat)
            pill.activate();
    }
    Keys.onSpacePressed: event => {
        if (!event.isAutoRepeat)
            pill.activate();
    }
    Keys.onDeletePressed: event => {
        event.accepted = pill.clearable && !pill.toggle;
        if (event.accepted)
            pill.cleared();
    }
}
