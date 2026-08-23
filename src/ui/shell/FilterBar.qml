pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import StrmQt

// FilterBar — the library page's sort / filter / alphabet bar (ARCHITECTURE.md).
//
// It decides nothing and stores nothing: LibraryCtl owns the query, and every
// control here is a *rendering* of a controller property plus one call back into
// it. That is deliberate — a bar that kept its own copy of "which sort is on"
// would disagree with the grid the first time a scope change (a genre, a person,
// Favorites) reset the query underneath it.
//
// Three rows of intent, in one bar:
//
//   sort field ▸ direction   what order
//   watched chips            what subset
//   A B C … Z #              where in that order to land
//
// Two rules shape the layout:
//
// 1. **The height never changes.** The clear affordance and the compact/wide
//    swap alter widths only, so nothing the user does here makes the grid below
//    jump under the cursor.
// 2. **Narrow degrades, it does not wrap.** Below `compact` the filter chips
//    collapse into one select, the labels drop to icons, and the alphabet
//    strip — which is the one genuinely wide thing here — scrolls horizontally
//    inside its own Flickable rather than reflowing onto a second line and
//    growing the bar.
//
// Focus model (ARCHITECTURE.md): hover never moves keyboard focus anywhere in
// this file. The alphabet strip is a SINGLE tab stop that owns Left/Right
// itself — 27 individually focusable letters would flood the focus chain and
// take the arrow keys away from the rest of the bar. Its cursor is a preview:
// moving it costs nothing, and only Return/Space or a click re-runs the query.
FocusScope {
    id: bar

    // Where Down leaves the bar. LibraryPage points this at its grid.
    property Item downTarget: null
    // Where the grid should send Up: the alphabet strip is the bar's last row,
    // so it is the row nearest the grid and the one Up should reach first.
    readonly property alias entryItem: alphaStrip

    // Wide enough for the labelled chips and the labelled buttons? Measured
    // against a fixed threshold rather than against the row's own implicitWidth,
    // because the row's width depends on this flag and that is a binding loop.
    readonly property bool compact: bar.width > 0 && bar.width < Theme.scale(860)

    // A–Z then "#", which is the bucket Emby files everything non-alphabetic in.
    readonly property var letters: {
        const out = [];
        for (let c = 65; c <= 90; ++c)
            out.push(String.fromCharCode(c));
        out.push("#");
        return out;
    }

    // LibraryCtl.availableSorts is [{key, label}] and varies by library kind;
    // StrmSelect wants [{text, value}].
    readonly property var sortModel: {
        const out = [];
        const source = LibraryCtl.availableSorts;
        for (let i = 0; i < source.length; ++i)
            out.push({ text: source[i].label, value: source[i].key });
        return out;
    }

    readonly property var watchedModel: [
        { text: qsTr("All"), value: "all" },
        { text: qsTr("Unwatched"), value: "unplayed" },
        { text: qsTr("Watched"), value: "played" },
        { text: qsTr("Favorites"), value: "favorites" }
    ]

    // Read from the controller, never from sortModel: both are recomputed from
    // the same scopeChanged signal and their order is not guaranteed, so the
    // cached copy can be one signal stale exactly when a scope change needs it.
    function sortIndexFor(key: string): int {
        const source = LibraryCtl.availableSorts;
        for (let i = 0; i < source.length; ++i) {
            if (source[i].key === key)
                return i;
        }
        return -1;
    }

    function watchedIndexFor(name: string): int {
        for (let i = 0; i < bar.watchedModel.length; ++i) {
            if (bar.watchedModel[i].value === name)
                return i;
        }
        return 0;
    }

    // Which way round a freshly chosen field should read. "Date added,
    // ascending" means oldest-first, which is nobody's idea of what picking
    // "Date added" was for; a name sort ascending is. Direction stays
    // user-changeable either way — this only picks the first guess.
    function defaultDescendingFor(key: string): bool {
        return key === "DateCreated" || key === "DatePlayed" || key === "PremiereDate"
            || key === "CommunityRating" || key === "CriticRating";
    }

    // Index of the alphabet cell a typed character should land on.
    function letterIndexFor(character: string): int {
        const upper = character.toUpperCase();
        const direct = bar.letters.indexOf(upper);
        if (direct >= 0)
            return direct;
        // Digits and punctuation all live in the "#" bucket.
        return /^[0-9]$/.test(upper) ? bar.letters.length - 1 : -1;
    }

    // The controller is the single source of truth, so every control is pushed
    // back into agreement with it after any query or scope change — including
    // the ones the user did not touch, and the ones a details-page drill-down
    // reset behind our back.
    function syncFromController(): void {
        sortSelect.currentIndex = bar.sortIndexFor(LibraryCtl.sortBy);
        watchedSelect.currentIndex = bar.watchedIndexFor(LibraryCtl.watchedFilter);
        alphaStrip.syncCursor();
    }

    implicitHeight: controls.height + Theme.spacingValue + alphaStrip.height
                    + Theme.spacingTight + 1

    Component.onCompleted: bar.syncFromController()

    Connections {
        target: LibraryCtl
        function onQueryChanged() { bar.syncFromController(); }
        function onScopeChanged() { bar.syncFromController(); }
    }

    // ── Row 1: what order, and what subset ─────────────────────────────────
    // One Row rather than a left group and a right group: at absurd window
    // widths a right-anchored group slides *under* the left one, and two
    // controls drawn on top of each other is a worse failure than a tail that
    // runs off the edge.
    Item {
        id: controls

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Theme.controlHeight

        Row {
            id: controlRow

            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingTight

            StrmIcon {
                anchors.verticalCenter: parent.verticalCenter
                visible: !bar.compact
                name: "sort"
                color: Theme.textTertiary
            }

            // The active sort is readable without opening anything: the select
            // shows its own label, which is the whole reason it is a select and
            // not an icon that hides the answer behind a menu.
            StrmSelect {
                id: sortSelect

                anchors.verticalCenter: parent.verticalCenter
                model: bar.sortModel
                placeholder: qsTr("Sort")
                // No currentIndex binding by design: syncFromController() pushes
                // it from LibraryCtl, so a sort a details drill-down reset moves
                // this control too. StrmSelect never writes it, so this push is
                // the only thing that does — asking the controller below is how
                // a pick gets back here.

                KeyNavigation.right: directionButton
                KeyNavigation.down: alphaStrip

                onActivated: index => {
                    const key = sortSelect.valueAt(index);
                    if (key === undefined)
                        return;
                    // Keep the direction when the field is unchanged (the menu
                    // re-emits on every pick), otherwise seed the sensible one.
                    const descending = key === LibraryCtl.sortBy
                                     ? LibraryCtl.sortDescending
                                     : bar.defaultDescendingFor(key);
                    LibraryCtl.setSort(key, descending);
                }
            }

            // Direction is a button, not a chip: it performs an action rather
            // than naming a filter. The chevron points the way the list runs,
            // and the label spells it out whenever there is room for it.
            StrmButton {
                id: directionButton

                anchors.verticalCenter: parent.verticalCenter
                // Random has no direction to reverse.
                enabled: LibraryCtl.sortBy !== "Random"
                iconName: LibraryCtl.sortDescending ? "chevron-down" : "chevron-up"
                text: bar.compact ? ""
                    : LibraryCtl.sortDescending ? qsTr("Descending") : qsTr("Ascending")

                KeyNavigation.left: sortSelect
                KeyNavigation.right: bar.compact ? watchedSelect : unwatchedChip
                KeyNavigation.down: alphaStrip

                onClicked: LibraryCtl.setSort(LibraryCtl.sortBy, !LibraryCtl.sortDescending)
                onHoveredChanged: {
                    if (directionButton.hovered)
                        directionTip.requestShow();
                    else
                        directionTip.requestHide();
                }

                StrmTooltip {
                    id: directionTip
                    target: directionButton
                    text: LibraryCtl.sortDescending ? qsTr("Descending — click for ascending")
                                                    : qsTr("Ascending — click for descending")
                }
            }

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 1
                height: Theme.scale(20)
                color: Theme.hairline
            }

            // Three toggles rather than four: "All" is the absence of a filter,
            // and clicking the lit chip turns it off. setWatchedFilter() is not
            // self-toggling (unlike setNameStartsWith), so the off-value is
            // named here — which is presentation, not query state.
            StrmChip {
                id: unwatchedChip

                anchors.verticalCenter: parent.verticalCenter
                visible: !bar.compact
                text: qsTr("Unwatched")
                iconName: "eye-off"
                checked: LibraryCtl.watchedFilter === "unplayed"

                KeyNavigation.left: directionButton
                KeyNavigation.right: watchedChip
                KeyNavigation.down: alphaStrip

                onToggled: LibraryCtl.setWatchedFilter(unwatchedChip.checked ? "all" : "unplayed")
            }

            StrmChip {
                id: watchedChip

                anchors.verticalCenter: parent.verticalCenter
                visible: !bar.compact
                text: qsTr("Watched")
                iconName: "eye"
                checked: LibraryCtl.watchedFilter === "played"

                KeyNavigation.left: unwatchedChip
                KeyNavigation.right: favoriteChip
                KeyNavigation.down: alphaStrip

                onToggled: LibraryCtl.setWatchedFilter(watchedChip.checked ? "all" : "played")
            }

            StrmChip {
                id: favoriteChip

                anchors.verticalCenter: parent.verticalCenter
                visible: !bar.compact
                text: qsTr("Favorites")
                iconName: "heart"
                checked: LibraryCtl.watchedFilter === "favorites"

                KeyNavigation.left: watchedChip
                KeyNavigation.right: clearButton.visible ? clearButton : null
                KeyNavigation.down: alphaStrip

                onToggled: LibraryCtl.setWatchedFilter(favoriteChip.checked ? "all" : "favorites")
            }

            // The compact stand-in for the three chips: same four states, one
            // control, and it still names the active one without being opened.
            StrmSelect {
                id: watchedSelect

                anchors.verticalCenter: parent.verticalCenter
                visible: bar.compact
                model: bar.watchedModel
                placeholder: qsTr("Show")
                // Pushed by syncFromController(), like the sort select above,
                // which is also what keeps it agreeing with the three chips it
                // stands in for at wider widths.

                KeyNavigation.left: directionButton
                KeyNavigation.right: clearButton.visible ? clearButton : null
                KeyNavigation.down: alphaStrip

                onActivated: index => {
                    const value = watchedSelect.valueAt(index);
                    if (value !== undefined)
                        LibraryCtl.setWatchedFilter(String(value));
                }
            }

            // Last in the row on purpose: appearing and disappearing then moves
            // nothing that was already on screen.
            StrmButton {
                id: clearButton

                anchors.verticalCenter: parent.verticalCenter
                visible: LibraryCtl.filtered
                variant: "ghost"
                iconName: "close"
                text: bar.compact ? "" : qsTr("Clear filters")

                KeyNavigation.left: bar.compact ? watchedSelect : favoriteChip
                KeyNavigation.down: alphaStrip

                onClicked: LibraryCtl.clearFilters()
                onHoveredChanged: {
                    if (clearButton.hovered)
                        clearTip.requestShow();
                    else
                        clearTip.requestHide();
                }

                StrmTooltip {
                    id: clearTip
                    target: clearButton
                    text: qsTr("Clear filters")
                }
            }
        }
    }

    // ── Row 2: the alphabet ────────────────────────────────────────────────
    // Emby matches NameStartsWith against the item's SORT name, not its title,
    // so "A Quiet Place" is filed under Q. Labelling this "first letter" would
    // be a lie the first time somebody looked for it under A, hence the hint.
    Item {
        id: alphaHint

        anchors.left: parent.left
        anchors.top: controls.bottom
        anchors.topMargin: Theme.spacingValue
        visible: !bar.compact
        width: alphaHint.visible ? hintRow.implicitWidth : 0
        height: alphaStrip.height

        Row {
            id: hintRow

            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.scale(6)

            StrmIcon {
                anchors.verticalCenter: parent.verticalCenter
                name: "info"
                size: Theme.scale(14)
                color: Theme.textTertiary
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("SORT NAME")
                color: Theme.textTertiary
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontCaption
                font.letterSpacing: Theme.fontCaption * Theme.trackLabel
            }
        }

        HoverHandler {
            id: hintHover
            onHoveredChanged: {
                if (hintHover.hovered)
                    hintTip.requestShow();
                else
                    hintTip.requestHide();
            }
        }

        StrmTooltip {
            id: hintTip
            target: alphaHint
            text: qsTr("Jumps by sort name, not title — “A Quiet Place” is filed under Q")
        }
    }

    Item {
        id: alphaStrip

        anchors.left: alphaHint.visible ? alphaHint.right : parent.left
        anchors.leftMargin: alphaHint.visible ? Theme.spacingValue : 0
        anchors.right: parent.right
        anchors.top: controls.bottom
        anchors.topMargin: Theme.spacingValue
        // Fixed: the scrollbar lives in the padding below the cells so that a
        // library narrow enough to need one is not a library whose grid moved.
        height: alphaStrip.cellHeight + Theme.spacingTight

        // Keyboard cursor. A preview — it never re-runs the query on its own.
        property int currentIndex: 0
        // Pointer hover, deliberately a separate value that never touches the
        // cursor above (ARCHITECTURE.md).
        property int hoveredIndex: -1

        readonly property int cellHeight: Theme.scale(26)
        // Letters spread to fill the width, within reason, and start scrolling
        // once even the minimum no longer fits.
        readonly property int cellWidth: Math.max(Theme.scale(24),
                                                  Math.min(Theme.scale(34),
                                                           Math.floor(flick.width / bar.letters.length)))

        activeFocusOnTab: true

        function syncCursor(): void {
            const active = LibraryCtl.nameStartsWith;
            if (active.length === 0)
                return;
            const index = bar.letters.indexOf(active);
            if (index >= 0)
                alphaStrip.currentIndex = index;
        }

        // Tapping the letter that is already on clears it — the controller
        // implements that toggle, so this stays a single call either way.
        function applyLetter(index: int): void {
            if (index < 0 || index >= bar.letters.length)
                return;
            LibraryCtl.setNameStartsWith(bar.letters[index]);
        }

        function moveCursor(index: int): void {
            alphaStrip.currentIndex = Math.max(0, Math.min(bar.letters.length - 1, index));
            alphaStrip.ensureVisible(alphaStrip.currentIndex);
        }

        function ensureVisible(index: int): void {
            if (flick.width <= 0 || flick.contentWidth <= flick.width)
                return;
            const left = index * alphaStrip.cellWidth;
            const right = left + alphaStrip.cellWidth;
            let target = flick.contentX;
            if (left < target)
                target = left;
            else if (right > target + flick.width)
                target = right - flick.width;
            flick.contentX = Math.max(0, Math.min(flick.contentWidth - flick.width, target));
        }

        // One handler for the whole strip rather than the per-key convenience
        // signals: every key this row consumes has to be weighed against the
        // ones it must let past (Up/Down leave the row, and are the reason
        // `accepted` is set explicitly on both paths instead of by default).
        //
        // Left/Right may auto-repeat — holding Right walks the alphabet, which
        // costs nothing because moving the cursor fetches nothing. Everything
        // that *does* re-run the query is auto-repeat guarded.
        Keys.onPressed: event => {
            const modified = (event.modifiers
                              & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)) !== 0;
            if (modified) {
                event.accepted = false;
                return;
            }
            switch (event.key) {
            case Qt.Key_Left:
                alphaStrip.moveCursor(alphaStrip.currentIndex - 1);
                event.accepted = true;
                return;
            case Qt.Key_Right:
                alphaStrip.moveCursor(alphaStrip.currentIndex + 1);
                event.accepted = true;
                return;
            case Qt.Key_Home:
                alphaStrip.moveCursor(0);
                event.accepted = true;
                return;
            case Qt.Key_End:
                alphaStrip.moveCursor(bar.letters.length - 1);
                event.accepted = true;
                return;
            case Qt.Key_Return:
            case Qt.Key_Enter:
            case Qt.Key_Space:
                if (!event.isAutoRepeat)
                    alphaStrip.applyLetter(alphaStrip.currentIndex);
                event.accepted = true;
                return;
            default:
                break;
            }
            // Type-to-jump: with the strip focused, pressing K goes to K and
            // applies it, which is how an alphabet strip is actually used once
            // you know it is there. Shift is allowed through — it is how most
            // people type a capital.
            if (event.isAutoRepeat || event.text.length !== 1) {
                event.accepted = false;
                return;
            }
            const index = bar.letterIndexFor(event.text);
            if (index < 0) {
                event.accepted = false;
                return;
            }
            alphaStrip.moveCursor(index);
            alphaStrip.applyLetter(index);
            event.accepted = true;
        }

        KeyNavigation.up: sortSelect
        KeyNavigation.down: bar.downTarget

        Flickable {
            id: flick

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: alphaStrip.cellHeight
            contentWidth: alphaStrip.cellWidth * bar.letters.length
            contentHeight: flick.height
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            clip: true

            ScrollBar.horizontal: StrmScrollBar {
                policy: flick.contentWidth > flick.width ? ScrollBar.AsNeeded
                                                         : ScrollBar.AlwaysOff
            }

            Row {
                spacing: 0

                Repeater {
                    model: bar.letters

                    delegate: Item {
                        id: cell

                        required property string modelData
                        required property int index

                        readonly property bool active: LibraryCtl.nameStartsWith === cell.modelData
                        readonly property bool hovered: cellHover.hovered
                        readonly property bool cursor: alphaStrip.currentIndex === cell.index

                        width: alphaStrip.cellWidth
                        height: alphaStrip.cellHeight

                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: 1
                            radius: Theme.radiusChip
                            color: cell.active ? Theme.accentColor
                                 : cell.hovered ? Theme.hoverTint
                                 : "transparent"

                            Behavior on color {
                                ColorAnimation {
                                    duration: Theme.animInstant
                                    easing.type: Theme.easeInstant
                                }
                            }
                        }

                        Text {
                            anchors.centerIn: parent
                            text: cell.modelData
                            color: cell.active ? Theme.accentText
                                 : (cell.hovered || (cell.cursor && alphaStrip.activeFocus))
                                   ? Theme.textPrimaryColor
                                 : Theme.textSecondaryColor
                            font.family: Theme.fontMono
                            font.pixelSize: Theme.fontSmall

                            Behavior on color {
                                ColorAnimation {
                                    duration: Theme.animInstant
                                    easing.type: Theme.easeInstant
                                }
                            }
                        }

                        // The ring follows the keyboard cursor only, and only
                        // while the strip actually holds focus.
                        FocusRing {
                            active: cell.cursor && alphaStrip.activeFocus
                            radius: Theme.radiusChip
                            inset: 1
                        }

                        HoverHandler {
                            id: cellHover
                            cursorShape: Qt.PointingHandCursor
                            // Records hover for painting and nothing else: it
                            // must not move the cursor, and must never take
                            // keyboard focus.
                            onHoveredChanged: {
                                if (cellHover.hovered)
                                    alphaStrip.hoveredIndex = cell.index;
                                else if (alphaStrip.hoveredIndex === cell.index)
                                    alphaStrip.hoveredIndex = -1;
                            }
                        }

                        TapHandler {
                            acceptedButtons: Qt.LeftButton
                            gesturePolicy: TapHandler.ReleaseWithinBounds
                            onTapped: {
                                // Clicking is a deliberate act, so it may take
                                // focus and move the cursor — hover may not.
                                alphaStrip.forceActiveFocus(Qt.MouseFocusReason);
                                alphaStrip.currentIndex = cell.index;
                                alphaStrip.applyLetter(cell.index);
                            }
                        }
                    }
                }
            }
        }
    }

    // Separates the chrome from the content it governs, so the grid reads as
    // being *under* the bar rather than as more of it.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: alphaStrip.bottom
        anchors.topMargin: Theme.spacingTight
        height: 1
        color: Theme.hairline
    }
}
