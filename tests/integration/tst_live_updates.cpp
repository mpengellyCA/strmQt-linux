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

QString userDataFrame(const QString &itemId, bool played, bool favorite)
{
    return QStringLiteral(
               R"({"MessageType":"UserDataChanged","Data":{"UserId":"u","UserDataList":)"
               R"([{"ItemId":"%1","Played":%2,"IsFavorite":%3,"PlaybackPositionTicks":0,)"
               R"("PlayCount":0}]}})")
        .arg(itemId, played ? QStringLiteral("true") : QStringLiteral("false"),
             favorite ? QStringLiteral("true") : QStringLiteral("false"));
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
    void libraryBurstCoalescesIntoOneInvalidation();
    void pollingFallbackEngagesAndSuspends();
    void suspensionDisarmsAnArmedDebounce();
    void transportGoesOffWhenDisabled();
    void userDataPatchesHomeModelsWithoutRefetch();
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

// The whole point of C2: a played/favourite change from another device patches
// the visible models and costs no extra request.
void LiveUpdatesTest::userDataPatchesHomeModelsWithoutRefetch()
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

    live.socket()->handleTextMessage(userDataFrame(itemId, true, true));
    QCOMPARE(patched.size(), 1);

    QVERIFY(home.resume()->get(0).value(QStringLiteral("favorite")).toBool());
    QVERIFY(home.resume()->get(0).value(QStringLiteral("played")).toBool());

    // No refetch, now or after the debounce would have elapsed.
    QTest::qWait(120);
    QCOMPARE(m_mock->requestCount(), requestsBefore);
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
