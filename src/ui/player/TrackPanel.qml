// Bound: the ListView delegate reaches this file's ids (panel, list), which is
// only well-defined — and only lint-clean — with bound component behaviour.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import StrmQt

// TrackPanel — real audio and subtitle pickers (ARCHITECTURE.md).
//
// What it replaces: `A` and `C` stepping blindly to the next track with a
// toast. There was no list, no indication of what was selected, no visible
// "off", and no way to do any of it with a mouse.
//
// Each row carries what actually distinguishes one track from another —
// language, codec, channel layout, and the default/forced/external flags — and
// the current selection is marked with a check. Subtitles get an explicit Off
// row at the top, because "no subtitles" is a choice you have to be able to see
// and point at, not the absence of one.
//
// The keyboard fast path survives: `A` and `C` still cycle (the page routes
// them through InputMap), and the toast still says where they landed. This
// panel is the surface that makes the cycling optional rather than mandatory.
FocusScope {
    id: panel

    signal closeRequested

    // 0 = audio, 1 = subtitles.
    property int tab: 0

    readonly property var backend: PlayerCtl.backend

    // Guarded: engines with no track surface return empty lists, and this file
    // is built against a contract another agent is landing in the same wave.
    readonly property var audioTracks: {
        const list = panel.backend.audioTracks;
        return (list !== undefined && list !== null) ? list : [];
    }
    readonly property var subtitleTracks: {
        const list = panel.backend.subtitleTracks;
        return (list !== undefined && list !== null) ? list : [];
    }
    readonly property int currentAudioId: {
        const id = panel.backend.currentAudioTrackId;
        return id !== undefined ? Number(id) : -1;
    }
    readonly property int currentSubtitleId: {
        const id = panel.backend.currentSubtitleTrackId;
        return id !== undefined ? Number(id) : -1;
    }

    // One row shape for both lists: { trackId, label, detail, selected }.
    function describe(track, currentId): var {
        const id = track.id !== undefined ? Number(track.id) : -1;
        const bits = [];
        if (track.language !== undefined && String(track.language).length > 0)
            bits.push(String(track.language).toUpperCase());
        if (track.codec !== undefined && String(track.codec).length > 0)
            bits.push(String(track.codec).toUpperCase());
        if (track.channelLayout !== undefined && String(track.channelLayout).length > 0)
            bits.push(String(track.channelLayout));
        else if (track.channels !== undefined && Number(track.channels) > 0)
            bits.push(Number(track.channels) + qsTr("ch"));
        if (track.isDefault === true)
            bits.push(qsTr("default"));
        if (track.isForced === true)
            bits.push(qsTr("forced"));
        if (track.isExternal === true)
            bits.push(qsTr("external"));

        const title = track.title !== undefined && String(track.title).length > 0
                    ? String(track.title) : qsTr("Track %1").arg(id);
        return ({
            "trackId": id,
            "label": title,
            "detail": bits.join("  ·  "),
            // The engine's own `selected` flag wins; the id comparison is the
            // fallback for engines that do not set it.
            "selected": track.selected === true || id === currentId
        });
    }

    readonly property var audioRows: {
        const out = [];
        for (let i = 0; i < panel.audioTracks.length; ++i)
            out.push(panel.describe(panel.audioTracks[i], panel.currentAudioId));
        return out;
    }

    readonly property var subtitleRows: {
        // Off first, always present, always selectable.
        const out = [({
            "trackId": -1,
            "label": qsTr("Off"),
            "detail": qsTr("No subtitles"),
            "selected": panel.currentSubtitleId < 0
        })];
        for (let i = 0; i < panel.subtitleTracks.length; ++i)
            out.push(panel.describe(panel.subtitleTracks[i], panel.currentSubtitleId));
        return out;
    }

    readonly property var rows: panel.tab === 0 ? panel.audioRows : panel.subtitleRows

    function applyRow(index: int): void {
        if (index < 0 || index >= panel.rows.length)
            return;
        const trackId = panel.rows[index].trackId;
        if (panel.tab === 0)
            // Through the controller, not the backend: it is what remembers
            // the choice for next time (ARCHITECTURE.md).
            PlayerCtl.setAudioTrack(trackId);
        else
            PlayerCtl.setSubtitleTrack(trackId);
    }

    // Esc closes the panel; the page only stops playback once nothing is open.
    Keys.onEscapePressed: event => {
        panel.closeRequested();
        event.accepted = true;
    }

    StrmPanel {
        id: surface

        anchors.fill: parent
        padding: Theme.spacingValue
        elevation: 3

        // Header, tab bar and list are the body Column's three children, so the
        // list's height is the panel minus the two paddings, those two siblings
        // and the two gaps between them.
        Item {
            id: headerRow

            width: parent.width
            height: Theme.controlHeight

            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Tracks")
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

        StrmTabBar {
            id: trackTabs

            width: parent.width
            tabs: [
                ({ "text": qsTr("Audio"), "badge": panel.audioTracks.length }),
                ({ "text": qsTr("Subtitles"), "badge": panel.subtitleTracks.length })
            ]
            currentIndex: panel.tab

            KeyNavigation.down: list

            onTabSelected: index => {
                panel.tab = index;
                list.currentIndex = 0;
            }
        }

        ListView {
            id: list

            width: parent.width
            height: Math.max(Theme.scale(64),
                             panel.height - 2 * surface.padding - headerRow.height
                             - trackTabs.height - 2 * Theme.spacingTight)
            clip: true
            focus: true
            spacing: Theme.scale(2)
            model: panel.rows
            keyNavigationWraps: false
            highlightMoveDuration: Theme.animFastMs

            ScrollBar.vertical: StrmScrollBar {}

            Keys.onReturnPressed: event => {
                if (!event.isAutoRepeat)
                    panel.applyRow(list.currentIndex);
            }
            Keys.onEnterPressed: event => {
                if (!event.isAutoRepeat)
                    panel.applyRow(list.currentIndex);
            }

            // Up off the top reaches the tab bar, done by hand so the view keeps
            // Up for moving between rows (the SeriesPage pattern).
            Keys.onUpPressed: event => {
                if (list.currentIndex <= 0) {
                    trackTabs.forceActiveFocus(Qt.BacktabFocusReason);
                    event.accepted = true;
                } else {
                    event.accepted = false;
                }
            }

            Text {
                anchors.centerIn: parent
                width: parent.width - Theme.spacingValue
                visible: list.count === 0
                text: panel.tab === 0
                      ? qsTr("The engine has not reported any audio tracks.")
                      : qsTr("The engine has not reported any subtitle tracks.")
                color: Theme.textTertiary
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }

            delegate: Item {
                id: trackRow

                required property int index
                required property var modelData

                readonly property bool hovered: rowHover.hovered
                readonly property bool highlighted: trackRow.ListView.isCurrentItem
                                                    && list.activeFocus

                width: ListView.view.width
                height: Theme.scale(52)

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusChip
                    color: trackRow.highlighted ? Theme.surfaceRaisedColor
                         : trackRow.hovered ? Theme.hoverTint
                         : "transparent"

                    Behavior on color {
                        ColorAnimation {
                            duration: trackRow.highlighted ? Theme.animFastMs : Theme.animInstant
                            easing.type: trackRow.highlighted ? Theme.easeStandard
                                                              : Theme.easeInstant
                        }
                    }
                }

                StrmIcon {
                    id: check

                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacingTight
                    anchors.verticalCenter: parent.verticalCenter
                    name: "check"
                    size: Theme.iconSize
                    color: Theme.accentColor
                    opacity: trackRow.modelData.selected ? 1 : 0
                }

                Column {
                    anchors.left: check.right
                    anchors.leftMargin: Theme.spacingTight
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingTight
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.scale(2)

                    Text {
                        width: parent.width
                        text: trackRow.modelData.label
                        color: trackRow.modelData.selected ? Theme.accentColor
                                                           : Theme.textPrimaryColor
                        font.family: Theme.fontBody
                        font.pixelSize: Theme.fontBodySize
                        elide: Text.ElideRight
                    }

                    Text {
                        width: parent.width
                        visible: trackRow.modelData.detail.length > 0
                        text: trackRow.modelData.detail
                        color: Theme.textSecondaryColor
                        // Mono: codec, channel layout and flags are gear data.
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontCaption
                        elide: Text.ElideRight
                    }
                }

                HoverHandler {
                    id: rowHover
                    cursorShape: Qt.PointingHandCursor
                    // Hover previews; it never calls forceActiveFocus().
                }

                TapHandler {
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: {
                        list.currentIndex = trackRow.index;
                        list.forceActiveFocus(Qt.MouseFocusReason);
                        panel.applyRow(trackRow.index);
                    }
                }
            }
        }
    }
}
