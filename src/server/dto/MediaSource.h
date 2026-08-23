#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

namespace strmqt {

// One elementary stream inside a MediaSource (Emby "MediaStreams" entry).
//
// Every field is optional on the wire; the defaults here are the contract the
// rest of the app codes against (AGENTS.md — Emby drift must never crash us).
// The wire `type` string is kept verbatim so an unknown kind still round-trips
// to the UI; `kind()` is the convenience classification.
struct MediaStream
{
    enum class Kind {
        Video,
        Audio,
        Subtitle,
        EmbeddedImage,
        Other, // anything the server invents that we do not model
    };

    int index = -1;
    QString type; // "Video" | "Audio" | "Subtitle" | "Embedded Image" | …
    QString codec;
    QString language; // ISO 639-2 code as the server reports it ("eng")
    QString displayTitle;
    QString title;
    bool isDefault = false;
    bool isForced = false;
    bool isExternal = false;

    // Video
    int width = 0;
    int height = 0;
    QString videoRange; // "SDR" | "HDR" | "HDR10" | "HDR10+" | "DV" | …
    double frameRate = 0.0;
    QString profile;
    int bitDepth = 0;

    // Audio
    int channels = 0;
    QString channelLayout; // "5.1", "stereo", …
    int sampleRate = 0;

    // Video and audio both carry a per-stream bitrate (bits/s).
    qint64 bitRate = 0;

    Kind kind() const;
    bool isVideo() const { return kind() == Kind::Video; }
    bool isAudio() const { return kind() == Kind::Audio; }
    bool isSubtitle() const { return kind() == Kind::Subtitle; }

    // True for any non-SDR video range the server reports.
    bool isHdr() const;

    // Best available human label: server DisplayTitle, else Title, else a
    // language/codec summary, else the codec alone.
    QString label() const;

    QVariantMap toVariantMap() const;
};

// One playable version of an item (Emby "MediaSources" entry). An item can have
// several — a 4K remux and a 1080p encode, say — and each carries its own
// delivery capabilities and its own stream list.
struct MediaSource
{
    QString id;
    QString name;
    QString container;
    qint64 size = 0;         // bytes
    qint64 bitrate = 0;      // bits/s, whole-source
    qint64 runtimeTicks = 0; // 100 ns ticks
    QString protocol;        // "File" | "Http" | …
    bool supportsDirectPlay = false;
    bool supportsDirectStream = false;
    bool supportsTranscoding = false;
    // Server-relative URLs exactly as sent; the mapper resolves them against the
    // base URL when it builds StreamCandidates.
    QString directStreamUrl;
    QString transcodingUrl;
    QString transcodingContainer;
    // Why the server will not direct play ("VideoCodecNotSupported", …). Exactly
    // what a stats overlay needs to explain a transcode.
    QStringList transcodeReasons;
    QList<MediaStream> streams;

    // Server name if present, else a resolution + codec summary, else the
    // container, else "Unknown".
    QString displayName() const;
    // "4K" | "1440p" | "1080p" | "720p" | "480p" | "<h>p" | empty when unknown.
    QString resolutionLabel() const;
    bool isHdr() const;

    // nullptr when the source has no video stream (audio-only, or no metadata).
    const MediaStream *videoStream() const;
    QList<MediaStream> audioStreams() const;
    QList<MediaStream> subtitleStreams() const;

