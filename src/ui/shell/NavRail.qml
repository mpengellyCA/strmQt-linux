pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import StrmQt

// NavRail — the left destination rail (ARCHITECTURE.md).
//
// Icon-only at rest so it costs almost nothing, expanding to labels on hover or
// when anything inside it takes keyboard focus. The expansion overlays the page
// rather than displacing it: Main.qml reserves only `collapsedWidth`, so moving
// the pointer near the left edge never reflows the grid the user is reading.
//
// The rail decides nothing — it says what was chosen and Main.qml navigates.
//
// Destinations: Home · every library from HomeCtl.libraries · Favorites ·
// Search · Settings. `current` is the destination key of the page on screen:
// "home", "favorites", "search", "settings", or a library id.
Item {
    id: rail

    property string current: "home"

    readonly property int collapsedWidth: Theme.touchTarget + Theme.spacingValue
    readonly property int expandedWidth: Theme.scale(232)

    // Set by whichever entry holds focus; an entry destroyed while focused
    // clears this on its own, which a counter would not.
    property Item focusedEntry: null

    // Held open explicitly (the Menu button / M). Hover and focus are pointer
    // and keyboard affordances; a gamepad has neither, so it needs a way to ask.
    property bool pinned: false

    readonly property bool expanded: hover.hovered || rail.focusedEntry !== null || rail.pinned

    // Every focusable entry, in visual order. Repeater-created entries are
    // siblings in the Column, so children order is the order on screen; the
    // dividers and the Repeater itself are filtered out by activeFocusOnTab.
    function _entries(): var {
        const out = [];
        for (let i = 0; i < column.children.length; ++i) {
            const child = column.children[i];
            if (child !== null && child.activeFocusOnTab === true
                    && child.visible && child.enabled)
                out.push(child);
        }
        return out;
    }

    // Arrow / D-pad movement between entries. The rail had none: entries were
    // reachable only by Tab, so opening it with the Menu button left a gamepad
    // user looking at a menu they could not move through.
    function focusStep(from, step): bool {
        const entries = rail._entries();
        const index = entries.indexOf(from);
        if (index < 0)
            return false;
        const next = index + step;
        if (next < 0 || next >= entries.length)
            return false; // stop at the ends rather than wrapping
        entries[next].forceActiveFocus(Qt.TabFocusReason);
        return true;
    }

    // Move focus into the rail, onto the entry for the page already on screen so
    // the first D-pad press moves from where the user actually is.
    function focusCurrent(): void {
        for (let i = 0; i < column.children.length; ++i) {
            const child = column.children[i];
            if (child !== null && child.active === true && child.enabled) {
                child.forceActiveFocus(Qt.TabFocusReason);
                return;
            }
        }
        // Nothing on the rail matches the current page (a details page, say):
        // Home is the honest place to start.
        for (let i = 0; i < column.children.length; ++i) {
            const child = column.children[i];
            if (child !== null && child.activeFocusOnTab === true && child.enabled) {
                child.forceActiveFocus(Qt.TabFocusReason);
                return;
            }
        }
    }

    signal homeRequested
    signal librarySelected(string libraryId, string name, string collectionType)
    signal favoritesRequested
    signal playlistsRequested
    signal searchRequested
    signal settingsRequested
    // Closed without choosing anything; the page takes focus back.
    signal dismissed

    // Input.actions is the notifying property that keeps the tooltips' shortcut
    // hints live when a binding is changed.
    readonly property var inputRevision: Input.actions

    function shortcutFor(actionId) {
        return rail.inputRevision.length > 0 ? Input.binding(actionId) : "";
    }

    width: rail.expanded ? rail.expandedWidth : rail.collapsedWidth
    clip: true

    Behavior on width {
        NumberAnimation { duration: Theme.animNormalMs; easing.type: Theme.easeStandard }
    }

    HoverHandler {
        id: hover
        // Hover expands the rail. It never moves keyboard focus — that is the
        // one rule this whole component set is built around (ARCHITECTURE.md).
    }

    // ── One destination ────────────────────────────────────────────────────
    component NavEntry: Item {
        id: entry

        property string iconName: ""
        property string label: ""
        property string hint: ""
        property bool active: false

        signal chosen

        implicitWidth: rail.width
        implicitHeight: Theme.touchTarget
        activeFocusOnTab: true

        onActiveFocusChanged: {
            if (entry.activeFocus)
                rail.focusedEntry = entry;
            else if (rail.focusedEntry === entry)
                rail.focusedEntry = null;
        }

        readonly property color glyphColor: entry.active ? Theme.accentColor
                                          : entry.activeFocus || entryHover.hovered
                                            ? Theme.textPrimaryColor
                                          : Theme.textSecondaryColor

        Rectangle {
            id: entryBg

            anchors.fill: parent
            anchors.leftMargin: Theme.spacingTight / 2
            anchors.rightMargin: Theme.spacingTight / 2
            anchors.topMargin: Theme.scale(2)
            anchors.bottomMargin: Theme.scale(2)
            radius: Theme.radiusChip
            color: entry.active ? Theme.surfaceRaisedColor
                 : entryTap.pressed ? Theme.pressTint
                 : entryHover.hovered ? Theme.hoverTint
                 : "transparent"

            Behavior on color {
                ColorAnimation { duration: Theme.animInstant; easing.type: Theme.easeInstant }
            }

            // The active marker is a stripe, not a fill: it stays legible when
            // the rail is collapsed to icons.
            Rectangle {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.scale(3)
                height: parent.height * 0.55
                radius: width / 2
                color: Theme.accentColor
                opacity: entry.active ? 1.0 : 0.0
                visible: opacity > 0.01

                Behavior on opacity {
                    NumberAnimation { duration: Theme.animFastMs; easing.type: Theme.easeStandard }
                }
            }
        }

        StrmIcon {
            id: entryIcon

            x: Math.round((rail.collapsedWidth - width) / 2)
            anchors.verticalCenter: parent.verticalCenter
            name: entry.iconName
            color: entry.glyphColor
        }

        Text {
            anchors.left: entryIcon.right
            anchors.leftMargin: Theme.spacingValue
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingTight
            anchors.verticalCenter: parent.verticalCenter
            text: entry.label
            color: entry.glyphColor
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontBodySize
            font.weight: entry.active ? Font.DemiBold : Font.Normal
            elide: Text.ElideRight
            opacity: rail.expanded ? 1.0 : 0.0
            visible: opacity > 0.01

            Behavior on opacity {
                NumberAnimation { duration: Theme.animFastMs; easing.type: Theme.easeStandard }
            }
        }

        FocusRing {
            active: entry.activeFocus
            radius: entryBg.radius
            inset: Theme.scale(2)
        }

        // The label is hidden while collapsed, so the tooltip is the only thing
        // naming this destination for a pointer user.
        StrmTooltip {
            id: entryTip
            target: entry
            text: entry.label
            shortcut: entry.hint
        }

        HoverHandler {
            id: entryHover
            cursorShape: Qt.PointingHandCursor
            onHoveredChanged: {
                if (entryHover.hovered && !rail.expanded)
                    entryTip.requestShow();
                else
                    entryTip.requestHide();
            }
        }

        TapHandler {
            id: entryTap
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: {
                entryTip.requestHide();
                entry.forceActiveFocus(Qt.MouseFocusReason);
                rail.pinned = false;
                entry.chosen();
            }
        }

        function choose(event) {
            if (event.isAutoRepeat)
                return;
            rail.pinned = false;
            entry.chosen();
        }

        Keys.onReturnPressed: event => entry.choose(event)
        Keys.onEnterPressed: event => entry.choose(event)
        Keys.onSpacePressed: event => entry.choose(event)
        // Escape leaves the rail without navigating; without this a gamepad user
        // who opened it with Menu has no way out but to pick something.
        Keys.onEscapePressed: event => {
            if (rail.pinned) {
                rail.pinned = false;
                rail.dismissed();
                event.accepted = true;
            }
        }

        Keys.onUpPressed: event => { event.accepted = rail.focusStep(entry, -1); }
        Keys.onDownPressed: event => { event.accepted = rail.focusStep(entry, 1); }
        // The rail is on the left edge, so Right leaves it for the page and Left
        // closes it. Both only while pinned: while it is merely hovered or
        // tabbed into, horizontal arrows belong to whatever the user was doing.
        Keys.onRightPressed: event => {
            if (rail.pinned) {
                rail.pinned = false;
                rail.dismissed();
                event.accepted = true;
            }
        }
        Keys.onLeftPressed: event => {
            if (rail.pinned) {
                rail.pinned = false;
                rail.dismissed();
                event.accepted = true;
            }
        }
    }

    component NavDivider: Item {
        implicitWidth: rail.width
        implicitHeight: Theme.spacingValue

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: Theme.spacingTight
            anchors.rightMargin: Theme.spacingTight
            height: 1
            color: Theme.hairline
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.surfaceColor

        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 1
            color: Theme.hairline
        }
    }

    Flickable {
        id: flick

        anchors.fill: parent
        anchors.topMargin: Theme.spacingTight
        anchors.bottomMargin: Theme.spacingTight
        contentWidth: width
        contentHeight: column.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentHeight > height

        ScrollBar.vertical: StrmScrollBar {}

        Column {
            id: column

            width: flick.width

            NavEntry {
                iconName: "home"
                label: qsTr("Home")
                active: rail.current === "home"
                onChosen: rail.homeRequested()
            }

            NavDivider {}

            Repeater {
                model: HomeCtl.libraries

                NavEntry {
                    id: libraryEntry

                    required property string libraryId
                    required property string name
                    required property string collectionType

                    iconName: MediaKinds.libraryIcon(libraryEntry.collectionType)
                    label: libraryEntry.name
                    active: rail.current === libraryEntry.libraryId
                    onChosen: rail.librarySelected(libraryEntry.libraryId, libraryEntry.name,
                                                   libraryEntry.collectionType)
                }
            }

            NavDivider {}

            NavEntry {
                iconName: "playlist"
                label: qsTr("Playlists")
                active: rail.current === "playlists"
                onChosen: rail.playlistsRequested()
            }

            NavEntry {
                iconName: "heart"
                label: qsTr("Favorites")
                active: rail.current === "favorites"
                onChosen: rail.favoritesRequested()
            }

            NavEntry {
                iconName: "search"
                label: qsTr("Search")
                hint: rail.shortcutFor("library.search")
                active: rail.current === "search"
                onChosen: rail.searchRequested()
            }

            NavEntry {
                iconName: "settings"
                label: qsTr("Settings")
                hint: rail.shortcutFor("app.settings")
                active: rail.current === "settings"
                onChosen: rail.settingsRequested()
            }
        }
    }
}
