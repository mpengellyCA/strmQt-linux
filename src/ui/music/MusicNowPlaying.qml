pragma ComponentBehavior: Bound

import QtQuick
import StrmQt

// MusicNowPlaying: the full player for a record (Crate spec §7.2, "Out of the
// sleeve"). PlayerPage shows it in audio mode in place of the video surface.
//
// Stage on the left (the record, what is playing, transport, scrubber, readout),
// MusicPlayerPanel on the right. Every value on screen comes finished from
// NowPlayingMusicCtl or PlayerCtl; this file lays them out and states intent.
//
// Pointer presses it does not claim fall through to PlayerPage's picture area,
// which is how click-to-pause and the volume wheel keep working here.
FocusScope {
    id: view

    // Back to wherever the player was opened from, playback continuing.
    signal leaveRequested

    // The shared sleeve is in the air (Main.qml's SleeveFlight).
    property bool sleeveInFlight: false
    // The player page is current and in audio mode: the record may turn.
    property bool live: true

    readonly property bool buffering: NowPlayingMusicCtl.recordState === "buffering"
    readonly property var queue: {
        const q = PlayerCtl.queue;
        return (q !== undefined && q !== null) ? q : null;
    }
    readonly property bool shuffled: view.queue !== null && view.queue.shuffled === true
    readonly property int repeatMode: view.queue !== null ? Number(view.queue.repeatMode) : 0
    readonly property bool animateRecord: typeof Prefs !== "undefined" ? Prefs.animateRecord : true
    readonly property string volumeIcon: (PlayerCtl.muted === true || PlayerCtl.volume <= 0)
                                         ? "volume-mute"
                                         : PlayerCtl.volume < 40 ? "volume-low" : "volume-high"

    // The transition's large endpoint, in `target`'s coordinates.
    function sleeveRect(target: Item): rect {
        return record.sleeveRect(target);
    }

    readonly property real sleeveRadius: record.sleeveRadius

    function focusTransport(): void {
        playPause.forceActiveFocus(Qt.TabFocusReason);
    }

    function focusScrubber(): void {
        scrubber.forceActiveFocus(Qt.TabFocusReason);
    }

    // A control that disables while it holds the keyboard (⏮ on the first
    // track once the restart window closes, ⏭ on the last, ♡ and ＋ between
    // tracks, the scrubber before a duration is known) must not strand focus
    // on the bare scope, where no ring shows. Qt clears the item's active focus
    // BEFORE `enabledChanged` fires (measured, Qt 6.11 — MiniPlayer's
    // rescueFocusFrom), so the loss of focus is where this is caught: `enabled`
    // already reads false there, and the scope still holds the keyboard.
    function rescueFocusFrom(control: Item): void {
        if (view.visible && view.enabled && view.activeFocus && !control.enabled && !control.activeFocus)
            playPause.forceActiveFocus(Qt.OtherFocusReason);
    }

    function runMenu(key: string): void {
        switch (key) {
        case "album":
            Actions.openAlbum(NowPlayingMusicCtl.albumId, NowPlayingMusicCtl.album);
            break;
        case "artist":
            Actions.openArtist(NowPlayingMusicCtl.artistId, NowPlayingMusicCtl.artist);
            break;
        case "radio":
            MusicPlay.radio(NowPlayingMusicCtl.trackId, NowPlayingMusicCtl.title);
            break;
        case "stop":
            PlayerCtl.stop(); // Main pops the page on stopped()
            break;
        default:
            break;
        }
    }

    // A credit on the stage. Pointer-only: the ⋯ menu is the keyboard's route.
    component CreditLink: Text {
        id: link

        property bool linked: true
        signal activated

        color: (linkHover.hovered && link.linked) ? Theme.textPrimaryColor : Theme.textSecondaryColor
        font.family: Theme.fontBody
        font.pixelSize: Theme.fontBodySize
        font.underline: linkHover.hovered && link.linked
        elide: Text.ElideRight

        HoverHandler {
            id: linkHover
            enabled: link.linked
            cursorShape: Qt.PointingHandCursor
        }

        TapHandler {
            enabled: link.linked
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: {
                Input.noteInput("mouse");
                link.activated();
            }
        }
    }

    // ── Ground ──────────────────────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        color: Theme.ground
    }

    CoverWash {
        anchors.fill: parent
        source: NowPlayingMusicCtl.coverUrl
    }

    Connections {
        target: NowPlayingMusicCtl

        // Emitted with coverUrl already moved and before any binding sees it.
        function onAlbumChanging() {
            record.changeAlbum(NowPlayingMusicCtl.coverUrl);
        }
    }

    // ── Stage ───────────────────────────────────────────────────────────────
    Item {
        id: stageZone

        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: panel.left

        Column {
            id: stageColumn

            // Everything in the column except the record, so the record takes
            // what height is left and never pushes the transport off screen.
            readonly property real reserved: kicker.height + titleText.height + credits.height
                                             + transport.height + scrubberRow.height + footer.height
                                             + 6 * stageColumn.spacing + 2 * Theme.spacingLoose

            anchors.centerIn: parent
            width: Math.max(0, Math.min(stageZone.width - 2 * Theme.pageMarginValue, Theme.scale(640)))
            spacing: Theme.spacingValue

            CrateKicker {
                id: kicker

                width: parent.width
                text: NowPlayingMusicCtl.sourceKicker
            }

            RecordStage {
                id: record

                sleeveSize: Math.max(Theme.scale(160),
                                     Math.min(Theme.scale(420),
                                              stageZone.height - stageColumn.reserved,
                                              stageColumn.width / 1.45))
                coverUrl: NowPlayingMusicCtl.coverUrl
                recordState: NowPlayingMusicCtl.recordState
                animate: view.animateRecord
                live: view.live
                holdIn: view.sleeveInFlight
            }

            CrateHeading {
                id: titleText

                width: parent.width
                text: NowPlayingMusicCtl.title
                pixelSize: Theme.crateHeroAlbum
            }

            Row {
                id: credits

                width: parent.width
                spacing: Theme.spacingTight

                CreditLink {
                    id: artistLink

                    text: NowPlayingMusicCtl.artist
                    linked: NowPlayingMusicCtl.artistId.length > 0
                    width: Math.min(implicitWidth, Math.round((credits.width - creditDot.width) / 2))
                    onActivated: Actions.openArtist(NowPlayingMusicCtl.artistId, NowPlayingMusicCtl.artist)
                }

                Text {
                    id: creditDot

                    visible: artistLink.text.length > 0 && albumLink.text.length > 0
                    text: "·"
                    color: Theme.textTertiary
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontBodySize
                }

                CreditLink {
                    id: albumLink

                    text: NowPlayingMusicCtl.album
                    linked: NowPlayingMusicCtl.albumId.length > 0
                    width: Math.min(implicitWidth, credits.width - artistLink.width - creditDot.width
                                                   - 2 * credits.spacing)
                    onActivated: Actions.openAlbum(NowPlayingMusicCtl.albumId, NowPlayingMusicCtl.album)
                }
            }

            // ⇄ ⏮ ⏯ ⏭ ↻ ♡ ＋ ⋯
            Row {
                id: transport

                spacing: Theme.spacingTight

                // The top row: Up has nowhere to go, and letting it through
                // would hand the keyboard to the page, which draws no ring.
                Keys.onUpPressed: event => event.accepted = true

                StrmIconButton {
                    id: shuffleButton

                    anchors.verticalCenter: parent.verticalCenter
                    size: Theme.scale(40)
                    activeFocusOnTab: false
                    iconName: "shuffle"
                    tooltip: view.shuffled ? qsTr("Shuffle on") : qsTr("Shuffle off")
                    checked: view.shuffled
                    enabled: view.queue !== null
                    onClicked: view.queue.shuffled = !view.queue.shuffled

                    onActiveFocusChanged: view.rescueFocusFrom(shuffleButton)

                    // First in its row: Left stays here rather than reaching
                    // the page, where it would seek.
                    Keys.onLeftPressed: event => event.accepted = true

                    KeyNavigation.right: prevButton
                    KeyNavigation.down: scrubber
                }

                StrmIconButton {
                    id: prevButton

                    anchors.verticalCenter: parent.verticalCenter
                    size: Theme.scale(40)
                    activeFocusOnTab: false
                    iconName: "skip-previous"
                    tooltip: qsTr("Previous")
                    // With no earlier entry it still restarts the track past
                    // five seconds, so it is live whenever it can do something.
                    enabled: PlayerCtl.hasPrevious === true || PlayerCtl.positionMs >= 5000
                    onClicked: PlayerCtl.playPrevious()

                    onActiveFocusChanged: view.rescueFocusFrom(prevButton)

                    KeyNavigation.left: shuffleButton
                    KeyNavigation.right: playPause
                    KeyNavigation.down: scrubber
                }

                StrmIconButton {
                    id: playPause

                    anchors.verticalCenter: parent.verticalCenter
                    size: Theme.scale(56)
                    round: true
                    focus: true
                    activeFocusOnTab: false
                    iconName: PlayerCtl.paused === true ? "play" : "pause"
                    tooltip: PlayerCtl.paused === true ? qsTr("Play") : qsTr("Pause")
                    onClicked: PlayerCtl.togglePause()

                    KeyNavigation.left: prevButton
                    KeyNavigation.right: nextButton
                    KeyNavigation.down: scrubber
                }

                StrmIconButton {
                    id: nextButton

                    anchors.verticalCenter: parent.verticalCenter
                    size: Theme.scale(40)
                    activeFocusOnTab: false
                    iconName: "skip-next"
                    tooltip: qsTr("Next")
                    enabled: PlayerCtl.hasNext === true
                    onClicked: PlayerCtl.playNext()

                    onActiveFocusChanged: view.rescueFocusFrom(nextButton)

                    KeyNavigation.left: playPause
                    KeyNavigation.right: repeatButton
                    KeyNavigation.down: scrubber
                }

                StrmIconButton {
                    id: repeatButton

                    anchors.verticalCenter: parent.verticalCenter
                    size: Theme.scale(40)
                    activeFocusOnTab: false
                    iconName: view.repeatMode === 2 ? "repeat-one" : "repeat"
                    tooltip: view.repeatMode === 0 ? qsTr("Repeat off")
                           : view.repeatMode === 1 ? qsTr("Repeat all")
                           : qsTr("Repeat one")
                    checked: view.repeatMode !== 0
                    enabled: view.queue !== null
                    onClicked: view.queue.cycleRepeatMode()

                    onActiveFocusChanged: view.rescueFocusFrom(repeatButton)

                    KeyNavigation.left: nextButton
                    KeyNavigation.right: favouriteButton
                    KeyNavigation.down: scrubber
                }

                StrmIconButton {
                    id: favouriteButton

                    anchors.verticalCenter: parent.verticalCenter
                    size: Theme.scale(40)
                    activeFocusOnTab: false
                    iconName: NowPlayingMusicCtl.favourite ? "heart-filled" : "heart"
                    tooltip: NowPlayingMusicCtl.favourite ? qsTr("Remove from favourites")
                                                          : qsTr("Add to favourites")
                    checked: NowPlayingMusicCtl.favourite
                    enabled: NowPlayingMusicCtl.trackId.length > 0
                    onClicked: NowPlayingMusicCtl.toggleFavourite()

                    onActiveFocusChanged: view.rescueFocusFrom(favouriteButton)

                    KeyNavigation.left: repeatButton
                    KeyNavigation.right: addButton
                    KeyNavigation.down: scrubber
                }

                StrmIconButton {
                    id: addButton

                    anchors.verticalCenter: parent.verticalCenter
                    size: Theme.scale(40)
                    activeFocusOnTab: false
                    iconName: "plus"
                    tooltip: qsTr("Add to playlist")
                    enabled: NowPlayingMusicCtl.trackId.length > 0
                    onClicked: playlistPicker.show(NowPlayingMusicCtl.title, [NowPlayingMusicCtl.trackId])

                    onActiveFocusChanged: view.rescueFocusFrom(addButton)

                    KeyNavigation.left: favouriteButton
                    KeyNavigation.right: moreButton
                    KeyNavigation.down: scrubber
                }

                StrmIconButton {
                    id: moreButton

                    anchors.verticalCenter: parent.verticalCenter
                    size: Theme.scale(40)
                    activeFocusOnTab: false
                    iconName: "more-horizontal"
                    tooltip: qsTr("More")
                    onClicked: {
                        const corner = moreButton.mapToItem(null, 0, moreButton.height);
                        moreMenu.popupAt(corner.x, corner.y);
                    }

                    KeyNavigation.left: addButton
                    KeyNavigation.down: scrubber
                    Keys.onRightPressed: event => {
                        panel.focusTabs();
                        event.accepted = true;
                    }
                }
            }

            Item {
                id: scrubberRow

                width: parent.width
                height: Theme.controlHeight

                StrmSlider {
                    id: scrubber

                    anchors.left: parent.left
                    anchors.right: timeLabel.left
                    anchors.rightMargin: Theme.spacingValue
                    anchors.verticalCenter: parent.verticalCenter
                    activeFocusOnTab: false
                    enabled: PlayerCtl.durationMs > 0
                    from: 0
                    to: Math.max(1, PlayerCtl.durationMs)
                    value: PlayerCtl.positionMs
                    buffered: PlayerCtl.bufferedEndMs
                    stepSize: 10000
                    armToScrub: true
                    accessibleName: qsTr("Playback position")
                    accessibleDescription: NowPlayingMusicCtl.timeText

                    onCommitted: v => PlayerCtl.seekTo(Math.round(v))

                    KeyNavigation.up: playPause
                    KeyNavigation.down: muteButton
                }

                // StrmSlider has an activeFocus handler of its own (the
                // disarm), so the rescue connects rather than declaring one.
                Connections {
                    target: scrubber

                    function onActiveFocusChanged() {
                        view.rescueFocusFrom(scrubber);
                    }
                }

                Text {
                    id: timeLabel

                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: NowPlayingMusicCtl.timeText
                    color: Theme.textTertiary
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontCaption
                    font.features: ({ "tnum": 1 })
                }
            }

            // The readout, and the volume beside it (mute, then level).
            Item {
                id: footer

                width: parent.width
                height: Math.max(readout.implicitHeight, volumeGroup.height)

                // The bottom row: Down stays put rather than falling through to
                // the page, which would send the keyboard back up to ⏯.
                Keys.onDownPressed: event => event.accepted = true

                Text {
                    id: readout

                    anchors.left: parent.left
                    anchors.right: volumeGroup.left
                    anchors.rightMargin: Theme.spacingValue
                    anchors.verticalCenter: parent.verticalCenter
                    text: view.buffering ? qsTr("Buffering") : NowPlayingMusicCtl.readout
                    color: Theme.textTertiary
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontCaption
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: Theme.fontCaption * Theme.crateKickerTracking
                    elide: Text.ElideRight

                    SequentialAnimation on opacity {
                        running: view.buffering && !Theme.reducedMotion
                        loops: Animation.Infinite
                        alwaysRunToEnd: true

                        NumberAnimation {
                            to: 0.35
                            duration: Theme.animAmbient / 2
                            easing.type: Easing.InOutSine
                        }
                        NumberAnimation {
                            to: 1.0
                            duration: Theme.animAmbient / 2
                            easing.type: Easing.InOutSine
                        }
                    }
                }

                Row {
                    id: volumeGroup

                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spacingTight

                    StrmIconButton {
                        id: muteButton

                        anchors.verticalCenter: parent.verticalCenter
                        size: Theme.scale(40)
                        activeFocusOnTab: false
                        iconName: view.volumeIcon
                        tooltip: PlayerCtl.muted === true ? qsTr("Unmute") : qsTr("Mute")
                        checked: PlayerCtl.muted === true
                        onClicked: PlayerCtl.toggleMute()

                        // First in its row: Left stays here rather than reaching
                        // the page, where it would seek.
                        Keys.onLeftPressed: event => event.accepted = true

                        KeyNavigation.right: volumeSlider
                        KeyNavigation.up: scrubber
                    }

                    // In the chain, last in its row: once focused, Left/Right
                    // are the level (StrmSlider owns them, and a volume step is
                    // harmless where a seek is not, so it is not armToScrub),
                    // and Up leaves.
                    StrmSlider {
                        id: volumeSlider

                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.scale(120)
                        activeFocusOnTab: false
                        showKnobOnHoverOnly: false
                        from: 0
                        to: PlayerCtl.maxVolume
                        stepSize: 5
                        value: PlayerCtl.muted === true ? 0 : PlayerCtl.volume
                        accessibleName: qsTr("Volume")
                        accessibleDescription: qsTr("%1 percent").arg(Math.round(value))

                        onMoved: value => {
                            PlayerCtl.setMuted(false);
                            PlayerCtl.setVolume(Math.round(value));
                        }
                        onCommitted: value => PlayerCtl.setVolume(Math.round(value))

                        KeyNavigation.up: scrubber
                    }
                }
            }
        }
    }

    // ── Panel ───────────────────────────────────────────────────────────────
    MusicPlayerPanel {
        id: panel

        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: view.width >= Theme.scale(1100) ? Theme.scale(440) : Math.round(view.width * 0.4)

        onLeftRequested: moreButton.forceActiveFocus(Qt.BacktabFocusReason)
    }

    // ── Back ────────────────────────────────────────────────────────────────
    StrmIconButton {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: Theme.spacingLoose
        size: Theme.scale(40)
        round: true
        activeFocusOnTab: false
        iconName: "arrow-left"
        tooltip: qsTr("Back")
        onClicked: view.leaveRequested()
    }

    // ── Menus and pickers ───────────────────────────────────────────────────
    StrmMenu {
        id: moreMenu

        actions: [
            { "key": "album", "text": qsTr("Go to album"), "iconName": "lib-music",
              "enabled": NowPlayingMusicCtl.albumId.length > 0 },
            { "key": "artist", "text": qsTr("Go to artist"), "iconName": "user",
              "enabled": NowPlayingMusicCtl.artistId.length > 0 },
            { "key": "radio", "text": qsTr("Radio"), "iconName": "playlist",
              "enabled": NowPlayingMusicCtl.trackId.length > 0 },
            { "separator": true },
            { "key": "stop", "text": qsTr("Stop"), "iconName": "stop" }
        ]

        onTriggered: index => view.runMenu(String(moreMenu.actions[index].key))
    }

    PlaylistPicker {
        id: playlistPicker

        z: 800
        mediaType: "Audio"
        onDismissed: addButton.forceActiveFocus(Qt.OtherFocusReason)
    }

    // `pending` tells this view's result apart from a playlist edited elsewhere.
    Connections {
        target: PlaylistCtl

        function onActionSucceeded(message) {
            if (!playlistPicker.pending)
                return;
            playlistPicker.pending = false;
            toasts.show(message, "success");
        }
        function onActionFailed(message) {
            if (!playlistPicker.pending)
                return;
            playlistPicker.pending = false;
            toasts.show(message, "error");
        }
    }

    StrmToastHost {
        id: toasts

        anchors.fill: parent
        z: 900
    }
}
