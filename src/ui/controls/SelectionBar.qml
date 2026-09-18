pragma ComponentBehavior: Bound
import QtQuick
import StrmQt

// SelectionBar — what a multi-selection in a TrackTable can be done to
// (MUSIC.md §7).
//
// A selection with nothing to do is dead code, which is why TrackTable's
// selection and this strip landed together. It appears only while rows are
// picked, states how many, and offers the verbs a set of tracks is worth
// picking FOR: queue them, file them in a playlist, favourite them — and, where
// the table is a playlist's own members, take them out of it.
//
// ── Why it is a control and not four copies ────────────────────────────────
// Four tables can select — the album's, the Songs tab's, the artist's top
// tracks and a playlist's members — and the four would have drifted the way the
// four track rows did before TrackRow existed. The strip states intent and
// nothing else: every signal is handled by the page, which owns the ids, the
// picker and the entry grammar.
//
// ── Focus ──────────────────────────────────────────────────────────────────
// The buttons DO take focus on Tab, unlike the verbs inside a TrackRow. A row's
// verbs are per-row and there are N of them, so Tab has to skip those or it
// walks a 200-track box set; these are one strip that exists only while a
// selection does, so they add stops exactly when there is something to do with
// them and none when there is not. A batch verb no keyboard could reach would
// be the same defect as a selection with no verbs.
//
// `enabled` follows `visible` below, which is what keeps a hidden strip out of
// the traversal rather than merely off the screen.
Item {
    id: bar

    property int count: 0
    // Off for a table whose page has no picker to raise. The other two verbs
    // are always available where there is a selection at all.
    property bool allowPlaylist: true
    // On only where the rows can be taken OUT of what is showing them, which
    // today is a playlist's members and nothing else: an album's track list is
    // the record, and there is no verb that removes a song from one.
    property bool allowRemove: false

    signal queueRequested()
    signal playlistRequested()
    signal favoriteRequested()
    signal removeRequested()
    signal clearRequested()

    readonly property bool shown: bar.count > 0

    implicitHeight: bar.shown ? Theme.controlHeight + Theme.spacingTight : 0
    height: implicitHeight
    visible: bar.shown
    // Follows `visible`, so a hidden strip is out of every traversal rather
    // than merely invisible.
    enabled: bar.shown

    Rectangle {
        id: surface

        anchors.fill: parent
        anchors.topMargin: Theme.spacingTight
        radius: Theme.radiusChip
        color: Theme.surfaceOverlay
        border.width: 1
        border.color: Theme.accentColor

        Text {
            id: countLabel

            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingValue
            anchors.verticalCenter: parent.verticalCenter
            // Mono, because it is a readout and it changes on every keystroke
            // of a Shift range — a proportional count reflows the row under it.
            text: qsTr("%n selected", "", bar.count)
            color: Theme.textPrimaryColor
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontSmall
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingTight
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingTight

            StrmButton {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Add to queue")
                iconName: "queue"
                variant: "secondary"
                onClicked: bar.queueRequested()
            }

            StrmButton {
                id: addToPlaylistButton

                anchors.verticalCenter: parent.verticalCenter
                visible: bar.allowPlaylist
                // Ruling P3-R2: StrmButton ties activeFocusOnTab to
                // `interactive`, never to `visible` — a caller that sets
                // allowPlaylist: false (PlaylistPage) would otherwise leave
                // this DECLARING a Tab stop nobody can see. Measured at Qt
                // 6.11.2: Tab traversal skips an invisible item whatever
                // activeFocusOnTab says, so what the override corrects is the
                // property value, which NavRail.qml:43 and :98 read directly.
                //
                // `|| activeFocus` is not belt-and-braces. `visible` here is
                // EFFECTIVE visibility, so it also goes false when the bar
                // itself hides on an emptied selection — and if this button was
                // the one holding the keyboard, Qt refuses to clear
                // activeFocusOnTab on the active focus item, warns, and keeps
                // the old value. Measured: one warning per hide. Staying true
                // for that instant lets the write land on the next tick, once
                // activeFocus has gone.
                activeFocusOnTab: addToPlaylistButton.visible
                                  || addToPlaylistButton.activeFocus
                text: qsTr("Add to playlist")
                iconName: "playlist"
                variant: "secondary"
                onClicked: bar.playlistRequested()
            }

            StrmIconButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: "heart"
                tooltip: qsTr("Add all to favourites")
                onClicked: bar.favoriteRequested()
            }

            StrmButton {
                id: removeButton

                anchors.verticalCenter: parent.verticalCenter
                visible: bar.allowRemove
                // Same shape, and the same `|| activeFocus` for the same
                // measured reason: most callers never set allowRemove, so this
                // button is invisible everywhere except a playlist — and where
                // it IS shown it is the verb whose own press empties the
                // selection that was keeping the bar on screen.
                activeFocusOnTab: removeButton.visible || removeButton.activeFocus
                text: qsTr("Remove")
                iconName: "close"
                variant: "ghost"
                onClicked: bar.removeRequested()
            }

            StrmIconButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: "close"
                tooltip: qsTr("Clear the selection")
                onClicked: bar.clearRequested()
            }
        }
    }
}
