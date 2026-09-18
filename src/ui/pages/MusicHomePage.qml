pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import StrmQt

// MusicHomePage — a music library's landing page (Crate spec §4).
//
// A hero ("Pick up where you left off", or "Pull one out" with no history) and
// seven shelves. Every shelf is an independent lane from MusicHomeCtl: it loads,
// fails and hides on its own, and this page only lays them out. Nothing here
// shapes data; the controller hands over rows and ready-made strings.
//
// Navigation: this page pushes nothing. Choosing a section or a genre raises a
// signal and Main.qml owns the route (ARCHITECTURE.md: Main alone navigates).
FocusScope {
    id: page

    property string libraryId: ""
    property string libraryName: ""

    signal sectionRequested(string key)
    signal genreRequested(string genreId, string genreName)

    readonly property var hero: MusicHomeCtl.hero
    readonly property string heroMode: page.hero && page.hero.mode ? String(page.hero.mode) : ""
    readonly property bool hasHero: page.heroMode.length > 0
    readonly property bool resumeMode: page.heroMode === "resume"
    readonly property bool heroLoading: MusicHomeCtl.heroLane.loading === true && !page.hasHero
    readonly property string heroError: !page.hasHero && MusicHomeCtl.heroLane.error
                                        ? String(MusicHomeCtl.heroLane.error) : ""

    readonly property int heroSleeveSize: Theme.scale(210)
    readonly property int sleeveSize: Theme.crateSleeveSize
    // A caption is a title line and a subtitle line under the art.
    readonly property int captionHeight: Theme.scale(48)

    readonly property bool musicActive: App.interactionContext === "music"
                                        && page.StackView.status === StackView.Active

    readonly property var sections: [strip, heroScope, recentShelf, newShelf, stationShelf,
                                     genreShelf, artistShelf, forgottenShelf, pullShelf]

    // The last section index that actually held focus. A section can hide
    // out from under the keyboard (a shelf's error resolves to empty, the
    // hero loses its only reason to be on screen) without Qt moving focus
    // anywhere in particular, so recovery needs somewhere to search from.
    property int _lastSectionIndex: -1

    Accessible.role: Accessible.Pane
    Accessible.name: page.libraryName

    // Stale shelves refetch when Home comes back on screen. Within the TTL this
    // sends nothing (MusicRepository's cache answers).
    StackView.onActivated: MusicHomeCtl.refreshStale()

    // ── Sections ───────────────────────────────────────────────────────────
    function sectionFocusable(section): bool {
        if (!section || !section.visible)
            return false
        if (section === heroScope)
            return page.hasHero || page.heroError.length > 0
        if (section.focusable !== undefined)
            return section.focusable === true
        return true
    }

    function currentSection(): int {
        for (let i = 0; i < page.sections.length; ++i) {
            if (page.sections[i].activeFocus)
                return i
        }
        return -1
    }

    function focusSection(section): void {
        // Every section's own onActiveFocusChanged already calls
        // ensureVisible() (and records itself as the last focused section),
        // so forceActiveFocus() alone is enough here.
        section.forceActiveFocus(Qt.TabFocusReason)
    }

    function noteSectionFocus(section): void {
        const i = page.sections.indexOf(section)
        if (i >= 0)
            page._lastSectionIndex = i
    }

    function moveSection(step): bool {
        const from = page.currentSection()
        if (from < 0)
            return false
        for (let i = from + step; i >= 0 && i < page.sections.length; i += step) {
            if (page.sectionFocusable(page.sections[i])) {
                page.focusSection(page.sections[i])
                return true
            }
        }
        return false
    }

    // Focus is stranded: some section held it, that section (or its content)
    // is gone, and nothing else claimed it. Search outward from the last
    // known place, forward first, then back toward the top of the page.
    function recoverStranded(): bool {
        const from = page._lastSectionIndex >= 0 ? page._lastSectionIndex : 0
        for (let i = from; i < page.sections.length; ++i) {
            if (page.sectionFocusable(page.sections[i])) {
                page.focusSection(page.sections[i])
                return true
            }
        }
        for (let i = from - 1; i >= 0; --i) {
            if (page.sectionFocusable(page.sections[i])) {
                page.focusSection(page.sections[i])
                return true
            }
        }
        return false
    }

    // Called after anything that might have hidden the focused section out
    // from under the keyboard. A no-op unless focus is actually stranded.
    // A focused section that hides keeps active focus (Qt does not move it),
    // so a hidden current section counts as stranded too. Only `visible` is
    // checked, not sectionFocusable(): a shelf reloading into its skeleton
    // keeps the keyboard.
    function isStranded(): bool {
        const cur = page.currentSection()
        if (cur < 0)
            return true
        if (page.sections[cur].visible)
            return false
        page._lastSectionIndex = cur
        return true
    }

    function recoverIfStranded(): void {
        if (page.activeFocus && page.isStranded())
            page.recoverStranded()
    }

    // Up/Down when focus is stranded recover instead of doing nothing.
    function moveOrRecover(step): bool {
        if (page.isStranded())
            return page.recoverStranded()
        return page.moveSection(step)
    }

    // The triggers (Main.pageFocusedView): a screenful is about three shelves.
    function pageBy(step): bool {
        let moved = false
        for (let i = 0; i < 3; ++i) {
            if (!page.moveSection(step > 0 ? 1 : -1))
                break
            moved = true
        }
        return moved
    }

    // The shoulders (Main.cycleSection). The strip emits the next section and
    // the handler below turns it into a route request.
    function cycleTab(step): bool {
        return strip.cycle(step)
    }

    Keys.onUpPressed: event => { event.accepted = page.moveOrRecover(-1) }
    // Down at the last shelf stays put rather than leaving the page; when
    // focus is stranded it recovers to the nearest section instead.
    Keys.onDownPressed: event => {
        page.moveOrRecover(1)
        event.accepted = true
    }

    // ── Scrolling ──────────────────────────────────────────────────────────
    function scrollTo(y): void {
        const maxY = Math.max(0, scroll.contentHeight - scroll.height)
        scrollAnim.stop()
        scrollAnim.from = scroll.contentY
        scrollAnim.to = Math.max(0, Math.min(maxY, y))
        scrollAnim.start()
    }

    function ensureVisible(section): void {
        if (!section || !section.visible)
            return
        if (section === strip || section === heroScope) {
            page.scrollTo(0)
            return
        }
        const top = section.mapToItem(content, 0, 0).y
        const bottom = top + section.height
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

    // ── Rows ───────────────────────────────────────────────────────────────
    function itemAt(lane, index): var {
        return lane && lane.model && index >= 0 ? lane.model.get(index) : null
    }

    function idOf(item): string {
        return item && item.itemId !== undefined ? String(item.itemId) : ""
    }

    function openItem(lane, index): void {
        const item = page.itemAt(lane, index)
        if (item)
            Actions.openDetails(item)
    }

    function playAlbum(lane, index): void {
        const item = page.itemAt(lane, index)
        const id = page.idOf(item)
        if (id.length > 0)
            MusicPlay.playAlbum(id, item.title !== undefined ? String(item.title) : "", 0)
    }

    function openGenre(index): void {
        const bin = page.itemAt(MusicHomeCtl.genreLane, index)
        if (!bin)
            return
        const id = page.idOf(bin)
        if (id.length === 0)
            page.sectionRequested("genres")
        else
            page.genreRequested(id, bin.name !== undefined ? String(bin.name) : "")
    }

    function stationRow(key: string): int {
        const model = MusicHomeCtl.stationLane.model
        for (let i = 0; model && i < model.count; ++i) {
            if (page.idOf(model.get(i)) === key)
                return i
        }
        return -1
    }

    // L: the thing under the keyboard. The hero's album, else the focused card.
    function toggleFocusedFavourite(): void {
        if (heroScope.activeFocus && page.hasHero) {
            Actions.setFavorite(String(page.hero.albumId), page.hero.favourite !== true)
            return
        }
        const shelves = [recentShelf, newShelf, artistShelf, forgottenShelf, pullShelf]
        for (let i = 0; i < shelves.length; ++i) {
            const shelf = shelves[i]
            if (shelf.activeFocus && shelf.rail) {
                const item = page.itemAt(shelf.lane, shelf.rail.currentIndex)
                if (item)
                    Actions.toggleFavorite(item)
                return
            }
        }
    }

    // ── Hero keyboard walk ─────────────────────────────────────────────────
    function heroStops(): var {
        const stops = [heroPrimary, heroShuffle, heroFavourite, heroAnother]
        for (let i = 0; i < recentRows.count; ++i)
            stops.push(recentRows.itemAt(i))
        return stops.filter(stop => stop !== null && stop.visible)
    }

    function moveHero(step): bool {
        const stops = page.heroStops()
        let at = -1
        for (let i = 0; i < stops.length; ++i) {
            if (stops[i].activeFocus)
                at = i
        }
        const next = at + step
        if (at < 0 || next < 0 || next >= stops.length)
            return false
        stops[next].forceActiveFocus(Qt.TabFocusReason)
        return true
    }

    // Whether something inside the hero visibly holds the keyboard: one of
    // the walk stops, or the error line's Retry while it is showing.
    function heroHasFocusedStop(): bool {
        if (page.heroError.length > 0)
            return heroErrorLine.activeFocus === true
        const stops = page.heroStops()
        for (let i = 0; i < stops.length; ++i) {
            if (stops[i].activeFocus)
                return true
        }
        return false
    }

    // The hero's remembered focus child can go missing without heroScope
    // itself ever losing active focus: Retry succeeding (the error line
    // hides), the mode flipping pull-one-out -> resume (hides "Another
    // one"), or a focused "Recently played" row being destroyed on refresh.
    // Whenever that could have happened, land back on the first visible stop
    // (or the error line) rather than leaving heroScope focused but inert.
    function ensureHeroFocus(): void {
        if (!heroScope.activeFocus || page.heroHasFocusedStop())
            return
        if (page.heroError.length > 0) {
            heroErrorLine.forceActiveFocus(Qt.OtherFocusReason)
            return
        }
        const stops = page.heroStops()
        if (stops.length > 0)
            stops[0].forceActiveFocus(Qt.OtherFocusReason)
    }

    // Deferred: these handlers run before the hero children's `visible`
    // bindings have caught up, so heroStops() would still see the old set.
    onHasHeroChanged: Qt.callLater(page.ensureHeroFocus)
    onResumeModeChanged: Qt.callLater(page.ensureHeroFocus)
    onHeroErrorChanged: Qt.callLater(page.ensureHeroFocus)

    component RecentRow: Item {
        id: row

        required property int index
        required property string title
        required property string subtitle
        required property string coverUrl

        width: parent ? parent.width : Theme.scale(320)
        height: Theme.scale(56)
        activeFocusOnTab: false

        Accessible.role: Accessible.Button
        Accessible.name: row.subtitle.length > 0 ? row.title + ", " + row.subtitle : row.title
        Accessible.onPressAction: page.openItem(MusicHomeCtl.heroLane, row.index)

        Keys.onReturnPressed: event => { if (!event.isAutoRepeat) page.openItem(MusicHomeCtl.heroLane, row.index) }
        Keys.onEnterPressed: event => { if (!event.isAutoRepeat) page.openItem(MusicHomeCtl.heroLane, row.index) }
        Keys.onMenuPressed: {
            const p = row.mapToItem(null, row.width / 2, row.height)
            albumMenu.popupForItem(page.itemAt(MusicHomeCtl.heroLane, row.index), p.x, p.y)
        }

        HoverHandler { id: rowHover; cursorShape: Qt.PointingHandCursor }

        TapHandler {
            acceptedButtons: Qt.LeftButton
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: page.openItem(MusicHomeCtl.heroLane, row.index)
        }

        TapHandler {
            acceptedButtons: Qt.RightButton
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: eventPoint => {
                const p = row.mapToItem(null, eventPoint.position.x, eventPoint.position.y)
                albumMenu.popupForItem(page.itemAt(MusicHomeCtl.heroLane, row.index), p.x, p.y)
            }
        }

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusChip
            color: Theme.surfaceRaisedColor
            opacity: rowHover.hovered ? 1 : 0
        }

        Rectangle {
            id: thumb
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingTight
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.scale(44)
            height: width
            radius: Theme.crateSleeveRadius
            color: Theme.surfaceRaisedColor
            clip: true

            StrmImage {
                anchors.fill: parent
                source: row.coverUrl
                suppressWarnings: true
            }
        }

        Column {
            anchors.left: thumb.right
            anchors.leftMargin: Theme.spacingValue
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingTight
            anchors.verticalCenter: parent.verticalCenter

            Text {
                width: parent.width
                text: row.title
                color: Theme.textPrimaryColor
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontSmall
                font.weight: Font.DemiBold
                textFormat: Text.PlainText
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                text: row.subtitle
                color: Theme.textSecondaryColor
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontCaption
                textFormat: Text.PlainText
                elide: Text.ElideRight
            }
        }

        FocusRing {
            active: row.activeFocus
            radius: Theme.radiusChip
        }
    }

    // ── Shelf delegates (Task 4 protocol: plain model/index/current/hovered) ─
    Component {
        id: sleeveCard

        CrateSleeve {
            id: sleeveItem

            property var model: null
            property int index: -1

            size: page.sleeveSize
            coverUrl: sleeveItem.model && sleeveItem.model.coverUrl ? String(sleeveItem.model.coverUrl) : ""
            title: sleeveItem.model && sleeveItem.model.title ? String(sleeveItem.model.title) : ""
            subtitle: sleeveItem.model && sleeveItem.model.subtitle ? String(sleeveItem.model.subtitle) : ""
            badge: sleeveItem.model && sleeveItem.model.releaseBadge ? String(sleeveItem.model.releaseBadge) : ""
        }
    }

    Component {
        id: portraitCard

        CratePortrait {
            id: portraitItem

            property var model: null
            property int index: -1

            size: Theme.cratePortraitSize
            imageUrl: portraitItem.model && portraitItem.model.coverUrl ? String(portraitItem.model.coverUrl) : ""
            name: portraitItem.model && portraitItem.model.name ? String(portraitItem.model.name) : ""
            subtitle: portraitItem.model && portraitItem.model.subtitle ? String(portraitItem.model.subtitle) : ""
        }
    }

    Component {
        id: stationCard

        StationTile {
            id: stationItem

            property var model: null
            property int index: -1

            size: page.sleeveSize
            covers: stationItem.model && stationItem.model.covers ? stationItem.model.covers : []
            label: stationItem.model && stationItem.model.label ? String(stationItem.model.label) : ""
        }
    }

    Component {
        id: genreCard

        GenreBinTile {
            id: genreItem

            property var model: null
            property int index: -1

            size: page.sleeveSize
            name: genreItem.model && genreItem.model.name ? String(genreItem.model.name) : ""
            subtitle: genreItem.model && genreItem.model.subtitle ? String(genreItem.model.subtitle) : ""
            covers: genreItem.model && genreItem.model.covers ? genreItem.model.covers : []
            isAllBin: genreItem.model !== null && String(genreItem.model.itemId || "").length === 0
        }
    }

    // ── Body ───────────────────────────────────────────────────────────────
    Flickable {
        id: scroll

        anchors.fill: parent
        contentWidth: width
        contentHeight: content.height
        boundsBehavior: Flickable.StopAtBounds
        interactive: scroll.contentHeight > scroll.height
        clip: true

        ScrollBar.vertical: StrmScrollBar {}

        Item {
            id: content

            width: scroll.width
            height: column.implicitHeight + Theme.pageMarginValue

            // The sleeve lights the room: the wash sits behind the strip and the
            // hero, and keeps CoverTint's clamp (CoverWash.qml decides nothing).
            CoverWash {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: heroScope.y + heroScope.height + Theme.railGap
                visible: page.hasHero
                source: page.hasHero ? String(page.hero.coverUrl) : ""
            }

            Column {
                id: column

                width: content.width
                spacing: Theme.railGap

                Item { width: 1; height: Math.max(0, Theme.spacingValue) }

                SectionStrip {
                    id: strip

                    x: Theme.pageMarginValue
                    currentKey: "home"
                    onSectionChosen: key => {
                        if (key !== "home")
                            page.sectionRequested(key)
                    }
                    onActiveFocusChanged: {
                        if (strip.activeFocus) {
                            page.ensureVisible(strip)
                            page.noteSectionFocus(strip)
                        }
                    }
                    onVisibleChanged: Qt.callLater(page.recoverIfStranded)
                }

                // ── Hero ───────────────────────────────────────────────────
                FocusScope {
                    id: heroScope

                    width: column.width
                    height: visible ? heroBody.height : 0
                    visible: page.hasHero || page.heroLoading || page.heroError.length > 0
                    focus: true
                    // The hero is one tab stop; its buttons and the error
                    // line's Retry opt out of the flat Tab chain below.
                    activeFocusOnTab: page.sectionFocusable(heroScope)

                    Keys.onLeftPressed: event => { event.accepted = page.moveHero(-1) }
                    Keys.onRightPressed: event => {
                        page.moveHero(1)
                        event.accepted = true
                    }
                    onActiveFocusChanged: {
                        if (heroScope.activeFocus) {
                            page.ensureVisible(heroScope)
                            page.noteSectionFocus(heroScope)
                        }
                        page.ensureHeroFocus()
                    }
                    onVisibleChanged: Qt.callLater(page.recoverIfStranded)

                    Item {
                        id: heroBody

                        x: Theme.pageMarginValue
                        width: parent.width - Theme.pageMarginValue * 2
                        height: page.heroError.length > 0 ? heroErrorLine.height
                              : Math.max(page.heroSleeveSize, heroText.implicitHeight)

                        // Skeleton, in the hero's shape: the sleeve and three lines.
                        Row {
                            visible: page.heroLoading
                            spacing: Theme.spacingLoose

                            StrmSkeleton {
                                width: page.heroSleeveSize
                                height: page.heroSleeveSize
                                radius: Theme.crateSleeveRadius
                            }

                            Column {
                                spacing: Theme.spacingValue
                                anchors.verticalCenter: parent.verticalCenter

                                StrmSkeleton { width: Theme.scale(180); height: Theme.crateKickerSize; radius: Theme.radiusChip }
                                StrmSkeleton { width: Theme.scale(420); height: Theme.crateHeroHome; radius: Theme.radiusChip }
                                StrmSkeleton { width: Theme.scale(300); height: Theme.fontSmall; radius: Theme.radiusChip }
                            }
                        }

                        ShelfError {
                            id: heroErrorLine

                            visible: page.heroError.length > 0
                            focus: page.heroError.length > 0
                            message: page.heroError
                            onRetry: MusicHomeCtl.heroLane.retry()
                        }

                        CrateSleeve {
                            id: heroSleeve

                            visible: page.hasHero
                            size: page.heroSleeveSize
                            showCaption: false
                            coverUrl: page.hasHero ? String(page.hero.coverUrl) : ""
                            title: page.hasHero ? String(page.hero.title) : ""
                            onActivated: Actions.openDetails(page.hero.albumItem)
                            onPlayRequested: MusicHomeCtl.resumeHero()
                            onMenuRequested: (mx, my) => albumMenu.popupForItem(page.hero.albumItem, mx, my)
                        }

                        Column {
                            id: heroText

                            visible: page.hasHero
                            anchors.left: heroSleeve.right
                            anchors.leftMargin: Theme.spacingLoose
                            anchors.right: recentColumn.visible ? recentColumn.left : parent.right
                            anchors.rightMargin: recentColumn.visible ? Theme.spacingLoose : 0
                            anchors.verticalCenter: heroSleeve.verticalCenter
                            spacing: Theme.spacingValue

                            CrateKicker {
                                width: parent.width
                                text: page.resumeMode ? qsTr("Pick up where you left off") : qsTr("Pull one out")
                            }

                            CrateHeading {
                                width: parent.width
                                pixelSize: Theme.crateHeroHome
                                maximumLineCount: 2
                                wrapMode: Text.WordWrap
                                text: page.hasHero ? String(page.hero.title) : ""
                            }

                            Text {
                                width: parent.width
                                text: page.hasHero ? String(page.hero.summary) : ""
                                color: Theme.textSecondaryColor
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.fontSmall
                                font.features: ({ "tnum": 1 })
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                            }

                            // The progress line through the album (resume only).
                            Rectangle {
                                id: progressTrack

                                visible: page.resumeMode
                                width: Math.min(heroText.width, Theme.scale(420))
                                height: Theme.scale(3)
                                radius: progressTrack.height / 2
                                color: Theme.hairline
                                Accessible.role: Accessible.ProgressBar
                                Accessible.name: qsTr("Album progress")

                                Rectangle {
                                    width: progressTrack.width
                                           * Math.max(0, Math.min(1, Number(page.hero.progress || 0)))
                                    height: progressTrack.height
                                    radius: progressTrack.radius
                                    color: Theme.accentColor
                                }
                            }

                            Row {
                                spacing: Theme.spacingValue

                                StrmButton {
                                    id: heroPrimary
                                    focus: true
                                    // The hero is one tab stop (heroScope); its
                                    // own Left/Right walk the buttons instead.
                                    activeFocusOnTab: false
                                    variant: "primary"
                                    iconName: "play"
                                    text: page.hasHero ? String(page.hero.resumeLabel) : ""
                                    onClicked: MusicHomeCtl.resumeHero()
                                }

                                StrmButton {
                                    id: heroShuffle
                                    visible: page.resumeMode
                                    activeFocusOnTab: false
                                    iconName: "shuffle"
                                    text: qsTr("Shuffle album")
                                    onClicked: MusicHomeCtl.shuffleHero()
                                }

                                StrmIconButton {
                                    id: heroFavourite
                                    visible: page.resumeMode
                                    activeFocusOnTab: false
                                    anchors.verticalCenter: parent.verticalCenter
                                    iconName: page.hero.favourite === true ? "heart-filled" : "heart"
                                    checked: page.hero.favourite === true
                                    tooltip: page.hero.favourite === true ? qsTr("Remove from favourites")
                                                                          : qsTr("Add to favourites")
                                    onClicked: Actions.setFavorite(String(page.hero.albumId),
                                                                   page.hero.favourite !== true)
                                }

                                StrmButton {
                                    id: heroAnother
                                    visible: page.hasHero && !page.resumeMode
                                    activeFocusOnTab: false
                                    iconName: "refresh"
                                    text: qsTr("Another one")
                                    onClicked: MusicHomeCtl.anotherOne()
                                }
                            }
                        }

                        // "Recently played": the three albums played before the hero.
                        Column {
                            id: recentColumn

                            visible: page.hasHero && recentRows.count > 0 && heroBody.width > Theme.scale(900)
                            anchors.right: parent.right
                            anchors.verticalCenter: heroSleeve.verticalCenter
                            width: Theme.scale(320)
                            spacing: Theme.spacingTight

                            CrateKicker {
                                text: qsTr("Recently played")
                            }

                            Repeater {
                                id: recentRows

                                model: MusicHomeCtl.heroLane.model
                                delegate: RecentRow {}
                                // A refresh can destroy the row the keyboard was
                                // on (heroScope's own activeFocus never toggles
                                // for this), so check explicitly. Deferred: a model
                                // reset removes every row, and itemAt() still hands
                                // back the dying focused row until the removal ends.
                                onItemRemoved: (index, item) => {
                                    if (item && item.activeFocus)
                                        Qt.callLater(page.ensureHeroFocus)
                                }
                            }
                        }
                    }
                }

                // ── Shelves, in spec order ─────────────────────────────────
                CrateShelf {
                    id: recentShelf

                    title: qsTr("Recently played")
                    lane: MusicHomeCtl.recentLane
                    delegate: sleeveCard
                    cardWidth: page.sleeveSize
                    cardHeight: page.sleeveSize + Theme.spacingTight + page.captionHeight
                    navigationFocusKey: "musicHome-recent"
                    onItemActivated: index => page.openItem(recentShelf.lane, index)
                    onItemPlayRequested: index => page.playAlbum(recentShelf.lane, index)
                    onMenuRequested: (index, mx, my) =>
                        albumMenu.popupForItem(page.itemAt(recentShelf.lane, index), mx, my)
                    onActiveFocusChanged: {
                        if (recentShelf.activeFocus) {
                            page.ensureVisible(recentShelf)
                            page.noteSectionFocus(recentShelf)
                        }
                    }
                    onVisibleChanged: Qt.callLater(page.recoverIfStranded)
                }

                CrateShelf {
                    id: newShelf

                    title: qsTr("New in the crate")
                    kicker: MusicHomeCtl.addedThisWeekText
                    lane: MusicHomeCtl.newLane
                    delegate: sleeveCard
                    cardWidth: page.sleeveSize
                    cardHeight: page.sleeveSize + Theme.spacingTight + page.captionHeight
                    navigationFocusKey: "musicHome-new"
                    onItemActivated: index => page.openItem(newShelf.lane, index)
                    onItemPlayRequested: index => page.playAlbum(newShelf.lane, index)
                    onMenuRequested: (index, mx, my) =>
                        albumMenu.popupForItem(page.itemAt(newShelf.lane, index), mx, my)
                    onActiveFocusChanged: {
                        if (newShelf.activeFocus) {
                            page.ensureVisible(newShelf)
                            page.noteSectionFocus(newShelf)
                        }
                    }
                    onVisibleChanged: Qt.callLater(page.recoverIfStranded)
                }

                CrateShelf {
                    id: stationShelf

                    title: qsTr("Stations")
                    lane: MusicHomeCtl.stationLane
                    delegate: stationCard
                    cardWidth: page.sleeveSize
                    cardHeight: page.sleeveSize + Theme.spacingTight + Theme.scale(24)
                    navigationFocusKey: "musicHome-stations"
                    // One press resolves and plays.
                    onItemActivated: index => MusicHomeCtl.playStation(index)
                    onMenuRequested: (index, mx, my) => {
                        stationMenu.row = index
                        stationMenu.popupAt(mx, my)
                    }
                    onActiveFocusChanged: {
                        if (stationShelf.activeFocus) {
                            page.ensureVisible(stationShelf)
                            page.noteSectionFocus(stationShelf)
                        }
                    }
                    onVisibleChanged: Qt.callLater(page.recoverIfStranded)
                }

                CrateShelf {
                    id: genreShelf

                    title: qsTr("Dig by genre")
                    lane: MusicHomeCtl.genreLane
                    delegate: genreCard
                    cardWidth: page.sleeveSize
                    cardHeight: Math.round(page.sleeveSize * 0.8) + Theme.spacingTight + page.captionHeight
                    navigationFocusKey: "musicHome-genres"
                    onItemActivated: index => page.openGenre(index)
                    onActiveFocusChanged: {
                        if (genreShelf.activeFocus) {
                            page.ensureVisible(genreShelf)
                            page.noteSectionFocus(genreShelf)
                        }
                    }
                    onVisibleChanged: Qt.callLater(page.recoverIfStranded)
                }

                CrateShelf {
                    id: artistShelf

                    title: qsTr("Artists you play")
                    lane: MusicHomeCtl.artistLane
                    delegate: portraitCard
                    skeletonShape: "round"
                    cardWidth: Theme.cratePortraitSize
                    cardHeight: Theme.cratePortraitSize + Theme.spacingTight + page.captionHeight
                    navigationFocusKey: "musicHome-artists"
                    onItemActivated: index => page.openItem(artistShelf.lane, index)
                    onMenuRequested: (index, mx, my) =>
                        albumMenu.popupForItem(page.itemAt(artistShelf.lane, index), mx, my)
                    onActiveFocusChanged: {
                        if (artistShelf.activeFocus) {
                            page.ensureVisible(artistShelf)
                            page.noteSectionFocus(artistShelf)
                        }
                    }
                    onVisibleChanged: Qt.callLater(page.recoverIfStranded)
                }

                CrateShelf {
                    id: forgottenShelf

                    title: qsTr("Forgotten favourites")
                    lane: MusicHomeCtl.forgottenLane
                    delegate: sleeveCard
                    cardWidth: page.sleeveSize
                    cardHeight: page.sleeveSize + Theme.spacingTight + page.captionHeight
                    navigationFocusKey: "musicHome-forgotten"
                    onItemActivated: index => page.openItem(forgottenShelf.lane, index)
                    onItemPlayRequested: index => page.playAlbum(forgottenShelf.lane, index)
                    onMenuRequested: (index, mx, my) =>
                        albumMenu.popupForItem(page.itemAt(forgottenShelf.lane, index), mx, my)
                    onActiveFocusChanged: {
                        if (forgottenShelf.activeFocus) {
                            page.ensureVisible(forgottenShelf)
                            page.noteSectionFocus(forgottenShelf)
                        }
                    }
                    onVisibleChanged: Qt.callLater(page.recoverIfStranded)
                }

                CrateShelf {
                    id: pullShelf

                    title: qsTr("Pull one out")
                    actionText: qsTr("Reshuffle")
                    actionIcon: "refresh"
                    lane: MusicHomeCtl.pullLane
                    delegate: sleeveCard
                    cardWidth: page.sleeveSize
                    cardHeight: page.sleeveSize + Theme.spacingTight + page.captionHeight
                    navigationFocusKey: "musicHome-pull"
                    onActionTriggered: MusicHomeCtl.reshuffle()
                    onItemActivated: index => page.openItem(pullShelf.lane, index)
                    onItemPlayRequested: index => page.playAlbum(pullShelf.lane, index)
                    onMenuRequested: (index, mx, my) =>
                        albumMenu.popupForItem(page.itemAt(pullShelf.lane, index), mx, my)
                    onActiveFocusChanged: {
                        if (pullShelf.activeFocus) {
                            page.ensureVisible(pullShelf)
                            page.noteSectionFocus(pullShelf)
                        }
                    }
                    onVisibleChanged: Qt.callLater(page.recoverIfStranded)
                }
            }
        }
    }

    // ── Menus ──────────────────────────────────────────────────────────────
    // Albums and artists: the central item menu, the same verbs as Browse.
    ItemMenu {
        id: albumMenu

        profile: "musicBrowse"
        allowMusicNavigation: true
    }

    StrmMenu {
        id: stationMenu

        property int row: -1

        actions: [
            { "text": qsTr("Play"), "iconName": "play" },
            { "text": qsTr("Shuffle"), "iconName": "shuffle" },
            { "text": qsTr("Add to queue"), "iconName": "queue" }
        ]
        onTriggered: index => {
            if (index === 0)
                MusicHomeCtl.playStation(stationMenu.row)
            else if (index === 1)
                MusicHomeCtl.shuffleStation(stationMenu.row)
            else if (index === 2)
                MusicHomeCtl.queueStation(stationMenu.row)
        }
    }

    // ── The music input context (spec §8) ──────────────────────────────────
    // Space plays or pauses, S shuffles, L favourites. Home has no instant mix
    // key: nothing on it is a single seed the way an album or artist page is.
    MappedShortcut {
        actionId: "music.playPause"
        fallback: ["Space"]
        // Only while something is loaded, so Space stays Select otherwise.
        active: page.musicActive && PlayerCtl.active
        onActivated: PlayerCtl.togglePause()
    }

    MappedShortcut {
        actionId: "music.shuffleAll"
        fallback: ["S"]
        // S on Home is the Shuffle all station: the whole library, sampled by the server.
        active: page.musicActive && page.stationRow("shuffleAll") >= 0
        onActivated: MusicHomeCtl.playStation(page.stationRow("shuffleAll"))
    }

    MappedShortcut {
        actionId: "music.favorite"
        fallback: ["L"]
        active: page.musicActive
        onActivated: page.toggleFocusedFavourite()
    }
}
