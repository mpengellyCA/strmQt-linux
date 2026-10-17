pragma ComponentBehavior: Bound
import QtQuick
// For ApplicationWindow.window: playlist results are announced by the window.
import QtQuick.Controls.Basic
import StrmQt

// Home: vertical stack of rails (Continue Watching → Next Up → Libraries →
// Favorites → Latest per library → one per genre), built on the shared control
// library.
//
// Keyboard/gamepad behaviour is unchanged from the prototype: Up/Down switch
// rails, Left/Right move within one, Return opens. What is new is that the
// mouse exists — StrmRail carries hover chevrons, edge fades and wheel routing,
// StrmCard carries hover raise, overlay actions and right-click, and this page
// carries the context menu, the loading/empty/error surfaces, and the backdrop
// wash (ARCHITECTURE.md).
//
// Navigation contract: pages do not declare navigation signals. Item verbs go
// through `Actions`, which emits one normalized route request for Main.qml. The
// one exception is `openLibrary`, because ItemActions has no library verb.
FocusScope {
    id: page

    // Genre rails are asked for, not pushed: HomeController does not fetch them
    // from refresh(), so a user who never reaches them never pays for them.
    // Idempotent, so a re-entry to Home costs nothing.
    Component.onCompleted: {
        if (typeof HomeCtl !== "undefined" && typeof HomeCtl.loadGenreRails === "function")
            HomeCtl.loadGenreRails();
    }

    signal openLibrary(string libraryId, string name, string collectionType)

    // HomeCtl.rails is a stable keyed QAbstractListModel. Each child model can
    // reset or change count without replacing this vertical model, so sibling
    // rail delegates and their image trees survive content refreshes.
    readonly property bool hasContent: HomeCtl.rails.count > 0
    readonly property bool showLoading: HomeCtl.busy && !page.hasContent
    readonly property bool showError: !HomeCtl.busy && !page.hasContent
                                      && HomeCtl.errorMessage.length > 0
    readonly property bool showEmpty: !HomeCtl.busy && !page.hasContent
                                      && HomeCtl.errorMessage.length === 0

    // ── The stage ──────────────────────────────────────────────────────────
    // One item, featured above the rails: full-bleed art, its logo (or its
    // title), why it is here, and the two verbs that matter — play it, or read
    // about it. HomeController chooses it (Continue Watching, else Next Up,
    // else the newest arrival) from rails it has already fetched; nothing here
    // decides which.
    //
    // It does not rotate. The stage answers "what is next for me", and a
    // carousel turns that answer into a slideshow the user has to wait out
    // or chase, at ten feet, under a focus ring. It changes when the answer
    // does: a refresh, or a resume point moved anywhere.
    //
    // It is not a row of the rail list. The first rail's cell leaves room
    // above its shelf (`stageInset`) and the stage is drawn over that room,
    // scrolling with it (`stageY`), so it is a sibling of the list rather
    // than its descendant: the list's own Up/Down then never sees a key
    // pressed on the stage, and Down from the stage is the navigator's
    // ordinary step to the nearest shelf below — the first rail, on the
    // column the button sat in (NavigationColumn).
    //
    // The room is part of the first rail's cell, not a top margin, so the
    // list's highlight range needs no special case: that cell is taller than
    // the range, and the view brings an oversized current item to the top of
    // its range — which, for the first rail, is the stage whole on screen.
    // (A range that switched with the cursor was overtaken by the view's own
    // move animation: Down off the first rail left the second one below the
    // window.)
    readonly property var featured: HomeCtl.featured
    readonly property bool heroShown: page.hasContent && page.featured !== undefined
                                      && page.featured !== null
                                      && page.featured.itemId !== undefined
                                      && String(page.featured.itemId).length > 0
    readonly property var heroCapabilities: page.heroShown ? Actions.itemCapabilities(page.featured)
                                                           : ({})
    readonly property bool heroEpisode: page.heroCapabilities.episode === true
    // A share of the window, inside a floor and a ceiling. 0.42 is what still
    // leaves the first rail — a 16:9 shelf, which is what Continue Watching
    // and Next Up are — whole on screen below it at 1280×720 in TV density.
    readonly property int heroHeight: Math.round(Math.max(Theme.scale(240),
                                                          Math.min(Theme.scale(520),
                                                                   page.height * 0.42)))
    // How far the stage has scrolled. The first cell starts at the list's
    // top margin and the stage a margin above it, so the stage is at 0 while
    // the list is at its beginning.
    readonly property real stageY: page.heroShown
                                   ? railList.originY - railList.contentY - Theme.pageMarginValue
                                   : 0
    // While most of the stage is on screen the stage is the room, and the
    // focus wash behind the top of the page steps aside for it.
    readonly property bool stageInView: page.heroShown
                                        && page.stageY + page.heroHeight > page.heroHeight * 0.4
    // Photographic art behind type. Higher than a page backdrop's 0.35
    // (DetailsPage) because the stage's own scrims, not the image's opacity,
    // carry the text's contrast here.
    readonly property real stageArtOpacity: 0.6

    // Put the list back at its beginning, where the stage is whole.
    function restOnStage(): void {
        if (!page.heroShown)
            return
        railList.contentY = railList.originY - railList.topMargin
    }

    // The stage arriving (first load, or after an empty Home) grows the first
    // cell under a list that may have moved meanwhile. Unless the user has
    // gone somewhere, the page opens on the stage.
    // And one leaving (its item finished, Home emptied) with the keyboard on
    // it hands the keyboard to the rails: the page's scope would otherwise
    // keep its focus on a stage that is no longer there, and Return or a
    // Down back from the header would land on nothing.
    onHeroShownChanged: {
        if (page.heroShown) {
            if (railList.currentIndex <= 0)
                Qt.callLater(page.restOnStage)
        } else if (stage.focus) {
            railList.focus = true
        }
    }

    function formatRuntime(ms): string {
        if (!ms || ms <= 0)
            return ""
        const minutes = Math.round(ms / 60000)
        return minutes >= 60 ? Math.floor(minutes / 60) + " h " + (minutes % 60) + " min"
                             : minutes + " min"
    }

    function formatPosition(ms): string {
        const totalSeconds = Math.floor(ms / 1000)
        const hours = Math.floor(totalSeconds / 3600)
        const minutes = Math.floor((totalSeconds / 60) % 60)
        const seconds = totalSeconds % 60
        function pad(value) { return (value < 10 ? "0" : "") + value }
        return hours > 0 ? hours + ":" + pad(minutes) + ":" + pad(seconds)
                         : minutes + ":" + pad(seconds)
    }

    // Year · certificate · runtime, as the details page writes it.
    readonly property string heroMeta: {
        if (!page.heroShown)
            return ""
        const parts = []
        if (page.featured.year > 0)
            parts.push(String(page.featured.year))
        if (page.featured.officialRating)
            parts.push(String(page.featured.officialRating))
        if (page.featured.runtimeMs > 0)
            parts.push(page.formatRuntime(page.featured.runtimeMs))
        return parts.join("  ·  ")
    }

    // An episode is named by its series (the logo, or the title in its
    // place) and then by this line: "S5E15  ·  Granite State".
    readonly property string heroEpisodeLine: {
        if (!page.heroEpisode)
            return ""
        const code = MediaKinds.episodeCode(page.featured.parentIndexNumber,
                                            page.featured.indexNumber)
        const name = page.featured.name ? String(page.featured.name) : ""
        return code.length > 0 && name.length > 0 ? code + "  ·  " + name
             : code.length > 0 ? code : name
    }

    // BoundedNavigationStack cannot find a semantic owner that a virtual
    // ListView has not instantiated. Position the outer rail by its stable key;
    // the stack's next bounded retry then discovers the real StrmRail and lets
    // that control restore its card identity without issuing network work.
    function prepareNavigationFocusOwner(controlKey): bool {
        const prefix = "home-"
        const key = String(controlKey)
        if (!key.startsWith(prefix))
            return false
        const row = HomeCtl.rails.indexOfKey(key.slice(prefix.length))
        if (row < 0)
            return false
        page._railCursorPlaced = true
        railList.currentIndex = row
        railList.positionViewAtIndex(row, ListView.Contain)
        railList.forceLayout()
        return true
    }

    // ── Where the cursor starts ────────────────────────────────────────────
    // Rails land in whatever order their requests finish. The genre rails are
    // one cheap query each and routinely arrive before Continue Watching, and
    // the descriptor sync then inserts the earlier rails ABOVE them. ListView
    // keeps its current item across an insert or a move, so the cursor rode
    // down with the first rail to arrive and Home opened on "Action" (the first
    // genre alphabetically). Until something deliberately places the cursor — a
    // key, a click, a back-stack restore — it belongs to the top rail.
    property bool _railCursorPlaced: false
    property bool _settlingRailCursor: false

    function _settleRailCursor(): void {
        // The view applies model changes at its next layout, and until then
        // currentIndex still reads the pre-insert value.
        railList.forceLayout()
        if (page._railCursorPlaced || railList.count <= 0 || railList.currentIndex === 0)
            return
        page._settlingRailCursor = true
        railList.currentIndex = 0
        page._settlingRailCursor = false
    }

    Connections {
        target: railList.model
        function onRowsInserted() { Qt.callLater(page._settleRailCursor) }
        function onRowsMoved() { Qt.callLater(page._settleRailCursor) }
        function onRowsRemoved() { Qt.callLater(page._settleRailCursor) }
        function onModelReset() { Qt.callLater(page._settleRailCursor) }
    }

    // ── Item verbs ─────────────────────────────────────────────────────────
    // Rails hand back an index into their own model; every verb needs the item
    // map, so one helper resolves it and every handler stays a single line.
    function itemAt(model, index) {
        if (!model || index < 0 || index >= model.count)
            return null
        return model.get(index)
    }

    function activate(model, index) {
        const item = page.itemAt(model, index)
        if (item)
            Actions.openDetails(item)
    }

    function playItem(model, index) {
        const item = page.itemAt(model, index)
        if (item)
            Actions.play(item)
    }

    function togglePlayed(model, index) {
        const item = page.itemAt(model, index)
        if (item)
            Actions.togglePlayed(item)
    }

    function toggleFavorite(model, index) {
        const item = page.itemAt(model, index)
        if (item)
            Actions.toggleFavorite(item)
    }

    // ── Context menu (ARCHITECTURE.md) ────────────────────────────────────────
    // One menu for the whole page, retargeted per request: a Popup per card
    // would be one overlay item per delegate for something only ever open once.
    // The action list itself lives in ItemMenu, shared with every other page.
    function showMenu(model, index, sceneX, sceneY) {
        itemMenu.popupForItem(page.itemAt(model, index), sceneX, sceneY)
    }

    // ── Backdrop wash (ARCHITECTURE.md) ─────────────────────────────────────
    // The hovered-or-focused item's backdrop fills the top of the page, blurred
    // and desaturated at ~18 %, crossfading on Theme.animSlow. Two layers rather
    // than one re-sourced Image, so a change is a genuine crossfade instead of
    // a fade to black and back.
    //
    // Hover and focus are tracked separately and the one that moved *last*
    // wins, which is the only reading of ARCHITECTURE.md that serves both users:
    // the pointer drives the wash while it is over a card, and the moment an
    // arrow key moves the selection the keyboard takes it back — even if the
    // cursor is still resting where it was.
    //
    // `washHoverOwner` is the rail delegate that owns the pointer, so a rail can
    // only clear the hover if it is the rail that set it. Only one rail can be
    // hovered at a time, but delegates come and go underneath a stationary
    // cursor and an unguarded clear would blank the wash. The token is the
    // delegate object and not its index, because `index` is already reset to -1
    // by the time Component.onDestruction runs — an index-keyed guard could
    // never match there, which left the wash stuck on a rail that had gone.
    property string washFocusUrl: ""
    property string washHoverUrl: ""
    property Item washHoverOwner: null
    property bool washHoverWins: false

    readonly property string washUrl: page.washHoverWins ? page.washHoverUrl
                                                         : page.washFocusUrl

    property string washFront: ""
    property string washBack: ""
    property bool washFrontActive: true

    // Which source wins is claimed explicitly on every publish, never derived
    // from a changed-handler on the URLs. Deriving it is wrong in a case that
    // happens constantly: focus takes the wash with the cursor still resting on
    // a card, then the cursor moves to a *different card in the same rail* —
    // the hovered rail index has not changed, so a rail-index handler never
    // fires and the wash would ignore the mouse until it left the rail entirely.
    //
    // Writes are ordered so `washUrl` resolves exactly once per call: taking the
    // wash, the new URL lands before hover starts winning; releasing it, hover
    // stops winning before the URL is cleared. The other order fades through a
    // blank frame on the way back to focus.
    function claimFocus(isCurrent, url) {
        if (!isCurrent)
            return
        page.washFocusUrl = url
        page.washHoverWins = false
    }

    // First evaluation of a delegate's binding is not a focus *move*, so it
    // seeds the value without taking the wash away from the pointer.
    function seedFocus(isCurrent, url) {
        if (isCurrent)
            page.washFocusUrl = url
    }

    function publishHover(owner, hovering, url) {
        if (hovering) {
            page.washHoverUrl = url
            page.washHoverOwner = owner
            page.washHoverWins = true
        } else if (page.washHoverOwner === owner) {
            page.washHoverWins = false
            page.washHoverOwner = null
            page.washHoverUrl = ""
        }
    }

    onWashUrlChanged: {
        if (page.washUrl === (page.washFrontActive ? page.washFront : page.washBack))
            return
        if (page.washFrontActive) {
            page.washBack = page.washUrl
            page.washFrontActive = false
        } else {
            page.washFront = page.washUrl
            page.washFrontActive = true
        }
    }

    onHasContentChanged: {
        if (!page.hasContent) {
            page.washFocusUrl = ""
            page.washHoverUrl = ""
            page.washHoverOwner = null
        }
    }

    Item {
        id: wash

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Math.round(page.height * 0.42)
        z: -1
        clip: true
        // 0.18 was the hardcoded default and stays the shipped one (ARCHITECTURE.md
        // §2.8: a wash, not a wallpaper). Turning it off must also stop the
        // work, not just hide it — a blurred layer at opacity 0 is
        // still a full offscreen render every frame. It also steps aside
        // while the stage is on screen: the stage is the room then, and a
        // second, different picture blurred under it is mud.
        visible: Prefs.backdropEnabled && wash.opacity > 0.001
        opacity: Prefs.backdropEnabled && !page.stageInView ? Prefs.backdropOpacity / 100 : 0

        Behavior on opacity {
            NumberAnimation { duration: Theme.animNormalMs; easing.type: Theme.easeStandard }
        }
        // Ken Burns (ARCHITECTURE.md): a 40 s drift, slow enough to read as
        // depth rather than motion. The wrapper scales the already-blurred
        // child texture; the static Image is the layer input, so its expensive
        // blur is not invalidated on every animation frame.
        component WashLayer: Item {
            id: washLayer

            property alias source: sourceImage.source
            property bool shown: false

            anchors.fill: parent
            opacity: (washLayer.shown && sourceImage.status === Image.Ready) ? 1 : 0

            Behavior on opacity {
                NumberAnimation { duration: Theme.animSlow; easing.type: Theme.easeEmphasis }
            }

            // Reset to rest when switched off, so a re-shown layer does not
            // resume mid-drift at an arbitrary zoom.
            scale: 1.0
            SequentialAnimation on scale {
                running: Prefs.backdropKenBurns && wash.visible && washLayer.opacity > 0.01
                loops: Animation.Infinite
                alwaysRunToEnd: false
                NumberAnimation { from: 1.0; to: 1.08; duration: 40000; easing.type: Easing.InOutSine }
                NumberAnimation { from: 1.08; to: 1.0; duration: 40000; easing.type: Easing.InOutSine }
            }

            Image {
                id: sourceImage

                anchors.fill: parent
                sourceSize.width: Theme.scale(960)
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                cache: true

                // Rendered through the effect rather than into a hidden source
                // item: a hidden source renders nothing in some paint paths.
                // Keep the texture only while this crossfade side can be seen;
                // disabling backdrops or finishing a fade releases the layer.
                layer.enabled: wash.visible
                               && (washLayer.shown || washLayer.opacity > 0.001)
                layer.effect: StrmBackdropBlur {}
            }
        }

        WashLayer {
            source: page.washFront
            shown: page.washFrontActive
        }

        WashLayer {
            source: page.washBack
            shown: !page.washFrontActive
        }
    }

    // The wash has to end somewhere; a hard edge two-fifths down the page reads
    // as a bug, so it dissolves into the ground colour instead.
    Rectangle {
        anchors.fill: wash
        z: -1
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 0.5; color: "transparent" }
            GradientStop { position: 1.0; color: Theme.ground }
        }
    }

    // ── Stage art ──────────────────────────────────────────────────────────
    // Behind the rails (z −1, like the wash) so the art can run on under the
    // first shelf's heading and fade there, instead of stopping at a line. It
    // is all ground by the top of that shelf's cards: anything still showing
    // there would be cut by the shelf's own edge fades, which paint ground.
    // The colour of the room comes off the same picture through CoverWash,
    // already clamped in C++ (CoverTint); the picture itself is drawn crisp,
    // its legibility carried by the two scrims over it. Turning backdrops off
    // turns this off too: it is decoration, and Settings has one switch for it.
    Item {
        id: stageArt

        y: page.stageY
        width: page.width
        height: page.heroHeight + Theme.railGap + Theme.fontTitle + Theme.spacingValue
        z: -1
        visible: page.heroShown && Prefs.backdropEnabled && stageArt.y + stageArt.height > 0

        readonly property string url: page.heroShown && page.featured.artUrl
                                      ? String(page.featured.artUrl) : ""

        // Two layers, so a change of featured item is a crossfade rather
        // than a fade to the ground and back (the wash above does the same).
        property string front: ""
        property string back: ""
        property bool frontActive: true

        onUrlChanged: {
            if (stageArt.url === (stageArt.frontActive ? stageArt.front : stageArt.back))
                return
            if (stageArt.frontActive) {
                stageArt.back = stageArt.url
                stageArt.frontActive = false
            } else {
                stageArt.front = stageArt.url
                stageArt.frontActive = true
            }
        }
        Component.onCompleted: stageArt.front = stageArt.url

        CoverWash {
            anchors.fill: parent
            source: stageArt.url
        }

        component StageLayer: StrmImage {
            id: stageLayer

            property bool shown: false

            anchors.fill: parent
            // Faces and horizons sit high in fanart; a band this wide keeps
            // the top of the frame and lets the bottom go under the fade.
            verticalAlignment: Image.AlignTop
            readyOpacity: stageLayer.shown ? page.stageArtOpacity : 0
            fadeDuration: Theme.animSlow
            suppressWarnings: true
            visible: stageLayer.source.toString().length > 0
        }

        StageLayer {
            source: stageArt.front
            shown: stageArt.frontActive
        }

        StageLayer {
            source: stageArt.back
            shown: !stageArt.frontActive
        }

        // The type sits on the left: the ground comes in from that side far
        // enough to hold it, and the right is left to the picture.
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Theme.ground }
                GradientStop { position: 0.32; color: Theme.veil }
                GradientStop { position: 0.72; color: "transparent" }
            }
        }

        // Under the header at the top, into the ground at the bottom: the
        // stage is a lit band of the page, not a picture pasted onto it.
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: Theme.veil }
                GradientStop { position: 0.18; color: "transparent" }
                GradientStop { position: 0.55; color: "transparent" }
                GradientStop { position: 1.0; color: Theme.ground }
            }
        }
    }

    // ── Rails ──────────────────────────────────────────────────────────────
    ListView {
        id: railList

        anchors.fill: parent
        topMargin: Theme.pageMarginValue
        bottomMargin: Theme.pageMarginValue
        spacing: Theme.railGap
        // Focus follows content rather than visibility: hiding an item clears
        // its active focus and does not hand it back when it reappears, so the
        // rails stay visible-but-empty and only the focus moves.
        focus: page.hasContent
        model: HomeCtl.rails
        // Preserved verbatim from the prototype: no wrapping, and ApplyRange so
        // the focused rail is never jammed against the top or bottom edge.
        keyNavigationWraps: false
        highlightMoveDuration: Theme.animNormalMs
        preferredHighlightBegin: Theme.pageMarginValue
        preferredHighlightEnd: height * 0.6
        highlightRangeMode: ListView.ApplyRange
        cacheBuffer: Theme.scale(800)
        boundsBehavior: Flickable.StopAtBounds

        // Only rail-to-rail steps reach here: a rail answers its own Left and
        // Right, and declines Up and Down so this view can move between rails.
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Up || event.key === Qt.Key_Down
                    || event.key === Qt.Key_PageUp || event.key === Qt.Key_PageDown)
                page._railCursorPlaced = true
            event.accepted = false
        }

        // A screenful of shelves, for the pad's triggers on a page that has no
        // alphabet strip to jump by (Main.qml pageFocusedView finds this by
        // walking up from whatever holds the keyboard). Rails differ in height,
        // so the step is however many are on screen rather than a fixed count.
        function pageBy(step) {
            if (railList.count <= 0)
                return false
            // Clamped to zero: the view's top margin puts contentY BELOW the
            // first delegate while it is scrolled to the top, and a probe in
            // that margin finds no row at all.
            const top = Math.max(0, railList.contentY)
            const first = railList.indexAt(railList.contentX + 1, top + 1)
            const last = railList.indexAt(railList.contentX + 1,
                                          top + railList.height - 1)
            const span = (first >= 0 && last > first) ? last - first : 1
            const target = Math.max(0, Math.min(railList.count - 1,
                                                railList.currentIndex + step * span))
            if (target === railList.currentIndex)
                return false
            page._railCursorPlaced = true
            railList.currentIndex = target
            railList.positionViewAtIndex(target, ListView.Contain)
            return true
        }

        // The Libraries rail is a different shape from a media rail — 16:9
        // tiles over a LibraryListModel, whose roles (libraryId / imageUrl)
        // are not the media roles StrmRail's delegate reads. A ListView takes
        // one delegate for the whole view, so both live in the cell and exactly
        // one of them is visible, enabled and focusable at a time.
        delegate: FocusScope {
            id: cell

            required property int index
            required property string railKey
            required property string title
            required property var railModel
            required property bool library
            required property bool wide
            required property string genreId

            readonly property bool isLibraryRail: cell.library
            readonly property bool isCurrent: cell.ListView.isCurrentItem

            // Backdrop of the card this rail has *selected*. Only meaningful
            // while this is the current rail; publishFocus() enforces that.
            readonly property string focusBackdropUrl: {
                if (cell.isLibraryRail)
                    return ""
                const item = page.itemAt(cell.railModel, mediaRail.currentIndex)
                return (item && item.backdropUrl) ? item.backdropUrl : ""
            }

            // Backdrop of the card the pointer is *over*. Deliberately not
            // gated on isCurrent: hovering a rail that does not hold the
            // keyboard's place still lights the wash, and must not move focus.
            readonly property bool hovering: !cell.isLibraryRail
                                             && mediaRail.hoveredIndex >= 0
            readonly property string hoverBackdropUrl: {
                if (cell.isLibraryRail)
                    return ""
                const item = page.itemAt(cell.railModel, mediaRail.hoveredIndex)
                return (item && item.backdropUrl) ? item.backdropUrl : ""
            }

            // The stage's room, above the first rail's shelf (see the stage).
            readonly property int stageInset: cell.index === 0 && page.heroShown
                                              ? page.heroHeight + Theme.railGap - Theme.pageMarginValue
                                              : 0

            width: railList.width
            height: cell.stageInset + (cell.isLibraryRail ? libraryRail.height : mediaRail.height)
            focus: cell.isCurrent

            onIsCurrentChanged: page.claimFocus(cell.isCurrent, cell.focusBackdropUrl)
            onFocusBackdropUrlChanged: page.claimFocus(cell.isCurrent, cell.focusBackdropUrl)
            Component.onCompleted: page.seedFocus(cell.isCurrent, cell.focusBackdropUrl)

            // The vertical cursor follows focus wherever it lands inside this
            // rail — including on a card's ✓/♥/⋯, which are real buttons and
            // take focus on tap without ever activating the card. Leaving the
            // cursor behind is what made the next Down jump relative to the rail
            // the keyboard last visited rather than the one the user is on.
            // Focus only: hovering a rail still never moves the cursor onto it.
            onActiveFocusChanged: {
                if (!cell.activeFocus)
                    return
                // Focus arriving on a rail other than the current one was put
                // there on purpose (a click, a restore), not by an insert.
                if (cell.index !== railList.currentIndex && !page._settlingRailCursor)
                    page._railCursorPlaced = true
                railList.currentIndex = cell.index
            }

            onHoveringChanged: page.publishHover(cell, cell.hovering,
                                                 cell.hoverBackdropUrl)
            onHoverBackdropUrlChanged: page.publishHover(cell, cell.hovering,
                                                         cell.hoverBackdropUrl)
            // A destroyed delegate must not leave the page thinking this row
            // still owns the pointer.
            Component.onDestruction: page.publishHover(cell, false, "")

            StrmRail {
                id: mediaRail

                navigationFocusKey: cell.isLibraryRail ? "" : "home-" + cell.railKey
                navigationFocusRefillActive: HomeCtl.busy

                // No anchors: StrmRail binds its own width to parent.width, and
                // anchoring left/right from here would override that binding.
                y: cell.stageInset
                visible: !cell.isLibraryRail
                enabled: !cell.isLibraryRail
                focus: !cell.isLibraryRail
                title: cell.isLibraryRail ? "" : cell.title
                railModel: cell.isLibraryRail ? null : cell.railModel
                // Poster for every media rail: MediaItemModel.posterUrl is the
                // server's Primary image for every type, so a "still" variant
                // would only change which items get cropped, not whether any do.
                cardVariant: cell.wide ? "still" : "poster"
                emptyText: ""
                // A genre rail is a doorway: the shelf is a sample, "See all"
                // is the genre itself. StrmRail already shows the affordance on
                // hover *and* on keyboard focus, so it is not a mouse-only exit.
                showMore: cell.genreId.length > 0

                onMoreRequested: {
                    if (cell.genreId.length > 0)
                        Actions.browseGenre(cell.genreId, cell.title)
                }

                onItemActivated: index => page.activate(cell.railModel, index)
                onItemPlayRequested: index => page.playItem(cell.railModel, index)
                onItemPlayedToggled: index => page.togglePlayed(cell.railModel, index)
                onItemFavoriteToggled: index => page.toggleFavorite(cell.railModel, index)
                onMenuRequested: (index, mx, my) =>
                    page.showMenu(cell.railModel, index, mx, my)
            }

            // ── Libraries rail ─────────────────────────────────────────────
            FocusScope {
                id: libraryRail

                readonly property string navigationFocusKind: "rail"
                readonly property string navigationFocusKey:
                    cell.isLibraryRail ? "home-" + cell.railKey : ""
                readonly property bool navigationFocusRestorePending:
                    libraryNavigationFocus.pending
                property bool _navigationFocusWriting: false

                // Published by whichever tile is current, so Return does not
                // have to reach into ListView.currentItem for untyped roles.
                property var currentLibrary: null

                function navigationFocusSnapshot(): var {
                    return libraryNavigationFocus.snapshot()
                }
                function restoreNavigationFocus(identity, index): bool {
                    return libraryNavigationFocus.restore(identity, index)
                }
                function cancelNavigationFocusRestore(): void {
                    libraryNavigationFocus.cancel()
                }
                function cancelNavigationFocusForUser(): void {
                    if (!libraryRail._navigationFocusWriting)
                        libraryNavigationFocus.cancel()
                }
                function applyNavigationFocus(index): void {
                    libraryRail._navigationFocusWriting = true
                    if (index >= 0) {
                        libList.currentIndex = index
                        libList.positionViewAtIndex(index, ListView.Contain)
                    }
                    libList.forceActiveFocus(Qt.OtherFocusReason)
                    libraryRail._navigationFocusWriting = false
                }
                function applyNavigationFallback(): void {
                    let candidate = libList
                    for (let step = 0; step < 256; ++step) {
                        candidate = candidate.nextItemInFocusChain(true)
                        if (!candidate || candidate === libList)
                            return
                        let cursor = candidate
                        while (cursor && cursor !== libraryRail)
                            cursor = cursor.parent
                        if (cursor !== libraryRail) {
                            candidate.forceActiveFocus(Qt.OtherFocusReason)
                            return
                        }
                    }
                }

                NavigationFocusRestorer {
                    id: libraryNavigationFocus
                    model: cell.isLibraryRail ? cell.railModel : null
                    count: libList.count
                    currentIndex: libList.currentIndex
                    refillActive: HomeCtl.busy
                    onFocusRequested: index => libraryRail.applyNavigationFocus(index)
                    onFallbackRequested: libraryRail.applyNavigationFallback()
                }

                Connections {
                    target: libraryRail
                    function onActiveFocusChanged() {
                        if (!libraryRail.activeFocus)
                            libraryRail.cancelNavigationFocusForUser()
                    }
                }

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.topMargin: cell.stageInset
                visible: cell.isLibraryRail
                enabled: cell.isLibraryRail
                focus: cell.isLibraryRail
                height: libHeading.implicitHeight + Theme.spacingValue + libList.height

                // One hidden card is the source of truth for tile size, the
                // same trick StrmRail and StrmGrid use.
                StrmCard {
                    id: libMetrics
                    visible: false
                    enabled: false
                    variant: "backdrop"
                }

                function activateCurrent() {
                    const lib = libraryRail.currentLibrary
                    if (lib && lib.libraryId)
                        page.openLibrary(lib.libraryId, lib.name, lib.collectionType)
                }

                Text {
                    id: libHeading

                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.leftMargin: Theme.pageMarginValue
                    anchors.rightMargin: Theme.pageMarginValue
                    text: cell.isLibraryRail ? cell.title : ""
                    color: Theme.textPrimaryColor
                    font.family: Theme.fontDisplay
                    font.pixelSize: Theme.fontTitle
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                ListView {
                    id: libList

                    anchors.top: libHeading.bottom
                    anchors.topMargin: Theme.spacingValue
                    anchors.left: parent.left
                    anchors.right: parent.right
                    // Headroom so a focused tile's raise is not clipped.
                    height: libMetrics.implicitHeight
                            + Math.ceil(libMetrics.implicitHeight * (Theme.focusScale - 1))
                            + Theme.spacingTight

                    orientation: ListView.Horizontal
                    spacing: Theme.spacingValue
                    leftMargin: Theme.pageMarginValue
                    rightMargin: Theme.pageMarginValue
                    focus: true
                    clip: true
                    model: cell.isLibraryRail ? cell.railModel : null
                    boundsBehavior: Flickable.StopAtBounds
                    // Same wheel contract as StrmRail: a vertical wheel belongs
                    // to the page, not to a horizontal shelf.
                    flickableDirection: Flickable.HorizontalFlick
                    keyNavigationWraps: false
                    highlightMoveDuration: Theme.animFastMs
                    preferredHighlightBegin: Theme.pageMarginValue
                    preferredHighlightEnd: width / 2
                    highlightRangeMode: ListView.ApplyRange

                    delegate: Item {
                        id: libCell

                        required property int index
                        required property var model

                        readonly property bool isCurrent: libCell.ListView.isCurrentItem

                        width: tile.implicitWidth
                        height: libList.height

                        function publish() {
                            if (libCell.isCurrent)
                                libraryRail.currentLibrary = {
                                    libraryId: libCell.model.libraryId,
                                    name: libCell.model.name,
                                    collectionType: libCell.model.collectionType
                                }
                        }

                        onIsCurrentChanged: libCell.publish()
                        Component.onCompleted: libCell.publish()

                        StrmCard {
                            id: tile

                            anchors.centerIn: parent
                            variant: "backdrop"
                            imageUrl: libCell.model.imageUrl
                            label: libCell.model.name
                            // Play / watched / favourite are meaningless for a
                            // library view, so the tile is click-and-open only.
                            showOverlayActions: false
                            highlighted: libCell.isCurrent && libList.activeFocus

                            onActivated: {
                                libraryRail.cancelNavigationFocusForUser()
                                // A click commits the keyboard's place too, so a
                                // later arrow key continues from where it landed.
                                libList.currentIndex = libCell.index
                                libList.forceActiveFocus(Qt.MouseFocusReason)
                                page.openLibrary(libCell.model.libraryId, libCell.model.name,
                                                 libCell.model.collectionType)
                            }
                        }
                    }

                    // Guard isAutoRepeat: a held/stuck Return must not
                    // machine-gun activations.
                    Keys.onReturnPressed: event => {
                        libraryRail.cancelNavigationFocusForUser()
                        if (!event.isAutoRepeat) libraryRail.activateCurrent()
                    }
                    Keys.onEnterPressed: event => {
                        libraryRail.cancelNavigationFocusForUser()
                        if (!event.isAutoRepeat) libraryRail.activateCurrent()
                    }
                    Keys.onPressed: event => {
                        libraryRail.cancelNavigationFocusForUser()
                        NavigationColumn.noteFromKey(libList, event.key)
                    }

                    // The Libraries shelf is a second horizontal view on this
                    // page, so it keeps the column exactly as a StrmRail does.
                    onActiveFocusChanged: {
                        if (libList.activeFocus)
                            NavigationColumn.applyTo(libList)
                    }
                }
            }
        }
    }

    // ── Stage ──────────────────────────────────────────────────────────────
    // Over the list (it is drawn on the list's top margin) so its buttons take
    // the pointer; a press or a wheel turn anywhere else on it falls through
    // to the list underneath, which scrolls as if the stage were part of it.
    FocusScope {
        id: stage

        y: page.stageY
        width: page.width
        height: page.heroHeight
        visible: page.heroShown
        enabled: page.heroShown

        // Back-stack focus memory (BoundedNavigationStack): a semantic owner,
        // so coming back from the details page it opened lands on the button
        // that opened it — not on a child-index path through a page whose
        // rails are virtual.
        readonly property string navigationFocusKey: "home-hero"

        function navigationFocusSnapshot(): var {
            return {
                "valid": page.heroShown,
                "identity": page.heroShown ? String(page.featured.itemId) : "",
                "index": heroInfo.activeFocus ? 1 : 0
            }
        }
        // The featured item may have changed while the user was away (they
        // finished it); the stage is still where they were, so its button is
        // still the answer. Refused only when there is no stage to land on.
        function restoreNavigationFocus(identity, index): bool {
            if (!page.heroShown)
                return false
            const target = (index === 1 || !heroPlay.visible) ? heroInfo : heroPlay
            target.forceActiveFocus(Qt.OtherFocusReason)
            return true
        }

        Accessible.role: Accessible.Pane
        Accessible.name: heroTitle.title

        // On the stage, the list is at its beginning and its cursor on the
        // first rail: that is where Down goes, and the first rail as current
        // is what keeps the stage whole (see `stageInset`). The list does not
        // hold the keyboard here, so moving its cursor moves no focus.
        onActiveFocusChanged: {
            if (!stage.activeFocus)
                return
            page._railCursorPlaced = true
            if (railList.currentIndex !== 0 && railList.count > 0)
                railList.currentIndex = 0
            page.restOnStage()
        }

        // A vertical step keeps the column (NavigationColumn): the shelf below
        // lands on the card under the button the step started from. The key
        // itself is the navigator's.
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
                const from = heroInfo.activeFocus ? heroInfo : heroPlay
                const point = from.mapToItem(null, from.width / 2, 0)
                NavigationColumn.arm(point.x)
            }
            event.accepted = false
        }

        Column {
            id: stageText

            // Everything that is not the logo is a fixed height; the logo's
            // box takes what is left, between a floor and a ceiling, and the
            // synopsis is shown only when the box can keep a useful size
            // beside it. Bottom-aligned, so spare height goes to the picture.
            readonly property int fixedHeight: Theme.spacingLoose
                + heroKicker.implicitHeight + heroMetaRow.height
                + (heroEpisodeText.visible ? heroEpisodeText.implicitHeight + stageText.spacing : 0)
                + heroButtons.height + stageText.spacing * 3 + Theme.spacingValue
            readonly property int overviewHeight: Math.ceil(Theme.fontSmall * Theme.lineNormal * 2)
                                                  + stageText.spacing
            readonly property int room: page.heroHeight - stageText.fixedHeight
            readonly property bool overviewFits: heroOverview.text.length > 0
                && stageText.room - stageText.overviewHeight >= Theme.scale(72)
            readonly property int logoBox: Math.max(Theme.scale(56), Math.min(Theme.scale(150),
                stageText.room - (stageText.overviewFits ? stageText.overviewHeight : 0)))

            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.leftMargin: Theme.pageMarginValue
            width: Math.min(stage.width - Theme.pageMarginValue * 2, Theme.scale(760))
            spacing: Theme.spacingTight

            // Why this item: "Continue watching", "Up next", "New in Movies".
            // The booth gear label (mono, uppercase, tracked out) that the
            // details page puts over its fields.
            Text {
                id: heroKicker

                width: parent.width
                text: page.heroShown && page.featured.featuredLabel
                      ? String(page.featured.featuredLabel).toUpperCase() : ""
                color: Theme.textSecondaryColor
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontCaption
                font.letterSpacing: Theme.fontCaption * Theme.trackLabel
                textFormat: Text.PlainText
                elide: Text.ElideRight
            }

            TitleLogo {
                id: heroTitle

                width: parent.width
                // An episode is the series' to name: its logo, or its title.
                title: !page.heroShown ? ""
                       : page.heroEpisode && page.featured.seriesName
                         ? String(page.featured.seriesName)
                         : String(page.featured.name || "")
                logoUrl: page.heroShown && page.featured.logoUrl
                         ? String(page.featured.logoUrl) : ""
                logoMaxWidth: Theme.scale(480)
                logoMaxHeight: stageText.logoBox
                decodeMaxHeight: Theme.scale(150)
                // The column grows upward from the buttons, so the box can
                // hug the logo: what moves when it lands is the label above.
                reserveBox: false
                pixelSize: Theme.fontDisplaySize
                // Two lines only where the box has room for them.
                maximumLineCount: stageText.logoBox >= Math.round(Theme.fontDisplaySize * 2.4)
                                  ? 2 : 1
            }

            Text {
                id: heroEpisodeText

                width: parent.width
                visible: page.heroEpisodeLine.length > 0
                text: page.heroEpisodeLine
                color: Theme.textPrimaryColor
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontBodyLarge
                font.weight: Font.Medium
                textFormat: Text.PlainText
                elide: Text.ElideRight
            }

            Row {
                id: heroMetaRow

                spacing: Theme.spacingValue
                height: Math.ceil(Theme.fontBodySize * 1.4)

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: page.heroMeta.length > 0
                    text: page.heroMeta
                    color: Theme.textSecondaryColor
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontBodySize
                    textFormat: Text.PlainText
                }

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: page.heroShown && page.featured.communityRating > 0
                    spacing: Theme.scale(6)

                    StrmIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "star"
                        size: Theme.fontBodySize
                        color: Theme.accentColor
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: page.heroShown ? Number(page.featured.communityRating).toFixed(1) : ""
                        color: Theme.accentColor
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontBodySize
                    }
                }

                // How far in, for something already started: the same
                // accent line a card's progress bar is.
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: page.heroCapabilities.resume === true
                             && page.featured.runtimeMs > 0
                    spacing: Theme.spacingTight

                    Rectangle {
                        id: heroProgress

                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.scale(96)
                        height: Theme.scale(3)
                        radius: height / 2
                        color: Theme.hairline
                        Accessible.role: Accessible.ProgressBar
                        Accessible.name: qsTr("Progress")

                        Rectangle {
                            width: heroProgress.width
                                   * Math.max(0, Math.min(1, Number(page.featured.progress || 0)))
                            height: heroProgress.height
                            radius: heroProgress.radius
                            color: Theme.accentColor
                        }
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: page.heroShown
                              ? qsTr("%1 left").arg(page.formatRuntime(Math.max(0,
                                    page.featured.runtimeMs - page.featured.positionMs)))
                              : ""
                        color: Theme.textSecondaryColor
                        font.family: Theme.fontBody
                        font.pixelSize: Theme.fontSmall
                        textFormat: Text.PlainText
                    }
                }
            }

            Text {
                id: heroOverview

                width: Math.min(parent.width, Theme.scale(640))
                visible: stageText.overviewFits
                text: page.heroShown && page.featured.overview ? String(page.featured.overview) : ""
                color: Theme.textPrimaryColor
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontSmall
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                lineHeight: Theme.lineNormal
                maximumLineCount: 2
                elide: Text.ElideRight
            }

            Item { width: 1; height: Theme.spacingValue - stageText.spacing }

            Row {
                id: heroButtons

                spacing: Theme.spacingTight
                height: Theme.controlHeight

                // Resume decides as a card and the details page do: the
                // item's own capability (ItemActions), which is a playable
                // item with a resume point. The verb is Actions', the
                // position the one the item map carries.
                StrmButton {
                    id: heroPlay

                    visible: page.heroCapabilities.play === true
                    variant: "primary"
                    iconName: "play"
                    text: page.heroCapabilities.resume === true
                          ? qsTr("Resume %1").arg(page.formatPosition(page.featured.positionMs))
                          : qsTr("Play")
                    onClicked: Actions.play(page.featured)
                }

                StrmButton {
                    id: heroInfo

                    iconName: "info"
                    text: qsTr("More info")
                    onClicked: Actions.openDetails(page.featured)
                }
            }
        }
    }

    ItemMenu {
        id: itemMenu

        allowAddToPlaylist: true
        onAddToPlaylistRequested: item => page.addToPlaylist(item)
    }

    // ── Add to playlist ────────────────────────────────────────────────────
    // The shared type-and-pick panel, as on the details page, so a film or an
    // episode can be filed from where it was found. The keyboard
    // goes back to the shelves, on the card it was last on.
    function addToPlaylist(item): void {
        playlistPicker.showItem(item)
    }

    PlaylistPicker {
        id: playlistPicker

        z: 800
        // The result goes to the window's toast host: by the time the server
        // answers, this page may be covered, and a covered page shows nothing.
        notifiesWindow: true
        onDismissed: railList.forceActiveFocus(Qt.OtherFocusReason)
    }

    // ── Page states ────────────────────────────────────────────────────────
    LoadingState {
        anchors.fill: parent
        visible: page.showLoading
        shape: "rails"
    }

    EmptyState {
        anchors.fill: parent
        visible: page.showError
        focus: page.showError
        severity: "error"
        iconName: "info"
        headline: qsTr("Couldn't load your home screen")
        body: HomeCtl.errorMessage
        actionText: qsTr("Retry")
        actionIcon: "refresh"
        onActionTriggered: HomeCtl.refresh()
    }

    EmptyState {
        anchors.fill: parent
        visible: page.showEmpty
        focus: page.showEmpty
        iconName: "library"
        headline: qsTr("Nothing here yet")
        body: qsTr("Add some media to your Emby server, or refresh once it has finished scanning.")
        actionText: qsTr("Refresh")
        actionIcon: "refresh"
        onActionTriggered: HomeCtl.refresh()
    }
}
