#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>
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

const QUrl kBase(QStringLiteral("https://server.example"));
const auto kToken = QStringLiteral("tok123");
const auto kDeviceId = QStringLiteral("dev456");

} // namespace

class PlaybackTicketTest : public QObject
{
    Q_OBJECT

private slots:
    void singleSourceLadderOrdering();
    void transcodeOnly();
    void emptyResponseIsInvalid();
    void reverseProxyBasePathPreserved();

    void multiVersionGetsOneLadderEach();
    void demotionNeverCrossesSources();
    void defaultSourceSkipsUnplayable();
    void malformedPayloadDegradesToDefaults();
    void missingMediaSourcesArray();
};

void PlaybackTicketTest::singleSourceLadderOrdering()
{
    const PlaybackTicket ticket =
        emby::parsePlaybackTicket(loadFixture(QStringLiteral("playback_info.json")), kBase,
                                  QStringLiteral("301001"), kToken, kDeviceId);

    QVERIFY(ticket.isValid());
    QCOMPARE(ticket.playSessionId, QStringLiteral("ps0011"));
    QCOMPARE(ticket.sourceCount(), 1);
    QCOMPARE(ticket.defaultSourceIndex(), 0);
    QCOMPARE(ticket.runtimeTicks(0), Q_INT64_C(81840000000));

    const MediaSourceCandidates *entry = ticket.source(0);
    QVERIFY(entry);
    QCOMPARE(entry->source.id, QStringLiteral("ms301001"));
    QCOMPARE(entry->source.name, QStringLiteral("The Matrix"));
    QCOMPARE(entry->source.protocol, QStringLiteral("File"));
    QCOMPARE(entry->source.size, Q_INT64_C(4232567890));
    QCOMPARE(entry->source.bitrate, Q_INT64_C(6892345));
    QCOMPARE(entry->candidates.size(), 3);
    QCOMPARE(ticket.rungCount(0), 3);

    const StreamCandidate &direct = entry->candidates[0];
    QCOMPARE(direct.method, PlayMethod::DirectPlay);
    QCOMPARE(direct.container, QStringLiteral("mkv"));
    QCOMPARE(direct.mediaSourceId, QStringLiteral("ms301001"));
    QCOMPARE(direct.url.path(), QStringLiteral("/Videos/301001/stream.mkv"));
    const QUrlQuery directQuery(direct.url.query());
    QCOMPARE(directQuery.queryItemValue(QStringLiteral("static")), QStringLiteral("true"));
    QCOMPARE(directQuery.queryItemValue(QStringLiteral("api_key")), kToken);
    QCOMPARE(directQuery.queryItemValue(QStringLiteral("PlaySessionId")), QStringLiteral("ps0011"));

    const StreamCandidate &remux = entry->candidates[1];
    QCOMPARE(remux.method, PlayMethod::DirectStream);
    QCOMPARE(remux.url.host(), QStringLiteral("server.example"));
    // Fixture's DirectStreamUrl has no api_key → mapper must append it.
    QCOMPARE(QUrlQuery(remux.url.query()).queryItemValue(QStringLiteral("api_key")), kToken);

    const StreamCandidate &hls = entry->candidates[2];
    QCOMPARE(hls.method, PlayMethod::Transcode);
    QCOMPARE(hls.container, QStringLiteral("ts"));
    QVERIFY(hls.url.path().endsWith(QStringLiteral("master.m3u8")));
    // Fixture's TranscodingUrl already has api_key → must not be duplicated.
    QCOMPARE(QUrlQuery(hls.url.query()).allQueryItemValues(QStringLiteral("api_key")).size(), 1);

    QCOMPARE(playMethodName(direct.method), QStringLiteral("DirectPlay"));
    QCOMPARE(playMethodName(hls.method), QStringLiteral("Transcode"));

    // Every rung belongs to the one source; walking past the end returns nullptr.
    for (qsizetype rung = 0; rung < ticket.rungCount(0); ++rung)
        QCOMPARE(ticket.candidate(0, rung)->mediaSourceId, QStringLiteral("ms301001"));
    QVERIFY(!ticket.candidate(0, 3));
    QVERIFY(!ticket.candidate(1, 0));
    QVERIFY(!ticket.source(1));
}

void PlaybackTicketTest::transcodeOnly()
{
    const PlaybackTicket ticket =
        emby::parsePlaybackTicket(loadFixture(QStringLiteral("playback_info_transcode_only.json")),
                                  kBase, QStringLiteral("999"), kToken, kDeviceId);

    QVERIFY(ticket.isValid());
    QCOMPARE(ticket.sourceCount(), 1);
    QCOMPARE(ticket.rungCount(0), 1);
    QCOMPARE(ticket.candidate(0, 0)->method, PlayMethod::Transcode);
    // Neither direct method is offered, so there is nothing to demote from.
    QVERIFY(!ticket.source(0)->source.supportsDirectPlay);
    QVERIFY(!ticket.source(0)->source.supportsDirectStream);
    QVERIFY(!ticket.candidate(0, 1));
}

