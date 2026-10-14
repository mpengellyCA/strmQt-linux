pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import StrmQt

// Item details (ARCHITECTURE.md): full-bleed backdrop, poster, action row, and —
// the point of the rebuild — every piece of metadata the server already sent us
// rendered as something you can *go to*, instead of three flat text lines.
//
// What changed and why:
//
//  * The page scrolls. Cast cards, crew, studios and links do not fit beside a
//    poster, and pinning "More like this" to the bottom edge (as this page did)
//    means anything added above it fights it for the same pixels. One vertical
//    Flickable, sections stacked in it, and keyboard focus drags the viewport
//    along through ensureVisible() — a focus ring below the fold is the same
//    bug as no focus ring at all.
//  * "Genres: Action, Adventure" became chip strips: genres, crew and studios
//    each navigate through `Actions.browse*`, which opens a filtered grid. A
//    record the server sent without an id is NOT a link — LinkChip renders it
//    as plain text, because a pill that looks clickable and is not is worse
//    than the text line it replaced.
//  * "Cast: …" became a shelf of PersonCards in the server's billing order,
//    which is information and is never re-sorted here.
//  * External links (IMDb / TheMovieDb / TheTVDB / Trakt) are taken verbatim
//    from `DetailsCtl.externalLinks` and opened with Qt.openUrlExternally.
//    They are never synthesised from provider ids: the server already knows
//    that a TVDB movie URL and a TVDB series URL have different shapes.
//  * Premiere date, critic rating and studios — all fetched, all previously
//    dropped — are shown. Every one of them is omitted whole when absent,
//    rather than drawn as a label with a blank beside it.
//
// Two things this page still deliberately does NOT do:
//
//  * It declares no navigation signals. Opening a series, an item, a genre or
//    a person is a request to `Actions`, which Main.qml listens to once.
//  * It does not mirror played/favorite locally. User state comes from
//    `Actions.isPlayed/isFavorite` and is refreshed from
//    `Actions.playedChanged/favoriteChanged`.
FocusScope {
    id: page

    property var item: ({})

    readonly property string itemId: page.item && page.item.itemId ? page.item.itemId : ""
    readonly property var capabilities: Actions.itemCapabilities(page.item)
    readonly property bool isSeries: page.capabilities.series === true
    readonly property bool isEpisode: page.capabilities.episode === true
    readonly property bool isCollection: page.capabilities.collection === true
    readonly property bool isPlayable: page.capabilities.leaf === true

    // DetailsCtl is process-wide and a covered details page stays alive under
    // the one that retargeted it, so anything read from it is this page's only
    // while it is this page's item that owns it.
    readonly property bool mine: page.itemId.length > 0 && DetailsCtl.itemId === page.itemId

    // ── Where Resume resumes from ──────────────────────────────────────────
    // `item` is a snapshot taken when the page was opened: watch twenty
    // minutes, come back, and it still said "Resume 0:41:10" — or "Play".
    // DetailsCtl keeps the item's user data current (its own reply, then live
    // records), so once it has answered for this item that is what the verbs
    // show. The snapshot stands in until then, and whenever the controller is
    // somebody else's.
    readonly property bool liveUserState: page.mine && !DetailsCtl.itemLoading
    readonly property bool resumable: page.liveUserState
                                      ? page.isPlayable && DetailsCtl.resumable
                                      : page.capabilities.resume === true
    readonly property real resumePositionMs: page.liveUserState
                                             ? DetailsCtl.positionMs
                                             : (page.item.positionMs > 0 ? page.item.positionMs : 0)
    // What Play and Start over are handed. ItemActions takes the start
    // position from the map it is given, so a label that follows the live
    // position over a verb that still carries the snapshot would promise one
    // place and start at another. Nothing is decided here: the two values are
    // the controller's, copied over the snapshot's.
    readonly property var playItem: {
        if (!page.liveUserState)
            return page.item
        const live = Object.assign({}, page.item)
        live.positionMs = DetailsCtl.positionMs
        live.resumable = DetailsCtl.resumable
        return live
    }

    // Cached projections of Actions' state, not a second copy of it: QML cannot
    // bind to a Q_INVOKABLE, so the value is re-read whenever Actions says it
    // changed. Nothing on this page ever assigns them from a click handler.
    property bool itemPlayed: false
    property bool itemFavorite: false

    // DetailsController exposes no `loading` role, so "still fetching" is
    // derived here: load() clears its fields and emits detailsChanged
    // synchronously, so the flag is raised *after* the call and lowered by the
    // next (asynchronous) emit. The guard timer stops a failed fetch — which
    // emits nothing — from leaving a skeleton on screen forever.
    property bool detailsLoading: true

    // ── Versions (ARCHITECTURE.md) ────────────────────────────────────────────
    // A 4K remux and a 1080p encode of the same film are two MediaSources on
    // one item, and until now the page said only how many there were. The
    // picker names them, spells out what each one actually is, and hands the
    // chosen index to the player — which is the whole point: `PlayerCtl` keys
    // its ladder to a source index, so a version chosen here is the version
    // that degrades, rather than the ladder wandering between versions.
    //
    // Single-source items show none of this. A control that always has exactly
    // one option is noise.
    property int versionIndex: 0

    readonly property var versions: DetailsCtl.mediaSources
    readonly property bool multiVersion: DetailsCtl.mediaSourceCount > 1
    readonly property var selectedVersion:
        (page.versionIndex >= 0 && page.versions && page.versionIndex < page.versions.length)
        ? page.versions[page.versionIndex] : null

    // Settings::rememberedVersion/rememberVersion exist in C++ but are not
    // Q_INVOKABLE, so QML cannot reach them yet (see this wave's report). Until
    // they are, the picker works and the choice lasts as long as the page.
    readonly property bool versionMemory: typeof Prefs !== "undefined"
                                          && typeof Prefs.rememberedVersion === "function"
                                          && typeof Prefs.rememberVersion === "function"

    readonly property int posterW: Theme.scale(300)
    readonly property int posterH: Math.round(page.posterW * 3 / 2)

    // ── Inline sections ────────────────────────────────────────────────────
    // ChipStrip appears five times and CastShelf once, all on this page and
    // nowhere else, so they are inline components rather than two more files in
    // `controls/`. ARCHITECTURE.md is about not re-inventing a *button* per
    // page; this is the opposite — one definition, six uses, one file, and no
    // new module registration for a shape no other page has.
    //
    // Both follow the library's focus contract exactly: one tab stop for the
    // whole strip, the view owns Left/Right and Return, and each chip/card gets
    // its `highlighted` from `ListView.isCurrentItem && view.activeFocus`.
    // Chips and cards never take focus themselves — SeriesPage.qml records why
    // (individually-focusable pills steal the arrow keys from the row).

    // A labelled row of LinkChips. The label is a booth gear label — small,
    // mono, uppercase, tracked out (ARCHITECTURE.md) — which keeps five strips
    // on one page from reading as five competing headings; the cast shelf
    // carries the display-size heading instead, because "Cast" is a section
    // and "Genres" is a field.
    component ChipStrip: FocusScope {
        id: strip

        property string title: ""
        // QVariantList from DetailsCtl, or any JS array of objects.
        property var chipModel: []
        // Which key of each record gives the chip's text, its secondary text,
        // and the value whose emptiness means "this is not a link".
        property string labelKey: "name"
        property string sublabelKey: ""
        property string linkKey: "id"
        property string iconName: ""
        property int sideMargin: Theme.pageMarginValue

        signal chipActivated(int index)

        readonly property int count: strip.chipModel ? strip.chipModel.length : 0

        readonly property int chipHeight: Theme.scale(32)
        // Headroom so a focused chip's Theme.focusScale raise is not clipped.
        readonly property int rowPadding: Math.ceil(strip.chipHeight * (Theme.focusScale - 1) / 2)
                                          + Theme.scale(4)

        width: parent ? parent.width : Theme.scale(600)
        height: strip.count > 0 ? label.height + Theme.spacingTight + chips.height : 0
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
            id: label

            anchors.left: parent.left
            anchors.top: parent.top
            anchors.leftMargin: strip.sideMargin
            text: strip.title.toUpperCase()
            color: Theme.textTertiary
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontCaption
            font.letterSpacing: Theme.fontCaption * Theme.trackLabel
        }

        ListView {
            id: chips

            anchors.top: label.bottom
            anchors.topMargin: Theme.spacingTight
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
                    sublabel: {
                        if (strip.sublabelKey.length === 0)
                            return ""
                        const v = chipCell.modelData[strip.sublabelKey]
                        return (v === undefined || v === null) ? "" : String(v)
                    }
                    iconName: strip.iconName
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

            // A vertical step keeps the column (NavigationColumn): Down out of
            // the fourth chip lands under it, not on wherever this shelf was
            // last parked. Both halves are needed — one publishes the column,
            // the other consumes it — and neither fires for a click, a Tab or a
            // restored back-stack cursor, because only Up/Down arm it.
            onActiveFocusChanged: {
                if (chips.activeFocus)
                    NavigationColumn.applyTo(chips)
            }

            Keys.onPressed: event => NavigationColumn.noteFromKey(chips, event.key)
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
    // emits four media verbs, where a person is a plain
    // {id, name, role, type, primaryImageTag} record with exactly one verb.
    // What the two genuinely share — no key wrapping, ApplyRange so the focused
    // card is never pinned to the edge, axis-filtered wheel routing, edge
    // fades, hover chevrons — is repeated here rather than parameterised into
    // one shelf with two personalities.
    component CastShelf: FocusScope {
        id: shelf

        property string title: ""
        // QVariantList of {id, name, role, type, primaryImageTag}.
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

                width: shelf.cardWidth
                height: cards.height

                PersonCard {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.verticalCenter: parent.verticalCenter
                    person: personCell.modelData
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

            // A vertical step keeps the column (NavigationColumn): Down out of
            // the fourth chip lands under it, not on wherever this shelf was
            // last parked. Both halves are needed — one publishes the column,
            // the other consumes it — and neither fires for a click, a Tab or a
            // restored back-stack cursor, because only Up/Down arm it.
            onActiveFocusChanged: {
                if (cards.activeFocus)
                    NavigationColumn.applyTo(cards)
            }

            Keys.onPressed: event => NavigationColumn.noteFromKey(cards, event.key)
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

        // Mouse affordances only, as on StrmRail: a shelf is one Tab stop, and
        // a hovered one must not slip its chevrons into the Tab chain or offer
        // them to an arrow. A click still gives a button the keyboard, and at
        // the end of the shelf it then disables itself — so it goes straight
        // back to the cards.
        StrmIconButton {
            activeFocusOnTab: false
            onActiveFocusChanged: if (activeFocus) cards.forceActiveFocus(Qt.OtherFocusReason)
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
            activeFocusOnTab: false
            onActiveFocusChanged: if (activeFocus) cards.forceActiveFocus(Qt.OtherFocusReason)
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

    // ── Formatting ─────────────────────────────────────────────────────────
    function formatRuntime(ms) {
        if (!ms || ms <= 0)
            return ""
        var minutes = Math.round(ms / 60000)
        return minutes >= 60 ? Math.floor(minutes / 60) + " h " + (minutes % 60) + " min"
                             : minutes + " min"
    }

    function formatPosition(ms) {
        var totalSeconds = Math.floor(ms / 1000)
        var hours = Math.floor(totalSeconds / 3600)
        var minutes = Math.floor((totalSeconds / 60) % 60)
        var seconds = totalSeconds % 60
        function pad(value) { return (value < 10 ? "0" : "") + value }
        return hours > 0 ? hours + ":" + pad(minutes) + ":" + pad(seconds)
                         : minutes + ":" + pad(seconds)
    }

    function syncUserState() {
        page.itemPlayed = page.itemId.length > 0 && Actions.isPlayed(page.itemId)
        page.itemFavorite = page.itemId.length > 0 && Actions.isFavorite(page.itemId)
    }

    function episodeCode(item) {
        if (!item)
            return ""
        return MediaKinds.episodeCode(item.parentIndexNumber, item.indexNumber)
    }

    readonly property var nextEpisodeItem: (page.isSeries && DetailsCtl.hasNextEpisode) ? DetailsCtl.nextEpisode : ({})
    readonly property string nextEpisodeCode: page.episodeCode(page.nextEpisodeItem)

    readonly property string nextEpisodeLine: {
        if (!page.isSeries || !DetailsCtl.hasNextEpisode)
            return ""
        const parts = []
        if (page.nextEpisodeCode.length > 0)
            parts.push(page.nextEpisodeCode)
        if (page.nextEpisodeItem.name)
            parts.push(String(page.nextEpisodeItem.name))
        const runtime = page.formatRuntime(page.nextEpisodeItem.runtimeMs)
        if (runtime.length > 0)
            parts.push(runtime)
        return parts.join("  ·  ")
    }

    // ── Trailers (ARCHITECTURE.md) ───────────────────────────────────────────
    // {name, url} straight from the server's RemoteTrailers. They leave the
    // application: openTrailer() hands the URL to the desktop, exactly as the
    // external-links strip does, and nothing here tries to hand one to mpv.
    readonly property var trailers: DetailsCtl.trailers
    readonly property int trailerCount: page.trailers ? page.trailers.length : 0

    function openTrailer(index) {
        if (index < 0 || index >= page.trailerCount)
            return
        const trailer = page.trailers[index]
        // In the app when it is a YouTube video and this build has YouTube;
        // anything else is a link, and leaves for the browser.
        if (trailer && trailer.url && !Actions.playOnYouTube(String(trailer.url)))
            Qt.openUrlExternally(String(trailer.url))
    }

    // More than one — a teaser and a full trailer, say. The button opens the
    // list rather than silently picking the first, which would make the other
    // ones unreachable.
    function showTrailerMenu() {
        const acts = []
        for (var i = 0; i < page.trailerCount; ++i) {
            const trailer = page.trailers[i]
            const name = trailer && trailer.name ? String(trailer.name) : ""
            acts.push({ text: name.length > 0 ? name : qsTr("Trailer %1").arg(i + 1),
                        iconName: "external-link" })
        }
        trailerMenu.actions = acts
        const p = trailerButton.mapToItem(null, 0, trailerButton.height)
        trailerMenu.popupAt(p.x, p.y)
    }

    // ── Version formatting ─────────────────────────────────────────────────
    // Keys come from MediaSource::toVariantMap(); every one of them is optional
    // on the wire, so each part is omitted rather than printed empty.
    function formatBitrate(bps) {
        if (!bps || bps <= 0)
            return ""
        return (bps / 1000000).toFixed(1) + " Mbps"
    }

    function formatSize(bytes) {
        if (!bytes || bytes <= 0)
            return ""
        return bytes >= 1000000000 ? (bytes / 1000000000).toFixed(1) + " GB"
                                   : Math.round(bytes / 1000000) + " MB"
    }

    // What the option in the dropdown says: the server's name for the version,
    // then the facts that distinguish it from the one below it.
    function versionTitle(source) {
        if (!source)
            return ""
        const name = source.displayName ? String(source.displayName) : qsTr("Version")
        const extras = []
        const resolution = source.resolutionLabel ? String(source.resolutionLabel) : ""
        if (resolution.length > 0 && name.indexOf(resolution) < 0)
            extras.push(resolution)
        if (source.isHdr === true)
            extras.push("HDR")
        const bitrate = page.formatBitrate(source.bitrate)
        if (bitrate.length > 0)
            extras.push(bitrate)
        const size = page.formatSize(source.size)
        if (size.length > 0)
            extras.push(size)
        return extras.length > 0 ? name + "  ·  " + extras.join("  ·  ") : name
    }

    // The technical readout under the picker — mono, because it is gear
    // labelling and every field of it is tabular (ARCHITECTURE.md).
    function versionSpec(source) {
        if (!source)
            return ""
        const video = source.videoStream ? source.videoStream : ({})
        const parts = []
        if (source.container)
            parts.push(String(source.container).toUpperCase())
        if (video.codec) {
            const depth = video.bitDepth > 8 ? " " + video.bitDepth + "-bit" : ""
            parts.push(String(video.codec).toUpperCase() + depth)
        }
        if (video.width > 0 && video.height > 0)
            parts.push(video.width + "×" + video.height)
        if (source.isHdr === true && video.videoRange)
            parts.push(String(video.videoRange).toUpperCase())
        const audio = source.audioStreams ? source.audioStreams.length : 0
        if (audio > 0)
            parts.push(qsTr("%1 audio").arg(audio))
        const subs = source.subtitleStreams ? source.subtitleStreams.length : 0
        if (subs > 0)
            parts.push(qsTr("%1 subtitle").arg(subs))
        return parts.join("  ·  ")
    }

    readonly property var versionModel: {
        const out = []
        const list = DetailsCtl.mediaSources
        for (var i = 0; i < list.length; ++i)
            out.push({ text: page.versionTitle(list[i]), value: i })
        return out
    }

    // Sources arrive with the details payload, so the remembered choice can
    // only be resolved once they are here. Matched by MediaSource id, never by
    // position: the server is free to reorder them between requests.
    function restoreVersion() {
        const list = DetailsCtl.mediaSources
        if (!list || list.length === 0) {
            page.versionIndex = 0
            return
        }
        if (page.versionMemory && page.itemId.length > 0) {
            const wanted = Prefs.rememberedVersion(page.itemId)
            if (wanted && wanted.length > 0) {
                for (var i = 0; i < list.length; ++i) {
                    if (list[i].id === wanted) {
                        page.versionIndex = i
                        return
                    }
                }
            }
        }
        page.versionIndex = 0
    }

    function selectVersion(index) {
        const list = DetailsCtl.mediaSources
        if (index < 0 || !list || index >= list.length)
            return
        page.versionIndex = index
        const source = list[index]
        if (page.versionMemory && page.itemId.length > 0 && source && source.id)
            Prefs.rememberVersion(page.itemId, String(source.id))
    }

    // Handed to the player *after* the verb, because playItem() resets the
    // preference to "let the ticket decide" and would otherwise overwrite it.
    // Deliberately not called on selection: a session playing some *other* item
    // must not have its source switched from this page.
    function applyVersion() {
        if (page.multiVersion && page.versionIndex >= 0)
            PlayerCtl.setPreferredSource(page.versionIndex)
    }

    readonly property string metaText: {
        var parts = []
        if (page.item.year > 0)
            parts.push(String(page.item.year))
        if (page.item.officialRating)
            parts.push(page.item.officialRating)
        if (page.item.runtimeMs > 0)
            parts.push(page.formatRuntime(page.item.runtimeMs))
        return parts.join("  ·  ")
    }

    // The full release date, which the year in metaText only summarises. Empty
    // — and so omitted entirely — when the server sent nothing parseable.
    readonly property string premiereText: {
        if (!DetailsCtl.premiereDate || DetailsCtl.premiereDate.length === 0)
            return ""
        var when = new Date(DetailsCtl.premiereDate)
        if (isNaN(when.getTime()))
            return ""
        return when.toLocaleDateString(Qt.locale(), Locale.LongFormat)
    }

    // ── Vertical navigation ────────────────────────────────────────────────
    // Sections come and go with the payload (no crew, no studios, no links on a
    // sparse item), so nothing here hardcodes what is above or below it.
    //
    // Down between sections is the navigator's (FocusNavigator.h): they are a
    // column, and the next one that is on screen is the next one down. Up is
    // named, from this list, because the top of the column is not a matter of
    // layout — it leads to the hero's primary verb (or the version picker),
    // not to whichever button happens to sit over the card. The hero's own
    // Down is named for the same reason. They are names, not KeyNavigation
    // links: a link accepts the key whether or not its target is there.
    //
    // "Part of" sits second, directly under the genres: it is a *where does
    // this belong* fact, not trivia, and it is also the one section on this
    // page that arrives on its own signal (collectionsChanged, a second request
    // that lands after the details) — so it must be able to appear mid-scroll
    // without renumbering anything. Walking this list is what makes that free.
    readonly property var navSections: [episodesRail, genreRow, collectionRow, castRow, crewRow,
                                        studioRow, linkRow, similarRail]

    function sectionBelow(start) {
        for (var i = start; i < page.navSections.length; ++i) {
            if (page.navSections[i].visible)
                return page.navSections[i]
        }
        return null
    }

    function sectionAbove(start) {
        for (var i = start; i >= 0; --i) {
            if (page.navSections[i].visible)
                return page.navSections[i]
        }
        return page.heroExit
    }

    // ── Horizontal navigation across the verbs ─────────────────────────────
    // A list walked by index, in the same order the Row draws them, exactly as
    // SeriesPage does it. The hand-written ternary chains this replaces were
    // already three levels deep with six buttons; at eleven — where "the button
    // to my left" depends on the item type, on whether it resumes, on whether
    // the server sent a trailer — they are unmaintainable, and each new verb
    // meant editing the two neighbours it was inserted between.
    readonly property var heroVerbs: [playNextSeriesButton, episodesButton, playAllButton, viewItemsButton,
                                      playButton, startOverButton, shuffleButton,
                                      trailerButton, playedButton, favoriteButton,
                                      playlistButton, moreButton]

    function verbAfter(index) {
        for (var i = index + 1; i < page.heroVerbs.length; ++i) {
            if (page.heroVerbs[i].visible)
                return page.heroVerbs[i]
        }
        return null
    }

    function verbBefore(index) {
        for (var i = index - 1; i >= 0; --i) {
            if (page.heroVerbs[i].visible)
                return page.heroVerbs[i]
        }
        return null
    }

    // ── Rows of verbs ──────────────────────────────────────────────────────
    // The verbs wrap (see buttonRow), so Up and Down have to be answered among
    // them before they leave the hero: the nearest verb in the next row that
    // way, by centre. Null when there is no such row — the caller then names
    // the hero's own exit (Down) or leaves the key to the navigator (Up).
    // Left and Right keep walking heroVerbs in reading order, so the end of
    // one row continues at the start of the next and nothing is skipped.
    function verbOnAdjacentRow(button, step) {
        let best = null
        let bestRow = 0
        let bestOffset = 0
        const centre = button.x + button.width / 2
        for (var i = 0; i < page.heroVerbs.length; ++i) {
            const verb = page.heroVerbs[i]
            if (!verb.visible || (verb.y - button.y) * step <= 0)
                continue
            const row = Math.abs(verb.y - button.y)
            const offset = Math.abs(verb.x + verb.width / 2 - centre)
            if (best === null || row < bestRow || (row === bestRow && offset < bestOffset)) {
                best = verb
                bestRow = row
                bestOffset = offset
            }
        }
        return best
    }

    function verbBelow(button) {
        const below = page.verbOnAdjacentRow(button, 1)
        return below ? below : page.heroDown
    }

    // Where Up out of the first section lands: whichever primary verb this item
    // type actually shows.
    readonly property Item heroButton: {
        for (var i = 0; i < page.heroVerbs.length; ++i) {
            if (page.heroVerbs[i].visible)
                return page.heroVerbs[i]
        }
        return moreButton
    }

    // The bottom and top of the hero for arrow purposes. The version picker
    // sits between the action row and the first section, so it takes both ends
    // of that chain whenever the item has one.
    readonly property Item heroDown: page.multiVersion ? versionSelect : page.sectionBelow(0)
    readonly property Item heroExit: page.multiVersion ? versionSelect : page.heroFoot

    // The first verb of the LAST row: what is directly above whatever follows
    // the verbs. The same button as heroButton while they fit on one line.
    readonly property Item heroFoot: {
        let foot = null
        for (var i = 0; i < page.heroVerbs.length; ++i) {
            const verb = page.heroVerbs[i]
            if (verb.visible && (foot === null || verb.y > foot.y))
                foot = verb
        }
        return foot ? foot : moreButton
    }

    // ── Where the keyboard starts ──────────────────────────────────────────
    // On the primary verb, chosen once the payload that decides which verb
    // that is has arrived — and chosen ONCE. The four candidates used to bind
    // `focus:` to their own conditions, and a binding is re-run by everything
    // it reads: marking an episode watched moved "Play next", which took the
    // keyboard back from the button that had just been pressed.
    //
    // `focus`, not forceActiveFocus(): this is the hero's default, and must
    // not pull the keyboard up from a shelf that history restored it to.
    property bool focusSeated: false

    function seatFocus() {
        // Not while covered: `visible` is false for every verb then, and the
        // first one on screen is what this is choosing.
        if (page.focusSeated || page.detailsLoading || !page.visible)
            return
        page.heroButton.focus = true
        page.focusSeated = true
    }

    // Deferred: the verbs' visibility hangs off the same payload this flag
    // reports, and the order the two are updated in is not ours to rely on.
    onDetailsLoadingChanged: Qt.callLater(page.seatFocus)

    onItemIdChanged: {
        page.syncUserState()
        page.focusSeated = false
    }

    function ensureDetailsScope(): void {
        if (page.itemId.length === 0) {
            page.detailsLoading = false
            loadGuard.stop()
            return
        }
        // Main may already have prepared this restored route. The controller's
        // ensure joins either its live request or its successfully retained
        // state; a failure/loadPerson cancellation clears itemId and therefore
        // turns the same call into a retry.
        DetailsCtl.ensureLoaded(page.itemId)
        page.detailsLoading = DetailsCtl.itemId === page.itemId && DetailsCtl.itemLoading
        if (page.detailsLoading)
            loadGuard.restart()
        else
            loadGuard.stop()
    }

    Component.onCompleted: {
        page.ensureDetailsScope()
        page.syncUserState()
    }

    onVisibleChanged: {
        if (page.visible) {
            Qt.callLater(page.ensureDetailsScope)
            Qt.callLater(page.seatFocus)
        }
    }

    Connections {
        target: Actions

        function onPlayedChanged(itemId, played) {
            if (itemId === page.itemId)
                page.itemPlayed = played
        }
        function onFavoriteChanged(itemId, favorite) {
            if (itemId === page.itemId)
                page.itemFavorite = favorite
        }
    }

    Connections {
        target: DetailsCtl

        function onDetailsChanged() {
            page.detailsLoading = DetailsCtl.itemId === page.itemId
                                  && DetailsCtl.itemLoading
            if (page.detailsLoading) {
                loadGuard.restart()
            } else {
                loadGuard.stop()
                if (DetailsCtl.itemId === page.itemId)
                    page.restoreVersion()
            }
        }
    }

    Timer {
        id: loadGuard
        interval: 6000
        onTriggered: page.detailsLoading = false
    }

    // ── Backdrop ───────────────────────────────────────────────────────────
    // Anchored to the page, not to the scrolling content: the artwork is the
    // room the page sits in, so it stays put while the content moves over it.
    StrmImage {
        id: backdrop
        anchors.fill: parent
        source: page.item.backdropUrl ? page.item.backdropUrl : ""
        sourceSize.width: 1280
        readyOpacity: 0.35
        fadeDuration: Theme.animSlow
    }

    // Gentle scrim so the metadata stays legible over any artwork.
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 0.45; color: Theme.veil }
            GradientStop { position: 0.85; color: Theme.ground }
        }
    }

    // Right-click anywhere on the page opens the same verbs as the ⋯ button.
    // Chips and cards accept the left button only, so a right-click over one of
    // them still lands here.
    TapHandler {
        acceptedButtons: Qt.RightButton
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: eventPoint => {
            var p = page.mapToItem(null, eventPoint.position.x, eventPoint.position.y)
            page.menuOrigin = null
            itemMenu.popupForItemNoDetails(page.item, p.x, p.y)
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

    // Keyboard and gamepad focus must drag the viewport with it.
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

    NumberAnimation {
        id: scrollAnim
        target: scroll
        property: "contentY"
        duration: Theme.animNormalMs
        easing.type: Theme.easeStandard
    }

    Flickable {
        id: scroll

        anchors.fill: parent
        contentWidth: width
        contentHeight: content.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        interactive: scroll.contentHeight > scroll.height
        clip: true
        // Opacity, not visibility: an invisible item cannot hold focus, and the
        // hero scope holds it while this is still loading (seatFocus() picks
        // the button once it can). A hidden body would hand the page back with
        // nothing focused.
        opacity: page.detailsLoading ? 0.0 : 1.0

        ScrollBar.vertical: StrmScrollBar {}

        Behavior on opacity {
            NumberAnimation { duration: Theme.animNormalMs; easing.type: Theme.easeStandard }
        }

        Column {
            id: content

            width: scroll.width
            spacing: Theme.railGap

            // Page padding as spacers rather than Flickable margins, which
            // would move contentY's origin off zero and make every scroll
            // calculation on this page carry an offset. The Column's own
            // spacing counts towards the margin, so it is subtracted here
            // instead of being added on top of it.
            Item { width: 1; height: Math.max(0, Theme.pageMarginValue - content.spacing) }

            // ── Hero ───────────────────────────────────────────────────────
            // A FocusScope so the page can tell that *any* action button holds
            // focus and scroll back to the top, instead of eight identical
            // onActiveFocusChanged handlers saying the same thing.
            FocusScope {
                id: heroScope

                width: content.width
                height: hero.implicitHeight
                focus: true

                onActiveFocusChanged: {
                    if (heroScope.activeFocus)
                        page.scrollTo(0)
                }

                Row {
                    id: hero

                    x: Theme.pageMarginValue
                    width: content.width - 2 * Theme.pageMarginValue
                    spacing: Theme.spacingLoose

                    Rectangle {
                        id: posterFrame
                        width: page.posterW
                        height: page.posterH
                        radius: Theme.radiusCardValue
                        color: Theme.surfaceColor
                        clip: true

                        StrmImage {
                            id: poster
                            anchors.fill: parent
                            source: page.item.posterUrl ? page.item.posterUrl : ""
                        }
                    }

                    Column {
                        id: infoColumn

                        width: hero.width - posterFrame.width - hero.spacing
                        spacing: Theme.spacingValue

                        // Prose gets a measure, not the whole width: on a wide
                        // window the synopsis would otherwise run ~150
                        // characters to the line and be unreadable. Headings,
                        // buttons and metadata still use the full column.
                        readonly property int proseWidth: Math.min(infoColumn.width,
                                                                   Theme.scale(820))

                        Text {
                            width: parent.width
                            text: page.item.name ? page.item.name : ""
                            color: Theme.textPrimaryColor
                            font.family: Theme.fontDisplay
                            font.pixelSize: Theme.fontDisplaySize
                            font.weight: Font.DemiBold
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }

                        Text {
                            width: parent.width
                            visible: page.isEpisode
                            text: (page.item.seriesName ? page.item.seriesName : "")
                                  + (page.item.parentIndexNumber >= 0
                                     ? "  ·  " + qsTr("Season %1, Episode %2")
                                           .arg(page.item.parentIndexNumber).arg(page.item.indexNumber)
                                     : "")
                            color: Theme.textSecondaryColor
                            font.family: Theme.fontBody
                            font.pixelSize: Theme.fontTitle
                            elide: Text.ElideRight
                        }

                        Row {
                            spacing: Theme.spacingValue

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                visible: page.metaText.length > 0
                                text: page.metaText
                                color: Theme.textSecondaryColor
                                font.family: Theme.fontBody
                                font.pixelSize: Theme.fontBodySize
                            }

                            Row {
                                anchors.verticalCenter: parent.verticalCenter
                                visible: page.item.communityRating > 0
                                spacing: Theme.scale(6)

                                StrmIcon {
                                    anchors.verticalCenter: parent.verticalCenter
                                    name: "star"
                                    size: Theme.fontBodySize
                                    color: Theme.accentColor
                                }

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: Number(page.item.communityRating).toFixed(1)
                                    color: Theme.accentColor
                                    font.family: Theme.fontMono
                                    font.pixelSize: Theme.fontBodySize
                                }
                            }

                            // Critics, kept visually distinct from the audience
                            // score beside it: a percentage in the "fresh"
                            // green or the "rotten" red, never a second star.
                            Row {
                                anchors.verticalCenter: parent.verticalCenter
                                visible: DetailsCtl.criticRating > 0
                                spacing: Theme.scale(6)

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: Math.round(DetailsCtl.criticRating) + "%"
                                    color: DetailsCtl.criticRating >= 60 ? Theme.positive
                                                                         : Theme.negative
                                    font.family: Theme.fontMono
                                    font.pixelSize: Theme.fontBodySize
                                }

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: qsTr("critics")
                                    color: Theme.textTertiary
                                    font.family: Theme.fontBody
                                    font.pixelSize: Theme.fontSmall
                                }
                            }

                            // The count that used to stand here moved into the
                            // version picker below the action row, which says
                            // the same thing and can be acted on.
                        }

                        // ── Actions ────────────────────────────────────────
                        // Every one of these is reachable by click and by
                        // keyboard, and the Left/Right chain skips whatever is
                        // hidden for this item type.
                        //
                        // A Flow, not a Row: a film with a resume point and a
                        // trailer shows seven of these, which at TV density or
                        // in a small window is wider than the column beside
                        // the poster, and a Row ran off its right-hand edge
                        // with the last verbs out of sight. They wrap
                        // instead of folding into ⋯, because the menu does not
                        // carry Trailer and a verb that moves into a menu at
                        // one window size is a verb that has to be looked for.
                        Flow {
                            id: buttonRow
                            width: parent.width
                            spacing: Theme.spacingTight

                            StrmButton {
                                id: playNextSeriesButton
                                visible: page.isSeries && DetailsCtl.hasNextEpisode
                                text: {
                                    const code = page.nextEpisodeCode;
                                    const resumable = page.nextEpisodeItem && page.nextEpisodeItem.resumable === true;
                                    if (resumable) {
                                        return code.length > 0
                                            ? qsTr("Resume  ·  %1").arg(code)
                                            : qsTr("Resume");
                                    }
                                    return code.length > 0
                                        ? qsTr("Play next  ·  %1").arg(code)
                                        : qsTr("Play next");
                                }
                                iconName: "play"
                                variant: "primary"
                                onClicked: Actions.play(DetailsCtl.nextEpisode)
                                property Item navRight: page.verbAfter(0)
                                property Item navUp: page.verbOnAdjacentRow(playNextSeriesButton, -1)
                                property Item navDown: page.verbBelow(playNextSeriesButton)
                            }

                            StrmButton {
                                id: episodesButton
                                visible: page.isSeries
                                text: qsTr("Episodes")
                                iconName: "list"
                                variant: (page.isSeries && DetailsCtl.hasNextEpisode) ? "secondary" : "primary"
                                onClicked: Actions.performItemVerb("series", page.item)
                                property Item navLeft: page.verbBefore(1)
                                property Item navRight: page.verbAfter(1)
                                property Item navUp: page.verbOnAdjacentRow(episodesButton, -1)
                                property Item navDown: page.verbBelow(episodesButton)
                            }

                            // A collection has no stream of its own, so its
                            // primary verb is "play the set", not "play this".
                            StrmButton {
                                id: playAllButton
                                visible: page.isCollection
                                text: qsTr("Play all")
                                iconName: "play"
                                variant: "primary"
                                onClicked: Actions.performItemVerb("play", page.item)
                                property Item navLeft: page.verbBefore(2)
                                property Item navRight: page.verbAfter(2)
                                property Item navUp: page.verbOnAdjacentRow(playAllButton, -1)
                                property Item navDown: page.verbBelow(playAllButton)
                            }

                            // The collection's own way in, the exact counterpart
                            // of "Episodes" on a series (E4). Without it a
                            // BoxSet could only be played whole or not at all:
                            // the members were reachable from a member's own
                            // "Part of" chip and from nowhere else, which is a
                            // door that only opens from the inside.
                            StrmButton {
                                id: viewItemsButton
                                visible: page.isCollection
                                text: qsTr("View items")
                                iconName: "lib-collections"
                                onClicked: Actions.performItemVerb("browseCollection", page.item)
                                property Item navLeft: page.verbBefore(3)
                                property Item navRight: page.verbAfter(3)
                                property Item navUp: page.verbOnAdjacentRow(viewItemsButton, -1)
                                property Item navDown: page.verbBelow(viewItemsButton)
                            }

                            StrmButton {
                                id: playButton
                                visible: page.isPlayable
                                text: page.resumable
                                      ? qsTr("Resume %1").arg(page.formatPosition(page.resumePositionMs))
                                      : qsTr("Play")
                                iconName: "play"
                                variant: "primary"
                                onClicked: {
                                    Actions.performItemVerb("play", page.playItem)
                                    page.applyVersion()
                                }
                                property Item navLeft: page.verbBefore(4)
                                property Item navRight: page.verbAfter(4)
                                property Item navUp: page.verbOnAdjacentRow(playButton, -1)
                                property Item navDown: page.verbBelow(playButton)
                            }

                            StrmButton {
                                id: startOverButton
                                visible: page.isPlayable && page.resumable
                                text: qsTr("Start over")
                                iconName: "skip-previous"
                                onClicked: {
                                    Actions.performItemVerb("playFromStart", page.playItem)
                                    page.applyVersion()
                                }
                                property Item navLeft: page.verbBefore(5)
                                property Item navRight: page.verbAfter(5)
                                property Item navUp: page.verbOnAdjacentRow(startOverButton, -1)
                                property Item navDown: page.verbBelow(startOverButton)
                            }

                            // Shuffle sits next to Episodes / Play all, for the
                            // two item kinds that *are* a set (ARCHITECTURE.md).
                            StrmButton {
                                id: shuffleButton
                                visible: page.capabilities.shuffle === true
                                text: qsTr("Shuffle")
                                iconName: "shuffle"
                                onClicked: Actions.performItemVerb("shuffle", page.item)
                                property Item navLeft: page.verbBefore(6)
                                property Item navRight: page.verbAfter(6)
                                property Item navUp: page.verbOnAdjacentRow(shuffleButton, -1)
                                property Item navDown: page.verbBelow(shuffleButton)
                            }

                            // ── Trailer (ARCHITECTURE.md) ────────────────────
                            // Absent, not disabled, when the server sent no
                            // RemoteTrailers — which is the common case for a
                            // film, so a permanently greyed-out button here
                            // would be the most-seen state of the control.
                            //
                            // The external-link glyph is the honest one: these
                            // are YouTube URLs and this opens a browser. mpv
                            // could only take them with yt-dlp installed, which
                            // is not a dependency this app declares, so playing
                            // one in the app would work on the developer's
                            // machine and fail on the user's.
                            StrmButton {
                                id: trailerButton
                                visible: page.trailerCount > 0
                                text: page.trailerCount > 1 ? qsTr("Trailers")
                                                            : qsTr("Trailer")
                                iconName: "external-link"
                                onClicked: {
                                    if (page.trailerCount === 1)
                                        page.openTrailer(0)
                                    else
                                        page.showTrailerMenu()
                                }
                                property Item navLeft: page.verbBefore(7)
                                property Item navRight: page.verbAfter(7)
                                property Item navUp: page.verbOnAdjacentRow(trailerButton, -1)
                                property Item navDown: page.verbBelow(trailerButton)
                            }

                            // Named for what clicking it does, in both states.
                            // It used to read "Watched" once the item was —
                            // which is a state, not a verb, and left "mark as
                            // unplayed" (E12, and one of Emby's own
                            // userdatabuttons) reachable only by guessing that
                            // the state label was also a button that undid it.
                            // The wording is ItemMenu's, so the menu row and
                            // the button never say different things about the
                            // same click.
                            StrmButton {
                                id: playedButton
                                visible: page.capabilities.markPlayed === true
                                text: page.itemPlayed ? qsTr("Mark unwatched")
                                                      : qsTr("Mark watched")
                                iconName: page.itemPlayed ? "eye-off" : "eye"
                                // The verb, not a local flip: Actions owns the
                                // value and tells every view about the change.
                                onClicked: Actions.performItemVerb("played", page.item)
                                property Item navLeft: page.verbBefore(8)
                                property Item navRight: page.verbAfter(8)
                                property Item navUp: page.verbOnAdjacentRow(playedButton, -1)
                                property Item navDown: page.verbBelow(playedButton)
                            }

                            StrmButton {
                                id: favoriteButton
                                visible: page.capabilities.favorite === true
                                text: page.itemFavorite ? qsTr("Favourite") : qsTr("Add favourite")
                                iconName: page.itemFavorite ? "heart-filled" : "heart"
                                onClicked: Actions.performItemVerb("favorite", page.item)
                                property Item navLeft: page.verbBefore(9)
                                property Item navRight: page.verbAfter(9)
                                property Item navUp: page.verbOnAdjacentRow(favoriteButton, -1)
                                property Item navDown: page.verbBelow(favoriteButton)
                            }

                            // ── Add to playlist (ARCHITECTURE.md) ────────
                            // A verb of its own rather than only a menu row:
                            // it is the one way an item gets into a playlist,
                            // and burying the only door inside ⋯ is how a
                            // feature ships and is never found.
                            StrmButton {
                                id: playlistButton
                                visible: page.capabilities.addToPlaylist === true
                                text: qsTr("Add to playlist")
                                iconName: "playlist"
                                onClicked: page.addToPlaylist(page.item, playlistButton)
                                property Item navLeft: page.verbBefore(10)
                                property Item navRight: page.verbAfter(10)
                                property Item navUp: page.verbOnAdjacentRow(playlistButton, -1)
                                property Item navDown: page.verbBelow(playlistButton)
                            }

                            StrmIconButton {
                                id: moreButton
                                iconName: "more-horizontal"
                                tooltip: qsTr("More actions")
                                onClicked: {
                                    var p = moreButton.mapToItem(null, 0, moreButton.height)
                                    page.menuOrigin = moreButton
                                    itemMenu.popupForItemNoDetails(page.item, p.x, p.y)
                                }
                                property Item navLeft: page.verbBefore(11)
                                property Item navUp: page.verbOnAdjacentRow(moreButton, -1)
                                property Item navDown: page.verbBelow(moreButton)
                            }
                        }

                        // UP NEXT indicator for series
                        Row {
                            width: parent.width
                            visible: page.isSeries && page.nextEpisodeLine.length > 0
                            spacing: Theme.spacingTight

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("UP NEXT")
                                color: Theme.textTertiary
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.fontCaption
                                font.letterSpacing: Theme.fontCaption * Theme.trackLabel
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                width: infoColumn.proseWidth - Theme.scale(90)
                                text: page.nextEpisodeLine
                                color: Theme.accentColor
                                font.family: Theme.fontBody
                                font.pixelSize: Theme.fontSmall
                                elide: Text.ElideRight
                                maximumLineCount: 1
                            }
                        }

                        // ── Version picker (ARCHITECTURE.md) ──────────────────
                        // Directly under the verbs it changes the meaning of,
                        // because that is the only place it reads as "this is
                        // what Play will play" rather than as trivia.
                        Column {
                            id: versionBlock

                            visible: page.multiVersion
                            width: infoColumn.proseWidth
                            spacing: Theme.spacingTight

                            Row {
                                spacing: Theme.spacingTight

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: qsTr("VERSION")
                                    color: Theme.textTertiary
                                    font.family: Theme.fontMono
                                    font.pixelSize: Theme.fontCaption
                                    font.letterSpacing: Theme.fontCaption * Theme.trackLabel
                                }

                                StrmSelect {
                                    id: versionSelect

                                    anchors.verticalCenter: parent.verticalCenter
                                    // Long names elide inside the control
                                    // rather than pushing the synopsis around.
                                    width: Math.min(versionSelect.implicitWidth,
                                                    Math.max(Theme.scale(200),
                                                             infoColumn.proseWidth
                                                             - Theme.scale(110)))
                                    model: page.versionModel
                                    // page.versionIndex is the only copy of the
                                    // choice: the label reads it, Play reads it,
                                    // and restoreVersion() writes it when the
                                    // remembered source resolves. The select
                                    // asks and never writes, so the two cannot
                                    // drift apart.
                                    currentIndex: page.versionIndex
                                    onActivated: index => page.selectVersion(index)

                                    property Item navUp: page.heroFoot
                                    property Item navDown: page.sectionBelow(0)
                                }
                            }

                            Text {
                                width: parent.width
                                visible: text.length > 0
                                text: page.versionSpec(page.selectedVersion)
                                color: Theme.textSecondaryColor
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.fontCaption
                                elide: Text.ElideRight
                                maximumLineCount: 1
                            }
                        }

                        Text {
                            width: infoColumn.proseWidth
                            visible: DetailsCtl.tagline.length > 0
                            text: DetailsCtl.tagline
                            color: Theme.textSecondaryColor
                            font.family: Theme.fontBody
                            font.pixelSize: Theme.fontBodySize
                            font.italic: true
                            elide: Text.ElideRight
                        }

                        // The page scrolls now, so the synopsis is no longer
                        // cut off at six lines.
                        Text {
                            width: infoColumn.proseWidth
                            visible: !!page.item.overview
                            text: page.item.overview ? page.item.overview : ""
                            color: Theme.textPrimaryColor
                            font.family: Theme.fontBody
                            font.pixelSize: Theme.fontBodySize
                            wrapMode: Text.Wrap
                            lineHeight: Theme.lineNormal
                        }

                        // A gear label and its value: the release date that the
                        // year in the meta line only approximates. Omitted
                        // whole when the server has no date.
                        Row {
                            visible: page.premiereText.length > 0
                            spacing: Theme.spacingTight

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("RELEASED")
                                color: Theme.textTertiary
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.fontCaption
                                font.letterSpacing: Theme.fontCaption * Theme.trackLabel
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: page.premiereText
                                color: Theme.textSecondaryColor
                                font.family: Theme.fontBody
                                font.pixelSize: Theme.fontSmall
                            }
                        }
                    }
                }
            }

            // ── Next episodes ──────────────────────────────────────────────
            StrmRail {
                id: episodesRail
                navigationFocusKey: "details-episodes"
                navigationFocusFallbackItem: page.heroButton
                navigationFocusRefillActive: DetailsCtl.itemId === page.itemId
                                             && DetailsCtl.upcomingEpisodesLoading
                visible: page.isSeries && DetailsCtl.upcomingEpisodes.count > 0
                title: qsTr("Next episodes")
                railModel: DetailsCtl.upcomingEpisodes
                cardVariant: "still"
                preferItemName: true
                showMore: true
                emptyText: ""

                onActiveFocusChanged: {
                    if (episodesRail.activeFocus)
                        page.ensureVisible(episodesRail)
                }
                property Item navUp: page.heroExit

                onMoreRequested: Actions.performItemVerb("series", page.item)
                onItemActivated: index => Actions.play(DetailsCtl.upcomingEpisodes.get(index))
                onItemPlayRequested: index => Actions.play(DetailsCtl.upcomingEpisodes.get(index))
                onItemPlayedToggled: index => Actions.togglePlayed(DetailsCtl.upcomingEpisodes.get(index))
                onItemFavoriteToggled: index => Actions.toggleFavorite(DetailsCtl.upcomingEpisodes.get(index))
                onMenuRequested: (index, mx, my) => {
                    page.menuOrigin = episodesRail
                    itemMenu.popupForItem(DetailsCtl.upcomingEpisodes.get(index), mx, my)
                }
            }

            // ── Genres ─────────────────────────────────────────────────────
            ChipStrip {
                id: genreRow
                title: qsTr("Genres")
                chipModel: DetailsCtl.genres
                onChipActivated: index => {
                    var genre = DetailsCtl.genres[index]
                    Actions.browseGenre(genre.id ? genre.id : "", genre.name)
                }
                onActiveFocusChanged: { if (genreRow.activeFocus) page.ensureVisible(genreRow) }
                property Item navUp: page.sectionAbove(0)
            }

            // ── Part of (E4) ───────────────────────────────────────────────
            // The collections this item belongs to, each one a way in. Most
            // items are in none, and DetailsCtl.collections arrives on its own
            // signal after the details have already been drawn — so the strip
            // must be *absent*, not empty: ChipStrip binds both its height and
            // its visibility to the model's length, and a Column skips
            // invisible children entirely, so an item in no collection leaves
            // no gap and one in three grows the page when the answer lands.
            //
            // The fifth use of ChipStrip and not a fifth pattern: a collection
            // is exactly what a chip strip is for — a short list of named
            // places, every one of them navigable, none of them ranked.
            ChipStrip {
                id: collectionRow
                title: qsTr("Part of")
                chipModel: DetailsCtl.collections
                iconName: "lib-collections"
                onChipActivated: index => {
                    var set = DetailsCtl.collections[index]
                    if (set)
                        Actions.browseCollection(set.id ? set.id : "", set.name)
                }
                onActiveFocusChanged: {
                    if (collectionRow.activeFocus)
                        page.ensureVisible(collectionRow)
                }
                property Item navUp: page.sectionAbove(1)
            }

            // ── Cast ───────────────────────────────────────────────────────
            // Billing order, exactly as the server sent it.
            CastShelf {
                id: castRow
                title: qsTr("Cast")
                people: DetailsCtl.cast
                onPersonActivated: index => {
                    var person = DetailsCtl.cast[index]
                    Actions.browsePerson(person.id ? person.id : "", person.name)
                }
                onActiveFocusChanged: { if (castRow.activeFocus) page.ensureVisible(castRow) }
                property Item navUp: page.sectionAbove(2)
            }

            // ── Crew ───────────────────────────────────────────────────────
            // Director, writers, producers: chips rather than cards, because
            // the server rarely has a headshot for them and a row of initials
            // is a worse answer than a row of names. `type` is the job here —
            // Emby fills Role for actors and leaves it empty for crew.
            ChipStrip {
                id: crewRow
                title: qsTr("Crew")
                chipModel: DetailsCtl.crew
                sublabelKey: "type"
                onChipActivated: index => {
                    var person = DetailsCtl.crew[index]
                    Actions.browsePerson(person.id ? person.id : "", person.name)
                }
                onActiveFocusChanged: { if (crewRow.activeFocus) page.ensureVisible(crewRow) }
                property Item navUp: page.sectionAbove(3)
            }

            // ── Studios ────────────────────────────────────────────────────
            ChipStrip {
                id: studioRow
                title: qsTr("Studios")
                chipModel: DetailsCtl.studios
                onChipActivated: index => {
                    var studio = DetailsCtl.studios[index]
                    Actions.browseStudio(studio.id ? studio.id : "", studio.name)
                }
                onActiveFocusChanged: { if (studioRow.activeFocus) page.ensureVisible(studioRow) }
                property Item navUp: page.sectionAbove(4)
            }

            // ── Off-site links ─────────────────────────────────────────────
            // Names and URLs exactly as the server built them.
            ChipStrip {
                id: linkRow
                title: qsTr("Links")
                chipModel: DetailsCtl.externalLinks
                linkKey: "url"
                // Not arrow-right: these leave the application entirely, and an
                // arrow that means "forward" everywhere else would say the wrong
                // thing here.
                iconName: "external-link"
                onChipActivated: index => {
                    var link = DetailsCtl.externalLinks[index]
                    if (link && link.url)
                        Qt.openUrlExternally(link.url)
                }
                onActiveFocusChanged: { if (linkRow.activeFocus) page.ensureVisible(linkRow) }
                property Item navUp: page.sectionAbove(5)
            }

            // ── "More like this" (Emby-web parity) ─────────────────────────
            // The rail supplies its own page margins, so it spans full width.
            StrmRail {
                id: similarRail
                navigationFocusKey: "details-similar"
                navigationFocusFallbackItem: page.heroButton
                navigationFocusRefillActive: DetailsCtl.itemId === page.itemId
                                             && DetailsCtl.similarLoading
                visible: DetailsCtl.similar.count > 0
                title: qsTr("More like this")
                railModel: DetailsCtl.similar
                cardVariant: "poster"
                emptyText: qsTr("Nothing similar found")

                onActiveFocusChanged: {
                    if (similarRail.activeFocus)
                        page.ensureVisible(similarRail)
                }
                property Item navUp: page.sectionAbove(6)

                onItemActivated: index => Actions.openDetails(DetailsCtl.similar.get(index))
                onItemPlayRequested: index => Actions.play(DetailsCtl.similar.get(index))
                onItemPlayedToggled: index => Actions.togglePlayed(DetailsCtl.similar.get(index))
                onItemFavoriteToggled: index => Actions.toggleFavorite(DetailsCtl.similar.get(index))
                onMenuRequested: (index, mx, my) => {
                    page.menuOrigin = similarRail
                    itemMenu.popupForItem(DetailsCtl.similar.get(index), mx, my)
                }
            }

            Item { width: 1; height: Math.max(0, Theme.pageMarginValue - content.spacing) }
        }
    }

    // Swallows pointer input aimed at the not-yet-drawn body underneath.
    MouseArea {
        anchors.fill: parent
        visible: page.detailsLoading
        acceptedButtons: Qt.AllButtons
    }

    // A half-populated details page reads as a broken one, so nothing shows
    // until the fetch settles.
    LoadingState {
        anchors.fill: parent
        shape: "details"
        active: page.detailsLoading
    }

    // ── Context menu ───────────────────────────────────────────────────────
    // One menu, two callers: the page's own item (⋯ / right-click on the page)
    // and any card in the similar rail. The list itself is ItemMenu's, shared
    // with Home, Library, Search and Series; only "Details" differs, because
    // for the page's own item it would navigate to where the user already is.
    //
    // "Add to playlist" needs a surface to open, and the page supplies it: the
    // shared PlaylistPicker below.
    ItemMenu {
        id: itemMenu

        allowAddToPlaylist: true
        onAddToPlaylistRequested: item => page.addToPlaylist(item, page.menuOrigin)
    }

    // The trailer chooser, only ever opened for items with more than one.
    StrmMenu {
        id: trailerMenu

        onTriggered: index => page.openTrailer(index)
    }

    // ── Add to playlist ────────────────────────────────────────────────────
    // The picker is raised from three places — the hero's own verb, the ⋯
    // menu, and the menu of a card in either shelf — and gives the keyboard
    // back to whichever of them it was. It used to hand it to the hero's
    // button unconditionally, so filing a film from "More like this" threw the
    // page back to the top. `menuOrigin` is what raised the menu; `pickerOrigin`
    // is what raised the picker, kept apart because a menu can be opened and
    // dismissed without the picker ever appearing.
    property Item menuOrigin: null
    property Item pickerOrigin: null

    function addToPlaylist(item, origin): void {
        if (!item || !item.itemId)
            return
        page.pickerOrigin = origin
        playlistPicker.show(item.name ? String(item.name) : "", [String(item.itemId)])
    }

    function restorePickerFocus(): void {
        const origin = page.pickerOrigin
        page.pickerOrigin = null
        // An origin that has since gone (a shelf that emptied, a right-click on
        // the page itself) falls back to the hero rather than to nothing.
        const target = (origin && origin.visible && origin.enabled) ? origin
                     : playlistButton.visible ? playlistButton : page.heroButton
        target.forceActiveFocus(Qt.OtherFocusReason)
    }

    PlaylistPicker {
        id: playlistPicker

        z: 800
        onDismissed: page.restorePickerFocus()
    }

    // PlaylistCtl's results are global signals, so they are only claimed while
    // this page has a write of its own outstanding — otherwise a playlist edit
    // made elsewhere would toast here too, and so would every other live page
    // with a picker. They go to the window's toast host rather than one of the
    // page's own: a covered page is not drawn, so a reply that landed after the
    // user had moved on was announced to nobody.
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

    // By name: the window is Main.qml's, and a page staged without it (a test,
    // the page self-test) has nowhere to say it and says nothing.
    function notify(message, severity): void {
        const win = page.toastWindow
        if (win && typeof win["notify"] === "function")
            win["notify"](message, severity)
    }

    // Untyped on purpose: `notify` is Main.qml's, not ApplicationWindow's.
    readonly property var toastWindow: page.ApplicationWindow.window
}
