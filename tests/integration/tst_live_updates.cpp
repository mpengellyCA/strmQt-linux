#include <QSignalSpy>
#include <QtTest>

#include "MockEmbyServer.h"
#include "app/controllers/HomeController.h"
#include "app/controllers/LibraryController.h"
#include "app/controllers/LiveUpdateService.h"
#include "app/models/MediaItemModel.h"
#include "core/Settings.h"
#include "server/emby/EmbyClient.h"
#include "server/emby/EmbyWebSocket.h"

#include <QTcpServer>
#include <QTemporaryDir>

using namespace strmqt;

namespace {

QString fixturePath(const QString &name)
{
    return QStringLiteral(STRMQT_FIXTURES_DIR "/") + name;
}

const auto kUserId = QStringLiteral("a1b2c3d4e5f60718293a4b5c6d7e8f90");
const auto kToken = QStringLiteral("not-a-real-token-fixture-only");

QString libraryChangedFrame(const QStringList &updated)
{
    return QStringLiteral(R"({"MessageType":"LibraryChanged","Data":{"ItemsAdded":[],)"
                          R"("ItemsRemoved":[],"ItemsUpdated":["%1"]}})")
        .arg(updated.join(QStringLiteral("\",\"")));
}

QString userDataFrame(const QString &itemId, bool played, bool favorite,
                      qint64 positionTicks = 0, int playCount = 0)
{
    return QStringLiteral(
               R"({"MessageType":"UserDataChanged","Data":{"UserId":"u","UserDataList":)"
               R"([{"ItemId":"%1","Played":%2,"IsFavorite":%3,"PlaybackPositionTicks":%4,)"
               R"("PlayCount":%5}]}})")
        .arg(itemId, played ? QStringLiteral("true") : QStringLiteral("false"),
             favorite ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(positionTicks)
        .arg(playCount);
}

// A port nothing is listening on: bind, read the port, then give it back.
quint16 deadPort()
{
    QTcpServer probe;
    probe.listen(QHostAddress::LocalHost, 0);
    const quint16 port = probe.serverPort();
    probe.close();
    return port;
}

} // namespace

// Live updates end to end: the event socket's reconnect discipline, the
// coalescing fan-out, the polling fallback, and the in-place model patch that
// makes a played/favourite change from another device land without a refetch.
class LiveUpdatesTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void liveSettingsDefaultAndValidate();
    void reconnectBacksOffWithoutSpinning();
    void socketConnectsAndDispatches();
    void socketReconnectsAfterServerDrop();
    void webSocketHandshakeTimeoutRetries();
    void oversizedWebSocketMessageReconnectsCleanly();
    void libraryBurstCoalescesIntoOneInvalidation();
    void suspendedUserDataBurstFallsBackToFullInvalidation();
    void pollingFallbackEngagesAndSuspends();
    void suspensionDisarmsAnArmedDebounce();
    void transportGoesOffWhenDisabled();
    void userDataPatchesThenReconcilesHomeMembership();
    void filteredLaterPageAnnouncesMembershipChange();
    void homeMembershipRefreshHasAFloorBetweenBursts();
    void homeHoldsUpdatesWhenAutoApplyIsOff();
    void libraryGridAnnouncesNewItemsInsteadOfReloading();

private:
    void routeHomeFixtures();

    MockEmbyServer *m_mock = nullptr;
    QTemporaryDir *m_dir = nullptr;
    Settings *m_settings = nullptr;
    emby::EmbyClient *m_client = nullptr;
};

void LiveUpdatesTest::init()
{
    m_mock = new MockEmbyServer(this);
    QVERIFY(m_mock->start());

    m_dir = new QTemporaryDir;
    QVERIFY(m_dir->isValid());
    m_settings = new Settings(m_dir->filePath(QStringLiteral("settings.ini")), this);

    m_client = new emby::EmbyClient(this);
    m_client->setDeviceId(QStringLiteral("test-device"));
    m_client->setBaseUrl(m_mock->baseUrl());
    m_client->setSession(kToken, kUserId);
}

void LiveUpdatesTest::cleanup()
{
    delete m_client;
    delete m_settings;
    delete m_dir;
    delete m_mock;
    m_client = nullptr;
    m_settings = nullptr;
    m_dir = nullptr;
    m_mock = nullptr;
}

