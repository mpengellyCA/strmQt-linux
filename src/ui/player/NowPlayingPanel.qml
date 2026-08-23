// Bound: the queue delegate reaches out to this file's ids (panel, queueList),
// which is only well-defined — and only lint-clean — with bound component
// behaviour.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import StrmQt

// NowPlayingPanel — the player's audio presentation (ARCHITECTURE.md).
//
// PlayerPage is built around a picture: a black frame, chrome that fades away,
// a cursor that disappears with it. Every one of those is wrong for music.
// There is no picture to uncover, so hiding the only thing on screen is pure
// loss; the useful content is the artwork, what is playing, and what is next.
//
// So this is not an overlay on a video plane, it is a *page*: an opaque surface
// that replaces the video surface entirely while an audio item is playing, and
// that never auto-hides. The OSD (PlayerOsd) is stood down for the duration —
// see PlayerPage — because a scrubber and a transport that fade out three
// seconds after the last keypress would leave a black rectangle and nothing
// else.
//
// ── Layout ────────────────────────────────────────────────────────────────
// Two panes, responsive, no video-mode code path anywhere near them:
//
//   · hero  — square cover art filling whatever height is left over, then the
//             track, artist and album, the scrubber, timecode, transport,
//             shuffle/repeat and volume.
//   · queue — what is coming next, as a plain list. NOT a floating panel: on
//             an audio page there is nothing to cover, so the queue earns its
//             place on screen rather than hiding behind a button.
//
// Side by side when the window is wide enough; stacked otherwise, and the
// queue keeps its place either way.
//
// ── Hover ≠ focus (ARCHITECTURE.md) ────────────────────────────────────────
// Nothing here calls forceActiveFocus() from a HoverHandler. The queue rows
// take focus on a *tap* (a deliberate act) and never on hover; the row's
// pointer state and its keyboard state are independent booleans, exactly as
// every rail and grid in the app already does it.
FocusScope {
    id: panel

    // ── Now-playing data ────────────────────────────────────────────────────
    // The queue is the only place the current item's metadata lives: the
    // controller publishes `title` and nothing else visual. Reading it through
    // `currentIndex` (a notifying property) rather than currentItem() is what
    // makes this a live binding instead of a snapshot — the same route
    // MiniPlayer takes, deliberately, rather than inventing a second one.
    readonly property var queueModel: {
        const q = PlayerCtl.queue;
        return (q !== undefined && q !== null) ? q : null;
    }

    readonly property var nowItem: {
        const q = panel.queueModel;
        if (q === null || q.currentIndex < 0 || q.count <= 0)
            return ({});
        return q.itemAt(q.currentIndex);
    }

    // Album art is square, so the poster is the right image — `thumbUrl` is a
    // 16:9 crop and only exists for things that have one at all.
    readonly property string artUrl: {
        const item = panel.nowItem;
        if (item.posterUrl !== undefined && String(item.posterUrl).length > 0)
            return String(item.posterUrl);
        if (item.thumbUrl !== undefined && String(item.thumbUrl).length > 0)
            return String(item.thumbUrl);
        return "";
    }

    // `name` is the bare track name; PlayerCtl.title is MediaItemModel's label,
    // which for a track is the same string. The queue entry wins because the
    // bare-playItem path seeds the queue from the title anyway, so there is no
    // case where the fallback is worse.
    readonly property string trackTitle: {
        const item = panel.nowItem;
        if (item.name !== undefined && String(item.name).length > 0)
            return String(item.name);
        return PlayerCtl.title !== undefined ? String(PlayerCtl.title) : "";
    }

    readonly property string artistText: {
        const item = panel.nowItem;
        const list = item.artists;
        if (list !== undefined && list !== null && list.length > 0)
            return Array.prototype.join.call(list, ", ");
        if (item.albumArtist !== undefined && String(item.albumArtist).length > 0)
            return String(item.albumArtist);
        return "";
    }

    readonly property string albumText: {
        const item = panel.nowItem;
        return (item.album !== undefined) ? String(item.album) : "";
    }

    readonly property real positionMs: Number(PlayerCtl.positionMs)
    readonly property real durationMs: Number(PlayerCtl.durationMs)
    readonly property bool seekable: panel.durationMs > 0

    // bufferedMs is measured ahead of the playhead; StrmSlider wants an
    // absolute value on the same axis as `value`.
    readonly property real bufferedPosition: {
        const b = PlayerCtl.backend;
        if (b === undefined || b === null || b.bufferedMs === undefined)
            return 0;
        return Number(b.bufferedMs) + panel.positionMs;
    }

    readonly property int repeatMode: panel.queueModel !== null
                                      ? Number(panel.queueModel.repeatMode) : 0
    readonly property bool shuffled: panel.queueModel !== null
                                     && panel.queueModel.shuffled === true

    readonly property string volumeIcon: (PlayerCtl.muted === true || PlayerCtl.volume <= 0)
                                         ? "volume-mute"
                                         : PlayerCtl.volume < 40 ? "volume-low" : "volume-high"

    // ── Helpers ─────────────────────────────────────────────────────────────
    function formatTime(ms: real): string {
        const totalSeconds = Math.max(0, Math.floor(ms / 1000));
        const hours = Math.floor(totalSeconds / 3600);
        const minutes = Math.floor((totalSeconds / 60) % 60);
        const seconds = totalSeconds % 60;
        const pad = v => (v < 10 ? "0" : "") + v;
        return hours > 0 ? hours + ":" + pad(minutes) + ":" + pad(seconds)
                         : minutes + ":" + pad(seconds);
    }

    readonly property string endsAt: {
        if (panel.durationMs <= 0)
            return "";
        const remaining = Math.max(0, panel.durationMs - panel.positionMs);
        return Qt.formatTime(new Date(Date.now() + remaining), Locale.ShortFormat);
    }

    // ── Keyboard entry points, used by the page ─────────────────────────────
    // Nothing in this file calls them by itself: focus enters on a click or
    // because the page handed it over, never because a pointer moved.
    function focusScrubber(): void {
        scrubber.forceActiveFocus(Qt.OtherFocusReason);
    }

    function focusTransport(): void {
        playPause.forceActiveFocus(Qt.OtherFocusReason);
    }

    function jumpTo(index: int): void {
        const q = panel.queueModel;
        if (q === null || index < 0 || index >= q.count)
            return;
        q.jumpTo(index);
    }

    function toggleShuffle(): void {
        if (panel.queueModel !== null)
            panel.queueModel.shuffled = !panel.queueModel.shuffled;
    }

    function cycleRepeat(): void {
        if (panel.queueModel !== null)
            panel.queueModel.cycleRepeatMode();
    }

    // ── Backdrop ────────────────────────────────────────────────────────────
    // Opaque, because this replaces the video surface rather than floating over
    // it. The artwork is reused at page scale and pushed right down: enough for
    // the room to take the record's colour, never enough to fight the text.
    Rectangle {
        anchors.fill: parent
        color: Theme.ground
    }

    Image {
        id: backdrop

        anchors.fill: parent
        source: panel.artUrl
        // A page-sized crop of a square image: ask for the pixels it draws, in
        // device pixels. A logical-pixel request comes back soft on any scaled
        // display (the lesson every card in this app learned the hard way).
        sourceSize.width: Math.round(Math.max(1, panel.width) * Screen.devicePixelRatio / 2)
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: true
        opacity: status === Image.Ready ? 0.14 : 0

        Behavior on opacity {
            NumberAnimation {
                duration: Theme.animSlow
                easing.type: Theme.easeStandard
            }
        }
    }

    Rectangle {
        anchors.fill: parent

        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.veil }
            GradientStop { position: 0.55; color: Theme.scrimColor }
            GradientStop { position: 1.0; color: Theme.ground }
        }
    }

    // ── Panes ───────────────────────────────────────────────────────────────
    Item {
        id: content

        anchors.fill: parent
        anchors.margins: Theme.pageMarginValue

        // The queue sits beside the artwork when there is room for both to be
        // legible, and underneath it when there is not. It is never dropped:
        // "what is next" is half the reason this page exists.
        readonly property bool sideBySide: content.width >= Theme.scale(900)
        readonly property int gap: Theme.spacingLoose
        // The queue takes a share, clamped: below the floor the track names are
        // all ellipsis, and above the ceiling it is a column of whitespace.
        readonly property int queueWidth: Math.round(
            Math.max(Theme.scale(340),
                     Math.min(Theme.scale(520), content.width * 0.34)))
        readonly property int heroWidth: content.sideBySide
            ? Math.max(0, content.width - content.gap - content.queueWidth)
            : content.width

        Item {
            id: heroPane

            x: 0
            y: 0
            width: content.heroWidth
            height: content.sideBySide ? content.height
                                       : Math.round(content.height * 0.58)

            // Laid out from the bottom up so the artwork can take every pixel
            // the controls do not need. Sizing the art from the *pane* rather
            // than from this column is what keeps that out of a binding loop.
            Column {
                id: heroControls

                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: Theme.spacingTight

                Text {
                    id: titleLabel

                    width: parent.width
                    text: panel.trackTitle
                    color: Theme.textPrimaryColor
                    font.family: Theme.fontDisplay
                    font.pixelSize: Theme.fontHeading
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                Text {
                    id: artistLabel

                    width: parent.width
                    visible: artistLabel.text.length > 0
                    text: panel.artistText
                    color: Theme.textPrimaryColor
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontBodyLarge
                    elide: Text.ElideRight
                }

                Text {
                    id: albumLabel

                    width: parent.width
                    visible: albumLabel.text.length > 0
                    text: panel.albumText
                    color: Theme.textSecondaryColor
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontBodySize
                    elide: Text.ElideRight
                    bottomPadding: Theme.spacingTight
                }

                StrmSlider {
                    id: scrubber

                    width: parent.width
                    from: 0
                    to: Math.max(1, panel.durationMs)
                    value: panel.positionMs
                    buffered: panel.bufferedPosition
                    // The knob stays out on an audio page: this is the primary
                    // control on screen, not a thing revealed by a hover.
                    showKnobOnHoverOnly: false
                    stepSize: 10000
                    enabled: panel.seekable

                    KeyNavigation.down: playPause
                    KeyNavigation.up: null

                    // Live readout while dragging; the seek itself waits for the
                    // release, so one scrub is one demuxer seek.
                    onMoved: value => positionLabel.scrubMs = value
                    onCommitted: value => {
                        PlayerCtl.seekTo(Math.round(value));
                        positionLabel.scrubMs = -1;
                    }
                }

                // Mono everywhere: tabular figures must not reflow as they tick
                // (ARCHITECTURE.md).
                Item {
                    id: timesRow

                    width: parent.width
                    height: positionLabel.implicitHeight

                    Text {
                        id: positionLabel

                        // >= 0 while a drag is in progress: the readout follows
                        // the pointer even though the player has not moved yet.
                        property real scrubMs: -1

                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: (PlayerCtl.paused === true ? qsTr("Paused") + "  ·  " : "")
                              + panel.formatTime(positionLabel.scrubMs >= 0
                                                 ? positionLabel.scrubMs : panel.positionMs)
                              + "  /  " + panel.formatTime(panel.durationMs)
                        color: Theme.textPrimaryColor
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontSmall
                    }

                    Text {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        visible: panel.endsAt.length > 0
                        text: qsTr("Ends at %1").arg(panel.endsAt)
                        color: Theme.textSecondaryColor
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontSmall
                    }
                }

                // ── Transport ───────────────────────────────────────────────
                // A control that cannot act is DISABLED, never hidden: a button
                // that vanishes moves every other button under the cursor.
                Item {
                    id: transportRow

                    width: parent.width
                    height: Theme.touchTarget + Theme.spacingTight
                    // Not a focus scope: the buttons below are the tab stops.

                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.spacingTight

                        StrmIconButton {
                            id: shuffleButton

                            anchors.verticalCenter: parent.verticalCenter
                            iconName: "shuffle"
                            round: true
                            tooltip: panel.shuffled ? qsTr("Shuffle on") : qsTr("Shuffle off")
                            checked: panel.shuffled
                            enabled: panel.queueModel !== null
                            onClicked: panel.toggleShuffle()

                            KeyNavigation.right: previousButton
                            KeyNavigation.up: scrubber
                            KeyNavigation.down: muteButton
                        }

                        StrmIconButton {
                            id: previousButton

                            anchors.verticalCenter: parent.verticalCenter
                            iconName: "skip-previous"
                            tooltip: qsTr("Previous")
                            // Live whenever it can do something: with no earlier
                            // entry it still restarts the track, which is the
                            // universal convention.
                            enabled: PlayerCtl.hasPrevious === true || panel.positionMs > 5000
                            onClicked: PlayerCtl.playPrevious()

                            KeyNavigation.left: shuffleButton
                            KeyNavigation.right: playPause
                            KeyNavigation.up: scrubber
                            KeyNavigation.down: muteButton
                        }

                        StrmIconButton {
                            id: playPause

                            anchors.verticalCenter: parent.verticalCenter
                            round: true
                            size: Theme.controlHeightLarge
                            checked: true
                            iconName: PlayerCtl.paused === true ? "play" : "pause"
                            tooltip: PlayerCtl.paused === true ? qsTr("Play") : qsTr("Pause")
                            onClicked: PlayerCtl.togglePause()

                            KeyNavigation.left: previousButton
                            KeyNavigation.right: nextButton
                            KeyNavigation.up: scrubber
                            KeyNavigation.down: muteButton
                        }

                        StrmIconButton {
                            id: nextButton

                            anchors.verticalCenter: parent.verticalCenter
                            iconName: "skip-next"
                            tooltip: qsTr("Next")
                            enabled: PlayerCtl.hasNext === true
                            onClicked: PlayerCtl.playNext()

                            KeyNavigation.left: playPause
                            KeyNavigation.right: repeatButton
                            KeyNavigation.up: scrubber
                            KeyNavigation.down: muteButton
                        }

                        StrmIconButton {
                            id: repeatButton

                            anchors.verticalCenter: parent.verticalCenter
                            iconName: panel.repeatMode === 2 ? "repeat-one" : "repeat"
                            round: true
                            tooltip: panel.repeatMode === 0 ? qsTr("Repeat off")
                                   : panel.repeatMode === 1 ? qsTr("Repeat all")
                                   : qsTr("Repeat one")
                            checked: panel.repeatMode !== 0
                            enabled: panel.queueModel !== null
                            onClicked: panel.cycleRepeat()

                            KeyNavigation.left: nextButton
                            KeyNavigation.up: scrubber
                            KeyNavigation.down: muteButton
                        }
                    }
                }

                // ── Volume and stop ─────────────────────────────────────────
                Item {
                    id: secondaryRow

                    width: parent.width
                    height: Theme.touchTarget

                    Row {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.spacingTight

                        StrmIconButton {
                            id: muteButton

                            anchors.verticalCenter: parent.verticalCenter
                            iconName: panel.volumeIcon
                            tooltip: PlayerCtl.muted === true ? qsTr("Unmute") : qsTr("Mute")
                            checked: PlayerCtl.muted === true
                            onClicked: PlayerCtl.toggleMute()

                            KeyNavigation.right: stopButton
                            KeyNavigation.up: playPause
                        }

                        // Deliberately OUT of the KeyNavigation chain: StrmSlider
                        // consumes Left/Right for its own value, so leaving it in
                        // would trap arrow navigation halfway along the row. The
                        // keyboard adjusts volume through the page's InputMap
                        // bindings instead.
                        StrmSlider {
                            id: volumeSlider

                            anchors.verticalCenter: parent.verticalCenter
                            width: Theme.scale(140)
                            activeFocusOnTab: false
                            showKnobOnHoverOnly: false
                            from: 0
                            to: PlayerCtl.maxVolume
                            stepSize: 5
                            value: PlayerCtl.muted === true ? 0 : PlayerCtl.volume

                            onMoved: value => {
                                PlayerCtl.setMuted(false);
                                PlayerCtl.setVolume(Math.round(value));
                            }
                            onCommitted: value => PlayerCtl.setVolume(Math.round(value))
                        }
                    }

                    StrmIconButton {
                        id: stopButton

                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        iconName: "stop"
                        tooltip: qsTr("Stop")
                        onClicked: PlayerCtl.stop()

                        KeyNavigation.left: muteButton
                        KeyNavigation.right: queueList
                        KeyNavigation.up: repeatButton
                        KeyNavigation.down: queueList
                    }
                }
            }

            // ── Cover art ───────────────────────────────────────────────────
            // Whatever height the controls left over, squared off and centred.
            Item {
                id: artArea

                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: heroControls.top
                anchors.bottomMargin: Theme.spacingLoose

                Rectangle {
                    id: artFrame

                    anchors.centerIn: parent
                    width: Math.max(Theme.scale(72),
                                    Math.min(artArea.width, artArea.height))
                    height: artFrame.width
                    radius: Theme.radiusPanel
                    color: Theme.surfaceColor
                    border.width: 1
                    border.color: Theme.hairline
                    clip: true

                    Image {
                        anchors.fill: parent
                        source: panel.artUrl
                        sourceSize.width: Math.round(artFrame.width * Screen.devicePixelRatio)
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

                    // A record with no cover is still a record: the placeholder
                    // is the library's own music glyph, not an empty hole.
                    StrmIcon {
                        anchors.centerIn: parent
                        visible: panel.artUrl.length === 0
                        name: "lib-music"
                        size: Math.round(artFrame.width * 0.28)
                        color: Theme.textTertiary
                    }
                }
            }
        }

        // ── Up next ─────────────────────────────────────────────────────────
        Item {
            id: queuePane

            x: content.sideBySide ? content.heroWidth + content.gap : 0
            y: content.sideBySide ? 0 : heroPane.height + content.gap
            width: content.sideBySide ? Math.max(0, content.width - content.heroWidth - content.gap)
                                      : content.width
            height: content.sideBySide ? content.height
                                       : Math.max(0, content.height - heroPane.height - content.gap)

            Item {
                id: queueHeader

                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: Theme.controlHeight

                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Up next")
                    color: Theme.textPrimaryColor
                    font.family: Theme.fontDisplay
                    font.pixelSize: Theme.fontTitle
                    font.weight: Font.DemiBold
                }

                Text {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    visible: queueList.count > 0
                    text: qsTr("%n track(s)", "", queueList.count)
                    color: Theme.textTertiary
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontCaption
                }
            }

            Rectangle {
                id: queueRule

                anchors.top: queueHeader.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: Theme.hairline
            }

            ListView {
                id: queueList

                anchors.top: queueRule.bottom
                anchors.topMargin: Theme.spacingTight
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                clip: true
                spacing: Theme.scale(2)
                model: panel.queueModel
                keyNavigationWraps: false
                highlightMoveDuration: Theme.animFastMs
                // Never `focus: true`: this list is on screen from the first
                // frame, and a list that grabbed the keyboard on arrival would
                // take the page's arrow keys away before the user asked.
                activeFocusOnTab: true

                ScrollBar.vertical: StrmScrollBar {}

                Keys.onReturnPressed: event => {
                    if (!event.isAutoRepeat)
                        panel.jumpTo(queueList.currentIndex);
                    event.accepted = true;
                }
                Keys.onEnterPressed: event => {
                    if (!event.isAutoRepeat)
                        panel.jumpTo(queueList.currentIndex);
                    event.accepted = true;
                }
                Keys.onLeftPressed: event => {
                    playPause.forceActiveFocus(Qt.BacktabFocusReason);
                    event.accepted = true;
                }
                Keys.onUpPressed: event => {
                    if (queueList.currentIndex <= 0) {
                        stopButton.forceActiveFocus(Qt.BacktabFocusReason);
                        event.accepted = true;
                    } else {
                        event.accepted = false;
                    }
                }

                // Follows the player: the row that starts playing scrolls itself
                // into view, which is the whole point of a visible queue.
                Connections {
                    target: panel.queueModel

                    function onCurrentChanged(): void {
                        const q = panel.queueModel;
                        if (q === null)
                            return;
                        const row = q.currentIndex;
                        if (row < 0)
                            return;
                        if (!queueList.activeFocus)
                            queueList.currentIndex = row;
                        queueList.positionViewAtIndex(row, ListView.Contain);
                    }
                }

                Component.onCompleted: {
                    if (panel.queueModel !== null)
                        queueList.currentIndex = Math.max(0, panel.queueModel.currentIndex);
                }

                Text {
                    anchors.centerIn: parent
                    width: parent.width - Theme.spacingValue
                    visible: queueList.count === 0
                    text: qsTr("Nothing queued after this.")
                    color: Theme.textTertiary
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                }

                delegate: Item {
                    id: trackRow

                    required property int index
                    required property var model

                    readonly property bool hovered: rowHover.hovered
                    // Focus and hover are independent, and the ring wins
                    // visually when both are true (ARCHITECTURE.md).
                    readonly property bool highlighted: trackRow.ListView.isCurrentItem
                                                        && queueList.activeFocus
                    readonly property bool playing: trackRow.model.isCurrent === true

                    width: ListView.view.width
                    height: Theme.scale(56)

                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radiusChip
                        color: trackRow.highlighted ? Theme.surfaceRaisedColor
                             : trackRow.hovered ? Theme.hoverTint
                             : "transparent"

                        Behavior on color {
                            ColorAnimation {
                                duration: trackRow.highlighted ? Theme.animFastMs
                                                               : Theme.animInstant
                                easing.type: trackRow.highlighted ? Theme.easeStandard
                                                                  : Theme.easeInstant
                            }
                        }
                    }

                    FocusRing {
                        active: trackRow.highlighted
                        radius: Theme.radiusChip
                        inset: 1
                    }

                    Rectangle {
                        id: art

                        anchors.left: parent.left
                        anchors.leftMargin: Theme.spacingTight
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.scale(40)
                        height: art.width
                        radius: Theme.radiusChip
                        color: Theme.surfaceColor
                        clip: true

                        Image {
                            anchors.fill: parent
                            source: trackRow.model.posterUrl !== undefined
                                    ? String(trackRow.model.posterUrl) : ""
                            sourceSize.width: Math.round(art.width * Screen.devicePixelRatio)
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            cache: true
                            visible: status === Image.Ready
                        }

                        // The playing row says so on the artwork rather than
                        // spending a column on a marker every other row leaves
                        // blank.
                        Rectangle {
                            anchors.fill: parent
                            visible: trackRow.playing
                            color: Theme.scrimColor

                            StrmIcon {
                                anchors.centerIn: parent
                                name: PlayerCtl.paused === true ? "play" : "pause"
                                size: Theme.scale(16)
                                color: Theme.accentColor
                            }
                        }
                    }

                    Column {
                        anchors.left: art.right
                        anchors.leftMargin: Theme.spacingTight
                        anchors.right: durationLabel.left
                        anchors.rightMargin: Theme.spacingTight
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.scale(2)

                        Text {
                            width: parent.width
                            text: trackRow.model.name !== undefined
                                  ? String(trackRow.model.name) : ""
                            color: trackRow.playing ? Theme.accentColor
                                 : (trackRow.hovered || trackRow.highlighted)
                                   ? Theme.textPrimaryColor
                                   : Qt.darker(Theme.textPrimaryColor, 1.06)
                            font.family: Theme.fontBody
                            font.pixelSize: Theme.fontSmall
                            font.weight: trackRow.playing ? Font.DemiBold : Font.Normal
                            elide: Text.ElideRight

                            Behavior on color {
                                ColorAnimation {
                                    duration: Theme.animInstant
                                    easing.type: Theme.easeInstant
                                }
                            }
                        }

                        Text {
                            id: rowSubtitle

                            width: parent.width
                            visible: rowSubtitle.text.length > 0
                            text: {
                                const list = trackRow.model.artists;
                                if (list !== undefined && list !== null && list.length > 0)
                                    return Array.prototype.join.call(list, ", ");
                                if (trackRow.model.albumArtist !== undefined)
                                    return String(trackRow.model.albumArtist);
                                return "";
                            }
                            color: Theme.textSecondaryColor
                            font.family: Theme.fontBody
                            font.pixelSize: Theme.fontCaption
                            elide: Text.ElideRight
                        }
                    }

                    Text {
                        id: durationLabel

                        anchors.right: parent.right
                        anchors.rightMargin: Theme.spacingTight
                        anchors.verticalCenter: parent.verticalCenter
                        visible: durationLabel.text.length > 0
                        text: {
                            const ms = Number(trackRow.model.runtimeMs);
                            return (isNaN(ms) || ms <= 0) ? "" : panel.formatTime(ms);
                        }
                        color: Theme.textTertiary
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontCaption
                    }

                    HoverHandler {
                        id: rowHover
                        // Hover only. It never calls forceActiveFocus().
                        cursorShape: Qt.PointingHandCursor
                    }

                    TapHandler {
                        gesturePolicy: TapHandler.ReleaseWithinBounds
                        onTapped: {
                            Input.noteInput("mouse");
                            queueList.currentIndex = trackRow.index;
                            queueList.forceActiveFocus(Qt.MouseFocusReason);
                            panel.jumpTo(trackRow.index);
                        }
                    }
                }
            }
        }
    }
}
