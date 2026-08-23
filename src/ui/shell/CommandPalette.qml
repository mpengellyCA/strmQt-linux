pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import StrmQt

// CommandPalette — Ctrl+K, one box that reaches everything (ARCHITECTURE.md).
//
// Three sources, always in the same order, because a palette that reorders its
// sections by score is a palette you can never use by muscle memory:
//
//   Libraries   from HomeCtl.libraries, matched locally — instant, no request
//   Items       from SearchCtl, whose `query` is debounced server-side search
//   Commands    from Input.actions, so the palette and the shortcut sheet list
//               the same verbs and neither can drift from what actually fires
//
// Only Application/Library commands are offered: playback verbs are meaningless
// with no player on screen, and Navigation verbs (Up/Down/Select) are the
// palette's own keys. Offering a command that cannot run is worse than omitting
// it.
//
// The palette decides nothing — it emits what was chosen and Main.qml navigates.
Item {
    id: palette

    property bool opened: false
    // How many results each source may contribute, so one source with hundreds
    // of matches cannot bury the other two.
    property int perSourceLimit: 6

    signal libraryChosen(string libraryId, string name, string collectionType)
    signal itemChosen(var item)
    signal actionChosen(string actionId)
    signal closed

    readonly property string query: field.text

    function open(): void {
        palette.opened = true;
        field.forceActiveFocus(Qt.OtherFocusReason);
        field.selectAll();
        palette.rebuild();
    }

    function close(): void {
        if (!palette.opened)
            return;
        palette.opened = false;
        palette.closed();
    }

    function toggle(): void {
        if (palette.opened)
            palette.close();
        else
            palette.open();
    }

    // ── Results ────────────────────────────────────────────────────────────
    // A plain array of { kind, label, sublabel, iconName, payload } records.
    // Rebuilt explicitly rather than as a binding: two of the three sources are
    // models whose contents change asynchronously, and an explicit rebuild on
    // each of those signals is easier to reason about than a chain of implicit
    // dependencies.
    property var results: []

    // Library mirror. LibraryListModel is a QAbstractListModel with no get(),
    // so the delegates below hand their roles over as plain values, indexed by
    // model row. Passing values out beats reading properties back off delegate
    // objects: the records stay ordinary JS objects, with no untyped QObject
    // property access anywhere in the search loop.
    property var libraryRecords: ({})
    property int libraryCount: 0

    function setLibrary(index, libraryId, name, collectionType): void {
        palette.libraryRecords[index] = {
            "libraryId": libraryId, "name": name, "collectionType": collectionType
        };
        if (palette.opened)
            palette.rebuild();
    }

    function matches(haystack, needle) {
        return needle.length === 0 || String(haystack).toLowerCase().indexOf(needle) >= 0;
    }

    // A command's own glyph. Everything shared one settings gear, so "Search"
    // and "Show / hide the menu" were indistinguishable at a glance.
    function commandIcon(actionId): string {
        switch (actionId) {
        case "library.search":      return "search";
        case "app.settings":        return "settings";
        case "app.fullscreen":      return "fullscreen";
        case "app.shortcuts":       return "info";
        case "app.commandPalette":  return "search";
        case "app.toggleMenu":      return "list";
        case "nav.nextTab":         return "chevron-right";
        case "nav.previousTab":     return "chevron-left";
        case "nav.pageDown":        return "chevron-down";
        case "nav.pageUp":          return "chevron-up";
        default:                    return "settings";
        }
    }

    function rebuild(): void {
        const needle = field.text.trim().toLowerCase();
        const out = [];

        for (let i = 0; i < palette.libraryCount && out.length < palette.perSourceLimit; ++i) {
            const lib = palette.libraryRecords[i];
            if (!lib || !palette.matches(lib.name, needle))
                continue;
            out.push({
                "kind": "library",
                "label": lib.name,
                "sublabel": qsTr("Library"),
                // Same glyph the nav rail shows for this library. A generic
                // icon here made the palette read as a different list of
                // things from the one in the rail.
                "iconName": MediaKinds.libraryIcon(lib.collectionType),
                "payload": { "libraryId": lib.libraryId, "name": lib.name,
                             "collectionType": lib.collectionType }
            });
        }

        // Items only once there is something to search for: an empty query
        // against the whole server is not a useful list.
        if (needle.length > 0 && SearchCtl.model) {
            const model = SearchCtl.model;
            const limit = Math.min(model.count, palette.perSourceLimit);
            for (let r = 0; r < limit; ++r) {
                const item = model.get(r);
                if (!item || !item.itemId)
                    continue;
                out.push({
                    "kind": "item",
                    "label": item.label && String(item.label).length > 0 ? item.label : item.name,
                    "sublabel": item.subtitle !== undefined && String(item.subtitle).length > 0
                                ? item.subtitle : item.type,
                    "iconName": "play",
                    "payload": item
                });
            }
        }

        const commands = Input.actions;
        let added = 0;
        for (let c = 0; c < commands.length && added < palette.perSourceLimit; ++c) {
            const action = commands[c];
            if (action.category !== "Application" && action.category !== "Library")
                continue;
            if (!palette.matches(action.name, needle))
                continue;
            out.push({
                "kind": "command",
                "label": action.name,
                "sublabel": action.sequences.join(" / "),
                "iconName": palette.commandIcon(action.actionId),
                "payload": action.actionId
            });
            ++added;
        }

        palette.results = out;
        list.currentIndex = out.length > 0 ? 0 : -1;
    }

    function activate(index): void {
        if (index < 0 || index >= palette.results.length)
            return;
        const record = palette.results[index];
        palette.close();
        if (record.kind === "library")
            palette.libraryChosen(record.payload.libraryId, record.payload.name,
                                  record.payload.collectionType);
        else if (record.kind === "item")
            palette.itemChosen(record.payload);
        else
            palette.actionChosen(record.payload);
    }

    visible: opacity > 0.01
    enabled: palette.opened
    opacity: palette.opened ? 1.0 : 0.0

    Behavior on opacity {
        NumberAnimation { duration: Theme.animFastMs; easing.type: Theme.easeStandard }
    }

    Item {
        id: libraryMirror

        visible: false

        Repeater {
            id: libraryRepeater

            model: HomeCtl.libraries
            onCountChanged: palette.libraryCount = libraryRepeater.count

            Item {
                id: libraryDelegate

                required property int index
                required property string libraryId
                required property string name
                required property string collectionType

                Component.onCompleted: palette.setLibrary(libraryDelegate.index,
                                                          libraryDelegate.libraryId,
                                                          libraryDelegate.name,
                                                          libraryDelegate.collectionType)
            }
        }
    }

    // Server-side results arrive later than the keystroke that asked for them.
    Connections {
        target: SearchCtl.model
        function onCountChanged() {
            if (palette.opened)
                palette.rebuild();
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.scrimColor

        TapHandler {
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: palette.close()
        }
    }

    Rectangle {
        id: surface

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Math.round(parent.height * 0.14)
        width: Math.min(parent.width - Theme.pageMarginValue * 2, Theme.scale(680))
        height: fieldRow.height + list.height + hint.height + Theme.spacingValue
        radius: Theme.radiusPanel
        color: Theme.surfaceOverlay
        border.width: 1
        border.color: Theme.hairline

        TapHandler { gesturePolicy: TapHandler.ReleaseWithinBounds }

        Item {
            id: fieldRow

            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: Theme.controlHeightLarge + Theme.spacingValue

            StrmSearchField {
                id: field

                anchors.fill: parent
                anchors.margins: Theme.spacingTight
                implicitHeight: Theme.controlHeightLarge
                placeholderText: qsTr("Jump to a library, an item, or a command…")

                onTextEdited: {
                    // The debounce lives in SearchController; setting the query
                    // on every keystroke is what it is designed for.
                    if (SearchCtl.query !== field.text)
                        SearchCtl.query = field.text;
                    palette.rebuild();
                }
                onCleared: {
                    SearchCtl.query = "";
                    palette.rebuild();
                }
                onEscapePressed: palette.close()
                onAccepted: palette.activate(list.currentIndex)

                // Arrow keys may auto-repeat — holding Down should walk the
                // list. Only activation is auto-repeat guarded.
                Keys.onUpPressed: {
                    if (list.count > 0)
                        list.currentIndex = (list.currentIndex - 1 + list.count) % list.count;
                }
                Keys.onDownPressed: {
                    if (list.count > 0)
                        list.currentIndex = (list.currentIndex + 1) % list.count;
                }
                Keys.onReturnPressed: event => {
                    if (!event.isAutoRepeat)
                        palette.activate(list.currentIndex);
                }
                Keys.onEnterPressed: event => {
                    if (!event.isAutoRepeat)
                        palette.activate(list.currentIndex);
                }
            }
        }

        ListView {
            id: list

            anchors.top: fieldRow.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: Theme.spacingTight
            anchors.rightMargin: Theme.spacingTight
            height: Math.min(contentHeight, Theme.scale(360))
            clip: true
            model: palette.results
            currentIndex: -1
            highlightMoveDuration: Theme.animFastMs
            boundsBehavior: Flickable.StopAtBounds
            keyNavigationEnabled: false

            ScrollBar.vertical: StrmScrollBar {}

            delegate: Item {
                id: row

                required property int index
                required property var modelData

                readonly property bool current: list.currentIndex === row.index

                width: list.width
                height: Theme.controlHeightLarge

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: Theme.scale(2)
                    radius: Theme.radiusChip
                    color: row.current ? Theme.hoverTint : "transparent"

                    Behavior on color {
                        ColorAnimation { duration: Theme.animInstant; easing.type: Theme.easeInstant }
                    }
                }

                StrmIcon {
                    id: rowIcon

                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacingValue
                    anchors.verticalCenter: parent.verticalCenter
                    name: row.modelData.iconName
                    color: row.current ? Theme.accentColor : Theme.textTertiary
                }

                Text {
                    anchors.left: rowIcon.right
                    anchors.leftMargin: Theme.spacingValue
                    anchors.right: sublabel.left
                    anchors.rightMargin: Theme.spacingValue
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.modelData.label
                    color: Theme.textPrimaryColor
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontBodySize
                    elide: Text.ElideRight
                }

                Text {
                    id: sublabel

                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingValue
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, row.width * 0.4)
                    horizontalAlignment: Text.AlignRight
                    text: row.modelData.sublabel !== undefined ? row.modelData.sublabel : ""
                    color: Theme.textTertiary
                    font.family: row.modelData.kind === "command" ? Theme.fontMono : Theme.fontBody
                    font.pixelSize: Theme.fontSmall
                    elide: Text.ElideRight
                }

                // Hover previews the row; it never commits and never takes the
                // keyboard's focus away from the field the user is typing in.
                HoverHandler {
                    cursorShape: Qt.PointingHandCursor
                    onHoveredChanged: if (hovered) list.currentIndex = row.index
                }

                TapHandler {
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: palette.activate(row.index)
                }
            }
        }

        // Never a bare "no results": the line always says what to do next
        // (ARCHITECTURE.md), and it reserves its own height so the panel does not
        // collapse around an invisible child.
        Text {
            id: hint

            anchors.top: list.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: Theme.spacingValue
            anchors.rightMargin: Theme.spacingValue
            height: visible ? Theme.controlHeightLarge : 0
            verticalAlignment: Text.AlignVCenter
            visible: palette.results.length === 0
            text: field.text.length === 0
                  ? qsTr("Type to search libraries, items and commands.")
                  : qsTr("Nothing matches “%1”. Try fewer words.").arg(field.text)
            color: Theme.textTertiary
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontSmall
            elide: Text.ElideRight
        }
    }
}
