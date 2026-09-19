pragma ComponentBehavior: Bound
import QtQuick
// For the StackView attached properties: a covered page drops its view.
import QtQuick.Controls.Basic
import StrmQt

// MusicBrowsePage — one music library read five ways (Crate spec §5).
//
// MusicBrowseCtl owns everything with a meaning: the section, the filters
// shared across Albums, Artists and Songs, the sort each section remembers,
// the letter, every count and every label. This page states intent and draws
// what it is told. Nothing here builds a query or a display string beyond a
// button caption.
//
// One Loader holds the visible section; the other four are Components, never
// live trees. Each keeps a navigation snapshot so switching back lands on the
// same record, and each has its own navigationFocusKey so Back/Forward restores
// focus exactly.
//
// Navigation contract: the page pushes nothing. Cards open through
// Actions.openDetails; Home is homeRequested(), which Main.qml routes.
FocusScope {
    id: page

    // ── Scope (set by Main.qml; the reconstruction properties) ─────────────
    property string libraryId: ""
    property string libraryName: ""
    property string initialSection: "albums"

    signal homeRequested

    // ── View state ─────────────────────────────────────────────────────────
    // Decoupled from MusicBrowseCtl.section so the old view can be snapshotted
    // before the Loader swaps it.
    property string loadedSection: "albums"
    property bool viewReady: false
    property var viewStates: ({})
    readonly property var activeView: viewLoader.item
    readonly property var lane: MusicBrowseCtl.currentLane
    readonly property bool laneLoading: page.lane !== null && page.lane.loading
    readonly property string laneError: page.lane !== null ? page.lane.error : ""
    readonly property bool songsShown: page.loadedSection === "songs"
    readonly property bool playlistsShown: page.loadedSection === "playlists"
    readonly property int shownCount: page.activeView && page.activeView.count !== undefined
                                      ? Number(page.activeView.count) : 0
    // The Playlists view always has its New playlist tile to stand on.
    readonly property bool contentFocusable: page.shownCount > 0 || page.playlistsShown

    // ── Covered-page content gate ──────────────────────────────────────────
    // A retained page the user cannot see keeps its scalar state and its view
    // snapshots, but holds no delegates and no decoded covers: the section view
    // IS the expensive part of this page. Off screen it also stops reacting to
    // the shared controller's section changes, which is what made every covered
    // copy of this page rebuild a view over MusicBrowseCtl's models at once.
    //
    // `inStack` latches when this page joins the shell's StackView. Until then
    // it is a bare page — the startup self-test and the unit probes construct
    // pages directly — and draws unconditionally.
    property bool inStack: false
    readonly property bool pageShown: !page.inStack
                                      || page.StackView.status === StackView.Active
    StackView.onActivated: page.inStack = true

    // Set while the Loader is rebuilding because the page came back on screen
    // rather than because the user switched section. Both need the view's cursor
    // put back — that IS the scroll position — but only a section switch may
    // take the keyboard with it: on the way back from Back the navigation
    // history owns where focus lands, and it restores a moment later.
    property bool uncovering: false

    onPageShownChanged: {
        if (page.pageShown) {
            // Applied immediately, inside the pop that uncovered this page, so
            // the view exists and its cursor restore is already queued before
            // the navigation history restores focus for this route.
            page.syncViewContent();
            return;
        }
        // Deferred. Evicting the oldest live page rebuilds the StackView (Qt 6
        // has no StackView.removeItem()), which takes the page on screen out of
        // the stack and puts it straight back inside one frame; settling the
        // tear-down collapses that into no change at all.
        Qt.callLater(page.syncViewContent);
    }

    readonly property string songRowFormat: qsTr("%1 · %2")
    readonly property int songRowHeight: Theme.scale(52)
    readonly property int songNumberColumn: Theme.scale(46)
    readonly property int songDurationColumn: Theme.scale(64)
    readonly property int songVerbsColumn: Theme.scale(72)
    readonly property int songArtistColumn:
        page.songsView() && page.songsView().showArtistColumn ? Theme.scale(220) : 0

    Component.onCompleted: {
        // Main.qml opens the controller before it pushes; this covers a page
        // constructed with a scope of its own. The self-test passes none, and
        // then nothing is fetched.
        if (page.libraryId.length > 0 && MusicBrowseCtl.libraryId !== page.libraryId)
            MusicBrowseCtl.open(page.libraryId, page.initialSection);
        page.loadedSection = MusicBrowseCtl.section;
        Qt.callLater(page.syncViewContent);
    }

    // Brings the view into line with pageShown. Covered: snapshot and drop it.
    // Shown: adopt whatever section the controller is on now — onSectionChanged
    // deliberately ignored every change while this page was off screen — and
    // rebuild, which restores the snapshot through viewLoader.onLoaded.
    function syncViewContent(): void {
        if (page.viewReady === page.pageShown)
            return;
        if (!page.pageShown) {
            page.captureActiveView();
            page.viewReady = false;
            return;
        }
        page.uncovering = page.inStack;
        page.adoptSection();
        page.viewReady = true;
    }

    function adoptSection(): void {
        if (page.loadedSection === MusicBrowseCtl.section)
            return;
        page.captureActiveView();
        page.loadedSection = MusicBrowseCtl.section;
    }

    Connections {
        target: MusicBrowseCtl

        // The controller has already started the new section's lane; only
        // now may the Loader build delegates over its model.
        function onSectionChanged() {
            // A covered page must not rebuild a view nobody can see over the
            // model the visible page is reading. It adopts the section when it
            // comes back on screen instead (syncViewContent).
            if (!page.pageShown)
                return;
            page.adoptSection();
        }

        function onAlbumTracksCollected(subject, trackIds) {
            playlistPicker.show(subject, trackIds);
        }

        function onActionFailed(message) {
            browseToasts.show(message, "error");
        }
    }

    function captureActiveView(): void {
        const view = page.activeView;
        if (!view || typeof view.navigationFocusSnapshot !== "function")
            return;
        const next = Object.assign({}, page.viewStates);
        next[page.loadedSection] = view.navigationFocusSnapshot();
        page.viewStates = next;
    }

    function restoreActiveView(): void {
        const view = page.activeView;
        const state = page.viewStates[page.loadedSection];
        const cursorOnly = page.uncovering;
        page.uncovering = false;
        if (!view || !state || state.valid !== true)
            return;
        if (cursorOnly) {
            if (typeof view.restoreNavigationCursor === "function")
                view.restoreNavigationCursor(Number(state.index));
            return;
        }
        if (typeof view.restoreNavigationFocus !== "function")
            return;
        view.restoreNavigationFocus(String(state.identity), Number(state.index));
    }

    function focusContent(): void {
        if (page.contentFocusable && page.activeView)
            page.activeView.forceActiveFocus(Qt.TabFocusReason);
    }

    // Focus is stranded: something held the keyboard and then stopped being a
    // place the keyboard can be. That happens in four measured shapes at Qt
    // 6.11.2, all four reproduced on this page in an offscreen probe, and Qt
    // behaves differently in each — which is why one test cannot find them all:
    //
    //  1. The focused item goes `visible: false`. Qt does NOT clear focus, so
    //     the keyboard stays on something nobody can see. Every pill, every
    //     clear-chip, the letter dividers and both Retry buttons do this.
    //  2. The focused item goes `enabled: false`. Qt DOES clear focus — onto
    //     the nearest enclosing FocusScope, which on this page is `pillBar`,
    //     NOT `page`. Measured with the sort-direction button, which disables
    //     when the section being switched to remembers a Random sort; the
    //     shoulder buttons make that section switch without first moving focus
    //     off the button. A `focused === page` test is enough for a page with
    //     no inner scope; here it would miss this entirely.
    //  3. The focused item goes `focus: false` — the views' `focus: count > 0`.
    //     Also parks on a scope, but StrmGrid's own NavigationFocusRestorer
    //     fallback already lands it on `navigationFocusFallbackItem` (`strip`),
    //     so this one is the control's to handle and is left to it.
    //  4. The keyboard is inside a view that is perfectly visible and EMPTY:
    //     not null, not a scope, not invisible, so none of the tests above can
    //     see it. `focusInEmptyView()` asks the one question they cannot.
    //
    // `focused === null` is insurance, not a measured case: every attempt to
    // strand focus by destroying its owner — switching section, which destroys
    // the Loader's item — ended with Qt re-parenting focus to an enclosing
    // scope, never with a null activeFocusItem.
    readonly property bool focusParked: {
        const focused = page.Window.activeFocusItem;
        return focused === null || focused === page || focused === pillBar
            || focused.visible === false;
    }

    // Shape 4. `contentFocusable` is the page's own answer to "is there anything
    // in the view to stand on", so this is "the keyboard is in the view and the
    // view says there is nothing there".
    function focusInEmptyView(): bool {
        if (page.contentFocusable || page.activeView === null)
            return false;
        let cursor = page.Window.activeFocusItem;
        for (let i = 0; i < 32 && cursor !== null; ++i) {
            if (cursor === page.activeView)
                return true;
            cursor = cursor.parent;
        }
        return false;
    }

    function isStranded(): bool {
        return page.activeFocus && (page.focusParked || page.focusInEmptyView());
    }

    // Recovery has three sensible landings, tried nearest-context first: the
    // pill row (where every pill/button case starts from), the section strip
    // (always on screen), and the content view (where a successful retry or a
    // section switch leaves something to focus). One of the three is always
    // reachable, so this never leaves focus nowhere.
    function recoverStranded(): bool {
        if (pillBar.entryItem) {
            pillBar.entryItem.forceActiveFocus(Qt.OtherFocusReason);
            return true;
        }
        if (page.contentFocusable && page.activeView) {
            page.activeView.forceActiveFocus(Qt.OtherFocusReason);
            return true;
        }
        strip.forceActiveFocus(Qt.OtherFocusReason);
        return true;
    }

    function recoverIfStranded(): void {
        // Mid-refill the answer is not knowable yet — the rows are on their way
        // and the view takes the keyboard back itself when they land — so every
        // trigger below fires again on the falling edge of `laneLoading`.
        if (page.laneLoading || !page.isStranded())
            return;
        page.recoverStranded();
    }

    // The per-control `onVisibleChanged` hooks below catch the cases we know
    // about; this catches the rest, including controls added later. The binding
    // re-reads the focused item, that item's own `visible`, and its identity
    // against the two container scopes, so it re-evaluates exactly when focus
    // can strand and never needs a hook of its own. Deferred for the same
    // reason the hooks are: the handler runs before the sibling bindings that
    // decide where focus should land have caught up.
    onFocusParkedChanged: {
        if (page.focusParked)
            Qt.callLater(page.recoverIfStranded);
    }

    // The page being handed the keyboard is its own case: a FocusScope opened on
    // an empty section has no `focus: true` child to forward to, so it parks the
    // keyboard on itself and `focusParked` never CHANGES — it was already true
    // while nothing in the window was focused at all.
    onActiveFocusChanged: Qt.callLater(page.recoverIfStranded)

    // Shape 4 moves the COUNT, not the focus, so nothing in `focusParked`
    // notices it. This is the edge on which a view that had rows stops having
    // any while the keyboard is still standing in it.
    onShownCountChanged: Qt.callLater(page.recoverIfStranded)

    // Every trigger above can fire while a lane is still filling, when
    // `recoverIfStranded` deliberately declines to answer. This is the edge
    // where the answer finally exists.
    onLaneLoadingChanged: {
        if (!page.laneLoading)
            Qt.callLater(page.recoverIfStranded);
    }

    // ── Item helpers ───────────────────────────────────────────────────────
    function modelFor(key): var {
        return key === "artists" ? MusicBrowseCtl.artists
             : key === "songs" ? MusicBrowseCtl.songs
             : key === "genres" ? MusicBrowseCtl.genres
             : key === "playlists" ? MusicBrowseCtl.playlists
             : MusicBrowseCtl.albums;
    }

    function itemAt(index): var {
        const model = page.modelFor(page.loadedSection);
        if (!model || index < 0 || index >= model.count)
            return null;
        return model.get(index);
    }

    function idOf(item): string {
        return item && item.itemId !== undefined ? String(item.itemId) : "";
    }

    function nameOf(item): string {
        return item && item.name !== undefined ? String(item.name) : "";
    }

    function songsView(): var {
        return page.songsShown ? page.activeView : null;
    }

    function focusedItem(): var {
        const view = page.activeView;
        if (!view || view.currentIndex === undefined)
            return null;
        return page.itemAt(view.currentIndex);
    }

    function openItem(index): void {
        const item = page.itemAt(index);
        if (page.idOf(item).length === 0)
            return;
        if (page.loadedSection === "genres")
            MusicBrowseCtl.openGenre(page.idOf(item), page.nameOf(item));
        else
            Actions.openDetails(item);
    }

    // ▶ on a card. A record plays in disc order, an artist starts a radio seeded
    // on them, a playlist opens (its order is the playlist).
    function playItem(index): void {
        const item = page.itemAt(index);
        const id = page.idOf(item);
        if (id.length === 0)
            return;
        if (page.loadedSection === "albums")
            MusicPlay.playAlbum(id, page.nameOf(item));
        else if (page.loadedSection === "artists")
            MusicPlay.radio(id, page.nameOf(item));
        else if (page.loadedSection === "playlists")
            Actions.openDetails(item);
    }

    function playSongFrom(index): void {
        const model = MusicBrowseCtl.songs;
        if (index < 0 || index >= model.count)
            return;
        Actions.playAllFrom(ModelUtils.drain(model), index);
    }

    function fileSongSelection(): void {
        const table = page.songsView();
        if (!table)
            return;
        const ids = table.selectedIds();
        if (ids.length > 0)
            playlistPicker.show(page.libraryName.length > 0 ? page.libraryName : qsTr("Music"), ids);
    }

    readonly property string nowPlayingId: {
        const queue = PlayerCtl.queue;
        if (!queue || queue.currentIndex < 0)
            return "";
        const current = queue.currentItem();
        return current && current.itemId !== undefined ? String(current.itemId) : "";
    }

    // ── Shell hooks (Main.qml) ─────────────────────────────────────────────
    // LB/RB walk Home · Albums · … · Playlists and wrap, so the pair is never
    // a dead button.
    function cycleTab(step): bool {
        const key = MusicBrowseCtl.cycleSection(step);
        if (key === "home")
            page.homeRequested();
        return true;
    }

    // LT/RT step the crate dividers while they show (name sort) and page the
    // view otherwise.
    function jumpLetter(step): bool {
        if (MusicBrowseCtl.jumpLetter(step))
            return true;
        const view = page.activeView;
        return view !== null && typeof view.pageBy === "function" && view.pageBy(step) === true;
    }

    // ── The music input context ────────────────────────────────────────────
    MappedShortcut {
        actionId: "music.playPause"
        fallback: ["Space"]
        active: App.interactionContext === "music" && PlayerCtl.active
        onActivated: PlayerCtl.togglePause()
    }

    MappedShortcut {
        actionId: "music.shuffleAll"
        fallback: ["S"]
        active: App.interactionContext === "music" && MusicBrowseCtl.libraryId.length > 0
                && MusicBrowseCtl.filtersAvailable
        onActivated: MusicBrowseCtl.shuffleFiltered()
    }

    MappedShortcut {
        actionId: "music.favorite"
        fallback: ["L"]
        active: App.interactionContext === "music" && page.loadedSection !== "genres"
        onActivated: {
            const table = page.songsView();
            if (table && table.selectionCount > 0) {
                Actions.setFavoriteAll(table.selectedIds(), true);
                return;
            }
            const item = page.focusedItem();
            if (item)
                Actions.toggleFavorite(item);
        }
    }

    MappedShortcut {
        actionId: "music.instantMix"
        fallback: ["R"]
        active: App.interactionContext === "music"
                && page.loadedSection !== "playlists" && page.loadedSection !== "genres"
        onActivated: {
            const item = page.focusedItem();
            if (item)
                Actions.instantMix(item);
        }
    }

    // ── Header and sections ────────────────────────────────────────────────
    PageHeader {
        id: header

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: Theme.pageMarginValue
        anchors.rightMargin: Theme.pageMarginValue
        anchors.topMargin: Theme.spacingValue
        height: header.implicitHeight
        title: page.libraryName.length > 0 ? page.libraryName : qsTr("Music")
    }

    SectionStrip {
        id: strip

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        anchors.leftMargin: Theme.pageMarginValue
        anchors.rightMargin: Theme.pageMarginValue
        anchors.topMargin: Theme.spacingTight
        currentKey: MusicBrowseCtl.section
        // Empty or loading, the strip holds the keyboard (see the Loader).
        focus: !page.contentFocusable

        onSectionChosen: key => {
            if (key === "home")
                page.homeRequested();
            else
                MusicBrowseCtl.section = key;
        }

        Keys.onDownPressed: event => {
            if (pillBar.entryItem) {
                pillBar.entryItem.forceActiveFocus(Qt.TabFocusReason);
                event.accepted = true;
            }
        }
    }

    // ── Pill row ───────────────────────────────────────────────────────────
    // One scope that owns Left/Right across both halves, in reading order, so
    // no control needs a KeyNavigation pair that goes stale when a pill hides.
    FocusScope {
        id: pillBar

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: strip.bottom
        anchors.leftMargin: Theme.pageMarginValue
        anchors.rightMargin: Theme.pageMarginValue
        anchors.topMargin: Theme.spacingValue
        height: Theme.controlHeight

        readonly property var order: [genrePill, decadePill, formatPill, favouritesPill,
                                      unplayedPill, clearAllButton, albumArtistsPill,
                                      everyonePill, sortSelect, directionButton, playButton,
                                      shuffleButton]
        readonly property Item entryItem: {
            for (let i = 0; i < pillBar.order.length; ++i) {
                if (pillBar.reachable(pillBar.order[i]))
                    return pillBar.order[i];
            }
            return null;
        }

        function reachable(item): bool {
            return item !== null && item.visible && item.enabled;
        }

        function step(delta): bool {
            let at = -1;
            for (let i = 0; i < pillBar.order.length; ++i) {
                if (pillBar.order[i].activeFocus)
                    at = i;
            }
            if (at < 0)
                return false;
            for (let i = at + delta; i >= 0 && i < pillBar.order.length; i += delta) {
                if (pillBar.reachable(pillBar.order[i])) {
                    pillBar.order[i].forceActiveFocus(Qt.TabFocusReason);
                    return true;
                }
            }
            // No reachable neighbour that way: same as pageBy/jumpLetter,
            // false means "did nothing," not "handled."
            return false;
        }

        function popupUnder(menu, anchor): void {
            const p = anchor.mapToItem(null, 0, anchor.height + Theme.scale(4));
            menu.popupAt(p.x, p.y);
        }

        Keys.onLeftPressed: event => event.accepted = pillBar.step(-1)
        Keys.onRightPressed: event => event.accepted = pillBar.step(1)
        Keys.onUpPressed: event => {
            strip.forceActiveFocus(Qt.TabFocusReason);
            event.accepted = true;
        }
        Keys.onDownPressed: event => {
            page.focusContent();
            event.accepted = true;
        }

        Item {
            id: pillClip

            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: Math.max(0, parent.width - rightCluster.width - Theme.spacingValue)
            clip: true

            Row {
                id: pillRow

                anchors.verticalCenter: parent.verticalCenter
                // Keep the focused pill on screen when the row is wider than its clip.
                x: {
                    let focused = null;
                    for (let i = 0; i < pillBar.order.length; ++i) {
                        if (pillBar.order[i].activeFocus && pillBar.order[i].parent === pillRow)
                            focused = pillBar.order[i];
                    }
                    if (!focused)
                        return 0;
                    const overflow = focused.x + focused.width - pillClip.width;
                    return overflow > 0 ? -overflow : 0;
                }
                spacing: Theme.spacingTight

                FilterPill {
                    id: genrePill
                    visible: MusicBrowseCtl.filtersAvailable
                    // Ruling P3-R2: FilterPill ties its own activeFocusOnTab to
                    // `enabled`, never to `visible` — override it here so a
                    // pill hidden on Genres/Playlists does not go on DECLARING
                    // a Tab stop. Qt's Tab traversal skips an invisible item
                    // whatever activeFocusOnTab says, so the damage is a wrong
                    // property value, not a stranded keyboard — and it is real
                    // damage, because NavRail.qml:43 and :98 read the property
                    // directly to build their navigation sets.
                    //
                    // The `|| activeFocus` term on every guard in this file is
                    // load-bearing, and the `&& enabled` term is not. Measured
                    // at Qt 6.11.2: nothing on this page ever binds `enabled`
                    // to the same condition as `visible` (Play and Shuffle bind
                    // it to libraryId, which does not flip while browsing), so
                    // `visible && enabled` is a bare `visible` in disguise —
                    // and a bare `visible` cannot settle. Qt refuses to clear
                    // activeFocusOnTab on the item that currently holds the
                    // keyboard: it warns, keeps the old value, and since
                    // `visible` does not change again the binding never gets a
                    // second chance — so the value stays stale at `true` for
                    // the life of the pill. `|| activeFocus` gives it one, on
                    // the tick after focus leaves. The shoulder buttons (Main.qml
                    // cycleSection -> page.cycleTab) change the section without
                    // first moving focus off a pill, which is what makes every
                    // one of these reachable rather than theoretical.
                    activeFocusOnTab: (genrePill.visible && genrePill.enabled)
                                      || genrePill.activeFocus
                    // Ruling P3-R2 (stranding half): visible going false does
                    // not move focus off a pill that held it — recover if it did.
                    onVisibleChanged: Qt.callLater(page.recoverIfStranded)
                    text: MusicBrowseCtl.genrePillText
                    active: MusicBrowseCtl.genreIds.length > 0
                    onActivated: {
                        MusicBrowseCtl.ensureGenreOptions();
                        genrePicker.open();
                    }
                    onCleared: MusicBrowseCtl.clearGenres()
                }

                FilterPill {
                    id: decadePill
                    visible: MusicBrowseCtl.filtersAvailable && MusicBrowseCtl.decadeAvailable
                    activeFocusOnTab: (decadePill.visible && decadePill.enabled)
                                      || decadePill.activeFocus
                    onVisibleChanged: Qt.callLater(page.recoverIfStranded)
                    text: MusicBrowseCtl.decadePillText
                    active: MusicBrowseCtl.decade !== 0
                    onActivated: pillBar.popupUnder(decadeMenu, decadePill)
                    onCleared: MusicBrowseCtl.decade = 0
                }

                FilterPill {
                    id: formatPill
                    visible: MusicBrowseCtl.filtersAvailable && MusicBrowseCtl.formatAvailable
                    activeFocusOnTab: (formatPill.visible && formatPill.enabled)
                                      || formatPill.activeFocus
                    onVisibleChanged: Qt.callLater(page.recoverIfStranded)
                    text: MusicBrowseCtl.formatPillText
                    active: MusicBrowseCtl.format !== "any"
                    onActivated: pillBar.popupUnder(formatMenu, formatPill)
                    onCleared: MusicBrowseCtl.format = "any"
                }

                FilterPill {
                    id: favouritesPill
                    visible: MusicBrowseCtl.filtersAvailable
                    activeFocusOnTab: (favouritesPill.visible && favouritesPill.enabled)
                                      || favouritesPill.activeFocus
                    onVisibleChanged: Qt.callLater(page.recoverIfStranded)
                    toggle: true
                    iconName: MusicBrowseCtl.favouritesOnly ? "heart-filled" : "heart"
                    text: qsTr("Favourites")
                    active: MusicBrowseCtl.favouritesOnly
                    onActivated: MusicBrowseCtl.favouritesOnly = !MusicBrowseCtl.favouritesOnly
                }

                FilterPill {
                    id: unplayedPill
                    visible: MusicBrowseCtl.filtersAvailable
                    activeFocusOnTab: (unplayedPill.visible && unplayedPill.enabled)
                                      || unplayedPill.activeFocus
                    onVisibleChanged: Qt.callLater(page.recoverIfStranded)
                    toggle: true
                    text: qsTr("Unplayed")
                    active: MusicBrowseCtl.unplayedOnly
                    onActivated: MusicBrowseCtl.unplayedOnly = !MusicBrowseCtl.unplayedOnly
                }

                StrmButton {
                    id: clearAllButton
                    anchors.verticalCenter: parent.verticalCenter
                    visible: MusicBrowseCtl.filtersAvailable && MusicBrowseCtl.activeFilterCount >= 2
                    // Ruling P3-R2: StrmButton ties activeFocusOnTab to
                    // `interactive` (enabled && !busy), never to `visible`.
                    activeFocusOnTab: (clearAllButton.visible && clearAllButton.enabled)
                                      || clearAllButton.activeFocus
                    onVisibleChanged: Qt.callLater(page.recoverIfStranded)
                    variant: "ghost"
                    iconName: "close"
                    text: qsTr("Clear all")
                    onClicked: MusicBrowseCtl.clearFilters()
                }

                // Which artists is a choice of endpoint, not a filter, so it
                // sits at the end of the row rather than among the pills.
                FilterPill {
                    id: albumArtistsPill
                    visible: MusicBrowseCtl.section === "artists"
                    activeFocusOnTab: (albumArtistsPill.visible && albumArtistsPill.enabled)
                                      || albumArtistsPill.activeFocus
                    // Same shape as the filter pills above: leaving Artists
                    // hides this one too, and can hide it while it holds focus.
                    onVisibleChanged: Qt.callLater(page.recoverIfStranded)
                    toggle: true
                    text: qsTr("Album artists")
                    active: MusicBrowseCtl.artistMode === "albumArtists"
                    onActivated: MusicBrowseCtl.artistMode = "albumArtists"
                }

                FilterPill {
                    id: everyonePill
                    visible: MusicBrowseCtl.section === "artists"
                    activeFocusOnTab: (everyonePill.visible && everyonePill.enabled)
                                      || everyonePill.activeFocus
                    onVisibleChanged: Qt.callLater(page.recoverIfStranded)
                    toggle: true
                    text: qsTr("Everyone")
                    active: MusicBrowseCtl.artistMode === "everyone"
                    onActivated: MusicBrowseCtl.artistMode = "everyone"
                }
            }
        }

        Row {
            id: rightCluster

            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingTight

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: MusicBrowseCtl.countText
                color: Theme.textSecondaryColor
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontCaption
                font.letterSpacing: Theme.trackLabel * Theme.fontCaption
            }

            StrmSelect {
                id: sortSelect

                anchors.verticalCenter: parent.verticalCenter
                placeholder: qsTr("Sort")
                model: {
                    const out = [];
                    const source = MusicBrowseCtl.availableSorts;
                    for (let i = 0; i < source.length; ++i)
                        out.push({ "text": source[i].label, "value": source[i].key });
                    return out;
                }
                // Controlled: the controller answers every pick.
                currentIndex: {
                    const source = MusicBrowseCtl.availableSorts;
                    for (let i = 0; i < source.length; ++i) {
                        if (source[i].key === MusicBrowseCtl.sortKey)
                            return i;
                    }
                    return -1;
                }
                onActivated: index => {
                    const key = sortSelect.valueAt(index);
                    if (key !== undefined)
                        MusicBrowseCtl.sortKey = String(key);
                }
            }

            StrmIconButton {
                id: directionButton

                anchors.verticalCenter: parent.verticalCenter
                enabled: MusicBrowseCtl.sortKey !== "random"
                iconName: MusicBrowseCtl.sortDescending ? "chevron-down" : "chevron-up"
                tooltip: MusicBrowseCtl.sortDescending ? qsTr("Descending") : qsTr("Ascending")
                onClicked: MusicBrowseCtl.sortDescending = !MusicBrowseCtl.sortDescending
            }

            StrmButton {
                id: playButton

                anchors.verticalCenter: parent.verticalCenter
                visible: MusicBrowseCtl.filtersAvailable
                enabled: MusicBrowseCtl.libraryId.length > 0
                activeFocusOnTab: (playButton.visible && playButton.enabled)
                                  || playButton.activeFocus
                text: qsTr("Play")
                iconName: "play"
                variant: "primary"
                accessibleDescription: MusicBrowseCtl.scopeLabel
                onClicked: MusicBrowseCtl.playFiltered()
            }

            StrmButton {
                id: shuffleButton

                anchors.verticalCenter: parent.verticalCenter
                visible: MusicBrowseCtl.filtersAvailable
                enabled: MusicBrowseCtl.libraryId.length > 0
                activeFocusOnTab: (shuffleButton.visible && shuffleButton.enabled)
                                  || shuffleButton.activeFocus
                text: qsTr("Shuffle")
                iconName: "shuffle"
                accessibleDescription: MusicBrowseCtl.scopeLabel
                onClicked: MusicBrowseCtl.shuffleFiltered()
            }
        }
    }

    StrmMenu {
        id: decadeMenu

        actions: {
            const out = [{ "text": qsTr("Any decade"), "checked": MusicBrowseCtl.decade === 0 },
                         { "separator": true }];
            const source = MusicBrowseCtl.decadeOptions;
            for (let i = 0; i < source.length; ++i)
                out.push({ "text": source[i].label, "checked": MusicBrowseCtl.decade === source[i].value });
            return out;
        }
        onTriggered: index => {
            if (index === 0)
                MusicBrowseCtl.decade = 0;
            else if (index >= 2)
                MusicBrowseCtl.decade = MusicBrowseCtl.decadeOptions[index - 2].value;
        }
    }

    StrmMenu {
        id: formatMenu

        actions: {
            const out = [];
            const source = MusicBrowseCtl.formatOptions;
            for (let i = 0; i < source.length; ++i)
                out.push({ "text": source[i].label, "checked": MusicBrowseCtl.format === source[i].key });
            return out;
        }
        onTriggered: index => MusicBrowseCtl.format = MusicBrowseCtl.formatOptions[index].key
    }

    // ── Songs selection ────────────────────────────────────────────────────
    SelectionBar {
        id: songSelection

        anchors.top: pillBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.pageMarginValue
        anchors.rightMargin: Theme.pageMarginValue
        // Guarded the way `shownCount` above guards `count`, and for the same
        // reason: `songsShown` turns true with `loadedSection`, while
        // `activeView` is `viewLoader.item` and is still the outgoing view (or
        // null) until the Loader swaps. A truthy view is therefore not yet a
        // view with a `selectionCount`, and the bare read assigned undefined to
        // an int.
        count: {
            const view = page.songsView();
            const selected = view ? view.selectionCount : 0;
            return selected === undefined ? 0 : Number(selected);
        }

        onQueueRequested: Actions.addAllToQueue(page.songsView().selectedItems())
        onPlaylistRequested: page.fileSongSelection()
        onFavoriteRequested: Actions.setFavoriteAll(page.songsView().selectedIds(), true)
        onClearRequested: {
            const table = page.songsView();
            if (table) {
                table.clearSelection();
                table.forceActiveFocus(Qt.OtherFocusReason);
            }
        }
    }

    // ── Section views ──────────────────────────────────────────────────────
    Component {
        id: albumsComponent

        StrmGrid {
            id: albumsGrid

            navigationFocusKey: "musicBrowse-albums"
            navigationFocusFallbackItem: strip
            navigationFocusRefillActive: MusicBrowseCtl.albumsLane.loading
            gridModel: MusicBrowseCtl.albums
            customCardWidth: Theme.crateSleeveSize
            customCardHeight: Theme.crateSleeveSize + Theme.scale(56)
            emptyText: ""
            prefetchThreshold: 30
            focus: albumsGrid.count > 0
            KeyNavigation.up: pillBar.entryItem
            onNearEnd: MusicBrowseCtl.loadMore()
            onItemActivated: index => page.openItem(index)
            onItemPlayRequested: index => page.playItem(index)
            onMenuRequested: (index, mx, my) => musicMenu.popupForItem(page.itemAt(index), mx, my)

            cardComponent: Component {
                CrateSleeve {
                    id: sleeve

                    property var model: null
                    property int index: -1

                    size: Theme.crateSleeveSize
                    showCaption: true
                    coverUrl: sleeve.model && sleeve.model.coverUrl !== undefined ? String(sleeve.model.coverUrl) : ""
                    title: sleeve.model && sleeve.model.title !== undefined ? String(sleeve.model.title) : ""
                    subtitle: sleeve.model && sleeve.model.subtitle !== undefined ? String(sleeve.model.subtitle) : ""
                    badge: sleeve.model && sleeve.model.releaseBadge !== undefined ? String(sleeve.model.releaseBadge) : ""
                    hiRes: false
                }
            }
        }
    }

    Component {
        id: artistsComponent

        StrmGrid {
            id: artistsGrid

            navigationFocusKey: "musicBrowse-artists"
            navigationFocusFallbackItem: strip
            navigationFocusRefillActive: MusicBrowseCtl.artistsLane.loading
            gridModel: MusicBrowseCtl.artists
            customCardWidth: Theme.cratePortraitSize
            customCardHeight: Theme.cratePortraitSize + Theme.scale(52)
            emptyText: ""
            prefetchThreshold: 30
            focus: artistsGrid.count > 0
            KeyNavigation.up: pillBar.entryItem
            onNearEnd: MusicBrowseCtl.loadMore()
            onItemActivated: index => page.openItem(index)
            // CratePortrait declares no playRequested signal (no ▶ on an
            // artist card), so there is nothing for onItemPlayRequested to
            // ever connect to here.
            onMenuRequested: (index, mx, my) => musicMenu.popupForItem(page.itemAt(index), mx, my)

            cardComponent: Component {
                CratePortrait {
                    id: portrait

                    property var model: null
                    property int index: -1

                    size: Theme.cratePortraitSize
                    imageUrl: portrait.model && portrait.model.coverUrl !== undefined ? String(portrait.model.coverUrl) : ""
                    name: portrait.model && portrait.model.name !== undefined ? String(portrait.model.name) : ""
                    subtitle: portrait.model && portrait.model.subtitle !== undefined ? String(portrait.model.subtitle) : ""
                }
            }
        }
    }

    Component {
        id: songsComponent

        TrackTable {
            id: songsTable

            navigationFocusKey: "musicBrowse-songs"
            navigationFocusFallbackItem: strip
            navigationFocusRefillActive: MusicBrowseCtl.songsLane.loading
            focus: songsTable.count > 0
            model: MusicBrowseCtl.songs
            rowHeight: page.songRowHeight
            discGrouping: false
            artistRule: false
            alwaysShowArtist: true
            multiSelect: true
            prefetchThreshold: 30
            KeyNavigation.up: pillBar.entryItem
            onActivated: index => page.playSongFrom(index)
            onMenuRequested: (index, mx, my) => songMenu.popupForItemNoDetails(page.itemAt(index), mx, my)
            onNearEnd: MusicBrowseCtl.loadMore()

            delegate: TrackRow {
                id: songRow

                required property int index
                required property var model

                readonly property string trackId: songRow.model.itemId !== undefined ? String(songRow.model.itemId) : ""
                readonly property string albumText: songRow.model.albumTitle !== undefined ? String(songRow.model.albumTitle) : ""
                readonly property string formatText: songRow.model.formatBadge !== undefined ? String(songRow.model.formatBadge) : ""

                width: songsTable.width
                navigationFocusOwner: songsTable
                rowHeight: page.songRowHeight
                numberColumn: page.songNumberColumn
                durationColumn: page.songDurationColumn
                verbsColumn: page.songVerbsColumn
                artistColumn: page.songArtistColumn
                title: songRow.model.name !== undefined ? String(songRow.model.name) : ""
                // Album · FORMAT: the badge rides the secondary line until
                // Phase 4's CrateTrackTable gives it a column.
                secondary: songRow.formatText.length === 0 ? songRow.albumText
                         : songRow.albumText.length === 0 ? songRow.formatText
                         // One argument per call: QML's String.arg() takes a single
                         // value, so the two-argument form threw "Invalid arguments"
                         // once per rendered song row (950 warnings in one session).
                         : page.songRowFormat.arg(songRow.albumText).arg(songRow.formatText)
                artist: songsTable.shownArtistFor(songRow.model)
                durationText: songRow.model.durationText !== undefined ? String(songRow.model.durationText) : ""
                number: songRow.index + 1
                coverUrl: songRow.model.coverUrl !== undefined ? String(songRow.model.coverUrl) : ""
                showCover: true
                current: songsTable.currentIndex === songRow.index && songsTable.activeFocus
                selected: songsTable.isSelected(songRow.index)
                playing: songRow.trackId.length > 0 && songRow.trackId === page.nowPlayingId
                favorite: songRow.model.favorite === true
                showFavorite: true
                showMenu: true
                verbsRevealed: songRow.hovered || songRow.favorite
                onActivated: modifiers => {
                    songsTable.forceActiveFocus(Qt.MouseFocusReason);
                    songsTable.activateAt(songRow.index, modifiers);
                }
                onFavoriteToggled: {
                    const item = page.itemAt(songRow.index);
                    if (item)
                        Actions.toggleFavorite(item);
                }
                onMenuRequested: (sceneX, sceneY) =>
                    songMenu.popupForItemNoDetails(page.itemAt(songRow.index), sceneX, sceneY)
            }
        }
    }

    Component {
        id: genresComponent

        StrmGrid {
            id: genresGrid

            navigationFocusKey: "musicBrowse-genres"
            navigationFocusFallbackItem: strip
            navigationFocusRefillActive: MusicBrowseCtl.genresLane.loading
            gridModel: MusicBrowseCtl.genres
            customCardWidth: Theme.crateSleeveSize
            customCardHeight: Theme.crateSleeveSize
            emptyText: ""
            prefetchThreshold: 24
            focus: genresGrid.count > 0
            KeyNavigation.up: pillBar.entryItem
            onNearEnd: MusicBrowseCtl.loadMore()
            onItemActivated: index => page.openItem(index)

            cardComponent: Component {
                GenreBinTile {
                    id: bin

                    property var model: null
                    property int index: -1

                    size: Theme.crateSleeveSize
                    isAllBin: false
                    name: bin.model && bin.model.name !== undefined ? String(bin.model.name) : ""
                    subtitle: bin.model && bin.model.subtitle !== undefined ? String(bin.model.subtitle) : ""
                    covers: bin.model && bin.model.covers ? bin.model.covers : []
                }
            }
        }
    }

    Component {
        id: playlistsComponent

        // The New playlist tile is not a row of the model (contract note 10),
        // so this view is a scope of tile + grid that answers the same view
        // API the Loader's other items do.
        FocusScope {
            id: playlistsScope

            readonly property alias count: playlistsGrid.count
            readonly property alias currentIndex: playlistsGrid.currentIndex

            function navigationFocusSnapshot(): var { return playlistsGrid.navigationFocusSnapshot(); }
            function restoreNavigationFocus(identity, index): bool {
                return playlistsGrid.restoreNavigationFocus(identity, index);
            }
            function restoreNavigationCursor(index): bool {
                return playlistsGrid.restoreNavigationCursor(index);
            }
            function pageBy(step): bool { return playlistsGrid.pageBy(step); }

            Item {
                id: newTile

                anchors.left: parent.left
                anchors.top: parent.top
                anchors.leftMargin: Theme.pageMarginValue
                width: Theme.scale(220)
                height: Theme.controlHeightLarge
                activeFocusOnTab: true
                focus: playlistsGrid.count === 0

                Accessible.role: Accessible.Button
                Accessible.name: qsTr("New playlist")
                Accessible.onPressAction: createPrompt.show()

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusChip
                    color: newHover.hovered ? Theme.hoverTint : "transparent"
                    border.width: 1
                    border.color: newTile.activeFocus ? Theme.accentColor : Theme.hairline
                }

                Row {
                    anchors.centerIn: parent
                    spacing: Theme.spacingTight

                    StrmIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "plus"
                        color: Theme.accentColor
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("New playlist")
                        color: Theme.textPrimaryColor
                        font.family: Theme.fontBody
                        font.pixelSize: Theme.fontBodySize
                    }
                }

                FocusRing {
                    active: newTile.activeFocus
                    radius: Theme.radiusChip
                }

                HoverHandler {
                    id: newHover
                    cursorShape: Qt.PointingHandCursor
                }

                TapHandler {
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: createPrompt.show()
                }

                Keys.onReturnPressed: event => {
                    if (!event.isAutoRepeat)
                        createPrompt.show();
                }
                Keys.onEnterPressed: event => {
                    if (!event.isAutoRepeat)
                        createPrompt.show();
                }
                Keys.onUpPressed: event => {
                    if (pillBar.entryItem)
                        pillBar.entryItem.forceActiveFocus(Qt.TabFocusReason);
                    event.accepted = true;
                }
                KeyNavigation.down: playlistsGrid
            }

            Text {
                anchors.left: newTile.right
                anchors.leftMargin: Theme.spacingValue
                anchors.verticalCenter: newTile.verticalCenter
                visible: MusicBrowseCtl.playlistsLane.ready && MusicBrowseCtl.playlistsLane.empty
                text: qsTr("No music playlists yet. Make one here, or add a record to one from its menu.")
                color: Theme.textTertiary
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontSmall
            }

            StrmGrid {
                id: playlistsGrid

                anchors.top: newTile.bottom
                anchors.topMargin: Theme.spacingValue
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                navigationFocusKey: "musicBrowse-playlists"
                navigationFocusFallbackItem: newTile
                navigationFocusRefillActive: MusicBrowseCtl.playlistsLane.loading
                gridModel: MusicBrowseCtl.playlists
                customCardWidth: Theme.crateSleeveSize
                customCardHeight: Theme.crateSleeveSize + Theme.scale(52)
                emptyText: ""
                prefetchThreshold: 30
                focus: playlistsGrid.count > 0
                KeyNavigation.up: newTile
                onNearEnd: MusicBrowseCtl.loadMore()
                onItemActivated: index => page.openItem(index)
                // The inline playlist card below declares no playRequested
                // signal either, so this had nothing to ever connect to.
                onMenuRequested: (index, mx, my) => musicMenu.popupForItem(page.itemAt(index), mx, my)

                cardComponent: Component {
                    Item {
                        id: playlistCard

                        property var model: null
                        property int index: -1
                        property bool current: false
                        property bool hovered: false

                        signal activated
                        signal menuRequested(real x, real y)

                        width: Theme.crateSleeveSize
                        height: Theme.crateSleeveSize + Theme.scale(52)

                        CoverCollage {
                            id: collage

                            size: Theme.crateSleeveSize
                            radius: Theme.crateSleeveRadius
                            covers: playlistCard.model && playlistCard.model.coverUrl
                                    ? [String(playlistCard.model.coverUrl)] : []

                            FocusRing {
                                active: playlistCard.current
                                radius: Theme.crateSleeveRadius
                            }
                        }

                        Column {
                            anchors.top: collage.bottom
                            anchors.topMargin: Theme.scale(8)
                            width: parent.width
                            spacing: Theme.scale(2)

                            Text {
                                width: parent.width
                                text: playlistCard.model && playlistCard.model.name !== undefined ? String(playlistCard.model.name) : ""
                                color: playlistCard.current || playlistCard.hovered ? Theme.textPrimaryColor : Theme.textSecondaryColor
                                font.family: Theme.fontBody
                                font.pixelSize: Theme.fontBodySize
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }

                            Text {
                                width: parent.width
                                text: playlistCard.model && playlistCard.model.subtitle !== undefined ? String(playlistCard.model.subtitle) : ""
                                color: Theme.textTertiary
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.fontCaption
                                elide: Text.ElideRight
                            }
                        }

                        TapHandler {
                            gesturePolicy: TapHandler.ReleaseWithinBounds
                            onTapped: playlistCard.activated()
                        }

                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            gesturePolicy: TapHandler.ReleaseWithinBounds
                            onTapped: (eventPoint, button) => {
                                const p = playlistCard.mapToItem(null, eventPoint.position.x, eventPoint.position.y);
                                playlistCard.menuRequested(p.x, p.y);
                            }
                        }
                    }
                }
            }
        }
    }

    Loader {
        id: viewLoader

        anchors.top: page.songsShown ? songSelection.bottom : pillBar.bottom
        anchors.topMargin: Theme.spacingValue
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: dividers.visible ? dividers.left : parent.right
        anchors.leftMargin: page.songsShown ? Theme.pageMarginValue : 0
        anchors.rightMargin: page.songsShown ? Theme.spacingValue : 0
        anchors.bottomMargin: page.songsShown ? Theme.spacingValue : 0
        active: page.viewReady
        // A Loader is a focus scope. Its claim follows the strip's, so the
        // keyboard is never parked in an empty view (see MusicPage history).
        focus: page.contentFocusable
        sourceComponent: page.loadedSection === "artists" ? artistsComponent
                       : page.loadedSection === "songs" ? songsComponent
                       : page.loadedSection === "genres" ? genresComponent
                       : page.loadedSection === "playlists" ? playlistsComponent
                       : albumsComponent
        onLoaded: Qt.callLater(page.restoreActiveView)
    }

    CrateDividers {
        id: dividers

        anchors.right: parent.right
        anchors.top: viewLoader.top
        anchors.bottom: parent.bottom
        anchors.rightMargin: Theme.spacingTight
        anchors.bottomMargin: Theme.spacingValue
        visible: MusicBrowseCtl.letterStripVisible
        // Ruling P3-R2: CrateDividers ties its own activeFocusOnTab to
        // `letters.length > 0`, and MusicBrowseCtl.letters is a fixed A–Z list
        // that is never empty — so without this override the dividers would go
        // on DECLARING a Tab stop while hidden (sort not Name, or
        // Genres/Playlists). Tab traversal skips the hidden strip either way;
        // the override is what keeps the property value honest for NavRail.
        // `|| activeFocus` for the reason spelled out at genrePill above:
        // measured, a shoulder-button section change hides this strip while it
        // still holds the keyboard, and the bare form can never settle after
        // that.
        activeFocusOnTab: dividers.visible || dividers.activeFocus
        // Same stranding shape: the sort leaving Name, or the section losing
        // its letters, can hide this control while it holds the keyboard.
        onVisibleChanged: Qt.callLater(page.recoverIfStranded)
        letters: MusicBrowseCtl.letters
        currentLetter: MusicBrowseCtl.letter
        onLetterChosen: letter => MusicBrowseCtl.toggleLetter(letter)
        Keys.onLeftPressed: event => {
            page.focusContent();
            event.accepted = true;
        }
    }

    // ── Page states ────────────────────────────────────────────────────────
    LoadingState {
        anchors.fill: viewLoader
        visible: page.laneLoading && page.shownCount === 0 && !page.playlistsShown
        shape: page.songsShown ? "list" : "grid"
    }

    EmptyState {
        id: sectionError

        anchors.fill: viewLoader
        visible: page.laneError.length > 0 && page.shownCount === 0
        severity: "error"
        iconName: "info"
        headline: qsTr("Couldn't load this section")
        body: page.laneError
        actionText: qsTr("Retry")
        actionIcon: "refresh"
        // A successful Retry (keyboard or otherwise) clears laneError and
        // hides this whole state — including its Retry button, which held
        // focus a moment ago.
        onVisibleChanged: Qt.callLater(page.recoverIfStranded)
        onActionTriggered: page.lane.retry()
    }

    EmptyState {
        id: unfilteredEmpty

        anchors.fill: viewLoader
        visible: page.lane !== null && page.lane.ready && page.lane.empty
                 && !MusicBrowseCtl.filtered && MusicBrowseCtl.letter.length === 0
                 && !page.playlistsShown
        iconName: "lib-music"
        headline: page.loadedSection === "artists" ? qsTr("No artists here")
                : page.loadedSection === "songs" ? qsTr("No songs here")
                : page.loadedSection === "genres" ? qsTr("No genres here")
                : qsTr("No records here")
        body: MusicBrowseCtl.section === "artists" && MusicBrowseCtl.artistMode === "albumArtists"
              ? qsTr("Nothing is filed under an album artist. Everyone who appears on a track is still listed.")
              : qsTr("Once your Emby server has scanned music into this library, it shows up here.")
        actionText: MusicBrowseCtl.section === "artists" && MusicBrowseCtl.artistMode === "albumArtists"
                    ? qsTr("Show everyone") : ""
        actionIcon: unfilteredEmpty.actionText.length > 0 ? "user" : ""
        // Two ways this can hide the "Show everyone" button under the
        // keyboard: the whole state hides (visible), or its own action button
        // hides because artistMode changed while the state stays on screen
        // (actionText, EmptyState's own visible condition for that button).
        onVisibleChanged: Qt.callLater(page.recoverIfStranded)
        onActionTextChanged: Qt.callLater(page.recoverIfStranded)
        onActionTriggered: MusicBrowseCtl.artistMode = "everyone"
    }

    // Narrowed to nothing: every active filter is repeated as its own way out.
    Column {
        id: noMatch

        anchors.centerIn: viewLoader
        width: Math.min(viewLoader.width - Theme.pageMarginValue * 2, Theme.scale(560))
        spacing: Theme.spacingValue
        visible: page.lane !== null && page.lane.ready && page.lane.empty
                 && (MusicBrowseCtl.filtered || MusicBrowseCtl.letter.length > 0)

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("No records match")
            color: Theme.textPrimaryColor
            font.family: Theme.fontDisplay
            font.pixelSize: Theme.fontHeading
            font.weight: Font.DemiBold
        }

        Flow {
            width: parent.width
            spacing: Theme.spacingTight

            StrmButton {
                id: clearGenreChip
                visible: MusicBrowseCtl.genreIds.length > 0
                // Ruling P3-R2: each chip here shows or hides on its own
                // condition inside an always-enabled row, so StrmButton's own
                // enabled-based activeFocusOnTab would go on DECLARING a Tab
                // stop for every filter that is not currently active. Tab
                // traversal skips the hidden chips regardless; the override
                // keeps the value right for NavRail.qml:43 and :98.
                //
                // These six are the sharpest case for `|| activeFocus` on the
                // page: each chip's onClicked clears exactly the filter its own
                // `visible` reads, so pressing one hides the button that is
                // holding the keyboard, in one step, every time. Measured: one
                // Qt refusal per press, and the stop stays DECLARED on an
                // invisible chip until that filter is set and cleared again.
                activeFocusOnTab: clearGenreChip.visible || clearGenreChip.activeFocus
                variant: "secondary"
                iconName: "close"
                text: MusicBrowseCtl.genrePillText
                onClicked: MusicBrowseCtl.clearGenres()
            }
            StrmButton {
                id: clearDecadeChip
                visible: MusicBrowseCtl.decadeAvailable && MusicBrowseCtl.decade !== 0
                activeFocusOnTab: clearDecadeChip.visible || clearDecadeChip.activeFocus
                variant: "secondary"
                iconName: "close"
                text: MusicBrowseCtl.decadePillText
                onClicked: MusicBrowseCtl.decade = 0
            }
            StrmButton {
                id: clearFormatChip
                visible: MusicBrowseCtl.formatAvailable && MusicBrowseCtl.format !== "any"
                activeFocusOnTab: clearFormatChip.visible || clearFormatChip.activeFocus
                variant: "secondary"
                iconName: "close"
                text: MusicBrowseCtl.formatPillText
                onClicked: MusicBrowseCtl.format = "any"
            }
            StrmButton {
                id: clearFavouritesChip
                visible: MusicBrowseCtl.favouritesOnly
                activeFocusOnTab: clearFavouritesChip.visible || clearFavouritesChip.activeFocus
                variant: "secondary"
                iconName: "close"
                text: qsTr("Favourites")
                onClicked: MusicBrowseCtl.favouritesOnly = false
            }
            StrmButton {
                id: clearUnplayedChip
                visible: MusicBrowseCtl.unplayedOnly
                activeFocusOnTab: clearUnplayedChip.visible || clearUnplayedChip.activeFocus
                variant: "secondary"
                iconName: "close"
                text: qsTr("Unplayed")
                onClicked: MusicBrowseCtl.unplayedOnly = false
            }
            StrmButton {
                id: clearLetterChip
                visible: MusicBrowseCtl.letter.length > 0
                activeFocusOnTab: clearLetterChip.visible || clearLetterChip.activeFocus
                variant: "secondary"
                iconName: "close"
                text: qsTr("Letter: %1").arg(MusicBrowseCtl.letter)
                onClicked: MusicBrowseCtl.letter = ""
            }
        }
    }

    // A page already on screen and the next one failed: say so in place.
    Rectangle {
        id: pagingBanner

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.pageMarginValue
        height: pagingRow.implicitHeight + Theme.spacingValue * 2
        visible: page.laneError.length > 0 && page.shownCount > 0
        // A successful Retry (see pagingRetry) clears laneError and hides
        // this banner along with the button that just held focus.
        onVisibleChanged: Qt.callLater(page.recoverIfStranded)
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
                width: pagingRow.width - Theme.iconSize - pagingRetry.width - pagingRow.spacing * 2
                text: qsTr("Couldn't load more: %1").arg(page.laneError)
                color: Theme.textSecondaryColor
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontSmall
                elide: Text.ElideRight
            }

            StrmButton {
                id: pagingRetry

                anchors.verticalCenter: parent.verticalCenter
                // Ruling P3-R2: this button's own visible/enabled stay true;
                // it is the enclosing banner's visible that hides it, which
                // StrmButton's enabled-based activeFocusOnTab never sees.
                // `|| pagingRetry.activeFocus` — not the banner's — because it
                // is this button that holds the keyboard when a successful
                // Retry clears laneError and takes the banner away with it.
                activeFocusOnTab: pagingBanner.visible || pagingRetry.activeFocus
                text: qsTr("Retry")
                iconName: "refresh"
                onClicked: page.lane.retry()
            }
        }
    }

    // ── Menus and overlays ─────────────────────────────────────────────────
    ItemMenu {
        id: songMenu

        allowAddToPlaylist: true
        onAddToPlaylistRequested: item => {
            if (page.idOf(item).length > 0)
                playlistPicker.show(page.nameOf(item), [page.idOf(item)]);
        }
    }

    ItemMenu {
        id: musicMenu

        profile: "musicBrowse"
        allowAddToPlaylist: true
        // A card's "Add to playlist": a playlist holds tracks, so the album's
        // tracks are collected first and arrive at onAlbumTracksCollected.
        onAddToPlaylistRequested: item => {
            if (page.idOf(item).length > 0)
                MusicBrowseCtl.collectAlbumTracks(page.idOf(item), page.nameOf(item));
        }
    }

    GenrePicker {
        id: genrePicker

        z: 700
        options: MusicBrowseCtl.genreOptions
        loading: MusicBrowseCtl.genreOptionsLoading
        failed: MusicBrowseCtl.genreOptionsFailed
        onGenresChosen: ids => MusicBrowseCtl.setGenres(ids)
        onRetryRequested: MusicBrowseCtl.ensureGenreOptions()
        onDismissed: genrePill.forceActiveFocus(Qt.OtherFocusReason)
    }

    PlaylistPicker {
        id: playlistPicker

        z: 800
        mediaType: "Audio"
        onDismissed: page.focusContent()
    }

    Item {
        id: createPrompt

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
            page.focusContent();
        }

        function commit(): void {
            const name = nameField.text.trim();
            if (name.length === 0)
                return;
            playlistPicker.pending = true;
            PlaylistCtl.create(name, [], "Audio");
            createPrompt.dismiss();
        }

        anchors.fill: parent
        z: 750
        visible: createPrompt.opened || createPrompt.opacity > 0.01
        enabled: createPrompt.opened
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
            subtitle: qsTr("A music playlist. It starts empty; add records and tracks from their menus.")

            TapHandler { gesturePolicy: TapHandler.ReleaseWithinBounds }

            StrmSearchField {
                id: nameField

                width: parent.width
                implicitHeight: Theme.controlHeightLarge
                placeholderText: qsTr("Name")
                onAccepted: createPrompt.commit()
                onEscapePressed: createPrompt.dismiss()
                KeyNavigation.down: createButton
            }

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

    // `pending` tells this page's result apart from a playlist edited elsewhere.
    Connections {
        target: PlaylistCtl

        function onActionSucceeded(message) {
            if (!playlistPicker.pending)
                return;
            playlistPicker.pending = false;
            browseToasts.show(message, "success");
        }
        function onActionFailed(message) {
            if (!playlistPicker.pending)
                return;
            playlistPicker.pending = false;
            browseToasts.show(message, "error");
        }
    }

    StrmToastHost {
        id: browseToasts

        anchors.fill: parent
        z: 900
    }
}
