import QtQuick
// For the StackView attached properties: a covered page drops its rows.
import QtQuick.Controls.Basic
import StrmQt

// One library, as a paged grid drawn as posters, wide art or a list (E2).
// Arrows navigate, Return opens details,
// clicking opens details, right-click (or the card's ⋯) opens the item menu,
// and approaching the end of loaded content fetches the next page.
//
// Navigation contract: no openDetails signal here — item verbs go through
// `Actions`, and Main.qml consumes its normalized route request once.
//
// The page reads only LibraryCtl.title / model / loading / errorMessage, never a
// library id, so LibraryCtl.openFavorites() — a filter across every library,
// with no parentId — renders through it unchanged.
FocusScope {
    id: page

    readonly property bool failed: LibraryCtl.errorMessage.length > 0
    readonly property bool isEmpty: grid.count === 0 && !LibraryCtl.loading

    // ── Covered-page content gate ──────────────────────────────────────────
    // The grid's delegates and their decoded posters are what this page costs,
    // and LibraryCtl.model is process-wide: a covered grid tearing delegates
    // down over a model the visible page is resetting is exactly the fan-out
    // measured on this page. So a page the user cannot see holds no rows — it
    // keeps its view mode, its card size and its cursor, and puts the cursor
    // back when it returns.
    //
    // `inStack` latches when this page joins the shell's StackView. Until then
    // it is a bare page — the startup self-test constructs pages directly — and
    // draws unconditionally.
    property bool inStack: false
    readonly property bool pageShown: !page.inStack
                                      || page.StackView.status === StackView.Active
    StackView.onActivated: page.inStack = true
    property bool gridReady: false
    property var gridState: null

    onPageShownChanged: {
        if (page.pageShown) {
            // Immediately, inside the pop that uncovered this page, so the
            // cursor restore is queued before the navigation history restores
            // focus for this route and can therefore not fight it.
            page.syncGridContent()
            return
        }
        // Deferred: evicting the oldest live page rebuilds the StackView (Qt 6
        // has no StackView.removeItem()), which takes the page on screen out of
        // the stack and puts it straight back inside one frame.
        Qt.callLater(page.syncGridContent)
    }

    function syncGridContent() {
        if (page.gridReady === page.pageShown)
            return
        if (!page.pageShown) {
            page.gridState = grid.navigationFocusSnapshot()
            page.gridReady = false
            return
        }
        page.gridReady = true
        Qt.callLater(page.restoreGridCursor)
    }

    // Cursor only. Where the keyboard lands after Back is the navigation
    // history's decision, and it restores the same {identity, index} itself
    // whenever the keyboard was in this grid.
    function restoreGridCursor() {
        const state = page.gridState
        if (!state || state.valid !== true)
            return
        grid.restoreNavigationCursor(Number(state.index))
    }

    // ── Where the keyboard starts ──────────────────────────────────────────
    // Decided here, once per scope, and not by `focus:` bindings on the grid
    // and the two empty states. Those were re-run by every change in the row
    // count, so sorting, filtering or picking a letter from the bar took the
    // keyboard out of the bar and dropped it in the grid the moment the new
    // rows landed.
    //
    // After the first seat only two things move it: the empty state the
    // keyboard was standing on going away (its own Retry / Clear filters did
    // that, and the rows it asked for are where the user is headed), and an
    // empty state appearing while the keyboard sits in a grid with no rows.
    // Everything else is the user's, or the navigator's to recover
    // (FocusNavigator.h). `focus`, not forceActiveFocus(): this is the page's
    // default, and must not take the keyboard off the shell's chrome.
    property bool focusSeated: false

    function holdsKeyboard(item) {
        for (let f = page.Window.activeFocusItem; f; f = f.parent) {
            if (f === item)
                return true
        }
        return false
    }

    function seatFocus() {
        // A covered page has no rows by design (above), which is not "empty".
        if (!page.gridReady)
            return
        if (page.isEmpty) {
            if (!page.focusSeated || page.holdsKeyboard(grid)) {
                // Two branches, not one conditional choosing between the
                // states: Qt 6.4's qmlcachegen crashes merging their types.
                if (page.failed)
                    errorState.focus = true
                else
                    emptyState.focus = true
                page.focusSeated = true
            }
            return
        }
        if (page.holdsKeyboard(errorState) || page.holdsKeyboard(emptyState)
                || (grid.count > 0 && !page.focusSeated)) {
            grid.focus = true
            page.focusSeated = true
        }
    }

    // Deferred: the states read above hang off the same properties, and the
    // order they are updated in is not this page's to rely on.
    onIsEmptyChanged: Qt.callLater(page.seatFocus)
    onGridReadyChanged: Qt.callLater(page.seatFocus)

    // ── View modes (ARCHITECTURE.md) ──────────────────────────────────────────
    // Three shapes of the same query: 2:3 posters, 16:9 wide art, or one row
    // per item. StrmGrid renders all three from one GridView, so switching is
    // a relayout and never a refetch — the model, the paging cursor and the
    // focused item all survive it.
    //
    // The choice is per library, because the right answer differs by content:
    // posters for films, wide art for a show whose episodes all have stills,
    // a list for a music or home-video library where the name is the content.
    readonly property var viewModes: [
        { text: qsTr("Posters"), value: "poster", icon: "grid" },
        { text: qsTr("Wide"),    value: "wide",   icon: "layout-wide" },
        { text: qsTr("List"),    value: "list",   icon: "list" }
    ]

    // Card-size steps, smallest first. 1.0 is the Theme's native card.
    readonly property var sizeSteps: [0.8, 0.9, 1.0, 1.15, 1.3]
    readonly property var sizeLabels: ["XS", "S", "M", "L", "XL"]
    readonly property int defaultSizeStep: 2

    property string viewMode: "poster"
    property int sizeStep: page.defaultSizeStep

    readonly property int viewModeIndex: {
        for (var i = 0; i < page.viewModes.length; ++i) {
            if (page.viewModes[i].value === page.viewMode)
                return i
        }
        return 0
    }

    // ── Per-library persistence ────────────────────────────────────────────
    // LibraryCtl publishes no library id, so the title is the only stable
    // per-scope key QML can see; it is also what a genre / person / studio /
    // Favorites view is distinguished by, which is exactly the granularity a
    // remembered view mode wants.
    //
    // Settings has no library-view storage yet (see the hand-back note in this
    // wave's report): until `Prefs.libraryViewMode` and friends exist, the
    // controls work and the preference simply does not outlive the page. The
    // capability check is the whole difference — nothing else here changes when
    // the invokables land.
    // A library id (or "favorites" / "genre:<id>" / "person:<id>"), not a title:
    // renaming a library on the server would otherwise lose its view settings,
    // and two libraries may share a name.
    readonly property string scopeKey: LibraryCtl.scopeKey !== undefined
                                       ? LibraryCtl.scopeKey : LibraryCtl.title

    // ── Collection scope (E4) ──────────────────────────────────────────────
    // A collection is not a library, and this page renders both. What the same
    // grid gets wrong for a franchise of five films is the *chrome*: a sort
    // control over a hand-curated release order re-scatters the only ordering
    // that mattered, and a 27-cell alphabet strip is a jump table for a list
    // you can already see all of. So on a collection the filter bar is not
    // shown at all, and the header carries the two verbs a set actually has —
    // Play all and Shuffle — plus the count, which is the whole of what a
    // collection needs said about it.
    //
    // The scope key is the only place the collection's id surfaces in QML
    // (LibraryCtl publishes no library id), and it is authoritative: the
    // controller builds "collection:<id>" in exactly the case where it also
    // suppressed SortBy, so the two can never disagree about which mode the
    // page is in.
    readonly property string collectionPrefix: "collection:"
    readonly property bool isCollection: page.scopeKey.indexOf(page.collectionPrefix) === 0
    readonly property string collectionId: page.isCollection
                                           ? page.scopeKey.substring(page.collectionPrefix.length)
                                           : ""
    readonly property int itemCount: LibraryCtl.model.totalRecordCount

    // Where Up out of the view controls lands: the alphabet strip when the
    // filter bar is on screen, the collection's own verbs when it is not.
    // Neither row may be skipped over into nothing, which is what a plain
    // `filterBar.entryItem` would do once the bar is hidden.
    readonly property Item viewBarUp: page.isCollection ? playAllButton : filterBar.entryItem
    readonly property bool viewPrefsPersist: typeof Prefs !== "undefined"
                                             && typeof Prefs.libraryViewMode === "function"
                                             && typeof Prefs.setLibraryViewMode === "function"
                                             && typeof Prefs.libraryCardSizeStep === "function"
                                             && typeof Prefs.setLibraryCardSizeStep === "function"

    function isKnownMode(mode) {
        for (var i = 0; i < page.viewModes.length; ++i) {
            if (page.viewModes[i].value === mode)
                return true
        }
        return false
    }

    function loadViewPrefs() {
        if (!page.viewPrefsPersist || page.scopeKey.length === 0)
            return
        const mode = Prefs.libraryViewMode(page.scopeKey)
        page.viewMode = page.isKnownMode(mode) ? mode : "poster"
        const step = Prefs.libraryCardSizeStep(page.scopeKey)
        page.sizeStep = (step >= 0 && step < page.sizeSteps.length) ? step
                                                                    : page.defaultSizeStep
    }

    function setViewMode(mode) {
        if (!page.isKnownMode(mode) || mode === page.viewMode)
            return
        page.viewMode = mode
        if (page.viewPrefsPersist && page.scopeKey.length > 0)
            Prefs.setLibraryViewMode(page.scopeKey, mode)
    }

    function setSizeStep(step) {
        const clamped = Math.max(0, Math.min(page.sizeSteps.length - 1, step))
        if (clamped === page.sizeStep)
            return
        page.sizeStep = clamped
        if (page.viewPrefsPersist && page.scopeKey.length > 0)
            Prefs.setLibraryCardSizeStep(page.scopeKey, clamped)
    }

    // The page outlives one library: opening a genre or Favorites through the
    // same controller changes the scope without rebuilding this page.
    onScopeKeyChanged: {
        page.loadViewPrefs()
        // A different library is a different visit: its first rows take the
        // keyboard the way this page's first rows did.
        page.focusSeated = false
    }
    Component.onCompleted: {
        page.loadViewPrefs()
        Qt.callLater(page.syncGridContent)
    }

    // ── The pad's triggers (Main.qml jumpLetter) ───────────────────────────
    // A letter is the only sane way across a 1300-item library from a
    // controller, so LT/RT step the alphabet strip and apply it. A collection
    // has no strip — sorting and alphabet-jumping are answers to questions a
    // curated set of six does not raise — so there the same buttons move the
    // grid a screenful instead, which is what they would have done anyway.
    function jumpLetter(step) {
        if (!page.isCollection && filterBar.stepLetter(step))
            return true
        return grid.pageBy(step)
    }

    // ── Item verbs ─────────────────────────────────────────────────────────
    // StrmGrid hands back an index into LibraryCtl.model; every verb needs the
    // item map, so one helper resolves it and each handler stays a single line.
    function itemAt(index) {
        const model = LibraryCtl.model
        if (!model || index < 0 || index >= model.count)
            return null
        return model.get(index)
    }

    // ── Context menu (ARCHITECTURE.md) ────────────────────────────────────────
    // The action list lives in ItemMenu, shared with every other page.
    function showMenu(index, sceneX, sceneY) {
        itemMenu.popupForItem(page.itemAt(index), sceneX, sceneY)
    }

    // ── Header ─────────────────────────────────────────────────────────────
    // The sort / filter / alphabet bar (ARCHITECTURE.md) is its own full-width row
    // below the header rather than a cluster inside PageHeader's action slot:
    // the alphabet strip is 27 cells wide and would have squeezed the title out
    // of a shared row at any window size worth using.
    PageHeader {
        id: header

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: Theme.pageMarginValue
        anchors.rightMargin: Theme.pageMarginValue
        anchors.topMargin: Theme.spacingValue
        height: header.implicitHeight

        title: LibraryCtl.title
        // A collection says what it is as well as how big it is: "7 items"
        // under a franchise name is the same sentence a library gets, and the
        // one thing the user cannot otherwise tell from this page is that they
        // are looking at a curated set rather than a filtered library.
        subtitle: page.isCollection
                  ? (page.itemCount > 0 ? qsTr("Collection  ·  %1 titles").arg(page.itemCount)
                                        : qsTr("Collection"))
                  : (page.itemCount > 0 ? qsTr("%1 items").arg(page.itemCount) : "")

        // ── Collection verbs (E4) ──────────────────────────────────────────
        // In the header's action slot rather than in a bar of their own: they
        // belong to the collection named beside them, and a set of two buttons
        // is not worth a third full-width row above a grid that already has
        // two. They are built unconditionally and hidden — the Row drops
        // invisible children, so a library header renders exactly as it did.
        //
        // Left to right: the one narrowing worth keeping from the filter bar,
        // then the two verbs, primary last and hard against the right edge
        // where the page's strongest action lives on every other page.
        StrmChip {
            id: unwatchedChip

            anchors.verticalCenter: parent.verticalCenter
            visible: page.isCollection
            text: qsTr("Unwatched")
            iconName: "eye-off"
            // Controlled, exactly as in FilterBar: the controller owns the
            // value and setWatchedFilter() is not self-toggling, so the
            // off-value is named here.
            checked: LibraryCtl.watchedFilter === "unplayed"
            onToggled: LibraryCtl.setWatchedFilter(unwatchedChip.checked ? "all" : "unplayed")

            property Item navRight: shuffleButton
            property Item navDown: viewSelect
        }

        StrmButton {
            id: shuffleButton

            anchors.verticalCenter: parent.verticalCenter
            visible: page.isCollection
            text: qsTr("Shuffle")
            iconName: "shuffle"
            enabled: page.itemCount > 0
            onClicked: Actions.shuffle(page.collectionId, "boxsets")

            property Item navLeft: unwatchedChip
            property Item navRight: playAllButton
            property Item navDown: viewSelect
        }

        StrmButton {
            id: playAllButton

            anchors.verticalCenter: parent.verticalCenter
            visible: page.isCollection
            text: qsTr("Play all")
            iconName: "play"
            variant: "primary"
            enabled: page.itemCount > 0
            // The collection id is the parent the server queues from, so the
            // queue is the whole set and not just the pages loaded into the
            // grid. playCollection, not playAll: playAll fixes SortBy=SortName,
            // which would queue a franchise alphabetically while this very grid
            // shows the server's curated release order.
            onClicked: Actions.playCollection(page.collectionId)

            property Item navLeft: shuffleButton
            property Item navDown: viewSelect
        }
    }

    // ── Sort / filter / alphabet (ARCHITECTURE.md) ────────────────────────────
    // Present and fixed-height for every *library* scope: nothing it does moves
    // the grid, and a filter that empties the grid must leave the control that
    // undoes it on screen. LibraryCtl owns every value in it.
    //
    // The one exception is a collection (E4), where the whole bar goes — and
    // goes to zero height with its margin, so the grid closes the gap instead
    // of hanging below an empty band. Sorting a curated order and alphabet-
    // jumping through six titles are both answers to questions a collection
    // does not raise; the "Unwatched" narrowing, the only part of the bar that
    // still earns its place, moved into the header beside the verbs.
    FilterBar {
        id: filterBar

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        anchors.leftMargin: Theme.pageMarginValue
        anchors.rightMargin: Theme.pageMarginValue
        anchors.topMargin: page.isCollection ? 0 : Theme.spacingValue
        visible: !page.isCollection
        height: page.isCollection ? 0 : filterBar.implicitHeight

        // The bar names no controller of its own, so the page says which query
        // it governs. LibraryCtl publishes watchedFilter, so the bar renders the
        // three watched chips.
        controller: LibraryCtl

        // Down out of the bar lands back in the content it governs.
        downTarget: viewSelect
        onFiltersRequested: filterSheet.open()
    }

    // ── View controls (ARCHITECTURE.md) ───────────────────────────────────────
    // Its own thin row rather than a cluster inside FilterBar: the filter bar
    // says *which* items, this says *how* they are drawn, and they are not the
    // same question. Right-aligned so it reads as chrome for the grid below it
    // rather than as another filter, and its height is fixed so nothing it does
    // moves the grid.
    //
    // Keyboard chain: alphabet strip → these three controls → the grid, so a
    // gamepad reaches them with the same Up/Down walk as everything else. The
    // pointer never moves focus here; hover only lights the control up.
    Item {
        id: viewBar

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: filterBar.bottom
        anchors.leftMargin: Theme.pageMarginValue
        anchors.rightMargin: Theme.pageMarginValue
        anchors.topMargin: Theme.spacingTight
        height: Theme.controlHeight

        Row {
            id: viewControls

            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingTight

            // Three mutually exclusive shapes read better as a segmented
            // control than a dropdown: the choice and the alternatives are both
            // visible without opening anything. One tab stop, not three —
            // Left/Right move between the segments, matching the alphabet strip.
            Row {
                id: viewSelect

                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.scale(2)
                activeFocusOnTab: true

                // Named for the navigator (FocusNavigator.h), which is asked
                // only once the handlers below have declined: Right walks the
                // segments first and leaves for the steppers from the last one.
                // An empty grid is not somewhere to send Down; the layout finds
                // the empty state's action instead.
                property Item navUp: page.viewBarUp
                property Item navRight: sizeDown
                property Item navDown: grid.count > 0 ? grid : null

                // Declined explicitly at either end. A per-key handler accepts
                // by default, and an accepted key is one the navigator never
                // hears about.
                Keys.onLeftPressed: event => {
                    const next = page.viewModeIndex - 1;
                    event.accepted = next >= 0;
                    if (event.accepted)
                        page.setViewMode(page.viewModes[next].value);
                }
                Keys.onRightPressed: event => {
                    const next = page.viewModeIndex + 1;
                    event.accepted = next < page.viewModes.length;
                    if (event.accepted)
                        page.setViewMode(page.viewModes[next].value);
                }

                Repeater {
                    model: page.viewModes

                    StrmIconButton {
                        required property int index
                        required property var modelData

                        iconName: modelData.icon
                        tooltip: modelData.text
                        checked: page.viewMode === modelData.value
                        // The row is the tab stop and owns the group's ring, so
                        // the segments stay out of the tab chain. StrmIconButton
                        // still takes focus on click (it forces it in its own
                        // TapHandler); that is fine and matches every other
                        // control — what must not happen is three stops here.
                        activeFocusOnTab: false
                        onClicked: page.setViewMode(modelData.value)
                    }
                }
            }

            // A gear label, not a heading: it names the pair of steppers beside
            // it the way a booth labels a control, and never competes with the
            // page title (ARCHITECTURE.md).
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("SIZE")
                color: Theme.textTertiary
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontCaption
                font.letterSpacing: Theme.fontCaption * Theme.trackLabel
                leftPadding: Theme.spacingTight
            }

            StrmIconButton {
                id: sizeDown

                anchors.verticalCenter: parent.verticalCenter
                iconName: "minus"
                enabled: page.sizeStep > 0
                tooltip: qsTr("Smaller cards")
                onClicked: page.setSizeStep(page.sizeStep - 1)

                property Item navUp: page.viewBarUp
                property Item navLeft: viewSelect
                property Item navRight: sizeUp
                property Item navDown: grid.count > 0 ? grid : null
            }

            // Tabular, so stepping through XS…XL does not shuffle the two
            // buttons either side of it.
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.scale(28)
                text: page.sizeLabels[page.sizeStep]
                color: Theme.textSecondaryColor
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontSmall
                horizontalAlignment: Text.AlignHCenter
            }

            StrmIconButton {
                id: sizeUp

                anchors.verticalCenter: parent.verticalCenter
                iconName: "plus"
                enabled: page.sizeStep < page.sizeSteps.length - 1
                tooltip: qsTr("Larger cards")
                onClicked: page.setSizeStep(page.sizeStep + 1)

                property Item navUp: page.viewBarUp
                property Item navLeft: sizeDown
                property Item navDown: grid.count > 0 ? grid : null
            }
        }

        // ── The group's focus ring ─────────────────────────────────────────
        // Outside the Row, and it has to be: FocusRing anchors-FILLS its
        // parent, and a positioner refuses fill anchors on its children — it
        // warns "Row will not function" and leaves the item 0×0 for the rest of
        // its life. Declared inside `viewSelect` it was therefore a 4 px speck
        // at the group's left edge, so landing here with a D-pad moved the
        // selection with nothing on screen to say where the keyboard was: a
        // control you could operate and could not see.
        //
        // The same shape as StrmTabBar's ring, and for the same reason: a
        // wrapper with real geometry, tracking the group from outside it.
        Item {
            x: viewControls.x + viewSelect.x
            y: viewControls.y + viewSelect.y
            width: viewSelect.width
            height: viewSelect.height

            FocusRing {
                active: viewSelect.activeFocus
                radius: Theme.radiusChip
                inset: -Theme.scale(2)
            }
        }
    }

    // Sits over the grid's top-right corner rather than in the layout: it must
    // never displace content the user is already reading (ARCHITECTURE.md).
    UpdateBanner {
        id: updateBanner

        controller: LibraryCtl
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingLoose
        anchors.top: viewBar.bottom
        anchors.topMargin: Theme.spacingValue
        z: 5
    }

    StrmGrid {
        id: grid

        navigationFocusKey: "library-items"
        navigationFocusFallbackItem: filterBar
        navigationFocusRefillActive: LibraryCtl.loading

        anchors.top: viewBar.bottom
        anchors.topMargin: Theme.spacingTight
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right

        // Up off the top row of the grid reaches the view controls — the row
        // nearest the grid — and one more Up from there reaches the alphabet
        // strip. The GridView only lets Up through when it cannot move within
        // itself, so this costs nothing while the cursor is anywhere below the
        // first row.
        property Item navUp: viewSelect
        // Never hidden when it is empty: hiding an item does not hand its
        // focus back when it reappears, so the grid stays visible-but-empty
        // and only the focus moves (seatFocus() above).
        onCountChanged: Qt.callLater(page.seatFocus)
        gridModel: page.gridReady ? LibraryCtl.model : null
        cardVariant: "poster"
        viewMode: page.viewMode
        cardScale: page.sizeSteps[page.sizeStep]
        emptyText: ""
        // Same 30-item lead the GridView-based page used.
        prefetchThreshold: 30

        // StrmGrid.nearEnd() fires for either reason the old page cared about:
        // the keyboard cursor closing on the end, or the viewport showing the
        // last rows. That second half is what a mouse user who scrolls without
        // ever moving the selection depends on. It is throttled per loaded
        // count, so calling loadMore() unconditionally here is safe.
        onNearEnd: if (LibraryCtl.canLoadMore) LibraryCtl.loadMore()

        onItemActivated: index => {
            const item = page.itemAt(index)
            if (item)
                Actions.openDetails(item)
        }
        onItemPlayRequested: index => {
            const item = page.itemAt(index)
            if (item)
                Actions.play(item)
        }
        onItemPlayedToggled: index => {
            const item = page.itemAt(index)
            if (item)
                Actions.togglePlayed(item)
        }
        onItemFavoriteToggled: index => {
            const item = page.itemAt(index)
            if (item)
                Actions.toggleFavorite(item)
        }
        onMenuRequested: (index, mx, my) => page.showMenu(index, mx, my)
    }

    ItemMenu {
        id: itemMenu

        allowAddToPlaylist: true
        onAddToPlaylistRequested: item => page.addToPlaylist(item)
    }

    // ── Add to playlist ────────────────────────────────────────────────────
    // The shared type-and-pick panel, as on the details page, so a film or an
    // episode can be filed from where it was found. The keyboard
    // goes back to the grid.
    function addToPlaylist(item): void {
        if (item && item.itemId)
            playlistPicker.show(item.name ? String(item.name) : "", [String(item.itemId)])
    }

    PlaylistPicker {
        id: playlistPicker

        z: 800
        onDismissed: grid.forceActiveFocus(Qt.OtherFocusReason)
    }

    // PlaylistCtl's results are global signals, so they are claimed only while
    // this page has a write of its own outstanding: several pages with a
    // picker are alive at once, and each of them hears every result. They go
    // to the window's toast host because a page that has since been covered is
    // not drawn, and neither is anything it shows.
    Connections {
        target: PlaylistCtl

        function onActionSucceeded(message) {
            if (!playlistPicker.pending)
                return
            playlistPicker.pending = false
            page.notify(message, "success")
        }
        function onActionFailed(message) {
            if (!playlistPicker.pending)
                return
            playlistPicker.pending = false
            page.notify(message, "error")
        }
    }

    // By name: the window is Main.qml's, and a page staged without it has
    // nowhere to say it and says nothing.
    function notify(message, severity): void {
        const win = page.toastWindow
        if (win && typeof win["notify"] === "function")
            win["notify"](message, severity)
    }

    // Untyped on purpose: `notify` is Main.qml's, not ApplicationWindow's.
    readonly property var toastWindow: page.ApplicationWindow.window

    // ── Page states ────────────────────────────────────────────────────────
    LoadingState {
        anchors.top: viewBar.bottom
        anchors.topMargin: Theme.spacingValue
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        visible: LibraryCtl.loading && grid.count === 0
        shape: "grid"
    }

    // Empty-because-the-request-failed. Deliberately a different surface from
    // empty-because-the-library-is-empty: before LibraryCtl.errorMessage existed
    // a failed page load left an empty grid and no explanation at all.
    EmptyState {
        id: errorState

        anchors.top: viewBar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        visible: page.isEmpty && page.failed
        property Item navUp: viewSelect
        severity: "error"
        iconName: "info"
        headline: qsTr("Couldn't load %1").arg(LibraryCtl.title)
        body: LibraryCtl.errorMessage
        // reload() re-runs the current query from page 0 and re-uses whichever
        // of open() / openFavorites() is armed, so it is the right verb here:
        // loadMore() cannot recover a failed *first* page (canLoadMore is
        // rowCount < totalRecordCount, and clear() zeroes both), which is
        // exactly the case this state exists for.
        actionText: qsTr("Retry")
        actionIcon: "refresh"
        onActionTriggered: LibraryCtl.reload()
    }

    // Empty-because-there-is-nothing — or, quite differently, empty because the
    // filter bar narrowed it to nothing. The second is a dead end without a way
    // back out, so it gets its own headline and the action that undoes it
    // (ARCHITECTURE.md).
    EmptyState {
        id: emptyState

        anchors.top: viewBar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        visible: page.isEmpty && !page.failed
        property Item navUp: viewSelect
        iconName: LibraryCtl.filtered ? "filter" : "library"
        headline: LibraryCtl.filtered ? qsTr("Nothing matches these filters")
                                      : qsTr("Nothing in this library")
        body: LibraryCtl.filtered
              ? qsTr("No item in %1 is left once these filters are applied.")
                .arg(LibraryCtl.title)
              : qsTr("Once your Emby server has scanned some media into %1, it shows up here.")
                .arg(LibraryCtl.title)
        actionText: LibraryCtl.filtered ? qsTr("Clear filters") : ""
        actionIcon: LibraryCtl.filtered ? "close" : ""
        onActionTriggered: LibraryCtl.clearFilters()
    }

    // A paging failure with rows already on screen: report it in place rather
    // than replacing everything the user has scrolled to. This Retry stays on
    // loadMore(), not reload(): the rows are fine, only the next page failed,
    // and reload() would throw away everything the user has scrolled past.
    Rectangle {
        id: pagingError

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.pageMarginValue
        height: pagingRow.implicitHeight + Theme.spacingValue * 2
        visible: page.failed && grid.count > 0
        radius: Theme.radiusPanel
        color: Theme.surfaceOverlay
        border.width: 1
        border.color: Theme.hairline

        Row {
            id: pagingRow

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: Theme.spacingValue
            anchors.rightMargin: Theme.spacingValue
            spacing: Theme.spacingValue

            StrmIcon {
                anchors.verticalCenter: parent.verticalCenter
                name: "info"
                color: Theme.negative
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: pagingRow.width - Theme.iconSize - retryButton.width
                       - pagingRow.spacing * 2
                text: qsTr("Couldn't load more: %1").arg(LibraryCtl.errorMessage)
                color: Theme.textSecondaryColor
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontSmall
                elide: Text.ElideRight
                maximumLineCount: 1
            }

            StrmButton {
                id: retryButton
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Retry")
                iconName: "refresh"
                variant: "secondary"
                onClicked: LibraryCtl.loadMore()
            }
        }
    }

    // ── Metadata filters ─────────────────────────────────────────────────────
    // Last, so it draws over the grid and the bars. It narrows the same query
    // the bar does and the grid behind the scrim refills as values are ticked.
    FilterSheet {
        id: filterSheet

        z: 20
        controller: LibraryCtl
        // Back to the button that opened it, which is where the user was.
        onDismissed: filterBar.filterEntry.forceActiveFocus(Qt.OtherFocusReason)
    }
}