void PlaybackTicketTest::emptyResponseIsInvalid()
{
    const PlaybackTicket ticket =
        emby::parsePlaybackTicket(QJsonObject(), kBase, QStringLiteral("1"), kToken, kDeviceId);
    QVERIFY(!ticket.isValid());
    QCOMPARE(ticket.sourceCount(), 0);
    QCOMPARE(ticket.defaultSourceIndex(), -1);
    QCOMPARE(ticket.rungCount(0), 0);
    QVERIFY(!ticket.candidate(0, 0));
    QCOMPARE(ticket.runtimeTicks(0), Q_INT64_C(0));
    QCOMPARE(ticket.indexOfSourceId(QStringLiteral("nope")), -1);
    QCOMPARE(ticket.indexOfSourceId(QString()), -1);
}

void PlaybackTicketTest::reverseProxyBasePathPreserved()
{
    const QUrl proxied(QStringLiteral("https://server.example/emby"));
    const PlaybackTicket ticket =
        emby::parsePlaybackTicket(loadFixture(QStringLiteral("playback_info.json")), proxied,
                                  QStringLiteral("301001"), kToken, kDeviceId);
    QCOMPARE(ticket.candidate(0, 0)->url.path(), QStringLiteral("/emby/Videos/301001/stream.mkv"));
    QCOMPARE(ticket.candidate(0, 2)->url.path(), QStringLiteral("/emby/videos/301001/master.m3u8"));
}

void PlaybackTicketTest::multiVersionGetsOneLadderEach()
{
    const PlaybackTicket ticket =
        emby::parsePlaybackTicket(loadFixture(QStringLiteral("playback_info_multi_version.json")),
                                  kBase, QStringLiteral("4242"), kToken, kDeviceId);

    QVERIFY(ticket.isValid());
    QCOMPARE(ticket.sourceCount(), 2);
    QCOMPARE(ticket.defaultSourceIndex(), 0);
    QCOMPARE(ticket.playSessionId, QStringLiteral("ps4242"));

    // Source 0: 4K remux, all three delivery methods.
    const MediaSourceCandidates *uhd = ticket.source(0);
    QVERIFY(uhd);
    QCOMPARE(uhd->source.id, QStringLiteral("ms4242uhd"));
    QCOMPARE(uhd->source.displayName(), QStringLiteral("4K Remux"));
    QCOMPARE(uhd->source.resolutionLabel(), QStringLiteral("4K"));
    QVERIFY(uhd->source.isHdr());
    QCOMPARE(ticket.rungCount(0), 3);
    QCOMPARE(ticket.candidate(0, 0)->method, PlayMethod::DirectPlay);
    QCOMPARE(ticket.candidate(0, 1)->method, PlayMethod::DirectStream);
    QCOMPARE(ticket.candidate(0, 2)->method, PlayMethod::Transcode);

    // Source 1: 1080p, no direct stream → its ladder is two rungs, not three.
    const MediaSourceCandidates *hd = ticket.source(1);
    QVERIFY(hd);
    QCOMPARE(hd->source.id, QStringLiteral("ms4242hd"));
    QCOMPARE(hd->source.resolutionLabel(), QStringLiteral("1080p"));
    QVERIFY(!hd->source.isHdr());
    QCOMPARE(ticket.rungCount(1), 2);
    QCOMPARE(ticket.candidate(1, 0)->method, PlayMethod::DirectPlay);
    QCOMPARE(ticket.candidate(1, 1)->method, PlayMethod::Transcode);

    QCOMPARE(ticket.indexOfSourceId(QStringLiteral("ms4242hd")), 1);
    QCOMPARE(ticket.indexOfSourceId(QStringLiteral("ms-nope")), -1);

    // TranscodeReasons: array form and comma-string form both parse.
    QCOMPARE(uhd->source.transcodeReasons,
             QStringList({QStringLiteral("VideoBitrateNotSupported"),
                          QStringLiteral("AudioCodecNotSupported")}));
    QCOMPARE(hd->source.transcodeReasons, QStringList({QStringLiteral("ContainerNotSupported")}));
}

void PlaybackTicketTest::demotionNeverCrossesSources()
{
    const PlaybackTicket ticket =
        emby::parsePlaybackTicket(loadFixture(QStringLiteral("playback_info_multi_version.json")),
                                  kBase, QStringLiteral("4242"), kToken, kDeviceId);

    // The regression this restructure exists to prevent: walking every rung of
    // source 0 must never surface a candidate belonging to source 1.
    for (qsizetype rung = 0; rung < ticket.rungCount(0); ++rung) {
        const StreamCandidate *candidate = ticket.candidate(0, rung);
        QVERIFY(candidate);
        QCOMPARE(candidate->mediaSourceId, QStringLiteral("ms4242uhd"));
        QVERIFY(candidate->url.query().contains(QStringLiteral("ms4242uhd")));
    }
    // And the ladder stops at source 0's end rather than rolling into source 1.
    QVERIFY(!ticket.candidate(0, ticket.rungCount(0)));

    for (qsizetype rung = 0; rung < ticket.rungCount(1); ++rung)
        QCOMPARE(ticket.candidate(1, rung)->mediaSourceId, QStringLiteral("ms4242hd"));
}

