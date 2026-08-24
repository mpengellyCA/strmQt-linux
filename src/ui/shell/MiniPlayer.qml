import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import StrmQt

// MiniPlayer — the docked now-playing bar (ARCHITECTURE.md, MUSIC.md §4).
//
// The gap it closes: playback and the player *page* were the same thing. There
// was no way to keep something playing and go and browse, because the only way
// off the page was Esc, and Esc calls stop(). This bar is the other half of
// that: while a session is live and the player page is not on top, what is
// playing stays visible, controllable, and one click away from being full again.
//
// ── One component, two modes ──────────────────────────────────────────────
// `PlayerCtl.isAudio` chooses. It is a controller property, not a predicate
// re-derived here: the player page keys its entire layout off the same answer
// and two derivations of one fact is one more than the architecture allows.
//
//  · video — today's bar, unchanged: 16:9 art, prev/play/next/stop, timecode.
//  · audio — the sleeve dialect. The square at full bar height, flush left and
//    unrounded on the outer edge (Rule 1: the square is the unit); the artist
//    and album as *links* rather than dead text; shuffle and repeat, because
//    they are queue state and this is where the queue is controlled; favourite,
//    because music favouriting happens per track and constantly; and a queue
//    peek. Stop is gone — for music, stop is pause plus forgetting where you
//    were, and nobody wants it. It stays in the video bar where it belongs.
//
// ── Contract with the owner (Main.qml) ────────────────────────────────────
// In:   `playerOnTop`  — true while the player page is the top of the stack.
// Out:  `reservedHeight` — the strip the page area must leave clear. It is
//                          animated, so a page anchored against it slides with
//                          the bar instead of jumping when it arrives.
//       `shown`        — the visibility rule, exposed for the owner to read.
//       `expandRequested` — the user asked for the full player back.
//       `artistRequested` / `albumRequested` — a link in the subline was
//                          followed. Requests, not commands: this file does not
//                          push pages, the same way no page does.
//       `dismissed`    — the keyboard left the bar; give focus back to the page.
//       `focusTransport()` — the owner's way to hand the keyboard *in*.
//
// ── One tab stop, not N (ARCHITECTURE.md §4) ──────────────────────────────
// The bar carries up to ten controls and is a single item in the tab chain.
// Every one of them sets `activeFocusOnTab: false` and the FocusScope root
// carries the stop; Left/Right walks the strip and Up reaches the scrubber,
// exactly as the alphabet bar and the season tabs do. This is what stopped the
// audio bar from putting six dead stops between a grid and whatever is after it.
//
// KeyNavigation does the mode switching by itself: the chain is declared once,
// straight through every control in visual order, and Qt skips the links that
// have no id and the buttons the current mode hides.
//
// Two things this file will not do, both deliberate:
//
//  · It never takes focus on its own. A bar that slid in under the cursor and
//    stole the keyboard from the grid you were arrowing through is precisely
//    the hover-≠-focus bug this project treats as a defect (ARCHITECTURE.md).
//    Focus enters only through a click, through Tab, or through focusTransport().
//  · It never covers content. `reservedHeight` is the whole mechanism: the
//    owner subtracts it from the page area, so the bar displaces rather than
//    overlaps — in both modes, because the audio square is sized to the strip
//    the bar already reserved rather than the strip being grown to fit it.
//    Anything that floats over the page would hide the last row of every grid
//    in the app. The queue peek is the one exception and it is a Popup, so it
//    lives in the window's overlay and is dismissed by clicking away from the
//    bar — the bar itself, because the button that toggles it stands on it.
//
// Tooltips here carry no keyboard shortcut on purpose. The InputMap bindings
// for play/pause and stop live in the *player* context, and this bar is only
// ever on screen in the *browse* context — printing a key that does nothing
// where the tooltip is legible would be worse than printing none.
FocusScope {
    id: mini

    // ── In ──────────────────────────────────────────────────────────────────
    property bool playerOnTop: false
    // The shared sleeve is in the air (MUSIC.md §4, "one continuous sleeve").
    // The bar hides ITS copy for the duration so there is only one square on
    // screen. Bound by the owner, never set: a binding cannot forget to undo
    // itself, and a cover stuck invisible is worse than no animation.
    property bool sleeveInFlight: false

    // ── Out ─────────────────────────────────────────────────────────────────
    signal expandRequested
    signal dismissed
    signal artistRequested(string artistId, string name)
    signal albumRequested(string albumId, string name)

    // Guarded the way every other PlayerCtl consumer in the tree is: a binding
    // that throws would take the whole bar — and therefore the page's bottom
    // margin — with it.
    readonly property bool active: PlayerCtl.active === true
    readonly property bool shown: mini.active && !mini.playerOnTop
    // The one answer, shared with PlayerPage (MUSIC.md §4).
    readonly property bool isAudio: PlayerCtl.isAudio === true
    readonly property bool compactWidth: mini.width < Theme.scale(900)
    readonly property bool narrowWidth: mini.width < Theme.scale(620)

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
    // Room between the top of the bar and the top of the window: what the queue
    // peek is allowed to grow into. Read here rather than at the use site
    // because qmllint cannot resolve an attached type through an id from inside
    // a Popup's bindings, and a warning it cannot see through is still a warning.
    readonly property int spaceAboveBar: mini.Window.height - mini.barHeight

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
    // The single tab stop. Tab lands on the scope, the scope hands the keyboard
    // to whichever control it was last left on (playPause to begin with), and
    // Left/Right walks the rest without ever leaving the stop.
    activeFocusOnTab: true

    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Now playing")
    Accessible.description: mini.trackTitle

    // ── Now-playing data ────────────────────────────────────────────────────
    // The queue is the only place the current item's artwork and its music
    // identity live: the controller publishes `title` and nothing else visual.
    // Reading it through `currentIndex` (a notifying property) rather than
    // through currentItem() is what makes this a live binding, not a snapshot.
    readonly property var nowItem: NowPlayingInfo.item
    readonly property string nowItemId: NowPlayingInfo.itemId
    readonly property var queueModel: NowPlayingInfo.queue

    // Rule 1, the square is the unit (MUSIC.md §4). A track's art is 1:1 and it
    // fills; `thumbUrl` is a 16:9 crop that a track does not have and an album
    // does not want. `posterUrl` is read and never rebuilt by hand —
    // MediaItem::coverSource() already resolves a track to its *album's* cover
    // rather than to whatever the ripper embedded, and reconstructing an image
    // URL here would throw that away.
    readonly property string artUrl: NowPlayingInfo.artUrl
    readonly property bool artIsWide: NowPlayingInfo.videoArtIsWide

    // Video metadata comes from structured queue roles, keeping punctuation in
    // the view layer instead of parsing a rendered label back into fields.
    //
    // Music does not go anywhere near it. A track's context was structured
    // before it was ever composed into a label, and PlayQueue::itemFromVariant
    // carries `album`, `albumId`, `albumArtist`, `artists` and `artistIds`
    // through the round trip precisely so this bar can read the fields instead
    // of guessing at a display string.
    readonly property string trackTitle: NowPlayingInfo.title

    // The bar shows ONE credit, not the whole cast: "Godspeed You! Black
    // Emperor" already fills the line it is on, and the link has one
    // destination anyway. The rest of the credit is on the album page the link
    // leads to.
    //
    // WHICH credit is not decided here. ItemActions.artistTarget() owns that
    // rule and the context menu's "Go to artist" follows the same one: the
    // album artist over the first performer, because an artist page is a
    // discography and a compilation track's first performer opens a page with
    // no albums on it. It also hands back a name and an id that describe the
    // same artist — reading `artists[0]` and `artistIds[0]` as a pair did not,
    // because the two lists are only aligned where the server has an artist
    // item for every credited name.
    readonly property var artistTarget: NowPlayingInfo.artistTarget
    readonly property string artistText: NowPlayingInfo.artistName
    readonly property string artistId: NowPlayingInfo.artistId
    readonly property string albumText: NowPlayingInfo.albumName
    readonly property string albumId: NowPlayingInfo.albumId

    // The video bar's second line, which is a caption rather than a route.
    readonly property string subline: NowPlayingInfo.videoContext

    // ── Queue state ─────────────────────────────────────────────────────────
    // PlayQueue already owns shuffle and repeat — per-entry keys, a shuffle
    // that restores the original order rather than re-sorting it, three repeat
    // modes. These read and drive that; there is no second implementation here.
    readonly property bool shuffled: mini.queueModel !== null
                                     && mini.queueModel.shuffled === true
    // 0 off · 1 all · 2 one, matching PlayQueue::RepeatMode.
    readonly property int repeatMode: mini.queueModel !== null
                                      ? Number(mini.queueModel.repeatMode) : 0

    function toggleShuffle(): void {
        if (mini.queueModel !== null)
            mini.queueModel.shuffled = !mini.queueModel.shuffled;
    }

    function cycleRepeat(): void {
        if (mini.queueModel !== null)
            mini.queueModel.cycleRepeatMode();
    }

    // ── Favourite ───────────────────────────────────────────────────────────
    // The queue's rows are not registered with ItemActions — PlayQueue keeps a
    // MediaItemModel of its own for role parity and nothing hands it over — so
    // the optimistic patch a toggle applies never reaches this map. Same shape
    // AlbumPage uses for the same reason: the role is the baseline (what the
    // server said when the entry was queued), the overlay is what has changed
    // since, and it is REPLACED rather than mutated because a mutated object
    // notifies nothing and the heart would keep its old state.
    property var favoriteOverrides: ({})

    readonly property bool nowFavorite: {
        const id = mini.nowItemId;
        if (id.length === 0)
            return false;
        if (mini.favoriteOverrides[id] !== undefined)
            return mini.favoriteOverrides[id] === true;
        return mini.nowItem.favorite === true;
    }

    Connections {
        target: Actions

        function onFavoriteChanged(itemId, favorite) {
            const next = Object.assign({}, mini.favoriteOverrides);
            next[itemId] = favorite;
            mini.favoriteOverrides = next;
        }
    }

    Connections {
        target: Session

        function onSessionBoundaryChanged() {
            mini.favoriteOverrides = ({});
        }
    }

    function toggleFavorite(): void {
        if (mini.nowItemId.length > 0)
            Actions.toggleFavorite(mini.nowItem);
    }

    // ── Navigation requests ─────────────────────────────────────────────────
    function openArtist(): void {
        if (mini.artistId.length > 0)
            mini.artistRequested(mini.artistId, mini.artistText);
    }

    function openAlbum(): void {
        if (mini.albumId.length > 0)
            mini.albumRequested(mini.albumId, mini.albumText);
    }

    readonly property real positionMs: NowPlayingInfo.positionMs
    readonly property real durationMs: NowPlayingInfo.durationMs
    readonly property bool seekable: NowPlayingInfo.seekable

    // bufferedMs is measured ahead of the playhead; StrmSlider wants an absolute
    // value on the same axis as `value`.
    readonly property real bufferedPosition: NowPlayingInfo.bufferedPosition

    function formatTime(ms: real): string {
        return NowPlayingInfo.formatTime(ms);
    }

    readonly property string elapsedText: NowPlayingInfo.elapsedText
    readonly property string remainingText: NowPlayingInfo.remainingText
    readonly property string timeText: NowPlayingInfo.timeText

    // Rule 3, numerals line up (MUSIC.md §4). Mono with tabular figures gives
    // every digit the same advance, which stops "1:12" turning into "1:13" a
    // pixel wider — but the readout still grows a whole character crossing
    // 9:59 → 10:00, and a right-aligned box of a KNOWN width is what stops
    // that, too. The template is this item's own longest form with every digit
    // replaced, so it measures exactly what will be drawn into it.
    //
    // Measured against the longer of the two clocks, not against the duration:
    // a live stream — or any source whose duration never resolves — leaves
    // `durationMs` at 0 while the elapsed side keeps counting, and a box
    // measured for "0:00" with "1:23:45  /  --:--" drawn right-aligned into it
    // paints three characters outside its own bounds. The right-hand side is
    // taken verbatim when there is no remaining time to show, because "--:--"
    // is what is drawn there and it is shorter than the clock it replaces.
    readonly property string timeTemplate: {
        const shape = mini.formatTime(Math.max(mini.durationMs,
                                                NowPlayingInfo.positionSeconds * 1000))
                          .replace(/[0-9]/g, "0");
        return shape + "  /  " + (mini.seekable ? "−" + shape : mini.remainingText);
    }

    TextMetrics {
        id: timeMetrics

        font.family: Theme.fontMono
        font.pixelSize: Theme.fontCaption
        font.features: ({ "tnum": 1 })
        text: mini.timeTemplate
    }

    // ── Keyboard entry and exit ─────────────────────────────────────────────
    // Where the keyboard was when the queue peek went up, in the shape
    // StrmMenu._focusEscrow uses — and typed Item for the same reason, so QML
    // nulls it for us if the item is destroyed while the peek is open.
    property Item peekFocusEscrow: null

    // Taken on the peek's aboutToShow rather than its onOpened: the popup takes
    // focus when its enter transition finishes, so by `opened` the item worth
    // remembering is already the one being restored from.
    function notePeekFocus(): void {
        mini.peekFocusEscrow = mini.Window.activeFocusItem;
    }

    // The transition's small endpoint (MUSIC.md §4). Where the square sits when
    // the bar is fully docked, in `target`'s coordinates.
    //
    // Docked, not current: by the time the full-screen view asks for this the
    // bar is already sliding out from under the player page, and the sleeve has
    // to leave from — and return to — the place the eye last saw it rather than
    // wherever the strip has slid to since. `bar.y` is the slide, so taking it
    // back off is the whole correction.
    function dockedArtRect(target: Item): rect {
        const corner = artFrame.mapToItem(target, 0, 0);
        return Qt.rect(corner.x, corner.y - bar.y, artFrame.width, artFrame.height);
    }

    readonly property real artRadius: artFrame.radius

    // The owner's way in. Nothing in this file calls it by itself.
    function focusTransport(): void {
        if (mini.shown)
            playPause.forceActiveFocus(Qt.OtherFocusReason);
    }

    // A bar that vanished while holding the keyboard would leave focus nowhere,
    // which is a dead arrow key until the user clicks something.
    onShownChanged: {
        if (mini.shown)
            return;
        // The peek renders in the window's overlay rather than inside the bar,
        // so it does not leave with it: hiding a popup's parent leaves the popup
        // open and floating (measured, Qt 6.11). It has to be dismissed here, or
        // expanding into the player page from a bar with the peek open would
        // strand a queue popover over the page that replaced it.
        queuePeek.close();
        if (mini.activeFocus)
            mini.dismissed();
    }

    Keys.onEscapePressed: event => {
        mini.dismissed();
        event.accepted = true;
    }

    // ── A link in the subline ───────────────────────────────────────────────
    // Not LinkChip: that is a pill sized for a details page and it deliberately
    // refuses focus, because it is built for a view that owns the arrow keys
    // and drives `highlighted`. Here the strip IS the view that owns the arrow
    // keys, so the link takes focus like every other control on the bar and
    // stays out of the tab chain like every other control on the bar.
    component MiniLink: Item {
        id: link

        property string label: ""
        property int maxWidth: 0
        // False → there is no id to go to. Render the text and drop every
        // affordance, exactly as LinkChip does: something that looks clickable
        // and does nothing is worse than plain text.
        property bool linked: true

        signal activated

        readonly property bool interactive: link.linked && link.enabled
        readonly property bool lit: linkHover.hovered || link.activeFocus

        visible: link.label.length > 0
        activeFocusOnTab: false
        implicitHeight: linkLabel.implicitHeight
        implicitWidth: link.maxWidth > 0 ? Math.min(linkLabel.implicitWidth, link.maxWidth)
                                         : linkLabel.implicitWidth

        Accessible.role: link.interactive ? Accessible.Link : Accessible.StaticText
        Accessible.name: link.label
        Accessible.focusable: link.interactive
        Accessible.focused: link.activeFocus
        Accessible.onPressAction: link.activate()

        function activate(): void {
            if (link.interactive)
                link.activated();
        }

        Text {
            id: linkLabel

            anchors.fill: parent
            text: link.label
            color: (link.interactive && link.lit) ? Theme.textPrimaryColor
                                                  : Theme.textSecondaryColor
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontCaption
            font.underline: link.interactive && link.lit
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter

            Behavior on color {
                ColorAnimation {
                    duration: Theme.animInstant
                    easing.type: Theme.easeInstant
                }
            }
        }

        FocusRing {
            active: link.activeFocus
            radius: Theme.radiusChip
            inset: -Theme.scale(2)
        }

        HoverHandler {
            id: linkHover
            enabled: link.interactive
            cursorShape: Qt.PointingHandCursor
        }

        TapHandler {
            enabled: link.interactive
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: {
                Input.noteInput("mouse");
                link.forceActiveFocus(Qt.MouseFocusReason);
                link.activated();
            }
        }

        Keys.onReturnPressed: event => {
            if (!event.isAutoRepeat)
                link.activate();
        }
        Keys.onEnterPressed: event => {
            if (!event.isAutoRepeat)
                link.activate();
        }
        Keys.onSpacePressed: event => {
            if (!event.isAutoRepeat)
                link.activate();
        }
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

        // ── Input floor ─────────────────────────────────────────────────
        // Every control on this bar is a TapHandler on a plain Item, and a
        // TapHandler takes only a PASSIVE grab on press: it never accepts the
        // event on its item's behalf. A plain Rectangle accepts no mouse events
        // at all. So a press anywhere on the bar carried on to whatever was
        // behind it — clicking the now-playing bar played the card underneath
        // it and followed entries in the rail.
        //
        // Declared FIRST so it is at the bottom of the bar's stacking order:
        // every control above still gets the press before this ever sees it,
        // and this only swallows what none of them claimed. The wheel goes with
        // it, because a bar that scrolled the grid behind it is the same bug
        // wearing a different hat.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            // Deliberately not hoverEnabled: the bar must not take hover away
            // from anything, it only has to stop events falling through it.
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
            activeFocusOnTab: false
            from: 0
            to: Math.max(1, mini.durationMs)
            value: mini.positionMs
            buffered: mini.bufferedPosition
            stepSize: 10000
            accessibleName: qsTr("Playback position")
            accessibleDescription: qsTr("%1 of %2")
                                   .arg(mini.formatTime(value))
                                   .arg(mini.formatTime(to))

            onCommitted: v => PlayerCtl.seekTo(Math.round(v))

            KeyNavigation.down: playPause
        }

        Item {
            id: content

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: scrubber.bottom
            // Flush left in audio mode: the sleeve runs into the edge of the
            // bar. The margin the identity block loses is given back to the
            // labels, so only the square moves.
            anchors.leftMargin: mini.isAudio ? 0 : Theme.spacingValue
            anchors.rightMargin: Theme.spacingValue
            // Audio takes every row the bar has left under the playhead, so the
            // square reaches the bottom edge as well as the left one; video
            // keeps the breathing space it has always had beneath the strip.
            // Either way this is the height the bar already reserved — the bar
            // is never grown to fit the sleeve.
            height: mini.isAudio ? mini.barHeight - 1 - mini.scrubberHeight
                                 : mini.contentHeight

            // ── Left: art + what is playing ─────────────────────────────────
            // The art and the title are the "go back into the player" target
            // for the pointer; the keyboard uses the explicit button on the
            // right rather than a second stop that does the same thing. The
            // subline is NOT part of that target — in audio mode it carries two
            // links of its own and a tap there means "go to the record", not
            // "open the player".
            Item {
                id: identity

                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                // Whatever is left of the centred transport. Derived from the
                // space, never from the labels' own width: a text block sized
                // by its parent cannot also size it.
                width: Math.max(0, transport.x - Theme.spacingTight)

                Rectangle {
                    id: artFrame

                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    // Full bar height and square for a record; the smaller
                    // inset frame the video bar has always drawn otherwise.
                    height: mini.isAudio ? parent.height : Theme.scale(44)
                    width: mini.isAudio ? height
                         : mini.artIsWide ? Math.round(height * 16 / 9)
                                          : Math.round(height * 2 / 3)
                    radius: mini.isAudio ? 0 : Theme.radiusChip
                    clip: true
                    color: Theme.surfaceRaisedColor
                    // Its geometry stays — the labels anchor to its right edge —
                    // but the square itself is in the air.
                    opacity: mini.sleeveInFlight ? 0 : 1

                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Open player: %1").arg(mini.trackTitle)
                    Accessible.onPressAction: mini.expandRequested()

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

                    HoverHandler {
                        id: artHover
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

                Column {
                    id: labels

                    anchors.left: artFrame.right
                    anchors.leftMargin: Theme.spacingValue
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.scale(2)

                    Item {
                        id: titleLine

                        width: parent.width
                        height: titleText.implicitHeight

                        Accessible.role: Accessible.Button
                        Accessible.name: qsTr("Open player: %1").arg(mini.trackTitle)
                        Accessible.onPressAction: mini.expandRequested()

                        Text {
                            id: titleText

                            anchors.fill: parent
                            text: mini.trackTitle
                            color: (titleHover.hovered || artHover.hovered)
                                   ? Theme.textPrimaryColor
                                   : Qt.darker(Theme.textPrimaryColor, 1.06)
                            font.family: Theme.fontDisplay
                            font.pixelSize: Theme.fontBodySize
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter

                            Behavior on color {
                                ColorAnimation {
                                    duration: Theme.animInstant
                                    easing.type: Theme.easeInstant
                                }
                            }
                        }

                        HoverHandler {
                            id: titleHover
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

                    // Audio: the credit and the record, both of which go
                    // somewhere. Video: the caption the label split produced,
                    // which does not.
                    Row {
                        id: musicSubline

                        width: parent.width
                        visible: mini.isAudio && !mini.narrowWidth
                        spacing: Theme.spacingTight

                        MiniLink {
                            id: artistLink

                            anchors.verticalCenter: parent.verticalCenter
                            label: mini.artistText
                            linked: mini.artistId.length > 0
                            // Half the line each, less the separator, so a long
                            // artist cannot elide the album out of existence.
                            maxWidth: Math.max(0, Math.round(
                                (musicSubline.width - separator.implicitWidth
                                 - 2 * musicSubline.spacing) / 2))

                            onActivated: mini.openArtist()

                            KeyNavigation.right: albumLink
                            KeyNavigation.up: scrubber
                        }

                        Text {
                            id: separator

                            anchors.verticalCenter: parent.verticalCenter
                            visible: artistLink.visible && albumLink.visible
                            text: "·"
                            color: Theme.textTertiary
                            font.family: Theme.fontBody
                            font.pixelSize: Theme.fontCaption
                        }

                        MiniLink {
                            id: albumLink

                            anchors.verticalCenter: parent.verticalCenter
                            label: mini.albumText
                            linked: mini.albumId.length > 0
                            maxWidth: Math.max(0, musicSubline.width - separator.implicitWidth
                                                  - artistLink.implicitWidth
                                                  - 2 * musicSubline.spacing)

                            onActivated: mini.openAlbum()

                            KeyNavigation.left: artistLink
                            KeyNavigation.right: prevButton
                            KeyNavigation.up: scrubber
                        }
                    }

                    Text {
                        width: parent.width
                        visible: !mini.isAudio && !mini.narrowWidth && mini.subline.length > 0
                        text: mini.subline
                        color: Theme.textSecondaryColor
                        font.family: Theme.fontBody
                        font.pixelSize: Theme.fontCaption
                        elide: Text.ElideRight
                    }
                }
            }

            // ── Centre: transport ───────────────────────────────────────────
            // Declared in one order and shown in two: a Row skips its invisible
            // children, and so does KeyNavigation, so the chain below runs
            // straight through both modes without a ternary in sight.
            Row {
                id: transport

                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingTight

                StrmIconButton {
                    id: prevButton

                    anchors.verticalCenter: parent.verticalCenter
                    size: Theme.scale(34)
                    activeFocusOnTab: false
                    iconName: "skip-previous"
                    tooltip: qsTr("Previous")
                    enabled: PlayerCtl.hasPrevious === true

                    onClicked: PlayerCtl.playPrevious()

                    KeyNavigation.left: albumLink
                    KeyNavigation.right: playPause
                    KeyNavigation.up: scrubber
                }

                StrmIconButton {
                    id: playPause

                    anchors.verticalCenter: parent.verticalCenter
                    size: Theme.scale(40)
                    // The bar's resting place for the keyboard: Tab lands on
                    // the scope, the scope hands it here.
                    focus: true
                    activeFocusOnTab: false
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
                    activeFocusOnTab: false
                    iconName: "skip-next"
                    tooltip: qsTr("Next")
                    enabled: PlayerCtl.hasNext === true

                    onClicked: PlayerCtl.playNext()

                    KeyNavigation.left: playPause
                    KeyNavigation.right: shuffleButton
                    KeyNavigation.up: scrubber
                }

                // Queue state is not transport, and the gap says so.
                Item {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: mini.isAudio && !mini.compactWidth
                    width: Theme.spacingValue
                    height: 1
                }

                StrmIconButton {
                    id: shuffleButton

                    anchors.verticalCenter: parent.verticalCenter
                    size: Theme.scale(34)
                    visible: mini.isAudio && !mini.compactWidth
                    activeFocusOnTab: false
                    iconName: "shuffle"
                    tooltip: mini.shuffled ? qsTr("Shuffle on") : qsTr("Shuffle off")
                    checked: mini.shuffled
                    enabled: mini.queueModel !== null

                    onClicked: mini.toggleShuffle()

                    KeyNavigation.left: nextButton
                    KeyNavigation.right: repeatButton
                    KeyNavigation.up: scrubber
                }

                StrmIconButton {
                    id: repeatButton

                    anchors.verticalCenter: parent.verticalCenter
                    size: Theme.scale(34)
                    visible: mini.isAudio && !mini.compactWidth
                    activeFocusOnTab: false
                    iconName: mini.repeatMode === 2 ? "repeat-one" : "repeat"
                    tooltip: mini.repeatMode === 0 ? qsTr("Repeat off")
                           : mini.repeatMode === 1 ? qsTr("Repeat all")
                           : qsTr("Repeat one")
                    checked: mini.repeatMode !== 0
                    enabled: mini.queueModel !== null

                    onClicked: mini.cycleRepeat()

                    KeyNavigation.left: shuffleButton
                    KeyNavigation.right: stopButton
                    KeyNavigation.up: scrubber
                }

                // Stop belongs to the video bar (MUSIC.md §4): for music it is
                // pause plus forgetting where you were, and nobody wants it.
                StrmIconButton {
                    id: stopButton

                    anchors.verticalCenter: parent.verticalCenter
                    size: Theme.scale(34)
                    visible: !mini.isAudio
                    activeFocusOnTab: false
                    iconName: "stop"
                    tooltip: qsTr("Stop")

                    onClicked: PlayerCtl.stop()

                    KeyNavigation.left: repeatButton
                    KeyNavigation.right: favoriteButton
                    KeyNavigation.up: scrubber
                }
            }

            // ── Right: timecode, per-track verbs, back into the player ──────
            Row {
                id: rightCluster

                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingTight

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: timeMetrics.width
                    horizontalAlignment: Text.AlignRight
                    text: mini.timeText
                    visible: !mini.compactWidth
                    color: Theme.textTertiary
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontCaption
                    // Tabular figures. IBM Plex Mono gives them anyway, but a
                    // theme that swaps the mono face for a proportional one
                    // must not take the alignment with it.
                    font.features: ({ "tnum": 1 })
                }

                StrmIconButton {
                    id: favoriteButton

                    anchors.verticalCenter: parent.verticalCenter
                    size: Theme.scale(34)
                    visible: mini.isAudio && !mini.compactWidth
                    activeFocusOnTab: false
                    iconName: mini.nowFavorite ? "heart-filled" : "heart"
                    tooltip: mini.nowFavorite ? qsTr("Remove from favourites")
                                              : qsTr("Add to favourites")
                    checked: mini.nowFavorite
                    enabled: mini.nowItemId.length > 0

                    onClicked: mini.toggleFavorite()

                    KeyNavigation.left: stopButton
                    KeyNavigation.right: queueButton
                    KeyNavigation.up: scrubber
                }

                StrmIconButton {
                    id: queueButton

                    anchors.verticalCenter: parent.verticalCenter
                    size: Theme.scale(34)
                    visible: mini.isAudio
                    activeFocusOnTab: false
                    iconName: "queue"
                    tooltip: qsTr("Queue")
                    checked: queuePeek.opened

                    onClicked: {
                        if (queuePeek.opened)
                            queuePeek.close();
                        else
                            queuePeek.open();
                    }

                    KeyNavigation.left: favoriteButton
                    KeyNavigation.right: expandButton
                    KeyNavigation.up: scrubber
                }

                StrmIconButton {
                    id: expandButton

                    anchors.verticalCenter: parent.verticalCenter
                    size: Theme.scale(34)
                    activeFocusOnTab: false
                    iconName: "chevron-up"
                    tooltip: qsTr("Open player")

                    onClicked: mini.expandRequested()

                    KeyNavigation.left: queueButton
                    KeyNavigation.up: scrubber
                }
            }
        }
    }

    // ── The queue peek ──────────────────────────────────────────────────────
    // QueuePanel, unchanged, in a Popup. Not a fourth queue list: the panel is
    // already built on the shared TrackTable and TrackRow, it already reads
    // `posterUrl` off the model, and it already carries jump / remove / shuffle
    // / repeat wired to the queue itself. What it needed was somewhere to be
    // reachable from outside the player page, and this is that.
    //
    // A Popup because it must render in the window's overlay layer: this bar
    // clips itself to its own strip (the slide-out depends on it), so a child
    // drawn above the bar would simply be cut off.
    Popup {
        id: queuePeek

        // The bar, not the window: `x` and `y` below are read in its
        // coordinates, and it is also the region the close policy tests
        // against.
        parent: mini

        // Anchored to the button that opened it and sitting on top of the bar,
        // which is where the eye already is.
        x: Math.max(Theme.spacingTight, mini.width - queuePeek.width - Theme.spacingValue)
        y: -queuePeek.height - Theme.spacingTight
        width: Math.min(Theme.scale(420), Math.max(Theme.scale(240),
                                                   mini.width - 2 * Theme.spacingValue))
        height: Math.min(Theme.scale(460),
                         Math.max(Theme.scale(180),
                                  mini.spaceAboveBar - Theme.spacingLoose))

        padding: 0
        modal: false
        dim: false
        focus: true
        // QueuePanel draws its own StrmPanel surface; a second background
        // behind it would double the border and the shadow.
        background: null
        // OutsideParent, not Outside, which is the distinction QQuickComboBox
        // draws for its own popup and for the same reason. A press outside the
        // popup closes it there and then — the exit transition is prepared
        // synchronously, so `opened` is false immediately — while StrmIconButton
        // fires `clicked` on RELEASE. The queue button is outside the popup, so
        // the press shut the peek and the release of that same click reopened
        // it: the button could open the peek but never close it, and only Esc or
        // a click elsewhere could. (Measured on Qt 6.11 with a standalone probe:
        // two clicks on the button gave two opens and one close.)
        //
        // The parent is the bar, so the button — and the rest of the strip — is
        // no longer "outside": nothing acts on the press, the toggle on the
        // release is the only thing that runs, and a press anywhere off the bar
        // still dismisses the peek.
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
                     | Popup.CloseOnReleaseOutsideParent

        enter: Transition {
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: Theme.animFastMs
                easing.type: Theme.easeStandard
            }
        }
        exit: Transition {
            NumberAnimation {
                property: "opacity"
                from: 1
                to: 0
                duration: Theme.animInstant
                easing.type: Theme.easeInstant
            }
        }

        // Not the assignment itself: qmllint cannot resolve an attached type
        // through an id from inside a Popup's bindings (see spaceAboveBar), so
        // the Window lookup is made where it can see it.
        onAboutToShow: mini.notePeekFocus()

        // A peek that closed while holding the keyboard would leave focus
        // nowhere. Deferred the way every other overlay in the app defers its
        // restore: `closed` fires inside the popup's own teardown, which is
        // still settling focus, and a forceActiveFocus() made in the middle of
        // that is simply overwritten.
        onClosed: Qt.callLater(mini.restorePeekFocus)

        contentItem: QueuePanel {
            focus: true

            onCloseRequested: queuePeek.close()
        }
    }

    // Give the keyboard back to whatever the peek took it from — and only when
    // the peek is what left it homeless. `closed` fires for EVERY close, the
    // press-outside one included, and that press has usually already landed on
    // something with a better claim than this bar: with the peek open, click a
    // poster in the grid behind it and the page that push produces holds the
    // keyboard by the time this runs. Focusing the bar on top of that would
    // walk the arrow keys along the strip instead of the new page — the bar
    // taking focus on its own, which is the one thing this file promises not to
    // do (see the header).
    //
    // What is repaired is therefore only what QQC2 leaves behind when nothing
    // else claimed the keyboard: nothing focused, or bare window content. That
    // is StrmMenu._restoreFocus()'s test, down to the two different items
    // "bare" means under an ApplicationWindow, and it is right here for the
    // same reasons it is right there.
    function restorePeekFocus(): void {
        // Re-opened while the restore was queued: the escrow belongs to that
        // open now and its own close will return it.
        if (queuePeek.visible)
            return;
        const remembered = mini.peekFocusEscrow;
        mini.peekFocusEscrow = null;
        if (!mini.shown || remembered === null)
            return;
        const current = mini.Window.activeFocusItem;
        const win = mini.Window.window;
        const bare = current === mini.Window.contentItem
                     || (win !== null && current === win.contentItem);
        if (current !== null && !bare)
            return;
        if (remembered.visible && remembered.enabled)
            remembered.forceActiveFocus(Qt.OtherFocusReason);
    }

    // Crossing the audio/video boundary hides six of the eleven controls, and
    // Qt does not take the keyboard off an item it has just hidden: a hidden
    // control keeps activeFocus (measured, Qt 6.11), so the focus ring vanishes
    // from the screen while Space still activates whatever it was on. Arrow
    // keys do walk out of it — KeyNavigation skips invisible links — so this is
    // a ring nobody can find rather than a dead strip, but it is the same
    // hazard PlayerPage re-seats for at PlayerPage.qml:72: the keyboard must
    // not be left inside chrome that has just become invisible.
    function reseatFocusAfterMode(): void {
        if (!mini.shown || !mini.activeFocus)
            return;
        const focused = mini.Window.activeFocusItem;
        if (focused !== null && focused !== mini && focused.visible && focused.enabled)
            return;
        playPause.forceActiveFocus(Qt.OtherFocusReason);
    }

    // The peek is about what is queued, and what is queued is a music idea. A
    // queue that ran off the end of the audio bar — a stop, or a step into a
    // music video — would otherwise stay open over a page it no longer belongs
    // to, with only Esc to find it.
    onIsAudioChanged: {
        if (!mini.isAudio)
            queuePeek.close();
        // One turn later: the controls' own `visible` bindings react to this
        // same change, and nothing orders them against this handler.
        Qt.callLater(mini.reseatFocusAfterMode);
    }
}
