#include <QHostAddress>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

#include "MockEmbyServer.h"
#include "RemoteTestClient.h"
#include "app/controllers/SessionController.h"
#include "core/Settings.h"
#include "platform/SecretsStore.h"
#include "remote/WebRemoteController.h"
#include "remote/WebRemoteServer.h"
#include "server/emby/EmbyClient.h"

using namespace strmqt;

namespace {

QString fixturePath(const QString &name)
{
    return QStringLiteral(STRMQT_FIXTURES_DIR "/") + name;
}

} // namespace

// The run policy (enabled AND signed in; stopped at every session boundary)
// and following the network in lan/tailscale modes.
class WebRemoteControllerTest : public QObject
{
    Q_OBJECT

    static constexpr quint16 kPort = 18345;
    static constexpr quint16 kBlockedPort = 18346;
    static constexpr quint16 kOtherPort = 18347;

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void runsOnlyWhenEnabledAndSignedIn();
    void stopsAtEverySessionBoundary();
    void connectedClientsAreCounted();
    void portInUseSetsError();
    void regenerateCertificateChangesFingerprint();
    void rebindsWhenTheNetworkChanges();
    void urlsChangedFollowsTheRestart();
    void failedRestartAfterRegenerateSetsError();

private:
    void signIn();

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<MockEmbyServer> m_emby;
    std::unique_ptr<Settings> m_settings;
    std::unique_ptr<SecretsStore> m_secrets;
    std::unique_ptr<emby::EmbyClient> m_client;
    std::unique_ptr<SessionController> m_session;
    std::unique_ptr<WebRemoteServer> m_server;
    std::unique_ptr<WebRemoteController> m_remote;
};

void WebRemoteControllerTest::initTestCase()
{
    // The certificate goes under ~/.qttest, not the user's data directory.
    QStandardPaths::setTestModeEnabled(true);
}

void WebRemoteControllerTest::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_emby = std::make_unique<MockEmbyServer>();
    QVERIFY(m_emby->start());
    QVERIFY(m_emby->addRouteFromFile(QStringLiteral("POST"),
                                     QStringLiteral("/Users/AuthenticateByName"),
                                     fixturePath(QStringLiteral("auth_by_name.json"))));
    m_settings = std::make_unique<Settings>(m_dir->filePath(QStringLiteral("settings.ini")));
    m_settings->setServerUrl(m_emby->baseUrl());
    m_settings->setWebRemotePort(kPort);
    m_settings->setWebRemoteBindMode(QStringLiteral("localhost"));
    m_secrets = std::make_unique<SecretsStore>(m_dir->filePath(QStringLiteral("secrets.ini")));
    m_client = std::make_unique<emby::EmbyClient>();
    m_client->setDeviceId(QStringLiteral("test-device"));
    m_session = std::make_unique<SessionController>(m_settings.get(), m_secrets.get(), m_client.get());
    m_server = std::make_unique<WebRemoteServer>(m_settings.get(), nullptr, nullptr, nullptr,
                                                 m_session.get(), m_client.get());
    m_remote = std::make_unique<WebRemoteController>(m_settings.get(), m_session.get(),
                                                     m_server.get());
}

void WebRemoteControllerTest::cleanup()
{
    m_remote.reset();
    m_server.reset();
    m_session.reset();
    m_client.reset();
    m_secrets.reset();
    m_settings.reset();
    m_emby.reset();
    m_dir.reset();
}

void WebRemoteControllerTest::signIn()
{
    m_session->login(QStringLiteral("mike"), QStringLiteral("pw"));
    QTRY_VERIFY_WITH_TIMEOUT(m_session->authenticated(), 10000);
}

void WebRemoteControllerTest::runsOnlyWhenEnabledAndSignedIn()
{
    QVERIFY(m_remote->isEnabled()); // StrmQt's default
    QVERIFY(!m_remote->isRunning()); // signed out: off
    QVERIFY(m_remote->error().isEmpty());
    QSignalSpy running(m_remote.get(), &WebRemoteController::runningChanged);
    signIn();
    QVERIFY(m_remote->isRunning());
    QCOMPARE(running.size(), 1);
    m_remote->setEnabled(false);
    QVERIFY(!m_remote->isRunning());
    QVERIFY(!m_settings->webRemoteEnabled());
    // start() follows the policy too.
    m_remote->start();
    QVERIFY(!m_remote->isRunning());
    m_remote->setEnabled(true);
    QVERIFY(m_remote->isRunning());
}

