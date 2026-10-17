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
#include "server/dto/music/MusicTypes.h"

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
    void shuffleAlbumStartsOnARandomTrack();
    void shuffleVerbsLeaveTheQueueShuffled_data();
    void shuffleVerbsLeaveTheQueueShuffled();
    void shuffleAllFromShufflesTheGivenOrderInTheQueue();
    void supersededShuffleIsDropped();
    void radioDeduplicatesAndLabels();
    void stationTileResolvesByKey();
    void failureLeavesTheQueueAndReports();
    void lastVerbWins();
    void playQueryQueuesTheFilteredScopeInOrder();
    void shuffleArtistQueuesTheServersRandomDraw();

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
    // The queue did the shuffling, so it says so (the player's indicator and
    // MPRIS Shuffle read this) and it still knows the record's own order.
    QVERIFY(queue()->shuffled());
    QCOMPARE(queue()->sourceLabel(), QStringLiteral("Shuffle · Sunburned Almanac"));
    QCOMPARE(queue()->contextLabel(), QStringLiteral("from Album alA"));

    const QString playing = queue()->current().id;
    QSignalSpy cursor(queue(), &PlayQueue::currentItemChanged);
    queue()->setShuffled(false);
    QCOMPARE(queueIds(queue()), (QStringList{"a1", "a2", "a3"}));
    QCOMPARE(queue()->current().id, playing); // what is playing keeps playing
    QCOMPARE(cursor.count(), 0);
    // Reordering is not a new queue: the label survives (ARCHITECTURE.md §3).
    QCOMPARE(queue()->sourceLabel(), QStringLiteral("Shuffle · Sunburned Almanac"));
}

// A queue shuffled around a fixed first row would open every shuffle of a
// record on its first track. Twenty shuffles of three tracks all landing on
// the same one has a probability of 3^-19.
void MusicPlaybackTest::shuffleAlbumStartsOnARandomTrack()
{
    QSet<QString> first;
    QSignalSpy replaced(m_actions, &ItemActions::queueChanged);
    for (int round = 0; round < 20; ++round) {
        m_playback->shuffleAlbum(QStringLiteral("alA"), QStringLiteral("Sunburned Almanac"));
        QTRY_COMPARE(replaced.count(), round + 1);
        QVERIFY(queue()->shuffled());
        first.insert(queue()->current().id);
    }
    QVERIFY2(first.size() > 1, qPrintable(first.values().join(QLatin1Char(','))));
}

void MusicPlaybackTest::shuffleVerbsLeaveTheQueueShuffled_data()
{
    QTest::addColumn<QString>("verb");
    QTest::addColumn<QString>("label");
    QTest::newRow("artist") << QStringLiteral("artist") << QStringLiteral("Shuffle · Björk");
    QTest::newRow("station") << QStringLiteral("station") << QStringLiteral("Station · More like Björk");
    QTest::newRow("query") << QStringLiteral("query") << QStringLiteral("Shuffle · Favourites");
}

// Every shuffle verb, not only the album's. Where the server made the draw
// (an artist, a filtered scope) or the list is a station, the order it arrived
// in is the one the queue gives back.
void MusicPlaybackTest::shuffleVerbsLeaveTheQueueShuffled()
{
    QFETCH(QString, verb);
    QFETCH(QString, label);

    QStringList given;
    if (verb == QLatin1String("artist")) {
        m_mock->addQueryRoute("GET", itemsPath(), Q{{"ArtistIds", "ar1"}, {"SortBy", "Random"}}, 200,
                              page({trackJson("a3", "alA", 3), trackJson("a1", "alA", 1),
                                    trackJson("b2", "alB", 2), trackJson("a2", "alA", 2)}));
        given = QStringList{"a3", "a1", "b2", "a2"};
        m_playback->shuffleArtist(QStringLiteral("ar1"), QStringLiteral("Björk"));
    } else if (verb == QLatin1String("station")) {
        Station station;
        station.kind = StationKind::MoreLike;
        station.label = QStringLiteral("More like Björk");
        station.seedId = QStringLiteral("ar1");
        given = QStringList{"m1", "m2"}; // the mix, de-duplicated by the repository
        m_playback->shuffleStation(kLibrary, station);
    } else {
        m_mock->addQueryRoute("GET", itemsPath(), Q{{"IncludeItemTypes", "Audio"}, {"SortBy", "Random"}},
                              200,
                              page({trackJson("b1", "alB", 1), trackJson("a2", "alA", 2),
                                    trackJson("a1", "alA", 1)}));
        given = QStringList{"b1", "a2", "a1"};
        MusicQuery query;
        query.libraryId = kLibrary;
        m_playback->shuffleQuery(query, QStringLiteral("Favourites"));
    }

    QTRY_COMPARE(queue()->rowCount(), given.size());
    QVERIFY(queue()->shuffled());
    QCOMPARE(queue()->sourceLabel(), label);
    QStringList sorted = queueIds(queue());
    sorted.sort();
    QStringList expected = given;
    expected.sort();
    QCOMPARE(sorted, expected);

    queue()->setShuffled(false);
    QCOMPARE(queueIds(queue()), given);
    QCOMPARE(queue()->sourceLabel(), label);
}

