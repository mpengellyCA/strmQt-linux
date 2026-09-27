#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>

#include "FakePlayerBackend.h"
#include "MockEmbyServer.h"
#include "app/ItemActions.h"
#include "app/PlayQueue.h"
#include "app/controllers/PlayerController.h"
#include "app/controllers/music/NowPlayingMusicController.h"
#include "app/models/MediaItemModel.h"
#include "app/music/MusicPlayback.h"
#include "app/music/MusicRepository.h"
#include "app/music/models/TrackListModel.h"
#include "core/Settings.h"
#include "server/emby/EmbyClient.h"

using namespace strmqt;
using namespace strmqt::music;

namespace {

const auto kUserId = QStringLiteral("a1b2c3d4e5f60718293a4b5c6d7e8f90");
const auto kToken = QStringLiteral("not-a-real-token-fixture-only");

QString fixturePath(const QString &name)
{
    return QStringLiteral(STRMQT_FIXTURES_DIR "/") + name;
}

QString itemsPath() { return QStringLiteral("/Users/%1/Items").arg(kUserId); }

QJsonObject trackJson(const QString &id, const QString &albumId, int number)
{
    return {{"Id", id}, {"Name", "Track " + id}, {"Type", "Audio"}, {"AlbumId", albumId},
            {"Album", "Album " + albumId}, {"AlbumPrimaryImageTag", "atag"},
            {"AlbumArtist", "Hollow Coves"},
            {"AlbumArtists", QJsonArray{QJsonObject{{"Id", "ar1"}, {"Name", "Hollow Coves"}}}},
            {"ArtistItems", QJsonArray{QJsonObject{{"Id", "ar1"}, {"Name", "Hollow Coves"}}}},
            {"Artists", QJsonArray{"Hollow Coves"}},
            {"ParentIndexNumber", 1}, {"IndexNumber", number},
            {"RunTimeTicks", 4 * 60 * 10'000'000LL}};
}

QByteArray page(const QJsonArray &items)
{
    return QJsonDocument(QJsonObject{{"Items", items}, {"TotalRecordCount", items.size()}})
        .toJson(QJsonDocument::Compact);
}

Track track(const QString &id, const QString &albumId, int number)
{
    Track t;
    t.id = id;
    t.title = QStringLiteral("Track ") + id;
    t.displayTitle = t.title;
    t.albumId = albumId;
    t.albumTitle = QStringLiteral("Album ") + albumId;
    t.discNumber = 1;
    t.trackNumber = number;
    t.runtimeMs = 4 * 60 * 1000;
    t.artists = {{QStringLiteral("ar1"), QStringLiteral("Hollow Coves")}};
    t.albumArtists = t.artists;
    t.coverRef = {albumId, QStringLiteral("Primary"), QStringLiteral("atag")};
    return t;
}

// The audio ticket, optionally with an .lrc sidecar as a second, external
// subtitle stream (Crate spec §9 V4).
QByteArray playbackInfo(bool withLyrics)
{
    QFile file(fixturePath(QStringLiteral("playback_info_audio.json")));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (withLyrics) {
        QJsonArray sources = root.value("MediaSources").toArray();
        QJsonObject source = sources.at(0).toObject();
        QJsonArray streams = source.value("MediaStreams").toArray();
        streams.append(QJsonObject{{"Codec", "lrc"}, {"Type", "Subtitle"}, {"Index", 1},
                                   {"IsExternal", true}, {"Language", "eng"}});
        source.insert("MediaStreams", streams);
        sources.replace(0, source);
        root.insert("MediaSources", sources);
    }
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

} // namespace

class NowPlayingMusicTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void followsTheCurrentTrack();
    void sourceLabelFallsBackToContext();
    void recordStateFollowsThePlayer();
    void albumChangingOnlyWhenTheAlbumChanges();
    void albumTracksComeFromTheRepositoryCache();
    void readoutFromTicketAndMethod();
    void timeTextFormatsBothClocks();
    void favouriteToggles();
    void lyricsBehindCaps();
    void lyricsTimedFollowsTheParser();

private:
    PlayQueue *queue() const { return m_player->queue(); }
    int requestsTo(const QString &method, const QString &path, const QString &queryPart = QString()) const;

