// Bound: the ListView delegate reaches this file's ids (panel, list).
pragma ComponentBehavior: Bound

import QtQuick
import StrmQt

// QueuePanel — what is playing and what is next (ARCHITECTURE.md).
//
// Bound straight to PlayerCtl.queue, which is a QAbstractListModel exposing
// MediaItemModel's roles plus `queueIndex` and `isCurrent` — so the rows here
// read the same role names every rail and grid in the app already uses.
//
// Verbs: click or Return jumps (`jumpTo`), the row's ✕ removes (`removeAt`),
// and the header carries shuffle and repeat bound to the queue's own state.
//
// Not done: drag-to-reorder. `moveItem(from, to)` exists and works, but a
// drag-reorder inside an auto-hiding OSD needs a drag affordance that does not
// fight the panel's own scrolling, and that is a design question rather than a
// wiring one. Reordering is therefore mouse-and-keyboard *absent* here, not
// broken — it is the one part of D4 this wave does not ship.
FocusScope {
    id: panel

    signal closeRequested

    readonly property var queue: {
        const q = PlayerCtl.queue;
        return (q !== undefined && q !== null) ? q : null;
    }
    readonly property int count: panel.queue !== null ? panel.queue.count : 0
    readonly property bool shuffled: panel.queue !== null && panel.queue.shuffled === true
    // 0 off · 1 all · 2 one, matching PlayQueue::RepeatMode.
    readonly property int repeatMode: panel.queue !== null ? Number(panel.queue.repeatMode) : 0

    function jump(index: int): void {
        if (panel.queue === null || index < 0 || index >= panel.count)
            return;
        panel.queue.jumpTo(index);
    }

    function remove(index: int): void {
        if (panel.queue === null || index < 0 || index >= panel.count)
            return;
        panel.queue.removeAt(index);
    }

    function toggleShuffle(): void {
        if (panel.queue !== null)
            panel.queue.shuffled = !panel.queue.shuffled;
    }

    function cycleRepeat(): void {
        if (panel.queue !== null)
            panel.queue.cycleRepeatMode();
    }

    Keys.onEscapePressed: event => {
        panel.closeRequested();
        event.accepted = true;
    }

    StrmPanel {
        id: surface

        anchors.fill: parent
        padding: Theme.spacingValue
        elevation: 3

        Item {
            id: headerRow

            width: parent.width
            height: Theme.controlHeight

            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Queue")
                color: Theme.textPrimaryColor
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
            }

            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.scale(2)

                StrmIconButton {
                    id: shuffleButton

                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "shuffle"
                    round: true
                    tooltip: qsTr("Shuffle")
                    checked: panel.shuffled
                    enabled: panel.queue !== null
                    onClicked: panel.toggleShuffle()

                    KeyNavigation.right: repeatButton
                    KeyNavigation.down: list
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
                    enabled: panel.queue !== null
                    onClicked: panel.cycleRepeat()

                    KeyNavigation.left: shuffleButton
                    KeyNavigation.right: closeButton
                    KeyNavigation.down: list
                }

                StrmIconButton {
                    id: closeButton

                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "close"
                    round: true
                    tooltip: qsTr("Close")
                    onClicked: panel.closeRequested()

                    KeyNavigation.left: repeatButton
                    KeyNavigation.down: list
                }
            }
        }

        TrackTable {
            id: list

            width: parent.width
            height: Math.max(Theme.scale(64),
                             panel.height - 2 * surface.padding - headerRow.height
                             - Theme.spacingTight)
            focus: true
            spacing: Theme.scale(2)
            model: panel.queue
            rowHeight: Theme.scale(58)
            // The queue is not an album: no discs, and every row's credit is
            // its own, so there is nothing to decide once for the table. What
            // it does want is type-to-jump — a shuffled 300-track queue is not
            // navigable by arrow key either — and the queue's display string is
            // the composed `label`, not the bare name.
            jumpRole: "label"

            onActivated: index => panel.jump(index)

            // Delete removes the highlighted row. Guarded like every other
            // activation: a held key must not empty the queue.
            Keys.onDeletePressed: event => {
                if (!event.isAutoRepeat) {
                    panel.remove(list.currentIndex);
                    event.accepted = true;
                }
            }
            Keys.onUpPressed: event => {
                if (list.currentIndex <= 0) {
                    shuffleButton.forceActiveFocus(Qt.BacktabFocusReason);
                    event.accepted = true;
                } else {
                    event.accepted = false;
                }
            }

            Component.onCompleted: {
                if (panel.queue !== null)
                    list.currentIndex = Math.max(0, panel.queue.currentIndex);
            }

            Text {
                anchors.centerIn: parent
                width: parent.width - Theme.spacingValue
                visible: list.count === 0
                text: qsTr("Nothing queued after this.")
                color: Theme.textTertiary
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }

            // The shared row (ARCHITECTURE.md). A queue entry has no track
            // number — its position is not a fact about the record — so the
            // number column is off and the entry that is playing is marked by
            // the amber spine instead. `posterUrl` is read straight off the
            // model: MediaItem::coverSource() already resolves a track to its
            // album's cover rather than the ripper's embedded art, which is
            // what stopped a queue of one record looking like a ransom note.
            delegate: TrackRow {
                id: queueRow

                required property int index
                required property var model

                width: list.width

                rowHeight: Theme.scale(58)
                surfaceBottomMargin: 0
                showNumber: false
                showPlayingMarker: true
                hoverPlayGlyph: false
                showCover: true
                coverSize: Theme.scale(40)

                coverUrl: queueRow.model.posterUrl !== undefined
                          ? String(queueRow.model.posterUrl) : ""
                title: queueRow.model.label !== undefined ? String(queueRow.model.label) : ""
                secondary: queueRow.model.subtitle !== undefined
                           ? String(queueRow.model.subtitle) : ""

                playing: queueRow.model.isCurrent === true
                current: queueRow.ListView.isCurrentItem && list.activeFocus
                verbsRevealed: queueRow.hovered || queueRow.current

                onActivated: {
                    list.currentIndex = queueRow.index;
                    list.forceActiveFocus(Qt.MouseFocusReason);
                    panel.jump(queueRow.index);
                }

                // Visible on hover or focus, like a card's overlay actions: a
                // permanent ✕ on every row turns a queue into a minefield.
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
}
