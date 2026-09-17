#include "server/emby/EmbyMusicMapper.h"

#include <QRegularExpression>

namespace strmqt::emby {

using namespace music;

namespace {

int integer(const QJsonValue &value)
{
    return static_cast<int>(value.toVariant().toLongLong());
}

// "44.1" for 44100, "96" for 96000, "88.2" for 88200.
QString khz(int hz)
{
    const int tenths = qRound(hz / 100.0);
    if (tenths % 10 == 0)
        return QString::number(tenths / 10);
    return QStringLiteral("%1.%2").arg(tenths / 10).arg(tenths % 10);
}

bool isDsd(const QString &codec) { return codec.startsWith(QLatin1String("dsd")); }
bool isPcm(const QString &codec)
{
    return codec.startsWith(QLatin1String("pcm_")) || codec == QLatin1String("wav");
}

QString codecLabel(const QString &codec)
{
    if (isDsd(codec))
        return QStringLiteral("DSD");
    if (isPcm(codec))
        return QStringLiteral("PCM");
    if (codec == QLatin1String("wavpack"))
        return QStringLiteral("WV");
    if (codec == QLatin1String("vorbis"))
        return QStringLiteral("OGG");
    return codec.toUpper();
}

} // namespace

AudioFormat deriveAudioFormat(const QString &codecIn, int bitDepth, int sampleRate, int bitrate,
                              int channels)
{
    const QString codec = codecIn.trimmed().toLower();
    if (codec.isEmpty())
        return {};

    static const QStringList kLossless = {
        QStringLiteral("flac"), QStringLiteral("alac"), QStringLiteral("ape"),
        QStringLiteral("wavpack"), QStringLiteral("tta"), QStringLiteral("truehd"),
        QStringLiteral("mlp")};

    AudioFormat format;
    format.codec = codec;
    format.bitDepth = qMax(0, bitDepth);
    format.sampleRate = qMax(0, sampleRate);
    format.bitrate = qMax(0, bitrate);
    format.channels = qMax(0, channels);
    format.isLossless = isDsd(codec) || isPcm(codec) || kLossless.contains(codec);
    format.isHiRes = format.isLossless && (format.bitDepth > 16 || format.sampleRate > 48000);

    const QString label = codecLabel(codec);
    if (isDsd(codec)) {
        format.isHiRes = true;
        // The real server reports DSF files at ffmpeg's decimated rate (DSD64 is
        // sent as SampleRate=352800, i.e. the true DSD rate / 8). Scale up before
        // deriving the DSDn number when the reported rate looks decimated;
        // format.sampleRate itself keeps the value the server reported.
        qint64 dsdRate = format.sampleRate;
        if (dsdRate > 0 && dsdRate < 1'000'000)
            dsdRate *= 8;
        format.badge = dsdRate > 0
                           ? QStringLiteral("DSD%1").arg(qRound(dsdRate / 44100.0))
                           : label;
    } else if (format.isLossless) {
        format.badge = (format.bitDepth > 0 && format.sampleRate > 0)
                           ? QStringLiteral("%1 %2/%3").arg(label).arg(format.bitDepth).arg(khz(format.sampleRate))
                           : label;
    } else {
        format.badge = format.bitrate > 0
                           ? QStringLiteral("%1 %2").arg(label).arg(qRound(format.bitrate / 1000.0))
                           : label;
    }
    return format;
}

AudioFormat parseAudioFormat(const QJsonObject &item)
{
    auto fromStreams = [](const QJsonArray &streams, const QString &container) -> AudioFormat {
        for (const QJsonValue &value : streams) {
            const QJsonObject stream = value.toObject();
            if (stream.value(QStringLiteral("Type")).toString() != QLatin1String("Audio"))
                continue;
            QString codec = stream.value(QStringLiteral("Codec")).toString();
            if (codec.isEmpty())
                codec = container;
            return deriveAudioFormat(codec, integer(stream.value(QStringLiteral("BitDepth"))),
                                     integer(stream.value(QStringLiteral("SampleRate"))),
                                     integer(stream.value(QStringLiteral("BitRate"))),
                                     integer(stream.value(QStringLiteral("Channels"))));
        }
        return {};
    };

    const QString container = item.value(QStringLiteral("Container")).toString();
    AudioFormat format = fromStreams(item.value(QStringLiteral("MediaStreams")).toArray(), container);
    if (format.isValid())
        return format;
    const QJsonArray sources = item.value(QStringLiteral("MediaSources")).toArray();
    const QJsonObject source = sources.isEmpty() ? QJsonObject{} : sources.at(0).toObject();
    QString sourceContainer = source.value(QStringLiteral("Container")).toString();
    if (sourceContainer.isEmpty())
        sourceContainer = container;
    return fromStreams(source.value(QStringLiteral("MediaStreams")).toArray(), sourceContainer);
}

FeaturedSplit splitFeatured(const QString &title)
{
    static const QRegularExpression kFeat(
        QStringLiteral(R"(\s*[\(\[]\s*(?:feat\.?|ft\.?|featuring)\s+([^\)\]]+)[\)\]]\s*$|\s+(?:feat\.|ft\.|featuring)\s+(.+)$)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression kSeparators(QStringLiteral(R"(\s*(?:,|&)\s*)"));

    const QRegularExpressionMatch match = kFeat.match(title);
    if (!match.hasMatch())
        return {title, {}};
    const QString names = match.captured(1).isEmpty() ? match.captured(2) : match.captured(1);
    QStringList list;
    for (const QString &name : names.split(kSeparators, Qt::SkipEmptyParts)) {
        const QString trimmed = name.trimmed();
        if (!trimmed.isEmpty())
            list.append(trimmed);
    }
    return {title.left(match.capturedStart()).trimmed(), list};
}

} // namespace strmqt::emby
