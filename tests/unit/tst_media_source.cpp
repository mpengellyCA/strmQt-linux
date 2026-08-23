#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

#include "server/emby/EmbyDtoMapper.h"

using namespace strmqt;

namespace {

QJsonObject loadFixture(const QString &name)
{
    QFile file(QStringLiteral(STRMQT_FIXTURES_DIR "/") + name);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

// The two MediaSources of the multi-version fixture.
MediaSource fixtureSource(int index)
{
    const QJsonArray sources = loadFixture(QStringLiteral("playback_info_multi_version.json"))
                                   .value(QLatin1String("MediaSources"))
                                   .toArray();
    return emby::parseMediaSource(sources.at(index).toObject());
}

MediaStream streamAt(const MediaSource &source, int index)
{
    for (const MediaStream &stream : source.streams) {
        if (stream.index == index)
            return stream;
    }
    return {};
}

} // namespace

class MediaSourceTest : public QObject
{
    Q_OBJECT

private slots:
    void videoStreamFields();
    void audioStreamFields();
    void subtitleFlags();
    void streamsSplitByKind();
    void hdrDetection();
    void resolutionLabels();
    void displayNameFallbacks();
    void streamLabelFallbacks();
    void emptyObjectsAreSafe();
    void variantMapsCarryTheUiFields();
};

void MediaSourceTest::videoStreamFields()
{
    const MediaSource uhd = fixtureSource(0);
    const MediaStream *video = uhd.videoStream();
    QVERIFY(video);
    QCOMPARE(video->index, 0);
    QCOMPARE(video->type, QStringLiteral("Video"));
    QCOMPARE(video->kind(), MediaStream::Kind::Video);
    QVERIFY(video->isVideo());
    QCOMPARE(video->codec, QStringLiteral("hevc"));
    QCOMPARE(video->width, 3840);
    QCOMPARE(video->height, 2160);
    QCOMPARE(video->videoRange, QStringLiteral("HDR10"));
    QCOMPARE(video->profile, QStringLiteral("Main 10"));
    QCOMPARE(video->bitDepth, 10);
    QCOMPARE(video->bitRate, Q_INT64_C(72000000));
    QVERIFY(qFuzzyCompare(video->frameRate, 23.976));
    QVERIFY(video->isDefault);

    // The 1080p source reports AverageFrameRate instead of RealFrameRate.
    const MediaStream *hdVideo = fixtureSource(1).videoStream();
    QVERIFY(hdVideo);
    QVERIFY(qFuzzyCompare(hdVideo->frameRate, 23.976));
}

void MediaSourceTest::audioStreamFields()
{
    const MediaSource uhd = fixtureSource(0);
    const QList<MediaStream> audio = uhd.audioStreams();
    QCOMPARE(audio.size(), 2);

    QCOMPARE(audio[0].language, QStringLiteral("eng"));
    QCOMPARE(audio[0].codec, QStringLiteral("truehd"));
    QCOMPARE(audio[0].channels, 8);
    QCOMPARE(audio[0].channelLayout, QStringLiteral("7.1"));
    QCOMPARE(audio[0].sampleRate, 48000);
    QCOMPARE(audio[0].bitRate, Q_INT64_C(4500000));
    QCOMPARE(audio[0].label(), QStringLiteral("English TrueHD Atmos 7.1"));
    QVERIFY(audio[0].isDefault);
    QVERIFY(audio[0].isAudio());

    QCOMPARE(audio[1].language, QStringLiteral("fre"));
    QVERIFY(!audio[1].isDefault);
    QCOMPARE(audio[1].bitRate, Q_INT64_C(0)); // absent on the wire
}

void MediaSourceTest::subtitleFlags()
{
    const QList<MediaStream> subs = fixtureSource(0).subtitleStreams();
    QCOMPARE(subs.size(), 2);

    QCOMPARE(subs[0].language, QStringLiteral("eng"));
    QVERIFY(subs[0].isForced);
    QVERIFY(subs[0].isDefault);
    QVERIFY(!subs[0].isExternal);
    // No DisplayTitle on this one → Title is the label.
    QCOMPARE(subs[0].label(), QStringLiteral("English (Forced)"));

    QCOMPARE(subs[1].language, QStringLiteral("nld"));
    QVERIFY(!subs[1].isForced);
    QVERIFY(!subs[1].isDefault);
    QVERIFY(subs[1].isExternal);
    QVERIFY(subs[1].isSubtitle());
}

void MediaSourceTest::streamsSplitByKind()
{
    const MediaSource uhd = fixtureSource(0);
    QCOMPARE(uhd.streams.size(), 6);
    QCOMPARE(uhd.audioStreams().size(), 2);
    QCOMPARE(uhd.subtitleStreams().size(), 2);
    QVERIFY(uhd.videoStream());
    // "Embedded Image" is classified, and deliberately not an audio/sub/video.
    QCOMPARE(streamAt(uhd, 5).kind(), MediaStream::Kind::EmbeddedImage);
    QVERIFY(!streamAt(uhd, 5).isVideo());

    // Both spellings Emby has shipped map to the same kind.
    MediaStream compact;
    compact.type = QStringLiteral("EmbeddedImage");
    QCOMPARE(compact.kind(), MediaStream::Kind::EmbeddedImage);
    MediaStream unknown;
    unknown.type = QStringLiteral("Lyric");
    QCOMPARE(unknown.kind(), MediaStream::Kind::Other);
    QCOMPARE(unknown.type, QStringLiteral("Lyric")); // wire string preserved
}

void MediaSourceTest::hdrDetection()
{
    QVERIFY(fixtureSource(0).isHdr());  // HDR10
    QVERIFY(!fixtureSource(1).isHdr()); // SDR

    MediaStream stream;
    stream.type = QStringLiteral("Video");
    QVERIFY(!stream.isHdr()); // absent VideoRange is not HDR
    for (const QString &range : {QStringLiteral("HDR"), QStringLiteral("HDR10"),
                                 QStringLiteral("HDR10+"), QStringLiteral("DV"),
                                 QStringLiteral("DOVIWithHDR10")}) {
        stream.videoRange = range;
        QVERIFY2(stream.isHdr(), qPrintable(range));
    }
    stream.videoRange = QStringLiteral("sdr"); // casing must not matter
    QVERIFY(!stream.isHdr());

    // An HDR audio-typed stream must not make the source HDR.
    MediaSource source;
    MediaStream audio;
    audio.type = QStringLiteral("Audio");
    audio.videoRange = QStringLiteral("HDR10");
    source.streams = {audio};
    QVERIFY(!source.isHdr());
}

void MediaSourceTest::resolutionLabels()
{
    struct Case
    {
        int width;
        int height;
        QString label;
    };
    const QList<Case> cases = {
        {3840, 2160, QStringLiteral("4K")},   {4096, 1716, QStringLiteral("4K")},
        {2560, 1440, QStringLiteral("1440p")},{1920, 1080, QStringLiteral("1080p")},
        {1920, 800, QStringLiteral("1080p")}, {1280, 720, QStringLiteral("720p")},
        {720, 480, QStringLiteral("480p")},   {320, 240, QStringLiteral("240p")},
        {0, 0, QString()},
    };
    for (const Case &c : cases) {
        MediaSource source;
        MediaStream video;
        video.type = QStringLiteral("Video");
        video.width = c.width;
        video.height = c.height;
        source.streams = {video};
        QCOMPARE(source.resolutionLabel(), c.label);
    }

    // No video stream at all (audio-only source, or no metadata requested).
    QCOMPARE(MediaSource().resolutionLabel(), QString());
}

void MediaSourceTest::displayNameFallbacks()
{
    QCOMPARE(fixtureSource(0).displayName(), QStringLiteral("4K Remux")); // server name wins

    MediaSource source;
    MediaStream video;
    video.type = QStringLiteral("Video");
    video.width = 1920;
    video.height = 1080;
    video.codec = QStringLiteral("h264");
    source.streams = {video};
    source.container = QStringLiteral("mkv");
    QCOMPARE(source.displayName(), QStringLiteral("1080p H264"));

    source.streams.clear();
    QCOMPARE(source.displayName(), QStringLiteral("MKV"));

    source.container.clear();
    QCOMPARE(source.displayName(), QStringLiteral("Unknown"));
}

void MediaSourceTest::streamLabelFallbacks()
{
    MediaStream stream;
    stream.type = QStringLiteral("Audio");
    stream.codec = QStringLiteral("dts");
    stream.language = QStringLiteral("eng");
    stream.channelLayout = QStringLiteral("5.1");
    QCOMPARE(stream.label(), QStringLiteral("eng DTS 5.1"));

    stream.title = QStringLiteral("Commentary");
    QCOMPARE(stream.label(), QStringLiteral("Commentary"));
    stream.displayTitle = QStringLiteral("English DTS 5.1 - Commentary");
    QCOMPARE(stream.label(), QStringLiteral("English DTS 5.1 - Commentary"));

    MediaStream bare;
    bare.type = QStringLiteral("Subtitle");
    QCOMPARE(bare.label(), QStringLiteral("Subtitle"));
}

void MediaSourceTest::emptyObjectsAreSafe()
{
    const MediaStream stream = emby::parseMediaStream(QJsonObject());
    QCOMPARE(stream.index, -1);
    QCOMPARE(stream.kind(), MediaStream::Kind::Other);
    QVERIFY(!stream.isHdr());
    QCOMPARE(stream.label(), QString());
    QVERIFY(!stream.toVariantMap().isEmpty());

    const MediaSource source = emby::parseMediaSource(QJsonObject());
    QVERIFY(source.id.isEmpty());
    QVERIFY(source.streams.isEmpty());
    QVERIFY(!source.videoStream());
    QVERIFY(source.audioStreams().isEmpty());
    QVERIFY(source.subtitleStreams().isEmpty());
    QCOMPARE(source.displayName(), QStringLiteral("Unknown"));

    QVERIFY(emby::parseMediaSources(QJsonArray()).isEmpty());
    QVERIFY(emby::parseMediaSources(QJsonArray({QJsonValue(1), QJsonValue(QJsonValue::Null)}))
                .isEmpty());
}

void MediaSourceTest::variantMapsCarryTheUiFields()
{
    const QVariantMap map = fixtureSource(0).toVariantMap();
    QCOMPARE(map.value(QStringLiteral("displayName")).toString(), QStringLiteral("4K Remux"));
    QCOMPARE(map.value(QStringLiteral("resolutionLabel")).toString(), QStringLiteral("4K"));
    QCOMPARE(map.value(QStringLiteral("isHdr")).toBool(), true);
    QCOMPARE(map.value(QStringLiteral("size")).toLongLong(), Q_INT64_C(68719476736));
    QCOMPARE(map.value(QStringLiteral("bitrate")).toLongLong(), Q_INT64_C(78450000));
    QCOMPARE(map.value(QStringLiteral("audioStreams")).toList().size(), 2);
    QCOMPARE(map.value(QStringLiteral("subtitleStreams")).toList().size(), 2);
    QCOMPARE(map.value(QStringLiteral("transcodeReasons")).toStringList().size(), 2);

    const QVariantMap video = map.value(QStringLiteral("videoStream")).toMap();
    QCOMPARE(video.value(QStringLiteral("codec")).toString(), QStringLiteral("hevc"));
    QCOMPARE(video.value(QStringLiteral("bitDepth")).toInt(), 10);

    const QVariantMap forced =
        map.value(QStringLiteral("subtitleStreams")).toList().first().toMap();
    QCOMPARE(forced.value(QStringLiteral("isForced")).toBool(), true);
    QCOMPARE(forced.value(QStringLiteral("label")).toString(), QStringLiteral("English (Forced)"));

    // A source with no video stream still yields a well-formed (empty) entry.
    QVERIFY(MediaSource().toVariantMap().value(QStringLiteral("videoStream")).toMap().isEmpty());
}

QTEST_GUILESS_MAIN(MediaSourceTest)
#include "tst_media_source.moc"
