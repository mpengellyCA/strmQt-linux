import QtQuick
import StrmQt

// What YouTube is playing, while the page on screen is something else.
//
// The same two pieces a film has, and deliberately the same shapes: a docked
// bar across the bottom (MiniPlayer.qml's video bar — scrubber on the top
// edge, what is playing on the left, transport in the middle, timecode and
// "open" on the right) and a picture floating above its right-hand end
// (Main.qml `pip`). The metrics are MiniPlayer's, so a film and a YouTube
// video leave the same chrome behind them.
//
// The picture is the shell's one YouTube view (Main.qml youtubeHost), parked in
// `slot` at a fraction of its size — still the live page, so the video in it
// is the video, not a copy.
//
// It decides nothing. Every control emits a signal; Main.qml asks the view.
FocusScope {
    id: mini

    // Up, and what the page reported last (YouTubeView.stateReported).
    property bool shown: false
    // (Not `state`: that is Item's own, and a different thing.)
    property var report: ({})
    // Width over height the view is laid out for: the picture keeps that
    // shape, so the parked view fills it exactly.
    property real aspect: 16 / 9
    // YouTube Music rather than a video: a record has a cover, not a
    // picture, and a queue to step through instead of ten seconds either way.
    property bool music: false
    // How far above the bar the picture stands: the film's own picture, when
    // there is one, has the place directly above the bar.
    property int pictureLift: 0

    readonly property Item slot: frameSlot
    readonly property bool playing: mini.report.playing === true
    readonly property bool muted: mini.report.muted === true
    readonly property real volume: mini.report.volume !== undefined ? Number(mini.report.volume) : 1
    readonly property real position: Number(mini.report.position || 0)
    readonly property real duration: Number(mini.report.duration || 0)
    readonly property string serviceName: mini.music ? qsTr("YouTube Music") : qsTr("YouTube")
    readonly property string title: mini.report.title ? String(mini.report.title) : mini.serviceName
    // Only ever a picture from Google's image hosts, over HTTPS: the address
    // is the page's word, and an Image fetches whatever it is given.
    readonly property string art: {
        const address = mini.music && mini.report.art ? String(mini.report.art) : "";
        return /^https:\/\/[a-z0-9.-]+\.(googleusercontent|ytimg|ggpht)\.com\//i.test(address)
               ? address : "";
    }
    readonly property string artist: mini.report.artist ? String(mini.report.artist) : ""
    readonly property bool compactWidth: mini.width < Theme.scale(900)
    readonly property bool narrowWidth: mini.width < Theme.scale(620)

    // MiniPlayer's video metrics, one for one.
    readonly property int scrubberHeight: Theme.scale(16)
    readonly property int contentHeight: Theme.scale(52)
    readonly property int barHeight: 1 + mini.scrubberHeight + mini.contentHeight
                                     + Theme.spacingTight
    // Animated, so the page area follows the bar rather than snapping.
    readonly property int reservedHeight: Math.round(mini.barHeight * mini.slide)
    readonly property int pictureWidth:
        Math.round(Math.min(Theme.scale(360),
                            Math.max(Theme.scale(200), mini.Window.width * 0.22)))

    // FocusNavigator.h: entered on play/pause, and Up leads back to the page.
    property Item navEntry: playPause
    property Item navUp: null
    // The film's bar, when both are up: it is docked underneath this one.
    property Item navDown: null

    signal expandRequested
    signal stopRequested
    signal playPauseRequested
    signal seekRequested(real seconds)
    signal seekToRequested(real seconds)
    signal previousRequested
    signal nextRequested
    signal volumeRequested(real volume)
    signal muteRequested(bool muted)
    // Back was pressed on it, or it left while holding the keyboard.
    signal dismissed

    function clock(seconds: real): string {
        const total = Math.max(0, Math.floor(seconds));
        const h = Math.floor(total / 3600);
        const m = Math.floor((total % 3600) / 60);
        const s = total % 60;
        const two = n => (n < 10 ? "0" : "") + n;
        return h > 0 ? h + ":" + two(m) + ":" + two(s) : m + ":" + two(s);
    }

    readonly property string timeText: mini.duration > 0
        ? mini.clock(mini.position) + "  /  " + mini.clock(mini.duration)
        : qsTr("Live")

    property real slide: mini.shown ? 1 : 0

    Behavior on slide {
        NumberAnimation {
            duration: Theme.animNormalMs
            easing.type: Theme.easeStandard
        }
    }

    implicitHeight: mini.barHeight
    // Never hidden as a whole. The view is parented into the picture at the
    // very moment this is told to show, and a web view that finds itself
    // inside something invisible — for one frame — tells its page it has been
    // hidden; YouTube pauses when it hears that and does not resume. The bar
    // and the picture fade and stop taking input instead.
    enabled: mini.shown
    activeFocusOnTab: mini.shown

    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Playing on YouTube")
    Accessible.description: mini.title

    onShownChanged: {
        if (!mini.shown && mini.activeFocus)
            mini.dismissed();
    }

    Keys.onPressed: event => {
        if (event.isAutoRepeat)
            return;
        if (Input.actionForKey(event.key, event.modifiers, "browse") === "nav.back") {
            mini.dismissed();
            event.accepted = true;
            return;
        }
        event.accepted = false;
    }

    TabularMetrics {
        id: timeMetrics

        font.family: Theme.fontMono
        font.pixelSize: Theme.fontCaption
        text: mini.timeText.replace(/[0-9]/g, "0")
    }

    // ── The picture ─────────────────────────────────────────────────────────
    Item {
        id: picture

        width: mini.pictureWidth
        height: Math.round(picture.width / Math.max(0.5, mini.aspect))
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingValue
        y: mini.barHeight - mini.reservedHeight - Theme.spacingValue - mini.pictureLift
           - picture.height
        opacity: mini.music ? 0 : mini.slide
        enabled: !mini.music
        scale: 0.94 + 0.06 * mini.slide

        Accessible.role: Accessible.Button
        Accessible.name: qsTr("Picture in picture: %1").arg(mini.title)
        Accessible.description: qsTr("Back to YouTube")
        Accessible.onPressAction: mini.expandRequested()

        // The rim Main.qml's `pip` draws, for the reason given there.
        Repeater {
            model: [{ grow: Theme.scale(6), alpha: 0.16 },
                    { grow: Theme.scale(3), alpha: 0.28 }]

            Rectangle {
                required property var modelData

                anchors.centerIn: parent
                width: parent.width + modelData.grow * 2
                height: parent.height + modelData.grow * 2
                radius: Theme.radiusCardValue + modelData.grow
                color: Qt.rgba(0, 0, 0, modelData.alpha)
            }
        }

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusCardValue
            color: "black"
            border.width: 1
            border.color: pictureHover.hovered ? Theme.accentColor : Theme.hairline
            clip: true

            Behavior on border.color {
                ColorAnimation {
                    duration: Theme.animFastMs
                    easing.type: Theme.easeStandard
                }
            }

            // The shell's view is parented here, and to the YouTube page
            // while that is up. Nothing else may occupy it.
            Item {
                id: frameSlot

                anchors.fill: parent
            }

            Rectangle {
                anchors.fill: parent
                color: Theme.scrimColor
                opacity: pictureHover.hovered ? 0.55 : 0
                visible: opacity > 0.001

                Behavior on opacity {
                    NumberAnimation {
                        duration: Theme.animFastMs
                        easing.type: Theme.easeStandard
                    }
                }

                StrmIcon {
                    anchors.centerIn: parent
                    name: "fullscreen"
                    size: Math.round(Theme.iconSize * 1.6)
                    color: Theme.textPrimaryColor
                }
            }
        }

        HoverHandler {
            id: pictureHover

            cursorShape: Qt.PointingHandCursor
        }

        // Over the page, so a click here is "open it" and never a click on
        // whatever YouTube control happens to be under it.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            hoverEnabled: true
            onClicked: mini.expandRequested()
            onWheel: wheel => wheel.accepted = true
        }
    }

    // ── The bar ─────────────────────────────────────────────────────────────
    Item {
        anchors.fill: parent
        // It slides out through the bottom edge; only clipped while it does,
        // so the focus rings that sit just outside a control are not cut.
        clip: mini.slide < 1
        visible: mini.slide > 0.001

        Rectangle {
            id: bar

            anchors.left: parent.left
            anchors.right: parent.right
            y: Math.round(mini.barHeight * (1 - mini.slide))
            height: mini.barHeight
            color: Theme.surfaceColor
            opacity: mini.slide

            // The input floor (MiniPlayer.qml): a press no control claimed
            // must not carry on to the page behind the bar.
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.AllButtons
                onWheel: wheel => wheel.accepted = true
            }

            Rectangle {
                id: edge

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: Theme.hairline
            }

            StrmSlider {
                id: scrubber

                objectName: "youtubeMiniScrubber"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: edge.bottom
                height: mini.scrubberHeight
                enabled: mini.duration > 0
                activeFocusOnTab: false
                from: 0
                to: Math.max(1, mini.duration)
                value: mini.position
                stepSize: 10
                armToScrub: true
                accessibleName: qsTr("Playback position")
                accessibleDescription: qsTr("%1 of %2").arg(mini.clock(value)).arg(mini.clock(to))

                onCommitted: v => mini.seekToRequested(v)

                property Item navDown: playPause
            }

            Item {
                id: content

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: scrubber.bottom
                anchors.leftMargin: Theme.spacingValue
                anchors.rightMargin: Theme.spacingValue
                height: mini.contentHeight

                // ── Left: what is playing ───────────────────────────────────
                Item {
                    id: identity

                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: Math.max(0, transport.x - Theme.spacingTight)

                    Rectangle {
                        id: badge

                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        height: Theme.scale(44)
                        width: mini.music ? badge.height : Math.round(badge.height * 16 / 9)
                        radius: Theme.radiusChip
                        color: Theme.surfaceRaisedColor
                        clip: true

                        StrmIcon {
                            anchors.centerIn: parent
                            name: mini.music ? "youtube-music" : "youtube"
                            size: Theme.iconSize
                            color: Theme.textSecondaryColor
                        }

                        // The cover YouTube Music names for the track. Not
                        // StrmImage: that is the media server's cache and
                        // its sign-in, and this address is neither's.
                        Image {
                            anchors.fill: parent
                            visible: mini.art.length > 0 && status === Image.Ready
                            source: mini.art
                            sourceSize.width: 2 * badge.width
                            sourceSize.height: 2 * badge.height
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                        }
                    }

                    Column {
                        anchors.left: badge.right
                        anchors.leftMargin: Theme.spacingValue
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.scale(2)

                        Text {
                            width: parent.width
                            // The page's words: shown, never interpreted.
                            textFormat: Text.PlainText
                            text: mini.title
                            color: identityHover.hovered ? Theme.textPrimaryColor
                                                         : Qt.darker(Theme.textPrimaryColor, 1.06)
                            font.family: Theme.fontDisplay
                            font.pixelSize: Theme.fontBodySize
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }

                        Text {
                            width: parent.width
                            visible: !mini.narrowWidth
                            textFormat: Text.PlainText
                            text: mini.artist.length > 0
                                  ? qsTr("%1  ·  %2").arg(mini.artist).arg(mini.serviceName)
                                  : mini.serviceName
                            color: Theme.textSecondaryColor
                            font.family: Theme.fontBody
                            font.pixelSize: Theme.fontCaption
                            elide: Text.ElideRight
                        }
                    }

                    HoverHandler {
                        id: identityHover

                        cursorShape: Qt.PointingHandCursor
                    }

                    TapHandler {
                        gesturePolicy: TapHandler.ReleaseWithinBounds
                        onTapped: {
                            Input.noteInput("mouse");
                            mini.expandRequested();
                        }
                    }
                }

                // ── Centre: transport ───────────────────────────────────────
                Row {
                    id: transport

                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spacingTight

                    StrmIconButton {
                        id: previousButton

                        objectName: "youtubeMiniPrevious"
                        anchors.verticalCenter: parent.verticalCenter
                        size: Theme.scale(34)
                        visible: mini.music
                        activeFocusOnTab: false
                        iconName: "skip-previous"
                        tooltip: qsTr("Previous")

                        onClicked: mini.previousRequested()

                        property Item navRight: playPause
                        property Item navUp: scrubber
                    }

                    StrmIconButton {
                        id: rewindButton

                        objectName: "youtubeMiniRewind"
                        anchors.verticalCenter: parent.verticalCenter
                        size: Theme.scale(34)
                        activeFocusOnTab: false
                        visible: !mini.music
                        iconName: "rewind-10"
                        tooltip: qsTr("Back 10 seconds")
                        enabled: mini.duration > 0

                        onClicked: mini.seekRequested(-10)

                        property Item navRight: playPause
                        property Item navUp: scrubber
                    }

                    StrmIconButton {
                        id: playPause

                        objectName: "youtubeMiniPlay"
                        anchors.verticalCenter: parent.verticalCenter
                        size: Theme.scale(40)
                        focus: true
                        activeFocusOnTab: false
                        iconName: mini.playing ? "pause" : "play"
                        tooltip: mini.playing ? qsTr("Pause") : qsTr("Play")

                        onClicked: mini.playPauseRequested()

                        property Item navLeft: mini.music ? previousButton : rewindButton
                        property Item navRight: mini.music ? nextButton : forwardButton
                        property Item navUp: scrubber
                    }

                    StrmIconButton {
                        id: forwardButton

                        objectName: "youtubeMiniForward"
                        anchors.verticalCenter: parent.verticalCenter
                        size: Theme.scale(34)
                        activeFocusOnTab: false
                        visible: !mini.music
                        iconName: "forward-10"
                        tooltip: qsTr("Forward 10 seconds")
                        enabled: mini.duration > 0

                        onClicked: mini.seekRequested(10)

                        property Item navLeft: playPause
                        property Item navRight: stopButton
                        property Item navUp: scrubber
                    }

                    StrmIconButton {
                        id: nextButton

                        objectName: "youtubeMiniNext"
                        anchors.verticalCenter: parent.verticalCenter
                        size: Theme.scale(34)
                        visible: mini.music
                        activeFocusOnTab: false
                        iconName: "skip-next"
                        tooltip: qsTr("Next")

                        onClicked: mini.nextRequested()

                        property Item navLeft: playPause
                        property Item navRight: stopButton
                        property Item navUp: scrubber
                    }

                    StrmIconButton {
                        id: stopButton

                        objectName: "youtubeMiniStop"
                        anchors.verticalCenter: parent.verticalCenter
                        size: Theme.scale(34)
                        activeFocusOnTab: false
                        iconName: "stop"
                        tooltip: qsTr("Stop")

                        onClicked: mini.stopRequested()

                        property Item navLeft: mini.music ? nextButton : forwardButton
                        property Item navRight: quieterButton
                        property Item navUp: scrubber
                    }
                }

                // ── Right: timecode, level, back into YouTube ───────────────
                Row {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spacingTight

                    TabularText {
                        anchors.verticalCenter: parent.verticalCenter
                        width: timeMetrics.width
                        horizontalAlignment: Text.AlignRight
                        text: mini.timeText
                        visible: !mini.compactWidth
                        color: Theme.textTertiary
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontCaption
                    }

                    // The level is here and not only in the full player, as it
                    // is for a film: YouTube's own volume control is a screen
                    // away, behind the video this bar exists to avoid opening.
                    StrmIconButton {
                        id: quieterButton

                        objectName: "youtubeMiniQuieter"
                        anchors.verticalCenter: parent.verticalCenter
                        size: Theme.scale(34)
                        visible: !mini.narrowWidth
                        activeFocusOnTab: false
                        iconName: "minus"
                        tooltip: qsTr("Volume down")

                        onClicked: mini.volumeRequested(Math.max(0, mini.volume - 0.1))

                        property Item navLeft: stopButton
                        property Item navRight: muteButton
                        property Item navUp: scrubber
                    }

                    StrmIconButton {
                        id: muteButton

                        objectName: "youtubeMiniMute"
                        anchors.verticalCenter: parent.verticalCenter
                        size: Theme.scale(34)
                        activeFocusOnTab: false
                        iconName: mini.muted || mini.volume <= 0.001 ? "volume-mute"
                                : mini.volume < 0.5 ? "volume-low" : "volume-high"
                        tooltip: mini.muted ? qsTr("Unmute")
                                            : qsTr("Mute (volume %1%)").arg(Math.round(mini.volume * 100))
                        checked: mini.muted

                        onClicked: mini.muteRequested(!mini.muted)

                        property Item navLeft: quieterButton
                        property Item navRight: louderButton
                        property Item navUp: scrubber
                    }

                    StrmIconButton {
                        id: louderButton

                        objectName: "youtubeMiniLouder"
                        anchors.verticalCenter: parent.verticalCenter
                        size: Theme.scale(34)
                        visible: !mini.narrowWidth
                        activeFocusOnTab: false
                        iconName: "plus"
                        tooltip: qsTr("Volume up")

                        onClicked: mini.volumeRequested(Math.min(1, mini.volume + 0.1))

                        property Item navLeft: muteButton
                        property Item navRight: expandButton
                        property Item navUp: scrubber
                    }

                    StrmIconButton {
                        id: expandButton

                        objectName: "youtubeMiniExpand"
                        anchors.verticalCenter: parent.verticalCenter
                        size: Theme.scale(34)
                        activeFocusOnTab: false
                        iconName: "chevron-up"
                        tooltip: qsTr("Back to %1").arg(mini.serviceName)

                        onClicked: mini.expandRequested()

                        property Item navLeft: louderButton
                        property Item navUp: scrubber
                    }
                }
            }
        }
    }
}
