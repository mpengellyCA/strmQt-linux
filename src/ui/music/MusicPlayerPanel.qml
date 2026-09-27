// Bound: the delegates reach this file's ids (panel, the lists).
pragma ComponentBehavior: Bound

import QtQuick
import StrmQt

// MusicPlayerPanel: the full player's side panel (Crate spec §7.2).
//
// Three tabs over what NowPlayingMusicCtl and the queue already hold:
//   Up next  the queue, "Playing from · <source>", jump / remove / reorder
//   Album    the current album's tracklist (cached), playing row marked
//   Lyrics   only when the server provides them; timed lines follow the playhead
//
// Its own tab strip rather than StrmTabBar: StrmTabBar consumes Left/Right for
// itself, and here Left off the first tab has to leave the panel for the stage.
FocusScope {
    id: panel

    // Left off the first tab: the stage takes the keyboard.
    signal leftRequested

    readonly property var queue: {
        const q = PlayerCtl.queue;
        return (q !== undefined && q !== null) ? q : null;
    }
    readonly property int queueCount: panel.queue !== null ? panel.queue.count : 0
    readonly property bool lyricsAvailable: NowPlayingMusicCtl.lyricsAvailable === true
    readonly property var tabKeys: panel.lyricsAvailable ? ["upNext", "album", "lyrics"]
                                                         : ["upNext", "album"]
    // The user's own pick, kept even while it is not showable. Lyrics is
    // cleared as soon as the queue moves — NowPlayingMusicController::
    // clearLyrics() runs before the next track's lyrics, if any, arrive back
    // over the network — so a straight `currentTab` write here would throw
    // away "I am reading the lyrics" for every single track boundary, not
    // just the tracks that truly have none.
    property string _desiredTab: "upNext"
    // The tab actually shown: the pick, unless it is not in `tabKeys` right
    // now, in which case Up next stands in until the pick comes back (or the
    // user picks something else, which overwrites `_desiredTab` too).
    readonly property string currentTab: panel.tabKeys.indexOf(panel._desiredTab) >= 0
                                         ? panel._desiredTab : "upNext"

    function tabLabel(key: string): string {
        if (key === "album")
            return qsTr("Album");
        if (key === "lyrics")
            return qsTr("Lyrics");
        return qsTr("Up next");
    }

    function stepTab(step: int): bool {
        const next = panel.tabKeys.indexOf(panel.currentTab) + step;
        if (next < 0 || next >= panel.tabKeys.length)
            return false;
        panel._desiredTab = panel.tabKeys[next];
        return true;
    }

    function focusTabs(): void {
        tabStrip.forceActiveFocus(Qt.TabFocusReason);
    }

    function focusContent(): void {
        if (panel.currentTab === "album")
            albumList.forceActiveFocus(Qt.TabFocusReason);
        else if (panel.currentTab === "lyrics")
            lyricList.forceActiveFocus(Qt.TabFocusReason);
        else
            queueList.forceActiveFocus(Qt.TabFocusReason);
    }

    function jump(index: int): void {
        if (panel.queue !== null && index >= 0 && index < panel.queueCount)
            panel.queue.jumpTo(index);
    }

    function remove(index: int): void {
        if (panel.queue !== null && index >= 0 && index < panel.queueCount)
            panel.queue.removeAt(index);
    }

    function move(from: int, to: int): bool {
        if (panel.queue === null || from < 0 || to < 0 || from >= panel.queueCount || to >= panel.queueCount)
            return false;
        panel.queue.moveItem(from, to);
        return true;
    }

    // What `currentTab` was the last time it changed, so the handler below
    // can tell which content view just lost its tab.
    property string _shownTab: "upNext"

    function _contentListFor(tabKey: string): var {
        if (tabKey === "album")
            return albumList;
        if (tabKey === "lyrics")
            return lyricList;
        return queueList;
    }

    // Measured on a real window: hiding the Item that wraps a focused list
    // does NOT move active focus anywhere — Qt leaves it exactly where it
    // was, on a now-invisible list with no ring drawn and no longer even the
    // tab shown. Lyrics vanishing mid-read is the one case that can do this
    // with no click or keypress of the user's own
    // (NowPlayingMusicController::clearLyrics(), run from a track change).
    // So this is not cleanup after Qt's own focus handling; it is the only
    // thing that moves focus back to something visible at all.
    onCurrentTabChanged: {
        const previousTab = panel._shownTab;
        panel._shownTab = panel.currentTab;
        if (previousTab !== panel.currentTab && panel._contentListFor(previousTab).activeFocus)
            panel.focusTabs();
    }

    // ── Surface ─────────────────────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        color: Theme.surfaceColor
        opacity: 0.82
    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: Theme.hairline
    }

    // Presses and the wheel on the panel's empty space must not reach the
    // page's picture area below, where a click pauses and the wheel is volume.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: wheel => wheel.accepted = true
    }

    // ── Tab strip ───────────────────────────────────────────────────────────
    Item {
        id: tabStrip
        objectName: "tabStrip"

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: Theme.spacingLoose
        anchors.rightMargin: Theme.spacingLoose
        anchors.topMargin: Theme.spacingLoose
        height: Theme.controlHeight
        focus: true
        activeFocusOnTab: false

        Accessible.role: Accessible.PageTabList
        Accessible.name: qsTr("Player panel")

        Keys.onLeftPressed: event => {
            if (!panel.stepTab(-1))
                panel.leftRequested();
            event.accepted = true;
        }
        Keys.onRightPressed: event => {
            panel.stepTab(1);
            event.accepted = true;
        }
        Keys.onDownPressed: event => {
            panel.focusContent();
            event.accepted = true;
        }
        Keys.onReturnPressed: event => {
            panel.focusContent();
            event.accepted = true;
        }
        Keys.onEnterPressed: event => {
            panel.focusContent();
            event.accepted = true;
        }

        Row {
            id: tabRow

            height: parent.height
            spacing: Theme.spacingLoose

            Repeater {
                model: panel.tabKeys

                delegate: Item {
                    id: tab

                    required property string modelData
                    readonly property bool selected: tab.modelData === panel.currentTab

                    width: tabText.implicitWidth
                    height: tabRow.height

                    Accessible.role: Accessible.PageTab
                    Accessible.name: tabText.text

                    Text {
                        id: tabText

                        anchors.verticalCenter: parent.verticalCenter
                        text: panel.tabLabel(tab.modelData)
                        color: (tab.selected || tabHover.hovered) ? Theme.textPrimaryColor
                                                                   : Theme.textSecondaryColor
                        font.family: Theme.fontDisplay
                        font.pixelSize: Theme.crateKickerSize
                        font.letterSpacing: Theme.crateKickerSize * Theme.crateKickerTracking
                        font.weight: Font.DemiBold
                        font.capitalization: Font.AllUppercase
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 3
                        color: Theme.accentColor
                        visible: tab.selected
                    }

                    HoverHandler {
                        id: tabHover
                        cursorShape: Qt.PointingHandCursor
                    }

                    TapHandler {
                        gesturePolicy: TapHandler.ReleaseWithinBounds
                        onTapped: {
                            Input.noteInput("mouse");
                            panel._desiredTab = tab.modelData;
                        }
                    }

                    FocusRing {
                        active: tabStrip.activeFocus && tab.selected
                        radius: Theme.radiusChip
                        inset: -Theme.scale(4)
                    }
                }
            }
        }
    }

    // ── Tabs ────────────────────────────────────────────────────────────────
    Item {
        id: body

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: tabStrip.bottom
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.spacingValue
        anchors.rightMargin: Theme.spacingValue
        anchors.topMargin: Theme.spacingValue
        anchors.bottomMargin: Theme.spacingValue

        // Up next
        Item {
            anchors.fill: parent
            visible: panel.currentTab === "upNext"

            CrateKicker {
                id: sourceKicker

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: Theme.spacingTight
                text: NowPlayingMusicCtl.sourceKicker
                visible: text.length > 0
            }

            TrackTable {
                id: queueList
                objectName: "queueList"

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.topMargin: sourceKicker.visible ? sourceKicker.height + Theme.spacingValue : 0
                clip: true
                spacing: Theme.scale(2)
                model: panel.queue
                rowHeight: Theme.scale(58)
                jumpRole: "label"
                // A small safety margin above row 0: TrackRow's own FocusRing
                // already insets to 0 for this table's clip (a Rectangle's
                // border draws inside its own bounds, so nothing is actually
                // cropped there), but a clipping ListView cropping the very
                // first row's ring is a known app-wide risk elsewhere in this
                // app. Cheap headroom, kept local to this call site rather
                // than touching TrackTable itself.
                topMargin: Theme.focusRingWidth

                onActivated: index => panel.jump(index)

                // Left leaves the panel for the stage, as it does off the tabs.
                Keys.onLeftPressed: event => {
                    panel.leftRequested();
                    event.accepted = true;
                }
                Keys.onDeletePressed: event => {
                    if (!event.isAutoRepeat)
                        panel.remove(queueList.currentIndex);
                    event.accepted = true;
                }
                Keys.onUpPressed: event => {
                    if (event.modifiers & Qt.ControlModifier) {
                        const from = queueList.currentIndex;
                        if (panel.move(from, from - 1))
                            queueList.currentIndex = from - 1;
                        event.accepted = true;
                    } else if (queueList.currentIndex <= 0) {
                        panel.focusTabs();
                        event.accepted = true;
                    } else {
                        event.accepted = false;
                    }
                }
                Keys.onDownPressed: event => {
                    if (event.modifiers & Qt.ControlModifier) {
                        const from = queueList.currentIndex;
                        if (panel.move(from, from + 1))
                            queueList.currentIndex = from + 1;
                        event.accepted = true;
                    } else {
                        event.accepted = false;
                    }
                }

                onVisibleChanged: {
                    if (queueList.visible && panel.queue !== null)
                        queueList.currentIndex = Math.max(0, panel.queue.currentIndex);
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

                // The queue's row, as QueuePanel draws it.
                delegate: TrackRow {
                    id: queueRow

                    required property int index
                    required property var model

                    width: queueList.width
                    rowHeight: Theme.scale(58)
                    surfaceBottomMargin: 0
                    showNumber: false
                    showPlayingMarker: true
                    hoverPlayGlyph: false
                    showCover: true
                    coverSize: Theme.scale(40)
                    coverUrl: queueRow.model.posterUrl !== undefined ? String(queueRow.model.posterUrl) : ""
                    title: queueRow.model.label !== undefined ? String(queueRow.model.label) : ""
                    secondary: queueRow.model.subtitle !== undefined ? String(queueRow.model.subtitle) : ""
                    playing: queueRow.model.isCurrent === true
                    current: queueRow.ListView.isCurrentItem && queueList.activeFocus
                    verbsRevealed: queueRow.hovered || queueRow.current

                    onActivated: {
                        queueList.currentIndex = queueRow.index;
                        queueList.forceActiveFocus(Qt.MouseFocusReason);
                        panel.jump(queueRow.index);
                    }

                    StrmIconButton {
                        iconName: "close"
                        round: true
                        size: Theme.scale(28)
                        tooltip: qsTr("Remove from queue")
                        activeFocusOnTab: false
                        onClicked: panel.remove(queueRow.index)
                    }
                }
            }
        }

        // Album
        Item {
            anchors.fill: parent
            visible: panel.currentTab === "album"

            Item {
                id: albumHeader

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: Theme.spacingTight
                height: Theme.scale(52)

                Rectangle {
                    id: albumCover

                    width: parent.height
                    height: parent.height
                    radius: Theme.crateSleeveRadius
                    clip: true
                    color: Theme.surfaceRaisedColor

                    StrmImage {
                        anchors.fill: parent
                        source: NowPlayingMusicCtl.coverUrl
                    }
                }

                Column {
                    anchors.left: albumCover.right
                    anchors.leftMargin: Theme.spacingValue
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.scale(2)

                    Text {
                        width: parent.width
                        text: NowPlayingMusicCtl.album
                        color: Theme.textPrimaryColor
                        font.family: Theme.fontDisplay
                        font.pixelSize: Theme.fontBodySize
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }

                    Text {
                        width: parent.width
                        text: NowPlayingMusicCtl.albumSummary
                        color: Theme.textTertiary
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontCaption
                        font.features: ({ "tnum": 1 })
                        elide: Text.ElideRight
                    }
                }
            }

            TrackTable {
                id: albumList
                objectName: "albumList"

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: albumHeader.bottom
                anchors.bottom: parent.bottom
                anchors.topMargin: Theme.spacingValue
                clip: true
                spacing: Theme.scale(2)
                model: NowPlayingMusicCtl.albumTracks
                rowHeight: Theme.scale(44)
                jumpRole: "displayTitle"
                // See queueList: the same small safety margin above row 0.
                topMargin: Theme.focusRingWidth

                onActivated: index => NowPlayingMusicCtl.playAlbumFrom(index)

                Keys.onLeftPressed: event => {
                    panel.leftRequested();
                    event.accepted = true;
                }
                Keys.onUpPressed: event => {
                    if (albumList.currentIndex <= 0) {
                        panel.focusTabs();
                        event.accepted = true;
                    } else {
                        event.accepted = false;
                    }
                }

                onVisibleChanged: {
                    if (albumList.visible && NowPlayingMusicCtl.currentAlbumRow >= 0)
                        albumList.currentIndex = NowPlayingMusicCtl.currentAlbumRow;
                }

                // Follow the playing track, but never move a highlight the
                // keyboard is holding.
                Connections {
                    target: NowPlayingMusicCtl

                    function onCurrentAlbumRowChanged() {
                        if (!albumList.activeFocus && NowPlayingMusicCtl.currentAlbumRow >= 0)
                            albumList.currentIndex = NowPlayingMusicCtl.currentAlbumRow;
                    }
                }

                delegate: TrackRow {
                    id: albumRow

                    required property int index
                    required property var model

                    width: albumList.width
                    rowHeight: Theme.scale(44)
                    surfaceBottomMargin: 0
                    showNumber: true
                    showPlayingMarker: true
                    number: Number(albumRow.model.trackNumber) > 0 ? Number(albumRow.model.trackNumber) : -1
                    title: albumRow.model.displayTitle !== undefined ? String(albumRow.model.displayTitle) : ""
                    // Credit only where it differs from the record's own artist.
                    secondary: albumRow.model.differsFromAlbumArtist === true
                               ? String(albumRow.model.artistText) : ""
                    durationText: albumRow.model.durationText !== undefined ? String(albumRow.model.durationText) : ""
                    favorite: albumRow.model.favourite === true
                    playing: albumRow.index === NowPlayingMusicCtl.currentAlbumRow
                    current: albumRow.ListView.isCurrentItem && albumList.activeFocus

                    onActivated: {
                        albumList.currentIndex = albumRow.index;
                        albumList.forceActiveFocus(Qt.MouseFocusReason);
                        NowPlayingMusicCtl.playAlbumFrom(albumRow.index);
                    }
                }
            }
        }

        // Lyrics
        Item {
            anchors.fill: parent
            visible: panel.currentTab === "lyrics"

            ListView {
                id: lyricList
                objectName: "lyricList"

                readonly property bool timed: NowPlayingMusicCtl.lyricsTimed === true
                readonly property int step: Theme.scale(48)

                anchors.fill: parent
                anchors.leftMargin: Theme.spacingTight
                anchors.rightMargin: Theme.spacingTight
                clip: true
                activeFocusOnTab: false
                boundsBehavior: Flickable.StopAtBounds
                spacing: Theme.spacingTight
                model: NowPlayingMusicCtl.lyrics
                currentIndex: lyricList.timed ? NowPlayingMusicCtl.currentLyricRow : -1
                highlightRangeMode: lyricList.timed ? ListView.ApplyRange : ListView.NoHighlightRange
                preferredHighlightBegin: Math.round(lyricList.height * 0.4)
                preferredHighlightEnd: Math.round(lyricList.height * 0.6)
                highlightMoveDuration: Theme.animSlow

                Keys.onLeftPressed: event => {
                    panel.leftRequested();
                    event.accepted = true;
                }
                Keys.onUpPressed: event => {
                    if (lyricList.atYBeginning)
                        panel.focusTabs();
                    else
                        lyricList.contentY = Math.max(lyricList.originY, lyricList.contentY - lyricList.step);
                    event.accepted = true;
                }
                Keys.onDownPressed: event => {
                    if (!lyricList.atYEnd)
                        lyricList.contentY = Math.min(lyricList.originY + lyricList.contentHeight - lyricList.height,
                                                      lyricList.contentY + lyricList.step);
                    event.accepted = true;
                }

                delegate: Text {
                    id: lyricLine

                    required property int index
                    required property var modelData
                    readonly property bool isCurrent: lyricList.timed
                                                      && lyricLine.index === NowPlayingMusicCtl.currentLyricRow

                    width: lyricList.width
                    text: lyricLine.modelData.text !== undefined ? String(lyricLine.modelData.text) : ""
                    wrapMode: Text.WordWrap
                    color: !lyricList.timed ? Theme.textSecondaryColor
                         : lyricLine.isCurrent ? Theme.textPrimaryColor : Theme.textTertiary
                    font.family: Theme.fontDisplay
                    font.pixelSize: lyricList.timed ? Theme.fontTitle : Theme.fontBodySize
                    font.weight: lyricLine.isCurrent ? Font.DemiBold : Font.Normal

                    Behavior on color {
                        ColorAnimation {
                            duration: Theme.animFastMs
                            easing.type: Theme.easeStandard
                        }
                    }
                }
            }

            FocusRing {
                active: lyricList.activeFocus
                radius: Theme.radiusChip
            }
        }
    }
}