    QVariantMap toVariantMap() const;

private:
    QList<MediaStream> streamsOfKind(MediaStream::Kind kind) const;
};

// ── inline implementations ─────────────────────────────────────────────────────

inline MediaStream::Kind MediaStream::kind() const
{
    if (type.compare(QLatin1String("Video"), Qt::CaseInsensitive) == 0)
        return Kind::Video;
    if (type.compare(QLatin1String("Audio"), Qt::CaseInsensitive) == 0)
        return Kind::Audio;
    if (type.compare(QLatin1String("Subtitle"), Qt::CaseInsensitive) == 0)
        return Kind::Subtitle;
    // Emby has shipped both spellings of this one.
    if (type.compare(QLatin1String("Embedded Image"), Qt::CaseInsensitive) == 0 ||
        type.compare(QLatin1String("EmbeddedImage"), Qt::CaseInsensitive) == 0)
        return Kind::EmbeddedImage;
    return Kind::Other;
}

inline bool MediaStream::isHdr() const
{
    if (videoRange.isEmpty())
        return false;
    return videoRange.compare(QLatin1String("SDR"), Qt::CaseInsensitive) != 0;
}

inline QString MediaStream::label() const
{
    if (!displayTitle.isEmpty())
        return displayTitle;
    if (!title.isEmpty())
        return title;
    QStringList parts;
    if (!language.isEmpty())
        parts << language;
    if (!codec.isEmpty())
        parts << codec.toUpper();
    if (!channelLayout.isEmpty())
        parts << channelLayout;
    if (parts.isEmpty())
        return type;
    return parts.join(QLatin1String(" "));
}

inline QVariantMap MediaStream::toVariantMap() const
{
    QVariantMap map;
    map.insert(QStringLiteral("index"), index);
    map.insert(QStringLiteral("type"), type);
    map.insert(QStringLiteral("codec"), codec);
    map.insert(QStringLiteral("language"), language);
    map.insert(QStringLiteral("displayTitle"), displayTitle);
    map.insert(QStringLiteral("title"), title);
    map.insert(QStringLiteral("label"), label());
    map.insert(QStringLiteral("isDefault"), isDefault);
    map.insert(QStringLiteral("isForced"), isForced);
    map.insert(QStringLiteral("isExternal"), isExternal);
    map.insert(QStringLiteral("width"), width);
    map.insert(QStringLiteral("height"), height);
    map.insert(QStringLiteral("videoRange"), videoRange);
    map.insert(QStringLiteral("frameRate"), frameRate);
    map.insert(QStringLiteral("profile"), profile);
    map.insert(QStringLiteral("bitDepth"), bitDepth);
    map.insert(QStringLiteral("channels"), channels);
    map.insert(QStringLiteral("channelLayout"), channelLayout);
    map.insert(QStringLiteral("sampleRate"), sampleRate);
    map.insert(QStringLiteral("bitRate"), QVariant::fromValue(bitRate));
    map.insert(QStringLiteral("isHdr"), isHdr());
    return map;
}

inline QList<MediaStream> MediaSource::streamsOfKind(MediaStream::Kind wanted) const
{
    QList<MediaStream> result;
    for (const MediaStream &stream : streams) {
        if (stream.kind() == wanted)
            result.append(stream);
    }
    return result;
}

inline const MediaStream *MediaSource::videoStream() const
{
    for (const MediaStream &stream : streams) {
        if (stream.isVideo())
            return &stream;
    }
    return nullptr;
}

inline QList<MediaStream> MediaSource::audioStreams() const
{
    return streamsOfKind(MediaStream::Kind::Audio);
}

inline QList<MediaStream> MediaSource::subtitleStreams() const
{
    return streamsOfKind(MediaStream::Kind::Subtitle);
}

inline QString MediaSource::resolutionLabel() const
{
    const MediaStream *video = videoStream();
    if (!video)
        return {};
    const int w = video->width;
    const int h = video->height;
    if (w <= 0 && h <= 0)
        return {};
    if (w >= 3800 || h >= 2000)
        return QStringLiteral("4K");
    if (w >= 2500 || h >= 1400)
        return QStringLiteral("1440p");
    if (w >= 1800 || h >= 1000)
        return QStringLiteral("1080p");
    if (w >= 1200 || h >= 700)
        return QStringLiteral("720p");
    if (w >= 700 || h >= 400)
        return QStringLiteral("480p");
    return h > 0 ? QStringLiteral("%1p").arg(h) : QString();
}

inline bool MediaSource::isHdr() const
{
    for (const MediaStream &stream : streams) {
        if (stream.isVideo() && stream.isHdr())
            return true;
    }
    return false;
}

inline QString MediaSource::displayName() const
{
    if (!name.isEmpty())
        return name;
    QStringList parts;
    const QString resolution = resolutionLabel();
    if (!resolution.isEmpty())
        parts << resolution;
    if (const MediaStream *video = videoStream(); video && !video->codec.isEmpty())
        parts << video->codec.toUpper();
    if (!parts.isEmpty())
        return parts.join(QLatin1String(" "));
    if (!container.isEmpty())
        return container.toUpper();
    return QStringLiteral("Unknown");
}

inline QVariantMap MediaSource::toVariantMap() const
{
    QVariantMap map;
    map.insert(QStringLiteral("id"), id);
    map.insert(QStringLiteral("name"), name);
    map.insert(QStringLiteral("displayName"), displayName());
    map.insert(QStringLiteral("container"), container);
    map.insert(QStringLiteral("size"), QVariant::fromValue(size));
    map.insert(QStringLiteral("bitrate"), QVariant::fromValue(bitrate));
    map.insert(QStringLiteral("runtimeTicks"), QVariant::fromValue(runtimeTicks));
    map.insert(QStringLiteral("protocol"), protocol);
    map.insert(QStringLiteral("supportsDirectPlay"), supportsDirectPlay);
    map.insert(QStringLiteral("supportsDirectStream"), supportsDirectStream);
    map.insert(QStringLiteral("supportsTranscoding"), supportsTranscoding);
    map.insert(QStringLiteral("transcodeReasons"), transcodeReasons);
    map.insert(QStringLiteral("resolutionLabel"), resolutionLabel());
    map.insert(QStringLiteral("isHdr"), isHdr());
    if (const MediaStream *video = videoStream())
        map.insert(QStringLiteral("videoStream"), video->toVariantMap());
    else
        map.insert(QStringLiteral("videoStream"), QVariantMap());

    const auto pack = [](const QList<MediaStream> &list) {
        QVariantList out;
        out.reserve(list.size());
        for (const MediaStream &stream : list)
            out.append(stream.toVariantMap());
        return out;
    };
    map.insert(QStringLiteral("audioStreams"), pack(audioStreams()));
    map.insert(QStringLiteral("subtitleStreams"), pack(subtitleStreams()));
    return map;
}

} // namespace strmqt
