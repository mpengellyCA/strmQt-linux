#include <QSignalSpy>
#include <QtTest>

#include "FakePlayerBackend.h"
#include "MockEmbyServer.h"
#include "app/ItemActions.h"
#include "app/PlayQueue.h"
#include "app/controllers/PlayerController.h"
#include "app/models/MediaItemModel.h"
#include "core/Settings.h"
#include "server/emby/EmbyClient.h"

using namespace strmqt;

namespace {

QString fixturePath(const QString &name)
{
    return QStringLiteral(STRMQT_FIXTURES_DIR "/") + name;
}

const auto kUserId = QStringLiteral("a1b2c3d4e5f60718293a4b5c6d7e8f90");
const auto kToken = QStringLiteral("not-a-real-token-fixture-only");
const auto kSeriesId = QStringLiteral("s-9000");

QString itemsPath()
{
    return QStringLiteral("/Users/%1/Items").arg(kUserId);
}

} // namespace

// The queue verbs of ItemActions (ARCHITECTURE.md): what actually goes on the
// wire for "play all" and "shuffle", and what lands in the queue afterwards.
class ItemActionsQueueTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void playAllFetchesPlayableTypesInOrder();
    void shuffleAsksTheServerForARandomOrder();
    void shuffleNarrowsATvLibraryToEpisodes();
    void shuffleSeriesQueuesEveryEpisodeFromARandomStart();
    void playAllFromQueuesTheItemsItIsGiven();
    void playNextAndAddToQueueDoNotInterrupt();
    void aFailedFetchSaysSoInsteadOfDoingNothing();
    void anEmptyResultSaysSoToo();

private:
    MockEmbyServer *m_mock = nullptr;
    emby::EmbyClient *m_client = nullptr;
    FakePlayerBackend *m_backend = nullptr;
    QTemporaryDir *m_dir = nullptr;
    Settings *m_settings = nullptr;
    PlayerController *m_player = nullptr;
    ItemActions *m_actions = nullptr;
};

