#include <QJsonDocument>
#include <QJsonObject>
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
const auto kItemId = QStringLiteral("301001");

QString playedPath(const QString &itemId)
{
    return QStringLiteral("/Users/%1/PlayedItems/%2").arg(kUserId, itemId);
}

QString favoritePath(const QString &itemId, const QString &userId = kUserId)
{
    return QStringLiteral("/Users/%1/FavoriteItems/%2").arg(userId, itemId);
}

MediaItem makeItem(const QString &id, const QString &name)
{
    MediaItem item;
    item.id = id;
    item.name = name;
    item.type = QStringLiteral("Episode");
    item.seriesId = QStringLiteral("s-9000");
    item.seriesName = QStringLiteral("Fixture Series");
    item.indexNumber = 4;
    item.parentIndexNumber = 2;
    item.runtimeTicks = 45 * 60 * kTicksPerSecond;
    item.playbackPositionTicks = 120 * kTicksPerSecond; // 2 min in → resumable
    return item;
}

QVariantMap itemMap(const QString &type, const QString &id = QStringLiteral("policy-id"))
{
    return {{QStringLiteral("itemId"), id},
            {QStringLiteral("name"), QStringLiteral("Policy item")},
            {QStringLiteral("type"), type}};
}

QStringList policyVerbs(const QVariantList &policy)
{
    QStringList verbs;
    for (const QVariant &entry : policy) {
        const QString verb = entry.toMap().value(QStringLiteral("verb")).toString();
        if (!verb.isEmpty())
            verbs.append(verb);
    }
    return verbs;
}

QVariantMap descriptorFor(const QVariantList &policy, const QString &verb)
{
    for (const QVariant &entry : policy) {
        const QVariantMap descriptor = entry.toMap();
        if (descriptor.value(QStringLiteral("verb")).toString() == verb)
            return descriptor;
    }
    return {};
}

} // namespace

class ItemActionsTest : public QObject
{
    Q_OBJECT

private slots:
    void containersPlayTheirContents();
    void init();
    void cleanup();

    void togglePlayedUpdatesOptimisticallyAndPatchesModels();
    void failedTogglePlayedRollsBack();
    void failedToggleFavoriteRollsBack();
    void rapidTogglesCoalesceIntoOneRequest();
    void rapidTogglePairConvergesOnTheServer();
    void phoneUpdateThenSiblingTogglesPreserveFreshState();
    void userStateCacheIsBoundedAndReset();
    void pendingUserStateAdmissionIsStrictlyBounded();
    void identityChangeClearsUserState();
    void delayedOldIdentityReplyCannotConsumeNewIdentityRequest();
    void signalsCarryTheRightItemId();
    void playResumesAndPlayFromStartDoesNot();
    void capabilityMatrixSeparatesContainersFromPlayableItems();
    void menuPoliciesOwnOrderFlagsAndTargets();
    void checkedPolicyAndDispatchUseFreshState();
    void musicMenuStateStartsFromTheMap_data();
    void musicMenuStateStartsFromTheMap();
    void failedMusicToggleRollsBackToTheMapState();
    void registeredModelStateBeatsTheMap();
    void liveRecordMovesTheAnswerAndAnnouncesItOnce();
    void liveRecordForAModelRowPatchesItBeforeAnnouncing();
    void liveRecordDuringAToggleKeepsTheIntentAndMovesTheRollback();
    void liveRecordReplacesAStateThisSessionToggled();
    void liveRecordLeavesAbsentFieldsAlone();
    void liveRecordsStayInsideTheBoundedCache();
    void liveRecordPatchesTheQueueWithoutRestartingIt();
    void localToggleReachesTheQueueEntry();
    void navigationVerbsEmitRequests();
    void refreshMetadataReportsThatItIsNotWired();

private:
    int requestCount(const QString &method, const QString &path) const;
    // Leaves the queue holding `items` with the first one current and nothing
    // playing: what the app looks like when the server's record for the item
    // that just stopped comes back.
    void playThenStop(const QList<MediaItem> &items);

    MockEmbyServer *m_mock = nullptr;
    emby::EmbyClient *m_client = nullptr;
    FakePlayerBackend *m_backend = nullptr;
    QTemporaryDir *m_dir = nullptr;
    Settings *m_settings = nullptr;
    PlayerController *m_player = nullptr;
    MediaItemModel *m_model = nullptr;
    ItemActions *m_actions = nullptr;
};

int ItemActionsTest::requestCount(const QString &method, const QString &path) const
{
    int count = 0;
    for (const MockEmbyServer::ReceivedRequest &request : m_mock->requests()) {
        if (request.method == method.toUpper() && request.path == path)
            ++count;
    }
    return count;
}

void ItemActionsTest::playThenStop(const QList<MediaItem> &items)
{
    const QString playbackInfo = QStringLiteral("/Items/%1/PlaybackInfo").arg(items.first().id);
    QVERIFY(m_mock->addRouteFromFile(QStringLiteral("POST"), playbackInfo,
                                     fixturePath(QStringLiteral("playback_info.json"))));
    m_player->playQueueItems(items, 0, false);
    QTRY_COMPARE(requestCount(QStringLiteral("POST"), playbackInfo), 1);
    QTRY_VERIFY(m_player->active());
    m_player->stop();
    QTRY_VERIFY(!m_player->active());
    QCOMPARE(m_player->queue()->rowCount(), int(items.size()));
    QCOMPARE(m_player->queue()->currentIndex(), 0);
}

