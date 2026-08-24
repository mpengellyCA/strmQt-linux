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
#include "app/controllers/MusicController.h"
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

QByteArray pageBody(const QJsonArray &items)
{
    return QJsonDocument(QJsonObject{{QStringLiteral("Items"), items},
                                     {QStringLiteral("TotalRecordCount"), items.size()}})
        .toJson(QJsonDocument::Compact);
}

QJsonObject itemObject(const QString &id, const QString &type, const QString &name = QString())
{
    return {{QStringLiteral("Id"), id},
            {QStringLiteral("Name"), name.isEmpty() ? id : name},
            {QStringLiteral("Type"), type}};
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
    void seasonDispatchExpandsEpisodeChildren();
    void playAllFromQueuesTheItemsItIsGiven();
    void playNextAndAddToQueueDoNotInterrupt();
    void aFailedFetchSaysSoInsteadOfDoingNothing();
    void anEmptyResultSaysSoToo();
    void playAlbumQueuesTheServersOrderWithoutOpeningTheAlbum();
    void canonicalAlbumPlayUsesTheOrderedControllerExpansion();
    void forbiddenContainerDispatchCannotReachPlaybackOrQueue();
    void idOnlyItemsKeepEveryQueueVerb();
    void mixedCollectionWalkIsOrderedAtomicAndTolerant();
    void boxSetShuffleRequestsMixedPlayableLeaves();
    void collectionWalkFailureIsAtomic();
    void collectionWalkCapsRowsAndNestedExpansions();
    void playCarriesTheItemsTypeOntoTheSeededQueue();
    void playAndResumeRetainTaggedArtwork();
    void artistTargetPrefersTheAlbumArtistAndPairsItWithItsOwnId();
    void instantMixQueuesTheServersStationAndDropsRepeats();
    void instantMixSaysSoWhenTheServerHasNoStation();
    void addAllToQueueIsOneGestureAndOneToast();
    void collectAlbumTracksReportsIdsWithoutTouchingThePlayer();
    void newerLeafPlayRetiresEveryAsynchronousQueueBuilder();
    void sessionResetRetiresQueueBuildersAndClearsRegisteredModels();

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
    m_actions->performItemVerb(QStringLiteral("shuffle"),
                               QVariantMap{{QStringLiteral("itemId"), kSeriesId},
                                           {QStringLiteral("name"), QStringLiteral("Series")},
                                           {QStringLiteral("type"), QStringLiteral("Series")}});

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

void ItemActionsQueueTest::seasonDispatchExpandsEpisodeChildren()
{
    m_mock->enqueueRoute(QStringLiteral("GET"), itemsPath(), 200, pageBody({}));
    QSignalSpy failed(m_actions, &ItemActions::actionFailed);
    m_actions->performItemVerb(QStringLiteral("play"),
                               QVariantMap{{QStringLiteral("itemId"), QStringLiteral("season-7")},
                                           {QStringLiteral("name"), QStringLiteral("Season 7")},
                                           {QStringLiteral("type"), QStringLiteral("Season")}});
    QTRY_COMPARE(failed.count(), 1);
    const QUrlQuery query(m_mock->lastRequestFor(QStringLiteral("GET"), itemsPath()).query);
    QCOMPARE(query.queryItemValue(QStringLiteral("ParentId")), QStringLiteral("season-7"));
    QCOMPARE(query.queryItemValue(QStringLiteral("Recursive")), QStringLiteral("true"));
    QCOMPARE(query.queryItemValue(QStringLiteral("IncludeItemTypes")), QStringLiteral("Episode"));
    QCOMPARE(query.queryItemValue(QStringLiteral("SortBy")),
             QStringLiteral("PremiereDate,SortName"));
}

void ItemActionsQueueTest::playAllFromQueuesTheItemsItIsGiven()
{
    QVariantMap first;
    first.insert(QStringLiteral("itemId"), QStringLiteral("301002"));
    first.insert(QStringLiteral("name"), QStringLiteral("Blade Runner 2049"));
    first.insert(QStringLiteral("type"), QStringLiteral("Movie"));
    QVariantMap second;
    second.insert(QStringLiteral("itemId"), QStringLiteral("301003"));
    second.insert(QStringLiteral("name"), QStringLiteral("Sparse"));
    second.insert(QStringLiteral("type"), QStringLiteral("Audio"));

    QSignalSpy queueSpy(m_actions, &ItemActions::queueChanged);
    m_actions->playAllFrom({first, second}, 1);
    QCOMPARE(queueSpy.count(), 1);
    QCOMPARE(m_player->queue()->rowCount(), 2);
    QCOMPARE(m_player->queue()->currentIndex(), 1);
    QCOMPARE(m_player->title(), QStringLiteral("Sparse"));

    QSignalSpy failedSpy(m_actions, &ItemActions::actionFailed);
    m_actions->playAllFrom(
        {QVariantMap{{QStringLiteral("itemId"), QStringLiteral("folder-drift")},
                     {QStringLiteral("type"), QStringLiteral("Folder")}}},
        0);
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(m_player->queue()->rowCount(), 2);
    m_actions->playAllFrom({}, 0);
    QCOMPARE(failedSpy.count(), 2);
}

void ItemActionsQueueTest::playNextAndAddToQueueDoNotInterrupt()
{
    m_player->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateState(PlayerBackend::State::Playing);

    QVariantMap next;
    next.insert(QStringLiteral("itemId"), QStringLiteral("301002"));
    next.insert(QStringLiteral("name"), QStringLiteral("Blade Runner 2049"));
    next.insert(QStringLiteral("type"), QStringLiteral("Movie"));
    QVariantMap later;
    later.insert(QStringLiteral("itemId"), QStringLiteral("301003"));
    later.insert(QStringLiteral("name"), QStringLiteral("Sparse"));
    later.insert(QStringLiteral("type"), QStringLiteral("Audio"));

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

// MusicPage's ▸ used to be a side channel: openAlbum() followed by a QML guard
// watching the shared `tracks` model fill, which meant playing a record
// *navigated controller state* and would have fought the album page the moment
// both were live. playAlbum() is the verb that replaced it — its own fetch, its
// own scratch model, and the album page's list left alone.
void ItemActionsQueueTest::playAlbumQueuesTheServersOrderWithoutOpeningTheAlbum()
{
    m_mock->addRoute(
        QStringLiteral("GET"), itemsPath(), 200,
        QByteArrayLiteral("{\"Items\":["
                          "{\"Id\":\"301001\",\"Name\":\"So What\",\"Type\":\"Audio\","
                          "\"IndexNumber\":1},"
                          "{\"Id\":\"301002\",\"Name\":\"Freddie Freeloader\",\"Type\":\"Audio\","
                          "\"IndexNumber\":2},"
                          "{\"Id\":\"301003\",\"Name\":\"Blue In Green\",\"Type\":\"Audio\","
                          "\"IndexNumber\":3}],\"TotalRecordCount\":3}"));

    MusicController music(m_client, this);
    music.setActions(m_actions);

    QSignalSpy queueSpy(m_actions, &ItemActions::queueChanged);
    music.playAlbum(QStringLiteral("al-kob"));

    QTRY_COMPARE(queueSpy.count(), 1);
    QCOMPARE(m_player->queue()->rowCount(), 3);
    QCOMPARE(m_player->queue()->currentIndex(), 0);
    // Disc/track order, which is the order the server returned. An album queued
    // alphabetically is the bug this verb exists to avoid.
    QCOMPARE(m_player->queue()->itemAt(0).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("301001"));
    QCOMPARE(m_player->queue()->itemAt(2).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("301003"));

    // The whole point of the scratch list: nothing the album page reads moved.
    QCOMPARE(music.tracks()->rowCount(), 0);
    QVERIFY(music.albumId().isEmpty());

    const QString query = m_mock->lastRequestFor(QStringLiteral("GET"), itemsPath()).query;
    QVERIFY2(query.contains(QStringLiteral("ParentId=al-kob")), qPrintable(query));
    // Neither sorted nor recursive: an album's children already come back in
    // disc then track order, and recursing an album folder means nothing.
    QVERIFY2(!query.contains(QStringLiteral("SortBy")), qPrintable(query));
    QVERIFY2(!query.contains(QStringLiteral("Recursive")), qPrintable(query));
}

void ItemActionsQueueTest::canonicalAlbumPlayUsesTheOrderedControllerExpansion()
{
    m_mock->addRoute(QStringLiteral("GET"), itemsPath(), 200,
                     QByteArrayLiteral("{\"Items\":["
                                       "{\"Id\":\"301002\",\"Name\":\"Second\",\"Type\":\"Audio\"},"
                                       "{\"Id\":\"301001\",\"Name\":\"First\",\"Type\":\"Audio\"}],"
                                       "\"TotalRecordCount\":2}"));
    MusicController music(m_client, this);
    music.setActions(m_actions);
    connect(m_actions, &ItemActions::orderedAlbumPlayRequested, &music,
            &MusicController::playAlbum);

    QVariantMap album{{QStringLiteral("itemId"), QStringLiteral("album-ordered")},
                      {QStringLiteral("name"), QStringLiteral("Server order")},
                      {QStringLiteral("type"), QStringLiteral("MusicAlbum")}};
    QSignalSpy ordered(m_actions, &ItemActions::orderedAlbumPlayRequested);
    QSignalSpy queue(m_actions, &ItemActions::queueChanged);
    m_actions->play(album);

    QCOMPARE(ordered.count(), 1);
    QTRY_COMPARE(queue.count(), 1);
    QCOMPARE(m_player->queue()->rowCount(), 2);
    QCOMPARE(m_player->queue()->itemAt(0).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("301002"));
    const QString query = m_mock->lastRequestFor(QStringLiteral("GET"), itemsPath()).query;
    QVERIFY2(query.contains(QStringLiteral("ParentId=album-ordered")), qPrintable(query));
    QVERIFY2(!query.contains(QStringLiteral("SortBy")), qPrintable(query));
    QVERIFY2(!query.contains(QStringLiteral("Recursive")), qPrintable(query));
}

void ItemActionsQueueTest::forbiddenContainerDispatchCannotReachPlaybackOrQueue()
{
    QSignalSpy queue(m_actions, &ItemActions::queueChanged);
    QSignalSpy routes(m_actions, &ItemActions::routeRequested);
    QSignalSpy ordered(m_actions, &ItemActions::orderedAlbumPlayRequested);
    QSignalSpy failed(m_actions, &ItemActions::actionFailed);
    const int beforeRequests = m_mock->requestCount();

    for (const QString &type : {QStringLiteral("MusicArtist"), QStringLiteral("Playlist")}) {
        const QVariantMap item{{QStringLiteral("itemId"), QStringLiteral("forbidden-id")},
                               {QStringLiteral("name"), QStringLiteral("Forbidden")},
                               {QStringLiteral("type"), type}};
        for (const QString &verb : {QStringLiteral("play"), QStringLiteral("playNext"),
                                    QStringLiteral("addToQueue"), QStringLiteral("shuffle")}) {
            m_actions->performItemVerb(verb, item);
        }
        m_actions->playNext(item);
        m_actions->addToQueue(item);
    }
    QCOMPARE(queue.count(), 0);
    QCOMPARE(ordered.count(), 0);
    QCOMPARE(routes.count(), 0);
    QCOMPARE(m_player->queue()->rowCount(), 0);
    QCOMPARE(m_mock->requestCount(), beforeRequests);
    // Refused, but never in silence: the direct verbs a card's Play button and
    // the remote both reach say why. (performItemVerb is gated by capability
    // and never calls through, so only the two direct calls per kind toast.)
    QCOMPARE(failed.count(), 4);

    // The legacy direct play surface is safe too: a playlist becomes an Open
    // route and an artist remains a no-op, never a guessed ParentId expansion.
    m_actions->play(QVariantMap{{QStringLiteral("itemId"), QStringLiteral("playlist-id")},
                                {QStringLiteral("name"), QStringLiteral("Playlist")},
                                {QStringLiteral("type"), QStringLiteral("Playlist")}});
    QCOMPARE(routes.count(), 1);
    QCOMPARE(routes.last().at(0).toString(), QStringLiteral("playlist"));
    m_actions->play(QVariantMap{{QStringLiteral("itemId"), QStringLiteral("artist-id")},
                                {QStringLiteral("name"), QStringLiteral("Artist")},
                                {QStringLiteral("type"), QStringLiteral("MusicArtist")}});
    QCOMPARE(routes.count(), 1);

    const QVariantMap unknown{{QStringLiteral("itemId"), QStringLiteral("unknown-id")},
                              {QStringLiteral("name"), QStringLiteral("Unknown")},
                              {QStringLiteral("type"), QStringLiteral("FutureServerType")},
                              {QStringLiteral("positionMs"), 20'000}};
    m_actions->play(unknown);
    m_actions->resume(unknown);
    QCOMPARE(routes.count(), 1);
    QCOMPARE(m_mock->requestCount(), beforeRequests);
}

// An Emby "Play on this client" command carries ItemIds and nothing else, so
// RemoteControlService hands ItemActions a map with no `type` at all whenever
// the item is not already in a registered model. An ABSENT type is not the
// server calling this a folder — the queue row fills in when the item's own
// details arrive — while a type the server DID state and this build does not
// recognise stays refused by the test above. Gating the queue verbs on a stated
// leaf kind alone silently took every remote verb out.
void ItemActionsQueueTest::idOnlyItemsKeepEveryQueueVerb()
{
    const QVariantMap first{{QStringLiteral("itemId"), QStringLiteral("301001")},
                            {QStringLiteral("name"), QString()}};
    const QVariantMap second{{QStringLiteral("itemId"), QStringLiteral("301002")},
                             {QStringLiteral("name"), QString()}};
    const QVariantMap third{{QStringLiteral("itemId"), QStringLiteral("301003")},
                            {QStringLiteral("name"), QString()}};

    QSignalSpy queue(m_actions, &ItemActions::queueChanged);
    m_actions->playAllFrom({first}, 0);
    QTRY_COMPARE(queue.count(), 1);
    QCOMPARE(m_player->queue()->rowCount(), 1);

    m_actions->playNextAll({second});
    m_actions->addAllToQueue({third});
    QCOMPARE(queue.count(), 3);
    QCOMPARE(m_player->queue()->rowCount(), 3);
    QCOMPARE(m_player->queue()->itemAt(1).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("301002"));
    QCOMPARE(m_player->queue()->itemAt(2).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("301003"));

    m_actions->playNext(second);
    m_actions->addToQueue(third);
    QCOMPARE(queue.count(), 5);
    QCOMPARE(m_player->queue()->rowCount(), 5);
}

void ItemActionsQueueTest::mixedCollectionWalkIsOrderedAtomicAndTolerant()
{
    m_mock->enqueueRoute(
        QStringLiteral("GET"), itemsPath(), 200,
        pageBody(
            {itemObject(QStringLiteral("301001"), QStringLiteral("Movie"), QStringLiteral("A")),
             itemObject(QStringLiteral("series-1"), QStringLiteral("Series")),
             itemObject(QStringLiteral("season-1"), QStringLiteral("Season")),
             itemObject(QStringLiteral("301003"), QStringLiteral("Movie"), QStringLiteral("B"))}));
    m_mock->enqueueRoute(
        QStringLiteral("GET"), itemsPath(), 200,
        pageBody({itemObject(QStringLiteral("402001"), QStringLiteral("Episode")),
                  itemObject(QStringLiteral("bad-folder"), QStringLiteral("Series")),
                  itemObject(QStringLiteral("402002"), QStringLiteral("Episode"))}));
    // The duplicate episode is intentional: collection slots are not media-id
    // sets, and two occurrences must survive materialization.
    m_mock->enqueueRoute(
        QStringLiteral("GET"), itemsPath(), 200,
        pageBody({itemObject(QStringLiteral("402002"), QStringLiteral("Episode"))}));

    QSignalSpy queue(m_actions, &ItemActions::queueChanged);
    m_actions->performItemVerb(QStringLiteral("play"),
                               QVariantMap{{QStringLiteral("itemId"), QStringLiteral("box-1")},
                                           {QStringLiteral("name"), QStringLiteral("Mixed")},
                                           {QStringLiteral("type"), QStringLiteral("BoxSet")}});
    QTRY_COMPARE(queue.count(), 1);

    QCOMPARE(m_player->queue()->rowCount(), 5);
    QCOMPARE(m_player->queue()->itemAt(0).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("301001"));
    QCOMPARE(m_player->queue()->itemAt(1).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("402001"));
    QCOMPARE(m_player->queue()->itemAt(2).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("402002"));
    QCOMPARE(m_player->queue()->itemAt(3).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("402002"));
    QCOMPARE(m_player->queue()->itemAt(4).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("301003"));

    QList<MockEmbyServer::ReceivedRequest> itemRequests;
    for (const auto &request : m_mock->requests()) {
        if (request.method == QLatin1String("GET") && request.path == itemsPath())
            itemRequests.append(request);
    }
    QCOMPARE(itemRequests.size(), 3);
    const QUrlQuery rootQuery(itemRequests.at(0).query);
    QCOMPARE(rootQuery.queryItemValue(QStringLiteral("ParentId")), QStringLiteral("box-1"));
    QVERIFY(rootQuery.queryItemValue(QStringLiteral("Recursive")).isEmpty());
    QVERIFY(rootQuery.queryItemValue(QStringLiteral("SortBy")).isEmpty());
    for (int index = 1; index < itemRequests.size(); ++index) {
        const QUrlQuery childQuery(itemRequests.at(index).query);
        QCOMPARE(childQuery.queryItemValue(QStringLiteral("Recursive")), QStringLiteral("true"));
        QCOMPARE(childQuery.queryItemValue(QStringLiteral("IncludeItemTypes")),
                 QStringLiteral("Episode"));
        QCOMPARE(childQuery.queryItemValue(QStringLiteral("SortBy")),
                 QStringLiteral("PremiereDate,SortName"));
    }
}

void ItemActionsQueueTest::boxSetShuffleRequestsMixedPlayableLeaves()
{
    m_mock->enqueueRoute(QStringLiteral("GET"), itemsPath(), 200, pageBody({}));
    QSignalSpy failed(m_actions, &ItemActions::actionFailed);
    m_actions->performItemVerb(
        QStringLiteral("shuffle"),
        QVariantMap{{QStringLiteral("itemId"), QStringLiteral("box-shuffle")},
                    {QStringLiteral("name"), QStringLiteral("Mixed")},
                    {QStringLiteral("type"), QStringLiteral("BoxSet")}});
    QTRY_COMPARE(failed.count(), 1);
    const QUrlQuery query(m_mock->lastRequestFor(QStringLiteral("GET"), itemsPath()).query);
    QCOMPARE(query.queryItemValue(QStringLiteral("ParentId")), QStringLiteral("box-shuffle"));
    QCOMPARE(query.queryItemValue(QStringLiteral("Recursive")), QStringLiteral("true"));
    QCOMPARE(query.queryItemValue(QStringLiteral("IncludeItemTypes")),
             QStringLiteral("Movie,Episode,Video"));
    QCOMPARE(query.queryItemValue(QStringLiteral("SortBy")), QStringLiteral("Random"));
}

void ItemActionsQueueTest::collectionWalkFailureIsAtomic()
{
    QVariantMap seed{{QStringLiteral("itemId"), QStringLiteral("301001")},
                     {QStringLiteral("name"), QStringLiteral("Existing")},
                     {QStringLiteral("type"), QStringLiteral("Movie")}};
    m_actions->play(seed);
    QCOMPARE(m_player->queue()->rowCount(), 1);

    m_mock->enqueueRoute(
        QStringLiteral("GET"), itemsPath(), 200,
        pageBody({itemObject(QStringLiteral("301003"), QStringLiteral("Movie")),
                  itemObject(QStringLiteral("series-fail"), QStringLiteral("Series"))}));
    m_mock->enqueueRoute(QStringLiteral("GET"), itemsPath(), 500, QByteArrayLiteral("{}"));
    QSignalSpy failed(m_actions, &ItemActions::actionFailed);
    QSignalSpy queue(m_actions, &ItemActions::queueChanged);
    m_actions->playCollection(QStringLiteral("box-fail"));

    QTRY_COMPARE(failed.count(), 1);
    QCOMPARE(queue.count(), 0);
    QCOMPARE(m_player->queue()->rowCount(), 1);
    QCOMPARE(m_player->queue()->current().id, QStringLiteral("301001"));
}

void ItemActionsQueueTest::collectionWalkCapsRowsAndNestedExpansions()
{
    QJsonArray duplicates;
    for (int index = 0; index < 500; ++index)
        duplicates.append(itemObject(QStringLiteral("301001"), QStringLiteral("Movie")));
    m_mock->enqueueRoute(QStringLiteral("GET"), itemsPath(), 200, pageBody(duplicates));
    QSignalSpy queue(m_actions, &ItemActions::queueChanged);
    m_actions->playCollection(QStringLiteral("box-500"));
    QTRY_COMPARE(queue.count(), 1);
    QCOMPARE(m_player->queue()->rowCount(), 500);

    m_player->stop();
    m_player->queue()->clear();
    QJsonArray nested;
    for (int index = 0; index < 65; ++index) {
        nested.append(itemObject(QStringLiteral("series-%1").arg(index), QStringLiteral("Series")));
    }
    m_mock->enqueueRoute(QStringLiteral("GET"), itemsPath(), 200, pageBody(nested));
    for (int index = 0; index < 64; ++index)
        m_mock->enqueueRoute(QStringLiteral("GET"), itemsPath(), 200, pageBody({}));
    QSignalSpy failed(m_actions, &ItemActions::actionFailed);
    const int beforeRequests = m_mock->requestCount();
    m_actions->playCollection(QStringLiteral("box-too-deep"));
    QTRY_COMPARE(failed.count(), 1);
    QCOMPARE(m_mock->requestCount() - beforeRequests, 65); // root + 64 expansions
    QCOMPARE(m_player->queue()->rowCount(), 0);
}

// A bare play seeds a one-item queue, and that seed is the first thing
// PlayerController::isAudio() reads. Every leaf item goes down this path — the
// menu's "Play", a card's Play button, a search result — so dropping the type
// here is what left the docked bar laid out for the previous item until the
// ticket came back.
void ItemActionsQueueTest::playCarriesTheItemsTypeOntoTheSeededQueue()
{
    QVariantMap track;
    track.insert(QStringLiteral("itemId"), QStringLiteral("301001"));
    track.insert(QStringLiteral("name"), QStringLiteral("So What"));
    track.insert(QStringLiteral("type"), QStringLiteral("Audio"));

    m_actions->play(track);
    // Synchronously, before any reply: nothing here waited on the network.
    QCOMPARE(m_player->queue()->rowCount(), 1);
    QCOMPARE(m_player->queue()->itemAt(0).value(QStringLiteral("type")).toString(),
             QStringLiteral("Audio"));
    QVERIFY(m_player->isAudio());

    QVariantMap movie;
    movie.insert(QStringLiteral("itemId"), QStringLiteral("301002"));
    movie.insert(QStringLiteral("name"), QStringLiteral("Blade Runner 2049"));
    movie.insert(QStringLiteral("type"), QStringLiteral("Movie"));

    m_actions->play(movie);
    QCOMPARE(m_player->queue()->itemAt(0).value(QStringLiteral("type")).toString(),
             QStringLiteral("Movie"));
    QVERIFY(!m_player->isAudio());
}

void ItemActionsQueueTest::playAndResumeRetainTaggedArtwork()
{
    QVariantMap item;
    item.insert(QStringLiteral("itemId"), QStringLiteral("301001"));
    item.insert(QStringLiteral("name"), QStringLiteral("The Matrix"));
    item.insert(QStringLiteral("type"), QStringLiteral("Movie"));
    item.insert(QStringLiteral("posterUrl"),
                QStringLiteral("image://emby/session/301001/Primary/poster-tag"));
    item.insert(QStringLiteral("positionMs"), 42000);

    m_actions->play(item);
    QCOMPARE(m_player->queue()->current().primaryImageTag, QStringLiteral("poster-tag"));

    m_actions->resume(item);
    QCOMPARE(m_player->queue()->current().primaryImageTag, QStringLiteral("poster-tag"));
    QCOMPARE(m_player->queue()->current().playbackPositionTicks, 42000 * kTicksPerMs);
}

// One rule for "go to artist", wherever it is asked from (MUSIC.md §4). The
// album artist wins, and the name and the id it hands back describe the same
// person even when the server credits a performer it has no artist item for.
void ItemActionsQueueTest::artistTargetPrefersTheAlbumArtistAndPairsItWithItsOwnId()
{
    QVariantMap track;
    track.insert(QStringLiteral("itemId"), QStringLiteral("301001"));
    track.insert(QStringLiteral("type"), QStringLiteral("Audio"));
    track.insert(QStringLiteral("artists"),
                 QStringList{QStringLiteral("Featured Guest"), QStringLiteral("Main Band")});
    // The mapper leaves a hole where a credited name has no artist item, so the
    // guest's name does not borrow the band's id.
    track.insert(QStringLiteral("artistIds"), QStringList{QString(), QStringLiteral("ar-band")});
    track.insert(QStringLiteral("albumArtist"), QStringLiteral("Main Band"));

    const QVariantMap target = m_actions->artistTarget(track);
    QCOMPARE(target.value(QStringLiteral("itemId")).toString(), QStringLiteral("ar-band"));
    QCOMPARE(target.value(QStringLiteral("name")).toString(), QStringLiteral("Main Band"));
    QCOMPARE(target.value(QStringLiteral("type")).toString(), QStringLiteral("MusicArtist"));

    // No album artist: the first credit, with its own id.
    QVariantMap other;
    other.insert(QStringLiteral("itemId"), QStringLiteral("301002"));
    other.insert(QStringLiteral("artists"),
                 QStringList{QStringLiteral("Main Band"), QStringLiteral("Featured Guest")});
    other.insert(QStringLiteral("artistIds"),
                 QStringList{QStringLiteral("ar-band"), QStringLiteral("ar-guest")});
    const QVariantMap first = m_actions->artistTarget(other);
    QCOMPARE(first.value(QStringLiteral("itemId")).toString(), QStringLiteral("ar-band"));
    QCOMPARE(first.value(QStringLiteral("name")).toString(), QStringLiteral("Main Band"));

    // A name with nowhere to go is still a name: the bar prints it and drops
    // the link rather than navigating to somebody else.
    QVariantMap nameless;
    nameless.insert(QStringLiteral("itemId"), QStringLiteral("301003"));
    nameless.insert(QStringLiteral("artists"), QStringList{QStringLiteral("Featured Guest")});
    const QVariantMap unlinked = m_actions->artistTarget(nameless);
    QCOMPARE(unlinked.value(QStringLiteral("name")).toString(), QStringLiteral("Featured Guest"));
    QVERIFY(unlinked.value(QStringLiteral("itemId")).toString().isEmpty());

    // Nothing credited at all: no target, and therefore no menu row.
    QVariantMap bare;
    bare.insert(QStringLiteral("itemId"), QStringLiteral("301003"));
    QVERIFY(m_actions->artistTarget(bare).isEmpty());
}

// ── Instant mix (MUSIC.md §7) ────────────────────────────────────────────────
// The endpoint's measured shape is in EmbyClient::instantMix(): the seed comes
// back as row 0, the rows are NOT distinct, and the count is the array's own
// size rather than a total. This asserts what the verb does about the second of
// those, because it is the one the user would hear.
void ItemActionsQueueTest::instantMixQueuesTheServersStationAndDropsRepeats()
{
    // Measured against the live server: 500 rows asked for came back with 493
    // distinct ids. Here, in miniature — the seed, two more, and the seed again.
    m_mock->addRoute(
        QStringLiteral("GET"), QStringLiteral("/Items/301001/InstantMix"), 200,
        QByteArrayLiteral("{\"Items\":["
                          "{\"Id\":\"301001\",\"Name\":\"So What\",\"Type\":\"Audio\"},"
                          "{\"Id\":\"301002\",\"Name\":\"Freddie\",\"Type\":\"Audio\"},"
                          "{\"Id\":\"301001\",\"Name\":\"So What\",\"Type\":\"Audio\"},"
                          "{\"Id\":\"301003\",\"Name\":\"Blue\",\"Type\":\"Audio\"}],"
                          "\"TotalRecordCount\":4}"));

    QSignalSpy queueSpy(m_actions, &ItemActions::queueChanged);
    m_actions->instantMix(QStringLiteral("301001"));
    QTRY_COMPARE(queueSpy.count(), 1);

    // Four rows in, three out, in the order the server sent them.
    QCOMPARE(m_player->queue()->rowCount(), 3);
    QCOMPARE(m_player->queue()->itemAt(0).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("301001"));
    QCOMPARE(m_player->queue()->itemAt(1).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("301002"));
    QCOMPARE(m_player->queue()->itemAt(2).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("301003"));
    // The station starts at its first row — the seed, for a track — and is NOT
    // flagged shuffled: there is no original order to give back.
    QCOMPARE(m_player->queue()->currentIndex(), 0);
    QVERIFY(!m_player->queue()->shuffled());

    const QString query =
        m_mock->lastRequestFor(QStringLiteral("GET"), QStringLiteral("/Items/301001/InstantMix"))
            .query;
    // No StartIndex: the endpoint does not page — StartIndex=5 was measured to
    // answer a fresh randomised set, not the sixth row onward.
    QVERIFY2(!query.contains(QStringLiteral("StartIndex")), qPrintable(query));
    QVERIFY2(query.contains(QStringLiteral("UserId=")), qPrintable(query));
}

void ItemActionsQueueTest::instantMixSaysSoWhenTheServerHasNoStation()
{
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Items/301001/InstantMix"), 200,
                     QByteArrayLiteral("{\"Items\":[],\"TotalRecordCount\":0}"));

    QSignalSpy failed(m_actions, &ItemActions::actionFailed);
    QSignalSpy queueSpy(m_actions, &ItemActions::queueChanged);
    m_actions->instantMix(QStringLiteral("301001"));

    QTRY_COMPARE(failed.count(), 1);
    QCOMPARE(queueSpy.count(), 0);
    QCOMPARE(m_player->queue()->rowCount(), 0);
}

// A batch verb is ONE gesture. Forty toasts is not forty pieces of feedback.
void ItemActionsQueueTest::addAllToQueueIsOneGestureAndOneToast()
{
    QVariantList picked;
    for (const QString &id : {QStringLiteral("301001"), QStringLiteral("301002"),
                              QStringLiteral("301003")}) {
        QVariantMap row;
        row.insert(QStringLiteral("itemId"), id);
        row.insert(QStringLiteral("name"), QStringLiteral("Track ") + id);
        row.insert(QStringLiteral("type"), QStringLiteral("Audio"));
        picked.append(row);
    }
    // One row with nothing usable on it, which a selection can legitimately
    // produce if a model row was dropped between the pick and the press.
    picked.append(QVariantMap{});

    QSignalSpy queueSpy(m_actions, &ItemActions::queueChanged);
    m_actions->addAllToQueue(picked);

    QCOMPARE(queueSpy.count(), 1);
    QCOMPARE(m_player->queue()->rowCount(), 3);
    QCOMPARE(m_player->queue()->itemAt(2).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("301003"));

    // Nothing usable at all is a failure, not a silent no-op.
    QSignalSpy failed(m_actions, &ItemActions::actionFailed);
    m_actions->addAllToQueue({QVariantMap{}});
    QCOMPARE(failed.count(), 1);
    QCOMPARE(queueSpy.count(), 1);
}

// The album grid's "Add to playlist" (MUSIC.md §3's carried-over gap): a
// playlist holds an album's TRACKS, and only the server can expand the id.
void ItemActionsQueueTest::collectAlbumTracksReportsIdsWithoutTouchingThePlayer()
{
    m_mock->addRoute(
        QStringLiteral("GET"), itemsPath(), 200,
        QByteArrayLiteral("{\"Items\":["
                          "{\"Id\":\"301001\",\"Name\":\"So What\",\"Type\":\"Audio\"},"
                          "{\"Id\":\"301002\",\"Name\":\"Freddie\",\"Type\":\"Audio\"}],"
                          "\"TotalRecordCount\":2}"));

    MusicController music(m_client, this);
    music.setActions(m_actions);

    QSignalSpy collected(&music, &MusicController::albumTracksCollected);
    QSignalSpy queueSpy(m_actions, &ItemActions::queueChanged);
    music.collectAlbumTracks(QStringLiteral("al-kob"), QStringLiteral("Kind of Blue"));

    QTRY_COMPARE(collected.count(), 1);
    QCOMPARE(collected.first().at(0).toString(), QStringLiteral("Kind of Blue"));
    QCOMPARE(collected.first().at(1).toStringList(),
             QStringList({QStringLiteral("301001"), QStringLiteral("301002")}));
    // It is not a play verb: nothing was queued and the album page's own model
    // did not move.
    QCOMPARE(queueSpy.count(), 0);
    QCOMPARE(m_player->queue()->rowCount(), 0);
    QCOMPARE(music.tracks()->rowCount(), 0);

    // The same query playAlbum() issues, because it is literally the same
    // expansion: unsorted and non-recursive, so the server's disc-then-track
    // order survives.
    const QString query = m_mock->lastRequestFor(QStringLiteral("GET"), itemsPath()).query;
    QVERIFY2(query.contains(QStringLiteral("ParentId=al-kob")), qPrintable(query));
    QVERIFY2(!query.contains(QStringLiteral("SortBy")), qPrintable(query));

    // An album the server has nothing for reports, rather than raising a picker
    // over an empty list.
    m_mock->addRoute(QStringLiteral("GET"), itemsPath(), 200,
                     QByteArrayLiteral("{\"Items\":[],\"TotalRecordCount\":0}"));
    QSignalSpy failed(&music, &MusicController::actionFailed);
    music.collectAlbumTracks(QStringLiteral("al-empty"), QStringLiteral("Nothing"));
    QTRY_COMPARE(failed.count(), 1);
    QCOMPARE(collected.count(), 1);
}

void ItemActionsQueueTest::newerLeafPlayRetiresEveryAsynchronousQueueBuilder()
{
    const auto directPlay = [this](const QString &id) {
        QVariantMap item;
        item.insert(QStringLiteral("itemId"), id);
        item.insert(QStringLiteral("name"), QStringLiteral("Newest choice"));
        item.insert(QStringLiteral("type"), QStringLiteral("Movie"));
        m_actions->play(item);
        QCOMPARE(m_player->queue()->rowCount(), 1);
        QCOMPARE(m_player->queue()->current().id, id);
    };

    QSignalSpy queueSpy(m_actions, &ItemActions::queueChanged);
    QSignalSpy failedSpy(m_actions, &ItemActions::actionFailed);

    // /Items backs Play All, shuffle, and curated collections.
    m_mock->setRouteDelay(QStringLiteral("GET"), itemsPath(), 180);
    m_actions->playCollection(QStringLiteral("collection-slow"));
    QTRY_VERIFY(!m_mock->lastRequestFor(QStringLiteral("GET"), itemsPath()).method.isEmpty());
    directPlay(QStringLiteral("301003"));
    QTest::qWait(240);
    QCOMPARE(m_player->queue()->current().id, QStringLiteral("301003"));

    m_mock->addRoute(
        QStringLiteral("GET"), QStringLiteral("/Items/301001/InstantMix"), 200,
        QByteArrayLiteral("{\"Items\":[{\"Id\":\"301001\",\"Name\":\"Old mix\","
                          "\"Type\":\"Audio\"}],\"TotalRecordCount\":1}"));
    m_mock->setRouteDelay(QStringLiteral("GET"),
                          QStringLiteral("/Items/301001/InstantMix"), 180);
    m_actions->instantMix(QStringLiteral("301001"));
    QTRY_VERIFY(!m_mock
                     ->lastRequestFor(QStringLiteral("GET"),
                                      QStringLiteral("/Items/301001/InstantMix"))
                     .method.isEmpty());
    directPlay(QStringLiteral("301002"));
    QTest::qWait(240);
    QCOMPARE(m_player->queue()->current().id, QStringLiteral("301002"));

    m_mock->setRouteDelay(QStringLiteral("GET"),
                          QStringLiteral("/Shows/%1/Episodes").arg(kSeriesId), 180);
    m_actions->shuffleSeries(kSeriesId);
    QTRY_VERIFY(!m_mock
                     ->lastRequestFor(QStringLiteral("GET"),
                                      QStringLiteral("/Shows/%1/Episodes").arg(kSeriesId))
                     .method.isEmpty());
    directPlay(QStringLiteral("301001"));
    QTest::qWait(240);
    QCOMPARE(m_player->queue()->current().id, QStringLiteral("301001"));

    // MusicController expands an album before handing it to ItemActions, so it
    // reserves the same global playback intent before starting that fetch.
    MusicController music(m_client, this);
    music.setActions(m_actions);
    const int beforeAlbum = m_mock->requestCount();
    music.playAlbum(QStringLiteral("album-slow"));
    QTRY_VERIFY(m_mock->requestCount() > beforeAlbum);
    directPlay(QStringLiteral("301003"));
    QTest::qWait(240);
    QCOMPARE(m_player->queue()->current().id, QStringLiteral("301003"));

    // Collection root has already completed; cancellation must own the nested
    // request too, not merely make its eventual continuation harmless.
    m_mock->setRouteDelay(QStringLiteral("GET"), itemsPath(), 0);
    m_mock->enqueueRoute(
        QStringLiteral("GET"), itemsPath(), 200,
        pageBody({itemObject(QStringLiteral("series-nested"), QStringLiteral("Series"))}));
    m_mock->addRoute(QStringLiteral("GET"), itemsPath(), 200,
                     pageBody({itemObject(QStringLiteral("402001"), QStringLiteral("Episode"))}));
    m_mock->setRouteDelay(QStringLiteral("GET"), itemsPath(), 180);
    const int beforeCollection = m_mock->requestCount();
    m_actions->playCollection(QStringLiteral("box-nested"));
    QTRY_VERIFY(m_mock->requestCount() >= beforeCollection + 2);
    directPlay(QStringLiteral("301002"));
    QTRY_VERIFY(m_mock->abortedResponseCount(itemsPath()) >= 1);
    QTest::qWait(220);
    QCOMPARE(m_player->queue()->current().id, QStringLiteral("301002"));

    // Stale completions are silent: they neither replace the queue nor toast a
    // failure/success for an intent the user has already superseded.
    QCOMPARE(queueSpy.count(), 0);
    QCOMPARE(failedSpy.count(), 0);
}

void ItemActionsQueueTest::sessionResetRetiresQueueBuildersAndClearsRegisteredModels()
{
    MediaItemModel model;
    MediaItem oldItem;
    oldItem.id = QStringLiteral("old-user-item");
    model.setItems({oldItem});
    m_actions->registerModel(&model);

    m_mock->enqueueRoute(
        QStringLiteral("GET"), itemsPath(), 200,
        pageBody({itemObject(QStringLiteral("series-reset"), QStringLiteral("Series"))}));
    m_mock->addRoute(QStringLiteral("GET"), itemsPath(), 200,
                     pageBody({itemObject(QStringLiteral("402001"), QStringLiteral("Episode"))}));
    m_mock->setRouteDelay(QStringLiteral("GET"), itemsPath(), 180);
    QSignalSpy queueSpy(m_actions, &ItemActions::queueChanged);
    const int beforeWalk = m_mock->requestCount();
    m_actions->playCollection(QStringLiteral("old-user-collection"));
    QTRY_VERIFY(m_mock->requestCount() >= beforeWalk + 2);

    m_actions->resetSessionState();
    QCOMPARE(model.rowCount(), 0);
    QTRY_VERIFY(m_mock->abortedResponseCount(itemsPath()) >= 1);
    QTest::qWait(240);
    QCOMPARE(queueSpy.count(), 0);
    QCOMPARE(m_player->queue()->rowCount(), 0);
}

QTEST_GUILESS_MAIN(ItemActionsQueueTest)
#include "tst_item_actions_queue.moc"
