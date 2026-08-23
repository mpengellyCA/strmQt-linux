pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects
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
// through `Actions` and Main.qml listens to Actions.detailsRequested once. The
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

    // ── Genre rails (ARCHITECTURE.md) ─────────────────────────────────────────
    // A genre is a destination, not a word under a poster: `HomeCtl.genreRails`
    // is the same {title, model} shape as `latestRails` plus the genre's own id,
    // and each one renders as an ordinary rail whose "See all" opens the full
    // genre through `Actions.browseGenre`.
    //
    // The property does not exist in HomeController yet — it is the one thing
    // this wave hands back (see the report; the exact contract is
    // `Q_PROPERTY(QVariantList genreRails ...)` with entries
    // {title, genreId, model}). It is read defensively rather than assumed,
    // because that is the difference between "the rails appear the day the
    // property lands" and "Home breaks today": an absent property on a context
    // object reads as undefined, and `railsData` must survive that. The same
    // capability-check pattern LibraryPage uses for `Prefs.libraryViewMode`.
    //
    // They are appended *last* deliberately. Home is the front door, the rail
    // ListView only builds what is near the viewport, and everything above them
    // is what the user came for — so genre rails cost nothing until scrolled to,
    // whatever order the fetches complete in.
    readonly property var genreRails: {
        const rails = HomeCtl.genreRails
        return (rails === undefined || rails === null) ? [] : rails
    }

    // Rail descriptors for the vertical list; rebuilt when models change shape.
    // `genreId` is empty for every rail that is not a genre, and its emptiness
    // is what decides whether the rail offers a "See all" at all — a "See all"
    // on Continue Watching would have nowhere to go.
    readonly property var railsData: {
        var rails = []
        if (HomeCtl.resume.count > 0)
            // Wide: these two are mostly episodes, whose art IS 16:9. A movie in
            // the same rail has a Thumb the server already provides.
            rails.push({ title: qsTr("Continue Watching"), model: HomeCtl.resume,
                         library: false, wide: true, genreId: "" })
        if (HomeCtl.nextUp.count > 0)
            rails.push({ title: qsTr("Next Up"), model: HomeCtl.nextUp,
                         library: false, wide: true, genreId: "" })
        if (HomeCtl.libraries.count > 0)
            rails.push({ title: qsTr("Libraries"), model: HomeCtl.libraries,
                         library: true, wide: false, genreId: "" })
        if (HomeCtl.favorites.count > 0)
            rails.push({ title: qsTr("Favorites"), model: HomeCtl.favorites,
                         library: false, wide: false, genreId: "" })
        for (var i = 0; i < HomeCtl.latestRails.length; ++i) {
            var rail = HomeCtl.latestRails[i]
            rails.push({ title: rail.title, model: rail.model,
                         library: false, wide: false, genreId: "" })
        }
        const genres = page.genreRails
        for (var g = 0; g < genres.length; ++g) {
            const genre = genres[g]
            if (!genre || !genre.model || genre.model.count === 0)
                continue
            const genreId = (genre.genreId === undefined || genre.genreId === null)
                            ? "" : String(genre.genreId)
            rails.push({ title: genre.title, model: genre.model,
                         library: false, wide: false, genreId: genreId })
        }
        return rails
    }

    readonly property bool hasContent: page.railsData.length > 0
    readonly property bool showLoading: HomeCtl.busy && !page.hasContent
    readonly property bool showError: !HomeCtl.busy && !page.hasContent
                                      && HomeCtl.errorMessage.length > 0
    readonly property bool showEmpty: !HomeCtl.busy && !page.hasContent
                                      && HomeCtl.errorMessage.length === 0

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
    // `washHoverRail` is the outer-list index of the rail that owns the pointer,
    // so a rail can only clear the hover if it is the rail that set it. Only one
    // rail can be hovered at a time, but delegates are recycled underneath a
    // stationary cursor and an unguarded clear would blank the wash.
    property string washFocusUrl: ""
    property string washHoverUrl: ""
    property int washHoverRail: -1
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

    function publishHover(railIndex, hovering, url) {
        if (hovering) {
            page.washHoverUrl = url
            page.washHoverRail = railIndex
            page.washHoverWins = true
        } else if (page.washHoverRail === railIndex) {
            page.washHoverWins = false
            page.washHoverRail = -1
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

    onRailsDataChanged: {
        if (!page.hasContent) {
            page.washFocusUrl = ""
            page.washHoverUrl = ""
            page.washHoverRail = -1
        }
    }

    Item {
        id: wash

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Math.round(page.height * 0.42)
        z: -1
        // 0.18 was the hardcoded default and stays the shipped one (ARCHITECTURE.md
        // §2.8: a wash, not a wallpaper). Turning it off must also stop the
        // work, not just hide it — a blurred MultiEffect layer at opacity 0 is
        // still a full offscreen render every frame.
        visible: Prefs.backdropEnabled && wash.opacity > 0.001
        opacity: Prefs.backdropEnabled ? Prefs.backdropOpacity / 100 : 0

        Behavior on opacity {
            NumberAnimation { duration: Theme.animNormalMs; easing.type: Theme.easeStandard }
        }
        // Rendered through the effect rather than into a hidden source item:
        // a hidden source renders nothing into its layer in some paint paths
        // (see the same note in StrmPanel).
        // Follows `visible` so switching the backdrop off actually stops the
        // offscreen render, rather than paying for a blurred layer nobody sees.
        layer.enabled: wash.visible
        layer.effect: MultiEffect {
            autoPaddingEnabled: false
            blurEnabled: true
            blur: 1.0
            blurMax: 48
            saturation: -0.55
        }

        // Ken Burns (ARCHITECTURE.md): a 40 s drift, slow enough to read as
        // depth rather than motion. Only ever a scale — a translation would
        // expose the crop edges of a PreserveAspectCrop fill.
        component WashLayer: Image {
            id: layer

            property bool shown: false

            anchors.fill: parent
            sourceSize.width: Theme.scale(960)
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            opacity: (layer.shown && layer.status === Image.Ready) ? 1 : 0

            Behavior on opacity {
                NumberAnimation { duration: Theme.animSlow; easing.type: Theme.easeEmphasis }
            }

            // Reset to rest when switched off, so a re-shown layer does not
            // resume mid-drift at an arbitrary zoom.
            scale: 1.0
            SequentialAnimation on scale {
                running: Prefs.backdropKenBurns && wash.visible && layer.opacity > 0.01
                loops: Animation.Infinite
                alwaysRunToEnd: false
                NumberAnimation { from: 1.0; to: 1.08; duration: 40000; easing.type: Easing.InOutSine }
                NumberAnimation { from: 1.08; to: 1.0; duration: 40000; easing.type: Easing.InOutSine }
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
        model: page.railsData
        // Preserved verbatim from the prototype: no wrapping, and ApplyRange so
        // the focused rail is never jammed against the top or bottom edge.
        keyNavigationWraps: false
        highlightMoveDuration: Theme.animNormalMs
        preferredHighlightBegin: Theme.pageMarginValue
        preferredHighlightEnd: height * 0.6
        highlightRangeMode: ListView.ApplyRange
        cacheBuffer: Theme.scale(800)
        boundsBehavior: Flickable.StopAtBounds

        // The Libraries rail is a different shape from a media rail — 16:9
        // tiles over a LibraryListModel, whose roles (libraryId / imageUrl)
        // are not the media roles StrmRail's delegate reads. A ListView takes
        // one delegate for the whole view, so both live in the cell and exactly
        // one of them is visible, enabled and focusable at a time.
        delegate: FocusScope {
            id: cell

            required property int index
            required property var modelData

            readonly property bool isLibraryRail: cell.modelData.library === true
            readonly property bool isCurrent: cell.ListView.isCurrentItem
            // Empty for every rail that is not a genre.
            readonly property string genreId: (cell.modelData.genreId === undefined
                                               || cell.modelData.genreId === null)
                                              ? "" : String(cell.modelData.genreId)

            // Backdrop of the card this rail has *selected*. Only meaningful
            // while this is the current rail; publishFocus() enforces that.
            readonly property string focusBackdropUrl: {
                if (cell.isLibraryRail)
                    return ""
                const item = page.itemAt(cell.modelData.model, mediaRail.currentIndex)
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
                const item = page.itemAt(cell.modelData.model, mediaRail.hoveredIndex)
                return (item && item.backdropUrl) ? item.backdropUrl : ""
            }

            width: railList.width
            height: cell.isLibraryRail ? libraryRail.height : mediaRail.height
            focus: cell.isCurrent

            onIsCurrentChanged: page.claimFocus(cell.isCurrent, cell.focusBackdropUrl)
            onFocusBackdropUrlChanged: page.claimFocus(cell.isCurrent, cell.focusBackdropUrl)
            Component.onCompleted: page.seedFocus(cell.isCurrent, cell.focusBackdropUrl)

            onHoveringChanged: page.publishHover(cell.index, cell.hovering,
                                                 cell.hoverBackdropUrl)
            onHoverBackdropUrlChanged: page.publishHover(cell.index, cell.hovering,
                                                         cell.hoverBackdropUrl)
            // A recycled delegate must not leave the page thinking this row
            // still owns the pointer.
            Component.onDestruction: page.publishHover(cell.index, false, "")

            StrmRail {
                id: mediaRail

                // No anchors: StrmRail binds its own width to parent.width, and
                // anchoring left/right from here would override that binding.
                visible: !cell.isLibraryRail
                enabled: !cell.isLibraryRail
                focus: !cell.isLibraryRail
                title: cell.isLibraryRail ? "" : cell.modelData.title
                railModel: cell.isLibraryRail ? null : cell.modelData.model
                // Poster for every media rail: MediaItemModel.posterUrl is the
                // server's Primary image for every type, so a "still" variant
                // would only change which items get cropped, not whether any do.
                cardVariant: cell.modelData.wide === true ? "still" : "poster"
                emptyText: ""
                // A genre rail is a doorway: the shelf is a sample, "See all"
                // is the genre itself. StrmRail already shows the affordance on
                // hover *and* on keyboard focus, so it is not a mouse-only exit.
                showMore: cell.genreId.length > 0

                onMoreRequested: {
                    if (cell.genreId.length > 0)
                        Actions.browseGenre(cell.genreId, cell.modelData.title)
                }

                onItemActivated: index => page.activate(cell.modelData.model, index)
                onItemPlayRequested: index => page.playItem(cell.modelData.model, index)
                onItemPlayedToggled: index => page.togglePlayed(cell.modelData.model, index)
                onItemFavoriteToggled: index => page.toggleFavorite(cell.modelData.model, index)
                onMenuRequested: (index, mx, my) =>
                    page.showMenu(cell.modelData.model, index, mx, my)
            }

            // ── Libraries rail ─────────────────────────────────────────────
            FocusScope {
                id: libraryRail

                // Published by whichever tile is current, so Return does not
                // have to reach into ListView.currentItem for untyped roles.
                property var currentLibrary: null

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
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
                    text: cell.isLibraryRail ? cell.modelData.title : ""
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
                    model: cell.isLibraryRail ? cell.modelData.model : null
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
                        if (!event.isAutoRepeat) libraryRail.activateCurrent()
                    }
                    Keys.onEnterPressed: event => {
                        if (!event.isAutoRepeat) libraryRail.activateCurrent()
                    }
                }
            }
        }
    }

    ItemMenu { id: itemMenu }

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

    // Home is a set of rails the user scrolls; a rebuild would move every one of
    // them. Same offer as the library grid: explicit, and never focus-stealing.
    UpdateBanner {
        id: updateBanner

        controller: HomeCtl
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingLoose
        anchors.top: parent.top
        anchors.topMargin: Theme.spacingLoose
        z: 5
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
