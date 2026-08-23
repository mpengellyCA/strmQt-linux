// Bound: the ListView delegate reaches this file's ids (panel, list).
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
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

        ListView {
            id: list

            width: parent.width
            height: Math.max(Theme.scale(64),
                             panel.height - 2 * surface.padding - headerRow.height
                             - Theme.spacingTight)
            clip: true
            focus: true
            spacing: Theme.scale(2)
            model: panel.queue
            keyNavigationWraps: false
            highlightMoveDuration: Theme.animFastMs

            ScrollBar.vertical: StrmScrollBar {}

            Keys.onReturnPressed: event => {
                if (!event.isAutoRepeat)
                    panel.jump(list.currentIndex);
            }
            Keys.onEnterPressed: event => {
                if (!event.isAutoRepeat)
                    panel.jump(list.currentIndex);
            }
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

            delegate: Item {
                id: queueRow

                required property int index
                required property var model

                readonly property bool hovered: rowHover.hovered
                readonly property bool highlighted: queueRow.ListView.isCurrentItem
                                                    && list.activeFocus
                readonly property bool playing: queueRow.model.isCurrent === true

                width: ListView.view.width
                height: Theme.scale(58)

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusChip
                    color: queueRow.highlighted ? Theme.surfaceRaisedColor
                         : queueRow.hovered ? Theme.hoverTint
                         : "transparent"

                    Behavior on color {
                        ColorAnimation {
                            duration: queueRow.highlighted ? Theme.animFastMs : Theme.animInstant
                            easing.type: queueRow.highlighted ? Theme.easeStandard
                                                              : Theme.easeInstant
                        }
                    }
                }

                Rectangle {
                    id: marker

                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacingTight
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.scale(3)
                    height: parent.height * 0.55
                    radius: width / 2
                    color: queueRow.playing ? Theme.accentColor : "transparent"
                }

                Rectangle {
                    id: thumb

                    anchors.left: marker.right
                    anchors.leftMargin: Theme.spacingTight
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.scale(40)
                    height: Theme.scale(40)
                    radius: Theme.radiusChip
                    color: Theme.surfaceColor
                    clip: true

                    Image {
                        anchors.fill: parent
                        source: queueRow.model.posterUrl !== undefined
                                ? queueRow.model.posterUrl : ""
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        sourceSize.width: Theme.scale(80)
                        visible: status === Image.Ready
                    }
                }

                Column {
                    anchors.left: thumb.right
                    anchors.leftMargin: Theme.spacingTight
                    anchors.right: removeButton.left
                    anchors.rightMargin: Theme.spacingTight
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.scale(2)

                    Text {
                        width: parent.width
                        text: queueRow.model.label !== undefined ? queueRow.model.label : ""
                        color: queueRow.playing ? Theme.accentColor : Theme.textPrimaryColor
                        font.family: Theme.fontBody
                        font.pixelSize: Theme.fontSmall
                        elide: Text.ElideRight
                    }

                    Text {
                        id: subtitleText

                        width: parent.width
                        visible: subtitleText.text.length > 0
                        text: queueRow.model.subtitle !== undefined ? queueRow.model.subtitle : ""
                        color: Theme.textSecondaryColor
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontCaption
                        elide: Text.ElideRight
                    }
                }

                // Visible on hover or focus, like a card's overlay actions: a
                // permanent ✕ on every row turns a queue into a minefield.
                StrmIconButton {
                    id: removeButton

                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingTight
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "close"
                    round: true
                    size: Theme.scale(28)
                    tooltip: qsTr("Remove from queue")
                    opacity: (queueRow.hovered || queueRow.highlighted) ? 1 : 0
                    enabled: queueRow.hovered || queueRow.highlighted
                    activeFocusOnTab: false
                    onClicked: panel.remove(queueRow.index)

                    Behavior on opacity {
                        NumberAnimation {
                            duration: Theme.animInstant
                            easing.type: Theme.easeInstant
                        }
                    }
                }

                HoverHandler {
                    id: rowHover
                    cursorShape: Qt.PointingHandCursor
                }

                TapHandler {
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: {
                        list.currentIndex = queueRow.index;
                        list.forceActiveFocus(Qt.MouseFocusReason);
                        panel.jump(queueRow.index);
                    }
                }
            }
        }
    }
}