void ItemActionsTest::init()
{
    m_mock = new MockEmbyServer(this);
    QVERIFY(m_mock->start());

    m_client = new emby::EmbyClient(this);
    m_client->setBaseUrl(m_mock->baseUrl());
    m_client->setDeviceId(QStringLiteral("test-device"));
    m_client->setSession(kToken, kUserId);
    // Playback reporting is not what this suite is about; answer it so the
    // controller does not thrash on 404s while a play verb is under test.
    m_mock->addRoute(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing"), 204, {});
    m_mock->addRoute(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing/Progress"), 204, {});
    m_mock->addRoute(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing/Stopped"), 204, {});

    m_backend = new FakePlayerBackend(this);
    m_dir = new QTemporaryDir;
    QVERIFY(m_dir->isValid());
    m_settings = new Settings(m_dir->filePath(QStringLiteral("settings.ini")), this);
    m_player = new PlayerController(m_client, m_backend, m_settings, this);

    m_model = new MediaItemModel(this);
    m_model->setItems({makeItem(kItemId, QStringLiteral("The One With The Fixture"))});

    m_actions = new ItemActions(m_client, m_player, this);
    m_actions->registerModel(m_model);
}

void ItemActionsTest::cleanup()
{
    delete m_actions;
    delete m_model;
    delete m_player;
    delete m_settings;
    delete m_dir;
    delete m_backend;
    delete m_client;
    delete m_mock;
    m_actions = nullptr;
    m_model = nullptr;
    m_player = nullptr;
    m_settings = nullptr;
    m_dir = nullptr;
    m_backend = nullptr;
    m_client = nullptr;
    m_mock = nullptr;
}

void ItemActionsTest::togglePlayedUpdatesOptimisticallyAndPatchesModels()
{
    m_mock->addRoute(QStringLiteral("POST"), playedPath(kItemId), 204, {});

    QSignalSpy played(m_actions, &ItemActions::playedChanged);
    QSignalSpy committed(m_actions, &ItemActions::playedCommitted);
    QSignalSpy failed(m_actions, &ItemActions::actionFailed);

    m_actions->togglePlayed(m_model->get(0));

    // Optimistic: the signal and the model are already updated, before any
    // event-loop turn could have delivered a reply.
    QCOMPARE(played.count(), 1);
    QCOMPARE(played.first().at(0).toString(), kItemId);
    QCOMPARE(played.first().at(1).toBool(), true);
    QCOMPARE(m_model->get(0).value(QStringLiteral("played")).toBool(), true);
    QVERIFY(m_actions->isPlayed(kItemId));

    QTRY_COMPARE(requestCount(QStringLiteral("POST"), playedPath(kItemId)), 1);
    QTRY_COMPARE(committed.count(), 1);
    QCOMPARE(committed.first().at(0).toString(), kItemId);
    QCOMPARE(committed.first().at(1).toBool(), true);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(played.count(), 1); // a confirmed reply is not a second change
    QCOMPARE(m_model->get(0).value(QStringLiteral("played")).toBool(), true);
}

void ItemActionsTest::failedTogglePlayedRollsBack()
{
    // No route registered → the mock answers 404 → the request fails.
    QSignalSpy played(m_actions, &ItemActions::playedChanged);
    QSignalSpy committed(m_actions, &ItemActions::playedCommitted);
    QSignalSpy failed(m_actions, &ItemActions::actionFailed);

    m_actions->setPlayed(kItemId, true);
    QCOMPARE(played.count(), 1);
    QCOMPARE(played.first().at(1).toBool(), true);
    QCOMPARE(m_model->get(0).value(QStringLiteral("played")).toBool(), true);

    QTRY_COMPARE(failed.count(), 1);
    QVERIFY(!failed.first().at(0).toString().isEmpty());
    QCOMPARE(played.count(), 2);
    QCOMPARE(played.last().at(0).toString(), kItemId);
    QCOMPARE(played.last().at(1).toBool(), false); // rolled back to the truth
    QCOMPARE(m_model->get(0).value(QStringLiteral("played")).toBool(), false);
    QVERIFY(!m_actions->isPlayed(kItemId));
    QCOMPARE(committed.count(), 0);
}

void ItemActionsTest::failedToggleFavoriteRollsBack()
{
    QSignalSpy favorite(m_actions, &ItemActions::favoriteChanged);
    QSignalSpy failed(m_actions, &ItemActions::actionFailed);

    m_actions->toggleFavorite(m_model->get(0));
    QCOMPARE(favorite.count(), 1);
    QCOMPARE(favorite.first().at(1).toBool(), true);
    QCOMPARE(m_model->get(0).value(QStringLiteral("favorite")).toBool(), true);

    QTRY_COMPARE(failed.count(), 1);
    QCOMPARE(favorite.count(), 2);
    QCOMPARE(favorite.last().at(1).toBool(), false);
    QCOMPARE(m_model->get(0).value(QStringLiteral("favorite")).toBool(), false);
    QVERIFY(!m_actions->isFavorite(kItemId));
}

// A held auto-repeating key must not turn into a burst of requests. Three
// toggles landing before any reply collapse into the single request already on
// the wire, because the trailing value equals the one in flight.
void ItemActionsTest::rapidTogglesCoalesceIntoOneRequest()
{
    m_mock->addRoute(QStringLiteral("POST"), playedPath(kItemId), 204, {});
    m_mock->addRoute(QStringLiteral("DELETE"), playedPath(kItemId), 204, {});

    const QVariantMap item = m_model->get(0);
    m_actions->togglePlayed(item); // → true, request starts
    m_actions->togglePlayed(item); // → false, queued
    m_actions->togglePlayed(item); // → true, replaces the queued value

    QVERIFY(m_actions->isPlayed(kItemId));
    QTest::qWait(200);
    QCOMPARE(requestCount(QStringLiteral("POST"), playedPath(kItemId)), 1);
    QCOMPARE(requestCount(QStringLiteral("DELETE"), playedPath(kItemId)), 0);
    QVERIFY(m_actions->isPlayed(kItemId));
    QCOMPARE(m_model->get(0).value(QStringLiteral("played")).toBool(), true);
}

// Two toggles do change the destination, so exactly one follow-up request is
// sent after the first reply — the server ends where the UI already is.
void ItemActionsTest::rapidTogglePairConvergesOnTheServer()
{
    m_mock->addRoute(QStringLiteral("POST"), playedPath(kItemId), 204, {});
    m_mock->addRoute(QStringLiteral("DELETE"), playedPath(kItemId), 204, {});

    const QVariantMap item = m_model->get(0);
    QSignalSpy committed(m_actions, &ItemActions::playedCommitted);
    m_actions->togglePlayed(item); // → true
    m_actions->togglePlayed(item); // → false

    QVERIFY(!m_actions->isPlayed(kItemId));
    QTRY_COMPARE(requestCount(QStringLiteral("DELETE"), playedPath(kItemId)), 1);
    QCOMPARE(requestCount(QStringLiteral("POST"), playedPath(kItemId)), 1);
    QTRY_COMPARE(committed.count(), 1);
    QCOMPARE(committed.first().at(1).toBool(), false);
    QVERIFY(!m_actions->isPlayed(kItemId));
    QCOMPARE(m_model->get(0).value(QStringLiteral("played")).toBool(), false);
}

// UserDataChanged carries played and favorite together. If a phone changes both
// after this process has cached the row, toggling either field here must carry
// the phone's fresh sibling through the optimistic model patch.
void ItemActionsTest::phoneUpdateThenSiblingTogglesPreserveFreshState()
{
    m_mock->addRoute(QStringLiteral("POST"), playedPath(kItemId), 204, {});
    m_mock->addRoute(QStringLiteral("DELETE"), playedPath(kItemId), 204, {});
    m_mock->addRoute(QStringLiteral("DELETE"), favoritePath(kItemId), 204, {});

    // Seed ItemActions' session cache through an ordinary local mutation.
    m_actions->setPlayed(kItemId, true);
    QTRY_COMPARE(requestCount(QStringLiteral("POST"), playedPath(kItemId)), 1);

    // This is the exact patch HomeController/LibraryController apply when the
    // live socket reports a phone-side change.
    m_model->updateUserData(kItemId, true, true);
    QVERIFY(m_actions->isFavorite(kItemId));

    m_actions->togglePlayed(m_model->get(0));
    const QVariantMap afterPlayedToggle = m_model->get(0);
    QVERIFY(!afterPlayedToggle.value(QStringLiteral("played")).toBool());
    QVERIFY(afterPlayedToggle.value(QStringLiteral("favorite")).toBool());
    QTRY_COMPARE(requestCount(QStringLiteral("DELETE"), playedPath(kItemId)), 1);
    QTRY_COMPARE(m_actions->pendingUserStateRequestCountForTests(), 0);

    m_model->updateUserData(kItemId, true, true);
    QVERIFY(m_actions->isPlayed(kItemId));

    m_actions->toggleFavorite(m_model->get(0));
    const QVariantMap afterFavoriteToggle = m_model->get(0);
    QVERIFY(afterFavoriteToggle.value(QStringLiteral("played")).toBool());
    QVERIFY(!afterFavoriteToggle.value(QStringLiteral("favorite")).toBool());
    QTRY_COMPARE(requestCount(QStringLiteral("DELETE"), favoritePath(kItemId)), 1);
}

void ItemActionsTest::userStateCacheIsBoundedAndReset()
{
    m_actions->setUserStateCacheLimitForTests(3);
    const QStringList ids{QStringLiteral("cache-0"), QStringLiteral("cache-1"),
                          QStringLiteral("cache-2"), QStringLiteral("cache-3")};
    for (const QString &id : ids) {
        m_mock->addRoute(QStringLiteral("POST"), favoritePath(id), 204, {});
        m_actions->setFavorite(id, true);
        QTRY_COMPARE(requestCount(QStringLiteral("POST"), favoritePath(id)), 1);
        QTRY_COMPARE(m_actions->pendingUserStateRequestCountForTests(), 0);
    }

    QCOMPARE(m_actions->cachedUserStateCountForTests(), 3);
    QVERIFY(!m_actions->isFavorite(ids.first()));
    QVERIFY(m_actions->isFavorite(ids.last()));

    m_actions->resetSessionState();
    QCOMPARE(m_actions->cachedUserStateCountForTests(), 0);
    QVERIFY(!m_actions->isFavorite(ids.last()));
}

void ItemActionsTest::pendingUserStateAdmissionIsStrictlyBounded()
{
    m_actions->setUserStateCacheLimitForTests(3);
    const QStringList ids{QStringLiteral("pending-0"), QStringLiteral("pending-1"),
                          QStringLiteral("pending-2"), QStringLiteral("pending-rejected")};
    for (const QString &id : ids) {
        m_mock->addRoute(QStringLiteral("POST"), favoritePath(id), 204, {});
        m_mock->setRouteDelay(QStringLiteral("POST"), favoritePath(id), 150);
    }

    QSignalSpy changed(m_actions, &ItemActions::favoriteChanged);
    QSignalSpy failed(m_actions, &ItemActions::actionFailed);
    m_actions->setFavoriteAll(ids, true);

    QCOMPARE(m_actions->cachedUserStateCountForTests(), 3);
    QCOMPARE(m_actions->pendingUserStateRequestCountForTests(), 3);
    QCOMPARE(changed.count(), 3);
    QCOMPARE(failed.count(), 1);
    QVERIFY(!failed.first().at(0).toString().isEmpty());
    QVERIFY(!m_actions->isFavorite(ids.last()));

    for (int index = 0; index < 3; ++index)
        QTRY_COMPARE(requestCount(QStringLiteral("POST"), favoritePath(ids.at(index))), 1);
    QTest::qWait(200);
    QCOMPARE(requestCount(QStringLiteral("POST"), favoritePath(ids.last())), 0);
    QTRY_COMPARE(m_actions->pendingUserStateRequestCountForTests(), 0);
    QCOMPARE(m_actions->cachedUserStateCountForTests(), 3);
}

void ItemActionsTest::identityChangeClearsUserState()
{
    m_mock->addRoute(QStringLiteral("POST"), favoritePath(kItemId), 204, {});
    m_actions->setFavorite(kItemId, true);
    QVERIFY(m_actions->isFavorite(kItemId));
    QCOMPARE(m_model->rowCount(), 1);

    m_client->setSession(QStringLiteral("second-token"), QStringLiteral("second-user"));

    QCOMPARE(m_actions->cachedUserStateCountForTests(), 0);
    QCOMPARE(m_model->rowCount(), 0);
    QVERIFY(!m_actions->isFavorite(kItemId));
}

void ItemActionsTest::delayedOldIdentityReplyCannotConsumeNewIdentityRequest()
{
    constexpr int oldReplyDelayMs = 500;
    const auto secondUser = QStringLiteral("second-user");
    m_mock->addRoute(QStringLiteral("POST"), favoritePath(kItemId), 204, {});
    m_mock->setRouteDelay(QStringLiteral("POST"), favoritePath(kItemId), oldReplyDelayMs);

    m_actions->setFavorite(kItemId, true);
    QTRY_COMPARE(requestCount(QStringLiteral("POST"), favoritePath(kItemId)), 1);
    QCOMPARE(m_actions->pendingUserStateRequestCountForTests(), 1);

    m_client->setSession(QStringLiteral("second-token"), secondUser);
    m_model->setItems({makeItem(kItemId, QStringLiteral("New identity item"))});
    QSignalSpy failedAfterBoundary(m_actions, &ItemActions::actionFailed);

    const QString secondPath = favoritePath(kItemId, secondUser);
    m_mock->addRoute(QStringLiteral("POST"), secondPath, 204, {});
    m_mock->setRouteDelay(QStringLiteral("POST"), secondPath, 150);
    m_actions->setFavorite(kItemId, true);
    QCOMPARE(m_actions->pendingUserStateRequestCountForTests(), 1);

    QTRY_COMPARE(requestCount(QStringLiteral("POST"), secondPath), 1);
    QTRY_COMPARE(m_actions->pendingUserStateRequestCountForTests(), 0);
    QVERIFY(m_actions->isFavorite(kItemId));
    QVERIFY(m_model->get(0).value(QStringLiteral("favorite")).toBool());
    QCOMPARE(failedAfterBoundary.count(), 0);
}

void ItemActionsTest::signalsCarryTheRightItemId()
{
    const auto otherId = QStringLiteral("777");
    m_model->setItems(
        {makeItem(kItemId, QStringLiteral("First")), makeItem(otherId, QStringLiteral("Second"))});
    m_mock->addRoute(QStringLiteral("POST"), favoritePath(otherId), 204, {});

    QSignalSpy favorite(m_actions, &ItemActions::favoriteChanged);
    m_actions->setFavorite(otherId, true);

    QCOMPARE(favorite.count(), 1);
    QCOMPARE(favorite.first().at(0).toString(), otherId);
    // Only the addressed row moved.
    QCOMPARE(m_model->get(1).value(QStringLiteral("favorite")).toBool(), true);
    QCOMPARE(m_model->get(0).value(QStringLiteral("favorite")).toBool(), false);
    QVERIFY(!m_actions->isFavorite(kItemId));

    QTRY_COMPARE(requestCount(QStringLiteral("POST"), favoritePath(otherId)), 1);
    QCOMPARE(requestCount(QStringLiteral("POST"), favoritePath(kItemId)), 0);
}

// play() resumes a resumable item; playFromStart() never does. The proof is the
// StartTimeTicks the client puts in the PlaybackInfo request.
void ItemActionsTest::playResumesAndPlayFromStartDoesNot()
{
    QVERIFY(m_mock->addRouteFromFile(QStringLiteral("POST"),
                                     QStringLiteral("/Items/%1/PlaybackInfo").arg(kItemId),
                                     fixturePath(QStringLiteral("playback_info.json"))));

    const QVariantMap item = m_model->get(0);
    QCOMPARE(item.value(QStringLiteral("resumable")).toBool(), true);

    m_actions->play(item);
    QCOMPARE(m_player->title(), item.value(QStringLiteral("label")).toString());
    QTRY_COMPARE(
        requestCount(QStringLiteral("POST"), QStringLiteral("/Items/%1/PlaybackInfo").arg(kItemId)),
        1);
    QJsonObject body =
        QJsonDocument::fromJson(
            m_mock
                ->lastRequestFor(QStringLiteral("POST"),
                                 QStringLiteral("/Items/%1/PlaybackInfo").arg(kItemId))
                .body)
            .object();
    QCOMPARE(qint64(body.value(QLatin1String("StartTimeTicks")).toDouble()), 120 * kTicksPerSecond);

    // From the start: no resume point on the wire at all.
    m_actions->playFromStart(item);
    QTRY_COMPARE(
        requestCount(QStringLiteral("POST"), QStringLiteral("/Items/%1/PlaybackInfo").arg(kItemId)),
        2);
    body = QJsonDocument::fromJson(
               m_mock
                   ->lastRequestFor(QStringLiteral("POST"),
                                    QStringLiteral("/Items/%1/PlaybackInfo").arg(kItemId))
                   .body)
               .object();
    QVERIFY(!body.contains(QLatin1String("StartTimeTicks")));

    // A bare item id resolves through the registered models.
    m_actions->play(kItemId);
    QTRY_COMPARE(
        requestCount(QStringLiteral("POST"), QStringLiteral("/Items/%1/PlaybackInfo").arg(kItemId)),
        3);
    m_player->stop();
}

void ItemActionsTest::capabilityMatrixSeparatesContainersFromPlayableItems()
{
    const auto capabilities = [this](const QString &type) {
        return m_actions->itemCapabilities(itemMap(type));
    };

    for (const QString &type :
         {QStringLiteral("Movie"), QStringLiteral("Episode"), QStringLiteral("Audio"),
          QStringLiteral("AudioBook"), QStringLiteral("Video"), QStringLiteral("MusicVideo")}) {
        const QVariantMap caps = capabilities(type);
        QVERIFY2(caps.value(QStringLiteral("play")).toBool(), qPrintable(type));
        QVERIFY2(caps.value(QStringLiteral("queue")).toBool(), qPrintable(type));
        QVERIFY2(caps.value(QStringLiteral("markPlayed")).toBool(), qPrintable(type));
    }

    for (const QString &type : {QStringLiteral("Series"), QStringLiteral("Season"),
                                QStringLiteral("BoxSet"), QStringLiteral("MusicAlbum")}) {
        const QVariantMap caps = capabilities(type);
        QVERIFY2(caps.value(QStringLiteral("expandable")).toBool(), qPrintable(type));
        QVERIFY2(caps.value(QStringLiteral("play")).toBool(), qPrintable(type));
        QVERIFY2(caps.value(QStringLiteral("shuffle")).toBool(), qPrintable(type));
        QVERIFY2(!caps.value(QStringLiteral("queue")).toBool(), qPrintable(type));
    }

    const QVariantMap artist = capabilities(QStringLiteral("MusicArtist"));
    QVERIFY(!artist.value(QStringLiteral("play")).toBool());
    QVERIFY(!artist.value(QStringLiteral("queue")).toBool());
    QVERIFY(!artist.value(QStringLiteral("markPlayed")).toBool());
    QVERIFY(artist.value(QStringLiteral("instantMix")).toBool());

    const QVariantMap playlist = capabilities(QStringLiteral("Playlist"));
    QVERIFY(!playlist.value(QStringLiteral("play")).toBool());
    QVERIFY(!playlist.value(QStringLiteral("queue")).toBool());
    QVERIFY(!playlist.value(QStringLiteral("markPlayed")).toBool());
    QVERIFY(playlist.value(QStringLiteral("favorite")).toBool());

    // An ABSENT type is not a claim that this is a folder. The remote-control
    // lane addresses items by id alone, and the queue row fills in when the
    // item's own details arrive. A type the server DID state and this build
    // does not recognise ("FutureServerType" below) stays refused.
    const QVariantMap idOnly = m_actions->itemCapabilities(
        QVariantMap{{QStringLiteral("itemId"), QStringLiteral("id-only")}});
    QVERIFY(idOnly.value(QStringLiteral("play")).toBool());
    QVERIFY(idOnly.value(QStringLiteral("queue")).toBool());
    QVERIFY(!idOnly.value(QStringLiteral("expandable")).toBool());
    QVERIFY(!idOnly.value(QStringLiteral("shuffle")).toBool());

    for (const QString &type :
         {QStringLiteral("Folder"), QStringLiteral("CollectionFolder"), QStringLiteral("Genre"),
          QStringLiteral("MusicGenre"), QStringLiteral("FutureServerType")}) {
        const QVariantMap caps = capabilities(type);
        QVERIFY2(!caps.value(QStringLiteral("play")).toBool(), qPrintable(type));
        QVERIFY2(!caps.value(QStringLiteral("queue")).toBool(), qPrintable(type));
        QVERIFY2(!caps.value(QStringLiteral("markPlayed")).toBool(), qPrintable(type));
        QVERIFY2(caps.value(QStringLiteral("details")).toBool(), qPrintable(type));
    }
}

void ItemActionsTest::menuPoliciesOwnOrderFlagsAndTargets()
{
    QVariantMap episode = itemMap(QStringLiteral("Episode"));
    episode.insert(QStringLiteral("resumable"), true);
    episode.insert(QStringLiteral("seriesId"), QStringLiteral("series-id"));
    episode.insert(QStringLiteral("seriesName"), QStringLiteral("Series name"));
    episode.insert(QStringLiteral("playlistItemId"), QStringLiteral("entry-2"));
    QVariantList policy = m_actions->itemMenuPolicy(episode, true, true, true, false, {});
    QCOMPARE(policyVerbs(policy),
             QStringList({QStringLiteral("resume"), QStringLiteral("playFromStart"),
                          QStringLiteral("playNext"), QStringLiteral("addToQueue"),
                          QStringLiteral("played"), QStringLiteral("favorite"),
                          QStringLiteral("addToPlaylist"), QStringLiteral("removeFromPlaylist"),
                          QStringLiteral("series"), QStringLiteral("details"),
                          QStringLiteral("refresh")}));
    const QVariantMap removal = descriptorFor(policy, QStringLiteral("removeFromPlaylist"));
    QVERIFY(removal.value(QStringLiteral("destructive")).toBool());
    QCOMPARE(removal.value(QStringLiteral("target"))
                 .toMap()
                 .value(QStringLiteral("playlistItemId"))
                 .toString(),
             QStringLiteral("entry-2"));
    const QVariantMap series = descriptorFor(policy, QStringLiteral("series"));
    QCOMPARE(series.value(QStringLiteral("routeKind")).toString(), QStringLiteral("series"));
    QCOMPARE(
        series.value(QStringLiteral("target")).toMap().value(QStringLiteral("itemId")).toString(),
        QStringLiteral("series-id"));

    QVariantMap album = itemMap(QStringLiteral("MusicAlbum"), QStringLiteral("album-id"));
    album.insert(QStringLiteral("artists"),
                 QStringList{QStringLiteral("Guest"), QStringLiteral("Album Artist")});
    album.insert(QStringLiteral("artistIds"),
                 QStringList{QStringLiteral("guest-id"), QStringLiteral("artist-id")});
    album.insert(QStringLiteral("albumArtist"), QStringLiteral("Album Artist"));
    policy =
        m_actions->itemMenuPolicy(album, true, true, false, true, QStringLiteral("musicBrowse"));
    QCOMPARE(policyVerbs(policy),
             QStringList({QStringLiteral("play"), QStringLiteral("shuffle"),
                          QStringLiteral("instantMix"), QStringLiteral("details"),
                          QStringLiteral("addToPlaylist"), QStringLiteral("favorite")}));
    QCOMPARE(descriptorFor(policy, QStringLiteral("details"))
                 .value(QStringLiteral("routeKind"))
                 .toString(),
             QStringLiteral("album"));

    policy = m_actions->itemMenuPolicy(itemMap(QStringLiteral("MusicArtist")), true, true, false,
                                       false, {});
    QCOMPARE(policyVerbs(policy),
             QStringList({QStringLiteral("favorite"), QStringLiteral("refresh")}));
    policy = m_actions->itemMenuPolicy(itemMap(QStringLiteral("MusicArtist")), true, true, false,
                                       true, {});
    QCOMPARE(policyVerbs(policy),
             QStringList({QStringLiteral("favorite"), QStringLiteral("refresh")}));
    policy = m_actions->itemMenuPolicy(itemMap(QStringLiteral("MusicArtist")), true, true, false,
                                       true, QStringLiteral("musicBrowse"));
    QCOMPARE(policyVerbs(policy),
             QStringList({QStringLiteral("instantMix"), QStringLiteral("details"),
                          QStringLiteral("favorite")}));
    policy = m_actions->itemMenuPolicy(itemMap(QStringLiteral("Playlist")), true, true, false, true,
                                       QStringLiteral("musicBrowse"));
    QCOMPARE(policyVerbs(policy),
             QStringList({QStringLiteral("details"), QStringLiteral("favorite")}));

    QVariantMap playlistEntry = itemMap(QStringLiteral("Playlist"));
    playlistEntry.insert(QStringLiteral("playlistItemId"), QStringLiteral("nested-entry"));
    policy = m_actions->itemMenuPolicy(playlistEntry, true, true, true, true, {});
    QCOMPARE(policyVerbs(policy),
             QStringList({QStringLiteral("details"), QStringLiteral("favorite"),
                          QStringLiteral("removeFromPlaylist")}));
    QCOMPARE(descriptorFor(policy, QStringLiteral("removeFromPlaylist"))
                 .value(QStringLiteral("target"))
                 .toMap()
                 .value(QStringLiteral("playlistItemId"))
                 .toString(),
             QStringLiteral("nested-entry"));
}

void ItemActionsTest::checkedPolicyAndDispatchUseFreshState()
{
    m_mock->addRoute(QStringLiteral("POST"), favoritePath(kItemId), 204, {});
    m_mock->addRoute(QStringLiteral("DELETE"), favoritePath(kItemId), 204, {});
    m_mock->setRouteDelay(QStringLiteral("POST"), favoritePath(kItemId), 100);

    const QVariantMap item = m_model->get(0);
    m_actions->performItemVerb(QStringLiteral("favorite"), item);
    QVERIFY(m_actions->isFavorite(kItemId));
    QVariantMap descriptor = descriptorFor(
        m_actions->itemMenuPolicy(item, true, false, false, false, {}), QStringLiteral("favorite"));
    QVERIFY(descriptor.value(QStringLiteral("checked")).toBool());

    // Dispatch re-reads the optimistic state. It must not invert the stale map
    // or the descriptor snapshot captured before the first request.
    m_actions->performItemVerb(QStringLiteral("favorite"), item);
    QVERIFY(!m_actions->isFavorite(kItemId));
    descriptor = descriptorFor(m_actions->itemMenuPolicy(item, true, false, false, false, {}),
                               QStringLiteral("favorite"));
    QVERIFY(!descriptor.value(QStringLiteral("checked")).toBool());
    QTRY_COMPARE(requestCount(QStringLiteral("DELETE"), favoritePath(kItemId)), 1);
}

// The music models are not MediaItemModels and are never registered, so for an
// album, an artist or a track the map a delegate hands over is the only place
// the server's answer lives. The menu used to read "not a favourite" for every
// one of them and the verb then POSTed a favourite that was already set, so a
// record could not be un-favourited from its own menu.
void ItemActionsTest::musicMenuStateStartsFromTheMap_data()
{
    QTest::addColumn<QString>("type");
    QTest::addColumn<QString>("profile");
    QTest::addColumn<QString>("favoriteKey");

    // AlbumGridModel exposes both spellings; TrackListModel only the British.
    QTest::newRow("album, favorite") << QStringLiteral("MusicAlbum") << QStringLiteral("musicBrowse")
                                     << QStringLiteral("favorite");
    QTest::newRow("album, favourite") << QStringLiteral("MusicAlbum")
                                      << QStringLiteral("musicBrowse") << QStringLiteral("favourite");
    QTest::newRow("track, favourite") << QStringLiteral("Audio") << QString()
                                      << QStringLiteral("favourite");
    QTest::newRow("artist, favorite") << QStringLiteral("MusicArtist")
                                      << QStringLiteral("musicBrowse") << QStringLiteral("favorite");
}

void ItemActionsTest::musicMenuStateStartsFromTheMap()
{
    QFETCH(QString, type);
    QFETCH(QString, profile);
    QFETCH(QString, favoriteKey);

    const QString id = QStringLiteral("music-only");
    QVariantMap item = itemMap(type, id);
    item.insert(favoriteKey, true);
    item.insert(QStringLiteral("played"), true);
    QVERIFY(m_actions->itemFor(id).isEmpty()); // no registered model holds it

    m_mock->addRoute(QStringLiteral("POST"), favoritePath(id), 204, {});
    m_mock->addRoute(QStringLiteral("DELETE"), favoritePath(id), 204, {});

    const auto menu = [&] { return m_actions->itemMenuPolicy(item, true, false, false, false, profile); };
    QVariantMap descriptor = descriptorFor(menu(), QStringLiteral("favorite"));
    QCOMPARE(descriptor.value(QStringLiteral("presentation")).toString(),
             QStringLiteral("removeFavorite"));
    QVERIFY(descriptor.value(QStringLiteral("checked")).toBool());
    const bool hasPlayed = type == QLatin1String("Audio");
    if (hasPlayed) {
        QCOMPARE(descriptorFor(menu(), QStringLiteral("played"))
                     .value(QStringLiteral("presentation"))
                     .toString(),
                 QStringLiteral("markUnplayed"));
    }

    QSignalSpy favorite(m_actions, &ItemActions::favoriteChanged);
    m_actions->performItemVerb(QStringLiteral("favorite"), item);
    QCOMPARE(favorite.count(), 1);
    QCOMPARE(favorite.first().at(0).toString(), id);
    QCOMPARE(favorite.first().at(1).toBool(), false);
    QVERIFY(!m_actions->isFavorite(id));
    QTRY_COMPARE(requestCount(QStringLiteral("DELETE"), favoritePath(id)), 1);
    QCOMPARE(requestCount(QStringLiteral("POST"), favoritePath(id)), 0);

    // The menu is reopened over the SAME map, which still says "favourite":
    // what was just done wins, during the request and after it settled.
    QTRY_COMPARE(m_actions->pendingUserStateRequestCountForTests(), 0);
    descriptor = descriptorFor(menu(), QStringLiteral("favorite"));
    QCOMPARE(descriptor.value(QStringLiteral("presentation")).toString(),
             QStringLiteral("addFavorite"));
    QVERIFY(!descriptor.value(QStringLiteral("checked")).toBool());
    // ...and toggling one field did not reset the other to a default.
    if (hasPlayed) {
        QCOMPARE(descriptorFor(menu(), QStringLiteral("played"))
                     .value(QStringLiteral("presentation"))
                     .toString(),
                 QStringLiteral("markUnplayed"));
        QVERIFY(m_actions->isPlayed(id));
    }

    // And back again, still over the stale map.
    m_actions->performItemVerb(QStringLiteral("favorite"), item);
    QVERIFY(m_actions->isFavorite(id));
    QTRY_COMPARE(requestCount(QStringLiteral("POST"), favoritePath(id)), 1);
}

// The rollback baseline is the map's state too: a failed un-favourite puts the
// item back to "favourite", not to the `false` an unknown item defaults to.
void ItemActionsTest::failedMusicToggleRollsBackToTheMapState()
{
    const QString id = QStringLiteral("music-only");
    QVariantMap item = itemMap(QStringLiteral("Audio"), id);
    item.insert(QStringLiteral("favourite"), true);
    item.insert(QStringLiteral("played"), true);

    // No routes: both requests fail with the mock's 404.
    QSignalSpy favorite(m_actions, &ItemActions::favoriteChanged);
    QSignalSpy played(m_actions, &ItemActions::playedChanged);
    QSignalSpy failed(m_actions, &ItemActions::actionFailed);

    m_actions->performItemVerb(QStringLiteral("favorite"), item);
    QCOMPARE(favorite.count(), 1);
    QCOMPARE(favorite.first().at(1).toBool(), false);
    QTRY_COMPARE(failed.count(), 1);
    QCOMPARE(favorite.count(), 2);
    QCOMPARE(favorite.last().at(1).toBool(), true);
    QVERIFY(m_actions->isFavorite(id));
    QCOMPARE(descriptorFor(m_actions->itemMenuPolicy(item, true, false, false, false, {}),
                           QStringLiteral("favorite"))
                 .value(QStringLiteral("presentation"))
                 .toString(),
             QStringLiteral("removeFavorite"));

    m_actions->performItemVerb(QStringLiteral("played"), item);
    QCOMPARE(played.count(), 1);
    QCOMPARE(played.first().at(1).toBool(), false);
    QTRY_COMPARE(failed.count(), 2);
    QCOMPARE(played.count(), 2);
    QCOMPARE(played.last().at(1).toBool(), true);
    QVERIFY(m_actions->isPlayed(id));
}

// The map is the fallback, not the answer. A registered model is patched by
// every toggle and every live update, so where one holds the item it is newer
// than whatever snapshot a delegate is still carrying.
void ItemActionsTest::registeredModelStateBeatsTheMap()
{
    m_mock->addRoute(QStringLiteral("POST"), favoritePath(kItemId), 204, {});

    QVariantMap stale = m_model->get(0);
    stale.insert(QStringLiteral("favorite"), true);
    stale.insert(QStringLiteral("played"), true);
    QVERIFY(!m_model->get(0).value(QStringLiteral("favorite")).toBool());

    const QVariantList policy = m_actions->itemMenuPolicy(stale, true, false, false, false, {});
    QCOMPARE(descriptorFor(policy, QStringLiteral("favorite"))
                 .value(QStringLiteral("presentation"))
                 .toString(),
             QStringLiteral("addFavorite"));
    QCOMPARE(descriptorFor(policy, QStringLiteral("played"))
                 .value(QStringLiteral("presentation"))
                 .toString(),
             QStringLiteral("markWatched"));

    m_actions->performItemVerb(QStringLiteral("favorite"), stale);
    QVERIFY(m_actions->isFavorite(kItemId));
    QTRY_COMPARE(requestCount(QStringLiteral("POST"), favoritePath(kItemId)), 1);
}

// AUDIT §3.3. The details and series pages read Actions.isPlayed/isFavorite
// and listen to playedChanged/favoriteChanged; a record from another client has
// to arrive there like a toggle made here — and cost no request.
void ItemActionsTest::liveRecordMovesTheAnswerAndAnnouncesItOnce()
{
    const QString id = QStringLiteral("in-no-model");
    QSignalSpy played(m_actions, &ItemActions::playedChanged);
    QSignalSpy favorite(m_actions, &ItemActions::favoriteChanged);
    QSignalSpy committed(m_actions, &ItemActions::playedCommitted);
    QVERIFY(!m_actions->isPlayed(id));
    QVERIFY(!m_actions->isFavorite(id));

    m_actions->applyExternalUserData(id, true, true);
    QVERIFY(m_actions->isPlayed(id));
    QVERIFY(m_actions->isFavorite(id));
    QCOMPARE(played.count(), 1);
    QCOMPARE(played.first().at(0).toString(), id);
    QCOMPARE(played.first().at(1).toBool(), true);
    QCOMPARE(favorite.count(), 1);
    QCOMPARE(favorite.first().at(0).toString(), id);
    QCOMPARE(favorite.first().at(1).toBool(), true);

    // The same record again: nothing changed, nothing is said.
    m_actions->applyExternalUserData(id, true, true);
    QCOMPARE(played.count(), 1);
    QCOMPARE(favorite.count(), 1);

    // One field moves, one signal.
    m_actions->applyExternalUserData(id, false, true);
    QVERIFY(!m_actions->isPlayed(id));
    QCOMPARE(played.count(), 2);
    QCOMPARE(played.last().at(1).toBool(), false);
    QCOMPARE(favorite.count(), 1);

    // An empty id is nobody's record.
    m_actions->applyExternalUserData(QString(), true, true);
    QCOMPARE(played.count(), 2);

    // Nothing was written back, and nothing claims to have been committed.
    QTest::qWait(50);
    QCOMPARE(m_actions->pendingUserStateRequestCountForTests(), 0);
    QCOMPARE(m_mock->requests().size(), 0);
    QCOMPARE(committed.count(), 0);
}

// The signal's contract for a toggle is that the registered rows already hold
// the new state when it fires; a page that re-reads a row from its handler
// depends on it. A live record keeps that contract, position included.
void ItemActionsTest::liveRecordForAModelRowPatchesItBeforeAnnouncing()
{
    int announced = 0;
    connect(m_actions, &ItemActions::playedChanged, this, [&](const QString &, bool played) {
        ++announced;
        QCOMPARE(played, true);
        QCOMPARE(m_model->items().first().played, true);
        QCOMPARE(m_model->items().first().playbackPositionTicks, 0);
    });
    QSignalSpy favorite(m_actions, &ItemActions::favoriteChanged);

    m_actions->applyUserDataPatches({QVariantMap{{QStringLiteral("itemId"), kItemId},
                                                 {QStringLiteral("played"), true},
                                                 {QStringLiteral("favorite"), false},
                                                 {QStringLiteral("positionTicks"), 0},
                                                 {QStringLiteral("playCount"), 2}}});
    QCOMPARE(announced, 1);
    QCOMPARE(favorite.count(), 0);
    QCOMPARE(m_model->items().first().playCount, 2);
    QVERIFY(m_actions->isPlayed(kItemId));
    // The row is the memory for an item a registered model holds.
    QCOMPARE(m_actions->cachedUserStateCountForTests(), 0);

    // A controller's own binding then patches the same row from the same
    // record (Application.cpp connects ItemActions first). That is a no-op.
    m_model->updateUserData(kItemId, true, false, 0, 2);
    QCOMPARE(announced, 1);
    QCOMPARE(favorite.count(), 0);
}

void ItemActionsTest::liveRecordDuringAToggleKeepsTheIntentAndMovesTheRollback()
{
    playThenStop({m_model->items().first()});
    if (QTest::currentTestFailed())
        return;
    PlayQueue *queue = m_player->queue();
    // No user-data routes: every toggle below fails with the mock's 404, later.
    QSignalSpy played(m_actions, &ItemActions::playedChanged);
    QSignalSpy favorite(m_actions, &ItemActions::favoriteChanged);
    QSignalSpy failed(m_actions, &ItemActions::actionFailed);

    m_actions->setFavorite(kItemId, true);
    QCOMPARE(favorite.count(), 1);
    QCOMPARE(m_actions->pendingUserStateRequestCountForTests(), 1);

    // The server's record crosses the request, still saying "not a favourite"
    // — and "watched", which nothing here is in the middle of changing.
    m_actions->applyExternalUserData(kItemId, true, false);
    QCOMPARE(favorite.count(), 1); // no flicker back to false
    QVERIFY(m_actions->isFavorite(kItemId));
    QCOMPARE(m_model->items().first().favorite, true);
    QCOMPARE(queue->current().favorite, true);
    QCOMPARE(played.count(), 1); // the settled field does follow
    QVERIFY(m_actions->isPlayed(kItemId));
    QCOMPARE(queue->current().played, true);

    // Another client favourites it too. Still nothing to announce: the user
    // already sees a favourite. But that is now what the server holds.
    m_actions->applyExternalUserData(kItemId, true, true);
    QCOMPARE(favorite.count(), 1);

    // The request fails. Rolling back to the value from before the toggle
    // (false) would now contradict the server.
    QTRY_COMPARE(failed.count(), 1);
    QVERIFY(m_actions->isFavorite(kItemId));
    QCOMPARE(favorite.last().at(1).toBool(), true);
    QCOMPARE(m_model->items().first().favorite, true);
    QCOMPARE(queue->current().favorite, true);

    // The same for the watched state, the other way round.
    m_actions->setPlayed(kItemId, false);
    QCOMPARE(played.count(), 2);
    m_actions->applyExternalUserData(kItemId, true, true); // stale: still watched
    QCOMPARE(played.count(), 2);
    QVERIFY(!m_actions->isPlayed(kItemId));
    QCOMPARE(queue->current().played, false);
    m_actions->applyExternalUserData(kItemId, false, true); // someone else unwatched it
    QCOMPARE(played.count(), 2);
    QTRY_COMPARE(failed.count(), 2);
    QVERIFY(!m_actions->isPlayed(kItemId));
    QCOMPARE(played.last().at(1).toBool(), false);
}

// A music item is in no registered model, so once it has been toggled here its
// state lives in the cache and beats the item map from then on. Without a live
// record reaching that cache, a change made on another client left its context
// menu one click stale.
void ItemActionsTest::liveRecordReplacesAStateThisSessionToggled()
{
    const QString id = QStringLiteral("music-only");
    m_mock->addRoute(QStringLiteral("POST"), favoritePath(id), 204, {});
    QVariantMap item = itemMap(QStringLiteral("Audio"), id);
    item.insert(QStringLiteral("favourite"), false);

    m_actions->performItemVerb(QStringLiteral("favorite"), item);
    QTRY_COMPARE(requestCount(QStringLiteral("POST"), favoritePath(id)), 1);
    QTRY_COMPARE(m_actions->pendingUserStateRequestCountForTests(), 0);
    QVERIFY(m_actions->isFavorite(id));

    // Unfavourited from a phone. The row's own map follows through the music
    // relay; the menu has to follow as well.
    QSignalSpy favorite(m_actions, &ItemActions::favoriteChanged);
    m_actions->applyExternalUserData(id, false, false);
    QCOMPARE(favorite.count(), 1);
    QCOMPARE(favorite.first().at(1).toBool(), false);
    QCOMPARE(descriptorFor(m_actions->itemMenuPolicy(item, true, false, false, false, {}),
                           QStringLiteral("favorite"))
                 .value(QStringLiteral("presentation"))
                 .toString(),
             QStringLiteral("addFavorite"));

    // And the next click asks for the right thing: favourite it, not remove it.
    m_actions->performItemVerb(QStringLiteral("favorite"), item);
    QTRY_COMPARE(requestCount(QStringLiteral("POST"), favoritePath(id)), 2);
    QCOMPARE(requestCount(QStringLiteral("DELETE"), favoritePath(id)), 0);
}

void ItemActionsTest::liveRecordLeavesAbsentFieldsAlone()
{
    const QString id = QStringLiteral("in-no-model");
    m_actions->applyExternalUserData(id, true, true);
    QSignalSpy played(m_actions, &ItemActions::playedChanged);
    QSignalSpy favorite(m_actions, &ItemActions::favoriteChanged);

    // A patch that states only the favourite says nothing about "watched".
    m_actions->applyUserDataPatches(
        {QVariantMap{{QStringLiteral("itemId"), id}, {QStringLiteral("favorite"), false}}});
    QCOMPARE(played.count(), 0);
    QVERIFY(m_actions->isPlayed(id));
    QCOMPARE(favorite.count(), 1);
    QVERIFY(!m_actions->isFavorite(id));

    // Nor does one that states nothing at all, or carries no id.
    m_actions->applyUserDataPatches({QVariantMap{{QStringLiteral("itemId"), id}},
                                     QVariantMap{{QStringLiteral("played"), false}},
                                     QVariant(QStringLiteral("not a map"))});
    QCOMPARE(played.count(), 0);
    QCOMPARE(favorite.count(), 1);
    QVERIFY(m_actions->isPlayed(id));

    // The model row keeps its resume point when the record carries none.
    const qint64 position = m_model->items().first().playbackPositionTicks;
    QVERIFY(position > 0);
    m_actions->applyUserDataPatches(
        {QVariantMap{{QStringLiteral("itemId"), kItemId}, {QStringLiteral("favorite"), true}}});
    QCOMPARE(m_model->items().first().favorite, true);
    QCOMPARE(m_model->items().first().played, false);
    QCOMPARE(m_model->items().first().playbackPositionTicks, position);
}

// The server mentions far more items in a session than this one will ever
// show. A record takes a slot only when it changes the answer for an item
// nothing else can answer for, and those slots are the same bounded,
// oldest-settled-first table the toggles use.
void ItemActionsTest::liveRecordsStayInsideTheBoundedCache()
{
    m_actions->setUserStateCacheLimitForTests(4);

    // Changes nothing we would have answered: no slot.
    for (int i = 0; i < 50; ++i)
        m_actions->applyExternalUserData(QStringLiteral("quiet-%1").arg(i), false, false);
    QCOMPARE(m_actions->cachedUserStateCountForTests(), 0);

    QSignalSpy played(m_actions, &ItemActions::playedChanged);
    for (int i = 0; i < 50; ++i)
        m_actions->applyExternalUserData(QStringLiteral("loud-%1").arg(i), true, false);
    QCOMPARE(played.count(), 50);
    QCOMPARE(m_actions->cachedUserStateCountForTests(), 4);
    QVERIFY(m_actions->isPlayed(QStringLiteral("loud-49")));
    QVERIFY(!m_actions->isPlayed(QStringLiteral("loud-0"))); // evicted, oldest first
}

// The record that most reliably arrives for the queue's current entry is the
// server's own, right after playback stops. The queue is intact and nothing is
// playing; enrichEntry() there would announce the cursor and start it again.
void ItemActionsTest::liveRecordPatchesTheQueueWithoutRestartingIt()
{
    const QString playbackInfo = QStringLiteral("/Items/%1/PlaybackInfo").arg(kItemId);
    playThenStop({m_model->items().first(), m_model->items().first()});
    if (QTest::currentTestFailed())
        return;
    PlayQueue *queue = m_player->queue();

    QSignalSpy cursor(queue, &PlayQueue::currentItemChanged);
    QSignalSpy queueChanged(queue, &PlayQueue::queueChanged);
    m_actions->applyUserDataPatches({QVariantMap{{QStringLiteral("itemId"), kItemId},
                                                 {QStringLiteral("played"), true},
                                                 {QStringLiteral("favorite"), true},
                                                 {QStringLiteral("positionTicks"), 0},
                                                 {QStringLiteral("playCount"), 1}}});

    // Both entries holding the item, the one under the cursor included.
    for (int row = 0; row < 2; ++row) {
        const QVariantMap entry = queue->itemAt(row);
        QCOMPARE(entry.value(QStringLiteral("played")).toBool(), true);
        QCOMPARE(entry.value(QStringLiteral("favorite")).toBool(), true);
        QCOMPARE(entry.value(QStringLiteral("positionMs")).toLongLong(), 0);
        QCOMPARE(entry.value(QStringLiteral("playCount")).toInt(), 1);
    }
    QCOMPARE(queueChanged.count(), 1);
    QCOMPARE(cursor.count(), 0);

    // And playback stays stopped: no new session, no new ticket.
    QTest::qWait(100);
    QVERIFY(!m_player->active());
    QCOMPARE(requestCount(QStringLiteral("POST"), playbackInfo), 1);
}

// The queue panel and the now-playing surfaces read the queue entry, which no
// registered model stands in for. Only the field that was toggled is written.
void ItemActionsTest::localToggleReachesTheQueueEntry()
{
    const QString id = QStringLiteral("queued-track");
    m_mock->addRoute(QStringLiteral("POST"), favoritePath(id), 204, {});
    MediaItem track = makeItem(id, QStringLiteral("Queued"));
    track.played = true;
    playThenStop({track});
    if (QTest::currentTestFailed())
        return;
    PlayQueue *queue = m_player->queue();

    QSignalSpy cursor(queue, &PlayQueue::currentItemChanged);
    m_actions->setFavorite(id, true);
    QCOMPARE(queue->current().favorite, true);
    QCOMPARE(queue->current().played, true); // not overwritten with a default
    QCOMPARE(cursor.count(), 0);
    QTRY_COMPARE(requestCount(QStringLiteral("POST"), favoritePath(id)), 1);
}

void ItemActionsTest::navigationVerbsEmitRequests()
{
    QSignalSpy routes(m_actions, &ItemActions::routeRequested);

    m_actions->openDetails(m_model->get(0));
    QCOMPARE(routes.count(), 1);
    QCOMPARE(routes.last().at(0).toString(), QStringLiteral("details"));
    QCOMPARE(routes.last().at(1).toMap().value(QStringLiteral("itemId")).toString(), kItemId);

    // An episode map from QML carries no series id; ItemActions fills it in from
    // the registered model rather than making the page do it.
    m_actions->openSeries(m_model->get(0));
    QCOMPARE(routes.count(), 2);
    QCOMPARE(routes.last().at(0).toString(), QStringLiteral("series"));
    QCOMPARE(routes.last().at(1).toMap().value(QStringLiteral("itemId")).toString(),
             QStringLiteral("s-9000"));
    QCOMPARE(routes.last().at(1).toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("Fixture Series"));

    // A Series item is its own series.
    MediaItem show;
    show.id = QStringLiteral("s-1234");
    show.name = QStringLiteral("Standalone Show");
    show.type = QStringLiteral("Series");
    m_model->setItems({show});
    m_actions->openSeries(m_model->get(0));
    QCOMPARE(routes.count(), 3);
    QCOMPARE(routes.last().at(1).toMap().value(QStringLiteral("itemId")).toString(),
             QStringLiteral("s-1234"));

    QVariantMap album = itemMap(QStringLiteral("MusicAlbum"), QStringLiteral("album-1"));
    album.insert(QStringLiteral("year"), 1971);
    album.insert(QStringLiteral("favorite"), true);
    m_actions->openDetails(album);
    QCOMPARE(routes.last().at(0).toString(), QStringLiteral("album"));
    QCOMPARE(routes.last().at(1).toMap().value(QStringLiteral("year")).toInt(), 1971);

    QVariantMap artist = itemMap(QStringLiteral("MusicArtist"), QStringLiteral("artist-1"));
    artist.insert(QStringLiteral("posterUrl"), QStringLiteral("image://artist"));
    m_actions->openDetails(artist);
    QCOMPARE(routes.last().at(0).toString(), QStringLiteral("artist"));
    QCOMPARE(routes.last().at(1).toMap().value(QStringLiteral("posterUrl")).toString(),
             QStringLiteral("image://artist"));

    QVariantMap track = itemMap(QStringLiteral("Audio"), QStringLiteral("track-1"));
    track.insert(QStringLiteral("albumId"), QStringLiteral("album-2"));
    track.insert(QStringLiteral("album"), QStringLiteral("Album two"));
    m_actions->openDetails(track);
    QCOMPARE(routes.last().at(0).toString(), QStringLiteral("album"));
    QCOMPARE(routes.last().at(1).toMap().value(QStringLiteral("type")).toString(),
             QStringLiteral("MusicAlbum"));
    track.remove(QStringLiteral("albumId"));
    m_actions->openDetails(track);
    QCOMPARE(routes.last().at(0).toString(), QStringLiteral("details"));

    m_actions->openDetails(itemMap(QStringLiteral("Playlist"), QStringLiteral("playlist-1")));
    QCOMPARE(routes.last().at(0).toString(), QStringLiteral("playlist"));
    m_actions->openDetails(itemMap(QStringLiteral("FutureServerType"), QStringLiteral("new-1")));
    QCOMPARE(routes.last().at(0).toString(), QStringLiteral("details"));

    m_actions->openAlbum(QStringLiteral("album-3"), QStringLiteral("Third"));
    QCOMPARE(routes.last().at(0).toString(), QStringLiteral("album"));
    QCOMPARE(routes.last().at(1).toMap().value(QStringLiteral("type")).toString(),
             QStringLiteral("MusicAlbum"));
    m_actions->openArtist(QStringLiteral("artist-3"), QStringLiteral("Third artist"));
    QCOMPARE(routes.last().at(0).toString(), QStringLiteral("artist"));
    QCOMPARE(routes.last().at(1).toMap().value(QStringLiteral("type")).toString(),
             QStringLiteral("MusicArtist"));

    // Descriptor verbs remain type-gated even if a caller fabricates fields
    // from a different kind. Explicit openAlbum/openArtist above are the only
    // APIs that intentionally construct a synthetic destination.
    const int beforeInvalid = routes.count();
    QVariantMap movie = itemMap(QStringLiteral("Movie"), QStringLiteral("movie-with-music"));
    movie.insert(QStringLiteral("albumId"), QStringLiteral("wrong-album"));
    movie.insert(QStringLiteral("album"), QStringLiteral("Wrong album"));
    movie.insert(QStringLiteral("artists"), QStringList{QStringLiteral("Wrong artist")});
    movie.insert(QStringLiteral("artistIds"), QStringList{QStringLiteral("wrong-artist")});
    m_actions->performItemVerb(QStringLiteral("album"), movie);
    m_actions->performItemVerb(QStringLiteral("artist"), movie);

    // Nothing to navigate to → no signal, no crash.
    m_actions->openDetails(QVariant());
    m_actions->openSeries(QVariant());
    QCOMPARE(routes.count(), beforeInvalid);
}

void ItemActionsTest::refreshMetadataReportsThatItIsNotWired()
{
    // The endpoint is wired now (EmbyClient::refreshMetadata → POST
    // /Items/{id}/Refresh). "Accepted" is all a caller can be told: the server
    // answers 204 and does the work in the background.
    QSignalSpy failures(m_actions, &ItemActions::actionFailed);

    // An empty id is refused before anything reaches the network.
    QVERIFY(!m_actions->refreshMetadata(QString()));
    QCOMPARE(failures.count(), 0);

    // A real id is accepted.
    QVERIFY(m_actions->refreshMetadata(kItemId));

    // The mock server has no route for /Items/{id}/Refresh, so the request
    // fails — which is the useful half to pin: a rejected refresh must surface
    // to the user rather than disappearing into the log.
    QVERIFY(failures.wait(2000));
    QCOMPARE(failures.count(), 1);
    QVERIFY(failures.first().at(0).toString().contains(QStringLiteral("refresh"),
                                                       Qt::CaseInsensitive));
}

// Pressing Play on a folder used to ask the server for a media source the item
// does not have. Measured live: Series, BoxSet, MusicArtist and MusicAlbum all
// answer PlaybackInfo with HTTP 500 —
//   "Unable to cast object of type '…Audio.MusicAlbum' to type '…IHasMediaSources'"
// — which reached the user as a raw HTTP 500 on screen.
void ItemActionsTest::containersPlayTheirContents()
{
    for (const QString &type : {QStringLiteral("MusicAlbum"), QStringLiteral("MusicArtist"),
                                QStringLiteral("Series"), QStringLiteral("Season"),
                                QStringLiteral("BoxSet"), QStringLiteral("Playlist")})
        QVERIFY2(ItemActions::isContainer(type), qPrintable(type));

    // Things that ARE media must keep playing directly.
    for (const QString &type : {QStringLiteral("Movie"), QStringLiteral("Episode"),
                                QStringLiteral("Audio"), QStringLiteral("Video"),
                                QStringLiteral("MusicVideo"), QString()})
        QVERIFY2(!ItemActions::isContainer(type), qPrintable(type.isEmpty() ? "<empty>" : type));
}

QTEST_GUILESS_MAIN(ItemActionsTest)
#include "tst_item_actions.moc"