void WebRemoteControllerTest::stopsAtEverySessionBoundary()
{
    signIn();
    QVERIFY(m_remote->isRunning());
    RemoteTestClient http(kPort);
    auto socket = http.connectTls();
    QVERIFY(socket);
    socket->write("GET /api/auth/pin HTTP/1.1\r\nHost: x\r\n\r\n");
    QTRY_VERIFY_WITH_TIMEOUT(socket->bytesAvailable() > 0, 5000); // a live keep-alive connection

    // Switching user: the boundary stops the server, and its connections, at once.
    m_session->switchUser();
    QVERIFY(!m_remote->isRunning());
    QTRY_COMPARE_WITH_TIMEOUT(socket->state(), QAbstractSocket::UnconnectedState, 5000);
    QTest::qWait(50); // the deferred re-evaluation leaves it off while signed out
    QVERIFY(!m_remote->isRunning());

    // Signing in again starts it; a phone signed in before does not carry over.
    m_settings->setWebRemoteRequirePin(true);
    m_settings->setWebRemotePin(QStringLiteral("2468"));
    signIn();
    QVERIFY(m_remote->isRunning());
    const RemoteResponse auth =
        http.post(QStringLiteral("/api/auth/pin"), QByteArray("{\"pin\":\"2468\"}"));
    QCOMPARE(auth.status, 200);
    const QString token = auth.json().value(QStringLiteral("token")).toString();
    QCOMPARE(http.get(QStringLiteral("/api/status?token=") + token).status, 200);

    // Logout: off, and it stays off.
    m_session->logout();
    QVERIFY(!m_remote->isRunning());
    QTest::qWait(50);
    QVERIFY(!m_remote->isRunning());

    // Back in: the old token was forgotten with the old session.
    signIn();
    QVERIFY(m_remote->isRunning());
    QCOMPARE(http.get(QStringLiteral("/api/status?token=") + token).status, 401);
}