    MockEmbyServer *m_mock = nullptr;
    emby::EmbyClient *m_client = nullptr;
    FakePlayerBackend *m_backend = nullptr;
    QTemporaryDir *m_dir = nullptr;
    Settings *m_settings = nullptr;
    PlayerController *m_player = nullptr;
    ItemActions *m_actions = nullptr;
    MusicRepository *m_repo = nullptr;
    MusicPlayback *m_playback = nullptr;
    NowPlayingMusicController *m_controller = nullptr;
};

using Q = QList<QPair<QString, QString>>;

void NowPlayingMusicTest::initTestCase()
{
    // coverUrl() goes through the emby image provider, which yields nothing
    // until a session namespace is set (as Application does).
    setEmbyImageSourceNamespace(QStringLiteral("t"));
}

void NowPlayingMusicTest::init()
{
    m_mock = new MockEmbyServer(this);
    QVERIFY(m_mock->start());
    const QStringList ids{"a1", "a2", "a3", "b1", "b2"};
    for (const QString &id : ids)
        m_mock->addRoute("POST", QStringLiteral("/Items/%1/PlaybackInfo").arg(id), 200, playbackInfo(false));
    m_mock->addRoute("POST", "/Items/l1/PlaybackInfo", 200, playbackInfo(true));
    m_mock->addRoute("GET", "/Videos/l1/ms301004/Subtitles/1/Stream.js", 200,
                     R"({"TrackEvents":[{"StartPositionTicks":10000000,"Text":"Salt on the window"},
                                        {"StartPositionTicks":50000000,"Text":"Tide in the hall"}]})");
    // Out of order, and the first line (a title) carries no time at all.
    m_mock->addRoute("POST", "/Items/l2/PlaybackInfo", 200, playbackInfo(true));
    m_mock->addRoute("GET", "/Videos/l2/ms301004/Subtitles/1/Stream.js", 200,
                     R"({"TrackEvents":[{"Text":"Harbour Song"},
                                        {"StartPositionTicks":80000000,"Text":"Bells at the quay"},
                                        {"StartPositionTicks":30000000,"Text":"Ropes on the deck"}]})");
    m_mock->addRoute("POST", "/Sessions/Playing", 204, {});
    m_mock->addRoute("POST", "/Sessions/Playing/Progress", 204, {});
    m_mock->addRoute("POST", "/Sessions/Playing/Stopped", 204, {});
    m_mock->addRoute("POST", QStringLiteral("/Users/%1/FavoriteItems/a1").arg(kUserId), 200,
                     R"({"IsFavorite":true})");
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"ParentId", "alA"}}, 200,
                          page({trackJson("a1", "alA", 1), trackJson("a2", "alA", 2), trackJson("a3", "alA", 3)}));
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"ParentId", "alB"}}, 200,
                          page({trackJson("b1", "alB", 1), trackJson("b2", "alB", 2)}));
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"ParentId", "alL"}}, 200, page({trackJson("l1", "alL", 1)}));

    m_client = new emby::EmbyClient(this);
    m_client->setBaseUrl(m_mock->baseUrl());
    m_client->setDeviceId(QStringLiteral("test-device"));
    m_client->setSession(kToken, kUserId);

    m_backend = new FakePlayerBackend(this);
    m_dir = new QTemporaryDir;
    QVERIFY(m_dir->isValid());
    m_settings = new Settings(m_dir->filePath(QStringLiteral("settings.ini")), this);
    m_player = new PlayerController(m_client, m_backend, m_settings, this);
    // This suite is not exercising the stall watchdog; a slow/contended host
    // can otherwise cross the default 3-tick*2s stall window before a QTRY
    // assertion below observes an unmoved FakePlayerBackend position, which
    // nudges the playhead by a real second and makes timeTextFormatsBothClocks
    // flaky. A tick far longer than the suite's own runtime keeps it inert.
    m_player->setTimingForTests(3'600'000, 1000, 1000);
    m_actions = new ItemActions(m_client, m_player, this);
    m_repo = new MusicRepository(m_client, this);
    m_playback = new MusicPlayback(m_repo, m_actions, this);
    m_controller = new NowPlayingMusicController(m_repo, m_playback, this);
    m_controller->bind(m_player, m_actions);
    m_controller->setLyricsEnabledForTests(false);
}

