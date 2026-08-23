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
//    lives in the window's overlay and is dismissed by clicking away from it.
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
    signal artistRequested(string artistId, string name)
    signal albumRequested(string albumId, string name)

    // Guarded the way every other PlayerCtl consumer in the tree is: a binding
    // that throws would take the whole bar — and therefore the page's bottom
    // margin — with it.
    readonly property bool active: PlayerCtl.active === true
    readonly property bool shown: mini.active && !mini.playerOnTop
    // The one answer, shared with PlayerPage (MUSIC.md §4).
    readonly property bool isAudio: PlayerCtl.isAudio === true

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

    // ── Now-playing data ────────────────────────────────────────────────────
    // The queue is the only place the current item's artwork and its music
    // identity live: the controller publishes `title` and nothing else visual.
    // Reading it through `currentIndex` (a notifying property) rather than
    // through currentItem() is what makes this a live binding, not a snapshot.
    readonly property var nowItem: {
        const q = PlayerCtl.queue;
        if (q === undefined || q === null)
            return ({});
        if (q.currentIndex < 0 || q.count <= 0)
            return ({});
        return q.itemAt(q.currentIndex);
    }

    readonly property string nowItemId: {
        const id = mini.nowItem.itemId;
        return (id !== undefined) ? String(id) : "";
    }

    readonly property var queueModel: {
        const q = PlayerCtl.queue;
        return (q !== undefined && q !== null) ? q : null;
    }

    // Rule 1, the square is the unit (MUSIC.md §4). A track's art is 1:1 and it
    // fills; `thumbUrl` is a 16:9 crop that a track does not have and an album
    // does not want. `posterUrl` is read and never rebuilt by hand —
    // MediaItem::coverSource() already resolves a track to its *album's* cover
    // rather than to whatever the ripper embedded, and reconstructing an image
    // URL here would throw that away.
    readonly property string artUrl: {
        const item = mini.nowItem;
        if (mini.isAudio)
            return (item.posterUrl !== undefined) ? String(item.posterUrl) : "";
        if (item.thumbUrl !== undefined && String(item.thumbUrl).length > 0)
            return String(item.thumbUrl);
        if (item.backdropUrl !== undefined && String(item.backdropUrl).length > 0)
            return String(item.backdropUrl);
        if (item.posterUrl !== undefined && String(item.posterUrl).length > 0)
            return String(item.posterUrl);
        return "";
    }
    readonly property bool artIsWide: {
        if (mini.isAudio)
            return false;
        const item = mini.nowItem;
        return (item.thumbUrl !== undefined && String(item.thumbUrl).length > 0)
            || (item.backdropUrl !== undefined && String(item.backdropUrl).length > 0);
    }

    // PlayerCtl.title is MediaItemModel's label — "Series — S5E14 — Name" for an
    // episode — so the same split the OSD does gives the VIDEO bar a subline
    // without the controller growing a second title property.
    //
    // Music does not go anywhere near it. A track's context was structured
    // before it was ever composed into a label, and PlayQueue::itemFromVariant
    // carries `album`, `albumId`, `albumArtist`, `artists` and `artistIds`
    // through the round trip precisely so this bar can read the fields instead
    // of guessing at a display string.
    readonly property var titleParts: {
        const raw = PlayerCtl.title !== undefined ? String(PlayerCtl.title) : "";
        const parts = raw.split(" — ");
        if (parts.length >= 3)
            return ({ "title": parts.slice(2).join(" — "),
                      "subline": parts[0] + "  ·  " + parts[1] });
        return ({ "title": raw, "subline": "" });
    }

    readonly property string trackTitle: {
        if (mini.isAudio) {
            const name = mini.nowItem.name;
            if (name !== undefined && String(name).length > 0)
                return String(name);
            return PlayerCtl.title !== undefined ? String(PlayerCtl.title) : "";
        }
        return mini.titleParts.title;
    }

    // The bar shows ONE credit, not the whole cast: "Godspeed You! Black
    // Emperor" already fills the line it is on, and the link has one
    // destination anyway. The rest of the credit is on the album page the link
    // leads to.
    readonly property string artistText: {
        const item = mini.nowItem;
        const list = item.artists;
        if (list !== undefined && list !== null && list.length > 0)
            return String(list[0]);
        if (item.albumArtist !== undefined)
            return String(item.albumArtist);
        return "";
    }

    readonly property string artistId: {
        const ids = mini.nowItem.artistIds;
        if (ids !== undefined && ids !== null && ids.length > 0)
            return String(ids[0]);
        return "";
    }

    readonly property string albumText: {
        const album = mini.nowItem.album;
        return (album !== undefined) ? String(album) : "";
    }

    readonly property string albumId: {
        const id = mini.nowItem.albumId;
        return (id !== undefined) ? String(id) : "";
    }

    // The video bar's second line, which is a caption rather than a route.
    readonly property string subline: {
        if (mini.titleParts.subline.length > 0)
            return mini.titleParts.subline;
        const item = mini.nowItem;
        if (item.subtitle !== undefined && String(item.subtitle).length > 0)
            return String(item.subtitle);
        return "";
    }

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
    readonly property string timeText: mini.elapsedText + "  /  " + mini.remainingText

    // Rule 3, numerals line up (MUSIC.md §4). Mono with tabular figures gives
    // every digit the same advance, which stops "1:12" turning into "1:13" a
    // pixel wider — but the readout still grows a whole character crossing
    // 9:59 → 10:00, and a right-aligned box of a KNOWN width is what stops
    // that, too. The template is this item's own longest form with every digit
    // replaced, so it measures exactly what will be drawn into it.
    readonly property string timeTemplate: {
        const shape = mini.formatTime(mini.durationMs).replace(/[0-9]/g, "0");
        return shape + "  /  −" + shape;
    }

    TextMetrics {
        id: timeMetrics

        font.family: Theme.fontMono
        font.pixelSize: Theme.fontCaption
        font.features: ({ "tnum": 1 })
        text: mini.timeTemplate
    }

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
                        visible: mini.isAudio
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
                        visible: !mini.isAudio && mini.subline.length > 0
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
                    visible: mini.isAudio
                    width: Theme.spacingValue
                    height: 1
                }

                StrmIconButton {
                    id: shuffleButton

                    anchors.verticalCenter: parent.verticalCenter
                    size: Theme.scale(34)
                    visible: mini.isAudio
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
                    visible: mini.isAudio
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
                    visible: mini.isAudio
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
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                     | Popup.CloseOnReleaseOutside

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

    function restorePeekFocus(): void {
        if (mini.shown && queueButton.visible && queueButton.enabled)
            queueButton.forceActiveFocus(Qt.OtherFocusReason);
    }

    // The peek is about what is queued, and what is queued is a music idea. A
    // queue that ran off the end of the audio bar — a stop, or a step into a
    // music video — would otherwise stay open over a page it no longer belongs
    // to, with only Esc to find it.
    onIsAudioChanged: {
        if (!mini.isAudio)
            queuePeek.close();
    }
}
