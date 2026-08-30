pragma Singleton
import QtQuick

// One structured reading of the currently presented queue entry. Playback
// surfaces choose their own geometry, but do not separately guess what the
// title, episode context, artwork, credits, or clocks mean.
QtObject {
    id: info

    readonly property var queue: {
        const value = PlayerCtl.queue;
        return value !== undefined && value !== null ? value : null;
    }
    readonly property var item: {
        const value = info.queue;
        if (value === null || value.currentIndex < 0 || value.count <= 0)
            return ({});
        return value.itemAt(value.currentIndex);
    }

    readonly property bool isAudio: PlayerCtl.isAudio === true
    readonly property string itemId: field("itemId")
    readonly property string title: field("name").length > 0
                                    ? field("name") : String(PlayerCtl.title || "")
    readonly property string seriesName: field("seriesName")
    readonly property int seasonNumber: numberField("parentIndexNumber", -1)
    readonly property int episodeNumber: numberField("indexNumber", -1)
    readonly property string episodeCode: MediaKinds.episodeCode(info.seasonNumber,
                                                                 info.episodeNumber)
    readonly property string videoContext: {
        const parts = [];
        if (info.seriesName.length > 0)
            parts.push(info.seriesName);
        if (info.episodeCode.length > 0)
            parts.push(info.episodeCode);
        if (parts.length > 0)
            return parts.join("  ·  ");
        return info.field("subtitle");
    }

    readonly property var artistTarget: {
        const target = Actions.artistTarget(info.item);
        return target !== undefined && target !== null ? target : ({});
    }
    readonly property string artistName: info.artistTarget.name !== undefined
                                         ? String(info.artistTarget.name) : ""
    readonly property string artistId: info.artistTarget.itemId !== undefined
                                       ? String(info.artistTarget.itemId) : ""
    readonly property string artistsText: {
        const names = info.item.artists;
        if (names !== undefined && names !== null && names.length > 0)
            return Array.prototype.join.call(names, ", ");
        return info.artistName.length > 0 ? info.artistName : info.field("albumArtist");
    }
    readonly property string albumName: field("album")
    readonly property string albumId: field("albumId")

    readonly property string audioArtUrl: firstField(["posterUrl", "thumbUrl"])
    readonly property string videoArtUrl: firstField(["thumbUrl", "backdropUrl", "posterUrl"])
    readonly property string artUrl: info.isAudio ? info.audioArtUrl : info.videoArtUrl
    readonly property bool videoArtIsWide: !info.isAudio
                                           && (field("thumbUrl").length > 0
                                               || field("backdropUrl").length > 0)

    readonly property real positionMs: Number(PlayerCtl.positionMs)
    readonly property real positionSeconds: Number(PlayerCtl.positionSeconds)
    readonly property real durationMs: Number(PlayerCtl.durationMs)
    readonly property bool seekable: info.durationMs > 0
    readonly property real bufferedPosition: Number(PlayerCtl.bufferedEndMs)
    readonly property string elapsedText: formatTime(info.positionSeconds * 1000)
    readonly property string remainingText: info.seekable
        ? "−" + formatTime(Math.max(0, info.durationMs - info.positionSeconds * 1000)) : "--:--"
    readonly property string timeText: info.elapsedText + "  /  " + info.remainingText
    readonly property string queueContext: info.queue !== null
                                           ? String(info.queue.contextLabel) : ""

    readonly property var audioStream: {
        const streams = PlayerCtl.audioStreams;
        return streams !== undefined && streams !== null && streams.length > 0
               ? streams[0] : null;
    }
    readonly property string streamMethodLabel: {
        const method = String(PlayerCtl.streamMethod || "");
        if (method === "DirectPlay")
            return qsTr("Direct play");
        if (method === "DirectStream")
            return qsTr("Direct stream");
        return method.length > 0 ? qsTr("Transcoding") : "";
    }
    readonly property string audioTechnicalText: {
        const parts = [];
        const stream = info.audioStream;
        const source = PlayerCtl.currentSource;
        let format = "";
        if (stream !== null && String(stream.codec || "").length > 0)
            format = String(stream.codec).toUpperCase();
        else if (source && String(source.container || "").length > 0)
            format = String(source.container).toUpperCase();
        if (stream !== null && Number(stream.sampleRate) > 0) {
            const khz = Number(stream.sampleRate) / 1000;
            const rate = khz === Math.round(khz) ? String(Math.round(khz)) : khz.toFixed(1);
            format += (format.length > 0 ? " " : "")
                    + (Number(stream.bitDepth) > 0 ? stream.bitDepth + "/" + rate
                                                   : rate + " kHz");
        }
        if (format.length > 0)
            parts.push(format);
        const bitrate = stream !== null && Number(stream.bitRate) > 0
                        ? Number(stream.bitRate) : Number(source && source.bitrate);
        if (bitrate > 0)
            parts.push(qsTr("%1 kbps").arg(Math.round(bitrate / 1000)
                                           .toLocaleString(Qt.locale(), "f", 0)));
        if (info.streamMethodLabel.length > 0)
            parts.push(info.streamMethodLabel);
        if (stream !== null && String(stream.channelLayout || "").length > 0
            && String(stream.channelLayout) !== "stereo")
            parts.push(String(stream.channelLayout));
        return parts.join("  ·  ");
    }
    readonly property var videoTechChips: {
        const chips = [];
        const method = String(PlayerCtl.streamMethod || "");
        if (method.length > 0)
            chips.push(method);
        const source = PlayerCtl.currentSource;
        const video = PlayerCtl.videoStream;
        const format = [];
        if (source && source.resolutionLabel)
            format.push(String(source.resolutionLabel));
        if (video && video.codec)
            format.push(String(video.codec).toUpperCase());
        if (format.length > 0)
            chips.push(format.join(" "));
        if (source && source.isHdr === true)
            chips.push("HDR");
        const backend = PlayerCtl.backend;
        const decoder = backend ? String(backend.decoderInfo || "") : "";
        if (decoder.length > 0)
            chips.push("hwdec " + decoder);
        return chips;
    }

    function field(name: string): string {
        const value = info.item[name];
        return value !== undefined && value !== null ? String(value) : "";
    }

    function numberField(name: string, fallback: int): int {
        const raw = info.item[name];
        if (raw === undefined || raw === null)
            return fallback;
        const value = Number(raw);
        return isNaN(value) ? fallback : value;
    }

    function firstField(names: var): string {
        for (let i = 0; i < names.length; ++i) {
            const value = info.field(names[i]);
            if (value.length > 0)
                return value;
        }
        return "";
    }

    function formatTime(ms: real): string {
        const totalSeconds = Math.max(0, Math.floor(ms / 1000));
        const hours = Math.floor(totalSeconds / 3600);
        const minutes = Math.floor((totalSeconds / 60) % 60);
        const seconds = totalSeconds % 60;
        const pad = value => (value < 10 ? "0" : "") + value;
        return hours > 0 ? hours + ":" + pad(minutes) + ":" + pad(seconds)
                         : minutes + ":" + pad(seconds);
    }

    // A duration in a list row rather than a clock: absent or non-positive
    // input renders as the caller's fallback ("", "–:––") rather than as
    // "0:00", and the value is rounded — a 59.6 s track is "1:00", which a
    // floor would read as 59 s forever. m:ss, or h:mm:ss past the hour.
    function formatDuration(ms: real, emptyText: string): string {
        const totalSeconds = Math.round(Number(ms) / 1000);
        if (!isFinite(totalSeconds) || totalSeconds <= 0)
            return emptyText;
        const hours = Math.floor(totalSeconds / 3600);
        const minutes = Math.floor((totalSeconds / 60) % 60);
        const seconds = totalSeconds % 60;
        const pad = value => (value < 10 ? "0" : "") + value;
        return hours > 0 ? hours + ":" + pad(minutes) + ":" + pad(seconds)
                         : minutes + ":" + pad(seconds);
    }
}