void NowPlayingMusicTest::cleanup()
{
    delete m_controller;
    delete m_playback;
    delete m_repo;
    delete m_actions;
    delete m_player;
    delete m_settings;
    delete m_dir;
    delete m_backend;
    delete m_client;
    delete m_mock;
    m_controller = nullptr;
    m_playback = nullptr;
    m_repo = nullptr;
    m_actions = nullptr;
    m_player = nullptr;
    m_settings = nullptr;
    m_dir = nullptr;
    m_backend = nullptr;
    m_client = nullptr;
    m_mock = nullptr;
}

int NowPlayingMusicTest::requestsTo(const QString &method, const QString &path, const QString &queryPart) const
{
    int count = 0;
    for (const auto &request : m_mock->requests()) {
        if (request.method == method && request.path == path
            && (queryPart.isEmpty() || request.query.contains(queryPart)))
            ++count;
    }
    return count;
}

void NowPlayingMusicTest::followsTheCurrentTrack()
{
    QVERIFY(!m_controller->active());
    QCOMPARE(m_controller->recordState(), QStringLiteral("stopped"));

    m_playback->playAlbum(QStringLiteral("alA"), QStringLiteral("Sunburned Almanac"), 1);
    QTRY_COMPARE(m_controller->trackId(), QStringLiteral("a2"));
    QVERIFY(m_controller->active());
    QCOMPARE(m_controller->title(), QStringLiteral("Track a2"));
    QCOMPARE(m_controller->album(), QStringLiteral("Album alA"));
    QCOMPARE(m_controller->albumId(), QStringLiteral("alA"));
    QCOMPARE(m_controller->artist(), QStringLiteral("Hollow Coves"));
    QCOMPARE(m_controller->artistId(), QStringLiteral("ar1"));
    QVERIFY2(m_controller->coverUrl().contains(QStringLiteral("alA")), qPrintable(m_controller->coverUrl()));
    QCOMPARE(m_controller->trackItem().value(QStringLiteral("itemId")).toString(), QStringLiteral("a2"));
    QCOMPARE(m_controller->sourceLabel(), QStringLiteral("Sunburned Almanac"));
    QCOMPARE(m_controller->sourceKicker(), QStringLiteral("Playing from · Sunburned Almanac"));

    QSignalSpy changed(m_controller, &NowPlayingMusicController::trackChanged);
    m_player->playNext();
    QTRY_COMPARE(m_controller->trackId(), QStringLiteral("a3"));
    QVERIFY(changed.count() >= 1);
    QCOMPARE(m_controller->title(), QStringLiteral("Track a3"));
}

void NowPlayingMusicTest::sourceLabelFallsBackToContext()
{
    m_player->playQueue(MusicPlayback::toMaps({track("b1", "alB", 1), track("b2", "alB", 2)}), 0);
    QTRY_COMPARE(m_controller->trackId(), QStringLiteral("b1"));
    QVERIFY(queue()->sourceLabel().isEmpty());
    QCOMPARE(m_controller->sourceLabel(), queue()->contextLabel());
    QCOMPARE(m_controller->sourceLabel(), QStringLiteral("from Album alB"));
    QCOMPARE(m_controller->sourceKicker(), QStringLiteral("Playing from Album alB"));
}

void NowPlayingMusicTest::recordStateFollowsThePlayer()
{
    QSignalSpy states(m_controller, &NowPlayingMusicController::recordStateChanged);
    m_player->playQueue(MusicPlayback::toMaps({track("a1", "alA", 1)}), 0);
    QTRY_COMPARE(m_controller->recordState(), QStringLiteral("buffering"));
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);

    m_backend->simulateState(PlayerBackend::State::Playing);
    QTRY_COMPARE(m_controller->recordState(), QStringLiteral("playing"));

    m_player->setPaused(true);
    QTRY_COMPARE(m_controller->recordState(), QStringLiteral("paused"));

    m_backend->simulateBuffering(true);
    QTRY_COMPARE(m_controller->recordState(), QStringLiteral("buffering"));
    m_backend->simulateBuffering(false);
    QTRY_COMPARE(m_controller->recordState(), QStringLiteral("paused"));

    m_player->stop();
    QTRY_COMPARE(m_controller->recordState(), QStringLiteral("stopped"));
    QVERIFY(states.count() >= 5);
}

