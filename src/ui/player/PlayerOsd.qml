// Bound: the inline panel Components and the scrubber's preview reach out to
// this file's ids (osd, scrubber), which is only well-defined — and only
// lint-clean — with bound component behaviour.
pragma ComponentBehavior: Bound

import QtQuick
import StrmQt

// PlayerOsd — the player's whole control surface (ARCHITECTURE.md).
//
// What this replaces: a title, a stream-method string, a non-interactive
// progress bar and two timestamps. What it is now: a draggable scrubber with a
// buffered range, chapter ticks and a hover preview; a transport row; volume;
// real track, chapter and queue panels; a settings sheet; and a stats overlay.
//
// Auto-hide is preserved exactly as the page had it: 3 s of idle hides the
// chrome, it stays up while paused, and any input wakes it. The two additions
// are that an open panel holds it up (a list that vanished mid-scroll would be
// unusable) and that the page hides the cursor with it (ARCHITECTURE.md).
//
// Division of labour: this file owns OSD state — visibility, which panel is
// open, where focus goes when one closes. Transport verbs go straight to
// PlayerCtl, because they are the player's state and not the OSD's. The one
// thing the OSD cannot own is the window, so fullscreen is a signal to the page.
Item {
    id: osd

    // ── Contract ────────────────────────────────────────────────────────────
    // "" | "tracks" | "chapters" | "queue" | "settings"
    property string panelKey: ""
    // Which page of the open panel is showing. The OSD owns it rather than the
    // panel because the button row lights from the same value — the Subtitles
    // button has to mean "the subtitle list is the one on screen", and a second
    // copy of that state inside the panel would drift the moment either side
    // moved alone. Only the tracks panel has more than one page.
    property int panelTab: 0
    property bool statsVisible: false
    // Rendered state of the window, supplied by the page.
    property bool fullscreen: false

    signal fullscreenRequested
    // The pointer's way off the player, mirroring Esc/Backspace. Until this
    // existed the only exits were keys with nothing on screen naming them.
    signal leaveRequested

    // Auto-hide. `requested` is what the timer and toggleOsd() move; everything
    // else reads `shown`.
    property bool requested: true
    readonly property bool shown: osd.requested
    // Where focus goes when the open panel closes.
    property Item panelOrigin: null

    // ── Derived player state ────────────────────────────────────────────────
    // Every one of these guards `undefined`: three agents are extending
    // PlayerController in parallel with this file, and a binding that throws
    // takes the whole OSD down with it. A missing property reads as "the
    // feature is not available", which is exactly what a disabled control means.
    readonly property var backend: PlayerCtl.backend
    readonly property bool compactGeometry: osd.width < Theme.scale(800)
                                            || osd.height < Theme.scale(520)
    readonly property int safeMargin: osd.compactGeometry ? Theme.spacingValue
                                                          : Theme.pageMarginValue

    readonly property var chapters: {
        const list = PlayerCtl.chapters;
        return (list !== undefined && list !== null) ? list : [];
    }
    readonly property bool hasChapters: osd.chapters.length > 0
    readonly property int currentChapter: PlayerCtl.currentChapter !== undefined
                                          ? Number(PlayerCtl.currentChapter) : -1

    readonly property var chapterMarkers: {
        const out = [];
        for (let i = 0; i < osd.chapters.length; ++i) {
            const ms = Number(osd.chapters[i].startMs);
            if (!isNaN(ms) && ms > 0)
                out.push(ms);
        }
        return out;
    }

    readonly property real bufferedPosition: Number(PlayerCtl.bufferedEndMs)

    // Structured queue roles, never punctuation parsed back out of a rendered
    // label. MiniPlayer and the audio page consume this same presentation.
    readonly property var titleParts: ({ "title": NowPlayingInfo.title,
                                         "subline": NowPlayingInfo.videoContext })

    // Technical readouts, mono, as chips. Same information the old OSD put in
    // one grey line at the top right.
    readonly property var techChips: NowPlayingInfo.videoTechChips

    // The cheap integer expression may be checked once a second, but the Date
    // allocation and formatting below only wake when the minute printed on
    // screen actually changes (including after a seek).
    readonly property real endsAtEpochMinute: PlayerCtl.durationMs > 0
        ? Math.floor((Date.now() + Math.max(0, PlayerCtl.durationMs
                                            - PlayerCtl.positionSeconds * 1000)) / 60000)
        : -1
    readonly property string endsAt: osd.endsAtEpochMinute >= 0
        ? Qt.formatTime(new Date(osd.endsAtEpochMinute * 60000), Locale.ShortFormat) : ""

    // ── API used by the page ────────────────────────────────────────────────
    function wake(): void {
        osd.requested = true;
        hideTimer.restart();
    }

    function toggleOsd(): void {
        osd.requested = !osd.requested;
        if (osd.requested)
            hideTimer.restart();
    }

    function openPanel(key: string, origin: Item, tab: int): void {
        osd.panelOrigin = origin;
        osd.panelTab = tab;
        osd.panelKey = key;
        osd.wake();
    }

    // The tab is part of the identity of what is open: Subtitles pressed while
    // the Audio list is up switches pages rather than closing the panel, which
    // is what the two buttons over one panel have to mean.
    function togglePanel(key: string, origin: Item, tab: int): void {
        if (osd.panelKey === key && osd.panelTab === tab)
            osd.closePanel();
        else
            osd.openPanel(key, origin, tab);
    }

    // True when something was actually closed — the page uses that to decide
    // whether Esc closes a panel or stops playback.
    function closePanel(): bool {
        if (osd.panelKey.length === 0)
            return false;
        osd.panelKey = "";
        const origin = osd.panelOrigin;
        osd.panelOrigin = null;
        if (origin !== null && origin.enabled && origin.visible)
            origin.forceActiveFocus(Qt.OtherFocusReason);
        osd.wake();
        return true;
    }

    // Esc order: the stats overlay is dismissed after any panel, because it is
    // the least modal thing on screen.
    function closeTopmost(): bool {
        if (osd.closePanel())
            return true;
        if (osd.statsVisible) {
            osd.statsVisible = false;
            return true;
        }
        return false;
    }

    function toggleStats(): void {
        osd.statsVisible = !osd.statsVisible;
        osd.wake();
    }

    // Track/chapter/source state belongs to one item. The queue panel is the
    // exception: it describes the playback session and may deliberately stay
    // open while its cursor advances.
    function resetItemState(keepQueue: bool): void {
        if (!keepQueue || osd.panelKey !== "queue")
            osd.panelKey = "";
        osd.panelTab = 0;
        osd.panelOrigin = null;
        osd.statsVisible = false;
        osd.requested = true;
        hideTimer.restart();
    }

    // Keyboard/gamepad entry point into the controls.
    //
    // wake() first, then focus in the same call, and that ordering is load
    // bearing: a DISABLED item declines focus outright (measured on 6.11 —
    // an invisible one accepts it, a disabled one does not), and `chrome` is
    // disabled for as long as the OSD is down. `enabled` follows `shown`
    // directly rather than the animated opacity, so the wake above has already
    // re-enabled the chrome by the time the line below runs; there is nothing
    // to wait for and the already-visible case costs nothing.
    //
    // The scrubber has a second `enabled` of its own — a stream with no
    // duration cannot be seeked — and Down would otherwise be a dead key on a
    // live stream, since the page accepts the event either way. Fall through to
    // the transport, which is always live.
    function focusScrubber(): void {
        osd.wake();
        if (scrubber.enabled)
            scrubber.forceActiveFocus(Qt.OtherFocusReason);
        else
            osd.focusControls();
    }

    function focusControls(): void {
        osd.wake();
        buttons.focusPrimary();
    }

    function showToast(message: string): void {
        toast.text = message;
        toast.opacity = 1;
        toastTimer.restart();
    }

    // ── Helpers ─────────────────────────────────────────────────────────────
    function formatTime(ms: real): string {
        return NowPlayingInfo.formatTime(ms);
    }

    function chapterIndexAt(ms: real): int {
        let found = -1;
        for (let i = 0; i < osd.chapters.length; ++i) {
            if (Number(osd.chapters[i].startMs) <= ms)
                found = i;
            else
                break;
        }
        return found;
    }

    function chapterNameAt(ms: real): string {
        const index = osd.chapterIndexAt(ms);
        return osd.chapterName(index);
    }

    function chapterName(index: int): string {
        if (index < 0)
            return "";
        const name = osd.chapters[index].name;
        return (name !== undefined && String(name).length > 0)
                ? String(name) : qsTr("Chapter %1").arg(index + 1);
    }

    // Hiding never happens while paused (that is today's behaviour) nor while a
    // panel is open.
    Timer {
        id: hideTimer
        interval: 3000
        running: true
        onTriggered: {
            if (!PlayerCtl.paused && osd.panelKey.length === 0)
                osd.requested = false;
        }
    }

    // ── Chrome ──────────────────────────────────────────────────────────────
    // One fading container so nothing inside has to animate itself, and
    // `enabled` follows opacity so a hidden OSD cannot be clicked or tabbed to.
    Item {
        id: chrome

        anchors.fill: parent
        opacity: osd.shown ? 1 : 0
        enabled: osd.shown
        visible: chrome.opacity > 0.01

        Behavior on opacity {
            NumberAnimation {
                duration: Theme.animNormalMs
                easing.type: Theme.easeStandard
            }
        }

        // ── Top: title, subline, technical chips ────────────────────────────
        Rectangle {
            id: topBar

            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: Math.max(topBlock.implicitHeight,
                             leaveButton.height + Theme.spacingValue)
                    + osd.safeMargin

            gradient: Gradient {
                GradientStop { position: 0.0; color: Theme.scrimColor }
                GradientStop { position: 1.0; color: "transparent" }
            }

            // Back, where every full-screen surface puts it. Pointer-only on
            // purpose: the keyboard already has Esc and Backspace, and a tab
            // stop here would sit in front of the transport row that the OSD's
            // Down-arrow contract hands the keyboard to.
            StrmIconButton {
                id: leaveButton

                anchors.top: parent.top
                anchors.left: parent.left
                anchors.topMargin: Theme.spacingValue
                anchors.leftMargin: osd.safeMargin
                iconName: "arrow-left"
                tooltip: qsTr("Back")
                accessibleName: qsTr("Leave the player, keep playing")
                activeFocusOnTab: false
                onClicked: osd.leaveRequested()
            }

            Column {
                id: topBlock

                anchors.top: parent.top
                anchors.left: leaveButton.right
                anchors.right: parent.right
                anchors.topMargin: Theme.spacingValue
                anchors.leftMargin: Theme.spacingValue
                anchors.rightMargin: osd.safeMargin
                spacing: Theme.scale(4)

                Text {
                    width: parent.width
                    text: osd.titleParts.title
                    color: Theme.textPrimaryColor
                    font.family: Theme.fontDisplay
                    font.pixelSize: Theme.fontHeading
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                Text {
                    width: parent.width
                    visible: osd.titleParts.subline.length > 0
                    text: osd.titleParts.subline
                    color: Theme.textSecondaryColor
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontBodySize
                    elide: Text.ElideRight
                }

                // Mono chips: booth-gear data, tabular and non-reflowing.
                Row {
                    visible: !osd.compactGeometry
                    spacing: Theme.spacingTight
                    topPadding: Theme.scale(4)

                    Repeater {
                        model: osd.techChips

                        delegate: Rectangle {
                            required property string modelData

                            width: chipLabel.implicitWidth + Theme.spacingValue
                            height: Theme.scale(24)
                            radius: Theme.radiusChip
                            color: Theme.surfaceColor
                            border.width: 1
                            border.color: Theme.hairline

                            Text {
                                id: chipLabel

                                anchors.centerIn: parent
                                text: parent.modelData
                                color: Theme.textSecondaryColor
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.fontCaption
                            }
                        }
                    }
                }
            }
        }

        // ── Bottom: scrubber, times, transport ──────────────────────────────
        Rectangle {
            id: bottomBar

            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: bottomBlock.implicitHeight + osd.safeMargin

            gradient: Gradient {
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 1.0; color: Theme.scrimColor }
            }

            Column {
                id: bottomBlock

                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottomMargin: Theme.spacingValue
                anchors.leftMargin: osd.safeMargin
                anchors.rightMargin: osd.safeMargin
                spacing: Theme.spacingTight

                // The scrubber. Seeks commit on release, never per motion event.
                StrmSlider {
                    id: scrubber

                    width: parent.width
                    from: 0
                    to: Math.max(1, PlayerCtl.durationMs)
                    value: PlayerCtl.positionMs
                    buffered: osd.bufferedPosition
                    markers: osd.chapterMarkers
                    // Matches the ±10 s seek convention rather than the control's
                    // default twentieth-of-the-media step.
                    stepSize: 10000
                    enabled: PlayerCtl.durationMs > 0
                    previewComponent: scrubPreview
                    accessibleName: qsTr("Playback position")
                    accessibleDescription: qsTr("%1 of %2")
                                           .arg(osd.formatTime(value))
                                           .arg(osd.formatTime(to))

                    KeyNavigation.down: buttons
                    KeyNavigation.up: null

                    // Live readout while dragging; the seek itself waits for the
                    // release, which is what keeps one scrub from becoming
                    // eight demuxer seeks.
                    onMoved: value => {
                        osd.wake();
                        positionLabel.scrubMs = value;
                    }
                    onCommitted: value => {
                        PlayerCtl.seekTo(Math.round(value));
                        positionLabel.scrubMs = -1;
                        osd.wake();
                    }
                }

                // Times. Mono everywhere: tabular figures must not reflow as
                // they tick (ARCHITECTURE.md).
                Item {
                    width: parent.width
                    height: positionLabel.implicitHeight

                    Text {
                        id: positionLabel

                        // >= 0 while a drag is in progress: the readout follows
                        // the pointer even though the player has not moved yet.
                        property real scrubMs: -1

                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        // A word rather than a glyph: the pause pictographs
                        // are not in IBM Plex Mono, and a tofu box in the
                        // timecode is worse than four extra characters.
                        text: (PlayerCtl.paused ? qsTr("Paused") + "  ·  " : "")
                              + osd.formatTime(positionLabel.scrubMs >= 0 ? positionLabel.scrubMs
                                                                          : PlayerCtl.positionSeconds * 1000)
                              + "  /  " + osd.formatTime(PlayerCtl.durationMs)
                        color: Theme.textPrimaryColor
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontSmall
                    }

                    Text {
                        id: chapterHere

                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.verticalCenter: parent.verticalCenter
                        visible: chapterHere.text.length > 0
                        text: osd.chapterName(osd.currentChapter)
                        color: Theme.textSecondaryColor
                        font.family: Theme.fontBody
                        font.pixelSize: Theme.fontSmall
                        elide: Text.ElideRight
                        width: Math.min(implicitWidth, parent.width * 0.4)
                    }

                    Text {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        visible: osd.endsAt.length > 0
                        text: qsTr("Ends at %1").arg(osd.endsAt)
                        color: Theme.textSecondaryColor
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontSmall
                    }
                }

                OsdButtonRow {
                    id: buttons

                    width: parent.width
                    panelKey: osd.panelKey
                    panelTab: osd.panelTab
                    statsVisible: osd.statsVisible
                    fullscreen: osd.fullscreen
                    hasChapters: osd.hasChapters
                    focusAbove: scrubber

                    onWoken: osd.wake()
                    onPanelRequested: (key, origin, tab) => osd.togglePanel(key, origin, tab)
                    onStatsRequested: osd.toggleStats()
                    onFullscreenRequested: osd.fullscreenRequested()
                }
            }
        }

        // ── Panels ──────────────────────────────────────────────────────────
        // Loaded on demand: four list views that are never opened are four
        // list views not built. The Loader gives its item the size below.
        Loader {
            id: panelLoader

            anchors.right: parent.right
            anchors.rightMargin: osd.safeMargin
            anchors.bottom: bottomBar.top
            anchors.bottomMargin: Theme.spacingTight
            // The settings sheet carries labelled rows and sliders rather than a
            // list of names, so it gets more room than the three list panels.
            width: Math.min(osd.panelKey === "settings" ? Theme.scale(444) : Theme.scale(400),
                            Math.max(Theme.scale(240), osd.width - 2 * osd.safeMargin))
            height: Math.min(osd.panelKey === "settings" ? Theme.scale(560) : Theme.scale(480),
                             Math.max(Theme.scale(120), osd.height - bottomBar.height
                                                        - topBar.height - Theme.spacingTight))
            active: osd.panelKey.length > 0
            sourceComponent: osd.panelKey === "tracks" ? tracksPanel
                           : osd.panelKey === "chapters" ? chaptersPanel
                           : osd.panelKey === "queue" ? queuePanel
                           : osd.panelKey === "settings" ? settingsPanel
                           : null

            // A panel that opens without focus is a panel a gamepad cannot use.
            // Loader.item is typed QObject; the cast is what lets this call the
            // Item API instead of reaching through an untyped handle.
            onLoaded: {
                const loaded = panelLoader.item as Item;
                if (loaded !== null)
                    loaded.forceActiveFocus(Qt.OtherFocusReason);
            }

            opacity: panelLoader.active ? 1 : 0

            Behavior on opacity {
                NumberAnimation {
                    duration: Theme.animFastMs
                    easing.type: Theme.easeStandard
                }
            }
        }
    }

    // ── Surfaces that outlive the chrome ────────────────────────────────────
    // Up Next and the stats overlay deliberately sit outside `chrome`: both are
    // meant to be readable while the OSD is hidden and the film is playing.
    UpNextCard {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: osd.safeMargin
        anchors.bottomMargin: osd.shown ? bottomBar.height + Theme.spacingTight
                                        : osd.safeMargin

        Behavior on anchors.bottomMargin {
            NumberAnimation {
                duration: Theme.animNormalMs
                easing.type: Theme.easeStandard
            }
        }

        onDismissed: osd.wake()
    }

    StatsOverlay {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: osd.safeMargin
        anchors.topMargin: osd.shown ? topBar.height : osd.safeMargin
        width: Math.min(Theme.scale(440), Math.max(Theme.scale(240),
                                                  osd.width - 2 * osd.safeMargin))

        shown: osd.statsVisible

        onCloseRequested: osd.statsVisible = false
    }

    // Track-switch toast, preserved from the page it moved out of: A and C
    // still report what they landed on (ARCHITECTURE.md).
    StrmToast {
        id: toast

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Theme.scale(130)
        opacity: 0
        visible: toast.opacity > 0.01

        Behavior on opacity {
            NumberAnimation {
                duration: Theme.animNormalMs
                easing.type: Theme.easeStandard
            }
        }

        onDismissRequested: toast.opacity = 0
    }

    Timer {
        id: toastTimer
        interval: 2200
        onTriggered: toast.opacity = 0
    }

    Connections {
        target: PlayerCtl

        function onTrackChanged(description) {
            osd.showToast(description);
        }

        function onActiveChanged(): void {
            if (!PlayerCtl.active)
                osd.resetItemState(false);
        }

        function onIsAudioChanged(): void {
            // Statistics describe a video decoder and must not remain armed
            // behind the audio surface.
            osd.resetItemState(false);
        }
    }

    Connections {
        target: PlayerCtl.queue

        function onCurrentChanged(): void {
            osd.resetItemState(true);
        }
    }

    // ── Components ──────────────────────────────────────────────────────────
    Component {
        id: scrubPreview

        Rectangle {
            width: previewColumn.implicitWidth + Theme.spacingValue
            height: previewColumn.implicitHeight + Theme.spacingTight * 2
            radius: Theme.radiusChip
            color: Theme.surfaceOverlay
            border.width: 1
            border.color: Theme.hairline

            // Chapter-thumbnail hook: when the server's chapter image endpoint
            // is wired up (it is not yet), an Image sourced from
            // image://emby/<itemId>/Chapter/<index> goes here, above the two
            // labels, and this Rectangle grows to fit it.
            Column {
                id: previewColumn

                anchors.centerIn: parent
                spacing: Theme.scale(2)

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: osd.formatTime(scrubber.hoverValue)
                    color: Theme.textPrimaryColor
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSmall
                }

                Text {
                    id: previewChapter

                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: previewChapter.text.length > 0
                    text: osd.chapterNameAt(scrubber.hoverValue)
                    color: Theme.textSecondaryColor
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontCaption
                    elide: Text.ElideRight
                    width: Math.min(previewChapter.implicitWidth, Theme.scale(240))
                }
            }
        }
    }

    Component {
        id: tracksPanel

        TrackPanel {
            // The panel asks and the OSD grants, so the tab the button row is
            // lit for and the tab the list is showing are the same value rather
            // than two that agree until one of them moves.
            tab: osd.panelTab
            onTabRequested: index => osd.panelTab = index
            onCloseRequested: osd.closePanel()
        }
    }

    Component {
        id: chaptersPanel

        ChapterPanel {
            chapters: osd.chapters
            currentChapter: osd.currentChapter
            onCloseRequested: osd.closePanel()
        }
    }

    Component {
        id: queuePanel

        QueuePanel {
            onCloseRequested: osd.closePanel()
        }
    }

    // D9/D10/D11/D13 grew the settings sheet past what belonged inline here:
    // a version picker, a bitrate ladder, a playback mode, speed, two sync
    // controls and four subtitle-appearance controls. It is its own file now.
    Component {
        id: settingsPanel

        PlaybackSettingsPanel {
            onCloseRequested: osd.closePanel()
        }
    }
}
