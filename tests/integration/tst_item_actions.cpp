#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>

#include "FakePlayerBackend.h"
#include "MockEmbyServer.h"
#include "app/ItemActions.h"
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
    void navigationVerbsEmitRequests();
    void refreshMetadataReportsThatItIsNotWired();

private:
    int requestCount(const QString &method, const QString &path) const;

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

void ItemActionsTest::navigationVerbsEmitRequests()
{
    QSignalSpy details(m_actions, &ItemActions::detailsRequested);
    QSignalSpy series(m_actions, &ItemActions::seriesRequested);

    m_actions->openDetails(m_model->get(0));
    QCOMPARE(details.count(), 1);
    QCOMPARE(details.first().at(0).toMap().value(QStringLiteral("itemId")).toString(), kItemId);

    // An episode map from QML carries no series id; ItemActions fills it in from
    // the registered model rather than making the page do it.
    m_actions->openSeries(m_model->get(0));
    QCOMPARE(series.count(), 1);
    QCOMPARE(series.first().at(0).toString(), QStringLiteral("s-9000"));
    QCOMPARE(series.first().at(1).toString(), QStringLiteral("Fixture Series"));

    // A Series item is its own series.
    MediaItem show;
    show.id = QStringLiteral("s-1234");
    show.name = QStringLiteral("Standalone Show");
    show.type = QStringLiteral("Series");
    m_model->setItems({show});
    m_actions->openSeries(m_model->get(0));
    QCOMPARE(series.count(), 2);
    QCOMPARE(series.last().at(0).toString(), QStringLiteral("s-1234"));
    QCOMPARE(series.last().at(1).toString(), QStringLiteral("Standalone Show"));

    // Nothing to navigate to → no signal, no crash.
    m_actions->openDetails(QVariant());
    m_actions->openSeries(QVariant());
    QCOMPARE(details.count(), 1);
    QCOMPARE(series.count(), 2);
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
