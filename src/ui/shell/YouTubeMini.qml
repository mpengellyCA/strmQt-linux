import QtQuick
import StrmQt

// What YouTube is playing, while the page on screen is something else.
//
// The film has a picture-in-picture frame and a docked bar; this is both for
// YouTube, in one card: the picture, what it is, and a row of controls. The
// picture is the shell's one YouTube view (Main.qml youtubeHost), parked in
// `slot` at a fraction of its size — still the live page, so the video in it
// is the video, not a copy.
//
// It decides nothing. Every control emits a signal; Main.qml asks the view.
Item {
    id: mini

    // Up, and what the page reported last (YouTubeView.stateReported).
    property bool shown: false
    property var state: ({})
    // Width over height of the page area the view is laid out for: the frame
    // keeps that shape, so the parked view fills it exactly.
    property real aspect: 16 / 9

    readonly property Item slot: frameSlot
    readonly property bool playing: mini.state.playing === true
    readonly property bool muted: mini.state.muted === true
    readonly property real volume: mini.state.volume !== undefined ? Number(mini.state.volume) : 1
    readonly property string title: mini.state.title ? String(mini.state.title) : qsTr("YouTube")

    // FocusNavigator.h: entered on Play, and Up leads back to the page.
    readonly property Item navEntry: playButton
    property Item navUp: null

    signal expandRequested
    signal stopRequested
    signal playPauseRequested
    signal seekRequested(real seconds)
    signal volumeRequested(real volume)
    signal muteRequested(bool muted)
    // Back was pressed on it: the keyboard goes back to the page.
    signal dismissed

    function clock(seconds: real): string {
        const total = Math.max(0, Math.floor(seconds));
        const h = Math.floor(total / 3600);
        const m = Math.floor((total % 3600) / 60);
        const s = total % 60;
        const two = n => (n < 10 ? "0" : "") + n;
        return h > 0 ? h + ":" + two(m) + ":" + two(s) : m + ":" + two(s);
    }

    readonly property int cardWidth:
        Math.round(Math.min(Theme.scale(400), Math.max(Theme.scale(320), mini.parent
                                                       ? mini.parent.width * 0.24 : 0)))
    property real reveal: mini.shown ? 1 : 0

    Behavior on reveal {
        NumberAnimation {
            duration: Theme.animNormalMs
            easing.type: Theme.easeStandard
        }
    }

    width: mini.cardWidth
    height: card.height
    visible: mini.shown || mini.reveal > 0.001
    enabled: mini.shown
    opacity: mini.reveal
    scale: 0.94 + 0.06 * mini.reveal

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

    Rectangle {
        id: card

        width: parent.width
        height: column.height
        radius: Theme.radiusCardValue
        color: Theme.surfaceRaisedColor
        border.width: 1
        border.color: Theme.hairline
        clip: true

        // Absorbs: the card floats over a grid, and a press that fell through
        // a gap between two buttons would open whatever is underneath.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            onWheel: wheel => wheel.accepted = true
        }

        Column {
            id: column

            width: parent.width

            Rectangle {
                id: frame

                width: parent.width
                height: Math.round(frame.width / Math.max(0.5, mini.aspect))
                color: "black"
                clip: true

                // The shell's view is parented here, and to the YouTube page
                // while that is up. Nothing else may occupy it.
                Item {
                    id: frameSlot

                    anchors.fill: parent
                }

                Rectangle {
                    anchors.fill: parent
                    color: Theme.scrimColor
                    opacity: frameHover.hovered ? 0.55 : 0
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

                HoverHandler {
                    id: frameHover

                    cursorShape: Qt.PointingHandCursor
                }

                // Over the page, so a click here is "open it" and never a
                // click on whatever YouTube control happens to be under it.
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    hoverEnabled: true
                    onClicked: mini.expandRequested()
                }
            }

            Item {
                width: parent.width
                height: caption.height + Theme.spacingTight

                Column {
                    id: caption

                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: Theme.spacingTight
                    y: Theme.spacingTight
                    spacing: 2

                    Text {
                        width: parent.width
                        text: mini.title
                        color: Theme.textPrimaryColor
                        font.family: Theme.fontBody
                        font.pixelSize: Theme.fontBodySize
                        elide: Text.ElideRight
                        maximumLineCount: 1
                    }

                    Text {
                        width: parent.width
                        readonly property real duration: Number(mini.state.duration || 0)
                        text: (duration > 0
                               ? mini.clock(Number(mini.state.position || 0)) + " / " + mini.clock(duration)
                               : qsTr("Live"))
                              + (mini.state.artist ? "  ·  " + String(mini.state.artist) : "")
                        color: Theme.textSecondaryColor
                        font.family: Theme.fontBody
                        font.pixelSize: Theme.fontCaption
                        elide: Text.ElideRight
                        maximumLineCount: 1
                    }
                }
            }

            Item {
                width: parent.width
                height: controls.height + Theme.spacingTight

                Row {
                    id: controls

                    anchors.horizontalCenter: parent.horizontalCenter
                    y: Math.round(Theme.spacingTight / 2)
                    spacing: 2

                    readonly property int buttonSize: Math.round(Theme.controlHeight * 0.9)

                    StrmIconButton {
                        objectName: "youtubeMiniRewind"
                        size: controls.buttonSize
                        iconName: "rewind-10"
                        tooltip: qsTr("Back 10 seconds")
                        onClicked: mini.seekRequested(-10)
                    }

                    StrmIconButton {
                        id: playButton

                        objectName: "youtubeMiniPlay"
                        size: controls.buttonSize
                        iconName: mini.playing ? "pause" : "play"
                        tooltip: mini.playing ? qsTr("Pause") : qsTr("Play")
                        onClicked: mini.playPauseRequested()
                    }

                    StrmIconButton {
                        objectName: "youtubeMiniForward"
                        size: controls.buttonSize
                        iconName: "forward-10"
                        tooltip: qsTr("Forward 10 seconds")
                        onClicked: mini.seekRequested(10)
                    }

                    StrmIconButton {
                        objectName: "youtubeMiniStop"
                        size: controls.buttonSize
                        iconName: "stop"
                        tooltip: qsTr("Stop")
                        onClicked: mini.stopRequested()
                    }

                    Item {
                        width: Theme.spacingTight
                        height: 1
                    }

                    StrmIconButton {
                        objectName: "youtubeMiniQuieter"
                        size: controls.buttonSize
                        iconName: "minus"
                        tooltip: qsTr("Volume down")
                        onClicked: mini.volumeRequested(Math.max(0, mini.volume - 0.1))
                    }

                    StrmIconButton {
                        objectName: "youtubeMiniMute"
                        size: controls.buttonSize
                        iconName: mini.muted || mini.volume <= 0.001 ? "volume-mute"
                                : mini.volume < 0.5 ? "volume-low" : "volume-high"
                        tooltip: mini.muted ? qsTr("Unmute")
                                            : qsTr("Mute (volume %1%)").arg(Math.round(mini.volume * 100))
                        checked: mini.muted
                        onClicked: mini.muteRequested(!mini.muted)
                    }

                    StrmIconButton {
                        objectName: "youtubeMiniLouder"
                        size: controls.buttonSize
                        iconName: "plus"
                        tooltip: qsTr("Volume up")
                        onClicked: mini.volumeRequested(Math.min(1, mini.volume + 0.1))
                    }

                    Item {
                        width: Theme.spacingTight
                        height: 1
                    }

                    StrmIconButton {
                        objectName: "youtubeMiniExpand"
                        size: controls.buttonSize
                        iconName: "fullscreen"
                        tooltip: qsTr("Back to YouTube")
                        onClicked: mini.expandRequested()
                    }
                }
            }
        }
    }
}
