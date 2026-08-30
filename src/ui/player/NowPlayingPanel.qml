// Bound: the queue delegate reaches out to this file's ids (panel, queueList),
// which is only well-defined — and only lint-clean — with bound component
// behaviour.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import QtQuick.Effects
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

    // ── In ──────────────────────────────────────────────────────────────────
    // The shared sleeve is in the air (MUSIC.md §4, "one continuous sleeve").
    // The hero hides ITS copy for the duration; the property is bound by the
    // owner and never set, so nothing can forget to undo it.
    property bool sleeveInFlight: false

    // ── Out ─────────────────────────────────────────────────────────────────
    // The pointer's way back to browsing without ending the record. A request,
    // not a command: this panel does not pop pages any more than a card does.
    signal leaveRequested
    readonly property bool compactGeometry: panel.width < Theme.scale(720)
                                            || panel.height < Theme.scale(560)
    readonly property bool shortLandscape: panel.width >= Theme.scale(600)
                                           && panel.height < Theme.scale(560)

    // The transition's large endpoint: where the sleeve lands, in `target`'s
    // coordinates. An empty rect while the pane has not laid out yet, which is
    // exactly the "not ready" answer the flight polls for.
    function heroArtRect(target: Item): rect {
        // The PANE is what says whether this has laid out, never the square:
        // the square is floored at Theme.scale(72) so that a pane with no
        // geometry at all still reports a 72 px sleeve, and a sentinel taken
        // off it would accept the very first tick and fly the cover to a stale
        // destination instead of waiting one more frame.
        if (!artFrame.visible || artArea.width <= 0 || artArea.height <= 0)
            return Qt.rect(0, 0, 0, 0);
        const corner = artFrame.mapToItem(target, 0, 0);
        return Qt.rect(corner.x, corner.y, artFrame.width, artFrame.height);
    }

    readonly property real heroArtRadius: artFrame.radius

    // ── Now-playing data ────────────────────────────────────────────────────
    // The queue is the only place the current item's metadata lives: the
    // controller publishes `title` and nothing else visual. Reading it through
    // `currentIndex` (a notifying property) rather than currentItem() is what
    // makes this a live binding instead of a snapshot — the same route
    // MiniPlayer takes, deliberately, rather than inventing a second one.
    readonly property var queueModel: NowPlayingInfo.queue
    readonly property var nowItem: NowPlayingInfo.item

    // Album art is square, so the poster is the right image — `thumbUrl` is a
    // 16:9 crop and only exists for things that have one at all.
    readonly property string artUrl: NowPlayingInfo.audioArtUrl

    // `name` is the bare track name; PlayerCtl.title is MediaItemModel's label,
    // which for a track is the same string. The queue entry wins because the
    // bare-playItem path seeds the queue from the title anyway, so there is no
    // case where the fallback is worse.
    readonly property string trackTitle: NowPlayingInfo.title

    readonly property string artistText: NowPlayingInfo.artistsText

    readonly property string albumText: NowPlayingInfo.albumName

    // ── Technical readout (MUSIC.md §4) ─────────────────────────────────────
    // "FLAC 24/96 · 2,304 kbps · Direct play", small and mono under the sleeve.
    // The detail that tells an audiophile the app respects them, and nearly
    // free: PlayerCtl already publishes the stream method and the chosen
    // source's streams, and this composes them the same way the OSD's tech
    // chips already do rather than inventing a second reading of the same data.
    //
    // The FIRST audio stream, not a selected one: a track has exactly one, and
    // where a file somehow has more the engine is playing the default, which is
    // the one the server lists first.
    readonly property var audioStream: NowPlayingInfo.audioStream
    readonly property string streamMethodLabel: NowPlayingInfo.streamMethodLabel
    readonly property string technicalText: NowPlayingInfo.audioTechnicalText

    // ── Up next, in context (MUSIC.md §4) ───────────────────────────────────
    // Where this queue came from. Derived by PlayQueue from what the queue
    // actually holds rather than remembered from the verb that built it: a
    // label carried from a click can outlive the queue it described, and this
    // one cannot be wrong about a queue it is reading.
    readonly property string queueContext: NowPlayingInfo.queueContext

    readonly property real positionMs: NowPlayingInfo.positionMs
    readonly property real positionSeconds: NowPlayingInfo.positionSeconds
    readonly property real durationMs: NowPlayingInfo.durationMs
    readonly property bool seekable: NowPlayingInfo.seekable

    // bufferedMs is measured ahead of the playhead; StrmSlider wants an
    // absolute value on the same axis as `value`.
    readonly property real bufferedPosition: NowPlayingInfo.bufferedPosition

    readonly property int repeatMode: panel.queueModel !== null
                                      ? Number(panel.queueModel.repeatMode) : 0
    readonly property bool shuffled: panel.queueModel !== null
                                     && panel.queueModel.shuffled === true

    readonly property string volumeIcon: (PlayerCtl.muted === true || PlayerCtl.volume <= 0)
                                         ? "volume-mute"
                                         : PlayerCtl.volume < 40 ? "volume-low" : "volume-high"

    // ── Helpers ─────────────────────────────────────────────────────────────
    function formatTime(ms: real): string {
        return NowPlayingInfo.formatTime(ms);
    }

    readonly property real endsAtEpochMinute: panel.durationMs > 0
        ? Math.floor((Date.now() + Math.max(0, panel.durationMs
                                            - panel.positionSeconds * 1000)) / 60000)
        : -1
    readonly property string endsAt: panel.endsAtEpochMinute >= 0
        ? Qt.formatTime(new Date(panel.endsAtEpochMinute * 60000), Locale.ShortFormat) : ""

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

    StrmImage {
        id: backdrop

        anchors.fill: parent
        source: panel.artUrl
        // A page-sized crop of a square image: ask for the pixels it draws, in
        // device pixels. A logical-pixel request comes back soft on any scaled
        // display (the lesson every card in this app learned the hard way).
        sourceSize.width: Math.round(Math.max(1, panel.width) * Screen.devicePixelRatio / 2)
        readyOpacity: 0.14
        fadeDuration: Theme.animSlow
    }

    Rectangle {
        anchors.fill: parent

        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.veil }
            GradientStop { position: 0.55; color: Theme.scrimColor }
            GradientStop { position: 1.0; color: Theme.ground }
        }
    }

    // Rule 2, the sleeve lights the room (MUSIC.md §4). Over the scrim rather
    // than under it: the scrim's job is to make the artwork behind it recede,
    // and a wash placed underneath would be the first thing it flattened. The
    // colour is sampled and CLAMPED in C++ — nothing here may brighten it.
    CoverWash {
        anchors.fill: parent
        source: panel.artUrl
    }

    // ── Panes ───────────────────────────────────────────────────────────────
    Item {
        id: content

        anchors.fill: parent
        anchors.margins: panel.compactGeometry ? Theme.spacingValue
                                               : Theme.pageMarginValue

        // The queue sits beside the artwork when there is room for both to be
        // legible, and underneath it when there is not. It is never dropped:
        // "what is next" is half the reason this page exists.
        readonly property bool sideBySide: content.width >= Theme.scale(900)
                                           || panel.shortLandscape
        readonly property int gap: Theme.spacingLoose
        // The queue takes a share, clamped: below the floor the track names are
        // all ellipsis, and above the ceiling it is a column of whitespace.
        readonly property int queueWidth: Math.round(
            Math.max(panel.compactGeometry ? Theme.scale(240) : Theme.scale(340),
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
                : Math.min(Math.max(0, content.height - content.gap - Theme.scale(140)),
                           Math.max(heroControls.implicitHeight + Theme.scale(72),
                                    Math.round(content.height * 0.58)))

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
                    visible: albumLabel.text.length > 0 && !panel.shortLandscape
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
                    accessibleName: qsTr("Playback position")
                    accessibleDescription: qsTr("%1 of %2")
                                           .arg(panel.formatTime(value))
                                           .arg(panel.formatTime(to))

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
                                                 ? positionLabel.scrubMs
                                                 : panel.positionSeconds * 1000)
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
                            enabled: PlayerCtl.hasPrevious === true || panel.positionSeconds >= 5
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
                            accessibleName: qsTr("Volume")
                            accessibleDescription: qsTr("%1 percent").arg(Math.round(value))

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

                readonly property bool hasArtRoom: !panel.shortLandscape
                    && artArea.width >= Theme.scale(72)
                    && artArea.height - artArea.readoutHeight >= Theme.scale(72)

                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: heroControls.top
                anchors.bottomMargin: Theme.spacingLoose

                // The readout's strip, taken out of the square's budget rather
                // than laid over it: the sleeve is allowed every pixel that is
                // left, and this is what "left" means once the underside has
                // its line.
                //
                // Measured off a TextMetrics, not off the laid-out Text — the
                // same reason MiniPlayer measures its clock that way. `technical`
                // is as wide as the square, the square is as wide as whatever is
                // left after this strip, so reading the Text's implicitHeight
                // here would route the square's own width back into its height.
                // It converges today only because an elided line with no
                // wrapMode has a height that does not depend on its width; add
                // wrapping, or a font whose metrics vary, and it is a live
                // binding loop. A TextMetrics has no width to close the circle
                // with. (Ceiled because that is what Text does to the same font
                // height: 16.328 → 17 for the mono caption, measured.)
                readonly property int readoutHeight: panel.technicalText.length > 0
                    ? Math.ceil(technicalMetrics.height) + Theme.spacingTight : 0

                TextMetrics {
                    id: technicalMetrics

                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontCaption
                    text: panel.technicalText
                }

                // The sleeve sits ON the surface, not in it (MUSIC.md §4).
                //
                // The shadow is cast by a plain rounded rectangle BEHIND the
                // frame, never by the frame itself, and both other ways of
                // writing it were tried against a real cover first:
                //
                //   · MultiEffect { source: artFrame } maps its auto-padded
                //     texture back into the frame's own rect, so the cover
                //     lands inset inside a black border;
                //   · layer.effect replaces the frame's rendering entirely, so
                //     an effect that does not run — a software or null RHI
                //     backend, say — takes the cover with it.
                //
                // A separate caster can do neither. It is fully covered by the
                // opaque frame, so what reaches the eye is only its shadow.
                Item {
                    id: sleeveShadow

                    x: artFrame.x
                    y: artFrame.y
                    width: artFrame.width
                    height: artFrame.height
                    visible: artFrame.visible && artFrame.opacity > 0

                    Rectangle {
                        id: shadowCaster

                        anchors.fill: parent
                        radius: artFrame.radius
                        color: Theme.shadowColor
                        // Drawn as well as sampled, for the reason StrmPanel
                        // states: a hidden source renders nothing into its
                        // layer on some paint paths.
                        layer.enabled: true
                    }

                    MultiEffect {
                        anchors.fill: parent
                        source: shadowCaster
                        autoPaddingEnabled: true
                        shadowEnabled: true
                        shadowColor: Theme.shadowColor
                        shadowBlur: Theme.elevation4.blur
                        shadowVerticalOffset: Theme.elevation4.y
                        shadowOpacity: Theme.elevation4.opacity
                    }
                }

                Rectangle {
                    id: artFrame

                    anchors.horizontalCenter: parent.horizontalCenter
                    y: Math.round((artArea.height - artArea.readoutHeight - artFrame.height) / 2)
                    width: artArea.hasArtRoom
                           ? Math.min(artArea.width, artArea.height - artArea.readoutHeight) : 0
                    height: artFrame.width
                    radius: Theme.radiusPanel
                    color: Theme.surfaceColor
                    border.width: 1
                    border.color: Theme.hairline
                    clip: true
                    // The square is in the air; its place is held, not drawn.
                    opacity: panel.sleeveInFlight ? 0 : 1
                    visible: artArea.hasArtRoom

                    StrmImage {
                        anchors.fill: parent
                        source: panel.artUrl
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

                // Rule 3: numerals are mono (MUSIC.md §4). Under the sleeve,
                // never wider than it, and quiet enough that it reads as a
                // caption on the record rather than a status line.
                Text {
                    id: technical

                    anchors.top: artFrame.bottom
                    anchors.topMargin: Theme.spacingTight
                    anchors.horizontalCenter: artFrame.horizontalCenter
                    width: artFrame.width
                    text: panel.technicalText
                    color: Theme.textTertiary
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontCaption
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                    visible: artFrame.visible && technical.text.length > 0
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
                height: Theme.controlHeight + (context.visible ? context.implicitHeight : 0)

                Text {
                    id: upNextLabel

                    anchors.left: parent.left
                    y: Math.round((Theme.controlHeight - upNextLabel.implicitHeight) / 2)
                    text: qsTr("Up next")
                    color: Theme.textPrimaryColor
                    font.family: Theme.fontDisplay
                    font.pixelSize: Theme.fontTitle
                    font.weight: Font.DemiBold
                }

                Text {
                    anchors.right: parent.right
                    anchors.verticalCenter: upNextLabel.verticalCenter
                    visible: queueList.count > 0
                    text: qsTr("%n track(s)", "", queueList.count)
                    color: Theme.textTertiary
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontCaption
                }

                // Where this queue came from. A queue with no single answer —
                // a hand-built one, a shuffle across the whole library — says
                // nothing rather than guessing.
                Text {
                    id: context

                    anchors.top: upNextLabel.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    visible: panel.queueContext.length > 0
                    text: panel.queueContext
                    color: Theme.textSecondaryColor
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontSmall
                    elide: Text.ElideRight
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

                Accessible.role: Accessible.List
                Accessible.name: qsTr("Up next")
                Accessible.focusable: true
                Accessible.focused: queueList.activeFocus

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

                    Accessible.role: Accessible.ListItem
                    Accessible.name: trackRow.model.name !== undefined
                                     ? String(trackRow.model.name) : ""
                    Accessible.description: rowSubtitle.text
                    Accessible.selectable: true
                    Accessible.selected: trackRow.highlighted || trackRow.playing
                    Accessible.focused: trackRow.highlighted
                    Accessible.onPressAction: panel.jumpTo(trackRow.index)

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

    // ── Back ────────────────────────────────────────────────────────────────
    // Declared last so it draws over the wash and the panes, and anchored to
    // the panel rather than to `content` so the compact layout's tighter
    // margins do not move it. Pointer-only, like the OSD's: Esc and Backspace
    // are the keyboard's route and the transport row owns the tab stops.
    StrmIconButton {
        id: leaveButton

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: Theme.spacingValue
        anchors.leftMargin: Theme.spacingValue
        iconName: "arrow-left"
        tooltip: qsTr("Back")
        accessibleName: qsTr("Leave the player, keep playing")
        activeFocusOnTab: false
        onClicked: panel.leaveRequested()
    }
}
