pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import StrmQt

// Playlists (ARCHITECTURE.md): browse them, open one, play it, add to it, remove
// from it, reorder it.
//
// Master/detail rather than two pages, because every verb on the right needs
// the left: "move this track up" and "remove this entry" are edits, and an edit
// surface that makes you navigate away to see the result of the previous edit is
// the wrong shape. The two panes are also the two different *kinds* of list this
// feature has — one very long and read-mostly, one short and write-heavy — and
// they are built differently on purpose:
//
//   Left   1,564 playlists on this server. Never drawn all at once: a ListView
//          over an array of row indices, so only the visible delegates exist,
//          and a filter field narrows it because scrolling to "Workout" past
//          fifteen hundred neighbours is not browsing. The records behind it are
//          cached ONCE per model reload (plain JS objects), so typing filters a
//          string array instead of re-marshalling 500 QVariantMaps per keystroke.
//
//   Right  the open playlist's members, straight off PlaylistCtl.items — a
//          QAbstractItemModel, virtualised by the view itself.
//
// ── Entries, not items ─────────────────────────────────────────────────────
// Every write verb here addresses a PLAYLIST ENTRY (`playlistItemId`), never an
// item id. Emby gives each row its own entry id because the same track can
// legitimately appear twice in one playlist, so "remove that item" is ambiguous
// where "remove that entry" is not — on a 25-track album with a repeated track,
// using the item id would silently take out the wrong row. `entryIdAt()` is the
// only way this page names a row to the controller, and it refuses to act when
// the server sent no entry id rather than guessing.
//
// ── The server owns the order ──────────────────────────────────────────────
// moveEntry() refetches the whole list afterwards, so the rows are replaced
// under the cursor. Nothing here reorders locally and then hopes the two agree;
// instead the moved entry's id is remembered and the cursor is put back on that
// entry once the refetch lands. The list is authoritative, the selection follows.
//
// ── Navigation contract ────────────────────────────────────────────────────
// Like every other page: no navigation signals. Playback and "go to details" are
// requests to `Actions`, which Main.qml already listens to once.
FocusScope {
    id: page

    objectName: "playlistPage"

    // ── Model mirrors ──────────────────────────────────────────────────────
    // Plain JS records for the browse list: { row, id, name, lower, art }.
    // Rebuilt only when the playlist model itself changes.
    property var playlistRecords: []
    // Indices into playlistRecords that survive the filter; the ListView's model.
    property var filteredRows: []

    // The entry a move is in flight for, so the cursor can follow it across the
    // refetch that the move triggers.
    property string pendingEntryId: ""

    // PlaylistController raises `loading` for member fetches only — refresh()
    // has no flag of its own — so "the playlist list has not arrived yet" is
    // derived here. Without it the page opens on "No playlists yet", which is a
    // wrong answer shown confidently for as long as the request takes. The guard
    // timer lowers it for the one case that emits nothing at all: a refresh that
    // returns the same (zero) count it started with.
    property bool playlistsPending: true

    readonly property int playlistCount: PlaylistCtl.playlists.count
    // The server has more playlists than one page of the query returns
    // (PlaylistController caps at 500). Saying so is better than quietly
    // pretending the tail does not exist.
    readonly property int playlistTotal: PlaylistCtl.playlists.totalRecordCount
    readonly property bool playlistsTruncated: page.playlistTotal > page.playlistCount

    readonly property int memberCount: PlaylistCtl.items.count
    readonly property bool hasOpenPlaylist: PlaylistCtl.currentId.length > 0
    readonly property bool failed: PlaylistCtl.errorMessage.length > 0

    readonly property int listPaneWidth: Math.max(Theme.scale(280),
                                                  Math.min(Theme.scale(360), page.width * 0.3))

    // ── Records and filtering ──────────────────────────────────────────────
    function rebuildRecords(): void {
        const model = PlaylistCtl.playlists;
        const out = [];
        for (let i = 0; i < model.count; ++i) {
            const entry = model.get(i);
            const name = entry.name !== undefined ? String(entry.name) : "";
            out.push({
                "row": i,
                "id": entry.itemId !== undefined ? String(entry.itemId) : "",
                "name": name,
                "lower": name.toLowerCase(),
                "art": entry.posterUrl !== undefined ? String(entry.posterUrl) : ""
            });
        }
        page.playlistRecords = out;
        page.rebuildFilter();
    }

    function rebuildFilter(): void {
        const needle = filterField.text.trim().toLowerCase();
        const source = page.playlistRecords;
        const out = [];
        for (let i = 0; i < source.length; ++i) {
            if (needle.length === 0 || source[i].lower.indexOf(needle) >= 0)
                out.push(i);
        }
        page.filteredRows = out;
        browseList.currentIndex = out.length > 0 ? 0 : -1;
    }

    function recordAt(filteredIndex) {
        if (filteredIndex < 0 || filteredIndex >= page.filteredRows.length)
            return null;
        return page.playlistRecords[page.filteredRows[filteredIndex]];
    }

    function openFiltered(filteredIndex): void {
        const record = page.recordAt(filteredIndex);
        if (!record || record.id.length === 0)
            return;
        browseList.currentIndex = filteredIndex;
        if (record.id === PlaylistCtl.currentId)
            return;
        PlaylistCtl.open(record.id, record.name);
    }

    // ── Members ────────────────────────────────────────────────────────────
    function memberAt(row) {
        const model = PlaylistCtl.items;
        if (!model || row < 0 || row >= model.count)
            return null;
        return model.get(row);
    }

    // The ONE place a row is named to the controller. Entry id, never item id.
    function entryIdAt(row): string {
        const item = page.memberAt(row);
        if (!item || item.playlistItemId === undefined)
            return "";
        return String(item.playlistItemId);
    }

    // Every loaded member, in order, as the queue. Built here rather than
    // fetched again: the page already holds exactly the rows the user is
    // looking at, in exactly the order the server said, and a second query
    // would have to re-derive an order that this list already is.
    function memberItems() {
        const model = PlaylistCtl.items;
        const out = [];
        for (let i = 0; i < model.count; ++i)
            out.push(model.get(i));
        return out;
    }

    function playFrom(row): void {
        const items = page.memberItems();
        if (items.length === 0)
            return;
        Actions.playAllFrom(items, Math.max(0, Math.min(items.length - 1, row)));
    }

    // Shuffled locally rather than through Actions.shuffle(): that verb asks the
    // server for SortBy=Random under a parent id, and its item-type filter for
    // an unknown collection kind is {Movie, Episode, Video} — which returns
    // nothing at all for a music playlist, and most playlists on this server are
    // music. The playlist IS the queue; shuffling the rows we have is both
    // correct for every media type and exactly what the user can see.
    function shuffleAll(): void {
        const items = page.memberItems();
        if (items.length === 0)
            return;
        for (let i = items.length - 1; i > 0; --i) {
            const j = Math.floor(Math.random() * (i + 1));
            const swap = items[i];
            items[i] = items[j];
            items[j] = swap;
        }
        Actions.playAllFrom(items, 0);
    }

    function removeRow(row): void {
        const entry = page.entryIdAt(row);
        if (entry.length === 0) {
            toasts.show(qsTr("The server did not give this row an entry id, so it cannot be removed."),
                        "error");
            return;
        }
        PlaylistCtl.removeEntries([entry]);
    }

    function moveRow(row, delta): void {
        const target = row + delta;
        if (row < 0 || row >= page.memberCount || target < 0 || target >= page.memberCount)
            return;
        const entry = page.entryIdAt(row);
        if (entry.length === 0) {
            toasts.show(qsTr("The server did not give this row an entry id, so it cannot be moved."),
                        "error");
            return;
        }
        // Remembered BEFORE the call: the refetch it triggers replaces every row.
        page.pendingEntryId = entry;
        PlaylistCtl.moveEntry(entry, target);
    }

    // Called once the refetch settles. Matching by entry id rather than by index
    // is the whole point — the index is exactly the thing that just changed.
    function restoreCursor(): void {
        const want = page.pendingEntryId;
        page.pendingEntryId = "";
        if (want.length === 0)
            return;
        const model = PlaylistCtl.items;
        for (let i = 0; i < model.count; ++i) {
            const item = model.get(i);
            if (item.playlistItemId !== undefined && String(item.playlistItemId) === want) {
                memberList.currentIndex = i;
                memberList.positionViewAtIndex(i, ListView.Contain);
                return;
            }
        }
    }

    function showMemberMenu(row, sceneX, sceneY): void {
        const item = page.memberAt(row);
        if (!item)
            return;
        memberList.currentIndex = row;
        itemMenu.popupForItem(item, sceneX, sceneY);
    }

    // ── Formatting ─────────────────────────────────────────────────────────
    function formatRuntime(ms): string {
        if (!ms || ms <= 0)
            return "";
        const totalSeconds = Math.round(ms / 1000);
        const hours = Math.floor(totalSeconds / 3600);
        const minutes = Math.floor((totalSeconds / 60) % 60);
        const seconds = totalSeconds % 60;
        function pad(value) { return (value < 10 ? "0" : "") + value; }
        return hours > 0 ? hours + ":" + pad(minutes) + ":" + pad(seconds)
                         : minutes + ":" + pad(seconds);
    }

    // ── Lifecycle ──────────────────────────────────────────────────────────
    Component.onCompleted: {
        if (PlaylistCtl.playlists.count === 0) {
            page.playlistsPending = true;
            PlaylistCtl.refresh();
            refreshGuard.restart();
        } else {
            page.playlistsPending = false;
            page.rebuildRecords();
        }
    }

    Timer {
        id: refreshGuard
        interval: 8000
        onTriggered: page.playlistsPending = false
    }

    Connections {
        target: PlaylistCtl.playlists

        function onCountChanged() {
            page.playlistsPending = false;
            refreshGuard.stop();
            page.rebuildRecords();
        }
    }

    // The only signal that says "the member list has been replaced". A move
    // raises it and lowers it again; the cursor follows the entry across that.
    Connections {
        target: PlaylistCtl

        function onLoadingChanged() {
            if (!PlaylistCtl.loading && page.pendingEntryId.length > 0)
                Qt.callLater(page.restoreCursor);
        }
        // A refresh that failed emits no count change, so this is the other way
        // the "still fetching" flag comes down.
        function onErrorChanged() {
            if (PlaylistCtl.errorMessage.length > 0) {
                page.playlistsPending = false;
                refreshGuard.stop();
            }
        }
        function onActionSucceeded(message) { toasts.show(message, "success"); }
        function onActionFailed(message) { toasts.show(message, "error"); }
    }

    // ── Header ─────────────────────────────────────────────────────────────
    PageHeader {
        id: header

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: Theme.pageMarginValue
        anchors.rightMargin: Theme.pageMarginValue
        anchors.topMargin: Theme.spacingValue
        height: header.implicitHeight

        title: qsTr("Playlists")
        subtitle: page.playlistsTruncated
                  ? qsTr("%1 of %2 — the server returns the first %1 by name")
                    .arg(page.playlistCount).arg(page.playlistTotal)
                  : (page.playlistCount > 0 ? qsTr("%1 playlists").arg(page.playlistCount) : "")

        StrmButton {
            id: newButton

            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("New playlist")
            iconName: "plus"
            onClicked: createPrompt.show()

            KeyNavigation.down: filterField
        }
    }

    // ── Browse pane ────────────────────────────────────────────────────────
    Item {
        id: browsePane

        anchors.left: parent.left
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.pageMarginValue
        anchors.topMargin: Theme.spacingValue
        anchors.bottomMargin: Theme.spacingValue
        width: page.listPaneWidth

        StrmSearchField {
            id: filterField

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            placeholderText: qsTr("Filter playlists…")

            onTextEdited: page.rebuildFilter()
            onCleared: page.rebuildFilter()
            onEscapePressed: {
                if (filterField.text.length > 0) {
                    filterField.clear();
                    page.rebuildFilter();
                } else {
                    browseList.forceActiveFocus(Qt.OtherFocusReason);
                }
            }
            onAccepted: {
                page.openFiltered(browseList.currentIndex);
                browseList.forceActiveFocus(Qt.OtherFocusReason);
            }

            // Down hands the keyboard to the list rather than driving the
            // list's cursor from here: unlike the command palette, this list is
            // a real destination with verbs of its own, and a filter you can
            // never leave is a trap.
            KeyNavigation.up: newButton
            KeyNavigation.down: browseList
        }

        // Never every playlist at once: this is a view over an index array, so
        // 1,564 names cost 1,564 small JS records and a dozen live delegates.
        ListView {
            id: browseList

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: filterField.bottom
            anchors.bottom: parent.bottom
            anchors.topMargin: Theme.spacingTight
            clip: true
            // The page's way in, and only while there is something to land on:
            // an empty view cannot hold focus and would hand the page back with
            // nothing focused.
            focus: browseList.count > 0
            model: page.filteredRows
            currentIndex: -1
            keyNavigationWraps: false
            highlightMoveDuration: Theme.animFastMs
            boundsBehavior: Flickable.StopAtBounds
            cacheBuffer: Theme.controlHeightLarge * 6

            ScrollBar.vertical: StrmScrollBar {}

            KeyNavigation.up: filterField
            KeyNavigation.right: page.hasOpenPlaylist ? memberList : null

            function pageStep() {
                return Math.max(1, Math.floor(browseList.height
                                              / Math.max(1, Theme.controlHeightLarge)));
            }

            Keys.onReturnPressed: event => {
                if (!event.isAutoRepeat)
                    page.openFiltered(browseList.currentIndex);
            }
            Keys.onEnterPressed: event => {
                if (!event.isAutoRepeat)
                    page.openFiltered(browseList.currentIndex);
            }

            Keys.onPressed: event => {
                if (browseList.count === 0)
                    return;
                if (event.key === Qt.Key_PageDown) {
                    browseList.currentIndex = Math.min(browseList.count - 1,
                                                       browseList.currentIndex
                                                       + browseList.pageStep());
                    event.accepted = true;
                } else if (event.key === Qt.Key_PageUp) {
                    browseList.currentIndex = Math.max(0, browseList.currentIndex
                                                       - browseList.pageStep());
                    event.accepted = true;
                } else if (event.key === Qt.Key_Home) {
                    browseList.currentIndex = 0;
                    event.accepted = true;
                } else if (event.key === Qt.Key_End) {
                    browseList.currentIndex = browseList.count - 1;
                    event.accepted = true;
                }
            }

            delegate: Item {
                id: browseRow

                required property int index
                required property var modelData

                readonly property var record: page.playlistRecords[browseRow.modelData]
                readonly property bool current: browseRow.ListView.isCurrentItem
                                                && browseList.activeFocus
                readonly property bool opened: browseRow.record !== undefined
                                               && browseRow.record !== null
                                               && browseRow.record.id === PlaylistCtl.currentId
                readonly property bool hovered: browseHover.hovered

                width: browseList.width
                height: Theme.controlHeightLarge

                Rectangle {
                    anchors.fill: parent
                    anchors.rightMargin: Theme.spacingTight
                    anchors.topMargin: Theme.scale(2)
                    anchors.bottomMargin: Theme.scale(2)
                    radius: Theme.radiusChip
                    color: browseRow.opened ? Theme.surfaceRaisedColor
                         : browseRow.hovered ? Theme.hoverTint
                         : "transparent"

                    Behavior on color {
                        ColorAnimation { duration: Theme.animInstant; easing.type: Theme.easeInstant }
                    }
                }

                // The open playlist gets a spine, not a ring: the amber 3 px
                // ring means "the keyboard is here" and nothing else borrows it.
                Rectangle {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.scale(3)
                    height: parent.height - Theme.spacingTight
                    radius: width / 2
                    visible: browseRow.opened
                    color: Theme.accentColor
                }

                StrmIcon {
                    id: browseGlyph

                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacingValue
                    anchors.verticalCenter: parent.verticalCenter
                    name: "playlist"
                    color: browseRow.opened ? Theme.accentColor
                         : (browseRow.current || browseRow.hovered) ? Theme.textPrimaryColor
                         : Theme.textTertiary
                }

                Text {
                    anchors.left: browseGlyph.right
                    anchors.leftMargin: Theme.spacingTight
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingValue
                    anchors.verticalCenter: parent.verticalCenter
                    text: browseRow.record ? browseRow.record.name : ""
                    color: (browseRow.current || browseRow.hovered || browseRow.opened)
                           ? Theme.textPrimaryColor : Theme.textSecondaryColor
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontBodySize
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }

                FocusRing {
                    active: browseRow.current
                    anchors.fill: parent
                    anchors.rightMargin: Theme.spacingTight
                    radius: Theme.radiusChip
                    inset: -Theme.scale(1)
                }

                // Hover lights the row; it never moves the keyboard's place.
                HoverHandler {
                    id: browseHover
                    cursorShape: Qt.PointingHandCursor
                }

                // A click is a deliberate act, so it commits AND becomes the
                // keyboard's place.
                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: {
                        browseList.forceActiveFocus(Qt.MouseFocusReason);
                        page.openFiltered(browseRow.index);
                    }
                }
            }
        }

        // The shape of the answer while it is on its way, rather than a blank
        // pane that then pops (ARCHITECTURE.md).
        LoadingState {
            anchors.top: filterField.bottom
            anchors.topMargin: Theme.spacingTight
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            shape: "list"
            active: page.playlistsPending && page.playlistRecords.length === 0
            margins: 0
        }

        // Filtered to nothing is a dead end unless the way out is on screen.
        EmptyState {
            anchors.top: filterField.bottom
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            visible: browseList.count === 0 && page.playlistRecords.length > 0
            iconName: "filter"
            headline: qsTr("No playlist matches")
            body: qsTr("Nothing here is called “%1”.").arg(filterField.text)
            actionText: qsTr("Clear filter")
            actionIcon: "close"
            onActionTriggered: {
                filterField.clear();
                page.rebuildFilter();
            }
        }
    }

    Rectangle {
        id: divider

        anchors.left: browsePane.right
        anchors.leftMargin: Theme.spacingValue
        anchors.top: browsePane.top
        anchors.bottom: browsePane.bottom
        width: 1
        color: Theme.hairline
    }

    // ── Member pane ────────────────────────────────────────────────────────
    Item {
        id: memberPane

        anchors.left: divider.right
        anchors.leftMargin: Theme.spacingValue
        anchors.right: parent.right
        anchors.rightMargin: Theme.pageMarginValue
        anchors.top: browsePane.top
        anchors.bottom: browsePane.bottom
        visible: page.hasOpenPlaylist

        Item {
            id: memberHeader

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: Theme.controlHeight

            Column {
                anchors.left: parent.left
                anchors.right: memberVerbs.left
                anchors.rightMargin: Theme.spacingValue
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.scale(2)

                Text {
                    width: parent.width
                    text: PlaylistCtl.currentName
                    color: Theme.textPrimaryColor
                    font.family: Theme.fontDisplay
                    font.pixelSize: Theme.fontTitle
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }

                Text {
                    width: parent.width
                    text: page.memberCount > 0 ? qsTr("%1 items").arg(page.memberCount) : ""
                    color: Theme.textTertiary
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontCaption
                }
            }

            Row {
                id: memberVerbs

                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingTight

                StrmButton {
                    id: playAllButton
                    text: qsTr("Play all")
                    iconName: "play"
                    variant: "primary"
                    enabled: page.memberCount > 0
                    onClicked: page.playFrom(0)

                    KeyNavigation.left: browseList
                    KeyNavigation.right: shuffleButton
                    KeyNavigation.down: memberList
                }

                StrmButton {
                    id: shuffleButton
                    text: qsTr("Shuffle")
                    iconName: "shuffle"
                    enabled: page.memberCount > 0
                    onClicked: page.shuffleAll()

                    KeyNavigation.left: playAllButton
                    KeyNavigation.right: reloadButton
                    KeyNavigation.down: memberList
                }

                StrmIconButton {
                    id: reloadButton
                    iconName: "refresh"
                    tooltip: qsTr("Reload from the server")
                    onClicked: PlaylistCtl.reload()

                    KeyNavigation.left: shuffleButton
                    KeyNavigation.right: renameButton
                    KeyNavigation.down: memberList
                }

                StrmIconButton {
                    id: renameButton
                    iconName: "edit"
                    tooltip: qsTr("Rename this playlist")
                    onClicked: {
                        renameSheet.seed = PlaylistCtl.currentName;
                        renameSheet.open();
                    }

                    KeyNavigation.left: reloadButton
                    KeyNavigation.right: deleteButton
                    KeyNavigation.down: memberList
                }

                StrmIconButton {
                    id: deleteButton
                    iconName: "trash"
                    tooltip: qsTr("Delete this playlist")
                    // Deleting is irreversible on the server, so it asks. The
                    // controller deliberately does not: confirmation belongs
                    // where the user is, not in a verb.
                    onClicked: confirmDelete.open()

                    KeyNavigation.left: renameButton
                    KeyNavigation.down: memberList
                }
            }
        }

        ListView {
            id: memberList

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: memberHeader.bottom
            anchors.bottom: parent.bottom
            anchors.topMargin: Theme.spacingValue
            clip: true
            model: PlaylistCtl.items
            currentIndex: 0
            keyNavigationWraps: false
            highlightMoveDuration: Theme.animFastMs
            boundsBehavior: Flickable.StopAtBounds
            cacheBuffer: Theme.scale(64) * 6
            // Opacity, not visibility: an invisible view drops active focus and
            // never gets it back, so a reload would eject the keyboard from the
            // page on every move.
            opacity: PlaylistCtl.loading ? 0.0 : 1.0

            Behavior on opacity {
                NumberAnimation { duration: Theme.animFastMs; easing.type: Theme.easeStandard }
            }

            ScrollBar.vertical: StrmScrollBar {}

            KeyNavigation.left: browseList
            KeyNavigation.up: playAllButton

            function pageStep() {
                return Math.max(1, Math.floor(memberList.height / Math.max(1, Theme.scale(64))));
            }

            Keys.onReturnPressed: event => {
                if (!event.isAutoRepeat)
                    page.playFrom(memberList.currentIndex);
            }
            Keys.onEnterPressed: event => {
                if (!event.isAutoRepeat)
                    page.playFrom(memberList.currentIndex);
            }

            // The edit verbs on the keyboard. Alt+Up/Alt+Down rather than plain
            // arrows, which have to keep meaning "move the cursor"; Delete is
            // the platform's remove key and is auto-repeat guarded so a held key
            // cannot walk down the playlist deleting it.
            Keys.onPressed: event => {
                if (memberList.count === 0)
                    return;
                const row = memberList.currentIndex;
                if (event.modifiers & Qt.AltModifier) {
                    if (event.key === Qt.Key_Up && !event.isAutoRepeat) {
                        page.moveRow(row, -1);
                        event.accepted = true;
                    } else if (event.key === Qt.Key_Down && !event.isAutoRepeat) {
                        page.moveRow(row, 1);
                        event.accepted = true;
                    }
                    return;
                }
                if (event.key === Qt.Key_Delete && !event.isAutoRepeat) {
                    page.removeRow(row);
                    event.accepted = true;
                } else if (event.key === Qt.Key_Menu && !event.isAutoRepeat) {
                    const item = memberList.currentItem;
                    if (item) {
                        const p = item.mapToItem(null, Theme.spacingValue, item.height);
                        page.showMemberMenu(row, p.x, p.y);
                    }
                    event.accepted = true;
                } else if (event.key === Qt.Key_PageDown) {
                    memberList.currentIndex = Math.min(memberList.count - 1,
                                                       row + memberList.pageStep());
                    event.accepted = true;
                } else if (event.key === Qt.Key_PageUp) {
                    memberList.currentIndex = Math.max(0, row - memberList.pageStep());
                    event.accepted = true;
                } else if (event.key === Qt.Key_Home) {
                    memberList.currentIndex = 0;
                    event.accepted = true;
                } else if (event.key === Qt.Key_End) {
                    memberList.currentIndex = memberList.count - 1;
                    event.accepted = true;
                }
            }

            delegate: Item {
                id: memberRow

                required property int index
                required property var model

                readonly property bool current: memberRow.ListView.isCurrentItem
                                                && memberList.activeFocus
                readonly property bool hovered: memberHover.hovered
                // Both may be true at once, and the actions appear for either:
                // the pointer needs them under the cursor, the keyboard needs
                // them on the row it is standing on.
                readonly property bool showActions: memberRow.hovered || memberRow.current
                readonly property string art: {
                    const poster = memberRow.model.posterUrl !== undefined
                                 ? String(memberRow.model.posterUrl) : "";
                    if (poster.length > 0)
                        return poster;
                    return memberRow.model.thumbUrl !== undefined
                           ? String(memberRow.model.thumbUrl) : "";
                }
                readonly property string title: {
                    const label = memberRow.model.label !== undefined
                                ? String(memberRow.model.label) : "";
                    if (label.length > 0)
                        return label;
                    return memberRow.model.name !== undefined ? String(memberRow.model.name) : "";
                }

                width: memberList.width
                height: Theme.scale(64)

                Rectangle {
                    anchors.fill: parent
                    anchors.topMargin: Theme.scale(2)
                    anchors.bottomMargin: Theme.scale(2)
                    anchors.rightMargin: Theme.spacingTight
                    radius: Theme.radiusChip
                    color: memberRow.hovered ? Theme.hoverTint : "transparent"

                    Behavior on color {
                        ColorAnimation { duration: Theme.animInstant; easing.type: Theme.easeInstant }
                    }
                }

                // Tabular, so the column does not jitter between 9 and 10.
                Text {
                    id: ordinal

                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacingTight
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.scale(34)
                    horizontalAlignment: Text.AlignRight
                    text: memberRow.index + 1
                    color: memberRow.current ? Theme.accentColor : Theme.textTertiary
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSmall
                }

                Rectangle {
                    id: artFrame

                    anchors.left: ordinal.right
                    anchors.leftMargin: Theme.spacingValue
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.scale(44)
                    height: Theme.scale(44)
                    radius: Theme.radiusChip
                    color: Theme.surfaceColor
                    clip: true

                    Image {
                        anchors.fill: parent
                        source: memberRow.art
                        sourceSize.width: Theme.scale(44)
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        cache: true
                        opacity: status === Image.Ready ? 1 : 0

                        Behavior on opacity {
                            NumberAnimation {
                                duration: Theme.animNormalMs
                                easing.type: Theme.easeStandard
                            }
                        }
                    }

                    // Played state, kept off the label so a long title never
                    // pushes it out of sight.
                    Rectangle {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        width: Theme.scale(16)
                        height: Theme.scale(16)
                        radius: height / 2
                        visible: memberRow.model.played === true
                        color: Theme.positive

                        StrmIcon {
                            anchors.centerIn: parent
                            name: "check"
                            size: Theme.scale(11)
                            color: Theme.accentText
                        }
                    }
                }

                Column {
                    id: memberLabels

                    anchors.left: artFrame.right
                    anchors.leftMargin: Theme.spacingValue
                    anchors.right: rowActions.left
                    anchors.rightMargin: Theme.spacingValue
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.scale(2)

                    Text {
                        width: parent.width
                        text: memberRow.title
                        color: (memberRow.current || memberRow.hovered)
                               ? Theme.textPrimaryColor : Theme.textSecondaryColor
                        font.family: Theme.fontBody
                        font.pixelSize: Theme.fontBodySize
                        elide: Text.ElideRight
                        maximumLineCount: 1
                    }

                    Text {
                        width: parent.width
                        visible: text.length > 0
                        text: {
                            const parts = [];
                            const sub = memberRow.model.subtitle !== undefined
                                      ? String(memberRow.model.subtitle) : "";
                            if (sub.length > 0)
                                parts.push(sub);
                            const runtime = page.formatRuntime(memberRow.model.runtimeMs);
                            if (runtime.length > 0)
                                parts.push(runtime);
                            return parts.join("  ·  ");
                        }
                        color: Theme.textTertiary
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontCaption
                        elide: Text.ElideRight
                        maximumLineCount: 1
                    }
                }

                // The edit verbs, for the pointer. `activeFocusOnTab: false`
                // throughout: the list is one tab stop and owns the arrow keys,
                // and five focusable buttons per row would make Tab walk the
                // playlist instead of leaving it. The keyboard reaches all of
                // these through the list's own key handler.
                Row {
                    id: rowActions

                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingValue
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.scale(2)
                    opacity: memberRow.showActions ? 1 : 0
                    visible: opacity > 0.01
                    enabled: visible

                    Behavior on opacity {
                        NumberAnimation { duration: Theme.animInstant; easing.type: Theme.easeInstant }
                    }

                    StrmIconButton {
                        iconName: "play"
                        round: true
                        size: Theme.scale(30)
                        activeFocusOnTab: false
                        tooltip: qsTr("Play from here")
                        onClicked: page.playFrom(memberRow.index)
                    }

                    StrmIconButton {
                        iconName: "chevron-up"
                        round: true
                        size: Theme.scale(30)
                        activeFocusOnTab: false
                        enabled: memberRow.index > 0
                        tooltip: qsTr("Move up")
                        shortcut: "Alt+Up"
                        onClicked: page.moveRow(memberRow.index, -1)
                    }

                    StrmIconButton {
                        iconName: "chevron-down"
                        round: true
                        size: Theme.scale(30)
                        activeFocusOnTab: false
                        enabled: memberRow.index < page.memberCount - 1
                        tooltip: qsTr("Move down")
                        shortcut: "Alt+Down"
                        onClicked: page.moveRow(memberRow.index, 1)
                    }

                    StrmIconButton {
                        iconName: "trash"
                        round: true
                        size: Theme.scale(30)
                        activeFocusOnTab: false
                        tooltip: qsTr("Remove from this playlist")
                        shortcut: "Del"
                        onClicked: page.removeRow(memberRow.index)
                    }

                    StrmIconButton {
                        id: memberMore
                        iconName: "more-horizontal"
                        round: true
                        size: Theme.scale(30)
                        activeFocusOnTab: false
                        tooltip: qsTr("More…")
                        onClicked: {
                            const p = memberMore.mapToItem(null, 0, memberMore.height);
                            page.showMemberMenu(memberRow.index, p.x, p.y);
                        }
                    }
                }

                FocusRing {
                    active: memberRow.current
                    anchors.fill: parent
                    anchors.rightMargin: Theme.spacingTight
                    radius: Theme.radiusChip
                    inset: -Theme.scale(1)
                }

                HoverHandler {
                    id: memberHover
                    cursorShape: Qt.PointingHandCursor
                }

                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: {
                        memberList.currentIndex = memberRow.index;
                        memberList.forceActiveFocus(Qt.MouseFocusReason);
                        page.playFrom(memberRow.index);
                    }
                }

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: eventPoint => {
                        const p = memberRow.mapToItem(null, eventPoint.position.x,
                                                      eventPoint.position.y);
                        page.showMemberMenu(memberRow.index, p.x, p.y);
                    }
                }
            }
        }

        // Swallows clicks aimed at the rows that are being replaced underneath.
        MouseArea {
            anchors.fill: memberList
            visible: PlaylistCtl.loading
            acceptedButtons: Qt.AllButtons
        }

        LoadingState {
            anchors.fill: memberList
            shape: "list"
            active: PlaylistCtl.loading
            margins: 0
        }

        EmptyState {
            anchors.fill: memberList
            visible: !PlaylistCtl.loading && page.memberCount === 0 && !page.failed
            iconName: "playlist"
            headline: qsTr("This playlist is empty")
            body: qsTr("Open anything in your library and use “Add to playlist” to put it here.")
        }

        EmptyState {
            anchors.fill: memberList
            visible: !PlaylistCtl.loading && page.memberCount === 0 && page.failed
            severity: "error"
            iconName: "info"
            headline: qsTr("Couldn't load this playlist")
            body: PlaylistCtl.errorMessage
            actionText: qsTr("Retry")
            actionIcon: "refresh"
            onActionTriggered: PlaylistCtl.reload()
        }
    }

    // Nothing opened yet: the right-hand pane says what to do rather than
    // sitting empty (ARCHITECTURE.md).
    EmptyState {
        anchors.fill: memberPane
        visible: !page.hasOpenPlaylist && page.playlistRecords.length > 0
        iconName: "playlist"
        headline: qsTr("Pick a playlist")
        body: qsTr("Choose one on the left to see what is in it, play it, or rearrange it.")
    }

    // No playlists at all — or the fetch failed, which is a different sentence
    // and gets a different one.
    EmptyState {
        anchors.centerIn: parent
        width: Theme.scale(420)
        visible: page.playlistRecords.length === 0 && !page.playlistsPending
        severity: page.failed ? "error" : "info"
        iconName: page.failed ? "info" : "playlist"
        headline: page.failed ? qsTr("Couldn't load your playlists")
                              : qsTr("No playlists yet")
        body: page.failed ? PlaylistCtl.errorMessage
                          : qsTr("A playlist is any set of items you want to keep together, "
                                 + "in an order you choose.")
        actionText: page.failed ? qsTr("Retry") : qsTr("New playlist")
        actionIcon: page.failed ? "refresh" : "plus"
        onActionTriggered: {
            if (page.failed)
                PlaylistCtl.refresh();
            else
                createPrompt.show();
        }
    }

    // ── Create ─────────────────────────────────────────────────────────────
    // An overlay rather than a row that appears in the list: naming a thing is a
    // modal act, and a half-typed name sitting in a browse list would be a
    // playlist that does not exist yet pretending to be one that does.
    Item {
        id: createPrompt

        anchors.fill: parent
        z: 100
        // `opened` as well as the animated opacity: show() focuses the name
        // field in the same call, before the fade has ticked, and an item that
        // is still invisible then does not take focus.
        visible: createPrompt.opened || createPrompt.opacity > 0.01
        enabled: createPrompt.opened

        property bool opened: false

        function show(): void {
            nameField.text = "";
            createPrompt.opened = true;
            nameField.forceActiveFocus(Qt.OtherFocusReason);
        }

        function dismiss(): void {
            if (!createPrompt.opened)
                return;
            createPrompt.opened = false;
            browseList.forceActiveFocus(Qt.OtherFocusReason);
        }

        function commit(): void {
            const name = nameField.text.trim();
            if (name.length === 0)
                return;
            // Empty on purpose: an empty playlist is a legitimate thing to make,
            // and the controller says so explicitly.
            PlaylistCtl.create(name, []);
            createPrompt.dismiss();
        }

        opacity: createPrompt.opened ? 1.0 : 0.0

        Behavior on opacity {
            NumberAnimation { duration: Theme.animFastMs; easing.type: Theme.easeStandard }
        }

        Rectangle {
            anchors.fill: parent
            color: Theme.scrimColor

            TapHandler {
                gesturePolicy: TapHandler.ReleaseWithinBounds
                onTapped: createPrompt.dismiss()
            }
        }

        StrmPanel {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: Math.round(parent.height * 0.18)
            width: Math.min(parent.width - Theme.pageMarginValue * 2, Theme.scale(480))
            elevation: 4
            padding: Theme.spacingLoose
            title: qsTr("New playlist")
            subtitle: qsTr("It starts empty; add items to it from anywhere in the app.")

            // Keeps clicks off the scrim behind it.
            TapHandler { gesturePolicy: TapHandler.ReleaseWithinBounds }

            NameField {
                id: nameField

                width: parent.width
                label: qsTr("Name")
                placeholder: qsTr("Saturday night")
                onAccepted: createPrompt.commit()
                onCancelled: createPrompt.dismiss()

                KeyNavigation.down: createButton
            }

            Item { width: 1; height: Theme.spacingTight }

            Row {
                spacing: Theme.spacingTight

                StrmButton {
                    id: createButton
                    text: qsTr("Create")
                    iconName: "plus"
                    variant: "primary"
                    enabled: nameField.text.trim().length > 0
                    onClicked: createPrompt.commit()

                    KeyNavigation.up: nameField
                    KeyNavigation.right: cancelButton
                }

                StrmButton {
                    id: cancelButton
                    text: qsTr("Cancel")
                    variant: "ghost"
                    onClicked: createPrompt.dismiss()

                    KeyNavigation.up: nameField
                    KeyNavigation.left: createButton
                }
            }
        }

        Keys.onEscapePressed: event => {
            createPrompt.dismiss();
            event.accepted = true;
        }
    }

    // ── Context menu ───────────────────────────────────────────────────────
    // The shared list, plus the one verb that only exists inside a playlist.
    // ItemMenu builds that row from the entry id it finds on the item, so it is
    // absent — not disabled — anywhere an item has none.
    ItemMenu {
        id: itemMenu

        allowRemoveFromPlaylist: true
        onRemoveFromPlaylistRequested: item => {
            const entry = item && item.playlistItemId !== undefined
                        ? String(item.playlistItemId) : "";
            if (entry.length > 0)
                PlaylistCtl.removeEntries([entry]);
        }
    }

    // This page's own toast surface: every verb on it is a write to the user's
    // real library, and "did that work?" must be answered where the work
    // happened rather than only in the log.
    StrmToastHost {
        id: toasts

        anchors.fill: parent
        z: 200
    }

    // A labelled single-line input. Not in the shared library for the same
    // reason LoginPage's is not: StrmSearchField is the app's shared text input,
    // and this is the second place in the whole app that takes free text which
    // is not a search.
    component NameField: FocusScope {
        id: field

        property alias text: input.text
        property string label: ""
        property string placeholder: ""

        signal accepted
        signal cancelled

        readonly property bool hovered: fieldHover.hovered

        implicitHeight: caption.height + Theme.scale(4) + box.height
        height: implicitHeight
        activeFocusOnTab: true

        Text {
            id: caption

            anchors.left: parent.left
            anchors.top: parent.top
            text: field.label
            color: field.activeFocus ? Theme.accentColor : Theme.textSecondaryColor
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontCaption
            font.capitalization: Font.AllUppercase
            font.letterSpacing: Theme.trackLabel * Theme.fontCaption

            Behavior on color {
                ColorAnimation { duration: Theme.animFastMs; easing.type: Theme.easeStandard }
            }
        }

        Rectangle {
            id: box

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: caption.bottom
            anchors.topMargin: Theme.scale(4)
            height: Theme.controlHeightLarge
            radius: Theme.radiusChip
            color: field.hovered ? Theme.surfaceRaisedColor : Theme.surfaceColor
            border.width: 1
            border.color: Theme.hairline

            Behavior on color {
                ColorAnimation { duration: Theme.animInstant; easing.type: Theme.easeInstant }
            }

            TextInput {
                id: input

                anchors.fill: parent
                anchors.leftMargin: Theme.spacingTight * 1.5
                anchors.rightMargin: Theme.spacingTight * 1.5
                verticalAlignment: TextInput.AlignVCenter
                focus: true
                color: Theme.textPrimaryColor
                selectionColor: Theme.accentColor
                selectedTextColor: Theme.accentText
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontBodySize
                selectByMouse: true
                clip: true
                maximumLength: 120

                onAccepted: field.accepted()

                Keys.onEscapePressed: event => {
                    field.cancelled();
                    event.accepted = true;
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    visible: input.text.length === 0
                    text: field.placeholder
                    color: Theme.textTertiary
                    font: input.font
                }
            }
        }

        FocusRing {
            active: field.activeFocus
            anchors.fill: box
            radius: Theme.radiusChip
            inset: -Theme.focusRingWidth
        }

        HoverHandler {
            id: fieldHover
            cursorShape: Qt.IBeamCursor
        }

        TapHandler {
            onTapped: eventPoint => {
                field.forceActiveFocus(Qt.MouseFocusReason);
                input.forceActiveFocus(Qt.MouseFocusReason);
                input.cursorPosition = input.positionAt(
                    input.mapFromItem(field, eventPoint.position).x, input.height / 2);
            }
        }
    }
    // ── Rename / delete ────────────────────────────────────────────────────
    StrmPanel {
        id: renameSheet

        property string seed: ""

        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(Theme.scale(460), page.width - Theme.spacingLoose * 2)
        visible: false
        z: 900
        title: qsTr("Rename playlist")

        function open() {
            visible = true;
            renameField.text = renameSheet.seed;
            renameField.forceActiveFocus();
        }
        function close() {
            visible = false;
            page.forceActiveFocus();
        }

        Column {
            width: parent.width
            spacing: Theme.spacingValue

            StrmSearchField {
                id: renameField
                width: parent.width
                placeholderText: qsTr("Playlist name")
                onAccepted: renameSheet.commit()
                Keys.onEscapePressed: renameSheet.close()
            }

            Row {
                spacing: Theme.spacingTight
                anchors.right: parent.right

                StrmButton {
                    text: qsTr("Cancel")
                    variant: "ghost"
                    onClicked: renameSheet.close()
                }

                StrmButton {
                    text: qsTr("Rename")
                    variant: "primary"
                    enabled: renameField.text.trim().length > 0
                             && renameField.text.trim() !== renameSheet.seed
                    onClicked: renameSheet.commit()
                }
            }
        }

        function commit() {
            const wanted = renameField.text.trim();
            if (wanted.length === 0 || wanted === renameSheet.seed) {
                renameSheet.close();
                return;
            }
            PlaylistCtl.rename(PlaylistCtl.currentId, wanted);
            renameSheet.close();
        }
    }

    StrmPanel {
        id: confirmDelete

        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(Theme.scale(440), page.width - Theme.spacingLoose * 2)
        visible: false
        z: 900
        title: qsTr("Delete this playlist?")

        function open() {
            visible = true;
            cancelDelete.forceActiveFocus();
        }
        function close() {
            visible = false;
            page.forceActiveFocus();
        }

        Column {
            width: parent.width
            spacing: Theme.spacingValue

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                // Named, because "are you sure" without saying what is being
                // deleted is how the wrong thing gets deleted.
                text: qsTr("\"%1\" will be removed from the server. The tracks themselves are not deleted.")
                          .arg(PlaylistCtl.currentName)
                color: Theme.textSecondaryColor
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontBodySize
            }

            Row {
                spacing: Theme.spacingTight
                anchors.right: parent.right

                StrmButton {
                    id: cancelDelete
                    text: qsTr("Cancel")
                    variant: "ghost"
                    onClicked: confirmDelete.close()
                    KeyNavigation.right: confirmDeleteButton
                }

                StrmButton {
                    id: confirmDeleteButton
                    text: qsTr("Delete")
                    destructive: true
                    onClicked: {
                        PlaylistCtl.remove(PlaylistCtl.currentId);
                        confirmDelete.close();
                    }
                    KeyNavigation.left: cancelDelete
                }
            }
        }
    }

    Connections {
        target: PlaylistCtl

        // The open playlist stopped existing; the detail pane must not keep
        // showing a list the server no longer has.
        function onCurrentRemoved() {
            page.forceActiveFocus();
        }
    }

}