void WebRemoteControllerTest::connectedClientsAreCounted()
{
    signIn();
    RemoteTestClient http(kPort);
    auto events = http.openEvents();
    QVERIFY(events);
    QCOMPARE(m_remote->connectedClientsCount(), 1);
    m_session->logout();
    QCOMPARE(m_remote->connectedClientsCount(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(events->state(), QAbstractSocket::UnconnectedState, 5000);
}

void WebRemoteControllerTest::portInUseSetsError()
{
    QTcpServer blocker;
    QVERIFY(blocker.listen(QHostAddress::LocalHost, kBlockedPort));
    signIn();
    QVERIFY(m_remote->isRunning());
    QSignalSpy errors(m_remote.get(), &WebRemoteController::errorChanged);
    m_remote->setPort(kBlockedPort);
    QVERIFY(!m_remote->isRunning());
    QVERIFY2(m_remote->error().contains(QStringLiteral("18346")), qPrintable(m_remote->error()));
    QCOMPARE(errors.size(), 1);
    QTest::qWait(300);
    QVERIFY(!m_remote->isRunning()); // no retry loop
    m_remote->setPort(kOtherPort);
    QVERIFY(m_remote->isRunning());
    QVERIFY(m_remote->error().isEmpty());
}

void WebRemoteControllerTest::regenerateCertificateChangesFingerprint()
{
    signIn();
    QVERIFY(m_remote->isRunning());
    const QString before = m_remote->certFingerprint();
    QVERIFY(!before.isEmpty());
    m_remote->regenerateCertificate();
    QVERIFY(m_remote->isRunning());
    QVERIFY(m_remote->error().isEmpty());
    QVERIFY(!m_remote->certFingerprint().isEmpty());
    QVERIFY(m_remote->certFingerprint() != before);
}

void WebRemoteControllerTest::rebindsWhenTheNetworkChanges()
{
    // lan mode on a fake network: 127.0.0.1 until "Wi-Fi" brings 127.0.0.2.
    QHostAddress lanAddress(QHostAddress::LocalHost);
    m_remote->setAddressResolverForTests(
        [&lanAddress](const QString &mode) {
            return mode == QLatin1String("lan") ? lanAddress : QHostAddress(QHostAddress::LocalHost);
        },
        50);
    m_remote->setBindMode(QStringLiteral("lan"));
    signIn();
    QVERIFY(m_remote->isRunning());
    QCOMPARE(m_remote->activeUrl(), QStringLiteral("https://127.0.0.1:18345"));

    QSignalSpy urls(m_remote.get(), &WebRemoteController::urlsChanged);
    QSignalSpy activeUrl(m_remote.get(), &WebRemoteController::activeUrlChanged);
    QSignalSpy running(m_remote.get(), &WebRemoteController::runningChanged);
    lanAddress = QHostAddress(QStringLiteral("127.0.0.2"));
    QTRY_COMPARE_WITH_TIMEOUT(m_remote->activeUrl(), QStringLiteral("https://127.0.0.2:18345"), 5000);
    QVERIFY(m_remote->isRunning());
    QCOMPARE(m_server->boundAddress(), QHostAddress(QStringLiteral("127.0.0.2")));
    QVERIFY(urls.size() >= 1);
    QVERIFY(activeUrl.size() >= 1); // what the QR code follows
    QCOMPARE(running.size(), 2);    // stopped, then started on the new address
    QVERIFY(m_remote->error().isEmpty());

    // Nothing changes while the address holds.
    QTest::qWait(300);
    QCOMPARE(running.size(), 2);

    // The network goes away again.
    lanAddress = QHostAddress(QHostAddress::LocalHost);
    QTRY_COMPARE_WITH_TIMEOUT(m_remote->activeUrl(), QStringLiteral("https://127.0.0.1:18345"), 5000);
    QVERIFY(m_remote->isRunning());

    // localhost mode never re-resolves.
    m_remote->setBindMode(QStringLiteral("localhost"));
    running.clear();
    lanAddress = QHostAddress(QStringLiteral("127.0.0.2"));
    QTest::qWait(300);
    QCOMPARE(running.size(), 0);
    QCOMPARE(m_server->boundAddress(), QHostAddress(QHostAddress::LocalHost));
}

void WebRemoteControllerTest::urlsChangedFollowsTheRestart()
{
    m_remote->setAddressResolverForTests(
        [](const QString &mode) {
            return mode == QLatin1String("lan") ? QHostAddress(QStringLiteral("127.0.0.2"))
                                                : QHostAddress(QHostAddress::LocalHost);
        },
        600000);
    m_remote->setBindMode(QStringLiteral("lan"));
    signIn();
    QVERIFY(m_remote->isRunning());
    QCOMPARE(m_remote->activeUrl(), QStringLiteral("https://127.0.0.2:18345"));
    // What Settings reads each time it is told the URL changed.
    QStringList seen;
    connect(m_remote.get(), &WebRemoteController::urlsChanged, this,
            [this, &seen] { seen.append(m_remote->activeUrl()); });
    m_remote->setBindMode(QStringLiteral("localhost"));
    QVERIFY(m_remote->isRunning());
    QVERIFY(!seen.isEmpty());
    QCOMPARE(seen.last(), QStringLiteral("https://127.0.0.1:18345"));
    // A port change as well.
    seen.clear();
    m_remote->setPort(kOtherPort);
    QVERIFY(!seen.isEmpty());
    QCOMPARE(seen.last(), QStringLiteral("https://127.0.0.1:18347"));
    disconnect(m_remote.get(), &WebRemoteController::urlsChanged, this, nullptr);
}

void WebRemoteControllerTest::failedRestartAfterRegenerateSetsError()
{
    QHostAddress lanAddress(QHostAddress::LocalHost);
    // A rebind interval far beyond the test: only regenerateCertificate() restarts.
    m_remote->setAddressResolverForTests(
        [&lanAddress](const QString &mode) {
            return mode == QLatin1String("lan") ? lanAddress : QHostAddress(QHostAddress::LocalHost);
        },
        600000);
    m_remote->setBindMode(QStringLiteral("lan"));
    signIn();
    QVERIFY(m_remote->isRunning());
    QVERIFY(m_remote->error().isEmpty());
    // The address the restart will use is taken.
    QTcpServer blocker;
    QVERIFY(blocker.listen(QHostAddress(QStringLiteral("127.0.0.2")), kPort));
    lanAddress = QHostAddress(QStringLiteral("127.0.0.2"));
    m_remote->regenerateCertificate();
    QVERIFY(!m_remote->isRunning());
    QVERIFY2(m_remote->error().contains(QStringLiteral("18345")), qPrintable(m_remote->error()));
}

QTEST_GUILESS_MAIN(WebRemoteControllerTest)
#include "tst_web_remote_controller.moc"