// The verb a playlist page shuffles through: items the caller already has, in
// playlist order, the same track twice included. Queue identity is per entry,
// so both occurrences survive the shuffle and come back in their own places.
void MusicPlaybackTest::shuffleAllFromShufflesTheGivenOrderInTheQueue()
{
    QVariantList playlist;
    for (const QString &id : {QStringLiteral("a1"), QStringLiteral("a2"), QStringLiteral("a1"),
                              QStringLiteral("a3")}) {
        playlist.append(QVariantMap{{QStringLiteral("itemId"), id},
                                    {QStringLiteral("name"), id},
                                    {QStringLiteral("type"), QStringLiteral("Audio")}});
    }
    // A folder among them is not playable and is not queued.
    playlist.insert(1, QVariantMap{{QStringLiteral("itemId"), QStringLiteral("folder")},
                                   {QStringLiteral("type"), QStringLiteral("Folder")}});

    queue()->setShuffled(true); // a previous shuffled session
    QSignalSpy replaced(m_actions, &ItemActions::queueChanged);
    m_actions->shuffleAllFrom(playlist);

    QCOMPARE(replaced.count(), 1);
    QCOMPARE(queue()->rowCount(), 4);
    QVERIFY(queue()->shuffled());
    QVERIFY(m_player->active());
    QCOMPARE(queue()->sourceLabel(), QString());
    const int playingRow = queue()->currentIndex();

    queue()->setShuffled(false);
    QCOMPARE(queueIds(queue()), (QStringList{"a1", "a2", "a1", "a3"}));
    QVERIFY(!queue()->shuffled());
    // The current entry kept the row it was dealt around.
    QCOMPARE(queue()->currentIndex(), playingRow);

    QSignalSpy failed(m_actions, &ItemActions::actionFailed);
    m_actions->shuffleAllFrom({});
    QCOMPARE(failed.count(), 1);
    QCOMPARE(queue()->rowCount(), 4);
}

// The generation guard holds for a shuffle as it does for a play: a reply that
// lands after a newer verb replaced the intent builds nothing.
void MusicPlaybackTest::supersededShuffleIsDropped()
{
    m_playback->shuffleAlbum(QStringLiteral("alA"), QStringLiteral("A"));
    m_playback->playAlbum(QStringLiteral("alB"), QStringLiteral("B")); // before A's reply
    QTRY_COMPARE(queueIds(queue()), (QStringList{"b1", "b2"}));
    QTest::qWait(100);
    QCOMPARE(queueIds(queue()), (QStringList{"b1", "b2"}));
    QVERIFY(!queue()->shuffled());
    QCOMPARE(queue()->sourceLabel(), QStringLiteral("B"));

    const quint64 stale = m_actions->reservePlaybackIntent();
    m_actions->reservePlaybackIntent();
    const QVariantList one{QVariantMap{{QStringLiteral("itemId"), QStringLiteral("a1")},
                                       {QStringLiteral("type"), QStringLiteral("Audio")}}};
    m_actions->shuffleAllFromIfCurrent(one, stale, QStringLiteral("stale"));
    QCOMPARE(queueIds(queue()), (QStringList{"b1", "b2"}));
    QCOMPARE(queue()->sourceLabel(), QStringLiteral("B"));
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

void MusicPlaybackTest::shuffleArtistQueuesTheServersRandomDraw()
{
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"ArtistIds", "ar1"}, {"SortBy", "Random"}}, 200,
                          page({trackJson("m2", "x", 2), trackJson("m1", "x", 1)}));

    m_playback->shuffleArtist(QStringLiteral("ar1"), QStringLiteral("Björk"));

    // The server already shuffled, and the user still asked for a shuffle: the
    // queue is flagged as one, and the server's draw is the order it holds as
    // its original — there being no better one for a sample of a discography.
    QTRY_COMPARE(queue()->rowCount(), 2);
    QCOMPARE(queue()->sourceLabel(), QStringLiteral("Shuffle · Björk"));
    QVERIFY(queue()->shuffled());
    queue()->setShuffled(false);
    QCOMPARE(queueIds(queue()), (QStringList{QStringLiteral("m2"), QStringLiteral("m1")}));

    const QUrlQuery query(m_mock->lastRequestFor(QStringLiteral("GET"), itemsPath()).query);
    QCOMPARE(query.queryItemValue(QStringLiteral("Limit")), QStringLiteral("200"));
    QCOMPARE(query.queryItemValue(QStringLiteral("IncludeItemTypes")), QStringLiteral("Audio"));
}

QTEST_GUILESS_MAIN(MusicPlaybackTest)
#include "tst_music_playback.moc"
