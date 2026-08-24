pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import StrmQt

// Series drill-down (ARCHITECTURE.md): hero backdrop, season tabs that switch on
// focus, episode cards with 16:9 stills, and the next unwatched episode marked
// wherever it is.
//
// What this page is for. Almost every visit answers one question — "what do I
// watch next" — so the answer is stated three times, in three registers: the
// primary verb names the episode ("Play next  ·  S2E5"), the gear line under
// the verbs spells it out, and the card itself sits on a lit plate with a
// NEXT UP badge so it is findable in place rather than only as a button. The
// grid also *starts* on it: when the loaded season holds the next unwatched
// episode, that is where the keyboard cursor and the viewport begin.
//
// Layout. Three anchored bands — hero, season bar, episode grid — rather than
// one long Flickable. The episode grid is the only scroller, which is what lets
// it stay a GridView: delegate recycling, real Left/Right/Up/Down, Page Up/Down
// and Home/End all come for free, and none of them survive being poured into a
// Column inside a page Flickable. The hero would then permanently own the top
// third of the window, so it **collapses**: scroll into a long season and the
// poster, synopsis and up-next line fold away, leaving the title and the verbs.
// Scroll back to the top and it returns. Hysteresis (collapse past 60, expand
// under 12) keeps the two states from fighting over one pixel of contentY.
//
// Season tabs. StrmTabBar already draws the distinction this page needs and it
// is the reason it is used here rather than a row of chips: moving the
// selection with Left/Right IS the switch (no Return required), while hover
// only *previews* — the label brightens, the underline stays put, and nothing
// is fetched. A tab bar that loaded a season on mouse-over would make reaching
// for season 9 load seasons 2 through 8 on the way.
//
// Kept from the previous page, deliberately: the isAutoRepeat guard on every
// activation path, keyNavigationWraps: false, Up off the top row landing on the
// season bar (by hand, so the grid keeps Up for itself), opacity rather than
// visibility while a season loads (an invisible view loses active focus and
// never gets it back), and the click-that-also-moves-the-keyboard-cursor rule.
//
// The series' own metadata comes from SeriesCtl.series, fetched for this series
// rather than looked up in a model — the page is commonly reached from an
// episode's "go to series", where the series was never in any model at all.
// Actions.itemFor() remains as the immediate fallback while that request is in
// flight. Every part of the hero that depends on a field is omitted whole when
// the field is absent, rather than drawn as a label with a blank beside it.
FocusScope {
    id: page

    // ── Season tabs ────────────────────────────────────────────────────────
    // Rebuilt rather than bound: MediaItemModel is a list model and StrmTabBar
    // takes a plain array of { text, badge }.
    property var seasonTabs: []
    // Parallel to seasonTabs. The season poster is the hero's fallback art when
    // the series item itself is not reachable, and it is honest: it is the
    // artwork for the season being shown.
    property var seasonPosters: []

    readonly property bool hasSeasons: SeriesCtl.seasons.count > 0
    readonly property bool hasEpisodes: SeriesCtl.episodes.count > 0

    // ── The series item ────────────────────────────────────────────────────
    // SeriesController carries no series-level metadata, so the backdrop,
    // overview, year range, rating and certificate come from the item map
    // Actions already holds — the one the user clicked to get here. Missing
    // (arriving from an episode's "Go to series", where the series was never in
    // a model) leaves a map with just enough in it for the context menu, and
    // every hero field that needs more simply does not appear.
    property var seriesItem: ({})

    // Cached projections of Actions' state: QML cannot bind to a Q_INVOKABLE,
    // so the values are re-read whenever Actions says they changed. Never
    // assigned from a click handler — the verb owns the value.
    property bool seriesPlayed: false
    property bool seriesFavorite: false

    readonly property var nextItem: SeriesCtl.hasNextUnwatched ? SeriesCtl.nextUnwatched : ({})
    readonly property string nextId: (page.nextItem && page.nextItem.itemId)
                                     ? String(page.nextItem.itemId) : ""

    // ── Hero collapse ──────────────────────────────────────────────────────
    property bool heroCollapsed: false
    // Deliberately measured against the PAGE height, not the grid's: the grid's
    // height depends on the hero, and asking "is there enough to scroll" in
    // terms of a quantity the answer changes would be a loop.
    readonly property bool canCollapse: episodeGrid.contentHeight > page.height

    // ── Card metrics ───────────────────────────────────────────────────────
    // One hidden card is the source of truth for how big a card is, so the grid
    // can never disagree with the card about its own geometry.
    StrmCard {
        id: metrics
        visible: false
        enabled: false
        variant: "still"
    }

    readonly property int cardWidth: metrics.implicitWidth
    readonly property int cardHeight: metrics.implicitHeight
    readonly property int columns: Math.max(1, Math.floor(episodeGrid.width
                                                          / (page.cardWidth + Theme.spacingValue)))

    // ── Formatting ─────────────────────────────────────────────────────────
    function formatRuntime(ms) {
        if (!ms || ms <= 0)
            return ""
        const minutes = Math.round(ms / 60000)
        return minutes >= 60 ? Math.floor(minutes / 60) + " h " + (minutes % 60) + " min"
                             : minutes + " min"
    }

    function episodeCode(item) {
        if (!item)
            return ""
        const season = item.parentIndexNumber
        const episode = item.indexNumber
        if (season === undefined || episode === undefined || season < 0 || episode < 0)
            return ""
        return "S" + season + "E" + episode
    }

    readonly property string nextCode: page.episodeCode(page.nextItem)

    // "S2E5 · The Fly · 47 min" — the gear line under the verbs.
    readonly property string nextLine: {
        if (!SeriesCtl.hasNextUnwatched)
            return ""
        const parts = []
        if (page.nextCode.length > 0)
            parts.push(page.nextCode)
        if (page.nextItem.name)
            parts.push(String(page.nextItem.name))
        const runtime = page.formatRuntime(page.nextItem.runtimeMs)
        if (runtime.length > 0)
            parts.push(runtime)
        return parts.join("  ·  ")
    }

    // Episode number and title. The number leads because within a season it is
    // the thing you scan for.
    function episodeLabel(item) {
        const number = (item.indexNumber !== undefined && item.indexNumber >= 0)
                     ? item.indexNumber + ". " : ""
        return number + (item.name ? String(item.name) : "")
    }

    // Runtime, or what is left of it. A half-watched episode is asked about in
    // terms of the remainder, not the total.
    function episodeSublabel(item) {
        if (item.resumable === true && item.runtimeMs > 0 && item.positionMs > 0) {
            const left = page.formatRuntime(item.runtimeMs - item.positionMs)
            if (left.length > 0)
                return qsTr("%1 left").arg(left)
        }
        return item.subtitle ? String(item.subtitle) : page.formatRuntime(item.runtimeMs)
    }

    // ── Hero metadata, all of it optional ──────────────────────────────────
    // Year range and status in one string: MediaItemModel's `subtitle` role
    // already renders a series as "2013 – Present" or "2013", which is the only
    // form the status ("Continuing"/"Ended") reaches QML in.
    readonly property string metaText: {
        const parts = []
        if (page.seriesItem.subtitle)
            parts.push(String(page.seriesItem.subtitle))
        if (page.seriesItem.officialRating)
            parts.push(String(page.seriesItem.officialRating))
        if (SeriesCtl.seasons.count > 0)
            parts.push(qsTr("%n season(s)", "", SeriesCtl.seasons.count))
        return parts.join("  ·  ")
    }

    // ── Building the season bar ────────────────────────────────────────────
    // Which series the tabs on screen belong to. The seasons model's `count`
    // says how many seasons there are, never *whose*, so it cannot tell a
    // different series from a refresh of the one already open — and only the
    // first of those may move the user's tab. Cleared back to "" whenever there
    // are no tabs, so re-opening the same series (which empties the model
    // first) still counts as an arrival and still lands on its default season.
    property string tabbedSeriesId: ""

    function rebuildSeasonTabs() {
        const tabs = []
        const posters = []
        for (let i = 0; i < SeriesCtl.seasons.count; ++i) {
            const season = SeriesCtl.seasons.get(i)
            tabs.push({ text: season.name, badge: season.unplayedCount })
            posters.push(season.posterUrl ? String(season.posterUrl) : "")
        }
        page.seasonTabs = tabs
        page.seasonPosters = posters

        // Opening a different series is the one thing that earns the cursor:
        // seasons landing again for the series already on screen is a refresh,
        // and snapping the selection back to SeriesCtl.currentSeason there
        // would drop the user out of the season they walked over to read.
        //
        // Today this guard cannot fire: SeriesController::open() clears the
        // model before it fills it, so every arrival passes through an empty
        // tab list and `arrived` is always "" first. It is kept because it is
        // the thing that makes an in-place refresh — seasons re-fetched without
        // a clear, which is what a live update would do — safe to add later.
        const arrived = tabs.length > 0 ? SeriesCtl.seriesId : ""
        if (page.tabbedSeriesId === arrived)
            return
        page.tabbedSeriesId = arrived
        seasonBar.currentIndex = Math.max(0, SeriesCtl.currentSeason)
    }

    // The unplayed badge is the one thing on a tab that changes while the page
    // is open, and moving a number is no reason to rebuild the bar around it —
    // `tabs` is a plain JS value, so StrmTabBar re-reads it on assignment while
    // `currentIndex` is a separate property that survives untouched.
    //
    // Only the loaded season can be recounted, and it is recounted from the
    // episodes on screen rather than adjusted by a delta: ItemActions patches
    // SeriesCtl.episodes before it announces the change, so the badge and the
    // ticks the user just clicked are derived from the same rows and cannot
    // disagree — and a repeated or rolled-back announcement, which
    // ItemActions::applyPlayed() does emit, costs nothing instead of drifting
    // the count by one each time. Seasons that were never opened keep the
    // number the server sent; nothing on this page knows better about those.
    function refreshSeasonBadges(itemId) {
        const index = SeriesCtl.currentSeason
        if (index < 0 || index >= page.seasonTabs.length)
            return
        let unplayed = 0
        let mine = false
        for (let i = 0; i < SeriesCtl.episodes.count; ++i) {
            const episode = SeriesCtl.episodes.get(i)
            if (String(episode.itemId) === itemId)
                mine = true
            if (episode.played !== true)
                ++unplayed
        }
        // An id from somewhere else in the app — another page's card, or this
        // series' own "Mark watched" — says nothing about this season's count.
        if (!mine || page.seasonTabs[index].badge === unplayed)
            return
        const tabs = page.seasonTabs.slice()
        tabs[index] = { text: tabs[index].text, badge: unplayed }
        page.seasonTabs = tabs
    }

    readonly property string seasonPoster: {
        const index = SeriesCtl.currentSeason
        if (index < 0 || index >= page.seasonPosters.length)
            return ""
        return page.seasonPosters[index]
    }

    readonly property string heroPoster: page.seriesItem.posterUrl
                                         ? String(page.seriesItem.posterUrl)
                                         : page.seasonPoster

    function syncSeriesItem() {
        // SeriesCtl.series is fetched for this series specifically, so it is the
        // only source that works when the page was reached from an episode's
        // "go to series" — the series was never in any model then, and a model
        // lookup silently returned nothing.
        const fetched = SeriesCtl.series
        if (fetched && fetched.itemId) {
            page.seriesItem = fetched
            page.syncUserState()
            return
        }
        // Arrives a moment later than the page; until then, whatever model the
        // user came through still has the record.
        const known = Actions.itemFor(SeriesCtl.seriesId)
        if (known && known.itemId) {
            page.seriesItem = known
        } else {
            // Enough for the context menu and the verbs, and nothing invented:
            // the id and the kind are facts, everything else is absent.
            page.seriesItem = ({
                "itemId": SeriesCtl.seriesId,
                "name": SeriesCtl.seriesName,
                "type": "Series"
            })
        }
        page.syncUserState()
    }

    function syncUserState() {
        const id = SeriesCtl.seriesId
        page.seriesPlayed = id.length > 0 && Actions.isPlayed(id)
        page.seriesFavorite = id.length > 0 && Actions.isFavorite(id)
    }

    function openSeriesMenu(item, px, py) {
        if (!SeriesCtl.seriesId)
            return
        const p = item.mapToItem(null, px, py)
        itemMenu.popupForItem(page.seriesItem, p.x, p.y)
    }

    // ── Episode verbs ──────────────────────────────────────────────────────
    function playRow(row) {
        if (row < 0 || row >= SeriesCtl.episodes.count)
            return
        episodeGrid.currentIndex = row
        Actions.play(SeriesCtl.episodes.get(row))
    }

    // Where the keyboard starts after a season loads: on the next unwatched
    // episode when this season holds it, otherwise at the top.
    function focusInitialEpisode() {
        // A Back/Forward locator is a stronger statement than the page's
        // next-unwatched default. The controller may report completion before
        // its model-reset notification is delivered, so remember that a
        // locator claimed this visit even after its exact row has landed.
        if (episodeGrid.navigationFocusClaimed) {
            // Suppress exactly this automatic placement. A later deliberate
            // season choice must regain the ordinary first/next focus policy.
            episodeGrid.navigationFocusClaimed = false
            return
        }
        let target = 0
        if (page.nextId.length > 0) {
            for (let i = 0; i < SeriesCtl.episodes.count; ++i) {
                if (String(SeriesCtl.episodes.get(i).itemId) === page.nextId) {
                    target = i
                    break
                }
            }
        }
        episodeGrid.currentIndex = SeriesCtl.episodes.count > 0 ? target : -1
        if (target > 0)
            episodeGrid.positionViewAtIndex(target, GridView.Contain)
        else
            episodeGrid.positionViewAtBeginning()
    }

    function selectSeason(row) {
        // An immediate restore may not have a loading transition on which to
        // consume the claim. A user season choice is a newer instruction and
        // must never inherit that stale suppression.
        episodeGrid.navigationFocusClaimed = false
        SeriesCtl.selectSeason(row)
    }

    // ── Vertical navigation ────────────────────────────────────────────────
    // A series with no seasons has no tab bar and a series with no episodes has
    // no grid, so Down and Up walk this list and skip whatever is absent rather
    // than each band naming its neighbours (the pattern DetailsPage sets).
    // The season BAR, not its Flickable: a Flickable takes focus and would then
    // sit there holding it with no way to move the selection. An Item's
    // `visible` is already the AND of its own and its parents', so a bar inside
    // a hidden scroller reports itself absent without being asked twice.
    readonly property var navSections: [seasonBar, episodeGrid]

    function sectionBelow(start) {
        for (let i = start; i < page.navSections.length; ++i) {
            if (page.navSections[i].visible)
                return page.navSections[i]
        }
        return null
    }

    function sectionAbove(start) {
        for (let i = start; i >= 0; --i) {
            if (page.navSections[i].visible)
                return page.navSections[i]
        }
        return page.heroButton
    }

    // ── Horizontal navigation across the verbs ─────────────────────────────
    readonly property var heroVerbs: [nextButton, playAllButton, shuffleButton,
                                      watchedButton, favoriteButton, moreButton]

    function verbAfter(index) {
        for (let i = index + 1; i < page.heroVerbs.length; ++i) {
            if (page.heroVerbs[i].visible)
                return page.heroVerbs[i]
        }
        return null
    }

    function verbBefore(index) {
        for (let i = index - 1; i >= 0; --i) {
            if (page.heroVerbs[i].visible)
                return page.heroVerbs[i]
        }
        return null
    }

    // The first verb this series actually shows: where Up out of the season bar
    // lands, and what takes focus when the page opens.
    readonly property Item heroButton: {
        for (let i = 0; i < page.heroVerbs.length; ++i) {
            if (page.heroVerbs[i].visible)
                return page.heroVerbs[i]
        }
        return moreButton
    }

    Component.onCompleted: {
        page.rebuildSeasonTabs()
        page.syncSeriesItem()
    }

    // The season LIST changing — emptied by open(), filled when the seasons
    // land. Rebuilding the tabs here is right; moving the selection is not,
    // which is why rebuildSeasonTabs() decides that for itself off the series
    // id rather than off the fact that a count moved. Badges do not come
    // through here at all: they move without the count moving, and
    // refreshSeasonBadges() carries them.
    Connections {
        target: SeriesCtl.seasons
        function onCountChanged() { page.rebuildSeasonTabs() }
    }

    Connections {
        target: SeriesCtl

        function onSeriesChanged() { page.syncSeriesItem() }
        // The series record lands after the page does; re-read when it arrives.
        function onSeriesMetadataChanged() { page.syncSeriesItem() }

        function onCurrentSeasonChanged() {
            seasonBar.currentIndex = SeriesCtl.currentSeason
            seasonScroll.ensureCurrentVisible()
        }

        // callLater, not straight through: the view has the new model but has
        // not laid it out yet, so positionViewAtIndex() would map the target
        // through the previous season's geometry.
        function onLoadingChanged() {
            if (!SeriesCtl.loading)
                Qt.callLater(page.focusInitialEpisode)
        }
    }

    Connections {
        target: Actions

        function onPlayedChanged(itemId, played) {
            if (itemId === SeriesCtl.seriesId)
                page.seriesPlayed = played
            // The season tabs carry an unplayed count and nothing refetches the
            // seasons while the page is open — SeriesCtl.seasons is not one of
            // the models ItemActions patches, so it is never told either.
            // Without this the tab the user is standing on goes on claiming the
            // episode they just ticked is unwatched.
            page.refreshSeasonBadges(itemId)
            // The whole-series episode list SeriesController derives
            // "next unwatched" from is not a model registered with Actions, so
            // it has to be told. Application.cpp makes the same connection in
            // C++; notePlayed() is idempotent, and a page that depends on the
            // value staying true is the right place to say so out loud.
            SeriesCtl.notePlayed(itemId, played)
        }

        function onFavoriteChanged(itemId, favorite) {
            if (itemId === SeriesCtl.seriesId)
                page.seriesFavorite = favorite
        }
    }

    // ── Backdrop ───────────────────────────────────────────────────────────
    // Anchored to the page rather than to the hero: the artwork is the room the
    // page sits in, so it stays put while the hero folds and the grid scrolls.
    Image {
        id: backdrop

        anchors.fill: parent
        source: page.seriesItem.backdropUrl ? page.seriesItem.backdropUrl : ""
        sourceSize.width: 1280
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        opacity: backdrop.status === Image.Ready ? 0.35 : 0.0

        Behavior on opacity {
            NumberAnimation { duration: Theme.animSlow; easing.type: Theme.easeStandard }
        }
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 0.45; color: Theme.veil }
            GradientStop { position: 0.85; color: Theme.ground }
        }
    }

    // Right-click on the page background opens the series' own verbs. Cards
    // accept the right button themselves, so a click over one lands there.
    TapHandler {
        acceptedButtons: Qt.RightButton
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: eventPoint => page.openSeriesMenu(page, eventPoint.position.x,
                                                    eventPoint.position.y)
    }

    // ── Hero ───────────────────────────────────────────────────────────────
    Item {
        id: heroBox

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: Theme.spacingValue
        anchors.leftMargin: Theme.pageMarginValue
        anchors.rightMargin: Theme.pageMarginValue
        height: heroRow.implicitHeight

        Behavior on height {
            NumberAnimation { duration: Theme.animNormalMs; easing.type: Theme.easeStandard }
        }

        Row {
            id: heroRow

            width: parent.width
            spacing: Theme.spacingLoose

            readonly property int posterW: Theme.scale(148)
            readonly property int posterH: Math.round(heroRow.posterW * 3 / 2)

            Rectangle {
                id: posterFrame

                width: heroRow.posterW
                height: heroRow.posterH
                visible: !page.heroCollapsed && page.heroPoster.length > 0
                radius: Theme.radiusCardValue
                color: Theme.surfaceColor
                clip: true

                Image {
                    id: poster
                    anchors.fill: parent
                    source: page.heroPoster
                    sourceSize.width: heroRow.posterW
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    opacity: poster.status === Image.Ready ? 1.0 : 0.0

                    Behavior on opacity {
                        NumberAnimation {
                            duration: Theme.animNormalMs
                            easing.type: Theme.easeStandard
                        }
                    }
                }
            }

            Column {
                id: heroColumn

                width: heroRow.width - (posterFrame.visible
                                        ? posterFrame.width + heroRow.spacing : 0)
                spacing: Theme.spacingTight

                // Prose gets a measure. On a wide window a synopsis would
                // otherwise run 150 characters to the line.
                readonly property int proseWidth: Math.min(heroColumn.width, Theme.scale(820))

                Text {
                    width: parent.width
                    text: SeriesCtl.seriesName
                    color: Theme.textPrimaryColor
                    font.family: Theme.fontDisplay
                    // The title is the first thing to give ground when the hero
                    // folds: it is still the largest thing on screen either way.
                    font.pixelSize: page.heroCollapsed ? Theme.fontTitle
                                                       : Theme.fontDisplaySize
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    maximumLineCount: 1
                    // No Behavior on the size: heroBox's height is BOUND to this
                    // column's implicit height, so animating the type would feed
                    // a new target into that height animation on every frame and
                    // the band would chase itself. One animation owns the fold —
                    // the band's — and everything inside it steps.
                }

                Row {
                    width: parent.width
                    visible: !page.heroCollapsed
                             && (page.metaText.length > 0
                                 || page.seriesItem.communityRating > 0)
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
                        visible: page.seriesItem.communityRating > 0
                        spacing: Theme.scale(6)

                        StrmIcon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "star"
                            size: Theme.fontBodySize
                            color: Theme.accentColor
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: Number(page.seriesItem.communityRating).toFixed(1)
                            color: Theme.accentColor
                            font.family: Theme.fontMono
                            font.pixelSize: Theme.fontBodySize
                        }
                    }
                }

                Text {
                    width: heroColumn.proseWidth
                    visible: !page.heroCollapsed && text.length > 0
                    text: page.seriesItem.overview ? String(page.seriesItem.overview) : ""
                    color: Theme.textPrimaryColor
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.Wrap
                    lineHeight: Theme.lineNormal
                    maximumLineCount: 3
                    elide: Text.ElideRight
                }

                Item { width: 1; height: Theme.scale(4) }

                // ── Verbs ──────────────────────────────────────────────────
                // Every one is reachable by click and by keyboard, and the
                // Left/Right chain skips whatever this series does not show.
                Row {
                    id: verbRow
                    spacing: Theme.spacingTight

                    // Whole-series, not this season's: SeriesController fetches
                    // every episode once per open() precisely so this button can
                    // mean what it says, and it disappears only when the entire
                    // series is finished. It names the episode, because "next"
                    // on its own is a promise the page can afford to keep.
                    StrmButton {
                        id: nextButton
                        visible: SeriesCtl.hasNextUnwatched
                        text: page.nextCode.length > 0
                              ? qsTr("Play next  ·  %1").arg(page.nextCode)
                              : qsTr("Play next")
                        iconName: "play"
                        variant: "primary"
                        onClicked: Actions.play(SeriesCtl.nextUnwatched)
                        KeyNavigation.right: page.verbAfter(0)
                        KeyNavigation.down: page.sectionBelow(0)
                    }

                    StrmButton {
                        id: playAllButton
                        visible: page.hasEpisodes
                        text: qsTr("Play all")
                        iconName: "playlist"
                        onClicked: Actions.playAll(SeriesCtl.seriesId, "tvshows")
                        KeyNavigation.left: page.verbBefore(1)
                        KeyNavigation.right: page.verbAfter(1)
                        KeyNavigation.down: page.sectionBelow(0)
                    }

                    // shuffleSeries() spans every season rather than just the
                    // one on screen.
                    StrmButton {
                        id: shuffleButton
                        visible: page.hasEpisodes
                        text: qsTr("Shuffle")
                        iconName: "shuffle"
                        onClicked: Actions.shuffleSeries(SeriesCtl.seriesId)
                        KeyNavigation.left: page.verbBefore(2)
                        KeyNavigation.right: page.verbAfter(2)
                        KeyNavigation.down: page.sectionBelow(0)
                    }

                    StrmButton {
                        id: watchedButton
                        text: page.seriesPlayed ? qsTr("Watched") : qsTr("Mark watched")
                        iconName: page.seriesPlayed ? "check" : "eye"
                        // The verb, not a local flip: Actions owns the value and
                        // tells every view about the change.
                        onClicked: Actions.setPlayed(SeriesCtl.seriesId, !page.seriesPlayed)
                        KeyNavigation.left: page.verbBefore(3)
                        KeyNavigation.right: page.verbAfter(3)
                        KeyNavigation.down: page.sectionBelow(0)
                    }

                    StrmButton {
                        id: favoriteButton
                        text: page.seriesFavorite ? qsTr("Favourite") : qsTr("Add favourite")
                        iconName: page.seriesFavorite ? "heart-filled" : "heart"
                        onClicked: Actions.setFavorite(SeriesCtl.seriesId, !page.seriesFavorite)
                        KeyNavigation.left: page.verbBefore(4)
                        KeyNavigation.right: page.verbAfter(4)
                        KeyNavigation.down: page.sectionBelow(0)
                    }

                    StrmIconButton {
                        id: moreButton
                        iconName: "more-horizontal"
                        tooltip: qsTr("More actions")
                        onClicked: page.openSeriesMenu(moreButton, 0, moreButton.height)
                        KeyNavigation.left: page.verbBefore(5)
                        KeyNavigation.down: page.sectionBelow(0)
                    }
                }

                // The answer spelled out, in the booth's gear-label register:
                // the button says which episode, this says what it is.
                Row {
                    width: parent.width
                    visible: !page.heroCollapsed && page.nextLine.length > 0
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
                        width: heroColumn.proseWidth - Theme.scale(90)
                        text: page.nextLine
                        color: Theme.accentColor
                        font.family: Theme.fontBody
                        font.pixelSize: Theme.fontSmall
                        elide: Text.ElideRight
                        maximumLineCount: 1
                    }
                }
            }
        }
    }

    // ── Season bar ─────────────────────────────────────────────────────────
    // Flickable so a 20-season show is still reachable with a mouse; the bar
    // itself is one tab stop and owns Left/Right.
    Flickable {
        id: seasonScroll

        anchors.top: heroBox.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: Theme.spacingValue
        anchors.leftMargin: Theme.pageMarginValue
        anchors.rightMargin: Theme.pageMarginValue
        height: seasonBar.implicitHeight
        visible: page.seasonTabs.length > 0
        clip: true

        contentWidth: seasonBar.implicitWidth
        contentHeight: seasonScroll.height
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.horizontal: StrmScrollBar {}

        function ensureCurrentVisible() {
            const left = seasonBar.indicatorX
            const right = left + seasonBar.indicatorWidth
            const maxX = Math.max(0, seasonScroll.contentWidth - seasonScroll.width)
            if (left < seasonScroll.contentX)
                seasonScroll.contentX = Math.max(0, left - Theme.spacingValue)
            else if (right > seasonScroll.contentX + seasonScroll.width)
                seasonScroll.contentX = Math.min(maxX,
                                                 right - seasonScroll.width + Theme.spacingValue)
        }

        StrmTabBar {
            id: seasonBar

            width: seasonBar.implicitWidth
            height: seasonScroll.height
            tabs: page.seasonTabs
            // Selection IS the switch: no Return required, and hover only
            // previews (StrmTabBar draws that line, it is not redrawn here).
            onTabSelected: index => page.selectSeason(index)

            KeyNavigation.up: page.heroButton
            KeyNavigation.down: page.sectionBelow(1)

            // Kept as a harmless confirm so muscle memory from the old page
            // still works; selectSeason() on the loaded season just reloads it.
            Keys.onReturnPressed: event => {
                if (!event.isAutoRepeat)
                    page.selectSeason(seasonBar.currentIndex)
            }
            Keys.onEnterPressed: event => {
                if (!event.isAutoRepeat)
                    page.selectSeason(seasonBar.currentIndex)
            }
        }
    }

    // ── Episodes ───────────────────────────────────────────────────────────
    GridView {
        id: episodeGrid

        readonly property string navigationFocusKind: "grid"
        readonly property string navigationFocusKey: "series-episodes"
        readonly property bool navigationFocusRestorePending: episodeNavigationFocus.pending
        property bool _navigationFocusWriting: false
        property bool navigationFocusClaimed: false

        function navigationFocusSnapshot(): var { return episodeNavigationFocus.snapshot() }
        function restoreNavigationFocus(identity, index): bool {
            episodeGrid.navigationFocusClaimed = true
            return episodeNavigationFocus.restore(identity, index)
        }
        function cancelNavigationFocusRestore(): void { episodeNavigationFocus.cancel() }
        function cancelNavigationFocusForUser(): void {
            if (!episodeGrid._navigationFocusWriting)
                episodeNavigationFocus.cancel()
        }
        function applyNavigationFocus(index): void {
            episodeGrid._navigationFocusWriting = true
            if (index >= 0) {
                episodeGrid.currentIndex = index
                episodeGrid.positionViewAtIndex(index, GridView.Contain)
            }
            episodeGrid.forceActiveFocus(Qt.OtherFocusReason)
            episodeGrid._navigationFocusWriting = false
        }
        function applyNavigationFallback(): void {
            if (page.heroButton && page.heroButton.visible && page.heroButton.enabled) {
                page.heroButton.forceActiveFocus(Qt.OtherFocusReason)
                return
            }
            let candidate = episodeGrid
            for (let step = 0; step < 256; ++step) {
                candidate = candidate.nextItemInFocusChain(true)
                if (!candidate || candidate === episodeGrid)
                    return
                let cursor = candidate
                while (cursor && cursor !== episodeGrid)
                    cursor = cursor.parent
                if (cursor !== episodeGrid) {
                    candidate.forceActiveFocus(Qt.OtherFocusReason)
                    return
                }
            }
        }

        NavigationFocusRestorer {
            id: episodeNavigationFocus
            model: SeriesCtl.episodes
            count: episodeGrid.count
            currentIndex: episodeGrid.currentIndex
            refillActive: SeriesCtl.loading
            onFocusRequested: index => episodeGrid.applyNavigationFocus(index)
            onFallbackRequested: episodeGrid.applyNavigationFallback()
        }

        Connections {
            target: SeriesCtl.episodes
            function onModelReset() { Qt.callLater(episodeNavigationFocus.retry) }
        }

        Connections {
            target: episodeGrid
            function onActiveFocusChanged() {
                if (!episodeGrid.activeFocus)
                    episodeGrid.cancelNavigationFocusForUser()
            }
            function onCountChanged() { episodeNavigationFocus.retry() }
        }

        anchors.top: seasonScroll.visible ? seasonScroll.bottom : heroBox.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: Theme.spacingValue
        anchors.leftMargin: Theme.pageMarginValue
        anchors.rightMargin: Theme.pageMarginValue
        // The page's single `focus: true`, and deliberately so: Main.qml focuses
        // a page by focusing its root FocusScope, which delegates to whatever
        // one thing claimed it. Two claimants would make "where does the page
        // open" depend on binding evaluation order. It opens on the episodes —
        // and, because focusInitialEpisode() puts the cursor on the next
        // unwatched one, arriving and pressing Return plays the right episode.
        focus: true
        clip: true
        model: SeriesCtl.episodes

        // Opacity, not visibility: an invisible view loses active focus and does
        // not get it back, so hiding it while a season loads would drop the
        // keyboard out of the page on every season switch.
        opacity: SeriesCtl.loading ? 0.0 : 1.0

        Behavior on opacity {
            NumberAnimation { duration: Theme.animFastMs; easing.type: Theme.easeStandard }
        }

        // Never 0: the view is laid out once before it has a width, and a
        // zero-width cell is a warning and a divide-by-zero waiting to happen.
        cellWidth: Math.max(1, Math.floor(episodeGrid.width / page.columns))
        // Loose rather than tight: these cells carry the next-unwatched plate,
        // which sits outside the card's own bounds.
        cellHeight: page.cardHeight + Theme.spacingLoose
        cacheBuffer: episodeGrid.cellHeight * 3
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds

        keyNavigationWraps: false
        highlightMoveDuration: Theme.animFastMs
        highlightRangeMode: GridView.ApplyRange
        preferredHighlightBegin: 0
        preferredHighlightEnd: Math.max(0, episodeGrid.height - episodeGrid.cellHeight)

        ScrollBar.vertical: StrmScrollBar {}

        // Scrolling into a long season folds the hero away; coming back to the
        // top unfolds it. Two thresholds, not one, so a hero that is mid-fold
        // cannot land on the pixel that toggles it and oscillate.
        onContentYChanged: {
            if (!page.heroCollapsed && page.canCollapse
                    && episodeGrid.contentY > Theme.scale(60))
                page.heroCollapsed = true
            else if (page.heroCollapsed && episodeGrid.contentY < Theme.scale(12))
                page.heroCollapsed = false
        }

        function playCurrent() {
            episodeGrid.cancelNavigationFocusForUser()
            page.playRow(episodeGrid.currentIndex)
        }

        // Guard isAutoRepeat: a held Return must not launch playback twice.
        Keys.onReturnPressed: event => { if (!event.isAutoRepeat) episodeGrid.playCurrent() }
        Keys.onEnterPressed: event => { if (!event.isAutoRepeat) episodeGrid.playCurrent() }

        // Up off the TOP ROW reaches the season bar. Done by hand rather than
        // with KeyNavigation.up, which would take Up away from the view entirely
        // and break moving between rows of episodes.
        Keys.onUpPressed: event => {
            if (episodeGrid.currentIndex < page.columns) {
                const above = page.sectionAbove(0)
                if (above && above !== episodeGrid) {
                    above.forceActiveFocus(Qt.BacktabFocusReason)
                    event.accepted = true
                    return
                }
            }
            event.accepted = false
        }

        // A screen at a time, and the ends. A 60-episode season is not a place
        // to arrive one keypress at a time.
        function pageStep() {
            const rows = Math.max(1, Math.floor(episodeGrid.height
                                                / Math.max(1, episodeGrid.cellHeight)))
            return rows * Math.max(1, page.columns)
        }

        Keys.onPressed: event => {
            episodeGrid.cancelNavigationFocusForUser()
            if (episodeGrid.count === 0)
                return
            if (event.key === Qt.Key_PageDown) {
                episodeGrid.currentIndex = Math.min(episodeGrid.count - 1,
                                                    episodeGrid.currentIndex
                                                    + episodeGrid.pageStep())
                event.accepted = true
            } else if (event.key === Qt.Key_PageUp) {
                episodeGrid.currentIndex = Math.max(0, episodeGrid.currentIndex
                                                    - episodeGrid.pageStep())
                event.accepted = true
            } else if (event.key === Qt.Key_Home) {
                episodeGrid.currentIndex = 0
                event.accepted = true
            } else if (event.key === Qt.Key_End) {
                episodeGrid.currentIndex = episodeGrid.count - 1
                event.accepted = true
            }
        }

        delegate: FocusScope {
            id: cell

            required property int index
            required property var model

            width: episodeGrid.cellWidth
            height: episodeGrid.cellHeight

            onActiveFocusChanged: {
                if (cell.activeFocus) {
                    episodeGrid.cancelNavigationFocusForUser()
                    episodeGrid.currentIndex = cell.index
                }
            }

            readonly property bool current: cell.GridView.isCurrentItem && episodeGrid.activeFocus
            // The one card most visits to this page came for.
            readonly property bool isNext: page.nextId.length > 0
                                           && String(cell.model.itemId) === page.nextId

            // An episode's 16:9 still is `thumbUrl`; it is empty when the server
            // has none, and then the poster is the honest fallback rather than a
            // hole in the grid.
            readonly property string art: {
                const thumb = cell.model.thumbUrl !== undefined ? String(cell.model.thumbUrl) : ""
                if (thumb.length > 0)
                    return thumb
                return cell.model.posterUrl !== undefined ? String(cell.model.posterUrl) : ""
            }

            // The next-unwatched plate. Deliberately NOT another amber ring:
            // the crisp 3 px ring means "the keyboard is here" and nothing else
            // may borrow it. This is a warm plate the card sits on — the same
            // hue family, a different statement — and it tracks the card's own
            // hover/focus raise so the two never drift apart.
            Item {
                anchors.fill: card
                z: -1
                visible: cell.isNext
                transformOrigin: Item.Center
                scale: card.highlighted ? Theme.focusScale
                     : card.hovered ? Theme.hoverScale : 1.0

                Behavior on scale {
                    NumberAnimation {
                        duration: card.highlighted ? Theme.animFastMs : Theme.animInstant
                        easing.type: card.highlighted ? Theme.easeStandard : Theme.easeInstant
                    }
                }

                Rectangle {
                    x: -Theme.spacingTight
                    y: -Theme.spacingTight
                    width: card.width + 2 * Theme.spacingTight
                    height: card.height + 2 * Theme.spacingTight
                    radius: Theme.radiusPanel
                    color: Theme.accentMuted
                    opacity: 0.38
                    border.width: 1
                    border.color: Theme.accentMuted
                }
            }

            StrmCard {
                id: card

                anchors.centerIn: parent
                variant: "still"
                imageUrl: cell.art
                label: page.episodeLabel(cell.model)
                sublabel: page.episodeSublabel(cell.model)
                badgeText: cell.isNext ? qsTr("NEXT UP") : ""
                progress: cell.model.progress !== undefined ? cell.model.progress : 0
                played: cell.model.played === true
                favorite: cell.model.favorite === true
                highlighted: cell.current

                // A click is a commit, so it also moves the keyboard here.
                // Hover never does — that is StrmCard's own contract and it is
                // not overridden anywhere on this page.
                onActivated: {
                    episodeGrid.cancelNavigationFocusForUser()
                    episodeGrid.currentIndex = cell.index
                    episodeGrid.forceActiveFocus(Qt.MouseFocusReason)
                    page.playRow(cell.index)
                }
                onPlayRequested: {
                    episodeGrid.cancelNavigationFocusForUser()
                    page.playRow(cell.index)
                }
                onPlayedToggled: {
                    episodeGrid.cancelNavigationFocusForUser()
                    Actions.togglePlayed(SeriesCtl.episodes.get(cell.index))
                }
                onFavoriteToggled: {
                    episodeGrid.cancelNavigationFocusForUser()
                    Actions.toggleFavorite(SeriesCtl.episodes.get(cell.index))
                }
                onMenuRequested: (mx, my) => {
                    episodeGrid.cancelNavigationFocusForUser()
                    itemMenu.popupForItem(SeriesCtl.episodes.get(cell.index), mx, my)
                }
            }
        }
    }

    // Swallows clicks aimed at the previous season's cards underneath.
    MouseArea {
        anchors.fill: episodeGrid
        visible: SeriesCtl.loading
        acceptedButtons: Qt.AllButtons
    }

    LoadingState {
        anchors.fill: episodeGrid
        shape: "grid"
        active: SeriesCtl.loading
        margins: 0
    }

    EmptyState {
        anchors.horizontalCenter: episodeGrid.horizontalCenter
        anchors.verticalCenter: episodeGrid.verticalCenter
        visible: !SeriesCtl.loading && episodeGrid.count === 0
        iconName: "list"
        headline: qsTr("No episodes here")
        body: page.hasSeasons
              ? qsTr("This season has no episodes on the server.")
              : qsTr("This series has no seasons on the server.")
        // Only offered when there is a season to reload: selectSeason(-1) is a
        // no-op in the controller, and a button that does nothing is worse than
        // no button.
        actionText: page.hasSeasons ? qsTr("Reload") : ""
        onActionTriggered: page.selectSeason(SeriesCtl.currentSeason)
    }

    // ── Context menu ───────────────────────────────────────────────────────
    // The card's ⋯, its right-click and the page background all open the shared
    // list — the same one Home, Library, Search and Details use.
    ItemMenu { id: itemMenu }
}