void NowPlayingMusicTest::albumChangingOnlyWhenTheAlbumChanges()
{
    QSignalSpy changing(m_controller, &NowPlayingMusicController::albumChanging);
    m_player->playQueue(MusicPlayback::toMaps({track("a1", "alA", 1), track("a2", "alA", 2),
                                               track("b1", "alB", 1)}), 0);
    QTRY_COMPARE(m_controller->trackId(), QStringLiteral("a1"));
    QCOMPARE(changing.count(), 0); // nothing was playing: no album to leave

    m_player->playNext();
    QTRY_COMPARE(m_controller->trackId(), QStringLiteral("a2"));
    QCOMPARE(changing.count(), 0); // same album keeps spinning

    // The cover is already the new album's when the signal fires.
    QString coverAtSignal;
    connect(m_controller, &NowPlayingMusicController::albumChanging, this,
            [&] { coverAtSignal = m_controller->coverUrl(); });
    m_player->playNext();
    QTRY_COMPARE(m_controller->trackId(), QStringLiteral("b1"));
    QCOMPARE(changing.count(), 1);
    QVERIFY2(coverAtSignal.contains(QStringLiteral("alB")), qPrintable(coverAtSignal));

    m_player->playPrevious();
    QTRY_COMPARE(m_controller->trackId(), QStringLiteral("a2"));
    QCOMPARE(changing.count(), 2);
}

void NowPlayingMusicTest::albumTracksComeFromTheRepositoryCache()
{
    m_playback->playAlbum(QStringLiteral("alA"), QStringLiteral("Sunburned Almanac"), 2);
    QTRY_COMPARE(m_controller->trackId(), QStringLiteral("a3"));
    QTRY_COMPARE(m_controller->trackModel()->rowCount(), 3);
    QTRY_COMPARE(m_controller->currentAlbumRow(), 2);
    QCOMPARE(m_controller->albumSummary(), QStringLiteral("3 tracks · 12 min"));
    QCOMPARE(m_controller->albumTracks(), static_cast<QObject *>(m_controller->trackModel()));
    // playAlbum filled the cache; the controller did not ask again.
    QCOMPARE(requestsTo(QStringLiteral("GET"), itemsPath(), QStringLiteral("ParentId=alA")), 1);

    m_controller->playAlbumFrom(0);
    QTRY_COMPARE(m_controller->trackId(), QStringLiteral("a1"));
    QTRY_COMPARE(m_controller->currentAlbumRow(), 0);
    QCOMPARE(queue()->sourceLabel(), QStringLiteral("Album alA"));
    QCOMPARE(queue()->rowCount(), 3);

    m_controller->playAlbumFrom(7); // out of range: ignored
    QCOMPARE(m_controller->trackId(), QStringLiteral("a1"));
}

void NowPlayingMusicTest::readoutFromTicketAndMethod()
{
    QCOMPARE(m_controller->readout(), QString());
    m_player->playQueue(MusicPlayback::toMaps({track("a1", "alA", 1)}), 0);
    QTRY_COMPARE(m_controller->readout(), QStringLiteral("FLAC 24/96 · DIRECT PLAY"));
}