void PlaybackTicketTest::defaultSourceSkipsUnplayable()
{
    // First source offers no delivery method at all; the default must be the
    // first source that actually has a ladder.
    QJsonObject json = loadFixture(QStringLiteral("playback_info_multi_version.json"));
    QJsonArray sources = json.value(QLatin1String("MediaSources")).toArray();
    QJsonObject dead = sources[0].toObject();
    dead.insert(QLatin1String("SupportsDirectPlay"), false);
    dead.insert(QLatin1String("SupportsDirectStream"), false);
    dead.remove(QLatin1String("TranscodingUrl"));
    sources.replace(0, dead);
    json.insert(QLatin1String("MediaSources"), sources);

    const PlaybackTicket ticket =
        emby::parsePlaybackTicket(json, kBase, QStringLiteral("4242"), kToken, kDeviceId);
    QCOMPARE(ticket.sourceCount(), 2); // still listed — the picker shows it
    QVERIFY(!ticket.source(0)->isValid());
    QCOMPARE(ticket.rungCount(0), 0);
    QCOMPARE(ticket.defaultSourceIndex(), 1);
    QVERIFY(ticket.isValid());
}

void PlaybackTicketTest::malformedPayloadDegradesToDefaults()
{
    const PlaybackTicket ticket =
        emby::parsePlaybackTicket(loadFixture(QStringLiteral("playback_info_malformed.json")),
                                  kBase, QStringLiteral("1"), kToken, kDeviceId);

    // Non-object entries in MediaSources are dropped, not turned into ghosts.
    QCOMPARE(ticket.sourceCount(), 2);
    QCOMPARE(ticket.playSessionId, QString()); // number on the wire → empty

    const MediaSource &broken = ticket.source(0)->source;
    QCOMPARE(broken.id, QString());
    QCOMPARE(broken.container, QString());
    QCOMPARE(broken.size, Q_INT64_C(0));
    QCOMPARE(broken.bitrate, Q_INT64_C(0));
    QCOMPARE(broken.runtimeTicks, Q_INT64_C(0));
    QCOMPARE(broken.protocol, QString());
    QVERIFY(!broken.supportsDirectPlay);  // "true" as a string is not a bool
    QVERIFY(!broken.supportsDirectStream); // null
    QVERIFY(!broken.supportsTranscoding);  // 1 is not a bool
    QVERIFY(broken.transcodeReasons.isEmpty()); // object form is ignored
    QVERIFY(broken.streams.isEmpty());          // "MediaStreams": "nope"
    QVERIFY(!broken.videoStream());
    QCOMPARE(broken.resolutionLabel(), QString());
    QVERIFY(!broken.isHdr());
    QCOMPARE(broken.displayName(), QStringLiteral("Unknown"));
    QVERIFY(!ticket.source(0)->isValid());

    // Second source is playable but its stream array is full of junk.
    const MediaSource &ok = ticket.source(1)->source;
    QCOMPARE(ok.id, QStringLiteral("msok"));
    QCOMPARE(ticket.rungCount(1), 1);
    QCOMPARE(ticket.defaultSourceIndex(), 1);
    QCOMPARE(ok.streams.size(), 1); // null / 5 / "text" skipped
    const MediaStream &junk = ok.streams.first();
    QCOMPARE(junk.index, -1);             // "x" is not an int
    QCOMPARE(junk.type, QString());       // 9 is not a string
    QCOMPARE(junk.kind(), MediaStream::Kind::Other);
    QCOMPARE(junk.width, 0);              // "1920" as a string is not an int
    QCOMPARE(junk.channels, 0);
    QCOMPARE(junk.bitRate, Q_INT64_C(0));
    QVERIFY(!junk.isDefault);             // "yes" is not a bool
    QVERIFY(!junk.isHdr());
    QVERIFY(ok.audioStreams().isEmpty());
    QVERIFY(ok.subtitleStreams().isEmpty());
}

void PlaybackTicketTest::missingMediaSourcesArray()
{
    // MediaSources absent, null, or the wrong type — never a crash.
    for (const QJsonValue &value :
         {QJsonValue(QJsonValue::Null), QJsonValue(QStringLiteral("oops")), QJsonValue(7),
          QJsonValue(QJsonObject())}) {
        QJsonObject json;
        json.insert(QLatin1String("MediaSources"), value);
        const PlaybackTicket ticket =
            emby::parsePlaybackTicket(json, kBase, QStringLiteral("1"), kToken, kDeviceId);
        QVERIFY(!ticket.isValid());
        QCOMPARE(ticket.sourceCount(), 0);
    }
}

QTEST_GUILESS_MAIN(PlaybackTicketTest)
#include "tst_playback_ticket.moc"
