import QtQuick
import StrmQt

// OsdButtonRow — the player's transport (ARCHITECTURE.md).
//
// Jellyfin's videoosd bar, minus the things this client has no verb for:
// previous · −10 s · play/pause · +10 s · next · previous/next chapter, then
// volume, audio, subtitles, chapters, queue, settings, stats and fullscreen.
//
// A control that cannot act is DISABLED, never hidden: a button that vanishes
// when there is no next episode teaches the user nothing and moves every other
// button under their cursor. Disabled buttons keep their place and their
// tooltip.
//
// A FocusScope with `focus: true` on play/pause, so the OSD can hand the
// keyboard to the row as a whole and land somewhere sensible. Left/Right walk
// the row through KeyNavigation; Up leaves it for `focusAbove` (the scrubber).
FocusScope {
    id: row

    // ── Contract ────────────────────────────────────────────────────────────
    property string panelKey: ""
    property bool statsVisible: false
    property bool fullscreen: false
    property bool hasChapters: false
    // Where Up goes from any button in the row.
    property Item focusAbove: null

    // Any use of the row is user input; the OSD's idle timer restarts on it.
    signal woken
    // key is one of "tracks" | "chapters" | "queue" | "settings"; origin is the
    // button that asked, so focus can go back to it when the panel closes.
    signal panelRequested(string key, Item origin)
    signal statsRequested
    signal fullscreenRequested

    implicitHeight: Theme.touchTarget
    height: implicitHeight

    // Shortcuts on tooltips come from InputMap, never from a literal. Reading
    // the notifying `Input.actions` here is what makes every tooltip below
    // follow a rebinding live (the same trick Main.qml uses). Actions InputMap
    // does not define yet fall back to the default this wave ships with, and
    // pick up a real binding the moment the catalogue grows one — exactly how
    // Main.qml handles app.shortcuts and app.commandPalette.
    readonly property var keymap: {
        const list = Input.actions;
        const map = ({});
        for (let i = 0; i < list.length; ++i)
            map[list[i].actionId] = list[i].sequence;
        return map;
    }

    function shortcutFor(actionId: string, fallback: string): string {
        const bound = row.keymap[actionId];
        return (bound !== undefined && String(bound).length > 0) ? String(bound) : fallback;
    }

    function focusPrimary(): void {
        playPause.forceActiveFocus(Qt.OtherFocusReason);
    }

    // Chapter navigation is being added to PlayerController by another agent in
    // this same wave. Guarded so a half-landed contract disables a button
    // instead of throwing inside a click handler.
    function jumpChapter(forward: bool): void {
        row.woken();
        if (forward && typeof PlayerCtl.nextChapter === "function")
            PlayerCtl.nextChapter();
        else if (!forward && typeof PlayerCtl.previousChapter === "function")
            PlayerCtl.previousChapter();
    }

    readonly property bool canGoNext: PlayerCtl.hasNext === true
    // Previous is live whenever it can do something: with no earlier queue entry
    // it still restarts the current item, which is the universal convention.
    readonly property bool canGoPrevious: PlayerCtl.hasPrevious === true
                                          || PlayerCtl.positionMs > 5000

    readonly property string volumeIcon: PlayerCtl.muted ? "volume-mute"
                                       : PlayerCtl.volume <= 0 ? "volume-mute"
                                       : PlayerCtl.volume < 40 ? "volume-low"
                                       : "volume-high"

    // ── Transport ───────────────────────────────────────────────────────────
    Row {
        id: leftGroup

        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spacingTight

        StrmIconButton {
            id: previousButton

            anchors.verticalCenter: parent.verticalCenter
            iconName: "skip-previous"
            tooltip: qsTr("Previous")
            shortcut: row.shortcutFor("player.previous", "Shift+P")
            enabled: row.canGoPrevious
            onClicked: {
                row.woken();
                PlayerCtl.playPrevious();
            }

            KeyNavigation.right: rewindButton
            KeyNavigation.up: row.focusAbove
        }

        StrmIconButton {
            id: rewindButton

            anchors.verticalCenter: parent.verticalCenter
            iconName: "rewind-10"
            tooltip: qsTr("Back 10 seconds")
            shortcut: row.shortcutFor("player.seekBackward", "Left")
            onClicked: {
                row.woken();
                PlayerCtl.seekRelative(-10000);
            }

            KeyNavigation.left: previousButton
            KeyNavigation.right: playPause
            KeyNavigation.up: row.focusAbove
        }

        // The primary. Round, accent-filled, and the one button in the row that
        // is bigger than the others.
        StrmIconButton {
            id: playPause

            anchors.verticalCenter: parent.verticalCenter
            focus: true
            round: true
            size: Theme.controlHeightLarge
            iconName: PlayerCtl.paused ? "play" : "pause"
            tooltip: PlayerCtl.paused ? qsTr("Play") : qsTr("Pause")
            shortcut: row.shortcutFor("player.togglePause", "Space")
            checked: true
            onClicked: {
                row.woken();
                PlayerCtl.togglePause();
            }

            KeyNavigation.left: rewindButton
            KeyNavigation.right: forwardButton
            KeyNavigation.up: row.focusAbove
        }

        StrmIconButton {
            id: forwardButton

            anchors.verticalCenter: parent.verticalCenter
            iconName: "forward-10"
            tooltip: qsTr("Forward 10 seconds")
            shortcut: row.shortcutFor("player.seekForward", "Right")
            onClicked: {
                row.woken();
                PlayerCtl.seekRelative(10000);
            }

            KeyNavigation.left: playPause
            KeyNavigation.right: nextButton
            KeyNavigation.up: row.focusAbove
        }

        StrmIconButton {
            id: nextButton

            anchors.verticalCenter: parent.verticalCenter
            iconName: "skip-next"
            tooltip: qsTr("Next")
            shortcut: row.shortcutFor("player.next", "Shift+N")
            enabled: row.canGoNext
            onClicked: {
                row.woken();
                PlayerCtl.playNext();
            }

            KeyNavigation.left: forwardButton
            KeyNavigation.right: chapterPrevious
            KeyNavigation.up: row.focusAbove
        }

        Item {
            width: Theme.spacingValue
            height: 1
        }

        StrmIconButton {
            id: chapterPrevious

            anchors.verticalCenter: parent.verticalCenter
            iconName: "chapter-previous"
            tooltip: qsTr("Previous chapter")
            shortcut: row.shortcutFor("player.previousChapter", "[")
            enabled: row.hasChapters
            onClicked: row.jumpChapter(false)

            KeyNavigation.left: nextButton
            KeyNavigation.right: chapterNext
            KeyNavigation.up: row.focusAbove
        }

        StrmIconButton {
            id: chapterNext

            anchors.verticalCenter: parent.verticalCenter
            iconName: "chapter-next"
            tooltip: qsTr("Next chapter")
            shortcut: row.shortcutFor("player.nextChapter", "]")
            enabled: row.hasChapters
            onClicked: row.jumpChapter(true)

            KeyNavigation.left: chapterPrevious
            KeyNavigation.right: muteButton
            KeyNavigation.up: row.focusAbove
        }
    }

    // ── Volume and panels ───────────────────────────────────────────────────
    Row {
        id: rightGroup

        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spacingTight

        // Volume: the mute button is the tab stop, the slider is the pointer's
        // affordance. It is deliberately OUT of the KeyNavigation chain —
        // StrmSlider consumes Left/Right for its own value, so leaving it in
        // would trap arrow navigation halfway along the row. The keyboard
        // adjusts volume with the player.volumeUp / player.volumeDown bindings
        // instead, which the page routes through InputMap.
        Item {
            id: volumeGroup

            readonly property bool expanded: volumeHover.hovered || muteButton.activeFocus
                                             || volumeSlider.dragging

            anchors.verticalCenter: parent.verticalCenter
            width: muteButton.width + (volumeGroup.expanded
                                       ? volumeSlider.implicitWidth + Theme.spacingTight : 0)
            height: Theme.touchTarget

            Behavior on width {
                NumberAnimation {
                    duration: Theme.animInstant
                    easing.type: Theme.easeInstant
                }
            }

            HoverHandler {
                id: volumeHover
                // Hover only: it never takes the keyboard's place.
            }

            StrmIconButton {
                id: muteButton

                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                iconName: row.volumeIcon
                tooltip: PlayerCtl.muted ? qsTr("Unmute") : qsTr("Mute")
                shortcut: row.shortcutFor("player.mute", "M")
                checked: PlayerCtl.muted
                onClicked: {
                    row.woken();
                    PlayerCtl.toggleMute();
                }

                KeyNavigation.left: chapterNext
                KeyNavigation.right: audioButton
                KeyNavigation.up: row.focusAbove
            }

            StrmSlider {
                id: volumeSlider

                anchors.left: muteButton.right
                anchors.leftMargin: Theme.spacingTight
                anchors.verticalCenter: parent.verticalCenter
                implicitWidth: Theme.scale(120)
                width: Math.max(0, volumeGroup.width - muteButton.width - Theme.spacingTight)
                visible: width > Theme.scale(8)
                // Not a tab stop: see the note on volumeGroup.
                activeFocusOnTab: false
                showKnobOnHoverOnly: false
                from: 0
                to: PlayerCtl.maxVolume
                stepSize: 5
                value: PlayerCtl.muted ? 0 : PlayerCtl.volume

                onMoved: value => {
                    row.woken();
                    PlayerCtl.setMuted(false);
                    PlayerCtl.setVolume(Math.round(value));
                }
                onCommitted: value => PlayerCtl.setVolume(Math.round(value))
            }
        }

        StrmIconButton {
            id: audioButton

            anchors.verticalCenter: parent.verticalCenter
            iconName: "audio-track"
            tooltip: qsTr("Audio track")
            shortcut: row.shortcutFor("player.cycleAudio", "A")
            checked: row.panelKey === "tracks"
            onClicked: row.panelRequested("tracks", audioButton)

            KeyNavigation.left: muteButton
            KeyNavigation.right: subtitleButton
            KeyNavigation.up: row.focusAbove
        }

        StrmIconButton {
            id: subtitleButton

            anchors.verticalCenter: parent.verticalCenter
            iconName: "subtitles"
            tooltip: qsTr("Subtitles")
            shortcut: row.shortcutFor("player.cycleSubtitle", "C")
            checked: row.panelKey === "tracks"
            onClicked: row.panelRequested("tracks", subtitleButton)

            KeyNavigation.left: audioButton
            KeyNavigation.right: chaptersButton
            KeyNavigation.up: row.focusAbove
        }

        StrmIconButton {
            id: chaptersButton

            anchors.verticalCenter: parent.verticalCenter
            iconName: "list"
            tooltip: qsTr("Chapters")
            enabled: row.hasChapters
            checked: row.panelKey === "chapters"
            onClicked: row.panelRequested("chapters", chaptersButton)

            KeyNavigation.left: subtitleButton
            KeyNavigation.right: queueButton
            KeyNavigation.up: row.focusAbove
        }

        StrmIconButton {
            id: queueButton

            anchors.verticalCenter: parent.verticalCenter
            iconName: "queue"
            tooltip: qsTr("Play queue")
            checked: row.panelKey === "queue"
            onClicked: row.panelRequested("queue", queueButton)

            KeyNavigation.left: chaptersButton
            KeyNavigation.right: settingsButton
            KeyNavigation.up: row.focusAbove
        }

        StrmIconButton {
            id: settingsButton

            anchors.verticalCenter: parent.verticalCenter
            iconName: "settings"
            tooltip: qsTr("Playback settings")
            checked: row.panelKey === "settings"
            onClicked: row.panelRequested("settings", settingsButton)

            KeyNavigation.left: queueButton
            KeyNavigation.right: statsButton
            KeyNavigation.up: row.focusAbove
        }

        StrmIconButton {
            id: statsButton

            anchors.verticalCenter: parent.verticalCenter
            iconName: "info"
            tooltip: qsTr("Playback statistics")
            shortcut: row.shortcutFor("player.stats", "Ctrl+I")
            checked: row.statsVisible
            onClicked: row.statsRequested()

            KeyNavigation.left: settingsButton
            KeyNavigation.right: fullscreenButton
            KeyNavigation.up: row.focusAbove
        }

        StrmIconButton {
            id: fullscreenButton

            anchors.verticalCenter: parent.verticalCenter
            iconName: row.fullscreen ? "fullscreen-exit" : "fullscreen"
            tooltip: row.fullscreen ? qsTr("Leave full screen") : qsTr("Full screen")
            shortcut: row.shortcutFor("app.fullscreen", "F11")
            onClicked: row.fullscreenRequested()

            KeyNavigation.left: statsButton
            KeyNavigation.up: row.focusAbove
        }
    }
}
