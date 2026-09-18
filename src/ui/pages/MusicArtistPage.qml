pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import StrmQt

// MusicArtistPage — a poster and the filed releases (Crate spec §6.2).
//
// ArtistCtl splits the profile before it gets here: one model per tab, and
// only non-empty tabs listed; the top tracks come with their captions. The
// page lays those out and routes verbs: a sleeve opens through Actions,
// a sleeve's play button and the page's verbs go through MusicPlay/ArtistCtl,
// so every queue carries a source label.
//
// Nothing on this page formats a display string. The kicker, the tab labels
// and counts, the top-track captions and every badge are the controller's.
//
// ── Two layouts, one scroll each ───────────────────────────────────────────
// Wide, the page does not scroll: the hero is pinned at the top and the two
// columns beside each other scroll themselves. Narrow, the columns stack, so
// the *page* scrolls and neither column does — the release grid is given its
// full content height rather than a viewport slice, because a grid that
// scrolled inside a scrolling page would trap the wheel at its own bounds.
FocusScope {
    id: page

    // ── Contract ───────────────────────────────────────────────────────────
    // The route's artist map. Only itemId and name are read; everything shown
    // is ArtistCtl's.
    property var artistItem: ({})
    // The music library this artist was opened under, carried by the route so
    // it survives Back and eviction. Reopening with "" CLEARS the controller's
    // stored library (ArtistController::open assigns unconditionally, and that
    // clearing is its tested contract), and the page has no way to recover it,
    // so ensureOpen must never invent one.
    property string libraryId: ""

    readonly property string artistId: page.artistItem && page.artistItem.itemId !== undefined
                                       ? String(page.artistItem.itemId) : ArtistCtl.artistId
    readonly property string artistName: page.artistItem && page.artistItem.name !== undefined
                                         ? String(page.artistItem.name) : ArtistCtl.name
    // The controller is a singleton and this page can outlive its own artist
    // (Back to a second artist page while the first is still on the stack), so
    // every read of ArtistCtl is gated on it still holding OUR artist.
    readonly property bool mine: page.artistId.length > 0 && ArtistCtl.artistId === page.artistId
    readonly property bool hasTopTracks: page.mine && ArtistCtl.topTracks.count > 0
    readonly property bool hasReleases: page.mine && ArtistCtl.tabs.length > 0
    readonly property bool hasSimilar: page.mine && ArtistCtl.similar.count > 0
    // ── The fold, derived rather than chosen (rulings P4-R25, P4-R32) ──────
    // THE RULE, not the number: the two-column layout opens at exactly the
    // sleeve width the narrowest window this app can reach already shows, and
    // only widens from there. Everything below computes that; the threshold is
    // a consequence, and it is deliberately not written down as a literal.
    //
    // The column that steps across this fold is the release grid's sleeve area,
    // and each layout spends a fixed amount on everything that is not sleeves:
    // `foldedChrome` is StrmGrid's own left and right page gutter
    // (StrmGrid.qml:319-320); unfolded adds the page margin outside the side
    // strip, the strip itself, and `spacingLoose * 2` between the columns. The
    // difference is `foldStep`, so crossing the fold in either direction always
    // costs the reader that much. It cannot be removed by moving the threshold,
    // only relocated — the only way to lower the fold is to narrow the strip.
    // (At density 1.0: folded 96, unfolded 612, step 516, fold 1416, and the
    // brief's 1100 would have opened the two-column layout at 488px.)
    //
    // Ruling P4-R32 is why this is arithmetic and not a scaled literal.
    // `Theme.scale()` tracks `densityMode`, a live user setting (compact 0.9,
    // normal 1.0, tv 1.15), but Main.qml's `minimumWidth: 960` is a window
    // minimum in window pixels and is NOT scaled. A floor derived from a mix of
    // the two and then frozen as one pre-scaled number stops being a floor the
    // moment the user changes density — on the album page compact opened the
    // unfolded layout 96px BELOW its own floor. Recomputing per density keeps
    // the floor a floor: 1371 at compact, 1416 at normal, 1485 at tv, each
    // opening at exactly its own narrowest sleeve width (820 / 804 / 781).
    readonly property int foldedChrome: Theme.pageMarginValue * 2
    readonly property int foldStep: Theme.pageMarginValue + page.sideWidth
                                    + Theme.spacingLoose * 2
    readonly property int unfoldedChrome: page.foldedChrome + page.foldStep

    // Main.qml:26 `minimumWidth: 960`, unscaled and left that way on purpose,
    // less what the shell reserves for the collapsed rail. That reservation
    // mirrors NavRail.qml:23 (`touchTarget + spacingValue`) rather than reading
    // it, because collapsedWidth lives on a NavRail instance this page has no
    // handle on; if that formula ever changes, this must follow it.
    readonly property int narrowestPageWidth: 960 - (Theme.touchTarget + Theme.spacingValue)
    readonly property int floorColumnWidth: page.narrowestPageWidth - page.foldedChrome
    readonly property int foldWidth: page.floorColumnWidth + page.unfoldedChrome

    readonly property bool narrow: page.width < page.foldWidth
    readonly property int heroHeight: page.narrow ? Theme.scale(240) : Theme.scale(320)
    readonly property bool isActivePage: page.StackView.status === StackView.Active
                                         || page.StackView.view === null
    readonly property int sideWidth: Theme.scale(420)

    // The side column is a 420px strip only while there is a release column
    // beside it. An artist with top tracks and no filed releases used to leave
    // it pinned at the far right with two-thirds of the page blank beside it
    // and no empty state to explain the gap (EmptyState needs !hasTopTracks),
    // so it takes the whole width instead — the mirror of the rule
    // releasesColumn.width already applied in the other direction.
    readonly property bool sideAlone: page.narrow || !page.hasReleases

    // The shown tab, clamped: a profile with fewer tabs than the last one
    // must not index past the end.
    readonly property int tabCount: ArtistCtl.tabs.length
    readonly property int tabIndex: Math.max(0, Math.min(tabBar.currentIndex, page.tabCount - 1))
    readonly property string tabKey: page.tabCount > 0 ? String(ArtistCtl.tabs[page.tabIndex].key) : ""
    readonly property var tabModel: page.tabKey === "epsAndSingles" ? ArtistCtl.epsAndSingles
                                  : page.tabKey === "appearsOn" ? ArtistCtl.appearsOn
                                  : ArtistCtl.albums

    // Clamping `tabIndex` is not enough on its own: StrmTabBar highlights the
    // tab whose index equals its OWN currentIndex, so when a tab disappears
    // from under the cursor for the same artist the grid would show the
    // clamped tab while the bar showed no current tab at all. Write the clamp
    // back. Not folded into onShownArtistIdChanged, which deliberately fires
    // only on an artist change.
    onTabCountChanged: {
        if (page.tabCount > 0 && tabBar.currentIndex > page.tabCount - 1)
            tabBar.currentIndex = page.tabCount - 1
    }

    // ── Narrow-fold geometry ───────────────────────────────────────────────
    // The release grid's full content height, mirroring what StrmGrid's own
    // GridView computes (cellHeight is grid.cardHeight + Theme.spacingValue in
    // card mode, and the view carries no vertical margins). Derived here
    // because StrmGrid publishes `columns`, `cardHeight` and `count` but not
    // its contentHeight; verified equal to the real GridView.contentHeight
    // across counts and widths.
    readonly property int releaseRows: releases.count > 0
                                       ? Math.ceil(releases.count / Math.max(1, releases.columns))
                                       : 0
    readonly property int releaseGridHeight: page.releaseRows
                                             * (releases.cardHeight + Theme.spacingValue)

    // ── Only the page on screen may retarget the shared controller ──────────
    // Deferred by both callers below, so by the time it runs this page may not
    // be the one the user is looking at: pop() does NOT destroy the popped item
    // synchronously, so a page just navigated away from is still alive with a
    // call queued from its own visibleChanged, and that stale call lands AFTER
    // the shell has prepared the route it went back to.
    //
    // The test is `visible`, and that is measured, not assumed. `isActivePage`
    // does NOT work here: it is `status === Active || StackView.view === null`,
    // and pop() DETACHES the item, so a popped page has `view === null` and
    // reports itself active. Gating on attachment instead (`view !== null &&
    // status !== Active`) fails the same way and for the same reason — the
    // popped page is the detached one. StackView sets `visible: false` on both
    // the popped page and a covered one, while a page built outside any
    // StackView (the self-test) keeps `visible: true`, so this one property
    // separates all three cases. Measured for each: popped page returns early,
    // covered page returns early, standalone page proceeds, and the current
    // page still fires when the controller has moved behind its back.
    //
    // Refusing costs nothing, which is what makes `visible` safe rather than
    // merely narrow: `onVisibleChanged` below queues this again the moment the
    // page comes back on screen, so a covered page that was refused reopens
    // when it is uncovered. The guard delays a reopen; it never drops one.
    function ensureOpen(): void {
        if (!page.visible)
            return
        if (page.artistId.length > 0 && ArtistCtl.artistId !== page.artistId)
            ArtistCtl.open(page.artistId, page.artistName, page.libraryId)
    }

    Component.onCompleted: Qt.callLater(page.ensureOpen)
    onVisibleChanged: {
        if (page.visible)
            Qt.callLater(page.ensureOpen)
    }

    // A different artist starts on their first tab. Bound to the id, not
    // to `tabs`: a heart on this artist re-emits artistChanged and must not
    // throw the user back to Albums.
    readonly property string shownArtistId: ArtistCtl.artistId
    onShownArtistIdChanged: tabBar.currentIndex = 0

    // The pad's shoulders walk the release tabs; with one tab there is
    // nothing to walk and the shell's library cycle keeps them.
    function cycleTab(step): bool {
        const count = page.tabCount
        if (count < 2)
            return false
        tabBar.currentIndex = (page.tabIndex + step + count) % count
        return true
    }

    function albumAt(index) {
        const model = page.tabModel
        return model && index >= 0 && index < model.count ? model.get(index) : null
    }

    // ── Focus recovery (MusicBrowsePage's shape) ───────────────────────────
    // Qt clears active focus when the focused item goes `enabled: false`, but
    // never when it merely goes `visible: false`. On this page the tab bar,
    // the sleeve grid, the top-track table and the similar strip all hide on a
    // condition that has nothing to do with `enabled` — a profile that loads
    // with no releases, a window narrowed past the fold — so each can strand
    // the keyboard on an item nobody can see. One page-wide watcher catches all
    // of them, plus any control added later.
    function isStranded(): bool {
        const focused = page.Window.activeFocusItem
        return page.activeFocus && focused !== null && focused.visible === false
    }

    // Landings, nearest-context first: the hero verbs (always on screen, and
    // interactive whenever the profile is ours), then the releases, then the
    // top tracks. When the page is showing only its empty state there is
    // nothing to hold the keyboard and the scope itself takes it back, which
    // at least keeps Tab traversal starting from here.
    function recoverStranded(): bool {
        if (shuffleButton.interactive) {
            shuffleButton.forceActiveFocus(Qt.OtherFocusReason)
            return true
        }
        if (releases.visible) {
            releases.forceActiveFocus(Qt.OtherFocusReason)
            return true
        }
        if (topTracks.visible) {
            topTracks.forceActiveFocus(Qt.OtherFocusReason)
            return true
        }
        page.forceActiveFocus(Qt.OtherFocusReason)
        return false
    }

    function recoverIfStranded(): void {
        if (page.isStranded())
            page.recoverStranded()
    }

    // The binding re-reads both the focused item AND that item's own
    // `visible`, so it re-evaluates exactly when focus can strand. Deferred
    // because the handler runs before the sibling bindings that decide where
    // focus should land have caught up.
    readonly property bool focusedItemVisible: {
        const focused = page.Window.activeFocusItem
        return focused === null || focused.visible === true
    }

    onFocusedItemVisibleChanged: {
        if (!page.focusedItemVisible)
            Qt.callLater(page.recoverIfStranded)
    }

    // Narrow, the tab bar and the grid can sit below the fold, so focus
    // arriving there has to bring the page with it. MusicHomePage's shape.
    function ensureVisible(item): void {
        if (!item || !item.visible || !pageScroll.interactive)
            return
        const top = item.mapToItem(content, 0, 0).y
        const bottom = top + item.height
        const maxY = Math.max(0, pageScroll.contentHeight - pageScroll.height)
        if (top - Theme.spacingValue < pageScroll.contentY)
            pageScroll.contentY = Math.max(0, Math.min(maxY, top - Theme.spacingValue))
        else if (bottom + Theme.spacingValue > pageScroll.contentY + pageScroll.height)
            pageScroll.contentY = Math.max(0, Math.min(maxY, bottom + Theme.spacingValue
                                                             - pageScroll.height))
    }

    // ── The music input context (spec §8) ──────────────────────────────────
    MappedShortcut {
        actionId: "music.playPause"
        fallback: ["Space"]
        active: App.interactionContext === "music" && page.isActivePage && PlayerCtl.active
        onActivated: PlayerCtl.togglePause()
    }

    MappedShortcut {
        actionId: "music.shuffleAll"
        fallback: ["S"]
        active: App.interactionContext === "music" && page.isActivePage && page.mine
        onActivated: ArtistCtl.shuffle()
    }

    // Ruling P4-R22: L hearts whatever the keyboard is on, so the gesture means
    // the same thing here as in the ⋯ menu of the thing under the cursor. A
    // focused track row hearts that track, a focused sleeve hearts that album,
    // and only with the keyboard elsewhere does it fall through to the artist
    // the page is about. Actions.toggleFavorite is the same entry point the
    // row's own ♥ and ItemMenu's "favorite" verb both end in
    // (ItemActions::toggleFavorite / performItemVerb both call setFavorite).
    MappedShortcut {
        actionId: "music.favorite"
        fallback: ["L"]
        active: App.interactionContext === "music" && page.isActivePage && page.mine
        onActivated: {
            if (topTracks.activeFocus) {
                const track = topTracks.rowAt(topTracks.currentIndex)
                if (track) {
                    Actions.toggleFavorite(track)
                    return
                }
            }
            if (releases.activeFocus) {
                const album = page.albumAt(releases.currentIndex)
                if (album) {
                    Actions.toggleFavorite(album)
                    return
                }
            }
            Actions.toggleFavorite(ArtistCtl.artistItem)
        }
    }

    MappedShortcut {
        actionId: "music.instantMix"
        fallback: ["R"]
        active: App.interactionContext === "music" && page.isActivePage && page.mine
        onActivated: ArtistCtl.radio()
    }

    // ── Body ───────────────────────────────────────────────────────────────
    // Wide this never scrolls (contentHeight is the viewport); narrow it is the
    // page's only scroll.
    Flickable {
        id: pageScroll

        anchors.fill: parent
        contentWidth: pageScroll.width
        contentHeight: content.height
        interactive: pageScroll.contentHeight > pageScroll.height
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        ScrollBar.vertical: StrmScrollBar {}

        Item {
            id: content

            width: pageScroll.width

            readonly property int stackBottom: releasesColumn.visible
                ? releasesColumn.y + releasesColumn.height
                : (side.visible ? side.y + side.height : page.heroHeight)

            // Nothing inside reads this, so the sum is not circular: every
            // child's height comes from the hero constant, its own content, or
            // the viewport.
            height: page.narrow
                    ? Math.max(pageScroll.height, content.stackBottom + Theme.pageMarginValue)
                    : pageScroll.height

            // ── Hero ───────────────────────────────────────────────────────
            Item {
                id: hero

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: page.heroHeight
                clip: true

                // The artist's colour, always: it is what shows while the
                // backdrop loads, and all there is when the server has no
                // backdrop.
                CoverWash {
                    anchors.fill: parent
                    source: ArtistCtl.coverUrl
                }

                StrmImage {
                    id: backdrop

                    anchors.fill: parent
                    visible: Prefs.backdropEnabled && ArtistCtl.backdropUrl.length > 0
                    opacity: Prefs.backdropEnabled ? Prefs.backdropOpacity / 100 : 0
                    source: backdrop.visible ? ArtistCtl.backdropUrl : ""
                    sourceSize.width: Theme.scale(1600)
                }

                // The scrim that lets the name read over any photograph, ending
                // in the page ground so the hero has no hard lower edge.
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "transparent" }
                        GradientStop { position: 0.35; color: Qt.rgba(Theme.ground.r, Theme.ground.g, Theme.ground.b, 0.35) }
                        GradientStop { position: 1.0; color: Theme.ground }
                    }
                }

                // Anchored rather than a Row. Both children are bottom aligned,
                // and a Row prints no warning for that in Qt 6.11.2 — measured,
                // both forms give identical geometry. What the rewrite buys is
                // that the name column no longer computes its own width as
                // `parent.width - (portrait.width + spacing)` — a Row child
                // reading the width the Row derives from its children — and that
                // the height rule is stated once, here, instead of being an
                // emergent property of the positioner.
                Item {
                    id: heroContent

                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: Theme.pageMarginValue
                    anchors.rightMargin: Theme.pageMarginValue
                    anchors.bottomMargin: Theme.spacingLoose
                    height: Math.max(portrait.visible ? portrait.height : 0, heroText.height)

                    // The portrait only when a backdrop took the hero; without
                    // one the wash is already the artist's picture, and a second
                    // copy is noise.
                    CratePortrait {
                        id: portrait

                        anchors.left: parent.left
                        anchors.bottom: parent.bottom
                        visible: backdrop.visible && ArtistCtl.coverUrl.length > 0 && !page.narrow
                        imageUrl: ArtistCtl.coverUrl
                        name: ""
                        subtitle: ""
                        size: Theme.cratePortraitSize
                    }

                    Column {
                        id: heroText

                        anchors.left: portrait.visible ? portrait.right : parent.left
                        anchors.leftMargin: portrait.visible ? Theme.spacingLoose : 0
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        spacing: Theme.spacingTight

                        CrateKicker {
                            width: parent.width
                            text: ArtistCtl.kicker
                            visible: text.length > 0
                        }

                        CrateHeading {
                            width: parent.width
                            text: ArtistCtl.name.length > 0 ? ArtistCtl.name : page.artistName
                            pixelSize: Theme.crateHeroArtist
                        }

                        Flow {
                            width: parent.width
                            spacing: Theme.spacingTight

                            StrmButton {
                                id: shuffleButton

                                text: qsTr("Shuffle artist")
                                iconName: "shuffle"
                                variant: "primary"
                                enabled: page.mine
                                onClicked: ArtistCtl.shuffle()

                                // The page's resting focus. A fresh open must
                                // leave the keyboard here so that arriving at an
                                // artist and pressing Return plays them; the
                                // grid takes it only when the navigation-focus
                                // restorer has a remembered position, and it
                                // takes it by forcing focus, which beats this.
                                focus: true

                                KeyNavigation.right: radioButton
                                KeyNavigation.down: page.narrow && page.hasTopTracks ? topTracks
                                                    : (page.narrow && page.hasSimilar ? similarStrip
                                                                                      : tabBar)
                            }

                            StrmButton {
                                id: radioButton

                                text: qsTr("Artist radio")
                                iconName: "audio-track"
                                enabled: page.mine
                                onClicked: ArtistCtl.radio()

                                KeyNavigation.left: shuffleButton
                                KeyNavigation.right: favouriteButton
                                KeyNavigation.down: page.narrow && page.hasTopTracks ? topTracks
                                                    : (page.narrow && page.hasSimilar ? similarStrip
                                                                                      : tabBar)
                            }

                            StrmIconButton {
                                id: favouriteButton

                                iconName: ArtistCtl.favourite ? "heart-filled" : "heart"
                                checked: ArtistCtl.favourite
                                enabled: page.mine
                                tooltip: ArtistCtl.favourite ? qsTr("Remove from favourites")
                                                             : qsTr("Add to favourites")
                                shortcut: "L"
                                onClicked: Actions.toggleFavorite(ArtistCtl.artistItem)

                                KeyNavigation.left: radioButton
                                KeyNavigation.down: page.narrow && page.hasTopTracks ? topTracks
                                                    : (page.narrow && page.hasSimilar ? similarStrip
                                                                                      : tabBar)
                            }
                        }
                    }
                }
            }

            // ── Side column: Most played, Similar artists ─────────────────
            // Wide beside the releases: a 420px strip that scrolls itself.
            // Narrow, or wide with no releases to sit beside: full width, its
            // own height, scrolled by the page.
            //
            // Ruling P4-R30: Similar artists stacks under Most played when the
            // page folds, it does not disappear. The brief said "Similar folds
            // away", written when the fold sat at 1100; deriving the fold at
            // 1416 (ruling P4-R25) changed what that sentence denotes — it would
            // have hidden a content shelf on any window narrower than ~1476px,
            // an ordinary laptop. The intent behind the sentence is that Similar
            // is secondary, and stacking it below Most played on a page that
            // already scrolls honours that at no cost.
            Flickable {
                id: side

                x: page.sideAlone ? Theme.pageMarginValue
                                  : page.width - Theme.pageMarginValue - page.sideWidth
                y: hero.y + hero.height + Theme.spacingValue
                width: page.sideAlone ? Math.max(0, page.width - Theme.pageMarginValue * 2)
                                      : page.sideWidth
                height: page.narrow ? sideColumn.implicitHeight
                                    : Math.max(0, pageScroll.height - side.y)
                visible: page.mine && (page.hasTopTracks || page.hasSimilar)
                contentHeight: sideColumn.implicitHeight + Theme.spacingLoose
                interactive: !page.narrow
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                onVisibleChanged: Qt.callLater(page.recoverIfStranded)

                Column {
                    id: sideColumn

                    width: side.width
                    spacing: Theme.spacingValue

                    CrateHeading {
                        width: parent.width
                        visible: page.hasTopTracks
                        text: qsTr("Most played")
                    }

                    CrateTrackTable {
                        id: topTracks

                        navigationFocusKey: "artist-top-tracks"
                        navigationFocusFallbackItem: shuffleButton
                        navigationFocusRefillActive: ArtistCtl.loading
                        visible: page.hasTopTracks
                        model: ArtistCtl.topTracks

                        width: parent.width
                        height: page.hasTopTracks ? topTracks.count * topTracks.rowHeight : 0
                        interactive: false
                        showCovers: true
                        numberFromIndex: true
                        captions: ArtistCtl.topTrackCaptions
                        verbsColumn: Theme.scale(64)
                        durationColumn: Theme.scale(56)

                        // A list is one tab stop (constraints, §focus), and the
                        // album page's record already is one — the two pages have
                        // to agree. `TrackTable` is a bare ListView, whose
                        // activeFocusOnTab defaults to false, so the tab stop has
                        // to be declared here. The `|| activeFocus` half is the
                        // measured Qt rule, not caution: `visible` here follows
                        // page.hasTopTracks and can flip while this table is
                        // enabled AND holds the keyboard, and Qt refuses to clear
                        // activeFocusOnTab on the item that holds focus — it
                        // warns, keeps `true`, and never re-evaluates, so the
                        // property stays wrong for the session. Tab itself is not
                        // the victim: Qt's traversal skips an invisible item
                        // whatever this says (measured). `NavRail.qml:43,98` reads
                        // `activeFocusOnTab` off its children to build its own
                        // navigation set, and that read is what a stale `true`
                        // corrupts.
                        activeFocusOnTab: topTracks.visible || topTracks.activeFocus

                        onVisibleChanged: Qt.callLater(page.recoverIfStranded)
                        onActiveFocusChanged: {
                            if (topTracks.activeFocus)
                                page.ensureVisible(topTracks)
                        }

                        KeyNavigation.up: shuffleButton
                        KeyNavigation.down: similarStrip
                        KeyNavigation.left: page.narrow ? null : releases

                        onActivated: index => ArtistCtl.playTopTrack(index)
                        onMenuRequested: (index, mx, my) =>
                            trackMenu.popupForItemNoDetails(topTracks.rowAt(index), mx, my)
                    }

                    Item {
                        width: 1
                        height: Theme.spacingValue
                        visible: page.hasSimilar
                    }

                    CrateHeading {
                        width: parent.width
                        visible: page.hasSimilar
                        text: qsTr("Similar artists")
                    }

                    // Ruling P4-R21. CratePortrait is a plain Item with no
                    // activeFocusOnTab and no Keys, and it stays that way — every
                    // other consumer draws it inside a rail or a grid that owns
                    // the keyboard for it. Here there is no such owner, so the
                    // strip is it, following the contract DetailsPage's ChipStrip
                    // set and LinerNotes already copied: ONE tab stop for the
                    // whole strip, the strip owns Left/Right and Return, and each
                    // portrait shows the cursor through `current` without ever
                    // taking focus itself. A Flow rather than ChipStrip's
                    // horizontal ListView because portraits wrap down a narrow
                    // panel instead of scrolling sideways.
                    Flow {
                        id: similarStrip

                        // Which portrait the keyboard is on. Never driven by
                        // hover: hover previews, the ring follows the keyboard.
                        property int currentSimilar: 0

                        width: parent.width
                        visible: page.hasSimilar
                        spacing: Theme.spacingValue

                        // Same measured Qt rule as the table above: this strip
                        // hides on the fold and on the similar count, either of
                        // which can happen while it holds the keyboard.
                        activeFocusOnTab: similarStrip.visible || similarStrip.activeFocus

                        readonly property int similarCount: ArtistCtl.similar.count

                        // A new artist's strip is shorter than the last one's;
                        // the cursor must not point past the end.
                        onSimilarCountChanged: {
                            if (similarStrip.currentSimilar > similarStrip.similarCount - 1)
                                similarStrip.currentSimilar = Math.max(0, similarStrip.similarCount - 1)
                        }

                        function moveCursor(step: int): void {
                            const count = similarStrip.similarCount
                            if (count === 0)
                                return
                            similarStrip.currentSimilar =
                                Math.max(0, Math.min(count - 1, similarStrip.currentSimilar + step))
                        }

                        function activateCurrent(): void {
                            const row = similarStrip.currentSimilar
                            if (row < 0 || row >= similarStrip.similarCount)
                                return
                            const artist = ArtistCtl.similar.get(row)
                            if (!artist || artist.itemId === undefined)
                                return
                            Actions.openArtist(String(artist.itemId), String(artist.name))
                        }

                        // Hand the keyboard on rather than strand it, and defer:
                        // the sibling bindings that decide where focus should
                        // land have not settled when this fires.
                        function releaseFocus(): void {
                            if (similarStrip.visible || !similarStrip.activeFocus)
                                return
                            page.recoverStranded()
                        }

                        onVisibleChanged: {
                            if (!similarStrip.visible && similarStrip.activeFocus)
                                Qt.callLater(similarStrip.releaseFocus)
                        }
                        onActiveFocusChanged: {
                            if (similarStrip.activeFocus)
                                page.ensureVisible(similarStrip)
                        }

                        KeyNavigation.up: topTracks
                        KeyNavigation.down: page.narrow ? tabBar : null
                        KeyNavigation.left: page.narrow ? null : releases

                        // Clamped, not wrapped, and accepted at both ends — the
                        // same thing ChipStrip's keyNavigationWraps: false
                        // ListView does, so Left/Right feel identical on all
                        // three surfaces.
                        Keys.onLeftPressed: event => {
                            similarStrip.moveCursor(-1)
                            event.accepted = true
                        }
                        Keys.onRightPressed: event => {
                            similarStrip.moveCursor(1)
                            event.accepted = true
                        }
                        Keys.onReturnPressed: event => {
                            if (!event.isAutoRepeat)
                                similarStrip.activateCurrent()
                        }
                        Keys.onEnterPressed: event => {
                            if (!event.isAutoRepeat)
                                similarStrip.activateCurrent()
                        }

                        Repeater {
                            model: ArtistCtl.similar

                            delegate: CratePortrait {
                                id: similarArtist

                                // `model`, not one required property per role:
                                // CratePortrait already declares `name` and
                                // `subtitle`, and re-declaring them would shadow
                                // the control's own properties.
                                required property var model
                                required property int index

                                imageUrl: String(similarArtist.model.coverUrl)
                                name: String(similarArtist.model.name)
                                subtitle: String(similarArtist.model.subtitle)
                                size: Theme.scale(120)
                                current: similarArtist.index === similarStrip.currentSimilar
                                         && similarStrip.activeFocus

                                // A click commits AND makes this portrait the
                                // keyboard's place, so a following arrow key
                                // continues from here.
                                onActivated: {
                                    similarStrip.currentSimilar = similarArtist.index
                                    similarStrip.forceActiveFocus(Qt.MouseFocusReason)
                                    Actions.openArtist(String(similarArtist.model.itemId),
                                                       String(similarArtist.model.name))
                                }
                            }
                        }
                    }
                }
            }

            // ── Main column: release tabs over sleeves ────────────────────
            Item {
                id: releasesColumn

                // x is 0, not Theme.pageMarginValue, and that is deliberate:
                // StrmGrid anchors its own GridView with leftMargin and
                // rightMargin of Theme.pageMarginValue (StrmGrid.qml:319-320),
                // so the page gutter for the sleeves is already inside the
                // control. Indenting this column as well put the first sleeve at
                // x=125 while the hero text sat at 48 — measured, and a step
                // every other music page avoids by giving its StrmGrid the full
                // width. The tab bar is the one child that needs the gutter
                // drawn by hand, and it sets its own leftMargin below.
                x: 0
                y: page.narrow && side.visible ? side.y + side.height + Theme.spacingLoose
                                               : hero.y + hero.height + Theme.spacingValue
                width: page.narrow || !side.visible
                       ? page.width
                       : Math.max(0, side.x - Theme.spacingLoose * 2)
                // Narrow, the grid gets its whole content and the page scrolls.
                // Wide, it gets the rest of the viewport and scrolls itself.
                height: page.narrow
                        ? tabBar.height + Theme.spacingValue + page.releaseGridHeight
                        : Math.max(0, pageScroll.height - releasesColumn.y)
                visible: page.hasReleases

                onVisibleChanged: Qt.callLater(page.recoverIfStranded)

                StrmTabBar {
                    id: tabBar

                    anchors.left: parent.left
                    anchors.top: parent.top
                    // The column sits at x: 0 so that StrmGrid's own gutter is
                    // the page gutter for the sleeves; the bar is not inside the
                    // grid, so it draws that gutter itself. StrmTabBar has no
                    // left padding of its own (StrmTabBar.qml:60-63, :81-85), so
                    // without this the bar and its first tab label sit at x=0
                    // against sleeves at 48 — measured.
                    anchors.leftMargin: Theme.pageMarginValue

                    // StrmTabBar sets `activeFocusOnTab: true` unconditionally and
                    // nothing in it is tied to `visible`. This bar hides with its
                    // column whenever the artist has no filed releases, so without
                    // this it would still declare itself a stop while hidden.
                    //
                    // `|| activeFocus` is not belt and braces, it is the whole
                    // trick. Qt REFUSES to clear activeFocusOnTab on the item that
                    // currently holds focus ("Cannot set activeFocusOnTab to false
                    // once item is the active focus item") — it warns and keeps the
                    // old value, and since `visible` does not change again the
                    // binding never gets a second chance. Measured, not reasoned: a
                    // plain `: tabBar.visible` left this property `true` for the
                    // rest of the session. Tab traversal itself is safe either way
                    // — Qt skips invisible items whatever this says — but
                    // `NavRail.qml:43,98` reads `activeFocusOnTab` off its children
                    // to build its own navigation set, so the stale value is a read
                    // path, not just log noise. Holding it true until the page's
                    // recovery has moved focus away lets the binding settle a tick
                    // later, with no warning.
                    activeFocusOnTab: tabBar.visible || tabBar.activeFocus

                    tabs: ArtistCtl.tabs.map(tab => ({ text: String(tab.label), badge: Number(tab.count) }))

                    onActiveFocusChanged: {
                        if (tabBar.activeFocus)
                            page.ensureVisible(tabBar)
                    }

                    KeyNavigation.up: page.narrow && page.hasSimilar ? similarStrip
                                      : (page.narrow && page.hasTopTracks ? topTracks
                                                                          : shuffleButton)
                    KeyNavigation.down: releases
                    KeyNavigation.right: page.narrow ? null : topTracks
                }

                StrmGrid {
                    id: releases

                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: tabBar.bottom
                    anchors.bottom: parent.bottom
                    anchors.topMargin: Theme.spacingValue

                    navigationFocusKey: "artist-" + page.tabKey
                    navigationFocusFallbackItem: tabBar
                    navigationFocusRefillActive: ArtistCtl.loading
                    gridModel: page.tabModel
                    emptyText: ""
                    customCardWidth: Theme.crateSleeveSize
                    customCardHeight: Theme.crateSleeveSize + Theme.scale(52)

                    // NOT `page.hasReleases`. A fresh open belongs to the hero
                    // verbs; the grid claims the keyboard only while the
                    // navigation-focus restorer is putting the user back where
                    // Back took them from, and then keeps it (`|| activeFocus`)
                    // once the restore has landed — without that second half the
                    // scope's focus item would be cleared the moment `pending`
                    // went false, dropping focus straight after restoring it.
                    focus: releases.navigationFocusRestorePending || releases.activeFocus

                    onActiveFocusChanged: {
                        if (releases.activeFocus)
                            page.ensureVisible(releasesColumn)
                    }

                    KeyNavigation.up: tabBar
                    KeyNavigation.right: page.narrow ? null : topTracks

                    cardComponent: Component {
                        CrateSleeve {
                            id: sleeve

                            property var model: null
                            property int index: -1

                            size: Theme.crateSleeveSize
                            showCaption: true
                            coverUrl: sleeve.model && sleeve.model.coverUrl !== undefined
                                      ? String(sleeve.model.coverUrl) : ""
                            title: sleeve.model && sleeve.model.title !== undefined
                                   ? String(sleeve.model.title) : ""
                            // On an artist's own page the artist is known; the
                            // year (and the release badge) is what tells two
                            // sleeves apart.
                            subtitle: sleeve.model && Number(sleeve.model.year) > 0
                                      ? String(sleeve.model.year) : ""
                            badge: sleeve.model && sleeve.model.releaseBadge !== undefined
                                   ? String(sleeve.model.releaseBadge) : ""
                            hiRes: false
                        }
                    }

                    onItemActivated: index => {
                        const album = page.albumAt(index)
                        if (album)
                            Actions.openAlbum(String(album.itemId), String(album.title))
                    }
                    onItemPlayRequested: index => {
                        const album = page.albumAt(index)
                        if (album)
                            MusicPlay.playAlbum(String(album.itemId), String(album.title), 0)
                    }
                    onMenuRequested: (index, mx, my) => albumMenu.popupForItem(page.albumAt(index), mx, my)
                }
            }
        }
    }

    // ── States ─────────────────────────────────────────────────────────────
    // Outside the scroll: they are viewport states, and when either is showing
    // there is nothing to scroll anyway.
    LoadingState {
        anchors.left: parent.left
        anchors.right: parent.right
        y: page.heroHeight
        height: Math.max(0, page.height - page.heroHeight)
        visible: ArtistCtl.loading && !page.hasReleases && !page.hasTopTracks
        shape: "rails"
    }

    EmptyState {
        anchors.left: parent.left
        anchors.right: parent.right
        y: page.heroHeight
        height: Math.max(0, page.height - page.heroHeight)
        visible: !ArtistCtl.loading && !page.hasReleases && !page.hasTopTracks
        iconName: "user"
        severity: ArtistCtl.error.length > 0 ? "error" : "info"
        headline: page.artistId.length === 0 ? qsTr("No artist open")
                : ArtistCtl.error.length > 0 ? qsTr("Couldn't load this artist")
                : qsTr("Nothing filed under this artist")
        body: ArtistCtl.error.length > 0 ? ArtistCtl.error
            : page.artistId.length === 0 ? qsTr("Open an artist from your music library.")
            : qsTr("The server lists no releases or tracks for them.")
        actionText: page.artistId.length > 0 ? qsTr("Try again") : ""
        actionIcon: page.artistId.length > 0 ? "refresh" : ""
        onActionTriggered: ArtistCtl.retry()
    }

    // ── Menus and overlays ─────────────────────────────────────────────────
    ItemMenu {
        id: albumMenu
    }

    // The track menu the album page raises, minus "Details": for a track the
    // album page IS the details page.
    ItemMenu {
        id: trackMenu

        allowAddToPlaylist: true
        onAddToPlaylistRequested: item => {
            const id = (item && item.itemId !== undefined) ? String(item.itemId) : ""
            const name = (item && item.name !== undefined) ? String(item.name) : ""
            if (id.length > 0)
                playlistPicker.show(name, [id])
        }
    }

    PlaylistPicker {
        id: playlistPicker

        z: 800
        mediaType: "Audio"
        onDismissed: {
            // The table may have gone away under the overlay (a retry that
            // came back empty); handing focus to a hidden item is the bug
            // this page's recovery exists to prevent.
            if (topTracks.visible)
                topTracks.forceActiveFocus(Qt.OtherFocusReason)
            else
                page.recoverStranded()
        }
    }

    // `pending` is what tells this page's toast apart from a playlist edited
    // on some other surface: PlaylistCtl's results are global.
    Connections {
        target: PlaylistCtl

        function onActionSucceeded(message) {
            if (!playlistPicker.pending)
                return
            playlistPicker.pending = false
            artistToasts.show(message, "success")
        }
        function onActionFailed(message) {
            if (!playlistPicker.pending)
                return
            playlistPicker.pending = false
            artistToasts.show(message, "error")
        }
    }

    StrmToastHost {
        id: artistToasts

        anchors.fill: parent
        z: 900
    }
}
