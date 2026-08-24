pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import StrmQt

// Server-side search (ARCHITECTURE.md).
//
// What changed and why:
//
//  * One flat grid of Movie/Series/Episode became **labelled sections**. A grid
//    that mixes a film, the series it was spun off from and four episodes of
//    that series is a list the user has to re-sort in their head; the server
//    already told us the type of every row, so the page says it. Each type gets
//    the shelf shape its artwork wants — 2:3 posters for films, series and
//    collections, 16:9 stills for episodes.
//  * People and genres are sections of their own, and they *go somewhere*: a
//    person opens their filmography, a genre opens the filtered grid. They are
//    the two things a search box is asked for that are not items, and until now
//    they were simply dropped.
//  * Music is three sections of its own (ARCHITECTURE.md): artists and albums as
//    square art, tracks as a dense table. On this server that is 4,871 + 5,037
//    + 56,283 items that were previously unreachable from here.
//  * An empty query is no longer a blank page. Recent searches — which the
//    controller has always persisted and nothing ever showed — are the page's
//    content when there is nothing typed.
//
// Two ordering decisions that are load-bearing rather than aesthetic:
//
//  * People and genres sit *below* the item sections. They arrive on their own
//    requests (facetsChanged), after the items are already drawn and being
//    read; anything appended below the reading position grows the page without
//    moving what is under the eye, and anything inserted above it does not.
//  * Sections are walked through `navSections`, never chained to each other, so
//    a section that is absent (no episodes matched, no genres came back yet) is
//    skipped by Up/Down instead of being a hole in the keyboard chain — the same
//    device DetailsPage.qml uses for exactly the same reason.
//
// objectName is load-bearing: Main.qml tests it so "/" does not push a second
// search page over this one.
//
// Navigation contract: no openDetails signal here — item verbs go through
// `Actions`, and Main.qml listens to Actions.detailsRequested once.
FocusScope {
    id: page
    objectName: "searchPage"
    signal backRequested()

    readonly property bool hasQuery: SearchCtl.query.length > 0
    readonly property bool failed: SearchCtl.errorMessage.length > 0

    // "Nothing matched" is a claim about the whole page, not about the item
    // grid: a query that finds no films but two directors has found something.
    readonly property int facetCount:
        (SearchCtl.people ? SearchCtl.people.length : 0)
        + (SearchCtl.genres ? SearchCtl.genres.length : 0)
    readonly property int resultCount: SearchCtl.model ? SearchCtl.model.count : 0

    // Live filtered views over SearchCtl.model. The controller owns the type
    // taxonomy and the proxies preserve source roles and user-state changes;
    // this page chooses only how each section is presented.
    readonly property var movieModel: SearchCtl.movies
    readonly property var seriesModel: SearchCtl.series
    readonly property var episodeModel: SearchCtl.episodes
    readonly property var collectionModel: SearchCtl.collections
    readonly property var artistModel: SearchCtl.artists
    readonly property var albumModel: SearchCtl.albums
    readonly property var trackModel: SearchCtl.tracks
    readonly property var otherModel: SearchCtl.other

    // ── Item verbs ─────────────────────────────────────────────────────────
    // Always read back through the source model. The section proxy is what is
    // drawn, while `resolve()` wants the complete source record (seriesId
    // included); sourceRow() keeps that mapping stable across mixed types.
    function itemAt(index) {
        const model = SearchCtl.model
        if (!model || index < 0 || index >= model.count)
            return null
        return model.get(index)
    }

    function sourceRow(sectionModel, index) {
        if (!sectionModel || index < 0 || index >= sectionModel.count)
            return -1
        return sectionModel.sourceRow(index)
    }

    function itemIn(sectionModel, index) {
        return page.itemAt(page.sourceRow(sectionModel, index))
    }

    // The one place a query becomes "recent": the user reached a result and
    // acted on it. Typing does not count — every keystroke is a query, and
    // recording those would fill the list with prefixes of one word.
    function rememberQuery() {
        if (SearchCtl.query.length > 0)
            SearchCtl.noteQueryUsed(SearchCtl.query)
    }

    function openResult(sectionModel, index) {
        const item = page.itemIn(sectionModel, index)
        if (!item)
            return
        page.rememberQuery()
        Actions.openDetails(item)
    }

    function playResult(sectionModel, index) {
        const item = page.itemIn(sectionModel, index)
        if (!item)
            return
        page.rememberQuery()
        Actions.play(item)
    }

    // An album has no stream of its own — asking the server to play one is an
    // HTTP 500, not an empty queue — so its ▸ queues its tracks. "music" is not
    // decoration: without it the query falls back to {Movie, Episode, Video},
    // which matches nothing under an album and plays nothing at all.
    function playAlbumResult(sectionModel, index) {
        const item = page.itemIn(sectionModel, index)
        if (!item)
            return
        const id = item.itemId !== undefined ? String(item.itemId) : ""
        if (id.length === 0)
            return
        page.rememberQuery()
        Actions.playAll(id, "music")
    }

    // mm:ss, or h:mm:ss for the rare long track. Mono and right-aligned where
    // it is drawn, so the column does not jitter between 9 and 10 seconds.
    function formatDuration(ms) {
        const total = Math.round(Number(ms) / 1000)
        if (!isFinite(total) || total <= 0)
            return ""
        const hours = Math.floor(total / 3600)
        const minutes = Math.floor((total % 3600) / 60)
        const seconds = total % 60
        const ss = seconds < 10 ? "0" + seconds : String(seconds)
        if (hours > 0)
            return hours + ":" + (minutes < 10 ? "0" + minutes : String(minutes)) + ":" + ss
        return minutes + ":" + ss
    }

    function togglePlayedResult(sectionModel, index) {
        const item = page.itemIn(sectionModel, index)
        if (item)
            Actions.togglePlayed(item)
    }

    function toggleFavoriteResult(sectionModel, index) {
        const item = page.itemIn(sectionModel, index)
        if (item)
            Actions.toggleFavorite(item)
    }

    // ── Context menu (ARCHITECTURE.md) ────────────────────────────────────────
    // The action list lives in ItemMenu, shared with every other page.
    function showMenu(sectionModel, index, sceneX, sceneY) {
        itemMenu.popupForItem(page.itemIn(sectionModel, index), sceneX, sceneY)
    }

    // The query the sections currently on screen were built from. Not for
    // display: it is what tells a *replaced* result set apart from the same
    // one arriving again, and only the first has any business moving the
    // viewport the user is reading from.
    property string builtQuery: ""

    function noteResultReset() {
        // A new QUERY is a new page: staying at the old scroll offset would land
        // the user in the middle of results they have not seen the top of. The
        // same query's model reset is not that, so the viewport stays put.
        if (page.builtQuery !== SearchCtl.query) {
            page.builtQuery = SearchCtl.query
            scrollAnim.stop()
            scroll.contentY = 0
        }
        // Deferred: the sections' visibility settles after this frame's
        // bindings, so "is anything still focusable" is not answerable yet.
        Qt.callLater(page.restoreFocus)
    }

    // A section that disappears takes the keyboard with it: Qt clears active
    // focus on a hidden item and hands it back to the enclosing FocusScope,
    // which leaves this page focused and nothing on it focusable. The field is
    // where the page starts and where it recovers to.
    function restoreFocus() {
        if (!page.activeFocus || searchField.activeFocus)
            return
        for (var i = 0; i < page.navSections.length; ++i) {
            if (page.navSections[i].activeFocus)
                return
        }
        searchField.forceActiveFocus()
    }

    // Section contents and incremental changes are owned by the C++ proxies.
    // The source reset still tells presentation that a replaced query may need
    // its viewport and focus repaired.
    Connections {
        target: SearchCtl.model
        function onModelReset() { page.noteResultReset() }
    }

    // The query survives leaving and re-entering the page, so the results may
    // already be there when this one is built.
    Component.onCompleted: page.builtQuery = SearchCtl.query

    // ── Recent searches ────────────────────────────────────────────────────
    // Rendered as chips through the same strip the genres use. The trailing
    // "Clear" is a chip rather than a button beside the heading on purpose: a
    // button there would be reachable only with the mouse — StrmRail's own
    // "See all" has that shape and that gap — whereas a last chip sits in the
    // row's existing Left/Right chain and is clickable in the same gesture.
    readonly property var recentChips: {
        var out = []
        const recent = SearchCtl.recentQueries
        for (var i = 0; i < recent.length; ++i)
            out.push({ name: recent[i], icon: "clock", verb: "use" })
        if (recent.length > 0)
            out.push({ name: qsTr("Clear"), icon: "close", verb: "clear" })
        return out
    }

    function applyQuery(text) {
        searchField.text = text
        SearchCtl.query = text
        searchField.forceActiveFocus()
    }

    // ── Vertical navigation ────────────────────────────────────────────────
    // Everything that can appear, in the order it appears. Absent sections are
    // skipped rather than chained around, so "no episodes matched" costs the
    // keyboard nothing and a facet landing late costs it nothing either.
    //
    // Music sits between the video sections and "Other results": broad to
    // narrow inside each medium (Movies → Series → Episodes, Artists → Albums →
    // Tracks), with the catch-all last so the grouped page can never show fewer
    // results than the flat grid it replaced.
    readonly property var navSections: [recentRow, movieRail, seriesRail, episodeRail,
                                        collectionRail, artistRail, albumRail, trackList,
                                        otherRail, peopleShelf, genreRow]

    function sectionBelow(start) {
        for (var i = start; i < page.navSections.length; ++i) {
            if (page.navSections[i].visible)
                return page.navSections[i]
        }
        return null
    }

    // The field is the top of this page and always will be: typing is the
    // primary act, so Up out of the first section has exactly one destination.
    function sectionAbove(start) {
        for (var i = start; i >= 0; --i) {
            if (page.navSections[i].visible)
                return page.navSections[i]
        }
        return searchField
    }

    // ── Inline sections ────────────────────────────────────────────────────
    // ChipStrip and PersonShelf are page-local `component`s rather than two more
    // files in `controls/`: both are shapes this page needs and no other page
    // has in this form, and ARCHITECTURE.md is about not re-inventing a *button*
    // per page, not about hoisting every arrangement into the shared module.
    //
    // Both follow the library's focus contract exactly: one tab stop for the
    // whole section, the view owns Left/Right and Return, and each chip/card
    // takes its `highlighted` from `ListView.isCurrentItem && view.activeFocus`.
    // Chips and cards never take focus themselves — individually focusable pills
    // steal the arrow keys from the row that contains them.

    // A labelled row of LinkChips. Carries the same display-size heading as the
    // rails and the person shelf, because on this page "Genres" is a section of
    // results and not a field on a record.
    component ChipStrip: FocusScope {
        id: strip

        property string title: ""
        // QVariantList from SearchCtl, or any JS array of objects.
        property var chipModel: []
        // Which key of each record gives the chip's text, its icon, and the
        // value whose emptiness means "this is not a link".
        property string labelKey: "name"
        property string iconKey: ""
        property string linkKey: "id"
        property int sideMargin: Theme.pageMarginValue

        signal chipActivated(int index)

        readonly property int count: strip.chipModel ? strip.chipModel.length : 0

        readonly property int chipHeight: Theme.scale(32)
        // Headroom so a focused chip's Theme.focusScale raise is not clipped.
        readonly property int rowPadding: Math.ceil(strip.chipHeight * (Theme.focusScale - 1) / 2)
                                          + Theme.scale(4)

        width: parent ? parent.width : Theme.scale(600)
        height: strip.count > 0 ? heading.height + Theme.spacingValue + chips.height : 0
        visible: strip.count > 0

        function activateCurrent(): void {
            if (chips.currentIndex >= 0 && chips.currentIndex < strip.count)
                strip.chipActivated(chips.currentIndex)
        }

        function clampX(x) {
            const minX = chips.originX
            const maxX = Math.max(minX, chips.originX + chips.contentWidth - chips.width)
            return Math.max(minX, Math.min(maxX, x))
        }

        function scrollBy(dx) {
            chips.contentX = strip.clampX(chips.contentX + dx)
        }

        Text {
            id: heading

            anchors.left: parent.left
            anchors.top: parent.top
            anchors.leftMargin: strip.sideMargin
            text: strip.title
            color: Theme.textPrimaryColor
            font.family: Theme.fontDisplay
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
        }

        ListView {
            id: chips

            anchors.top: heading.bottom
            anchors.topMargin: Theme.spacingValue
            anchors.left: parent.left
            anchors.right: parent.right
            height: strip.chipHeight + strip.rowPadding * 2

            orientation: ListView.Horizontal
            spacing: Theme.spacingTight
            leftMargin: strip.sideMargin
            rightMargin: strip.sideMargin
            focus: true
            clip: true
            model: strip.chipModel
            boundsBehavior: Flickable.StopAtBounds

            keyNavigationWraps: false
            highlightMoveDuration: Theme.animFastMs
            preferredHighlightBegin: strip.sideMargin
            preferredHighlightEnd: width / 2
            highlightRangeMode: ListView.ApplyRange

            // A horizontal strip must not eat the page's vertical scroll:
            // filtering by axis means a plain wheel is never grabbed here and
            // falls through to the page Flickable (ARCHITECTURE.md).
            flickableDirection: Flickable.HorizontalFlick

            WheelHandler {
                orientation: Qt.Horizontal
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: event => strip.scrollBy(-event.angleDelta.x / 120 * Theme.wheelStepPx)
            }

            delegate: Item {
                id: chipCell

                required property int index
                required property var modelData

                readonly property string linkValue: {
                    const v = chipCell.modelData[strip.linkKey]
                    return (v === undefined || v === null) ? "" : String(v)
                }

                width: chip.implicitWidth
                height: chips.height

                LinkChip {
                    id: chip

                    anchors.verticalCenter: parent.verticalCenter
                    label: {
                        const v = chipCell.modelData[strip.labelKey]
                        return (v === undefined || v === null) ? "" : String(v)
                    }
                    iconName: {
                        if (strip.iconKey.length === 0)
                            return ""
                        const v = chipCell.modelData[strip.iconKey]
                        return (v === undefined || v === null) ? "" : String(v)
                    }
                    // No id (an older payload) → plain text, not a dead pill.
                    linked: chipCell.linkValue.length > 0
                    highlighted: chipCell.ListView.isCurrentItem && chips.activeFocus

                    onActivated: {
                        // A click commits *and* makes this chip the keyboard's
                        // place, so a following arrow key continues from here.
                        chips.currentIndex = chipCell.index
                        chips.forceActiveFocus(Qt.MouseFocusReason)
                        strip.chipActivated(chipCell.index)
                    }
                }
            }

            // Guarded against auto-repeat like every other activation path.
            Keys.onReturnPressed: event => { if (!event.isAutoRepeat) strip.activateCurrent() }
            Keys.onEnterPressed: event => { if (!event.isAutoRepeat) strip.activateCurrent() }
        }

        // Shift+wheel pans the strip; everything else is handed straight back
        // so the page keeps scrolling (StrmRail documents why this is a
        // MouseArea and not a second WheelHandler).
        MouseArea {
            anchors.fill: chips
            z: 1
            acceptedButtons: Qt.NoButton
            hoverEnabled: false
            onWheel: wheel => {
                if (!(wheel.modifiers & Qt.ShiftModifier) || wheel.angleDelta.y === 0) {
                    wheel.accepted = false
                    return
                }
                wheel.accepted = true
                strip.scrollBy(-wheel.angleDelta.y / 120 * Theme.wheelStepPx)
            }
        }

        // Edge fades, shown only when there is something past the edge.
        Rectangle {
            anchors.left: chips.left
            anchors.top: chips.top
            anchors.bottom: chips.bottom
            width: strip.sideMargin
            z: 2
            visible: !chips.atXBeginning
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Theme.ground }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }

        Rectangle {
            anchors.right: chips.right
            anchors.top: chips.top
            anchors.bottom: chips.bottom
            width: strip.sideMargin
            z: 2
            visible: !chips.atXEnd
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 1.0; color: Theme.ground }
            }
        }
    }

    // A horizontal shelf of PersonCards. Deliberately not a StrmRail: that
    // shelf's delegate reads MediaItemModel roles off a QAbstractItemModel and
    // emits four media verbs, where a search person is a plain {id, name,
    // imageUrl} record with exactly one verb.
    component PersonShelf: FocusScope {
        id: shelf

        property string title: ""
        // QVariantList of {id, name, imageUrl}.
        property var people: []
        property int sideMargin: Theme.pageMarginValue

        signal personActivated(int index)

        readonly property int count: shelf.people ? shelf.people.length : 0
        readonly property bool hovered: shelfHover.hovered

        // One hidden card is the source of truth for card metrics, so the shelf
        // can never disagree with the card about how big a card is.
        PersonCard {
            id: metrics
            visible: false
            enabled: false
        }

        readonly property int cardWidth: metrics.implicitWidth
        readonly property int cardHeight: metrics.implicitHeight
        readonly property int rowPadding: Math.ceil(shelf.cardHeight * (Theme.focusScale - 1) / 2)
                                          + Theme.spacingTight

        width: parent ? parent.width : Theme.scale(800)
        height: shelf.count > 0 ? heading.height + Theme.spacingValue + cards.height : 0
        visible: shelf.count > 0

        function activateCurrent(): void {
            if (cards.currentIndex >= 0 && cards.currentIndex < shelf.count)
                shelf.personActivated(cards.currentIndex)
        }

        function clampX(x) {
            const minX = cards.originX
            const maxX = Math.max(minX, cards.originX + cards.contentWidth - cards.width)
            return Math.max(minX, Math.min(maxX, x))
        }

        // Immediate (wheel): the pointer expects 1:1 tracking, so no animation.
        function scrollBy(dx) {
            shelfScroll.stop()
            cards.contentX = shelf.clampX(cards.contentX + dx)
        }

        // Animated (chevrons): about a viewport, less half a card, so the card
        // that was at the edge stays visible as an anchor.
        function scrollPage(direction) {
            const step = Math.max(shelf.cardWidth, cards.width - shelf.cardWidth * 0.5)
            shelfScroll.stop()
            shelfScroll.from = cards.contentX
            shelfScroll.to = shelf.clampX(cards.contentX + direction * step)
            shelfScroll.start()
        }

        NumberAnimation {
            id: shelfScroll
            target: cards
            property: "contentX"
            duration: Theme.animNormalMs
            easing.type: Theme.easeEmphasis
            onFinished: cards.returnToBounds()
        }

        HoverHandler {
            id: shelfHover
            // No cursorShape: only the cards and chevrons are clickable, and
            // each of those sets its own.
        }

        Text {
            id: heading

            anchors.left: parent.left
            anchors.top: parent.top
            anchors.leftMargin: shelf.sideMargin
            text: shelf.title
            color: Theme.textPrimaryColor
            font.family: Theme.fontDisplay
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
        }

        ListView {
            id: cards

            anchors.top: heading.bottom
            anchors.topMargin: Theme.spacingValue
            anchors.left: parent.left
            anchors.right: parent.right
            height: shelf.cardHeight + shelf.rowPadding * 2

            orientation: ListView.Horizontal
            spacing: Theme.spacingValue
            leftMargin: shelf.sideMargin
            rightMargin: shelf.sideMargin
            focus: true
            clip: true
            model: shelf.people
            boundsBehavior: Flickable.StopAtBounds

            keyNavigationWraps: false
            highlightMoveDuration: Theme.animFastMs
            preferredHighlightBegin: shelf.sideMargin
            preferredHighlightEnd: width / 2
            highlightRangeMode: ListView.ApplyRange
            cacheBuffer: shelf.cardWidth * 4

            flickableDirection: Flickable.HorizontalFlick

            WheelHandler {
                orientation: Qt.Horizontal
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: event => shelf.scrollBy(-event.angleDelta.x / 120 * Theme.wheelStepPx)
            }

            delegate: Item {
                id: personCell

                required property int index
                required property var modelData

                // PersonCard builds its own provider URL from an id and a
                // Primary image tag; SearchController hands over the finished
                // image://emby/<id>/Primary/<tag> instead. The tag is the last
                // path segment of exactly that URL, so it is taken back out
                // rather than teaching the card a second image contract.
                readonly property var record: {
                    const source = personCell.modelData
                    const url = (source && source.imageUrl) ? String(source.imageUrl) : ""
                    const cut = url.lastIndexOf("/")
                    return {
                        id: (source && source.id) ? String(source.id) : "",
                        name: (source && source.name) ? String(source.name) : "",
                        primaryImageTag: cut >= 0 ? url.substring(cut + 1) : ""
                    }
                }

                width: shelf.cardWidth
                height: cards.height

                PersonCard {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.verticalCenter: parent.verticalCenter
                    person: personCell.record
                    // Focus, not hover: the card's own HoverHandler owns the
                    // other half of the pair and the two never touch.
                    highlighted: personCell.ListView.isCurrentItem && cards.activeFocus

                    onActivated: {
                        cards.currentIndex = personCell.index
                        cards.forceActiveFocus(Qt.MouseFocusReason)
                        shelf.personActivated(personCell.index)
                    }
                }
            }

            Keys.onReturnPressed: event => { if (!event.isAutoRepeat) shelf.activateCurrent() }
            Keys.onEnterPressed: event => { if (!event.isAutoRepeat) shelf.activateCurrent() }
        }

        MouseArea {
            anchors.fill: cards
            z: 1
            acceptedButtons: Qt.NoButton
            hoverEnabled: false
            onWheel: wheel => {
                if (!(wheel.modifiers & Qt.ShiftModifier) || wheel.angleDelta.y === 0) {
                    wheel.accepted = false
                    return
                }
                wheel.accepted = true
                shelf.scrollBy(-wheel.angleDelta.y / 120 * Theme.wheelStepPx)
            }
        }

        Rectangle {
            anchors.left: cards.left
            anchors.top: cards.top
            anchors.bottom: cards.bottom
            width: shelf.sideMargin
            z: 2
            visible: !cards.atXBeginning
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Theme.ground }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }

        Rectangle {
            anchors.right: cards.right
            anchors.top: cards.top
            anchors.bottom: cards.bottom
            width: shelf.sideMargin
            z: 2
            visible: !cards.atXEnd
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 1.0; color: Theme.ground }
            }
        }

        StrmIconButton {
            anchors.left: cards.left
            anchors.leftMargin: Theme.spacingTight
            anchors.verticalCenter: cards.verticalCenter
            z: 3
            iconName: "chevron-left"
            round: true
            tooltip: qsTr("Scroll left")
            enabled: !cards.atXBeginning
            visible: opacity > 0.01
            opacity: (shelf.hovered && !cards.atXBeginning && cards.count > 0) ? 1 : 0
            onClicked: shelf.scrollPage(-1)

            Behavior on opacity {
                NumberAnimation { duration: Theme.animInstant; easing.type: Theme.easeInstant }
            }
        }

        StrmIconButton {
            anchors.right: cards.right
            anchors.rightMargin: Theme.spacingTight
            anchors.verticalCenter: cards.verticalCenter
            z: 3
            iconName: "chevron-right"
            round: true
            tooltip: qsTr("Scroll right")
            enabled: !cards.atXEnd
            visible: opacity > 0.01
            opacity: (shelf.hovered && !cards.atXEnd && cards.count > 0) ? 1 : 0
            onClicked: shelf.scrollPage(1)

            Behavior on opacity {
                NumberAnimation { duration: Theme.animInstant; easing.type: Theme.easeInstant }
            }
        }
    }

    // A dense table of tracks. Deliberately NOT a rail of cards: a song has no
    // artwork of its own — every track on a record shares the sleeve — so a
    // shelf of twelve identical squares says nothing, and the three facts that
    // do identify a track (title, artist, album) do not fit under a card. One
    // row per track, forty of them in the height six cards would take.
    //
    // The list is `interactive: false` and laid out at its full height inside
    // the page's Flickable, so the page scrolls as one surface: a nested
    // scroller here would trap the wheel over a section the user is reading
    // past. That makes row-level scroll-into-view this component's job, which
    // is what `rowFocused` is for.
    component TrackList: FocusScope {
        id: tracks

        property string title: ""
        property var trackListModel: null
        property int sideMargin: Theme.pageMarginValue
        property string navigationFocusKey: ""
        property Item navigationFocusFallbackItem: null
        property bool navigationFocusRefillActive: false

        signal trackActivated(int index)
        signal trackMenuRequested(int index, real x, real y)
        // y is the row's top within this section; the page maps it into the
        // scroller and nudges only when the row is actually outside it.
        signal rowFocused(real y, real h)

        readonly property int count: tracks.trackListModel ? tracks.trackListModel.count : 0
        readonly property int rowHeight: Theme.scale(56)
        readonly property int currentIndex: list.currentIndex
        readonly property string navigationFocusKind: "tracks"
        readonly property bool navigationFocusRestorePending: navigationFocus.pending
        property bool _navigationFocusWriting: false

        width: parent ? parent.width : Theme.scale(600)
        height: tracks.count > 0 ? heading.height + Theme.spacingValue + list.height : 0
        visible: tracks.count > 0

        function activateCurrent(): void {
            tracks._cancelNavigationFocusForUser()
            if (list.currentIndex >= 0 && list.currentIndex < tracks.count)
                tracks.trackActivated(list.currentIndex)
        }

        function navigationFocusSnapshot(): var { return navigationFocus.snapshot() }
        function restoreNavigationFocus(identity, index): bool {
            return navigationFocus.restore(identity, index)
        }
        function cancelNavigationFocusRestore(): void { navigationFocus.cancel() }
        function _cancelNavigationFocusForUser(): void {
            if (!tracks._navigationFocusWriting)
                navigationFocus.cancel()
        }
        function _applyNavigationFocus(index): void {
            tracks._navigationFocusWriting = true
            if (index >= 0) {
                list.currentIndex = index
                list.positionViewAtIndex(index, ListView.Contain)
            }
            list.forceActiveFocus(Qt.OtherFocusReason)
            if (index >= 0)
                tracks.rowFocused(list.y + index * tracks.rowHeight, tracks.rowHeight)
            tracks._navigationFocusWriting = false
        }
        function _applyNavigationFallback(): void {
            if (tracks.navigationFocusFallbackItem
                    && tracks.navigationFocusFallbackItem.visible
                    && tracks.navigationFocusFallbackItem.enabled) {
                tracks.navigationFocusFallbackItem.forceActiveFocus(Qt.OtherFocusReason)
                return
            }
            let candidate = list
            for (let step = 0; step < 256; ++step) {
                candidate = candidate.nextItemInFocusChain(true)
                if (!candidate || candidate === list)
                    return
                let cursor = candidate
                while (cursor && cursor !== tracks)
                    cursor = cursor.parent
                if (cursor !== tracks) {
                    candidate.forceActiveFocus(Qt.OtherFocusReason)
                    return
                }
            }
        }

        NavigationFocusRestorer {
            id: navigationFocus
            model: tracks.trackListModel
            count: tracks.count
            currentIndex: list.currentIndex
            refillActive: tracks.navigationFocusRefillActive
            onFocusRequested: index => tracks._applyNavigationFocus(index)
            onFallbackRequested: tracks._applyNavigationFallback()
        }

        Connections {
            target: tracks
            function onActiveFocusChanged() {
                if (!tracks.activeFocus)
                    tracks._cancelNavigationFocusForUser()
            }
        }

        Connections {
            target: Qt.isQtObject(tracks.trackListModel) ? tracks.trackListModel : null
            ignoreUnknownSignals: true
            function onModelReset() { Qt.callLater(navigationFocus.noteProgress) }
        }

        onCountChanged: navigationFocus.noteProgress()
        onTrackListModelChanged: navigationFocus.cancel()

        Text {
            id: heading

            anchors.left: parent.left
            anchors.top: parent.top
            anchors.leftMargin: tracks.sideMargin
            text: tracks.title
            color: Theme.textPrimaryColor
            font.family: Theme.fontDisplay
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
        }

        ListView {
            id: list

            anchors.top: heading.bottom
            anchors.topMargin: Theme.spacingValue
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: tracks.sideMargin
            anchors.rightMargin: tracks.sideMargin
            height: tracks.rowHeight * tracks.count

            focus: true
            model: tracks.trackListModel
            currentIndex: 0
            keyNavigationWraps: false
            // The page owns the scroll; this view is a layout, not a viewport.
            interactive: false

            // Up at row 0 and Down at the last row are left unaccepted by
            // ListView itself, which is what lets the section's KeyNavigation
            // hand the keyboard to the next section instead of dead-ending.
            onCurrentIndexChanged: {
                if (list.activeFocus)
                    tracks.rowFocused(list.y + list.currentIndex * tracks.rowHeight,
                                      tracks.rowHeight)
            }

            Keys.onReturnPressed: event => { if (!event.isAutoRepeat) tracks.activateCurrent() }
            Keys.onEnterPressed: event => { if (!event.isAutoRepeat) tracks.activateCurrent() }
            Keys.onPressed: event => {
                tracks._cancelNavigationFocusForUser()
                // The context-menu key, anchored under the row it belongs to —
                // the keyboard's equivalent of a right-click.
                if (event.key === Qt.Key_Menu && !event.isAutoRepeat && list.currentItem) {
                    const p = list.currentItem.mapToItem(null, Theme.spacingValue,
                                                         list.currentItem.height)
                    tracks.trackMenuRequested(list.currentIndex, p.x, p.y)
                    event.accepted = true
                }
            }

            delegate: Item {
                id: trackRow

                required property int index
                required property var model

                readonly property bool current: trackRow.ListView.isCurrentItem && list.activeFocus
                readonly property bool hovered: trackHover.hovered
                // Both may be true at once, and the actions appear for either:
                // the pointer needs them under the cursor, the keyboard needs
                // them on the row it is standing on.
                readonly property bool showActions: trackRow.hovered || trackRow.current

                readonly property string title: {
                    const name = trackRow.model.name !== undefined
                               ? String(trackRow.model.name) : ""
                    if (name.length > 0)
                        return name
                    return trackRow.model.label !== undefined ? String(trackRow.model.label) : ""
                }

                // Artist and album, in that order, because that is the order
                // someone scanning a result list disambiguates by.
                readonly property string credit: {
                    const parts = []
                    const artist = trackRow.model.artistText !== undefined
                                 ? String(trackRow.model.artistText) : ""
                    if (artist.length > 0)
                        parts.push(artist)
                    const album = trackRow.model.albumText !== undefined
                                ? String(trackRow.model.albumText) : ""
                    if (album.length > 0)
                        parts.push(album)
                    return parts.join("  ·  ")
                }

                width: list.width
                height: tracks.rowHeight

                Rectangle {
                    anchors.fill: parent
                    anchors.topMargin: Theme.scale(2)
                    anchors.bottomMargin: Theme.scale(2)
                    radius: Theme.radiusChip
                    color: trackRow.hovered ? Theme.hoverTint : "transparent"

                    Behavior on color {
                        ColorAnimation { duration: Theme.animInstant; easing.type: Theme.easeInstant }
                    }
                }

                Rectangle {
                    id: trackArt

                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacingTight
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.scale(40)
                    height: Theme.scale(40)
                    radius: Theme.radiusChip
                    color: Theme.surfaceColor
                    clip: true

                    StrmIcon {
                        anchors.centerIn: parent
                        name: "lib-music"
                        size: Theme.scale(18)
                        color: Theme.textTertiary
                    }

                    Image {
                        anchors.fill: parent
                        source: trackRow.model.posterUrl !== undefined
                                ? trackRow.model.posterUrl : ""
                        sourceSize.width: Theme.scale(40)
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
                        width: Theme.scale(15)
                        height: Theme.scale(15)
                        radius: height / 2
                        visible: trackRow.model.played === true
                        color: Theme.positive

                        StrmIcon {
                            anchors.centerIn: parent
                            name: "check"
                            size: Theme.scale(10)
                            color: Theme.accentText
                        }
                    }
                }

                Column {
                    anchors.left: trackArt.right
                    anchors.leftMargin: Theme.spacingValue
                    anchors.right: trackTail.left
                    anchors.rightMargin: Theme.spacingValue
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.scale(2)

                    Text {
                        width: parent.width
                        text: trackRow.title
                        color: (trackRow.current || trackRow.hovered)
                               ? Theme.textPrimaryColor : Theme.textSecondaryColor
                        font.family: Theme.fontBody
                        font.pixelSize: Theme.fontBodySize
                        elide: Text.ElideRight
                        maximumLineCount: 1
                    }

                    Text {
                        width: parent.width
                        visible: text.length > 0
                        text: trackRow.credit
                        color: Theme.textTertiary
                        font.family: Theme.fontBody
                        font.pixelSize: Theme.fontCaption
                        elide: Text.ElideRight
                        maximumLineCount: 1
                    }
                }

                Row {
                    id: trackTail

                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingTight
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spacingTight

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: page.formatDuration(trackRow.model.runtimeMs)
                        visible: text.length > 0
                        color: Theme.textTertiary
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontCaption
                    }

                    // `activeFocusOnTab: false` throughout: the list is one tab
                    // stop and owns the arrow keys. Two focusable buttons per
                    // row would make Tab walk the results instead of leaving
                    // them; the keyboard reaches both through the list's own
                    // Return and Menu handling.
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.scale(2)
                        opacity: trackRow.showActions ? 1 : 0
                        visible: opacity > 0.01
                        enabled: visible

                        Behavior on opacity {
                            NumberAnimation {
                                duration: Theme.animInstant
                                easing.type: Theme.easeInstant
                            }
                        }

                        StrmIconButton {
                            iconName: "play"
                            round: true
                            size: Theme.scale(30)
                            activeFocusOnTab: false
                            tooltip: qsTr("Play")
                            onClicked: {
                                tracks._cancelNavigationFocusForUser()
                                list.currentIndex = trackRow.index
                                tracks.trackActivated(trackRow.index)
                            }
                        }

                        StrmIconButton {
                            id: trackMore
                            iconName: "more-horizontal"
                            round: true
                            size: Theme.scale(30)
                            activeFocusOnTab: false
                            tooltip: qsTr("More…")
                            onClicked: {
                                tracks._cancelNavigationFocusForUser()
                                const p = trackMore.mapToItem(null, 0, trackMore.height)
                                tracks.trackMenuRequested(trackRow.index, p.x, p.y)
                            }
                        }
                    }
                }

                FocusRing {
                    active: trackRow.current
                    anchors.fill: parent
                    radius: Theme.radiusChip
                    inset: -Theme.scale(1)
                }

                HoverHandler {
                    id: trackHover
                    cursorShape: Qt.PointingHandCursor
                }

                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: {
                        // A click commits *and* makes this row the keyboard's
                        // place, so a following arrow key continues from here.
                        tracks._cancelNavigationFocusForUser()
                        list.currentIndex = trackRow.index
                        list.forceActiveFocus(Qt.MouseFocusReason)
                        tracks.trackActivated(trackRow.index)
                    }
                }

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: eventPoint => {
                        tracks._cancelNavigationFocusForUser()
                        const p = trackRow.mapToItem(null, eventPoint.position.x,
                                                     eventPoint.position.y)
                        tracks.trackMenuRequested(trackRow.index, p.x, p.y)
                    }
                }
            }
        }
    }

    // ── The field ──────────────────────────────────────────────────────────
    // No `text: SearchCtl.query` binding on purpose: TextField.clear() — which
    // StrmSearchField's × button calls — is an imperative write and would break
    // such a binding silently, leaving the box and the controller disagreeing
    // from then on. The field owns its text and pushes it to the controller.
    StrmSearchField {
        id: searchField

        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: Theme.spacingLoose
        width: Math.min(parent.width - 2 * Theme.pageMarginValue, Theme.scale(700))
        focus: true
        placeholderText: qsTr("Search movies, shows, episodes, people…")

        // Whatever is first *today*: recent searches with nothing typed, the
        // first non-empty result section once there is.
        KeyNavigation.down: page.sectionBelow(0)

        Component.onCompleted: searchField.text = SearchCtl.query

        onTextEdited: SearchCtl.query = searchField.text
        onCleared: SearchCtl.query = ""
        onAccepted: {
            const first = page.sectionBelow(0)
            if (first)
                first.forceActiveFocus()
        }
        onEscapePressed: {
            // First Esc drops the query, a second one leaves the page. The
            // field consumes Esc (StrmSearchField accepts it), so the pop that
            // Main.qml's StackView would otherwise do has to happen here.
            if (searchField.text.length > 0) {
                searchField.text = ""
                SearchCtl.query = ""
                return
            }
            page.backRequested()
        }
    }

    // ── Scrolling body ─────────────────────────────────────────────────────
    function scrollTo(y) {
        var maxY = Math.max(0, scroll.contentHeight - scroll.height)
        scrollAnim.stop()
        scrollAnim.from = scroll.contentY
        scrollAnim.to = Math.max(0, Math.min(maxY, y))
        scrollAnim.start()
    }

    // Keyboard and gamepad focus must drag the viewport with it: a focus ring
    // below the fold is the same bug as no focus ring at all.
    function ensureVisible(section) {
        if (!section || !section.visible)
            return
        var top = section.mapToItem(content, 0, 0).y
        var bottom = top + section.height
        if (top - Theme.spacingValue < scroll.contentY)
            page.scrollTo(top - Theme.spacingValue)
        else if (bottom + Theme.spacingValue > scroll.contentY + scroll.height)
            page.scrollTo(bottom + Theme.spacingValue - scroll.height)
    }

    // The same nudge one row deep. A rail is one card tall and scrolling the
    // section is enough; a track list can be forty rows, so the row that has
    // the keyboard is what has to be on screen, not the heading above it.
    function ensureRowVisible(section, y, h) {
        if (!section || !section.visible)
            return
        const top = section.mapToItem(content, 0, y).y
        if (top - Theme.spacingValue < scroll.contentY)
            page.scrollTo(top - Theme.spacingValue)
        else if (top + h + Theme.spacingValue > scroll.contentY + scroll.height)
            page.scrollTo(top + h + Theme.spacingValue - scroll.height)
    }

    NumberAnimation {
        id: scrollAnim
        target: scroll
        property: "contentY"
        duration: Theme.animNormalMs
        easing.type: Theme.easeStandard
    }

    Flickable {
        id: scroll

        anchors.top: searchField.bottom
        anchors.topMargin: Theme.spacingLoose
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        contentWidth: width
        contentHeight: content.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        interactive: scroll.contentHeight > scroll.height
        clip: true

        ScrollBar.vertical: StrmScrollBar {}

        Column {
            id: content

            width: scroll.width
            spacing: Theme.railGap

            // ── Recent searches ────────────────────────────────────────────
            // The empty-query state, and the only section that is *hidden* by a
            // query rather than revealed by one.
            ChipStrip {
                id: recentRow
                title: qsTr("Recent searches")
                chipModel: page.hasQuery ? [] : page.recentChips
                iconKey: "icon"
                // Every recent chip is a link; `name` is never empty because
                // noteQueryUsed() refuses a blank query.
                linkKey: "name"

                onChipActivated: index => {
                    const chip = page.recentChips[index]
                    if (!chip)
                        return
                    if (chip.verb === "clear") {
                        // Clearing empties the strip the user is standing in,
                        // so the keyboard has to be handed somewhere first.
                        searchField.forceActiveFocus()
                        SearchCtl.clearRecentQueries()
                    } else {
                        page.applyQuery(String(chip.name))
                    }
                }
                onActiveFocusChanged: { if (recentRow.activeFocus) page.ensureVisible(recentRow) }
                KeyNavigation.up: searchField
                KeyNavigation.down: page.sectionBelow(1)
            }

            // ── Movies ─────────────────────────────────────────────────────
            StrmRail {
                id: movieRail
                navigationFocusKey: "search-movies"
                navigationFocusFallbackItem: searchField
                navigationFocusRefillActive: SearchCtl.searching
                visible: page.movieModel.count > 0
                title: qsTr("Movies")
                railModel: page.movieModel
                cardVariant: "poster"
                emptyText: ""

                onActiveFocusChanged: { if (movieRail.activeFocus) page.ensureVisible(movieRail) }
                KeyNavigation.up: page.sectionAbove(0)
                KeyNavigation.down: page.sectionBelow(2)

                onItemActivated: index => page.openResult(page.movieModel, index)
                onItemPlayRequested: index => page.playResult(page.movieModel, index)
                onItemPlayedToggled: index => page.togglePlayedResult(page.movieModel, index)
                onItemFavoriteToggled: index => page.toggleFavoriteResult(page.movieModel, index)
                onMenuRequested: (index, mx, my) => page.showMenu(page.movieModel, index, mx, my)
            }

            // ── Series ─────────────────────────────────────────────────────
            StrmRail {
                id: seriesRail
                navigationFocusKey: "search-series"
                navigationFocusFallbackItem: searchField
                navigationFocusRefillActive: SearchCtl.searching
                visible: page.seriesModel.count > 0
                title: qsTr("Series")
                railModel: page.seriesModel
                cardVariant: "poster"
                emptyText: ""

                onActiveFocusChanged: { if (seriesRail.activeFocus) page.ensureVisible(seriesRail) }
                KeyNavigation.up: page.sectionAbove(1)
                KeyNavigation.down: page.sectionBelow(3)

                onItemActivated: index => page.openResult(page.seriesModel, index)
                onItemPlayRequested: index => page.playResult(page.seriesModel, index)
                onItemPlayedToggled: index => page.togglePlayedResult(page.seriesModel, index)
                onItemFavoriteToggled: index => page.toggleFavoriteResult(page.seriesModel, index)
                onMenuRequested: (index, mx, my) => page.showMenu(page.seriesModel, index, mx, my)
            }

            // ── Episodes ───────────────────────────────────────────────────
            // 16:9, because an episode's art is a frame of the episode. Drawing
            // it in a 2:3 poster frame crops it to a fragment of itself; the
            // card falls back to the poster on its own when there is no still.
            StrmRail {
                id: episodeRail
                navigationFocusKey: "search-episodes"
                navigationFocusFallbackItem: searchField
                navigationFocusRefillActive: SearchCtl.searching
                visible: page.episodeModel.count > 0
                title: qsTr("Episodes")
                railModel: page.episodeModel
                cardVariant: "still"
                emptyText: ""

                onActiveFocusChanged: {
                    if (episodeRail.activeFocus)
                        page.ensureVisible(episodeRail)
                }
                KeyNavigation.up: page.sectionAbove(2)
                KeyNavigation.down: page.sectionBelow(4)

                onItemActivated: index => page.openResult(page.episodeModel, index)
                onItemPlayRequested: index => page.playResult(page.episodeModel, index)
                onItemPlayedToggled: index => page.togglePlayedResult(page.episodeModel, index)
                onItemFavoriteToggled: index => page.toggleFavoriteResult(page.episodeModel, index)
                onMenuRequested: (index, mx, my) => page.showMenu(page.episodeModel, index, mx, my)
            }

            // ── Collections ────────────────────────────────────────────────
            // Present and correct the day BoxSet joins the search query's
            // IncludeItemTypes; absent, and costing nothing, until it does.
            StrmRail {
                id: collectionRail
                navigationFocusKey: "search-collections"
                navigationFocusFallbackItem: searchField
                navigationFocusRefillActive: SearchCtl.searching
                visible: page.collectionModel.count > 0
                title: qsTr("Collections")
                railModel: page.collectionModel
                cardVariant: "poster"
                emptyText: ""

                onActiveFocusChanged: {
                    if (collectionRail.activeFocus)
                        page.ensureVisible(collectionRail)
                }
                KeyNavigation.up: page.sectionAbove(3)
                KeyNavigation.down: page.sectionBelow(5)

                onItemActivated: index => page.openResult(page.collectionModel, index)
                onItemPlayRequested: index => page.playResult(page.collectionModel, index)
                onItemPlayedToggled: index => page.togglePlayedResult(page.collectionModel, index)
                onItemFavoriteToggled: index => page.toggleFavoriteResult(page.collectionModel, index)
                onMenuRequested: (index, mx, my) => page.showMenu(page.collectionModel, index, mx, my)
            }

            // ── Artists ────────────────────────────────────────────────────
            // Square, like every other piece of music art: an artist's Primary
            // image is a photograph, not a 2:3 poster, and a poster frame crops
            // it to a slice of a face.
            StrmRail {
                id: artistRail
                navigationFocusKey: "search-artists"
                navigationFocusFallbackItem: searchField
                navigationFocusRefillActive: SearchCtl.searching
                visible: page.artistModel.count > 0
                title: qsTr("Artists")
                railModel: page.artistModel
                cardVariant: "square"
                emptyText: ""

                onActiveFocusChanged: { if (artistRail.activeFocus) page.ensureVisible(artistRail) }
                KeyNavigation.up: page.sectionAbove(4)
                KeyNavigation.down: page.sectionBelow(6)

                onItemActivated: index => page.openResult(page.artistModel, index)
                // An artist has no ParentId a play query can address — see the
                // note in ItemMenu — so ▸ opens the artist rather than logging
                // and doing nothing. It is the card's own verb, one click
                // earlier. The day an artist-scoped Audio query exists this
                // becomes a real play and nothing else here changes.
                onItemPlayRequested: index => page.openResult(page.artistModel, index)
                onItemPlayedToggled: index => page.togglePlayedResult(page.artistModel, index)
                onItemFavoriteToggled: index => page.toggleFavoriteResult(page.artistModel, index)
                onMenuRequested: (index, mx, my) => page.showMenu(page.artistModel, index, mx, my)
            }

            // ── Albums ─────────────────────────────────────────────────────
            StrmRail {
                id: albumRail
                navigationFocusKey: "search-albums"
                navigationFocusFallbackItem: searchField
                navigationFocusRefillActive: SearchCtl.searching
                visible: page.albumModel.count > 0
                title: qsTr("Albums")
                railModel: page.albumModel
                cardVariant: "square"
                emptyText: ""

                onActiveFocusChanged: { if (albumRail.activeFocus) page.ensureVisible(albumRail) }
                KeyNavigation.up: page.sectionAbove(5)
                KeyNavigation.down: page.sectionBelow(7)

                onItemActivated: index => page.openResult(page.albumModel, index)
                onItemPlayRequested: index => page.playAlbumResult(page.albumModel, index)
                onItemPlayedToggled: index => page.togglePlayedResult(page.albumModel, index)
                onItemFavoriteToggled: index => page.toggleFavoriteResult(page.albumModel, index)
                onMenuRequested: (index, mx, my) => page.showMenu(page.albumModel, index, mx, my)
            }

            // ── Tracks ─────────────────────────────────────────────────────
            // Enter and click PLAY a track rather than opening it: a song has
            // no page to open, and 56,283 of them on this server means the
            // result the user is reaching for is one they want to hear.
            TrackList {
                id: trackList
                navigationFocusKey: "search-tracks"
                navigationFocusFallbackItem: searchField
                navigationFocusRefillActive: SearchCtl.searching
                title: qsTr("Tracks")
                trackListModel: page.trackModel

                onActiveFocusChanged: { if (trackList.activeFocus) page.ensureVisible(trackList) }
                KeyNavigation.up: page.sectionAbove(6)
                KeyNavigation.down: page.sectionBelow(8)

                onTrackActivated: index => page.playResult(page.trackModel, index)
                onTrackMenuRequested: (index, mx, my) => page.showMenu(page.trackModel, index, mx, my)
                onRowFocused: (y, h) => page.ensureRowVisible(trackList, y, h)
            }

            // ── Anything else the server sent ──────────────────────────────
            StrmRail {
                id: otherRail
                navigationFocusKey: "search-other"
                navigationFocusFallbackItem: searchField
                navigationFocusRefillActive: SearchCtl.searching
                visible: page.otherModel.count > 0
                title: qsTr("Other results")
                railModel: page.otherModel
                cardVariant: "poster"
                emptyText: ""

                onActiveFocusChanged: { if (otherRail.activeFocus) page.ensureVisible(otherRail) }
                KeyNavigation.up: page.sectionAbove(7)
                KeyNavigation.down: page.sectionBelow(9)

                onItemActivated: index => page.openResult(page.otherModel, index)
                onItemPlayRequested: index => page.playResult(page.otherModel, index)
                onItemPlayedToggled: index => page.togglePlayedResult(page.otherModel, index)
                onItemFavoriteToggled: index => page.toggleFavoriteResult(page.otherModel, index)
                onMenuRequested: (index, mx, my) => page.showMenu(page.otherModel, index, mx, my)
            }

            // ── People ─────────────────────────────────────────────────────
            // Lands on facetsChanged, after the items are drawn. Below them, so
            // it grows the page downwards instead of moving what is being read.
            PersonShelf {
                id: peopleShelf
                title: qsTr("People")
                people: page.hasQuery && SearchCtl.people ? SearchCtl.people : []

                onPersonActivated: index => {
                    const person = SearchCtl.people[index]
                    if (!person)
                        return
                    page.rememberQuery()
                    Actions.browsePerson(person.id ? String(person.id) : "",
                                         person.name ? String(person.name) : "")
                }
                onActiveFocusChanged: {
                    if (peopleShelf.activeFocus)
                        page.ensureVisible(peopleShelf)
                }
                KeyNavigation.up: page.sectionAbove(8)
                KeyNavigation.down: page.sectionBelow(10)
            }

            // ── Genres ─────────────────────────────────────────────────────
            ChipStrip {
                id: genreRow
                title: qsTr("Genres")
                chipModel: page.hasQuery && SearchCtl.genres ? SearchCtl.genres : []

                onChipActivated: index => {
                    const genre = SearchCtl.genres[index]
                    if (!genre)
                        return
                    page.rememberQuery()
                    Actions.browseGenre(genre.id ? String(genre.id) : "",
                                        genre.name ? String(genre.name) : "")
                }
                onActiveFocusChanged: { if (genreRow.activeFocus) page.ensureVisible(genreRow) }
                KeyNavigation.up: page.sectionAbove(9)
            }

            // Bottom padding as a spacer rather than a Flickable margin, which
            // would move contentY's origin off zero and put an offset in every
            // scroll calculation on this page.
            Item { width: 1; height: Math.max(0, Theme.pageMarginValue - content.spacing) }
        }
    }

    ItemMenu {
        id: itemMenu
        // Search is where a music collection is actually entered — a title
        // typed into the box is as likely to be a song as a film — so a track
        // and an album both offer the two verbs that get you out of a flat
        // result list and into the collection: its album, and its artist.
        allowMusicNavigation: true
    }

    // ── Page states ────────────────────────────────────────────────────────
    LoadingState {
        anchors.fill: scroll
        visible: SearchCtl.searching && page.resultCount === 0
        shape: "rails"
    }

    // Empty-because-the-primary request failed is not a successful empty
    // search. Facet requests remain optional, but without the item lane the page
    // cannot claim that the server found nothing.
    EmptyState {
        anchors.fill: scroll
        visible: page.hasQuery && !SearchCtl.searching && page.failed
        focus: visible
        severity: "error"
        iconName: "info"
        headline: qsTr("Couldn't search your library")
        body: SearchCtl.errorMessage
        actionText: qsTr("Retry")
        actionIcon: "refresh"
        onActionTriggered: SearchCtl.retry()
    }

    // Nothing typed and nothing to offer. With recent searches on file the
    // strip above is the better answer, so this stands down for it.
    EmptyState {
        anchors.fill: scroll
        visible: !page.hasQuery && !SearchCtl.searching && page.recentChips.length === 0
        iconName: "search"
        headline: qsTr("Search your library")
        body: qsTr("Type a title, a show, an episode or a person. Results appear as you type.")
    }

    // Typed, searched, found nothing — including no people and no genres, which
    // are results too. A different thing from the above, and it says so.
    EmptyState {
        anchors.fill: scroll
        visible: page.hasQuery && !SearchCtl.searching
                 && !page.failed && page.resultCount === 0 && page.facetCount === 0
        iconName: "search"
        headline: qsTr("No results for “%1”").arg(SearchCtl.query)
        body: qsTr("Check the spelling, or try a shorter search.")
        actionText: qsTr("Clear search")
        actionIcon: "close"
        onActionTriggered: {
            searchField.text = ""
            SearchCtl.query = ""
            searchField.forceActiveFocus()
        }
    }
}
