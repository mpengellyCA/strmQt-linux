pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import StrmQt

// FilterBar — the sort / filter / alphabet bar (ARCHITECTURE.md).
//
// It decides nothing and stores nothing: the CONTROLLER owns the query, and
// every control here is a *rendering* of a controller property plus one call
// back into it. That is deliberate — a bar that kept its own copy of "which
// sort is on" would disagree with the grid the first time a scope change (a
// genre, a person, Favorites, a different music tab) reset the query underneath
// it.
//
// ── One bar, two controllers ───────────────────────────────────────────────
// `controller` is LibraryCtl on the library page and MusicCtl on the music
// page. It used to name LibraryCtl in twenty-odd bindings, which meant music
// could only have a filter bar by getting a second copy of this file — and two
// copies of a bar this fiddly drift within one release.
//
// What it reads is a shape, not a type (the UpdateBanner pattern): `sortBy`,
// `sortDescending`, `availableSorts`, `nameStartsWith`, `filtered`,
// `setSort()`, `setNameStartsWith()`, `clearFilters()`. Two rows are optional
// and appear only when the controller actually has them, so neither page has to
// say which controls it wants:
//
//   `watchedFilter` + `setWatchedFilter()`  → the Unwatched / Watched /
//                                             Favorites row (film and TV)
//   `favoritesOnly` + `setFavoritesOnly()`  → a single Favourites toggle, which
//                                             is the whole of that question for
//                                             music: "unwatched songs" is not a
//                                             thing anyone asks for
//
// `extraFilters` is how a page adds a narrowing axis of its own without this
// file learning about it: a list of multi-select descriptors,
// `{ key, label, options: [{key,label}], selected: [key] }`, rendered as
// multi-select StrmSelects that report back through `extraFilterActivated`.
// Music's Genre filter is one of these — and a multi-select rather than chips
// because the measured library has 289 genres and chips stop scaling at about
// six.
//
// Three rows of intent, in one bar:
//
//   sort field ▸ direction   what order
//   watched chips / extras   what subset
//   A B C … Z #              where in that order to land
//
// Two rules shape the layout:
//
// 1. **The height never changes.** The clear affordance and the compact/wide
//    swap alter widths only, so nothing the user does here makes the grid below
//    jump under the cursor. (`showAlphabet` does change it — but that is a
//    property of the page, fixed for the life of the bar, not something the
//    user can toggle.)
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

    // The controller that owns the query. `var`, not a typed QtObject, for the
    // same reason UpdateBanner.controller is: these arrive as context
    // properties and have no QML type to check members against.
    property var controller: null

    // Off for a scope where jumping by first letter answers no question — a
    // list short enough to see all of, or one whose order is not alphabetical.
    property bool showAlphabet: true

    // Page-supplied narrowing axes. See the header note for the shape.
    property var extraFilters: []

    // A row of `extraFilters` was picked. `values` is the new selection for
    // that key; the page hands it straight to its controller.
    signal extraFilterActivated(string key, var values)

    // Where Down leaves the bar. The page points this at its content.
    property Item downTarget: null
    // Where Up leaves the bar's FIRST row. Null — the library page's case —
    // leaves Up doing nothing there, which is what it has always done. The
    // music page sets it to its tab bar, because Up out of a music grid used to
    // reach the tabs and a filter bar in between must not swallow that.
    property Item upTarget: null
    // Where the grid should send Up: the row nearest it. Normally the alphabet
    // strip, and the sort select when there is no alphabet strip — never
    // nothing, or Up out of a grid lands in a hole.
    readonly property Item entryItem: bar.showAlphabet ? alphaStrip : sortSelect

    // Wide enough for the labelled chips and the labelled buttons? Measured
    // against a fixed threshold rather than against the row's own implicitWidth,
    // because the row's width depends on this flag and that is a binding loop.
    readonly property bool compact: bar.width > 0 && bar.width < Theme.scale(860)

    // ── What the controller offers ─────────────────────────────────────────
    // Read once here, so the twenty controls below are not each repeating a
    // null test, and so what this bar needs of a controller is stated in one
    // place rather than inferred from the bindings.
    readonly property bool hasController: bar.controller !== null
                                          && bar.controller !== undefined
    readonly property bool hasWatchedFilter: bar.hasController
                                             && bar.controller.watchedFilter !== undefined
    readonly property bool hasFavoritesToggle: bar.hasController
                                               && bar.controller.favoritesOnly !== undefined
    readonly property string activeSort: bar.hasController
                                         && bar.controller.sortBy !== undefined
                                         ? String(bar.controller.sortBy) : ""
    readonly property bool activeDescending: bar.hasController
                                             && bar.controller.sortDescending === true
    readonly property string activeLetter: bar.hasController
                                           && bar.controller.nameStartsWith !== undefined
                                           ? String(bar.controller.nameStartsWith) : ""
    readonly property bool anyFilterOn: bar.hasController && bar.controller.filtered === true
    readonly property string watchedValue: bar.hasWatchedFilter
                                           ? String(bar.controller.watchedFilter) : "all"
    readonly property bool favoritesOn: bar.hasFavoritesToggle
                                        && bar.controller.favoritesOnly === true

    // The control the extras (and, with no extras, the clear button) chain back
    // to on Left: the last one of the fixed set that is actually on screen.
    readonly property Item lastFixedControl: bar.hasFavoritesToggle ? favoritesToggle
                                           : !bar.hasWatchedFilter ? directionButton
                                           : bar.compact ? watchedSelect
                                           : favoriteChip
    // Right out of that same set. Named once because five controls want it.
    readonly property Item firstExtraOrClear: extraRepeater.count > 0
                                              ? extraRepeater.itemAt(0)
                                              : (clearButton.visible ? clearButton : null)

    // A–Z then "#", which is the bucket Emby files everything non-alphabetic in.
    readonly property var letters: {
        const out = [];
        for (let c = 65; c <= 90; ++c)
            out.push(String.fromCharCode(c));
        out.push("#");
        return out;
    }

    // controller.availableSorts is [{key, label}] and varies by library kind and
    // by music tab; StrmSelect wants [{text, value}].
    readonly property var sortModel: {
        const out = [];
        if (!bar.hasController)
            return out;
        const source = bar.controller.availableSorts;
        if (!source)
            return out;
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
    // the same scope/tab signal and their order is not guaranteed, so the
    // cached copy can be one signal stale exactly when a scope change needs it.
    function sortIndexFor(key: string): int {
        if (!bar.hasController)
            return -1;
        const source = bar.controller.availableSorts;
        if (!source)
            return -1;
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
    //
    // ProductionYear and PlayCount are music's: a release-year sort means
    // newest records first, and "Most played" ascending is the least played.
    function defaultDescendingFor(key: string): bool {
        return key === "DateCreated" || key === "DatePlayed" || key === "PremiereDate"
            || key === "CommunityRating" || key === "CriticRating"
            || key === "ProductionYear" || key === "PlayCount";
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

    // ── Stepping the alphabet from outside the strip ───────────────────────
    // The pad's triggers and the "[" / "]" keys (InputMap nav.previousLetter /
    // nav.nextLetter). Unlike the strip's own Left/Right, which only move a
    // preview cursor, this APPLIES the letter — the caller has no cursor to
    // look at, so a step that changed nothing visible would not be a step.
    //
    // False when this bar has no strip, which is how the shell knows to fall
    // back to paging the list instead (Main.qml jumpLetter).
    function stepLetter(step: int): bool {
        if (!bar.showAlphabet || !bar.hasController)
            return false;
        const current = bar.letters.indexOf(bar.activeLetter);
        // With no letter applied yet the first step starts at the end it came
        // from — A going forwards, "#" going back — rather than from wherever
        // the preview cursor happens to be resting.
        const next = current < 0 ? (step > 0 ? 0 : bar.letters.length - 1)
                                 : current + step;
        // Clamped, and still TRUE at either end: the alternative is the trigger
        // silently paging the grid instead the moment the alphabet runs out,
        // which is one button doing two different things a letter apart.
        const target = Math.max(0, Math.min(bar.letters.length - 1, next));
        if (target === current)
            return true;
        alphaStrip.moveCursor(target);
        // setNameStartsWith() is self-toggling, so applyLetter() would CLEAR a
        // letter that is already on. A step always lands on a different one, so
        // it can never toggle — which is why it calls the controller directly.
        bar.controller.setNameStartsWith(bar.letters[target]);
        return true;
    }

    // ── Extra filters ──────────────────────────────────────────────────────
    // Toggling one row of a multi-select, expressed as data so the controller
    // stays the only thing that decides what a selection means.
    function toggledSelection(entry, value): var {
        const current = (entry && entry.selected) ? entry.selected : [];
        const out = [];
        let removed = false;
        for (let i = 0; i < current.length; ++i) {
            if (String(current[i]) === String(value))
                removed = true;
            else
                out.push(current[i]);
        }
        if (!removed)
            out.push(value);
        return out;
    }

    // The controller is the single source of truth, so every control is pushed
    // back into agreement with it after any query or scope change — including
    // the ones the user did not touch, and the ones a details-page drill-down
    // or a music tab switch reset behind our back.
    function syncFromController(): void {
        sortSelect.currentIndex = bar.sortIndexFor(bar.activeSort);
        watchedSelect.currentIndex = bar.watchedIndexFor(bar.watchedValue);
        alphaStrip.syncCursor();
    }

    implicitHeight: controls.height + (bar.showAlphabet
                                       ? Theme.spacingValue + alphaStrip.height
                                         + Theme.spacingTight + 1
                                       : Theme.spacingTight + 1)

    Component.onCompleted: bar.syncFromController()
    onControllerChanged: bar.syncFromController()

    Connections {
        target: bar.controller
        // MusicController raises tabChanged where LibraryController raises
        // scopeChanged, and neither has the other's signal.
        ignoreUnknownSignals: true
        function onQueryChanged() { bar.syncFromController(); }
        function onScopeChanged() { bar.syncFromController(); }
        function onTabChanged() { bar.syncFromController(); }
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
                // it from the controller, so a sort a details drill-down or a
                // tab switch reset moves this control too. StrmSelect never
                // writes it, so this push is the only thing that does — asking
                // the controller below is how a pick gets back here.

                KeyNavigation.right: directionButton
                KeyNavigation.up: bar.upTarget
                KeyNavigation.down: bar.entryItem === sortSelect ? bar.downTarget : alphaStrip

                onActivated: index => {
                    if (!bar.hasController)
                        return;
                    const key = sortSelect.valueAt(index);
                    if (key === undefined)
                        return;
                    // Keep the direction when the field is unchanged (the menu
                    // re-emits on every pick), otherwise seed the sensible one.
                    const descending = key === bar.activeSort
                                     ? bar.activeDescending
                                     : bar.defaultDescendingFor(key);
                    bar.controller.setSort(key, descending);
                }
            }

            // Direction is a button, not a chip: it performs an action rather
            // than naming a filter. The chevron points the way the list runs,
            // and the label spells it out whenever there is room for it.
            StrmButton {
                id: directionButton

                anchors.verticalCenter: parent.verticalCenter
                // Random has no direction to reverse.
                enabled: bar.hasController && bar.activeSort !== "Random"
                iconName: bar.activeDescending ? "chevron-down" : "chevron-up"
                text: bar.compact ? ""
                    : bar.activeDescending ? qsTr("Descending") : qsTr("Ascending")

                KeyNavigation.left: sortSelect
                KeyNavigation.right: bar.hasWatchedFilter
                                     ? (bar.compact ? watchedSelect : unwatchedChip)
                                     : bar.hasFavoritesToggle ? favoritesToggle
                                     : bar.firstExtraOrClear
                KeyNavigation.up: bar.upTarget
                KeyNavigation.down: bar.entryItem === sortSelect ? bar.downTarget : alphaStrip

                onClicked: {
                    if (bar.hasController)
                        bar.controller.setSort(bar.activeSort, !bar.activeDescending);
                }
                onHoveredChanged: {
                    if (directionButton.hovered)
                        directionTip.requestShow();
                    else
                        directionTip.requestHide();
                }

                StrmTooltip {
                    id: directionTip
                    target: directionButton
                    text: bar.activeDescending ? qsTr("Descending — click for ascending")
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
                visible: bar.hasWatchedFilter && !bar.compact
                text: qsTr("Unwatched")
                iconName: "eye-off"
                checked: bar.watchedValue === "unplayed"

                KeyNavigation.left: directionButton
                KeyNavigation.right: watchedChip
                KeyNavigation.up: bar.upTarget
                KeyNavigation.down: bar.entryItem === sortSelect ? bar.downTarget : alphaStrip

                onToggled: bar.controller.setWatchedFilter(unwatchedChip.checked ? "all"
                                                                                 : "unplayed")
            }

            StrmChip {
                id: watchedChip

                anchors.verticalCenter: parent.verticalCenter
                visible: bar.hasWatchedFilter && !bar.compact
                text: qsTr("Watched")
                iconName: "eye"
                checked: bar.watchedValue === "played"

                KeyNavigation.left: unwatchedChip
                KeyNavigation.right: favoriteChip
                KeyNavigation.up: bar.upTarget
                KeyNavigation.down: bar.entryItem === sortSelect ? bar.downTarget : alphaStrip

                onToggled: bar.controller.setWatchedFilter(watchedChip.checked ? "all" : "played")
            }

            StrmChip {
                id: favoriteChip

                anchors.verticalCenter: parent.verticalCenter
                visible: bar.hasWatchedFilter && !bar.compact
                text: qsTr("Favorites")
                iconName: "heart"
                checked: bar.watchedValue === "favorites"

                KeyNavigation.left: watchedChip
                KeyNavigation.right: bar.firstExtraOrClear
                KeyNavigation.up: bar.upTarget
                KeyNavigation.down: bar.entryItem === sortSelect ? bar.downTarget : alphaStrip

                onToggled: bar.controller.setWatchedFilter(favoriteChip.checked ? "all"
                                                                                : "favorites")
            }

            // The compact stand-in for the three chips: same four states, one
            // control, and it still names the active one without being opened.
            StrmSelect {
                id: watchedSelect

                anchors.verticalCenter: parent.verticalCenter
                visible: bar.hasWatchedFilter && bar.compact
                model: bar.watchedModel
                placeholder: qsTr("Show")
                // Pushed by syncFromController(), like the sort select above,
                // which is also what keeps it agreeing with the three chips it
                // stands in for at wider widths.

                KeyNavigation.left: directionButton
                KeyNavigation.right: bar.firstExtraOrClear
                KeyNavigation.up: bar.upTarget
                KeyNavigation.down: bar.entryItem === sortSelect ? bar.downTarget : alphaStrip

                onActivated: index => {
                    const value = watchedSelect.valueAt(index);
                    if (value !== undefined)
                        bar.controller.setWatchedFilter(String(value));
                }
            }

            // Music's whole answer to "what subset". A record is favourited or
            // it is not; there is no useful "unplayed songs" view to put beside
            // it, so this is one chip rather than the row of three above.
            StrmChip {
                id: favoritesToggle

                anchors.verticalCenter: parent.verticalCenter
                visible: bar.hasFavoritesToggle
                text: bar.compact ? "" : qsTr("Favourites")
                iconName: bar.favoritesOn ? "heart-filled" : "heart"
                checked: bar.favoritesOn

                KeyNavigation.left: directionButton
                KeyNavigation.right: bar.firstExtraOrClear
                KeyNavigation.up: bar.upTarget
                KeyNavigation.down: bar.entryItem === sortSelect ? bar.downTarget : alphaStrip

                onToggled: bar.controller.setFavoritesOnly(!favoritesToggle.checked)
            }

            // ── Page-supplied axes ─────────────────────────────────────────
            // One multi-select per descriptor, in the order the page gave them.
            // The bar renders and reports; it never decides what a genre id
            // means, which is why the toggled set goes back out as a signal
            // rather than into a call this file chooses.
            Repeater {
                id: extraRepeater

                // The COUNT, not the array. `extraFilters` is a JS array the
                // page rebuilds every time its controller's selection changes —
                // which is every pick — and a Repeater whose model is that array
                // destroys and recreates its delegates each time. That would
                // tear down the very menu the user is picking from, undoing the
                // whole point of a multi-select that stays open. Modelling the
                // length keeps one delegate alive and lets its own bindings
                // carry the new selection in.
                model: bar.extraFilters ? bar.extraFilters.length : 0

                delegate: StrmSelect {
                    id: extraSelect

                    required property int index

                    readonly property var descriptor: bar.extraFilters[extraSelect.index]

                    anchors.verticalCenter: parent.verticalCenter
                    multiSelect: true
                    model: {
                        const out = [];
                        const source = extraSelect.descriptor
                                       ? extraSelect.descriptor.options : null;
                        if (!source)
                            return out;
                        for (let i = 0; i < source.length; ++i)
                            out.push({ text: source[i].label, value: source[i].key });
                        return out;
                    }
                    selectedValues: (extraSelect.descriptor && extraSelect.descriptor.selected)
                                    ? extraSelect.descriptor.selected : []
                    placeholder: (extraSelect.descriptor
                                  && extraSelect.descriptor.label !== undefined)
                                 ? extraSelect.descriptor.label : qsTr("Filter")
                    // Nothing to pick yet (the genre walk is still in flight, or
                    // this library has none): shown, so the row does not move
                    // under the pointer when it arrives, but not offerable.
                    enabled: extraSelect.model.length > 0

                    KeyNavigation.left: extraSelect.index > 0
                                        ? extraRepeater.itemAt(extraSelect.index - 1)
                                        : bar.lastFixedControl
                    KeyNavigation.right: extraSelect.index + 1 < extraRepeater.count
                                         ? extraRepeater.itemAt(extraSelect.index + 1)
                                         : (clearButton.visible ? clearButton : null)
                    KeyNavigation.up: bar.upTarget
                    KeyNavigation.down: bar.entryItem === sortSelect ? bar.downTarget
                                                                     : alphaStrip

                    onActivated: index => {
                        const value = extraSelect.valueAt(index);
                        if (value === undefined || !extraSelect.descriptor)
                            return;
                        bar.extraFilterActivated(String(extraSelect.descriptor.key),
                                                 bar.toggledSelection(extraSelect.descriptor,
                                                                      value));
                    }
                }
            }

            // Last in the row on purpose: appearing and disappearing then moves
            // nothing that was already on screen.
            StrmButton {
                id: clearButton

                anchors.verticalCenter: parent.verticalCenter
                visible: bar.anyFilterOn
                variant: "ghost"
                iconName: "close"
                text: bar.compact ? "" : qsTr("Clear filters")

                KeyNavigation.left: extraRepeater.count > 0
                                    ? extraRepeater.itemAt(extraRepeater.count - 1)
                                    : bar.lastFixedControl
                KeyNavigation.up: bar.upTarget
                KeyNavigation.down: bar.entryItem === sortSelect ? bar.downTarget : alphaStrip

                onClicked: {
                    if (bar.hasController)
                        bar.controller.clearFilters();
                }
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
    // Emby matches the letter against the item's SORT name, not its title, so
    // "A Quiet Place" is filed under Q. Labelling this "first letter" would be
    // a lie the first time somebody looked for it under A, hence the hint.
    // (The controllers choose the wire form: LibraryController sends
    // NameStartsWith, MusicController an indexable sort-name range — same
    // sort-name matching either way.)
    //
    // It is if anything MORE right for music: "The Beatles" files under B, and
    // a user who does not know that will look under T and find nothing.
    Item {
        id: alphaHint

        anchors.left: parent.left
        anchors.top: controls.bottom
        anchors.topMargin: Theme.spacingValue
        visible: bar.showAlphabet && !bar.compact
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
        height: bar.showAlphabet ? alphaStrip.cellHeight + Theme.spacingTight : 0
        visible: bar.showAlphabet
        enabled: bar.showAlphabet

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

        activeFocusOnTab: bar.showAlphabet

        function syncCursor(): void {
            const active = bar.activeLetter;
            if (active.length === 0)
                return;
            const index = bar.letters.indexOf(active);
            if (index >= 0)
                alphaStrip.currentIndex = index;
        }

        // Tapping the letter that is already on clears it — the controller
        // implements that toggle, so this stays a single call either way.
        function applyLetter(index: int): void {
            if (index < 0 || index >= bar.letters.length || !bar.hasController)
                return;
            bar.controller.setNameStartsWith(bar.letters[index]);
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
            height: bar.showAlphabet ? alphaStrip.cellHeight : 0
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
                    model: bar.showAlphabet ? bar.letters : []

                    delegate: Item {
                        id: cell

                        required property string modelData
                        required property int index

                        readonly property bool active: bar.activeLetter === cell.modelData
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
        anchors.top: bar.showAlphabet ? alphaStrip.bottom : controls.bottom
        anchors.topMargin: Theme.spacingTight
        height: 1
        color: Theme.hairline
    }
}