void LiveUpdatesTest::routeHomeFixtures()
{
    QVERIFY(m_mock->addRouteFromFile(QStringLiteral("GET"),
                                     QStringLiteral("/Users/%1/Items/Resume").arg(kUserId),
                                     fixturePath(QStringLiteral("resume.json"))));
    QVERIFY(m_mock->addRouteFromFile(QStringLiteral("GET"), QStringLiteral("/Shows/NextUp"),
                                     fixturePath(QStringLiteral("nextup.json"))));
    QVERIFY(m_mock->addRouteFromFile(QStringLiteral("GET"),
                                     QStringLiteral("/Users/%1/Views").arg(kUserId),
                                     fixturePath(QStringLiteral("views.json"))));
    QVERIFY(m_mock->addRouteFromFile(QStringLiteral("GET"),
                                     QStringLiteral("/Users/%1/Items/Latest").arg(kUserId),
                                     fixturePath(QStringLiteral("latest.json"))));
    QVERIFY(m_mock->addRouteFromFile(QStringLiteral("GET"),
                                     QStringLiteral("/Users/%1/Items").arg(kUserId),
                                     fixturePath(QStringLiteral("items_movies.json"))));
}

void LiveUpdatesTest::liveSettingsDefaultAndValidate()
{
    // Defaults per ARCHITECTURE.md.
    QVERIFY(m_settings->liveUpdatesEnabled());
    QCOMPARE(m_settings->pollIntervalSeconds(), 60);
    QVERIFY(Settings::pollIntervalChoices().contains(60));

    QSignalSpy intervalChanged(m_settings, &Settings::pollIntervalSecondsChanged);
    m_settings->setPollIntervalSeconds(120);
    QCOMPARE(m_settings->pollIntervalSeconds(), 120);
    QCOMPARE(intervalChanged.size(), 1);
    m_settings->setPollIntervalSeconds(120); // idempotent
    QCOMPARE(intervalChanged.size(), 1);

    // A hand-edited INI must not produce a request flood or a dead timer.
    m_settings->setPollIntervalSeconds(1);
    QCOMPARE(m_settings->pollIntervalSeconds(), 15);
    m_settings->setPollIntervalSeconds(99999);
    QCOMPARE(m_settings->pollIntervalSeconds(), 3600);
    m_settings->setPollIntervalSeconds(0);
    QCOMPARE(m_settings->pollIntervalSeconds(), 60);

    QSignalSpy enabledChanged(m_settings, &Settings::liveUpdatesEnabledChanged);
    m_settings->setLiveUpdatesEnabled(false);
    QVERIFY(!m_settings->liveUpdatesEnabled());
    QCOMPARE(enabledChanged.size(), 1);

    // Nothing above may have disturbed the [input] group InputMap owns.
    QSettings raw(m_dir->filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    QVERIFY(raw.childGroups().contains(QStringLiteral("live")));
    QVERIFY(!raw.childGroups().contains(QStringLiteral("input")));
}

// A server that refuses every connection must not turn into a busy loop.
void LiveUpdatesTest::reconnectBacksOffWithoutSpinning()
{
    emby::EmbyWebSocket socket;
    socket.setBackoffForTests(20, 160);
    QSignalSpy scheduled(&socket, &emby::EmbyWebSocket::reconnectScheduled);

    const QUrl dead(QStringLiteral("http://127.0.0.1:%1").arg(deadPort()));
    socket.connectToServer(dead, kToken, QStringLiteral("dev"));

    // Wait for the backoff to reach its cap; with base 20 ms and cap 160 ms that
    // is attempt 4, so a window of ~700 ms is generous.
    QTRY_VERIFY_WITH_TIMEOUT(scheduled.size() >= 5, 3000);
    QVERIFY(!socket.isConnected());

    QList<int> delays;
    for (const auto &args : scheduled)
        delays.append(args.at(0).toInt());

    // Growth, allowing for the ±25 % jitter: attempt n+2 must be clearly longer
    // than attempt n until the cap is reached.
    QVERIFY2(delays.at(2) > delays.at(0), qPrintable(QStringLiteral("delays: %1 then %2")
                                                         .arg(delays.at(0))
                                                         .arg(delays.at(2))));
    for (int delay : delays) {
        QVERIFY(delay > 0);
        QVERIFY2(delay <= 160 + 40, qPrintable(QStringLiteral("delay %1 above cap").arg(delay)));
    }

    // The real anti-spin assertion: a fixed window cannot contain unbounded
    // attempts. At the 160 ms cap, 800 ms allows at most ~7 more.
    const int before = socket.reconnectAttempts();
    QTest::qWait(800);
    const int added = socket.reconnectAttempts() - before;
    QVERIFY2(added <= 10, qPrintable(QStringLiteral("%1 attempts in 800 ms").arg(added)));

    socket.disconnectFromServer();
}

void LiveUpdatesTest::socketConnectsAndDispatches()
{
    QVERIFY(m_mock->startWebSocket());

    emby::EmbyWebSocket socket;
    socket.setKeepAliveIntervalForTests(50);
    QSignalSpy library(&socket, &emby::EmbyWebSocket::libraryChanged);
    QSignalSpy userData(&socket, &emby::EmbyWebSocket::userDataEntriesChanged);

    socket.connectToServer(m_mock->webSocketBaseUrl(), kToken, QStringLiteral("dev"));
    QTRY_VERIFY(socket.isConnected());
    QTRY_COMPARE(m_mock->webSocketClientCount(), 1);

    m_mock->sendWebSocketMessage(libraryChangedFrame({QStringLiteral("301001")}));
    QTRY_COMPARE(library.size(), 1);
    QCOMPARE(library.first().at(2).toStringList(), QStringList{QStringLiteral("301001")});

    m_mock->sendWebSocketMessage(userDataFrame(QStringLiteral("301001"), true, true));
    QTRY_COMPARE(userData.size(), 1);

    // The keep-alive is write-only against the real server, but it must actually
    // go out — the server hangs up after ~30 s of client silence.
    QTRY_VERIFY(!m_mock->webSocketMessagesReceived().isEmpty());
    QVERIFY(m_mock->webSocketMessagesReceived().first().contains(QStringLiteral("KeepAlive")));

    socket.disconnectFromServer();
    QVERIFY(!socket.isConnected());
}

void LiveUpdatesTest::socketReconnectsAfterServerDrop()
{
    QVERIFY(m_mock->startWebSocket());

    emby::EmbyWebSocket socket;
    socket.setBackoffForTests(20, 100);
    socket.connectToServer(m_mock->webSocketBaseUrl(), kToken, QStringLiteral("dev"));
    QTRY_VERIFY(socket.isConnected());

    m_mock->dropWebSocketClients();
    QTRY_VERIFY(!socket.isConnected());
    // It comes back on its own — nothing in the app has to notice.
    QTRY_VERIFY_WITH_TIMEOUT(socket.isConnected(), 5000);

    socket.disconnectFromServer();
}

void LiveUpdatesTest::webSocketHandshakeTimeoutRetries()
{
    // Accept TCP but hold the HTTP upgrade response past the application-level
    // deadline: this is the blackholed handshake that used to wedge polling for
    // the entire authenticated session.
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/embywebsocket"), 500, {});
    m_mock->setRouteDelay(QStringLiteral("GET"), QStringLiteral("/embywebsocket"), 500);

    emby::EmbyWebSocket socket;
    socket.setHandshakeTimeoutForTests(120);
    socket.setBackoffForTests(30, 30);
    QSignalSpy retries(&socket, &emby::EmbyWebSocket::reconnectScheduled);
    socket.connectToServer(m_mock->baseUrl(), kToken, QStringLiteral("dev"));

    // Reasserting the same tuple while an upgrade is viable must not create
    // parallel sockets or reset its timeout.
    for (int i = 0; i < 5; ++i)
        socket.connectToServer(m_mock->baseUrl(), kToken, QStringLiteral("dev"));
    QTest::qWait(40);
    QCOMPARE(m_mock->requestCount(), 1);

    QTRY_COMPARE_WITH_TIMEOUT(m_mock->requestCount(), 2, 1000);
    QCOMPARE(retries.size(), 1);
    for (int i = 0; i < 5; ++i)
        socket.connectToServer(m_mock->baseUrl(), kToken, QStringLiteral("dev"));
    QTest::qWait(40);
    QCOMPARE(m_mock->requestCount(), 2);
    QCOMPARE(retries.size(), 1);
    socket.disconnectFromServer();
}

void LiveUpdatesTest::oversizedWebSocketMessageReconnectsCleanly()
{
    QVERIFY(m_mock->startWebSocket());

    emby::EmbyWebSocket socket;
    socket.setBackoffForTests(200, 200);
    socket.connectToServer(m_mock->webSocketBaseUrl(), kToken, QStringLiteral("dev"));
    QTRY_VERIFY(socket.isConnected());

    m_mock->sendWebSocketMessage(QString(1024 * 1024 + 1, QLatin1Char('x')));
    QTRY_VERIFY_WITH_TIMEOUT(!socket.isConnected(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(socket.isConnected(), 5000);
    QCOMPARE(m_mock->webSocketClientCount(), 1);

    QSignalSpy messages(&socket, &emby::EmbyWebSocket::messageReceived);
    m_mock->sendWebSocketMessage(QStringLiteral(
        R"({"MessageType":"RefreshProgress","Data":{"ItemId":"healthy"}})"));
    QTRY_COMPARE(messages.size(), 1);
    QCOMPARE(messages.first().at(1).toJsonObject().value(QStringLiteral("ItemId")).toString(),
             QStringLiteral("healthy"));
    socket.disconnectFromServer();
}

// A library scan emits a storm; the UI must refresh once, not forty times.
void LiveUpdatesTest::libraryBurstCoalescesIntoOneInvalidation()
{
    LiveUpdateService live(m_client, m_settings);
    live.setDebounceForTests(60, 30, 2000);
    QSignalSpy invalidated(&live, &LiveUpdateService::libraryInvalidated);

    for (int i = 0; i < 40; ++i)
        live.socket()->handleTextMessage(
            libraryChangedFrame({QStringLiteral("item-%1").arg(i % 5)}));

    QVERIFY(invalidated.isEmpty()); // nothing fires while the burst is still arriving
    QTRY_COMPARE(invalidated.size(), 1);

    QStringList ids = invalidated.first().at(0).toStringList();
    ids.sort();
    QCOMPARE(ids.size(), 5); // the union, deduplicated
    QCOMPARE(ids.first(), QStringLiteral("item-0"));

    // A continuous stream must still deliver: the max-deferral ceiling fires
    // even though every message restarts the debounce.
    live.setDebounceForTests(60, 30, 150);
    invalidated.clear();
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < 400) {
        live.socket()->handleTextMessage(libraryChangedFrame({QStringLiteral("storm")}));
        QTest::qWait(20);
    }
    QVERIFY2(!invalidated.isEmpty(), "a continuous storm deferred delivery forever");
}

void LiveUpdatesTest::suspendedUserDataBurstFallsBackToFullInvalidation()
{
    LiveUpdateService live(m_client, m_settings);
    live.setDebounceForTests(20, 10, 500);
    QSignalSpy invalidated(&live, &LiveUpdateService::userDataInvalidated);
    QSignalSpy patched(&live, &LiveUpdateService::userDataPatched);

    live.setSuspended(true);
    for (int i = 0; i <= 200; ++i)
        live.socket()->handleTextMessage(
            userDataFrame(QStringLiteral("item-%1").arg(i), true, false));

    // Lightweight role patches are immediate, but the retained reconciliation
    // state is capped while playback or an unfocused window suspends refreshes.
    QCOMPARE(patched.size(), 201);
    QCOMPARE(invalidated.size(), 0);

    live.setSuspended(false);
    QCOMPARE(invalidated.size(), 1);
    QVERIFY(invalidated.first().at(0).toStringList().isEmpty());
}

void LiveUpdatesTest::pollingFallbackEngagesAndSuspends()
{
    // No websocket server on the mock: the socket cannot connect, so the
    // fallback has to carry the app.
    LiveUpdateService live(m_client, m_settings);
    live.socket()->setBackoffForTests(50, 200);
    live.setPollIntervalMsForTests(40);
    QSignalSpy invalidated(&live, &LiveUpdateService::libraryInvalidated);

    QCOMPARE(live.transport(), QStringLiteral("off"));
    live.start();
    QCOMPARE(live.transport(), QStringLiteral("polling"));
    QVERIFY(!live.isConnected());
    QTRY_VERIFY(invalidated.size() >= 2);

    // Playback (or a background window) suspends it: a poll that fights the
    // decoder is worse than stale data.
    live.setSuspended(true);
    QVERIFY(live.suspended());
    invalidated.clear();
    QTest::qWait(250);
    QCOMPARE(invalidated.size(), 0);

    live.setSuspended(false);
    QTRY_VERIFY(invalidated.size() >= 1);

    live.stop();
    QCOMPARE(live.transport(), QStringLiteral("off"));
    invalidated.clear();
    QTest::qWait(150);
    QCOMPARE(invalidated.size(), 0);
}

// Suspension stopped the poll timer and nothing else. A debounce armed in the
// second before playback started stayed armed, fired mid-playback and reloaded
// the library grid — the one thing suspension exists to prevent.
void LiveUpdatesTest::suspensionDisarmsAnArmedDebounce()
{
    LiveUpdateService live(m_client, m_settings);
    live.setDebounceForTests(120, 120, 5000);
    QSignalSpy invalidated(&live, &LiveUpdateService::libraryInvalidated);
    QSignalSpy userData(&live, &LiveUpdateService::userDataInvalidated);

    // A scan message and a watched-state change arm both debounces; playback
    // starts before either window elapses.
    live.socket()->handleTextMessage(libraryChangedFrame({QStringLiteral("item-1")}));
    live.socket()->handleTextMessage(userDataFrame(QStringLiteral("item-1"), true, false));
    live.setSuspended(true);

    QTest::qWait(300);
    QCOMPARE(invalidated.size(), 0);
    QCOMPARE(userData.size(), 0);

    // Deferred, not dropped: the socket does not resend, so resume has to
    // deliver what was held or the grid stays wrong until something unrelated
    // invalidates it.
    live.setSuspended(false);
    QTRY_COMPARE(invalidated.size(), 1);
    QCOMPARE(invalidated.first().at(0).toStringList(), QStringList{QStringLiteral("item-1")});
    QTRY_COMPARE(userData.size(), 1);
    QCOMPARE(userData.first().at(0).toStringList(), QStringList{QStringLiteral("item-1")});
}

void LiveUpdatesTest::transportGoesOffWhenDisabled()
{
    m_settings->setLiveUpdatesEnabled(false);
    LiveUpdateService live(m_client, m_settings);
    live.setPollIntervalMsForTests(30);
    QSignalSpy invalidated(&live, &LiveUpdateService::libraryInvalidated);

    live.start();
    QCOMPARE(live.transport(), QStringLiteral("off"));
    QTest::qWait(150);
    QCOMPARE(invalidated.size(), 0);

    // Turning it back on restarts the service without another start() call.
    m_settings->setLiveUpdatesEnabled(true);
    QCOMPARE(live.transport(), QStringLiteral("polling"));
    QTRY_VERIFY(invalidated.size() >= 1);
    live.stop();
}

// UserData fields patch immediately; server-owned rail membership is then
// reconciled once after the debounce.
void LiveUpdatesTest::userDataPatchesThenReconcilesHomeMembership()
{
    routeHomeFixtures();

    HomeController home(m_client);
    LiveUpdateService live(m_client, m_settings);
    live.setDebounceForTests(20, 10, 500);
    home.bindLiveUpdates(&live);

    home.refresh();
    QTRY_VERIFY(!home.busy());
    QVERIFY(home.resume()->rowCount() > 0);

    const QString itemId = home.resume()->get(0).value(QStringLiteral("itemId")).toString();
    QVERIFY(!itemId.isEmpty());
    QVERIFY(!home.resume()->get(0).value(QStringLiteral("favorite")).toBool());

    const int requestsBefore = m_mock->requestCount();
    QSignalSpy patched(&live, &LiveUpdateService::userDataPatched);

    live.socket()->handleTextMessage(userDataFrame(itemId, true, true, 420000000, 7));
    QCOMPARE(patched.size(), 1);

    QVERIFY(home.resume()->get(0).value(QStringLiteral("favorite")).toBool());
    QVERIFY(home.resume()->get(0).value(QStringLiteral("played")).toBool());
    QCOMPARE(home.resume()->get(0).value(QStringLiteral("positionMs")).toLongLong(), 42000);
    QCOMPARE(home.resume()->get(0).value(QStringLiteral("playCount")).toInt(), 7);

    // The patch itself is network-free, then the debounced invalidation reloads
    // Continue Watching / Next Up / Favorites because membership is not local.
    QCOMPARE(m_mock->requestCount(), requestsBefore);
    QTRY_VERIFY(m_mock->requestCount() > requestsBefore);
}

void LiveUpdatesTest::filteredLaterPageAnnouncesMembershipChange()
{
    QVERIFY(m_mock->addRouteFromFile(QStringLiteral("GET"),
                                     QStringLiteral("/Users/%1/Items").arg(kUserId),
                                     fixturePath(QStringLiteral("items_movies.json"))));

    LibraryController library(m_client);
    library.openFavorites();
    QTRY_VERIFY(!library.loading());

    // Represent a grid that has paged beyond its first 100 rows. An equal total
    // after UserData invalidation can still mean one favorite left and another
    // entered, so total-delta-only probing is insufficient.
    QList<MediaItem> laterPage;
    laterPage.reserve(101);
    for (int i = 0; i < 101; ++i) {
        MediaItem item;
        item.id = QStringLiteral("later-%1").arg(i);
        item.name = item.id;
        item.type = QStringLiteral("Movie");
        item.favorite = true;
        laterPage.append(item);
    }
    library.model()->setItems(laterPage, 101);

    m_mock->addRoute(QStringLiteral("GET"),
                     QStringLiteral("/Users/%1/Items").arg(kUserId), 200,
                     QByteArrayLiteral(R"({"Items":[{"Id":"later-0","Name":"later-0",)"
                                       R"("Type":"Movie"}],"TotalRecordCount":101})"));

    QVariantMap patch;
    patch.insert(QStringLiteral("itemId"), QStringLiteral("later-0"));
    patch.insert(QStringLiteral("played"), true);
    patch.insert(QStringLiteral("favorite"), false);
    patch.insert(QStringLiteral("positionTicks"), 10000000);
    patch.insert(QStringLiteral("playCount"), 3);
    library.onUserDataPatched({patch});
    QVERIFY(!library.model()->get(0).value(QStringLiteral("favorite")).toBool());
    QCOMPARE(library.model()->get(0).value(QStringLiteral("playCount")).toInt(), 3);

    library.onUserDataInvalidated({QStringLiteral("later-0")});
    QTRY_VERIFY(library.updatesPending());
    QCOMPARE(library.pendingNewCount(), 0); // "Updated", not a fictitious add
    QCOMPARE(library.model()->rowCount(), 101); // cursor/scroll remain stable
}

// Audio playback keeps live updates running for hours, and every progress
// report moves user data on the server. Membership reconciliation is three
// queries, so it gets a floor: the first invalidation refreshes at once, and
// everything arriving behind it collapses into a single later snapshot.
void LiveUpdatesTest::homeMembershipRefreshHasAFloorBetweenBursts()
{
    routeHomeFixtures();

    HomeController home(m_client);
    home.setUserDataRefreshFloorMsForTests(300);
    home.refresh();
    QTRY_VERIFY(!home.busy());

    const int beforeFirst = m_mock->requestCount();
    home.onUserDataInvalidated({});
    QTRY_VERIFY(!home.busy());
    const int afterFirst = m_mock->requestCount();
    const int requestsPerSnapshot = afterFirst - beforeFirst;
    QVERIFY(requestsPerSnapshot > 0);

    home.onUserDataInvalidated({});
    home.onUserDataInvalidated({});
    home.onUserDataInvalidated({});
    QCOMPARE(m_mock->requestCount(), afterFirst); // nothing goes out inside the floor

    QTRY_VERIFY(m_mock->requestCount() > afterFirst);
    QTRY_VERIFY(!home.busy());
    // One snapshot for the whole burst, not one per invalidation.
    QCOMPARE(m_mock->requestCount() - afterFirst, requestsPerSnapshot);
}

void LiveUpdatesTest::homeHoldsUpdatesWhenAutoApplyIsOff()
{
    routeHomeFixtures();

    HomeController home(m_client);
    home.refresh();
    QTRY_VERIFY(!home.busy());
    const int railsBefore = home.latestRails().size();
    QVERIFY(railsBefore > 0);
    auto *rail = home.latestRails().first().toMap()
                     .value(QStringLiteral("model")).value<MediaItemModel *>();
    QVERIFY(rail);
    const int itemsBefore = rail->rowCount();

    // The user is scrolled away: nothing may move under the cursor.
    home.setAutoApplyUpdates(false);

    // The server now has an extra item in Latest.
    m_mock->addRoute(QStringLiteral("GET"),
                     QStringLiteral("/Users/%1/Items/Latest").arg(kUserId), 200,
                     QByteArrayLiteral(R"([{"Id":"999999","Name":"Brand New","Type":"Movie"},)"
                                       R"({"Id":"301099","Name":"Dune: Part Two","Type":"Movie"}])"));

    QSignalSpy pending(&home, &HomeController::pendingChanged);
    home.onLibraryInvalidated({});
    QTRY_VERIFY(!home.busy());

    QVERIFY2(home.updatesPending(), "a server-side change was applied under the cursor");
    QCOMPARE(home.pendingNewCount(), 1);
    QCOMPARE(rail->rowCount(), itemsBefore); // untouched until the user says so
    QVERIFY(!pending.isEmpty());

    home.applyPending();
    QVERIFY(!home.updatesPending());
    QCOMPARE(home.pendingNewCount(), 0);
    auto *railAfter = home.latestRails().first().toMap()
                          .value(QStringLiteral("model")).value<MediaItemModel *>();
    QCOMPARE(railAfter->rowCount(), 2);
    QCOMPARE(railAfter->get(0).value(QStringLiteral("itemId")).toString(), QStringLiteral("999999"));
    // The rail model is reused across refreshes, so QML's bindings survive.
    QCOMPARE(railAfter, rail);
}

void LiveUpdatesTest::libraryGridAnnouncesNewItemsInsteadOfReloading()
{
    QVERIFY(m_mock->addRouteFromFile(QStringLiteral("GET"),
                                     QStringLiteral("/Users/%1/Items").arg(kUserId),
                                     fixturePath(QStringLiteral("items_movies.json"))));

    LibraryController library(m_client);
    library.open(QStringLiteral("4"), QStringLiteral("Movies"), QStringLiteral("movies"));
    QTRY_VERIFY(!library.loading());
    QCOMPARE(library.model()->rowCount(), 3);
    QCOMPARE(library.model()->totalRecordCount(), 42);

    // The user is browsing: do not yank the grid back to page 0.
    library.setAutoApplyUpdates(false);
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Users/%1/Items").arg(kUserId), 200,
                     QByteArrayLiteral(R"({"Items":[{"Id":"301001","Name":"The Matrix",)"
                                       R"("Type":"Movie"}],"TotalRecordCount":45})"));

    library.onLibraryInvalidated({});
    QTRY_VERIFY(library.updatesPending());
    QCOMPARE(library.pendingNewCount(), 3);
    QCOMPARE(library.model()->rowCount(), 3); // still showing what it was showing

    // The probe is one cheap request, not a page.
    const auto probe = m_mock->lastRequestFor(QStringLiteral("GET"),
                                              QStringLiteral("/Users/%1/Items").arg(kUserId));
    QVERIFY(probe.query.contains(QStringLiteral("Limit=1")));

    library.applyPending();
    QTRY_VERIFY(!library.loading());
    QVERIFY(!library.updatesPending());
    QCOMPARE(library.model()->totalRecordCount(), 45);

    // With auto-apply on and the grid still on its first page, an invalidation
    // reloads silently instead of raising a pill.
    library.setAutoApplyUpdates(true);
    library.onLibraryInvalidated({});
    QTRY_VERIFY(!library.loading());
    QVERIFY(!library.updatesPending());
}

QTEST_GUILESS_MAIN(LiveUpdatesTest)
#include "tst_live_updates.moc"
