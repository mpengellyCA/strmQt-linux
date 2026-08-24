pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import StrmQt

// Person page (ARCHITECTURE.md): headshot, name, birth facts, biography, and the
// person's filmography on this server.
//
// What this replaces: clicking a cast member opened the library grid with the
// person's name pinned to the header. That is a *filter*, not a page — it
// answers "what else is here" and nothing else. Everything the server already
// sends about a person (Overview, PremiereDate, ProductionLocations, the
// headshot) was fetched by nobody and shown nowhere.
//
// ── Where the data comes from ──────────────────────────────────────────────
//
//  * The person record is `DetailsCtl.loadPerson(id)` → `DetailsCtl.person`,
//    a {id, name, overview, birthDate, imageUrl} map. Emby files a person's
//    birth date under PremiereDate; the controller renames it, which is why
//    nothing here parses a date field with a misleading name.
//  * The filmography is `LibraryCtl.openPerson(id, name)` — the same PersonIds
//    query, newest first, paged, that the filtered grid used. It is deliberately
//    NOT re-implemented: this page wants exactly that list.
//
// ── Sharing two controllers with two other pages ───────────────────────────
//
// Both controllers are single instances shared app-wide, so this page can be
// left holding neither:
//
//  * opening a film from the filmography calls DetailsCtl.load(), which clears
//    `person` — so the record is snapshotted into `personData` on arrival and
//    never read live. Coming back shows the person immediately, with no refetch
//    and no flash of an empty hero.
//  * that same navigation leaves LibraryCtl where it was, but opening a
//    *second* person (cast → film → cast) re-scopes it. `scopeKey` is the
//    controller's own answer to "whose list is this" ("person:<id>"), so the
//    grid is shown only while the scope is this page's, and re-armed when the
//    page comes back on screen. Nothing is guessed and no state is mirrored.
//
// This is a workaround for a shared controller, not a fix; the report for this
// wave asks for the fix (a second LibraryController instance for scoped browse
// views, or `prepare` being re-run on pop as well as on forward).
//
// Focus model, as everywhere: hover never moves the keyboard's place. The hero
// has exactly one tab stop (the biography's More/Less button, and only when the
// biography is long enough to need one); everything else on it is text.
FocusScope {
    id: page

    property string personId: ""
    property string personName: ""

    // Snapshot of DetailsCtl.person, taken when it is *this* person. Never a
    // live binding — see the header note.
    property var personData: ({})
    property bool bioExpanded: false
    // DetailsController exposes no `loading` role, so "still fetching" is
    // derived: the flag is raised around the request and lowered by the arrival
    // of a record for *this* person, and the guard timer stops a failed fetch —
    // which emits nothing further — from leaving a skeleton up forever. Same
    // shape as DetailsPage.
    property bool personLoading: true

    readonly property bool haveRecord: page.personData
                                       && page.personData.id !== undefined
                                       && String(page.personData.id).length > 0

    // The pushed title is available before the fetch lands, so the page never
    // opens with a blank name.
    readonly property string displayName: {
        const fromRecord = page.haveRecord && page.personData.name
                           ? String(page.personData.name) : ""
        return fromRecord.length > 0 ? fromRecord : page.personName
    }

    readonly property string headshotUrl: (page.haveRecord && page.personData.imageUrl)
                                          ? String(page.personData.imageUrl) : ""
    readonly property string biography: (page.haveRecord && page.personData.overview)
                                        ? String(page.personData.overview) : ""

    // ── Birth facts ────────────────────────────────────────────────────────
    // `birthDate` is ISO-8601 as the server sent it. An unparseable value is
    // shown as nothing rather than as "Invalid Date", and a date-only person
    // (the common case) still formats.
    readonly property string birthDateText: {
        const raw = (page.haveRecord && page.personData.birthDate)
                    ? String(page.personData.birthDate) : ""
        if (raw.length === 0)
            return ""
        const parsed = new Date(raw)
        if (isNaN(parsed.getTime()))
            return ""
        return Qt.formatDate(parsed, Locale.LongFormat)
    }

    // NOT in the controller's map yet (reported for this wave). Reading it
    // defensively means the field appears the moment `birthplace` is added and
    // nothing here changes; until then the row is simply absent, which is also
    // the right rendering for a person the server has no birthplace for.
    readonly property string birthplaceText: {
        const value = page.haveRecord ? page.personData.birthplace : undefined
        return (value === undefined || value === null) ? "" : String(value)
    }

    // ── Filmography scope ──────────────────────────────────────────────────
    readonly property string wantScope: page.personId.length > 0
                                        ? "person:" + page.personId : ""
    readonly property bool scopeMine: page.wantScope.length > 0
                                      && LibraryCtl.scopeKey === page.wantScope
    readonly property int filmCount: page.scopeMine
                                     ? LibraryCtl.model.totalRecordCount : 0
    readonly property bool filmsLoading: page.scopeMine && LibraryCtl.loading
                                         && LibraryCtl.model.count === 0
    readonly property bool filmsFailed: page.scopeMine && !LibraryCtl.loading
                                        && LibraryCtl.model.count === 0
                                        && LibraryCtl.errorMessage.length > 0
    readonly property bool filmsEmpty: page.scopeMine && !LibraryCtl.loading
                                       && LibraryCtl.model.count === 0
                                       && LibraryCtl.errorMessage.length === 0

    // ── Loading ────────────────────────────────────────────────────────────
    function loadPerson(): void {
        if (page.personId.length === 0) {
            // Nothing to fetch and nothing to wait for. Without this the page
            // sits on a skeleton forever, because the guard timer is only armed
            // by a request that was actually made — which is exactly what the
            // page-construction self-test would build it with.
            page.personLoading = false
            return
        }
        // Flag first, request second. loadPerson() clears the controller's map
        // and emits synchronously before it fetches; raising the flag afterwards
        // would let a record that arrives on that same call stack be overwritten
        // by a `true` nothing ever clears except the guard timer.
        page.personLoading = true
        loadGuard.restart()
        DetailsCtl.loadPerson(page.personId)
    }

    // Re-arms the shared library controller when something else has taken it.
    function ensureScope(): void {
        if (page.personId.length === 0 || page.scopeMine)
            return
        LibraryCtl.openPerson(page.personId, page.displayName)
    }

    function itemAt(index) {
        const model = LibraryCtl.model
        if (!model || index < 0 || index >= model.count)
            return null
        return model.get(index)
    }

    function showMenu(index, sceneX, sceneY): void {
        itemMenu.popupForItem(page.itemAt(index), sceneX, sceneY)
    }

    Component.onCompleted: {
        page.loadPerson()
        page.ensureScope()
    }

    // StackView hides a covered page and shows it again on pop. Coming back
    // from a film opened out of the filmography is the case this exists for:
    // the record is already snapshotted, so only the shared grid needs re-arming.
    onVisibleChanged: {
        if (!page.visible)
            return
        page.ensureScope()
        // `!haveRecord` alone would re-request on the page's *first* show too —
        // StackView pushes an item invisible and then shows it, and the fetch
        // started in Component.onCompleted has not landed by then. The
        // in-flight flag is what makes this a recovery path rather than a
        // second request every time the page is entered.
        if (!page.haveRecord && !page.personLoading)
            page.loadPerson()
    }

    Connections {
        target: DetailsCtl

        function onPersonChanged() {
            const record = DetailsCtl.person
            // loadPerson() clears the map before it fetches, and DetailsPage
            // clears it by loading an item; neither is this person arriving, so
            // an empty or foreign map must never overwrite the snapshot.
            if (!record || record.id === undefined
                    || String(record.id) !== page.personId)
                return
            page.personData = record
            page.personLoading = false
            loadGuard.stop()
        }
    }

    Timer {
        id: loadGuard
        interval: 6000
        onTriggered: page.personLoading = false
    }

    // ── Atmosphere (ARCHITECTURE.md) ────────────────────────────────────────
    // The headshot itself, blurred and desaturated behind the hero. A person
    // has no backdrop art on any server, so this is the only way the page gets
    // the wash every other page has — and it costs one already-cached image.
    // Gated on Prefs so the backdrop switch means the same thing everywhere,
    // and on `visible` so an off wash stops the offscreen render rather than
    // drawing it at zero opacity.
    Item {
        id: wash

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Math.round(page.height * 0.42)
        z: -1
        visible: Prefs.backdropEnabled && page.headshotUrl.length > 0
                 && wash.opacity > 0.001
        opacity: Prefs.backdropEnabled ? Prefs.backdropOpacity / 100 : 0

        Behavior on opacity {
            NumberAnimation { duration: Theme.animNormalMs; easing.type: Theme.easeStandard }
        }

        layer.enabled: wash.visible
        layer.effect: MultiEffect {
            autoPaddingEnabled: false
            blurEnabled: true
            blur: 1.0
            blurMax: 48
            saturation: -0.55
        }

        Image {
            anchors.fill: parent
            source: page.headshotUrl
            sourceSize.width: Theme.scale(640)
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            opacity: status === Image.Ready ? 1 : 0

            Behavior on opacity {
                NumberAnimation { duration: Theme.animSlow; easing.type: Theme.easeEmphasis }
            }
        }
    }

    // The wash has to end somewhere; a hard edge reads as a bug.
    Rectangle {
        anchors.fill: wash
        z: -1
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 0.45; color: "transparent" }
            GradientStop { position: 1.0; color: Theme.ground }
        }
    }

    // ── Inline pieces ──────────────────────────────────────────────────────
    // Both are used only on this page, so they are page-local components rather
    // than two more files in controls/ — the same call DetailsPage made for its
    // ChipStrip and CastShelf. ARCHITECTURE.md is about not re-inventing a
    // *button* per page; this is one definition with local uses.

    // A booth gear label over a value: small tracked mono caption, then the
    // fact. Absent facts render as nothing at all rather than as a label with
    // a blank beside it.
    component Fact: Column {
        id: fact

        property string label: ""
        property string value: ""

        spacing: Theme.scale(2)
        visible: fact.value.length > 0

        Text {
            text: fact.label.toUpperCase()
            color: Theme.textTertiary
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontCaption
            font.letterSpacing: Theme.fontCaption * Theme.trackLabel
        }

        Text {
            text: fact.value
            color: Theme.textPrimaryColor
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontBodySize
        }
    }

    // The headshot, with the fallback treated as the normal case it is: on a
    // typical film the server has a Primary image for part of the billing order
    // and nothing for the rest. Initials on a tinted ground keep the page's
    // shape identical either way, and the photo — when there is one — crossfades
    // in over it rather than over a hole.
    component Portrait: Rectangle {
        id: portrait

        property string imageUrl: ""
        property string name: ""

        readonly property string initials: {
            const parts = portrait.name.trim().split(/\s+/).filter(p => p.length > 0)
            if (parts.length === 0)
                return ""
            if (parts.length === 1)
                return parts[0].charAt(0).toUpperCase()
            return String(parts[0].charAt(0)
                          + parts[parts.length - 1].charAt(0)).toUpperCase()
        }

        radius: Theme.radiusPanel
        color: Theme.surfaceColor
        border.width: 1
        border.color: Theme.hairline
        clip: true

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: Theme.surfaceRaisedColor }
                GradientStop { position: 1.0; color: Theme.surfaceColor }
            }

            Text {
                anchors.centerIn: parent
                visible: portrait.initials.length > 0
                text: portrait.initials
                color: Theme.textTertiary
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.fontDisplaySize
                font.weight: Font.DemiBold
            }

            StrmIcon {
                anchors.centerIn: parent
                visible: portrait.initials.length === 0
                name: "user"
                size: Theme.scale(56)
                color: Theme.textTertiary
            }
        }

        Image {
            anchors.fill: parent
            source: portrait.imageUrl
            sourceSize.width: portrait.width
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            opacity: status === Image.Ready ? 1 : 0

            Behavior on opacity {
                NumberAnimation { duration: Theme.animNormalMs; easing.type: Theme.easeStandard }
            }
        }
    }

    // ── Hero ───────────────────────────────────────────────────────────────
    readonly property int portraitWidth: Theme.scale(200)
    readonly property int portraitHeight: Math.round(page.portraitWidth * 3 / 2)

    // Collapsed biography height, in lines. Long enough to be worth reading,
    // short enough that the filmography is still on screen without scrolling.
    readonly property int bioCollapsedLines: 4
    readonly property int bioLineHeight: Math.round(Theme.fontBodySize * Theme.lineLoose)
    readonly property int bioCollapsedHeight: page.bioLineHeight * page.bioCollapsedLines
    // Expanded, the biography is capped so it can never push the filmography
    // off the page; past the cap it scrolls inside its own box.
    readonly property int bioMaxHeight: Math.max(page.bioCollapsedHeight,
                                                 Math.round(page.height * 0.34))
    readonly property bool bioOverflows: bioText.implicitHeight > page.bioCollapsedHeight + 1

    Item {
        id: hero

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: Theme.pageMarginValue
        anchors.leftMargin: Theme.pageMarginValue
        anchors.rightMargin: Theme.pageMarginValue
        height: Math.max(portraitFrame.height, heroText.implicitHeight)
        // A person the server would not describe has no hero to draw: the page
        // is the error state, not the error state laid over a blank hero.
        visible: !page.personLoading && page.haveRecord

        Portrait {
            id: portraitFrame

            anchors.top: parent.top
            anchors.left: parent.left
            width: page.portraitWidth
            height: page.portraitHeight
            imageUrl: page.headshotUrl
            name: page.displayName
        }

        Column {
            id: heroText

            anchors.top: parent.top
            anchors.left: portraitFrame.right
            anchors.right: parent.right
            anchors.leftMargin: Theme.spacingLoose
            spacing: Theme.spacingValue

            Text {
                width: parent.width
                text: page.displayName
                color: Theme.textPrimaryColor
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.fontDisplaySize
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                maximumLineCount: 2
                wrapMode: Text.WordWrap
            }

            Row {
                spacing: Theme.spacingLoose
                visible: page.birthDateText.length > 0 || page.birthplaceText.length > 0

                Fact {
                    label: qsTr("Born")
                    value: page.birthDateText
                }

                Fact {
                    label: qsTr("Birthplace")
                    value: page.birthplaceText
                }
            }

            // ── Biography ──────────────────────────────────────────────────
            // Clipped to a fixed box that animates between collapsed and
            // expanded. Expanded past the cap the text scrolls in place, so the
            // filmography below never moves off screen no matter how long a
            // biography the server has.
            Item {
                id: bioBox

                width: parent.width
                visible: page.biography.length > 0
                clip: true
                height: {
                    if (!bioBox.visible)
                        return 0
                    if (!page.bioExpanded)
                        return Math.min(bioText.implicitHeight, page.bioCollapsedHeight)
                    return Math.min(bioText.implicitHeight, page.bioMaxHeight)
                }

                Behavior on height {
                    NumberAnimation {
                        duration: Theme.animNormalMs
                        easing.type: Theme.easeStandard
                    }
                }

                Flickable {
                    id: bioScroll

                    anchors.fill: parent
                    contentWidth: width
                    contentHeight: bioText.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds
                    interactive: page.bioExpanded
                                 && bioScroll.contentHeight > bioScroll.height
                    // The page owns no vertical scroll of its own, so this box
                    // may take the wheel — but only while it can actually use it.
                    ScrollBar.vertical: StrmScrollBar {}

                    Text {
                        id: bioText

                        width: bioScroll.width
                        text: page.biography
                        color: Theme.textSecondaryColor
                        font.family: Theme.fontBody
                        font.pixelSize: Theme.fontBodySize
                        lineHeight: Theme.lineLoose
                        lineHeightMode: Text.ProportionalHeight
                        wrapMode: Text.WordWrap
                        textFormat: Text.PlainText
                    }
                }

                // Collapsed, the text is cut mid-line; fading the last line into
                // the ground says "there is more" before the button does.
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: page.bioLineHeight
                    visible: !page.bioExpanded && page.bioOverflows
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "transparent" }
                        GradientStop { position: 1.0; color: Theme.ground }
                    }
                }
            }

            StrmButton {
                id: bioToggle

                visible: page.biography.length > 0 && page.bioOverflows
                // Focus follows content, not visibility: an invisible button
                // must not be the thing the page hands focus to.
                focus: bioToggle.visible
                variant: "ghost"
                text: page.bioExpanded ? qsTr("Show less") : qsTr("Read more")
                iconName: page.bioExpanded ? "chevron-up" : "chevron-down"
                KeyNavigation.down: filmGrid
                onClicked: page.bioExpanded = !page.bioExpanded
            }
        }
    }

    // ── Filmography ────────────────────────────────────────────────────────
    Item {
        id: filmHeading

        anchors.top: hero.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: Theme.railGap
        anchors.leftMargin: Theme.pageMarginValue
        anchors.rightMargin: Theme.pageMarginValue
        height: filmHeading.visible ? headingLabel.implicitHeight : 0
        visible: !page.personLoading && page.haveRecord

        Text {
            id: headingLabel

            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Filmography").toUpperCase()
            color: Theme.textTertiary
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontCaption
            font.letterSpacing: Theme.fontCaption * Theme.trackLabel
        }

        // Tabular count, in mono, for the same reason every other readout in
        // this app is: it is a number, not a sentence.
        Text {
            anchors.left: headingLabel.right
            anchors.leftMargin: Theme.spacingValue
            anchors.baseline: headingLabel.baseline
            visible: page.filmCount > 0
            text: qsTr("%1 titles", "", page.filmCount).arg(page.filmCount)
            color: Theme.textSecondaryColor
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontCaption
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.bottomMargin: -Theme.spacingTight
            height: 1
            color: Theme.hairline
        }
    }

    StrmGrid {
        id: filmGrid

        navigationFocusKey: "person-films"
        navigationFocusRefillActive: LibraryCtl.loading

        anchors.top: filmHeading.bottom
        anchors.topMargin: Theme.spacingValue
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom

        // Shown only while the shared controller is still scoped to this
        // person: a grid quietly full of somebody else's films is worse than
        // no grid at all.
        visible: page.scopeMine && !page.personLoading && page.haveRecord
        // Focus follows content rather than visibility.
        focus: filmGrid.count > 0 && page.haveRecord && !bioToggle.visible
        KeyNavigation.up: bioToggle.visible ? bioToggle : null

        gridModel: LibraryCtl.model
        cardVariant: "poster"
        emptyText: ""
        prefetchThreshold: 30

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

    ItemMenu { id: itemMenu }

    // ── Page states ────────────────────────────────────────────────────────
    // A half-drawn person reads as a broken page, so the whole thing waits.
    LoadingState {
        anchors.fill: parent
        shape: "details"
        active: page.personLoading
    }

    // Swallows pointer input aimed at the not-yet-drawn page underneath.
    MouseArea {
        anchors.fill: parent
        visible: page.personLoading
        acceptedButtons: Qt.AllButtons
    }

    EmptyState {
        id: personError

        anchors.fill: parent
        visible: !page.personLoading && !page.haveRecord
        focus: personError.visible
        severity: "error"
        iconName: "user"
        headline: qsTr("Couldn't load this person")
        body: qsTr("The server didn't return a record for %1.").arg(page.displayName)
        actionText: qsTr("Retry")
        actionIcon: "refresh"
        onActionTriggered: page.loadPerson()
    }

    LoadingState {
        anchors.top: filmGrid.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        shape: "grid"
        active: page.haveRecord && !page.personLoading && page.filmsLoading
    }

    EmptyState {
        anchors.top: filmGrid.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        visible: page.haveRecord && !page.personLoading && page.filmsFailed
        severity: "error"
        iconName: "info"
        headline: qsTr("Couldn't load the filmography")
        body: LibraryCtl.errorMessage
        actionText: qsTr("Retry")
        actionIcon: "refresh"
        onActionTriggered: LibraryCtl.reload()
    }

    EmptyState {
        anchors.top: filmGrid.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        visible: page.haveRecord && !page.personLoading && page.filmsEmpty
        iconName: "library"
        headline: qsTr("Nothing on this server")
        body: qsTr("%1 isn't credited on anything in your libraries yet.")
              .arg(page.displayName)
        actionText: qsTr("Refresh")
        actionIcon: "refresh"
        onActionTriggered: LibraryCtl.reload()
    }
}
