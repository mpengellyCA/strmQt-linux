// Bound: the Repeater delegate reads this file's id (stats).
pragma ComponentBehavior: Bound

import QtQuick
import StrmQt

// StatsOverlay — stats for nerds, on Ctrl+I (ARCHITECTURE.md).
//
// The old OSD's answer to "what is actually happening" was one grey line:
// `streamMethod · decoderInfo`. This is the whole picture — what the server
// decided, why it decided it, and what the engine is doing with the result —
// laid out as a labelled table in mono, because every value in it is a number
// or an identifier and none of them should reflow as they tick.
//
// Rows whose value the engine cannot answer are ABSENT, not zero: PlayerBackend
// omits keys it has no answer for precisely so this panel can hide a row rather
// than print a lie.
Item {
    id: stats

    property bool shown: false

    signal closeRequested

    readonly property var backend: PlayerCtl.backend

    readonly property var videoStats: {
        // videoStats() performs synchronous mpv property reads. Keep that
        // dependency out of the binding entirely while the panel is hidden;
        // dropped-frame notifications then cost no mpv core-lock round trips.
        if (!stats.shown)
            return ({});
        const map = stats.backend.videoStats;
        return (map !== undefined && map !== null) ? map : ({});
    }

    function has(key: string): bool {
        return stats.videoStats[key] !== undefined;
    }

    function formatBitrate(bits: real): string {
        if (bits <= 0)
            return "";
        return bits >= 1000000 ? (bits / 1000000).toFixed(2) + " Mb/s"
                               : Math.round(bits / 1000) + " kb/s";
    }

    // Every row is { label, value }; empty values are dropped by the builder,
    // so the table never shows a label with nothing beside it.
    readonly property var rows: {
        // Dynamic binding dependencies are only the values read on this pass.
        // Returning before touching backend state makes the hidden overlay
        // inert instead of rebuilding an invisible delegate tree.
        if (!stats.shown)
            return [];
        const out = [];
        const s = stats.videoStats;
        const push = (label, value) => {
            if (value !== undefined && value !== null && String(value).length > 0)
                out.push({ "label": label, "value": String(value) });
        };

        // ── What the server decided ──
        push(qsTr("Play method"), PlayerCtl.streamMethod);
        const source = PlayerCtl.currentSource;
        if (source) {
            push(qsTr("Version"), source.displayName);
            push(qsTr("Container"), source.container);
            if (source.bitrate !== undefined && Number(source.bitrate) > 0)
                push(qsTr("Source bitrate"), stats.formatBitrate(Number(source.bitrate)));
            const reasons = source.transcodeReasons;
            if (reasons !== undefined && reasons !== null && reasons.length > 0)
                push(qsTr("Transcode reason"), Array.prototype.join.call(reasons, ", "));
        }

        // ── What the engine is doing ──
        if (stats.has("width") && stats.has("height"))
            push(qsTr("Resolution"), s.width + "×" + s.height);
        push(qsTr("Video codec"), s.codec);
        if (stats.has("videoBitrate"))
            push(qsTr("Video bitrate"), stats.formatBitrate(Number(s.videoBitrate)));
        if (stats.has("fps"))
            push(qsTr("Frame rate"), Number(s.fps).toFixed(3));
        if (stats.has("estimatedFps"))
            push(qsTr("Measured rate"), Number(s.estimatedFps).toFixed(3));
        if (stats.has("droppedFrames"))
            push(qsTr("Dropped frames"), String(Number(s.droppedFrames)));

        const hwdec = stats.has("hwdec") ? String(s.hwdec) : String(stats.backend.decoderInfo);
        push(qsTr("Hardware decode"), hwdec.length > 0 ? hwdec : qsTr("software"));

        const audioBits = [];
        if (stats.has("audioCodec"))
            audioBits.push(String(s.audioCodec).toUpperCase());
        if (stats.has("audioChannels"))
            audioBits.push(Number(s.audioChannels) + qsTr("ch"));
        if (stats.has("audioBitrate"))
            audioBits.push(stats.formatBitrate(Number(s.audioBitrate)));
        push(qsTr("Audio"), audioBits.join("  ·  "));

        push(qsTr("Engine"), stats.backend.engineName);
        return out;
    }

    readonly property string bufferedAhead: {
        if (!stats.shown)
            return "";
        const ahead = stats.backend.bufferedMs;
        return ahead !== undefined ? (Number(ahead) / 1000).toFixed(1) + " s" : "";
    }

    component StatRow: Item {
        id: statRow

        required property string rowLabel
        required property string rowValue

        width: parent.width
        implicitHeight: Math.max(labelText.contentHeight, valueLabel.contentHeight)
        height: implicitHeight

        Text {
            id: labelText

            anchors.left: parent.left
            anchors.top: parent.top
            width: Theme.scale(140)
            text: statRow.rowLabel
            color: Theme.textTertiary
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontCaption
            elide: Text.ElideRight
        }

        Text {
            id: valueLabel

            anchors.left: parent.left
            anchors.leftMargin: Theme.scale(146)
            anchors.right: parent.right
            anchors.top: parent.top
            text: statRow.rowValue
            color: Theme.textPrimaryColor
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontCaption
            wrapMode: Text.WordWrap
        }
    }

    implicitHeight: surface.implicitHeight
    height: implicitHeight

    visible: stats.opacity > 0.01
    enabled: stats.shown
    opacity: stats.shown ? 1 : 0

    Behavior on opacity {
        NumberAnimation {
            duration: Theme.animFastMs
            easing.type: Theme.easeStandard
        }
    }

    StrmPanel {
        id: surface

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        padding: Theme.spacingValue
        elevation: 3

        Item {
            width: parent.width
            height: Theme.controlHeight

            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Playback statistics")
                color: Theme.textPrimaryColor
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.fontBodyLarge
                font.weight: Font.DemiBold
            }

            StrmIconButton {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                iconName: "close"
                round: true
                tooltip: qsTr("Close")
                onClicked: stats.closeRequested()
            }
        }

        // The table. Fixed-width label column so the values line up as a column
        // of their own — that alignment is most of what makes a readout of
        // numbers scannable.
        Column {
            width: parent.width
            spacing: Theme.scale(3)

            Repeater {
                model: stats.rows

                delegate: StatRow {
                    required property var modelData
                    rowLabel: String(modelData.label)
                    rowValue: String(modelData.value)
                }
            }

            // The buffered counter is the only value that ticks every few
            // hundred milliseconds. Updating one row avoids replacing the
            // Repeater's complete model on every cache notification.
            StatRow {
                visible: stats.bufferedAhead.length > 0
                height: visible ? implicitHeight : 0
                rowLabel: qsTr("Buffered ahead")
                rowValue: stats.bufferedAhead
            }
        }
    }
}
