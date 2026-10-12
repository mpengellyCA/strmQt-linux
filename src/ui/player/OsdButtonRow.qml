import QtQuick
import StrmQt

// OsdButtonRow — the player's transport (ARCHITECTURE.md).
//
// Jellyfin's videoosd bar, minus the things this client has no verb for:
// previous · −10 s · play/pause · +10 s · next · stop · previous/next chapter, then
// volume, audio, subtitles, chapters, queue, settings, stats and fullscreen.
//
// A control that cannot act is DISABLED, never hidden: a button that vanishes
// when there is no next episode teaches the user nothing and moves every other
// button under their cursor. Disabled buttons keep their place and their
// tooltip.
//
// A FocusScope with `focus: true` on play/pause, so the OSD can hand the
// keyboard to the row as a whole and land somewhere sensible. Left/Right walk
// the row by the neighbours each button names (`navLeft` / `navRight`, read by
// FocusNavigator); Up leaves it for `focusAbove` (the scrubber).
FocusScope {
    id: row

    // ── Contract ────────────────────────────────────────────────────────────
    property string panelKey: ""
    // Which page of the open panel is showing. Only the tracks panel has more
    // than one, and it is why audio and subtitles are two buttons over one
    // panel: without this both would light together and neither would say which
    // list is actually on screen.
    property int panelTab: 0
    property bool statsVisible: false
    property bool fullscreen: false
    property bool hasChapters: false
    // Where Up goes from any button in the row.
    property Item focusAbove: null

    // Any use of the row is user input; the OSD's idle timer restarts on it.
    signal woken
    // key is one of "tracks" | "chapters" | "queue" | "settings"; origin is the
    // button that asked, so focus can go back to it when the panel closes; tab
    // is the page of that panel the button means (0 for the single-page ones).
    signal panelRequested(string key, Item origin, int tab)
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

    // Up from any button. Said once here rather than as a `navUp` on each of
    // them: the page answers every Up its controls decline (that is how the
    // keyboard gets back to rest) before the navigator is ever asked, so a
    // name would not be read. A scrubber that cannot take focus — a stream
    // with no duration — is declined rather than swallowed, and Up then does
    // from here what it does from the scrubber.
    Keys.onUpPressed: event => {
        const above = row.focusAbove;
        if (above === null || !above.visible || !above.enabled) {
            event.accepted = false;
            return;
        }
        above.forceActiveFocus(Qt.BacktabFocusReason);
        event.accepted = true;
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
                                          || PlayerCtl.positionSeconds >= 5

    readonly property string volumeIcon: PlayerCtl.muted ? "volume-mute"
                                       : PlayerCtl.volume <= 0 ? "volume-mute"
                                       : PlayerCtl.volume < 40 ? "volume-low"
                                       : "volume-high"
    // At compact widths the transport stays intact and secondary panels move
    // behind one labelled menu. Nothing overlaps and every verb remains
    // available to pointer, keyboard, gamepad, and accessibility clients.
    readonly property bool compact: row.width < Theme.scale(960)

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

            property Item navRight: rewindButton
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

            property Item navLeft: previousButton
            property Item navRight: playPause
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

            property Item navLeft: rewindButton
            property Item navRight: forwardButton
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

            property Item navLeft: playPause
            property Item navRight: nextButton
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

            property Item navLeft: forwardButton
            property Item navRight: stopButton
        }

        // Stop is part of the transport, not a panel verb, so it stays in the
        // row at compact widths. Main pops the player page on stopped().
        StrmIconButton {
            id: stopButton

            anchors.verticalCenter: parent.verticalCenter
            iconName: "stop"
            tooltip: qsTr("Stop")
            shortcut: row.shortcutFor("player.stop", "S")
            onClicked: {
                row.woken();
                PlayerCtl.stop();
            }

            property Item navLeft: nextButton
            property Item navRight: row.compact ? muteButton : chapterPrevious
        }

        Item {
            width: Theme.spacingValue
            height: 1
            visible: !row.compact
        }

        StrmIconButton {
            id: chapterPrevious

            anchors.verticalCenter: parent.verticalCenter
            iconName: "chapter-previous"
            tooltip: qsTr("Previous chapter")
            shortcut: row.shortcutFor("player.previousChapter", "[")
            enabled: row.hasChapters
            visible: !row.compact
            onClicked: row.jumpChapter(false)

            property Item navLeft: stopButton
            property Item navRight: chapterNext
        }

        StrmIconButton {
            id: chapterNext

            anchors.verticalCenter: parent.verticalCenter
            iconName: "chapter-next"
            tooltip: qsTr("Next chapter")
            shortcut: row.shortcutFor("player.nextChapter", "]")
            enabled: row.hasChapters
            visible: !row.compact
            onClicked: row.jumpChapter(true)

            property Item navLeft: chapterPrevious
            property Item navRight: muteButton
        }
    }

    // ── Volume and panels ───────────────────────────────────────────────────
    Row {
        id: rightGroup

        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spacingTight

        // Volume: the mute button is the tab stop, the slider is the pointer's
        // affordance. It is deliberately OUT of the arrow chain —
        // StrmSlider consumes Left/Right for its own value, so leaving it in
        // would trap arrow navigation halfway along the row. The keyboard
        // adjusts volume with the player.volumeUp / player.volumeDown bindings
        // instead, which the page routes through InputMap.
        Item {
            id: volumeGroup

            readonly property bool expanded: volumeHover.hovered || muteButton.activeFocus
                                             || volumeSlider.dragging

            anchors.verticalCenter: parent.verticalCenter
            width: muteButton.width + (!row.compact && volumeGroup.expanded
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

                property Item navLeft: row.compact ? stopButton : chapterNext
                property Item navRight: row.compact ? fullscreenButton : audioButton
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
                accessibleName: qsTr("Volume")
                accessibleDescription: qsTr("%1 percent").arg(Math.round(value))

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
            // Lit only for its own half of the panel, never for the subtitle
            // list sharing it.
            checked: row.panelKey === "tracks" && row.panelTab === 0
            visible: !row.compact
            onClicked: row.panelRequested("tracks", audioButton, 0)

            property Item navLeft: muteButton
            property Item navRight: subtitleButton
        }

        StrmIconButton {
            id: subtitleButton

            anchors.verticalCenter: parent.verticalCenter
            iconName: "subtitles"
            tooltip: qsTr("Subtitles")
            shortcut: row.shortcutFor("player.cycleSubtitle", "C")
            checked: row.panelKey === "tracks" && row.panelTab === 1
            visible: !row.compact
            onClicked: row.panelRequested("tracks", subtitleButton, 1)

            property Item navLeft: audioButton
            property Item navRight: chaptersButton
        }

        StrmIconButton {
            id: chaptersButton

            anchors.verticalCenter: parent.verticalCenter
            iconName: "list"
            tooltip: qsTr("Chapters")
            enabled: row.hasChapters
            checked: row.panelKey === "chapters"
            visible: !row.compact
            onClicked: row.panelRequested("chapters", chaptersButton, 0)

            property Item navLeft: subtitleButton
            property Item navRight: queueButton
        }

        StrmIconButton {
            id: queueButton

            anchors.verticalCenter: parent.verticalCenter
            iconName: "queue"
            tooltip: qsTr("Play queue")
            checked: row.panelKey === "queue"
            visible: !row.compact
            onClicked: row.panelRequested("queue", queueButton, 0)

            property Item navLeft: chaptersButton
            property Item navRight: settingsButton
        }

        StrmIconButton {
            id: settingsButton

            anchors.verticalCenter: parent.verticalCenter
            iconName: "settings"
            tooltip: qsTr("Playback settings")
            checked: row.panelKey === "settings"
            visible: !row.compact
            onClicked: row.panelRequested("settings", settingsButton, 0)

            property Item navLeft: queueButton
            property Item navRight: statsButton
        }

        StrmIconButton {
            id: statsButton

            anchors.verticalCenter: parent.verticalCenter
            iconName: "info"
            tooltip: qsTr("Playback statistics")
            shortcut: row.shortcutFor("player.stats", "Ctrl+I")
            checked: row.statsVisible
            visible: !row.compact
            onClicked: row.statsRequested()

            property Item navLeft: settingsButton
            property Item navRight: fullscreenButton
        }

        StrmIconButton {
            id: fullscreenButton

            anchors.verticalCenter: parent.verticalCenter
            iconName: row.fullscreen ? "fullscreen-exit" : "fullscreen"
            tooltip: row.fullscreen ? qsTr("Leave full screen") : qsTr("Full screen")
            shortcut: row.shortcutFor("app.fullscreen", "F11")
            onClicked: row.fullscreenRequested()

            property Item navLeft: row.compact ? muteButton : statsButton
            property Item navRight: row.compact ? moreButton : null
        }

        StrmIconButton {
            id: moreButton

            anchors.verticalCenter: parent.verticalCenter
            visible: row.compact
            iconName: "more-horizontal"
            tooltip: qsTr("More playback actions")
            checked: compactMenu.opened || row.panelKey.length > 0 || row.statsVisible
            onClicked: {
                const p = moreButton.mapToItem(null, moreButton.width, 0);
                compactMenu.popupAt(p.x, p.y);
            }

            property Item navLeft: fullscreenButton
        }
    }

    StrmMenu {
        id: compactMenu

        parent: row
        actions: [
            ({ text: qsTr("Audio track"), iconName: "audio-track",
               checked: row.panelKey === "tracks" && row.panelTab === 0 }),
            ({ text: qsTr("Subtitles"), iconName: "subtitles",
               checked: row.panelKey === "tracks" && row.panelTab === 1 }),
            ({ text: qsTr("Chapters"), iconName: "list", enabled: row.hasChapters,
               checked: row.panelKey === "chapters" }),
            ({ text: qsTr("Play queue"), iconName: "queue",
               checked: row.panelKey === "queue" }),
            ({ text: qsTr("Playback settings"), iconName: "settings",
               checked: row.panelKey === "settings" }),
            ({ text: qsTr("Playback statistics"), iconName: "info",
               checked: row.statsVisible })
        ]
        onTriggered: index => {
            if (index === 0)
                row.panelRequested("tracks", moreButton, 0);
            else if (index === 1)
                row.panelRequested("tracks", moreButton, 1);
            else if (index === 2)
                row.panelRequested("chapters", moreButton, 0);
            else if (index === 3)
                row.panelRequested("queue", moreButton, 0);
            else if (index === 4)
                row.panelRequested("settings", moreButton, 0);
            else if (index === 5)
                row.statsRequested();
        }
    }
}
