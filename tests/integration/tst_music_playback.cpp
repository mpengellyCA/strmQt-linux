#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QUrlQuery>
#include <QtTest>

#include "FakePlayerBackend.h"
#include "MockEmbyServer.h"
#include "app/ItemActions.h"
#include "app/PlayQueue.h"
#include "app/controllers/PlayerController.h"
#include "app/music/MusicPlayback.h"
#include "app/music/MusicRepository.h"
#include "core/Settings.h"
#include "server/emby/EmbyClient.h"
#include "server/dto/music/MusicQuery.h"

using namespace strmqt;
using namespace strmqt::music;

namespace {

const auto kUserId = QStringLiteral("a1b2c3d4e5f60718293a4b5c6d7e8f90");
const auto kToken = QStringLiteral("not-a-real-token-fixture-only");
const auto kLibrary = QStringLiteral("1868998");

QString fixturePath(const QString &name)
{
    return QStringLiteral(STRMQT_FIXTURES_DIR "/") + name;
}

QString itemsPath() { return QStringLiteral("/Users/%1/Items").arg(kUserId); }

QJsonObject trackJson(const QString &id, const QString &albumId, int number)
{
    return {{"Id", id}, {"Name", "Track " + id}, {"Type", "Audio"}, {"AlbumId", albumId},
            {"Album", "Album " + albumId}, {"ParentIndexNumber", 1}, {"IndexNumber", number},
            {"RunTimeTicks", 4 * 60 * 10'000'000LL}};
}

QByteArray page(const QJsonArray &items)
{
    return QJsonDocument(QJsonObject{{"Items", items}, {"TotalRecordCount", items.size()}})
        .toJson(QJsonDocument::Compact);
}

QStringList queueIds(PlayQueue *queue)
{
    QStringList ids;
    for (int row = 0; row < queue->rowCount(); ++row)
        ids.append(queue->itemAt(row).value(QStringLiteral("itemId")).toString());
    return ids;
}

using Q = QList<QPair<QString, QString>>;

} // namespace

class MusicPlaybackTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void playAlbumQueuesInDiscOrderFromTheStart();
    void shuffleAlbumKeepsEveryTrack();
    void radioDeduplicatesAndLabels();
    void stationTileResolvesByKey();
    void failureLeavesTheQueueAndReports();
    void lastVerbWins();
    void playQueryQueuesTheFilteredScopeInOrder();

private:
    PlayQueue *queue() const { return m_player->queue(); }

    MockEmbyServer *m_mock = nullptr;
    emby::EmbyClient *m_client = nullptr;
    FakePlayerBackend *m_backend = nullptr;
    QTemporaryDir *m_dir = nullptr;
    Settings *m_settings = nullptr;
    PlayerController *m_player = nullptr;
    ItemActions *m_actions = nullptr;
    MusicRepository *m_repo = nullptr;
    MusicPlayback *m_playback = nullptr;
};

void MusicPlaybackTest::init()
{
    m_mock = new MockEmbyServer(this);
    QVERIFY(m_mock->start());
    const QStringList ids{"a1", "a2", "a3", "b1", "b2", "m1", "m2"};
    for (const QString &id : ids) {
        QVERIFY(m_mock->addRouteFromFile("POST", QStringLiteral("/Items/%1/PlaybackInfo").arg(id),
                                         fixturePath(QStringLiteral("playback_info.json"))));
    }
    m_mock->addRoute("POST", "/Sessions/Playing", 204, {});
    m_mock->addRoute("POST", "/Sessions/Playing/Progress", 204, {});
    m_mock->addRoute("POST", "/Sessions/Playing/Stopped", 204, {});
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"ParentId", "alA"}}, 200,
                          page({trackJson("a1", "alA", 1), trackJson("a2", "alA", 2), trackJson("a3", "alA", 3)}));
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"ParentId", "alB"}}, 200,
                          page({trackJson("b1", "alB", 1), trackJson("b2", "alB", 2)}));
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"ParentId", "gone"}}, 500, "{}");
    m_mock->addRoute("GET", "/Items/ar1/InstantMix", 200,
                     page({trackJson("m1", "x", 1), trackJson("m2", "x", 2), trackJson("m1", "x", 1)}));

    m_client = new emby::EmbyClient(this);
    m_client->setBaseUrl(m_mock->baseUrl());
    m_client->setDeviceId(QStringLiteral("test-device"));
    m_client->setSession(kToken, kUserId);

    m_backend = new FakePlayerBackend(this);
    m_dir = new QTemporaryDir;
    QVERIFY(m_dir->isValid());
    m_settings = new Settings(m_dir->filePath(QStringLiteral("settings.ini")), this);
    m_player = new PlayerController(m_client, m_backend, m_settings, this);
    m_actions = new ItemActions(m_client, m_player, this);
    m_repo = new MusicRepository(m_client, this);
    m_playback = new MusicPlayback(m_repo, m_actions, this);
}