void ItemActionsQueueTest::init()
{
    m_mock = new MockEmbyServer(this);
    QVERIFY(m_mock->start());
    QVERIFY(m_mock->addRouteFromFile(QStringLiteral("GET"), itemsPath(),
                                     fixturePath(QStringLiteral("items_movies.json"))));
    QVERIFY(m_mock->addRouteFromFile(QStringLiteral("GET"),
                                     QStringLiteral("/Shows/%1/Episodes").arg(kSeriesId),
                                     fixturePath(QStringLiteral("episodes.json"))));
    for (const QString &id : {QStringLiteral("301001"), QStringLiteral("301002"),
                              QStringLiteral("301003"), QStringLiteral("402001"),
                              QStringLiteral("402002")}) {
        QVERIFY(m_mock->addRouteFromFile(QStringLiteral("POST"),
                                         QStringLiteral("/Items/%1/PlaybackInfo").arg(id),
                                         fixturePath(QStringLiteral("playback_info.json"))));
    }
    m_mock->addRoute(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing"), 204, {});
    m_mock->addRoute(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing/Progress"), 204, {});
    m_mock->addRoute(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing/Stopped"), 204, {});

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
}

void ItemActionsQueueTest::cleanup()
{
    delete m_actions;
    delete m_player;
    delete m_settings;
    delete m_dir;
    delete m_backend;
    delete m_client;
    delete m_mock;
    m_actions = nullptr;
    m_player = nullptr;
    m_settings = nullptr;
    m_dir = nullptr;
    m_backend = nullptr;
    m_client = nullptr;
    m_mock = nullptr;
}

void ItemActionsQueueTest::playAllFetchesPlayableTypesInOrder()
{
    QSignalSpy queueSpy(m_actions, &ItemActions::queueChanged);
    m_actions->playAll(QStringLiteral("lib-movies"), QStringLiteral("movies"));

    QTRY_COMPARE(queueSpy.count(), 1);
    QCOMPARE(m_player->queue()->rowCount(), 3);
    QCOMPARE(m_player->queue()->currentIndex(), 0);
    QVERIFY(!m_player->queue()->shuffled());
    QCOMPARE(m_player->queue()->current().id, QStringLiteral("301001"));
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);

    const QString query = m_mock->lastRequestFor(QStringLiteral("GET"), itemsPath()).query;
    QVERIFY2(query.contains(QStringLiteral("ParentId=lib-movies")), qPrintable(query));
    QVERIFY2(query.contains(QStringLiteral("IncludeItemTypes=Movie")), qPrintable(query));
    QVERIFY2(query.contains(QStringLiteral("Recursive=true")), qPrintable(query));
    QVERIFY2(query.contains(QStringLiteral("SortBy=SortName")), qPrintable(query));
    // Capped: a library is not a queue.
    QVERIFY2(query.contains(QStringLiteral("Limit=500")), qPrintable(query));
}

void ItemActionsQueueTest::shuffleAsksTheServerForARandomOrder()
{
    QSignalSpy queueSpy(m_actions, &ItemActions::queueChanged);
    m_actions->shuffle(QStringLiteral("lib-movies"), QStringLiteral("movies"));

    QTRY_COMPARE(queueSpy.count(), 1);
    QCOMPARE(m_player->queue()->rowCount(), 3);
    // The queue is flagged shuffled so the OSD toggle tells the truth.
    QVERIFY(m_player->queue()->shuffled());

    const QString query = m_mock->lastRequestFor(QStringLiteral("GET"), itemsPath()).query;
    QVERIFY2(query.contains(QStringLiteral("SortBy=Random")), qPrintable(query));
    QVERIFY2(query.contains(QStringLiteral("Limit=500")), qPrintable(query));
}

// A shuffle of a TV library must yield episodes; queueing series folders would
// queue things that cannot be played at all.
void ItemActionsQueueTest::shuffleNarrowsATvLibraryToEpisodes()
{
    m_actions->shuffle(QStringLiteral("lib-tv"), QStringLiteral("tvshows"));
    QTRY_VERIFY(!m_mock->lastRequestFor(QStringLiteral("GET"), itemsPath()).method.isEmpty());
    const QString query = m_mock->lastRequestFor(QStringLiteral("GET"), itemsPath()).query;
    QVERIFY2(query.contains(QStringLiteral("IncludeItemTypes=Episode")), qPrintable(query));

    // An unknown library kind falls back to every playable leaf type.
    m_actions->shuffle(QStringLiteral("lib-mixed"));
    QTRY_VERIFY(m_mock->lastRequestFor(QStringLiteral("GET"), itemsPath())
                    .query.contains(QStringLiteral("lib-mixed")));
    const QString mixed = m_mock->lastRequestFor(QStringLiteral("GET"), itemsPath()).query;
    QVERIFY2(mixed.contains(QStringLiteral("Movie")), qPrintable(mixed));
    QVERIFY2(mixed.contains(QStringLiteral("Episode")), qPrintable(mixed));
}

void ItemActionsQueueTest::shuffleSeriesQueuesEveryEpisodeFromARandomStart()
{
    QSignalSpy queueSpy(m_actions, &ItemActions::queueChanged);
    m_actions->shuffleSeries(kSeriesId);

    QTRY_COMPARE(queueSpy.count(), 1);
    QCOMPARE(m_player->queue()->rowCount(), 2);
    QVERIFY(m_player->queue()->shuffled());
    QVERIFY(m_player->active());
    // Whatever the random start was, the queue is playing one of the episodes.
    const QString playing = m_player->queue()->current().id;
    QVERIFY(playing == QStringLiteral("402001") || playing == QStringLiteral("402002"));

    // Un-shuffling gives back air order, which /Shows/{id}/Episodes returned.
    m_player->queue()->setShuffled(false);
    QCOMPARE(m_player->queue()->itemAt(0).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("402001"));
    QCOMPARE(m_player->queue()->itemAt(1).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("402002"));
    QCOMPARE(m_player->queue()->current().id, playing);
}

void ItemActionsQueueTest::playAllFromQueuesTheItemsItIsGiven()
{
    QVariantMap first;
    first.insert(QStringLiteral("itemId"), QStringLiteral("301002"));
    first.insert(QStringLiteral("name"), QStringLiteral("Blade Runner 2049"));
    QVariantMap second;
    second.insert(QStringLiteral("itemId"), QStringLiteral("301003"));
    second.insert(QStringLiteral("name"), QStringLiteral("Sparse"));

    QSignalSpy queueSpy(m_actions, &ItemActions::queueChanged);
    m_actions->playAllFrom({first, second}, 1);
    QCOMPARE(queueSpy.count(), 1);
    QCOMPARE(m_player->queue()->rowCount(), 2);
    QCOMPARE(m_player->queue()->currentIndex(), 1);
    QCOMPARE(m_player->title(), QStringLiteral("Sparse"));

    QSignalSpy failedSpy(m_actions, &ItemActions::actionFailed);
    m_actions->playAllFrom({}, 0);
    QCOMPARE(failedSpy.count(), 1);
}

void ItemActionsQueueTest::playNextAndAddToQueueDoNotInterrupt()
{
    m_player->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateState(PlayerBackend::State::Playing);

    QVariantMap next;
    next.insert(QStringLiteral("itemId"), QStringLiteral("301002"));
    next.insert(QStringLiteral("name"), QStringLiteral("Blade Runner 2049"));
    QVariantMap later;
    later.insert(QStringLiteral("itemId"), QStringLiteral("301003"));
    later.insert(QStringLiteral("name"), QStringLiteral("Sparse"));

    QSignalSpy queueSpy(m_actions, &ItemActions::queueChanged);
    m_actions->addToQueue(later);
    m_actions->playNext(next);
    QCOMPARE(queueSpy.count(), 2);

    QCOMPARE(m_player->queue()->rowCount(), 3);
    QCOMPARE(m_player->queue()->currentIndex(), 0);
    // "Play next" goes ahead of what was merely appended.
    QCOMPARE(m_player->queue()->itemAt(1).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("301002"));
    QCOMPARE(m_player->queue()->itemAt(2).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("301003"));
    // Nothing reloaded: the running item keeps running.
    QTest::qWait(30);
    QCOMPARE(m_backend->loadedUrls.size(), 1);
    QCOMPARE(m_player->title(), QStringLiteral("The Matrix"));

    // An item with no id is not a queue entry, and says nothing to the user.
    m_actions->playNext(QVariantMap{});
    QCOMPARE(queueSpy.count(), 2);
    QCOMPARE(m_player->queue()->rowCount(), 3);
}

void ItemActionsQueueTest::aFailedFetchSaysSoInsteadOfDoingNothing()
{
    m_mock->addRoute(QStringLiteral("GET"), itemsPath(), 500, QByteArrayLiteral("{}"));
    QSignalSpy failedSpy(m_actions, &ItemActions::actionFailed);
    QSignalSpy queueSpy(m_actions, &ItemActions::queueChanged);

    m_actions->playAll(QStringLiteral("lib-movies"), QStringLiteral("movies"));
    QTRY_COMPARE(failedSpy.count(), 1);
    QCOMPARE(queueSpy.count(), 0);
    QCOMPARE(m_player->queue()->rowCount(), 0);

    m_actions->shuffle(QStringLiteral("lib-movies"), QStringLiteral("movies"));
    QTRY_COMPARE(failedSpy.count(), 2);
}

void ItemActionsQueueTest::anEmptyResultSaysSoToo()
{
    m_mock->addRoute(QStringLiteral("GET"), itemsPath(), 200,
                     QByteArrayLiteral("{\"Items\":[],\"TotalRecordCount\":0}"));
    QSignalSpy failedSpy(m_actions, &ItemActions::actionFailed);
    m_actions->playAll(QStringLiteral("lib-empty"), QStringLiteral("movies"));
    QTRY_COMPARE(failedSpy.count(), 1);
    QCOMPARE(m_player->queue()->rowCount(), 0);
    QVERIFY(!m_player->active());
}

QTEST_GUILESS_MAIN(ItemActionsQueueTest)
#include "tst_item_actions_queue.moc"
