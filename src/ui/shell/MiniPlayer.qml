import QtQuick
import StrmQt

// MiniPlayer — the docked now-playing bar (ARCHITECTURE.md).
//
// The gap it closes: playback and the player *page* were the same thing. There
// was no way to keep something playing and go and browse, because the only way
// off the page was Esc, and Esc calls stop(). This bar is the other half of
// that: while a session is live and the player page is not on top, what is
// playing stays visible, controllable, and one click away from being full again.
//
// ── Contract with the owner (Main.qml) ────────────────────────────────────
// In:   `playerOnTop`  — true while the player page is the top of the stack.
// Out:  `reservedHeight` — the strip the page area must leave clear. It is
//                          animated, so a page anchored against it slides with
//                          the bar instead of jumping when it arrives.
//       `shown`        — the visibility rule, exposed for the owner to read.
//       `expandRequested` — the user asked for the full player back.
//       `dismissed`    — the keyboard left the bar; give focus back to the page.
//       `focusTransport()` — the owner's way to hand the keyboard *in*.
//
// Two things this file will not do, both deliberate:
//
//  · It never takes focus on its own. A bar that slid in under the cursor and
//    stole the keyboard from the grid you were arrowing through is precisely
//    the hover-≠-focus bug this project treats as a defect (ARCHITECTURE.md).
//    Focus enters only through a click or through focusTransport().
//  · It never covers content. `reservedHeight` is the whole mechanism: the
//    owner subtracts it from the page area, so the bar displaces rather than
//    overlaps. Anything that floats over the page would hide the last row of
//    every grid in the app.
//
// Tooltips here carry no keyboard shortcut on purpose. The InputMap bindings
// for play/pause and stop live in the *player* context, and this bar is only
// ever on screen in the *browse* context — printing a key that does nothing
// where the tooltip is legible would be worse than printing none.
FocusScope {
    id: mini

    // ── In ──────────────────────────────────────────────────────────────────
    property bool playerOnTop: false

    // ── Out ─────────────────────────────────────────────────────────────────
    signal expandRequested
    signal dismissed

    // Guarded the way every other PlayerCtl consumer in the tree is: a binding
    // that throws would take the whole bar — and therefore the page's bottom
    // margin — with it.
    readonly property bool active: PlayerCtl.active === true
    readonly property bool shown: mini.active && !mini.playerOnTop

    // Fixed metrics, declared here rather than read back off the children: the
    // root's implicitHeight is what the owner reserves, and deriving it from
    // items nested inside a Rectangle that is itself sized by it is the kind of
    // circle that only shows up on someone else's display scale.
    readonly property int scrubberHeight: Theme.scale(16)
    readonly property int contentHeight: Theme.scale(52)
    readonly property int barHeight: 1 + mini.scrubberHeight + mini.contentHeight
                                     + Theme.spacingTight
    // Animated, so the page area follows the bar rather than snapping once it
    // has arrived.
    readonly property int reservedHeight: Math.round(mini.barHeight * mini.slide)

    // 0 → fully docked away, 1 → fully in. One driver for the slide and the
    // fade: two animated properties on one Rectangle is the whole cost of this
    // thing appearing, which matters because it sits over every page.
    property real slide: mini.shown ? 1 : 0

    Behavior on slide {
        NumberAnimation {
            duration: Theme.animNormalMs
            easing.type: Theme.easeStandard
        }
    }

    implicitHeight: mini.barHeight
    // The bar slides out through the bottom edge; without this it would still
    // be painted over the page while off its own bounds.
    clip: true
    visible: mini.slide > 0.001
    enabled: mini.shown

    // ── Now-playing data ────────────────────────────────────────────────────
    // The queue is the only place the current item's artwork lives: the
    // controller publishes `title` and nothing visual. Reading it through
    // `currentIndex` (a notifying property) rather than through currentItem()
    // is what makes this a live binding instead of a snapshot.
    readonly property var nowItem: {
        const q = PlayerCtl.queue;
        if (q === undefined || q === null)
            return ({});
        if (q.currentIndex < 0 || q.count <= 0)
            return ({});
        return q.itemAt(q.currentIndex);
    }

    // Wide art is what a bar wants; the poster is the honest fallback, because
    // `thumbUrl` is empty for anything the server has no 16:9 image for.
    readonly property string artUrl: {
        const item = mini.nowItem;
        if (item.thumbUrl !== undefined && String(item.thumbUrl).length > 0)
            return String(item.thumbUrl);
        if (item.backdropUrl !== undefined && String(item.backdropUrl).length > 0)
            return String(item.backdropUrl);
        if (item.posterUrl !== undefined && String(item.posterUrl).length > 0)
            return String(item.posterUrl);
        return "";
    }
    readonly property bool artIsWide: {
        const item = mini.nowItem;
        return (item.thumbUrl !== undefined && String(item.thumbUrl).length > 0)
            || (item.backdropUrl !== undefined && String(item.backdropUrl).length > 0);
    }

    // PlayerCtl.title is MediaItemModel's label — "Series — S5E14 — Name" for an
    // episode — so the same split the OSD does gives this bar a subline without
    // the controller growing a second title property.
    readonly property var titleParts: {
        const raw = PlayerCtl.title !== undefined ? String(PlayerCtl.title) : "";
        const parts = raw.split(" — ");
        if (parts.length >= 3)
            return ({ "title": parts.slice(2).join(" — "),
                      "subline": parts[0] + "  ·  " + parts[1] });
        return ({ "title": raw, "subline": "" });
    }

    readonly property string subline: {
        if (mini.titleParts.subline.length > 0)
            return mini.titleParts.subline;
        const item = mini.nowItem;
        if (item.subtitle !== undefined && String(item.subtitle).length > 0)
            return String(item.subtitle);
        return "";
    }

    readonly property real positionMs: Number(PlayerCtl.positionMs)
    readonly property real durationMs: Number(PlayerCtl.durationMs)
    readonly property bool seekable: mini.durationMs > 0

    // bufferedMs is measured ahead of the playhead; StrmSlider wants an absolute
    // value on the same axis as `value`.
    readonly property real bufferedPosition: {
        const b = PlayerCtl.backend;
        if (b === undefined || b === null || b.bufferedMs === undefined)
            return 0;
        return Number(b.bufferedMs) + mini.positionMs;
    }

    function formatTime(ms: real): string {
        const totalSeconds = Math.max(0, Math.floor(ms / 1000));
        const hours = Math.floor(totalSeconds / 3600);
        const minutes = Math.floor((totalSeconds / 60) % 60);
        const seconds = totalSeconds % 60;
        const pad = v => (v < 10 ? "0" : "") + v;
        return hours > 0 ? hours + ":" + pad(minutes) + ":" + pad(seconds)
                         : minutes + ":" + pad(seconds);
    }

    readonly property string elapsedText: mini.formatTime(mini.positionMs)
    readonly property string remainingText: mini.seekable
        ? "−" + mini.formatTime(Math.max(0, mini.durationMs - mini.positionMs))
        : "--:--"

    // ── Keyboard entry and exit ─────────────────────────────────────────────
    // The owner's way in. Nothing in this file calls it by itself.
    function focusTransport(): void {
        if (mini.shown)
            playPause.forceActiveFocus(Qt.OtherFocusReason);
    }

    // A bar that vanished while holding the keyboard would leave focus nowhere,
    // which is a dead arrow key until the user clicks something.
    onShownChanged: {
        if (!mini.shown && mini.activeFocus)
            mini.dismissed();
    }

    Keys.onEscapePressed: event => {
        mini.dismissed();
        event.accepted = true;
    }

    // ── The bar ─────────────────────────────────────────────────────────────
    Rectangle {
        id: bar

        anchors.left: parent.left
        anchors.right: parent.right
        y: Math.round(mini.barHeight * (1 - mini.slide))
        height: mini.barHeight
        color: Theme.surfaceColor
        opacity: mini.slide

        Rectangle {
            id: edge

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 1
            color: Theme.hairline
        }

        // Full-bleed scrubber along the top edge, exactly where every player
        // that has ever had a docked bar puts it. Draggable, because a progress
        // bar you cannot seek with is a decoration.
        StrmSlider {
            id: scrubber

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: edge.bottom
            height: mini.scrubberHeight
            enabled: mini.seekable
            from: 0
            to: Math.max(1, mini.durationMs)
            value: mini.positionMs
            buffered: mini.bufferedPosition
            stepSize: 10000

            onCommitted: v => PlayerCtl.seekTo(Math.round(v))

            KeyNavigation.down: playPause
        }

        Item {
            id: content

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: scrubber.bottom
            anchors.leftMargin: Theme.spacingValue
            anchors.rightMargin: Theme.spacingValue
            height: mini.contentHeight

            // ── Left: art + what is playing. The whole block is the "go back
            // into the player" target for the pointer; the keyboard uses the
            // explicit button on the right rather than a second tab stop that
            // does the same thing.
            Item {
                id: identity

                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                height: Theme.scale(44)
                // Whatever is left of the centred transport. Derived from the
                // space, never from the labels' own width: a text block sized
                // by its parent cannot also size it.
                width: Math.max(0, transport.x - Theme.spacingValue)

                Rectangle {
                    id: artFrame

                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    height: parent.height
                    width: mini.artIsWide ? Math.round(height * 16 / 9)
                                          : Math.round(height * 2 / 3)
                    radius: Theme.radiusChip
                    clip: true
                    color: Theme.surfaceRaisedColor

                    Image {
                        anchors.fill: parent
                        source: mini.artUrl
                        // Ask the provider for the pixels this actually draws,
                        // in device pixels — a logical-pixel request comes back
                        // soft on any scaled display.
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
                }

                Column {
                    id: labels

                    anchors.left: artFrame.right
                    anchors.leftMargin: Theme.spacingTight
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.scale(2)

                    Text {
                        width: parent.width
                        text: mini.titleParts.title
                        color: identityHover.hovered ? Theme.textPrimaryColor
                                                     : Qt.darker(Theme.textPrimaryColor, 1.06)
                        font.family: Theme.fontDisplay
                        font.pixelSize: Theme.fontBodySize
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight

                        Behavior on color {
                            ColorAnimation {
                                duration: Theme.animInstant
                                easing.type: Theme.easeInstant
                            }
                        }
                    }

                    Text {
                        width: parent.width
                        visible: mini.subline.length > 0
                        text: mini.subline
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

            // ── Centre: transport ───────────────────────────────────────────
            Row {
                id: transport

                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingTight

                StrmIconButton {
                    id: prevButton

                    anchors.verticalCenter: parent.verticalCenter
                    size: Theme.scale(34)
                    iconName: "skip-previous"
                    tooltip: qsTr("Previous")
                    enabled: PlayerCtl.hasPrevious === true

                    onClicked: PlayerCtl.playPrevious()

                    KeyNavigation.right: playPause
                    KeyNavigation.up: scrubber
                }

                StrmIconButton {
                    id: playPause

                    anchors.verticalCenter: parent.verticalCenter
                    size: Theme.scale(40)
                    iconName: PlayerCtl.paused === true ? "play" : "pause"
                    tooltip: PlayerCtl.paused === true ? qsTr("Play") : qsTr("Pause")

                    onClicked: PlayerCtl.togglePause()

                    KeyNavigation.left: prevButton
                    KeyNavigation.right: nextButton
                    KeyNavigation.up: scrubber
                }

                StrmIconButton {
                    id: nextButton

                    anchors.verticalCenter: parent.verticalCenter
                    size: Theme.scale(34)
                    iconName: "skip-next"
                    tooltip: qsTr("Next")
                    enabled: PlayerCtl.hasNext === true

                    onClicked: PlayerCtl.playNext()

                    KeyNavigation.left: playPause
                    KeyNavigation.right: stopButton
                    KeyNavigation.up: scrubber
                }

                StrmIconButton {
                    id: stopButton

                    anchors.verticalCenter: parent.verticalCenter
                    size: Theme.scale(34)
                    iconName: "stop"
                    tooltip: qsTr("Stop")

                    onClicked: PlayerCtl.stop()

                    KeyNavigation.left: nextButton
                    KeyNavigation.right: expandButton
                    KeyNavigation.up: scrubber
                }
            }

            // ── Right: timecode + back into the player ──────────────────────
            Row {
                id: rightCluster

                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingTight

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: mini.elapsedText + "  /  " + mini.remainingText
                    color: Theme.textTertiary
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontCaption
                }

                StrmIconButton {
                    id: expandButton

                    anchors.verticalCenter: parent.verticalCenter
                    size: Theme.scale(34)
                    iconName: "chevron-up"
                    tooltip: qsTr("Open player")

                    onClicked: mini.expandRequested()

                    KeyNavigation.left: stopButton
                    KeyNavigation.up: scrubber
                }
            }
        }
    }
}