void MusicPlaybackTest::cleanup()
{
    delete m_playback;
    delete m_repo;
    delete m_actions;
    delete m_player;
    delete m_settings;
    delete m_dir;
    delete m_backend;
    delete m_client;
    delete m_mock;
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

void MusicPlaybackTest::playAlbumQueuesInDiscOrderFromTheStart()
{
    m_playback->playAlbum(QStringLiteral("alA"), QStringLiteral("Sunburned Almanac"), 1);
    QTRY_COMPARE(queueIds(queue()), (QStringList{"a1", "a2", "a3"}));
    QCOMPARE(queue()->currentIndex(), 1);
    QCOMPARE(queue()->sourceLabel(), QStringLiteral("Sunburned Almanac"));
    QCOMPARE(queue()->itemAt(0).value("type").toString(), QStringLiteral("Audio"));
}

void MusicPlaybackTest::shuffleAlbumKeepsEveryTrack()
{
    m_playback->shuffleAlbum(QStringLiteral("alA"), QStringLiteral("Sunburned Almanac"));
    QTRY_COMPARE(queue()->rowCount(), 3);
    QStringList ids = queueIds(queue());
    ids.sort();
    QCOMPARE(ids, (QStringList{"a1", "a2", "a3"}));
    QCOMPARE(queue()->currentIndex(), 0);
    QVERIFY(!queue()->shuffled());
    QCOMPARE(queue()->sourceLabel(), QStringLiteral("Shuffle · Sunburned Almanac"));
}

void MusicPlaybackTest::radioDeduplicatesAndLabels()
{
    m_playback->radio(QStringLiteral("ar1"), QStringLiteral("Björk"));
    QTRY_COMPARE(queueIds(queue()), (QStringList{"m1", "m2"}));
    QCOMPARE(queue()->sourceLabel(), QStringLiteral("Radio · Björk"));
}

void MusicPlaybackTest::stationTileResolvesByKey()
{
    m_playback->playStationTile(kLibrary, QVariantMap{{"itemId", "moreLike"}, {"label", "More like Björk"},
                                                      {"seedId", "ar1"}});
    QTRY_COMPARE(queue()->rowCount(), 2);
    QCOMPARE(queue()->sourceLabel(), QStringLiteral("Station · More like Björk"));

    QSignalSpy failed(m_actions, &ItemActions::actionFailed);
    m_playback->playStationTile(kLibrary, QVariantMap{{"itemId", "bogus"}});
    QCOMPARE(failed.count(), 1);
}

void MusicPlaybackTest::failureLeavesTheQueueAndReports()
{
    m_playback->playAlbum(QStringLiteral("alB"), QStringLiteral("B"));
    QTRY_COMPARE(queue()->rowCount(), 2);
    QSignalSpy failed(m_actions, &ItemActions::actionFailed);
    m_playback->playAlbum(QStringLiteral("gone"), QStringLiteral("Gone"));
    QTRY_COMPARE(failed.count(), 1);
    QCOMPARE(queueIds(queue()), (QStringList{"b1", "b2"}));
    QCOMPARE(queue()->sourceLabel(), QStringLiteral("B"));
}

void MusicPlaybackTest::lastVerbWins()
{
    m_playback->playAlbum(QStringLiteral("alA"), QStringLiteral("A"));
    m_playback->playAlbum(QStringLiteral("alB"), QStringLiteral("B")); // before A's reply
    QTRY_COMPARE(queueIds(queue()), (QStringList{"b1", "b2"}));
    QTest::qWait(100); // A's reply, if it were not dropped, would land now
    QCOMPARE(queueIds(queue()), (QStringList{"b1", "b2"}));
    QCOMPARE(queue()->sourceLabel(), QStringLiteral("B"));
}

void MusicPlaybackTest::playQueryQueuesTheFilteredScopeInOrder()
{
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"IncludeItemTypes", "Audio"}, {"Filters", "IsFavorite"}}, 200,
                          page({trackJson("a1", "alA", 1), trackJson("a2", "alA", 2), trackJson("a3", "alA", 3)}));

    MusicQuery query;
    query.libraryId = kLibrary;
    query.section = Section::Albums; // playQuery scopes to Songs itself
    query.sortKey = QStringLiteral("album");
    query.letter = QStringLiteral("Q");
    query.favouritesOnly = true;
    m_playback->playQuery(query, QStringLiteral("Favourites"));

    QTRY_COMPARE(queueIds(queue()), (QStringList{"a1", "a2", "a3"}));
    QCOMPARE(queue()->currentIndex(), 0);
    QCOMPARE(queue()->sourceLabel(), QStringLiteral("Favourites"));

    const QUrlQuery sent(m_mock->lastRequestFor("GET", itemsPath()).query);
    QCOMPARE(sent.queryItemValue("Limit"), QStringLiteral("500"));
    QCOMPARE(sent.queryItemValue("SortBy"), QStringLiteral("Album,ParentIndexNumber,IndexNumber,SortName"));
    QVERIFY(!sent.hasQueryItem("NameStartsWithOrGreater"));
}

QTEST_GUILESS_MAIN(MusicPlaybackTest)
#include "tst_music_playback.moc"