void NowPlayingMusicTest::timeTextFormatsBothClocks()
{
    m_player->playQueue(MusicPlayback::toMaps({track("a1", "alA", 1)}), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateState(PlayerBackend::State::Playing);
    // The audio fixture's RunTimeTicks (playback_info_audio.json) is the
    // ticket-derived duration fallback until the backend reports its own.
    QTRY_COMPARE(m_controller->timeText(), QStringLiteral("0:00 / 3:42:00"));
    m_backend->simulateDuration(297'000);
    m_backend->simulatePosition(112'000);
    QTRY_COMPARE(m_controller->timeText(), QStringLiteral("1:52 / 4:57"));
}

void NowPlayingMusicTest::favouriteToggles()
{
    m_player->playQueue(MusicPlayback::toMaps({track("a1", "alA", 1)}), 0);
    QTRY_COMPARE(m_controller->trackId(), QStringLiteral("a1"));
    QVERIFY(!m_controller->favourite());

    QSignalSpy spy(m_controller, &NowPlayingMusicController::favouriteChanged);
    m_controller->toggleFavourite();
    QVERIFY(m_controller->favourite()); // optimistic
    QVERIFY(spy.count() >= 1);
    QTRY_COMPARE(requestsTo(QStringLiteral("POST"), QStringLiteral("/Users/%1/FavoriteItems/a1").arg(kUserId)), 1);
}

void NowPlayingMusicTest::lyricsBehindCaps()
{
    const QString lyricsPath = QStringLiteral("/Videos/l1/ms301004/Subtitles/1/Stream.js");

    // Caps say no: the ticket has a sidecar, but nothing is asked for.
    m_player->playQueue(MusicPlayback::toMaps({track("l1", "alL", 1)}), 0);
    QTRY_COMPARE(m_controller->readout(), QStringLiteral("FLAC 24/96 · DIRECT PLAY"));
    QTest::qWait(50);
    QCOMPARE(requestsTo(QStringLiteral("GET"), lyricsPath), 0);
    QVERIFY(!m_controller->lyricsAvailable());
    QVERIFY(m_controller->lyrics().isEmpty());

    // Caps say yes: the current ticket's sidecar is read.
    QSignalSpy lyricsSpy(m_controller, &NowPlayingMusicController::lyricsChanged);
    m_controller->setLyricsEnabledForTests(true);
    QTRY_VERIFY(m_controller->lyricsAvailable());
    QCOMPARE(requestsTo(QStringLiteral("GET"), lyricsPath), 1);
    QVERIFY(m_controller->lyricsTimed());
    const QVariantList lines = m_controller->lyrics();
    QCOMPARE(lines.size(), 2);
    QCOMPARE(lines.at(1).toMap().value(QStringLiteral("timeMs")).toLongLong(), 5000);
    QCOMPARE(lines.at(1).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("Tide in the hall"));
    QCOMPARE(m_controller->currentLyricRow(), -1);

    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulatePosition(6000);
    QTRY_COMPARE(m_controller->currentLyricRow(), 1);
    m_backend->simulatePosition(1200);
    QTRY_COMPARE(m_controller->currentLyricRow(), 0);

    // A track without a sidecar clears them.
    m_player->playQueue(MusicPlayback::toMaps({track("a1", "alA", 1)}), 0);
    QTRY_COMPARE(m_controller->trackId(), QStringLiteral("a1"));
    QVERIFY(!m_controller->lyricsAvailable());
    QCOMPARE(m_controller->currentLyricRow(), -1);

    // A skip to a new track must never request lyrics against the OUTGOING
    // track's ticket: PlayQueue's currentChanged fires before PlayerController
    // resets the ticket, so a naive "load on track move" reads l1's still-live
    // source/stream index for a1's id. a1 has no sidecar at all, so nothing
    // should ever be requested at l1's lyrics path with a1's id substituted in.
    const QString a1LyricsPath = QStringLiteral("/Videos/a1/ms301004/Subtitles/1/Stream.js");
    QTest::qWait(50);
    QCOMPARE(requestsTo(QStringLiteral("GET"), a1LyricsPath), 0);
    QVERIFY(m_controller->lyrics().isEmpty());
}

void NowPlayingMusicTest::lyricsTimedFollowsTheParser()
{
    m_controller->setLyricsEnabledForTests(true);
    m_player->playQueue(MusicPlayback::toMaps({track("l2", "alL", 1)}), 0);
    QTRY_VERIFY(m_controller->lyricsAvailable());
    // An untimed first line does not make the whole sidecar untimed.
    QVERIFY(m_controller->lyricsTimed());
    const QVariantList lines = m_controller->lyrics();
    QCOMPARE(lines.size(), 3);
    QCOMPARE(lines.at(0).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("Harbour Song"));
    QCOMPARE(lines.at(1).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("Ropes on the deck"));
    QCOMPARE(lines.at(2).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("Bells at the quay"));

    // The playhead finds the right line in the sorted order.
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulatePosition(1000);
    QTRY_COMPARE(m_controller->currentLyricRow(), 0);
    m_backend->simulatePosition(4000);
    QTRY_COMPARE(m_controller->currentLyricRow(), 1);
    m_backend->simulatePosition(9000);
    QTRY_COMPARE(m_controller->currentLyricRow(), 2);
}

QTEST_GUILESS_MAIN(NowPlayingMusicTest)
#include "tst_now_playing_music.moc"
