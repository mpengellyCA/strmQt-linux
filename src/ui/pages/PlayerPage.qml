import QtQuick
import QtQuick.Window
import StrmQt

// Full-screen playback: the engine's video plane plus PlayerOsd (ARCHITECTURE.md).
//
// This page used to *be* the OSD — a title, a stream-method string, a
// non-interactive progress bar and two timestamps. All of that moved into
// ui/player/, and what is left here is the three things only the page can own:
// the video plane, the session's error and loading surfaces, and input routing.
//
// Input model (PLAN §3.7), unchanged in meaning and now resolved entirely
// through InputMap so the M12 remap UI moves it:
//   · every player action comes from Input.actionForKey(key, mods, "player");
//   · auto-repeat is REJECTED for everything except seek and volume, where a
//     held key is exactly what the user means;
//   · Esc closes an open OSD panel before it stops playback.
//
// Mouse (ARCHITECTURE.md): motion wakes the OSD and the cursor, the cursor hides
// again with it, click toggles pause, double-click toggles full screen, and the
// wheel is volume.
//
// ── Audio mode (ARCHITECTURE.md) ─────────────────────────────────────────────
// Everything above describes a page built around a picture. For music every
// one of those decisions inverts: there is nothing to look at, so the chrome
// must not fade, and the content worth showing is the artwork, the track and
// what is next. `audioMode` below swaps the video surface for NowPlayingPanel
// and stands the OSD down for the duration. The video path is untouched:
// `audioMode` is false for every non-audio item and for a page with no session,
// which is exactly the state every existing binding here was written against.
FocusScope {
    id: page

    // Leave the player but keep playing (ARCHITECTURE.md).
    signal minimizeRequested

    // The page announces its slot rather than the owner reaching in for it:
    // `stack.currentItem` is a bare QQuickItem, so anything read off it is
    // invisible to qmllint and unchecked until it fails at runtime.
    signal videoSlotReady(Item slot)
    // The owner has to take the video plane back before this page goes away,
    // or it is destroyed with it and the picture does not come back. Ordinary
    // navigation reseats it earlier than this; this is the backstop.
    signal videoSlotReleasing

    // The shared sleeve is in the air (MUSIC.md §4). Passed through to the
    // now-playing panel, which owns the square this hides.
    property bool sleeveInFlight: false

    // The transition's large endpoint, in `target`'s coordinates; an empty rect
    // for a film, or before the panel has laid out. Main.qml polls this after
    // pushing the page, because on expand the page does not exist yet at the
    // moment the user asks for it.
    function sleeveRect(target: Item): rect {
        if (!page.audioMode)
            return Qt.rect(0, 0, 0, 0);
        return nowPlaying.heroArtRect(target);
    }

    readonly property real sleeveRadius: nowPlaying.heroArtRadius

    objectName: "playerPage" // Main.qml tests this to hide the chrome and to pop on stopped()

    Component.onCompleted: page.videoSlotReady(videoSlot)
    Component.onDestruction: page.videoSlotReleasing()

    focus: true

    // The window is the one piece of state the OSD cannot own, so the page
    // holds it and the OSD asks by signal.
    readonly property bool fullscreen: page.Window.window !== null
                                       && page.Window.window.visibility === Window.FullScreen

    function toggleFullscreen(): void {
        const win = page.Window.window;
        if (win === null)
            return;
        win.visibility = win.visibility === Window.FullScreen ? Window.Windowed
                                                             : Window.FullScreen;
    }

    // ── Is this a record or a film? ─────────────────────────────────────────
    // `PlayerCtl.isAudio` (MUSIC.md §4). This page used to derive the answer
    // itself from the queue entry's type with the media source as a fallback,
    // and the docked bar needed the same answer for the same reason — so the
    // derivation moved into PlayerController, where the queue and the ticket
    // both already live, and both surfaces now read one property.
    //
    // The ordering the controller applies is the one this page depended on and
    // is worth restating, because it is what stops a black video plane from
    // flipping to a now-playing page a second later: the item's own type wins
    // whenever the server gave one, and the source's streams are consulted only
    // for the bare play-by-id path, which is the only one that has no type.
    readonly property bool audioMode: PlayerCtl.isAudio === true

    // Crossing the boundary mid-session (a queue that runs from a track into a
    // music video, or a stop) must not leave the keyboard inside chrome that
    // just became invisible, nor leave an OSD panel open behind the now-playing
    // page where only Esc could find it.
    onAudioModeChanged: {
        osd.closePanel();
        osd.wake();
        page.forceActiveFocus(Qt.OtherFocusReason);
    }

    // Actions InputMap does not define yet carry the default this wave ships
    // with and pick up a real binding the moment the catalogue grows one —
    // the pattern Main.qml uses for app.shortcuts and app.commandPalette. A
    // literal is never the source of truth; it is only the fallback.
    component PlayerShortcut: Item {
        id: mapped

        property string actionId: ""
        property var fallback: []
        // Opt-in, and off by default for the same reason Keys.onPressed below
        // rejects auto-repeat: every action wired through here so far is a
        // toggle or a jump, and a held key machine-guns it. That guard cannot
        // help here — QShortcutMap matches before the key is ever delivered to
        // the page — so the default has to live on the component. A seek
        // shortcut added later genuinely wants a held key to keep firing; it
        // says so by setting this, rather than inheriting it by accident.
        property bool repeats: false
        property bool available: true

        signal activated

        Shortcut {
            sequences: {
                const bound = Input.bindings(mapped.actionId);
                return (bound !== undefined && bound.length > 0) ? bound : mapped.fallback;
            }
            enabled: PlayerCtl.active && mapped.available && page.visible
            autoRepeat: mapped.repeats
            onActivated: {
                Input.noteInput("keyboard");
                mapped.activated();
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "black" // The film's own letterbox, not a themed surface.
    }

    // Where the video plane sits while this page is up. The plane itself lives
    // in Main.qml and is moved in and out of here, because it must outlive the
    // page: freeing mpv's render context disables video for the file that is
    // loaded and mpv does not bring it back — a recreated context renders a
    // black frame, and neither vid=auto nor video-reload recovers it. A page
    // that owned the plane therefore cost the picture every time it was left.
    //
    // Still occupied while a record plays, only not drawn: there is nothing on
    // it, and hiding costs nothing where destroying costs the session.
    Item {
        id: videoSlot

        anchors.fill: parent
        visible: !page.audioMode
    }

    // ── Pointer (ARCHITECTURE.md) ──────────────────────────────────────────────
    // Below the OSD in the child order, so every control in the OSD gets the
    // press first and this only ever sees clicks on the film itself.
    MouseArea {
        id: videoArea

        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        // The cursor goes away with the OSD and comes back with it — except in
        // audio mode, where the OSD is stood down and hiding the pointer over a
        // page full of controls would be a bug rather than a courtesy.
        cursorShape: (page.audioMode || osd.shown) ? Qt.ArrowCursor : Qt.BlankCursor

        onPositionChanged: {
            Input.noteInput("mouse");
            osd.wake();
        }

        onClicked: {
            PlayerCtl.togglePause();
            osd.wake();
        }

        // Qt delivers clicked() for the first release and doubleClicked() on the
        // second press, so the pause above has already happened by the time we
        // get here: undo it, then toggle the window. That keeps a single click
        // instant instead of holding every pause behind a double-click timer.
        onDoubleClicked: {
            PlayerCtl.togglePause();
            page.toggleFullscreen();
            osd.wake();
        }

        onWheel: wheel => {
            Input.noteInput("mouse");
            const delta = wheel.angleDelta.y !== 0 ? wheel.angleDelta.y : wheel.angleDelta.x;
            if (delta !== 0)
                PlayerCtl.adjustVolume(delta > 0 ? 5 : -5);
            osd.wake();
        }
    }

    // ── Audio presentation ──────────────────────────────────────────────────
    // Above the pointer area rather than below it, so its own controls get the
    // press first; everything it does not claim — the artwork, the margins —
    // still falls through to videoArea, which is why click-to-pause, the volume
    // wheel and double-click-fullscreen keep working on a record too.
    NowPlayingPanel {
        id: nowPlaying

        anchors.fill: parent
        visible: page.audioMode
        enabled: page.audioMode
        sleeveInFlight: page.sleeveInFlight
        onLeaveRequested: page.minimizeRequested()
    }

    // ── Loading / buffering ─────────────────────────────────────────────────
    Text {
        anchors.centerIn: parent
        visible: PlayerCtl.busy || PlayerCtl.buffering
        text: PlayerCtl.busy ? qsTr("Loading…") : qsTr("Buffering…")
        color: Theme.textPrimaryColor
        font.family: Theme.fontBody
        font.pixelSize: Theme.fontTitle
    }

    // ── The OSD ─────────────────────────────────────────────────────────────
    PlayerOsd {
        id: osd

        anchors.fill: parent
        fullscreen: page.fullscreen
        // Stood down for a record. The OSD's whole premise is that it is chrome
        // over a picture, which is why it fades: three seconds after the last
        // keypress it gets out of the way of the film. On an audio page there
        // is no film to get out of the way of, so a fading scrubber would be
        // the only thing on screen disappearing for no gain — the requirement
        // that it "must not auto-hide" met by removing the reason it hides at
        // all. NowPlayingPanel is the permanent control surface instead, and it
        // carries the scrubber, the transport, shuffle/repeat, volume and the
        // queue that the OSD would have owned.
        visible: !page.audioMode

        onFullscreenRequested: page.toggleFullscreen()
        onLeaveRequested: page.minimizeRequested()
    }

    // ── Error surface ───────────────────────────────────────────────────────
    // Still the session's own message, still centred, still not a dialog: the
    // ladder may recover on its own, and a modal would be in the way when it
    // does. Esc stops, as it always did.
    Column {
        anchors.centerIn: parent
        width: parent.width * 0.7
        spacing: Theme.spacingTight
        visible: PlayerCtl.errorMessage.length > 0

        Text {
            width: parent.width
            text: PlayerCtl.errorMessage
            color: Theme.negative
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontBodySize
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
        }

        Text {
            width: parent.width
            text: qsTr("%1 — stop playback").arg(Input.binding("player.stop"))
            color: Theme.textTertiary
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontSmall
            horizontalAlignment: Text.AlignHCenter
        }
    }

    // ── Keyboard / gamepad ──────────────────────────────────────────────────
    Keys.onPressed: event => {
        Input.noteInput("keyboard");
        const action = Input.actionForKey(event.key, event.modifiers, "player");

        // Auto-repeat is the *wanted* behaviour for seek and volume — holding
        // Right must scrub — and is a bug everywhere else, where a held key
        // would machine-gun a toggle.
        const repeatable = action === "player.seekBackward" || action === "player.seekForward"
                        || action === "player.seekBackwardLong"
                        || action === "player.seekForwardLong"
                        || action === "player.volumeUp" || action === "player.volumeDown";
        if (event.isAutoRepeat && !repeatable) {
            event.accepted = true;
            return;
        }

        switch (action) {
        case "player.togglePause":
            PlayerCtl.togglePause();
            osd.wake();
            break;
        case "player.seekBackward":
            PlayerCtl.seekRelative(-10000);
            osd.wake();
            break;
        case "player.seekForward":
            PlayerCtl.seekRelative(10000);
            osd.wake();
            break;
        case "player.seekBackwardLong":
            PlayerCtl.seekRelative(-60000);
            osd.wake();
            break;
        case "player.seekForwardLong":
            PlayerCtl.seekRelative(60000);
            osd.wake();
            break;
        case "player.volumeUp":
            PlayerCtl.adjustVolume(5);
            osd.wake();
            break;
        case "player.volumeDown":
            PlayerCtl.adjustVolume(-5);
            osd.wake();
            break;
        case "player.cycleAudio":
            // Kept as the fast path now that TrackPanel is the real surface.
            PlayerCtl.cycleAudioTrack();
            osd.wake();
            break;
        case "player.cycleSubtitle":
            PlayerCtl.cycleSubtitleTrack();
            osd.wake();
            break;
        case "player.toggleOsd":
            osd.toggleOsd();
            break;
        case "player.frameNext":
            PlayerCtl.frameStep(1);
            break;
        case "player.framePrevious":
            PlayerCtl.frameStep(-1);
            break;
        case "player.screenshot":
            PlayerCtl.takeScreenshot();
            break;
        case "player.markLoop":
            PlayerCtl.markLoopPoint();
            break;
        case "player.minimize":
            // Leaves the page without ending the session; the mini player takes
            // over. Distinct from stop precisely because stop is destructive.
            //
            // Esc arrives here too now, not at player.stop. Ending a record
            // because someone pressed the key every other application spells
            // "go back" is a trap, and it was the only way off this page for a
            // pointer user: leaving is the reversible answer, and Stop is still
            // one key (S) and one button away for anyone who means it.
            //
            // A panel that could only be dismissed by leaving the page would
            // not be a panel, so an open one closes first. Audio mode stands
            // the OSD down entirely, and asking it anyway could silently
            // dismiss a panel nobody can see.
            if (!page.audioMode && osd.closeTopmost())
                break;
            page.minimizeRequested();
            break;
        case "player.stop":
            PlayerCtl.stop(); // Main pops the page on stopped()
            break;
        default:
            osd.wake();
            event.accepted = false;
            return;
        }
        event.accepted = true;
    }

    // Down is not a shortcut, it is structural navigation: it hands the
    // keyboard to the OSD's own controls, which then own the arrow keys until
    // Up walks back off the top of them. Handled here rather than in the map
    // for the same reason StrmTabBar owns Left/Right itself.
    Keys.onDownPressed: event => {
        if (page.audioMode)
            nowPlaying.focusScrubber();
        else
            osd.focusScrubber();
        event.accepted = true;
    }

    // Up out of the OSD's controls hands the keyboard back to the page, where
    // the arrows seek again — the other half of Down above. Suppressed while a
    // panel is open, so walking off the top of a list inside a panel cannot
    // strand the panel without focus.
    Keys.onUpPressed: event => {
        if (osd.panelKey.length > 0) {
            event.accepted = false;
            return;
        }
        page.forceActiveFocus(Qt.BacktabFocusReason);
        osd.wake();
        event.accepted = true;
    }

    PlayerShortcut {
        actionId: "player.stats"
        fallback: ["Ctrl+I"]
        available: !page.audioMode
        onActivated: osd.toggleStats()
    }

    PlayerShortcut {
        actionId: "player.next"
        fallback: ["Shift+N"]
        onActivated: {
            if (PlayerCtl.hasNext === true)
                PlayerCtl.playNext();
            osd.wake();
        }
    }

    PlayerShortcut {
        actionId: "player.previous"
        fallback: ["Shift+P"]
        onActivated: {
            PlayerCtl.playPrevious();
            osd.wake();
        }
    }

    PlayerShortcut {
        actionId: "player.nextChapter"
        fallback: ["]"]
        onActivated: {
            if (typeof PlayerCtl.nextChapter === "function")
                PlayerCtl.nextChapter();
            osd.wake();
        }
    }

    PlayerShortcut {
        actionId: "player.previousChapter"
        fallback: ["["]
        onActivated: {
            if (typeof PlayerCtl.previousChapter === "function")
                PlayerCtl.previousChapter();
            osd.wake();
        }
    }

    PlayerShortcut {
        actionId: "player.mute"
        fallback: ["M"]
        onActivated: {
            PlayerCtl.toggleMute();
            osd.wake();
        }
    }
}
