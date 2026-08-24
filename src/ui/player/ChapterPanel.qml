// Bound: the ListView delegate reaches this file's ids (panel, list).
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import StrmQt

// ChapterPanel — the chapter list (ARCHITECTURE.md).
//
// Chapters are three things in this OSD: ticks on the scrubber, the name in the
// scrubber's hover preview, and this list. The list is the only one of the
// three you can act on, so it marks where you are and seeks where you click.
//
// The owner supplies `chapters` (PlayerCtl.chapters: {name, startMs, imageTag})
// and `positionMs`, rather than this file reaching for the controller itself —
// the OSD already guards that surface once and there is no reason to guard it
// twice.
FocusScope {
    id: panel

    property var chapters: []
    property real positionMs: 0

    signal closeRequested

    // Last chapter whose start is at or before the playhead.
    readonly property int currentChapter: {
        let found = -1;
        for (let i = 0; i < panel.chapters.length; ++i) {
            if (Number(panel.chapters[i].startMs) <= panel.positionMs)
                found = i;
            else
                break;
        }
        return found;
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

    function chapterName(index: int): string {
        if (index < 0 || index >= panel.chapters.length)
            return "";
        const name = panel.chapters[index].name;
        return (name !== undefined && String(name).length > 0)
                ? String(name) : qsTr("Chapter %1").arg(index + 1);
    }

    // seekToChapter is the controller's verb (it knows about chapter indices);
    // the absolute seek is the fallback for a build where it has not landed.
    function jumpTo(index: int): void {
        if (index < 0 || index >= panel.chapters.length)
            return;
        if (typeof PlayerCtl.seekToChapter === "function")
            PlayerCtl.seekToChapter(index);
        else
            PlayerCtl.seekTo(Number(panel.chapters[index].startMs));
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
                text: qsTr("Chapters")
                color: Theme.textPrimaryColor
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
            }

            StrmIconButton {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                iconName: "close"
                round: true
                tooltip: qsTr("Close")
                onClicked: panel.closeRequested()
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
            model: panel.chapters
            keyNavigationWraps: false
            highlightMoveDuration: Theme.animFastMs

            Accessible.role: Accessible.List
            Accessible.name: qsTr("Chapters")
            Accessible.focusable: true
            Accessible.focused: list.activeFocus

            // Set once rather than bound: the keyboard writes currentIndex, and
            // a binding that the first Down keypress destroys is a binding that
            // was lying about what it did.
            Component.onCompleted: {
                list.currentIndex = Math.max(0, panel.currentChapter);
                list.positionViewAtIndex(list.currentIndex, ListView.Center);
            }

            ScrollBar.vertical: StrmScrollBar {}

            Keys.onReturnPressed: event => {
                if (!event.isAutoRepeat)
                    panel.jumpTo(list.currentIndex);
            }
            Keys.onEnterPressed: event => {
                if (!event.isAutoRepeat)
                    panel.jumpTo(list.currentIndex);
            }

            Text {
                anchors.centerIn: parent
                width: parent.width - Theme.spacingValue
                visible: list.count === 0
                text: qsTr("This item has no chapters.")
                color: Theme.textTertiary
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }

            delegate: Item {
                id: chapterRow

                required property int index
                required property var modelData

                readonly property bool hovered: rowHover.hovered
                readonly property bool highlighted: chapterRow.ListView.isCurrentItem
                                                    && list.activeFocus
                readonly property bool playing: chapterRow.index === panel.currentChapter

                width: ListView.view.width
                height: Theme.scale(46)

                Accessible.role: Accessible.ListItem
                Accessible.name: panel.chapterName(chapterRow.index)
                Accessible.description: qsTr("Starts at %1").arg(startLabel.text)
                Accessible.selectable: true
                Accessible.selected: chapterRow.playing || chapterRow.highlighted
                Accessible.focused: chapterRow.highlighted
                Accessible.onPressAction: panel.jumpTo(chapterRow.index)

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusChip
                    color: chapterRow.highlighted ? Theme.surfaceRaisedColor
                         : chapterRow.hovered ? Theme.hoverTint
                         : "transparent"

                    Behavior on color {
                        ColorAnimation {
                            duration: chapterRow.highlighted ? Theme.animFastMs : Theme.animInstant
                            easing.type: chapterRow.highlighted ? Theme.easeStandard
                                                                : Theme.easeInstant
                        }
                    }
                }

                // Chapter-thumbnail hook. Emby serves a per-chapter image at
                // Items/{id}/Images/Chapter/{index}?tag={imageTag}, and the row
                // carries `imageTag` already — but EmbyImageProvider has no
                // Chapter route yet (its id grammar is <itemId>/<type>/<tag>,
                // with no index segment). When that lands, an Image sourced from
                // image://emby/<itemId>/Chapter/<index>/<tag> replaces this
                // marker strip, at 16:9 and Theme.scale(72) wide.
                Rectangle {
                    id: marker

                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacingTight
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.scale(3)
                    height: parent.height * 0.55
                    radius: width / 2
                    color: chapterRow.playing ? Theme.accentColor : Theme.hairline
                }

                Text {
                    id: startLabel

                    anchors.left: marker.right
                    anchors.leftMargin: Theme.spacingTight
                    anchors.verticalCenter: parent.verticalCenter
                    text: panel.formatTime(Number(chapterRow.modelData.startMs))
                    color: chapterRow.playing ? Theme.accentColor : Theme.textSecondaryColor
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSmall
                }

                Text {
                    anchors.left: startLabel.right
                    anchors.leftMargin: Theme.spacingValue
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingTight
                    anchors.verticalCenter: parent.verticalCenter
                    text: panel.chapterName(chapterRow.index)
                    color: chapterRow.playing ? Theme.textPrimaryColor : Theme.textSecondaryColor
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontBodySize
                    elide: Text.ElideRight
                }

                HoverHandler {
                    id: rowHover
                    cursorShape: Qt.PointingHandCursor
                }

                TapHandler {
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: {
                        list.currentIndex = chapterRow.index;
                        list.forceActiveFocus(Qt.MouseFocusReason);
                        panel.jumpTo(chapterRow.index);
                    }
                }
            }
        }
    }
}
