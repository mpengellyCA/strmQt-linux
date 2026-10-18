#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>
#include <utility>
#include <vector>

#include "FakePlayerBackend.h"
#include "MockEmbyServer.h"
#include "RemoteTestClient.h"
#include "app/controllers/PlayerController.h"
#include "core/Settings.h"
#include "remote/TlsCertificateGenerator.h"
#include "remote/WebRemoteServer.h"
#include "server/emby/EmbyClient.h"

using namespace strmqt;

class WebRemoteServerTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();

    void serverStartsAndListens();
    void servesStaticHtml();
    void apiStatusReturnsValidJson();
    void pinRequirementCheck();
    void pinRateLimiting();
    void pinLockoutEscalatesPerAddress();
    void pinPeerKeyGroupsWhatOneHostCanHold();
    void pinMissesAreLimitedAcrossAddresses();
    void pinLengthMismatchIsRefused();
    void removedDeviceIsSignedOut();

    // Pairing
    void codeIsShownOnRequestAndWorksOnce();
    void requestKeepsTheCodeThatIsShowing();
    void dismissedCodeHoldsOffRequests();
    void requestWithPairingOffShowsNothing();
    void codeDiesAfterFiveWrongGuesses();
    void strmLightWaitsUntilItsCodeIsTypedIn();
    void strmLightIsToldWhichSunshineToTrust();
    void strmLightCodeIsForAStrmLightOnly();
    void strmLightPollingAfterATypoIsNotGuessing();
    void strmLightIsHeldToFiftyTriesIn95Seconds();
    void strmLightCodeNeedsAPairedPhone();
    void pairedDeviceOutlivesARestart();
    void keyNavigationSignal();
    void postWithoutJsonIsRefused();
    void navigationAllowlist();
    void navigationKeysMapToActions();
    void actionsAreInvokedById();
    void staticFilesAreWhitelisted();
    void apiSendsNoCorsHeaders();
    void statusCarriesQualityAndApp();
    void qualityWritesSettings();
    void subtitleStyleValidatesColour();
    void playbackWithoutPlayerIsUnavailable();
    void nextAndPreviousStepByChapter();
    void screenshotReplyHasNoPath();
    void imageProxyServesOnlyRasterImages();

    // Request framing
    void oversizedBodyIsRefused();
    void malformedContentLengthIsRefused();
    void oversizedHeadersAre431();
    void ambiguousFramingIsRefused();
    void controlCharactersInPathAreRefused();
    void queryTokenIsGetOnly();

    // Resource limits
    void pipelinedRequestsAreBackpressured();
    void slowAndIdleClientsAreClosed();
    void connectionsAreCapped();

    // Event streams
    void eventStreamIsExemptFromTimeouts();
    void removedDeviceLosesItsEventStream();
    void oneEventStreamPerToken();
    void inputOnAnEventStreamIsIgnored();
    void streamThatStopsReadingIsDropped();

    // Last: it stops the server.
    void stopClosesOpenConnections();

private:
    struct Response {
        int status = -1;
        QByteArray body;
        QNetworkReply::NetworkError error = QNetworkReply::NoError;
        QHash<QByteArray, QByteArray> headers;
    };
    Response request(const QByteArray &method, const QString &path, const QByteArray &body = {},
                     const QByteArray &contentType = "application/json");

    QTemporaryDir m_dir;
    Settings *m_settings = nullptr;
    WebRemoteServer *m_server = nullptr;
    emby::EmbyClient *m_client = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
    std::unique_ptr<RemoteTestClient> m_http;
    int m_port = 18337;
    QSslCertificate m_cert;

    // Pairing codes are random; here the next one shown is m_nextCode.
    QString m_nextCode;
    // Puts `code` on the TV unless it is already the code showing. A code is
    // used up by a pairing and by five wrong guesses, so every guess goes
    // through this: what is being tested is then the lockout, not the code.
    void ensureCode(const QString &code)
    {
        if (m_server->pairingCode() == code)
            return;
        m_server->cancelPairing();
        m_nextCode = code;
        QVERIFY(m_server->showPairingCode());
        QCOMPARE(m_server->pairingCode(), code);
    }
    // A wrong code never given before. Only a different code is another
    // guess: the same wrong code again from the same peer is not counted.
    int m_wrongCodes = 0;
    QByteArray wrong() { return QByteArray::number(6000 + m_wrongCodes++); }
    RemoteResponse attempt(const QByteArray &pin, const QString &code)
    {
        ensureCode(code);
        return m_http->post(QStringLiteral("/api/auth/pin"), "{\"pin\":\"" + pin + "\"}");
    }
    int attemptFrom(const QHostAddress &from, const QByteArray &pin, const QString &code)
    {
        ensureCode(code);
        const QByteArray body = "{\"pin\":\"" + pin + "\"}";
        return RemoteTestClient::statusOf(m_http->raw(
            "POST /api/auth/pin HTTP/1.1\r\nHost: x\r\nContent-Type: application/json\r\n"
            "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body,
            3000, from));
    }
    // A StrmLight's try: the header, its name, and the code on its screen.
    QByteArray strmLightAttempt(const QByteArray &code, const QByteArray &name = "Living room")
    {
        const QByteArray body = "{\"pin\":\"" + code + "\"}";
        return m_http->raw(
            "POST /api/auth/pin HTTP/1.1\r\nHost: x\r\nContent-Type: application/json\r\n"
            "X-StrmLight: " + name + "\r\n"
            "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
    }
    // Pairs a phone and returns its token.
    QString pairPhone(const QByteArray &name = "Test phone")
    {
        m_server->cancelPairing();
        m_nextCode = QStringLiteral("2580");
        if (!m_server->showPairingCode())
            return {};
        return m_http->post(QStringLiteral("/api/auth/pin"),
                            "{\"pin\":\"2580\",\"name\":\"" + name + "\"}")
            .json()
            .value(QStringLiteral("token"))
            .toString();
    }
};

void WebRemoteServerTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_settings = new Settings(m_dir.filePath(QStringLiteral("test.ini")), this);
    m_settings->setWebRemotePort(m_port);
    m_settings->setWebRemoteBindMode(QStringLiteral("localhost"));
    m_settings->setWebRemoteRequirePin(false);

    // Signed out until a test signs it in against a mock Emby.
    m_client = new emby::EmbyClient(this);
    m_server = new WebRemoteServer(m_settings, nullptr, nullptr, nullptr, nullptr, m_client, this);
    QVERIFY(m_server->start());
    QVERIFY(m_server->isRunning());

    QSslKey key;
    QVERIFY(TlsCertificateGenerator::load(m_cert, key));

    m_nam = new QNetworkAccessManager(this);
    m_nam->setProxy(QNetworkProxy::NoProxy);
    connect(m_nam, &QNetworkAccessManager::sslErrors, this, [](QNetworkReply *reply, const QList<QSslError> &) {
        reply->ignoreSslErrors();
    });
    m_http = std::make_unique<RemoteTestClient>(static_cast<quint16>(m_port));
}

void WebRemoteServerTest::cleanupTestCase()
{
    m_http.reset();
    if (m_server) {
        m_server->stop();
    }
}

void WebRemoteServerTest::init()
{
    m_settings->setWebRemoteRequirePin(false);
    m_http->setToken({});
    m_server->setCodeGeneratorForTests([this] { return m_nextCode; });
    m_server->cancelPairing();
    m_server->removeAllPairedDevices();
}

void WebRemoteServerTest::serverStartsAndListens()
{
    QVERIFY(m_server != nullptr);
    QVERIFY(m_server->isRunning());
    QCOMPARE(m_server->port(), static_cast<quint16>(m_port));
}

void WebRemoteServerTest::servesStaticHtml()
{
    QNetworkRequest req(QUrl(QStringLiteral("https://127.0.0.1:%1/").arg(m_port)));
    QSslConfiguration sslConf = QSslConfiguration::defaultConfiguration();
    sslConf.setCaCertificates({m_cert});
    sslConf.setPeerVerifyMode(QSslSocket::VerifyNone);
    req.setSslConfiguration(sslConf);

    QNetworkReply *reply = m_nam->get(req);
    reply->ignoreSslErrors();

    QSignalSpy spy(reply, &QNetworkReply::finished);
    QVERIFY(spy.wait(5000));
    QCOMPARE(reply->error(), QNetworkReply::NoError);
    QVERIFY(reply->header(QNetworkRequest::ContentTypeHeader).toString().contains(QStringLiteral("text/html")));
    const QByteArray body = reply->readAll();
    QVERIFY(body.contains("<title>StrmQt Remote</title>"));
    reply->deleteLater();
}

void WebRemoteServerTest::apiStatusReturnsValidJson()
{
    QNetworkRequest req(QUrl(QStringLiteral("https://127.0.0.1:%1/api/status").arg(m_port)));
    QSslConfiguration sslConf = QSslConfiguration::defaultConfiguration();
    sslConf.setCaCertificates({m_cert});
    sslConf.setPeerVerifyMode(QSslSocket::VerifyNone);
    req.setSslConfiguration(sslConf);

    QNetworkReply *reply = m_nam->get(req);
    reply->ignoreSslErrors();
    QSignalSpy spy(reply, &QNetworkReply::finished);
    QVERIFY(spy.wait(5000));
    QCOMPARE(reply->error(), QNetworkReply::NoError);

    const QByteArray data = reply->readAll();
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    QVERIFY(doc.isObject());
    const QJsonObject obj = doc.object();
    QVERIFY(obj.contains(QStringLiteral("playback")));
    QVERIFY(obj.contains(QStringLiteral("version")));
    reply->deleteLater();
}

void WebRemoteServerTest::pinRequirementCheck()
{
    QNetworkRequest req(QUrl(QStringLiteral("https://127.0.0.1:%1/api/auth/pin").arg(m_port)));
    QSslConfiguration sslConf = QSslConfiguration::defaultConfiguration();
    sslConf.setCaCertificates({m_cert});
    sslConf.setPeerVerifyMode(QSslSocket::VerifyNone);
    req.setSslConfiguration(sslConf);

    QNetworkReply *reply = m_nam->get(req);
    reply->ignoreSslErrors();
    QSignalSpy spy(reply, &QNetworkReply::finished);
    QVERIFY(spy.wait(5000));
    QCOMPARE(reply->error(), QNetworkReply::NoError);

    const QByteArray data = reply->readAll();
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    QVERIFY(doc.isObject());
    QCOMPARE(doc.object().value(QStringLiteral("required")).toBool(), false);
    reply->deleteLater();
}

void WebRemoteServerTest::pinRateLimiting()
{
    qint64 now = 1000000;
    m_server->setClockForTests([&now] { return now; });
    const auto restoreClock = qScopeGuard([this] { m_server->setClockForTests({}); });
    const QString code = QStringLiteral("0000");

    // 5 failed attempts return 403 Forbidden
    for (int i = 0; i < 5; ++i)
        QCOMPARE(attempt(wrong(), code).status, 403);

    // The next attempt is refused outright, even with the right code.
    const RemoteResponse locked = attempt(wrong(), code);
    QCOMPARE(locked.status, 429);
    QCOMPARE(locked.json().value(QStringLiteral("error")).toString(),
             QStringLiteral("Too many failed attempts. Please wait."));
    QCOMPARE(attempt("0000", code).status, 429);
    now += 1999;
    QCOMPARE(attempt("0000", code).status, 429);
    now += 1;
    QCOMPARE(attempt("0000", code).status, 200);
}

void WebRemoteServerTest::pinLockoutEscalatesPerAddress()
{
    qint64 now = 5000000;
    m_server->setClockForTests([&now] { return now; });
    const auto restoreClock = qScopeGuard([this] { m_server->setClockForTests({}); });
    m_settings->setWebRemoteRequirePin(true);
    const QString code = QStringLiteral("0000");
    const QHostAddress other(QStringLiteral("127.0.0.2"));
    const auto fromOther = [this, &other, &code](const QByteArray &pin) {
        return attemptFrom(other, pin, code);
    };

    for (int i = 0; i < 5; ++i)
        QCOMPARE(attempt(wrong(), code).status, 403);
    QCOMPARE(attempt(wrong(), code).status, 429);
    // Another address is not locked out by this one's misses...
    QCOMPARE(fromOther("0000"), 200);
    // ...and its own misses count from zero.
    for (int i = 0; i < 4; ++i)
        QCOMPARE(fromOther(wrong()), 403);
    QCOMPARE(fromOther(wrong()), 403); // fifth miss: locked for 2 s
    QCOMPARE(fromOther("0000"), 429);

    // 127.0.0.1: each miss once a lockout has passed doubles it (2, 4, 8 s...).
    qint64 lock = 2000;
    for (int round = 0; round < 10; ++round) {
        now += lock - 1;
        QCOMPARE(attempt("0000", code).status, 429);
        now += 1;
        QCOMPARE(attempt(wrong(), code).status, 403);
        lock = std::min<qint64>(lock * 2, 5 * 60 * 1000);
    }
    QCOMPARE(lock, 5 * 60 * 1000); // capped at 5 min
    now += lock - 1;
    QCOMPARE(attempt("0000", code).status, 429);
    now += 1;
    // The right code resets the address: five fresh misses before the next lockout.
    QCOMPARE(attempt("0000", code).status, 200);
    for (int i = 0; i < 5; ++i)
        QCOMPARE(attempt(wrong(), code).status, 403);
    QCOMPARE(attempt(wrong(), code).status, 429);
    now += 2000;
    QCOMPARE(attempt("0000", code).status, 200);

    // An address idle for over 10 min is forgotten: 127.0.0.2 had five misses
    // and a lockout; after pruning it has five fresh ones.
    now += 10 * 60 * 1000 + 1;
    QCOMPARE(fromOther(wrong()), 403);
    QCOMPARE(fromOther(wrong()), 403);
    QCOMPARE(fromOther("0000"), 200);
}

void WebRemoteServerTest::pinPeerKeyGroupsWhatOneHostCanHold()
{
    const auto key = [](const char *address) {
        return WebRemoteServer::pinPeerKey(QHostAddress(QString::fromLatin1(address)));
    };
    // IPv4 by address; the IPv4-mapped IPv6 form is the same peer.
    QCOMPARE(key("192.168.1.5"), QStringLiteral("192.168.1.5"));
    QCOMPARE(key("::ffff:192.168.1.5"), key("192.168.1.5"));
    QVERIFY(key("192.168.1.6") != key("192.168.1.5"));
    // IPv6 by /64: every address a host can give itself inside its prefix
    // (AUDIT §5.2.2), however it is spelled.
    QCOMPARE(key("2001:db8:1:2::1"), key("2001:db8:1:2:ffff:ffff:ffff:ffff"));
    QCOMPARE(key("2001:DB8:1:2:0:0:0:9"), key("2001:db8:1:2::1"));
    QVERIFY(key("2001:db8:1:3::1") != key("2001:db8:1:2::1"));
    QVERIFY(key("2001:db8:1:2::1") != key("192.168.1.5"));
    QCOMPARE(key("::1"), key("::2"));

    // On this computer's own link the /64 is the whole LAN: one guesser must
    // not lock out every phone on it, so there the address is the peer. A
    // link-local address is always on the link; anything else is when it is
    // in the /64 of the address the connection arrived on.
    const auto via = [](const char *address, const char *local) {
        return WebRemoteServer::pinPeerKey(QHostAddress(QString::fromLatin1(address)),
                                           QHostAddress(QString::fromLatin1(local)));
    };
    // The scope id names an interface of this computer, not the peer.
    QCOMPARE(key("fe80::1%eth0"), key("fe80::1%wlan0"));
    QVERIFY(key("fe80::1%eth0") != key("fe80::2%eth0"));
    QVERIFY(via("fe80::1", "fe80::99") != via("fe80::2", "fe80::99"));
    QVERIFY(via("2001:db8:1:2::1", "2001:db8:1:2::99") !=
            via("2001:db8:1:2::5", "2001:db8:1:2::99"));
    QVERIFY(via("fd12:3456:789a:1::1", "fd12:3456:789a:1::99") !=
            via("fd12:3456:789a:1::5", "fd12:3456:789a:1::99"));
    QCOMPARE(via("2001:db8:1:2::1", "2001:db8:1:2::99"),
             via("2001:DB8:1:2:0:0:0:1", "2001:db8:1:2::99"));
    // From another prefix it is still the /64, whatever this end is.
    QCOMPARE(via("2001:db8:9:9::1", "2001:db8:1:2::99"),
             via("2001:db8:9:9::2", "2001:db8:1:2::99"));
    QCOMPARE(via("2001:db8:9:9::1", "192.168.1.2"), key("2001:db8:9:9::2"));
    // IPv4 is the address either way.
    QCOMPARE(via("::ffff:192.168.1.5", "::ffff:192.168.1.2"), key("192.168.1.5"));
}

void WebRemoteServerTest::pinMissesAreLimitedAcrossAddresses()
{
    qint64 now = 90000000;
    m_server->setClockForTests([&now] { return now; });
    const auto restoreClock = qScopeGuard([this] { m_server->setClockForTests({}); });
    m_settings->setWebRemoteRequirePin(true);
    QString code = QStringLiteral("2468");
    const auto from = [this, &code](int host, const QByteArray &pin) {
        return attemptFrom(QHostAddress(QStringLiteral("127.0.1.%1").arg(host)), pin, code);
    };

    // The phone that is already paired (127.0.0.1).
    QCOMPARE(attempt("2468", code).status, 200);

    // Five addresses, four misses each: none reaches its own lockout.
    for (int host = 1; host <= 5; ++host) {
        for (int i = 0; i < 4; ++i)
            QCOMPARE(from(host, wrong()), 403);
    }
    // Twenty misses between them: a sixth address is not even heard out...
    QCOMPARE(from(6, "2468"), 429);
    QCOMPARE(from(6, wrong()), 429);
    // ...nor is one of the five, which still had a try of its own left.
    QCOMPARE(from(1, "2468"), 429);
    // The phone that paired before is not locked out with them, and answers
    // for its own misses only.
    QCOMPARE(attempt("2468", code).status, 200);
    QCOMPARE(attempt(wrong(), code).status, 403);
    QCOMPARE(attempt("2468", code).status, 200);
    QCOMPARE(from(6, "2468"), 429); // and its success forgives the others nothing

    // One try comes back every 5 min, whoever takes it.
    now += 5 * 60 * 1000 - 1;
    QCOMPARE(from(6, "2468"), 429);
    now += 1;
    QCOMPARE(from(6, wrong()), 403);
    QCOMPARE(from(7, "2468"), 429);
    now += 5 * 60 * 1000;
    QCOMPARE(from(7, "2468"), 200);
    // 127.0.1.7 has paired; the others are still one miss from the limit.
    QCOMPARE(from(8, wrong()), 403);
    QCOMPARE(from(9, "2468"), 429);
    QCOMPARE(from(7, "2468"), 200);

    // Removing every paired device at the TV clears it for everyone.
    m_server->removeAllPairedDevices();
    code = QStringLiteral("9753");
    QCOMPARE(from(9, "9753"), 200);
}

void WebRemoteServerTest::pinLengthMismatchIsRefused()
{
    m_settings->setWebRemoteRequirePin(true);
    const QString code = QStringLiteral("1357");
    QCOMPARE(attempt("135", code).status, 403);
    QCOMPARE(attempt("13570", code).status, 403);
    QCOMPARE(attempt("1357", code).status, 200);
}

void WebRemoteServerTest::removedDeviceIsSignedOut()
{
    m_settings->setWebRemoteRequirePin(true);
    QCOMPARE(m_http->get(QStringLiteral("/api/status")).status, 401);

    const QString token = pairPhone("Kitchen phone");
    QVERIFY(!token.isEmpty());
    m_http->setToken(token);
    QCOMPARE(m_http->get(QStringLiteral("/api/status")).status, 200);

    const QVariantList devices = m_server->pairedDevices();
    QCOMPARE(devices.size(), 1);
    const QVariantMap device = devices.first().toMap();
    QCOMPARE(device.value(QStringLiteral("name")).toString(), QStringLiteral("Kitchen phone"));
    QCOMPARE(device.value(QStringLiteral("kind")).toString(), QStringLiteral("phone"));
    QVERIFY(!device.contains(QStringLiteral("hash"))); // QML never sees it

    m_server->removePairedDevice(device.value(QStringLiteral("id")).toString());
    QCOMPARE(m_http->get(QStringLiteral("/api/status")).status, 401);
}

void WebRemoteServerTest::codeIsShownOnRequestAndWorksOnce()
{
    m_settings->setWebRemoteRequirePin(true);
    m_nextCode = QStringLiteral("4821");
    QSignalSpy pairing(m_server, &WebRemoteServer::pairingChanged);
    QSignalSpy paired(m_server, &WebRemoteServer::devicePaired);

    const RemoteResponse asked = m_http->post(QStringLiteral("/api/auth/request"), "{}");
    QCOMPARE(asked.status, 202);
    QVERIFY(asked.json().value(QStringLiteral("expiresIn")).toInt() > 0);
    QCOMPARE(m_server->pairingCode(), QStringLiteral("4821"));
    QCOMPARE(pairing.size(), 1);
    QCOMPARE(m_http->get(QStringLiteral("/api/auth/pin")).json().value(QStringLiteral("pairing")).toString(),
             QStringLiteral("shown"));

    const RemoteResponse auth =
        m_http->post(QStringLiteral("/api/auth/pin"), R"({"pin":"4821","name":"Test phone"})");
    QCOMPARE(auth.status, 200);
    QVERIFY(!auth.json().value(QStringLiteral("token")).toString().isEmpty());
    // Off the TV once used.
    QVERIFY(m_server->pairingCode().isEmpty());
    QCOMPARE(pairing.size(), 2);
    QCOMPARE(paired.size(), 1);
    QCOMPARE(paired.first().at(0).toString(), QStringLiteral("Test phone"));
    QCOMPARE(paired.first().at(1).toString(), QStringLiteral("phone"));

    // One code, one device.
    const RemoteResponse again =
        m_http->post(QStringLiteral("/api/auth/pin"), R"({"pin":"4821"})");
    QCOMPARE(again.status, 403);
    QCOMPARE(again.json().value(QStringLiteral("error")).toString(),
             QStringLiteral("No code is showing on the TV."));
}

void WebRemoteServerTest::requestKeepsTheCodeThatIsShowing()
{
    m_settings->setWebRemoteRequirePin(true);
    m_nextCode = QStringLiteral("1111");
    QCOMPARE(m_http->post(QStringLiteral("/api/auth/request"), "{}").status, 202);
    m_nextCode = QStringLiteral("2222");
    QCOMPARE(m_http->post(QStringLiteral("/api/auth/request"), "{}").status, 202);
    // Asking again cannot cycle codes until one is easy to guess.
    QCOMPARE(m_server->pairingCode(), QStringLiteral("1111"));
}

void WebRemoteServerTest::dismissedCodeHoldsOffRequests()
{
    qint64 now = 70000000;
    m_server->setClockForTests([&now] { return now; });
    const auto restoreClock = qScopeGuard([this] { m_server->setClockForTests({}); });
    m_settings->setWebRemoteRequirePin(true);
    m_nextCode = QStringLiteral("3141");
    QCOMPARE(m_http->post(QStringLiteral("/api/auth/request"), "{}").status, 202);
    m_server->cancelPairing(); // Back, at the TV
    QVERIFY(m_server->pairingCode().isEmpty());
    QCOMPARE(m_http->post(QStringLiteral("/api/auth/request"), "{}").status, 429);
    QVERIFY(m_server->pairingCode().isEmpty());
    now += 30 * 1000;
    QCOMPARE(m_http->post(QStringLiteral("/api/auth/request"), "{}").status, 202);
    QCOMPARE(m_server->pairingCode(), QStringLiteral("3141"));
}

void WebRemoteServerTest::requestWithPairingOffShowsNothing()
{
    m_settings->setWebRemoteRequirePin(false);
    const RemoteResponse asked = m_http->post(QStringLiteral("/api/auth/request"), "{}");
    QCOMPARE(asked.status, 200);
    QCOMPARE(asked.json().value(QStringLiteral("required")).toBool(true), false);
    QVERIFY(m_server->pairingCode().isEmpty());
}

void WebRemoteServerTest::codeDiesAfterFiveWrongGuesses()
{
    m_settings->setWebRemoteRequirePin(true);
    m_nextCode = QStringLiteral("8080");
    QVERIFY(m_server->showPairingCode());
    // From five addresses, so no one of them is locked out by its own misses.
    for (int host = 1; host <= 4; ++host) {
        QCOMPARE(RemoteTestClient::statusOf(m_http->raw(
                     "POST /api/auth/pin HTTP/1.1\r\nHost: x\r\nContent-Type: application/json\r\n"
                     "Content-Length: 12\r\n\r\n{\"pin\":\"1111\"}",
                     3000, QHostAddress(QStringLiteral("127.0.2.%1").arg(host)))),
                 403);
    }
    QCOMPARE(m_server->pairingCode(), QStringLiteral("8080"));
    QSignalSpy pairing(m_server, &WebRemoteServer::pairingChanged);
    QCOMPARE(RemoteTestClient::statusOf(m_http->raw(
                 "POST /api/auth/pin HTTP/1.1\r\nHost: x\r\nContent-Type: application/json\r\n"
                 "Content-Length: 12\r\n\r\n{\"pin\":\"1111\"}",
                 3000, QHostAddress(QStringLiteral("127.0.2.5")))),
             403);
    QVERIFY(m_server->pairingCode().isEmpty());
    QCOMPARE(pairing.size(), 1);
    // The right code is no good now: there is no code.
    QCOMPARE(m_http->post(QStringLiteral("/api/auth/pin"), R"({"pin":"8080"})").status, 403);
}

void WebRemoteServerTest::strmLightWaitsUntilItsCodeIsTypedIn()
{
    m_settings->setWebRemoteRequirePin(true);
    m_http->setToken(pairPhone());

    // Every 2 s from the moment its code is on its screen: told to wait, and
    // never locked out for it.
    for (int i = 0; i < 12; ++i) {
        const QByteArray reply = strmLightAttempt("5555");
        QCOMPARE(RemoteTestClient::statusOf(reply), 202);
        QVERIFY(reply.contains("\"pending\":true"));
    }

    // Typed in on the paired phone (Settings does the same through the
    // controller). Nothing goes on the TV: the StrmLight shows the code.
    QSignalSpy pairing(m_server, &WebRemoteServer::pairingChanged);
    QCOMPARE(m_http->post(QStringLiteral("/api/auth/strmlight"), R"({"code":"5555"})").status, 200);
    QCOMPARE(pairing.size(), 1);
    QVERIFY(m_server->awaitingStrmLight());
    QVERIFY(m_server->pairingCode().isEmpty());

    QSignalSpy paired(m_server, &WebRemoteServer::devicePaired);
    const QByteArray reply = strmLightAttempt("5555", "Living room");
    QCOMPARE(RemoteTestClient::statusOf(reply), 200);
    const QJsonObject body = QJsonDocument::fromJson(reply.mid(reply.indexOf("\r\n\r\n") + 4)).object();
    const QString token = body.value(QStringLiteral("token")).toString();
    QVERIFY(!token.isEmpty());
    QVERIFY(!m_server->awaitingStrmLight());
    QCOMPARE(paired.size(), 1);
    QCOMPARE(paired.first().at(0).toString(), QStringLiteral("Living room"));
    QCOMPARE(paired.first().at(1).toString(), QStringLiteral("strmlight"));

    bool listed = false;
    for (const QVariant &device : m_server->pairedDevices()) {
        const QVariantMap map = device.toMap();
        listed = listed || (map.value(QStringLiteral("kind")).toString() == QStringLiteral("strmlight")
                            && map.value(QStringLiteral("name")).toString() == QStringLiteral("Living room"));
    }
    QVERIFY(listed);
    m_http->setToken(token);
    QCOMPARE(m_http->get(QStringLiteral("/api/status")).status, 200);
}

void WebRemoteServerTest::strmLightIsToldWhichSunshineToTrust()
{
    const QString sunshine = QStringLiteral("14f04bd951e34c3fa35a09d06245b868c540bf1da58dada259de769e24e46a62");
    m_server->setSunshineFingerprint([sunshine] { return sunshine; });
    const auto cleanup = qScopeGuard([this] { m_server->setSunshineFingerprint({}); });
    m_settings->setWebRemoteRequirePin(true);
    const QString phone = pairPhone();
    m_http->setToken(phone);
    QCOMPARE(m_http->post(QStringLiteral("/api/auth/strmlight"), R"({"code":"6161"})").status, 200);

    // In the answer that pairs it, over the remote's pinned certificate.
    const QByteArray reply = strmLightAttempt("6161");
    QCOMPARE(RemoteTestClient::statusOf(reply), 200);
    const QJsonObject body = QJsonDocument::fromJson(reply.mid(reply.indexOf("\r\n\r\n") + 4)).object();
    QCOMPARE(body.value(QStringLiteral("sunshineFingerprint")).toString(), sunshine);
    const QString token = body.value(QStringLiteral("token")).toString();
    QVERIFY(!token.isEmpty());

    // And again whenever a paired one asks.
    m_http->setToken(token);
    const auto host = m_http->get(QStringLiteral("/api/strmlight/host"));
    QCOMPARE(host.status, 200);
    QCOMPARE(host.json().value(QStringLiteral("sunshineFingerprint")).toString(), sunshine);

    // Not to anyone else, even with the PIN setting off.
    m_settings->setWebRemoteRequirePin(false);
    m_http->setToken(QString());
    QCOMPARE(m_http->get(QStringLiteral("/api/strmlight/host")).status, 401);

    // A phone pairs as before, and is not told.
    m_server->cancelPairing();
    m_nextCode = QStringLiteral("2580");
    QVERIFY(m_server->showPairingCode());
    m_http->setToken(QString());
    const auto paired = m_http->post(QStringLiteral("/api/auth/pin"), R"({"pin":"2580","name":"Other phone"})");
    QCOMPARE(paired.status, 200);
    QVERIFY(!paired.json().contains(QStringLiteral("sunshineFingerprint")));
}

void WebRemoteServerTest::strmLightCodeIsForAStrmLightOnly()
{
    m_settings->setWebRemoteRequirePin(true);
    // A code put on the TV for a phone is not a StrmLight's: it keeps waiting.
    ensureCode(QStringLiteral("7777"));
    QCOMPARE(RemoteTestClient::statusOf(strmLightAttempt("7777")), 202);
    QCOMPARE(m_server->pairingCode(), QStringLiteral("7777"));

    // And a StrmLight's code is no use without the header.
    QVERIFY(m_server->expectStrmLightCode(QStringLiteral("6060")));
    QVERIFY(m_server->pairingCode().isEmpty()); // it replaced the shown one
    const RemoteResponse phone = m_http->post(QStringLiteral("/api/auth/pin"), R"({"pin":"6060"})");
    QCOMPARE(phone.status, 403);
    QVERIFY(m_server->awaitingStrmLight());
    // A phone cannot ask for a code over it either.
    QCOMPARE(m_http->post(QStringLiteral("/api/auth/request"), "{}").status, 409);
    // A StrmLight with the wrong code is a guess like any other.
    QCOMPARE(RemoteTestClient::statusOf(strmLightAttempt("6061")), 403);
    QCOMPARE(RemoteTestClient::statusOf(strmLightAttempt("6060")), 200);
}

void WebRemoteServerTest::strmLightPollingAfterATypoIsNotGuessing()
{
    m_settings->setWebRemoteRequirePin(true);
    m_http->setToken(pairPhone());

    // Typed in wrong: the StrmLight shows 5555, and goes on asking with it.
    QVERIFY(m_server->expectStrmLightCode(QStringLiteral("5556")));
    for (int i = 0; i < 30; ++i)
        QCOMPARE(RemoteTestClient::statusOf(strmLightAttempt("5555")), 403);
    // One guess, not thirty: the typed code is still there, and the StrmLight
    // is not locked out.
    QVERIFY(m_server->awaitingStrmLight());

    // Typed in again, right this time: its next try gets in.
    QVERIFY(m_server->expectStrmLightCode(QStringLiteral("5555")));
    QCOMPARE(RemoteTestClient::statusOf(strmLightAttempt("5555")), 200);

    // A different code each time is guessing, and five of them end the code.
    QVERIFY(m_server->expectStrmLightCode(QStringLiteral("7777")));
    for (int i = 0; i < 5; ++i)
        QCOMPARE(RemoteTestClient::statusOf(strmLightAttempt(wrong())), 403);
    QVERIFY(!m_server->awaitingStrmLight());
}

void WebRemoteServerTest::strmLightIsHeldToFiftyTriesIn95Seconds()
{
    qint64 now = 20000000;
    m_server->setClockForTests([&now] { return now; });
    const auto restoreClock = qScopeGuard([this] { m_server->setClockForTests({}); });
    m_settings->setWebRemoteRequirePin(true);
    const auto tryAt = [this](const QByteArray &name = "Living room") {
        return RemoteTestClient::statusOf(strmLightAttempt("5555", name));
    };

    // Every 2 s, as a StrmLight asks, and every 1.9 s: never refused.
    for (int i = 0; i < 60; ++i, now += 2000)
        QCOMPARE(tryAt(), 202);
    now += 95000;
    for (int i = 0; i < 120; ++i, now += 1900)
        QCOMPARE(tryAt(), 202);

    // Fifty at once are let through, and the fifty-first is not...
    now += 95000;
    for (int i = 0; i < 50; ++i)
        QCOMPARE(tryAt(), 202);
    QCOMPARE(tryAt(), 429);
    // ...whatever name it gives: the limit is the address's.
    QCOMPARE(tryAt("Bedroom"), 429);
    // A phone from the same address is not held to it.
    QCOMPARE(m_http->post(QStringLiteral("/api/auth/pin"), R"({"pin":"1234"})").status, 403);
    // The window moves: 95 s after the burst, the tries are back.
    now += 94999;
    QCOMPARE(tryAt(), 429);
    now += 1;
    QCOMPARE(tryAt(), 202);

    // Faster than 1.9 s on average is refused within the window.
    now += 95000;
    int refused = 0;
    for (int i = 0; i < 60; ++i, now += 1800)
        refused += tryAt() == 429;
    QVERIFY(refused > 0);
}

void WebRemoteServerTest::strmLightCodeNeedsAPairedPhone()
{
    m_settings->setWebRemoteRequirePin(true);
    QCOMPARE(m_http->post(QStringLiteral("/api/auth/strmlight"), R"({"code":"1234"})").status, 401);
    QVERIFY(!m_server->awaitingStrmLight());
    m_http->setToken(pairPhone());
    QCOMPARE(m_http->post(QStringLiteral("/api/auth/strmlight"), R"({"code":"12a4"})").status, 400);
    QCOMPARE(m_http->post(QStringLiteral("/api/auth/strmlight"), R"({"code":"12345"})").status, 400);
    QVERIFY(!m_server->awaitingStrmLight());
    QVERIFY(!m_server->expectStrmLightCode(QStringLiteral("")));
}

void WebRemoteServerTest::pairedDeviceOutlivesARestart()
{
    m_settings->setWebRemoteRequirePin(true);
    const QString token = pairPhone();
    QVERIFY(!token.isEmpty());
    m_server->stop();
    QVERIFY(m_server->start());
    m_http->setToken(token);
    QCOMPARE(m_http->get(QStringLiteral("/api/status")).status, 200);

    // Only a hash of the token is written down.
    m_settings->flush();
    QFile ini(m_dir.filePath(QStringLiteral("test.ini")));
    QVERIFY(ini.open(QIODevice::ReadOnly));
    const QByteArray stored = ini.readAll();
    QVERIFY(stored.contains("Test phone"));
    QVERIFY(!stored.contains(token.toUtf8()));
}

void WebRemoteServerTest::keyNavigationSignal()
{
    QSignalSpy navSpy(m_server, &WebRemoteServer::actionRequested);

    QNetworkRequest req(QUrl(QStringLiteral("https://127.0.0.1:%1/api/navigate").arg(m_port)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QSslConfiguration sslConf = QSslConfiguration::defaultConfiguration();
    sslConf.setCaCertificates({m_cert});
    sslConf.setPeerVerifyMode(QSslSocket::VerifyNone);
    req.setSslConfiguration(sslConf);

    QJsonObject body;
    body.insert(QStringLiteral("key"), QStringLiteral("down"));
    QNetworkReply *reply = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    reply->ignoreSslErrors();
    QSignalSpy spy(reply, &QNetworkReply::finished);
    QVERIFY(spy.wait(5000));
    QCOMPARE(reply->error(), QNetworkReply::NoError);

    QCOMPARE(navSpy.count(), 1);
    QCOMPARE(navSpy.first().first().toString(), QStringLiteral("nav.down"));
    reply->deleteLater();
}

WebRemoteServerTest::Response WebRemoteServerTest::request(const QByteArray &method,
                                                           const QString &path,
                                                           const QByteArray &body,
                                                           const QByteArray &contentType)
{
    QNetworkRequest req(QUrl(QStringLiteral("https://127.0.0.1:%1%2").arg(m_port).arg(path)));
    QSslConfiguration sslConf = QSslConfiguration::defaultConfiguration();
    sslConf.setCaCertificates({m_cert});
    sslConf.setPeerVerifyMode(QSslSocket::VerifyNone);
    req.setSslConfiguration(sslConf);
    if (method == "POST" && !contentType.isEmpty())
        req.setRawHeader("Content-Type", contentType);

    QNetworkReply *reply = method == "POST" ? m_nam->post(req, body) : m_nam->get(req);
    reply->ignoreSslErrors();
    QSignalSpy spy(reply, &QNetworkReply::finished);
    Response res;
    if (!spy.wait(5000)) {
        reply->deleteLater();
        return res;
    }
    res.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    res.error = reply->error();
    res.body = reply->readAll();
    for (const auto &pair : reply->rawHeaderPairs())
        res.headers.insert(pair.first.toLower(), pair.second);
    reply->deleteLater();
    return res;
}

void WebRemoteServerTest::postWithoutJsonIsRefused()
{
    // A cross-site <form> can post text/plain without a preflight; it must not
    // reach a handler.
    QSignalSpy navSpy(m_server, &WebRemoteServer::actionRequested);
    const Response res = request("POST", QStringLiteral("/api/navigate"),
                                 R"({"key":"select"})", "text/plain");
    QCOMPARE(res.status, 415);
    QCOMPARE(navSpy.count(), 0);
}

void WebRemoteServerTest::navigationAllowlist()
{
    QSignalSpy keySpy(m_server, &WebRemoteServer::actionRequested);
    QSignalSpy destSpy(m_server, &WebRemoteServer::navigationRequested);

    QCOMPARE(request("POST", QStringLiteral("/api/navigate"), R"({"key":"format-disk"})").status, 400);
    QCOMPARE(keySpy.count(), 0);

    for (const QString &key : WebRemoteServer::navigationKeys()) {
        const QByteArray body = QJsonDocument(QJsonObject{{QStringLiteral("key"), key}}).toJson();
        QCOMPARE(request("POST", QStringLiteral("/api/navigate"), body).status, 200);
    }
    QCOMPARE(keySpy.count(), WebRemoteServer::navigationKeys().size());

    QCOMPARE(request("POST", QStringLiteral("/api/navigate"), R"({"destination":"settings"})").status, 200);
    QCOMPARE(destSpy.count(), 1);
    QCOMPARE(destSpy.first().first().toString(), QStringLiteral("settings"));

    const int actionsBefore = keySpy.count();
    QCOMPARE(request("POST", QStringLiteral("/api/navigate"), R"({"destination":"osd"})").status, 200);
    QCOMPARE(keySpy.count(), actionsBefore + 1);
    QCOMPARE(keySpy.last().first().toString(), QStringLiteral("player.toggleOsd"));
    QCOMPARE(destSpy.count(), 1);
}

void WebRemoteServerTest::actionsAreInvokedById()
{
    QSignalSpy spy(m_server, &WebRemoteServer::actionRequested);

    // No catalogue, no actions: the allowlist is what was handed over.
    m_server->setActions({});
    QCOMPARE(request("POST", QStringLiteral("/api/action"), R"({"action":"player.toggleView"})").status, 400);

    m_server->setActions({QStringLiteral("player.toggleView"), QStringLiteral("nav.nextLetter")});
    QCOMPARE(request("POST", QStringLiteral("/api/action"), R"({"action":"player.toggleView"})").status, 200);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.last().first().toString(), QStringLiteral("player.toggleView"));

    QCOMPARE(request("POST", QStringLiteral("/api/action"), R"({"action":"app.quit"})").status, 400);
    QCOMPARE(request("POST", QStringLiteral("/api/action"), R"({"action":""})").status, 400);
    QCOMPARE(request("POST", QStringLiteral("/api/action"), R"({"key":"up"})").status, 400);
    QCOMPARE(request("POST", QStringLiteral("/api/action"), R"({"action":"player.toggleView"})",
                     "text/plain").status, 415);
    QCOMPARE(spy.count(), 1);
}

void WebRemoteServerTest::navigationKeysMapToActions()
{
    for (const QString &key : WebRemoteServer::navigationKeys())
        QVERIFY2(!WebRemoteServer::actionForNavigationKey(key).isEmpty(), qPrintable(key));
    QCOMPARE(WebRemoteServer::actionForNavigationKey(QStringLiteral("back")), QStringLiteral("nav.back"));
    QCOMPARE(WebRemoteServer::actionForNavigationKey(QStringLiteral("menu")), QStringLiteral("nav.contextMenu"));
    QCOMPARE(WebRemoteServer::actionForNavigationKey(QStringLiteral("prevTab")), QStringLiteral("nav.previousTab"));
    QVERIFY(WebRemoteServer::actionForNavigationKey(QStringLiteral("nav.back")).isEmpty());
}

void WebRemoteServerTest::staticFilesAreWhitelisted()
{
    const Response js = request("GET", QStringLiteral("/app.js"));
    QCOMPARE(js.status, 200);
    QVERIFY(js.headers.value("content-type").startsWith("text/javascript"));
    // Shipped inside the binary: an upgrade must not leave a phone on stale script.
    QCOMPARE(js.headers.value("cache-control"), QByteArray("no-cache"));

    QCOMPARE(request("GET", QStringLiteral("/index.html")).status, 200);
    QCOMPARE(request("GET", QStringLiteral("/nope.js")).status, 404);
    // Only flat names under the remote's own prefix resolve.
    QCOMPARE(request("GET", QStringLiteral("/%2e%2e/fonts/x.ttf")).status, 404);
    QCOMPARE(request("GET", QStringLiteral("/sub/app.js")).status, 404);
    QCOMPARE(request("GET", QStringLiteral("/App.JS")).status, 404);
}

void WebRemoteServerTest::apiSendsNoCorsHeaders()
{
    const Response res = request("GET", QStringLiteral("/api/status"));
    QCOMPARE(res.status, 200);
    QVERIFY(!res.headers.contains("access-control-allow-origin"));
    QCOMPARE(res.headers.value("x-content-type-options"), QByteArray("nosniff"));
}

void WebRemoteServerTest::statusCarriesQualityAndApp()
{
    m_settings->setMaxBitrateKbps(20000);
    m_server->setInteractionContext(QStringLiteral("browse"));
    const Response res = request("GET", QStringLiteral("/api/status"));
    QCOMPARE(res.status, 200);
    const QJsonObject obj = QJsonDocument::fromJson(res.body).object();
    QCOMPARE(obj.value(QStringLiteral("quality")).toObject().value(QStringLiteral("maxBitrateKbps")).toInt(), 20000);
    const QJsonObject app = obj.value(QStringLiteral("app")).toObject();
    QCOMPARE(app.value(QStringLiteral("context")).toString(), QStringLiteral("browse"));
    // Nothing is playing, so there is no view to toggle whatever is on top.
    m_server->setPlayerOnTop(true);
    QCOMPARE(QJsonDocument::fromJson(request("GET", QStringLiteral("/api/status")).body)
                 .object().value(QStringLiteral("app")).toObject()
                 .value(QStringLiteral("playerView")).toString(),
             QStringLiteral("none"));
    m_server->setPlayerOnTop(false);
    QVERIFY(app.value(QStringLiteral("accent")).toString().startsWith(QLatin1Char('#')));
    QVERIFY(obj.contains(QStringLiteral("subtitleStyle")));
    QCOMPARE(obj.value(QStringLiteral("playback")).toObject().value(QStringLiteral("active")).toBool(), false);
}

void WebRemoteServerTest::qualityWritesSettings()
{
    QCOMPARE(request("POST", QStringLiteral("/api/quality"),
                     R"({"maxBitrateKbps":4000,"playbackMode":"transcode"})").status, 200);
    QCOMPARE(m_settings->maxBitrateKbps(), 4000);
    QCOMPARE(m_settings->playbackMode(), QStringLiteral("transcode"));

    QCOMPARE(request("POST", QStringLiteral("/api/quality"), R"({"playbackMode":"warp"})").status, 400);
    QCOMPARE(m_settings->playbackMode(), QStringLiteral("transcode"));
    QCOMPARE(request("POST", QStringLiteral("/api/quality"), R"({"maxBitrateKbps":-5})").status, 400);
    QCOMPARE(m_settings->maxBitrateKbps(), 4000);
}

void WebRemoteServerTest::subtitleStyleValidatesColour()
{
    QCOMPARE(request("POST", QStringLiteral("/api/subtitles/style"), R"({"color":"#FFE066"})").status, 200);
    QCOMPARE(m_settings->subtitleColor().toUpper(), QStringLiteral("#FFE066"));
    QCOMPARE(request("POST", QStringLiteral("/api/subtitles/style"), R"({"color":"red;x"})").status, 400);
    QCOMPARE(m_settings->subtitleColor().toUpper(), QStringLiteral("#FFE066"));

    // #FFE066 is the yellow an older list offered: drawn as stored, shown
    // picked as the Yellow that replaced it, as the desktop does.
    const QJsonObject style = QJsonDocument::fromJson(request("GET", QStringLiteral("/api/status")).body)
                                  .object()
                                  .value(QStringLiteral("subtitleStyle"))
                                  .toObject();
    QCOMPARE(style.value(QStringLiteral("color")).toString(), QStringLiteral("#FFE066"));
    QCOMPARE(style.value(QStringLiteral("swatch")).toString(), QStringLiteral("#F2E14C"));
    QCOMPARE(Settings::subtitleColorSwatch(QStringLiteral("#bfbfbf")), QStringLiteral("#A0A0A0"));
    QCOMPARE(Settings::subtitleColorSwatch(QStringLiteral("#123456")), QStringLiteral("#123456"));
}

void WebRemoteServerTest::playbackWithoutPlayerIsUnavailable()
{
    QCOMPARE(request("POST", QStringLiteral("/api/playback"), R"({"action":"togglePause"})").status, 503);
    QCOMPARE(request("GET", QStringLiteral("/api/libraries")).status, 503);
    QCOMPARE(request("GET", QStringLiteral("/api/unknown")).status, 404);
}

// spec 2026-09-27 §9.3: the web remote's Next/Previous are the same verbs as
// the remote keys, the pad and MPRIS: skipForward/skipBack, which step by
// chapter when the item has chapters.
void WebRemoteServerTest::nextAndPreviousStepByChapter()
{
    // A playing film with three chapters (0 s, 600 s, 1200 s), positioned at
    // 700 s, wired up the way tst_player_skip's chapter tests are.
    const QString userId = QStringLiteral("a1b2c3d4e5f60718293a4b5c6d7e8f90");
    const QString itemId = QStringLiteral("301001");

    MockEmbyServer emby;
    QVERIFY(emby.start());
    QVERIFY(emby.addRouteFromFile(QStringLiteral("POST"),
                                  QStringLiteral("/Items/%1/PlaybackInfo").arg(itemId),
                                  QStringLiteral(STRMQT_FIXTURES_DIR "/playback_info.json")));
    emby.addRoute(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing"), 204, {});
    emby.addRoute(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing/Progress"), 204, {});
    emby.addRoute(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing/Stopped"), 204, {});
    emby.addRoute(QStringLiteral("GET"), QStringLiteral("/Users/%1/Items/%2").arg(userId, itemId), 200,
                 QByteArrayLiteral(R"json({
        "Id": "301001", "Name": "301001", "Type": "Movie",
        "Chapters": [
            {"Name": "One", "StartPositionTicks": 0},
            {"Name": "Two", "StartPositionTicks": 6000000000},
            {"Name": "Three", "StartPositionTicks": 12000000000}
        ]
    })json"));

    emby::EmbyClient client;
    client.setBaseUrl(emby.baseUrl());
    client.setDeviceId(QStringLiteral("test-device"));
    client.setSession(QStringLiteral("not-a-real-token-fixture-only"), userId);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Settings settings(dir.filePath(QStringLiteral("settings.ini")));
    settings.setWebRemotePort(m_port + 1);
    settings.setWebRemoteBindMode(QStringLiteral("localhost"));
    settings.setWebRemoteRequirePin(false);

    FakePlayerBackend backend;
    PlayerController controller(&client, &backend, &settings);

    WebRemoteServer server(&settings, &controller, nullptr, nullptr, nullptr, &client);
    QVERIFY(server.start());
    const auto stopServer = qScopeGuard([&server] { server.stop(); });

    RemoteTestClient http(static_cast<quint16>(m_port + 1));

    QVariantMap item;
    item.insert(QStringLiteral("itemId"), itemId);
    item.insert(QStringLiteral("name"), itemId);
    item.insert(QStringLiteral("label"), itemId);
    item.insert(QStringLiteral("type"), QStringLiteral("Movie"));
    item.insert(QStringLiteral("runtimeMs"), 1800'000);

    controller.playQueue({item}, 0);
    QTRY_COMPARE(backend.loadedUrls.size(), 1);
    backend.simulateState(PlayerBackend::State::Playing);
    backend.simulateDuration(1800'000);
    QTRY_COMPARE(controller.chapters().size(), 3);
    backend.simulatePosition(700'000);

    // The page enables ⏮ / ⏭ from these, so a film with chapters left keeps
    // Next live even with nothing queued after it.
    const auto skipState = [&http] {
        const QJsonObject playback = QJsonDocument::fromJson(http.get(QStringLiteral("/api/status")).body)
                                         .object().value(QStringLiteral("playback")).toObject();
        return std::pair{playback.value(QStringLiteral("canSkipForward")).toBool(),
                         playback.value(QStringLiteral("canSkipBack")).toBool()};
    };
    QTRY_COMPARE(skipState(), std::pair(true, true));

    QCOMPARE(http.post(QStringLiteral("/api/playback"), R"({"action":"next"})").status, 200);
    QCOMPARE(backend.seeks.constLast(), Q_INT64_C(1200'000)); // next chapter, not the next item
    QCOMPARE(backend.loadedUrls.size(), 1);                   // no new item was loaded

    QCOMPARE(http.post(QStringLiteral("/api/playback"), R"({"action":"previous"})").status, 200);
    // Exactly at chapter three's start: skipBack's rule is "restart the current
    // chapter past the window, else step back" — at 0 into it, that is chapter two.
    QCOMPARE(backend.seeks.constLast(), Q_INT64_C(600'000));
}

// AUDIT §5.2.3: where the screenshot went (/home/<user>/Pictures/...) is not
// the phone's business, and the page never read it.
void WebRemoteServerTest::screenshotReplyHasNoPath()
{
    const QString userId = QStringLiteral("a1b2c3d4e5f60718293a4b5c6d7e8f90");
    const QString itemId = QStringLiteral("301001");

    MockEmbyServer emby;
    QVERIFY(emby.start());
    QVERIFY(emby.addRouteFromFile(QStringLiteral("POST"),
                                  QStringLiteral("/Items/%1/PlaybackInfo").arg(itemId),
                                  QStringLiteral(STRMQT_FIXTURES_DIR "/playback_info.json")));
    emby.addRoute(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing"), 204, {});
    emby.addRoute(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing/Progress"), 204, {});
    emby.addRoute(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing/Stopped"), 204, {});

    emby::EmbyClient client;
    client.setBaseUrl(emby.baseUrl());
    client.setDeviceId(QStringLiteral("test-device"));
    client.setSession(QStringLiteral("not-a-real-token-fixture-only"), userId);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Settings settings(dir.filePath(QStringLiteral("settings.ini")));
    settings.setWebRemotePort(m_port + 1);
    settings.setWebRemoteBindMode(QStringLiteral("localhost"));
    settings.setWebRemoteRequirePin(false);

    FakePlayerBackend backend;
    PlayerController controller(&client, &backend, &settings);
    controller.setScreenshotDirectoryForTests(dir.filePath(QStringLiteral("shots")));
    WebRemoteServer server(&settings, &controller, nullptr, nullptr, nullptr, &client);
    QVERIFY(server.start());
    const auto stopServer = qScopeGuard([&server] { server.stop(); });
    RemoteTestClient http(static_cast<quint16>(m_port + 1));

    QVariantMap item;
    item.insert(QStringLiteral("itemId"), itemId);
    item.insert(QStringLiteral("name"), itemId);
    item.insert(QStringLiteral("label"), itemId);
    item.insert(QStringLiteral("type"), QStringLiteral("Movie"));
    item.insert(QStringLiteral("runtimeMs"), 1800'000);
    controller.playQueue({item}, 0);
    QTRY_COMPARE(backend.loadedUrls.size(), 1);
    backend.simulateState(PlayerBackend::State::Playing);
    QTRY_VERIFY(controller.active());

    const RemoteResponse taken = http.post(QStringLiteral("/api/playback"), R"({"action":"screenshot"})");
    QCOMPARE(taken.status, 200);
    QCOMPARE(backend.screenshots.size(), 1); // it was taken, on this computer
    QVERIFY(backend.screenshots.constFirst().startsWith(dir.path()));
    const QJsonObject reply = taken.json();
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(!reply.contains(QStringLiteral("path")));
    QVERIFY(!taken.body.contains(dir.path().toUtf8()));

    backend.screenshotSucceeds = false;
    const RemoteResponse failed = http.post(QStringLiteral("/api/playback"), R"({"action":"screenshot"})");
    QCOMPARE(failed.status, 500);
    QCOMPARE(failed.json().value(QStringLiteral("ok")).toBool(), false);
    QVERIFY(!failed.json().contains(QStringLiteral("path")));
}

void WebRemoteServerTest::imageProxyServesOnlyRasterImages()
{
    MockEmbyServer emby;
    QVERIFY(emby.start());
    m_client->setBaseUrl(emby.baseUrl());
    m_client->setSession(QStringLiteral("image-proxy-token"), QStringLiteral("user"));
    const auto signOut = qScopeGuard([this] { m_client->setSession({}, {}); });

    // A PNG the upstream mislabels: typed by its bytes, not by the label.
    QByteArray png("\x89PNG\r\n\x1a\n", 8);
    png += QByteArray(64, '\0');
    emby.addRoute(QStringLiteral("GET"), QStringLiteral("/Items/301001/Images/Primary"), 200, png,
                  "text/html");
    const RemoteResponse image = m_http->get(QStringLiteral("/api/image/301001/Primary?w=400"));
    QCOMPARE(image.status, 200);
    QCOMPARE(image.body, png);
    QCOMPARE(image.headers.value("content-type"), QByteArray("image/png"));
    QCOMPARE(image.headers.value("content-security-policy"),
             QByteArray("default-src 'none'; sandbox"));
    QCOMPARE(image.headers.value("x-content-type-options"), QByteArray("nosniff"));
    // The key goes to Emby in a header and never back to the phone.
    QCOMPARE(emby.lastRequestFor(QStringLiteral("GET"), QStringLiteral("/Items/301001/Images/Primary"))
                 .headers.value("x-emby-token"),
             QByteArray("image-proxy-token"));
    QVERIFY(!image.body.contains("image-proxy-token"));
    for (const QByteArray &value : image.headers)
        QVERIFY(!value.contains("image-proxy-token"));

    // An SVG can carry script that would run on the remote's own origin.
    emby.addRoute(QStringLiteral("GET"), QStringLiteral("/Items/301002/Images/Primary"), 200,
                  "<svg xmlns=\"http://www.w3.org/2000/svg\"><script>alert(1)</script></svg>",
                  "image/svg+xml");
    QCOMPARE(m_http->get(QStringLiteral("/api/image/301002/Primary")).status, 502);
    // So can a login page from a proxy in front of Emby, whatever it is labelled.
    emby.addRoute(QStringLiteral("GET"), QStringLiteral("/Items/301003/Images/Primary"), 200,
                  "<!DOCTYPE html><html><body>Sign in</body></html>", "image/jpeg");
    QCOMPARE(m_http->get(QStringLiteral("/api/image/301003/Primary")).status, 502);

    // Ids and types in the forms Emby issues; nothing else reaches the path.
    emby.addRoute(QStringLiteral("GET"),
                  QStringLiteral("/Items/6c17e0e282e84f7c92e8358ebf054111/Images/Backdrop"), 200,
                  png, "image/png");
    QCOMPARE(m_http->get(QStringLiteral("/api/image/6c17e0e282e84f7c92e8358ebf054111/Backdrop")).status,
             200);
    const int before = emby.requestCount();
    QCOMPARE(m_http->get(QStringLiteral("/api/image/301001/Script")).status, 400);
    QCOMPARE(RemoteTestClient::statusOf(m_http->raw(
                 "GET /api/image/..%2F..%2FUsers/Primary HTTP/1.1\r\nHost: x\r\n\r\n")),
             404);
    QCOMPARE(m_http->get(QStringLiteral("/api/image/301001%20x/Primary")).status, 400);
    QCOMPARE(m_http->get(QStringLiteral("/api/image/abc_def/Primary")).status, 400);
    QCOMPARE(emby.requestCount(), before);
}

void WebRemoteServerTest::oversizedBodyIsRefused()
{
    QSignalSpy nav(m_server, &WebRemoteServer::actionRequested);
    // Refused on the header alone: the body is never read.
    const QByteArray res = m_http->raw("POST /api/navigate HTTP/1.1\r\nHost: x\r\n"
                                       "Content-Type: application/json\r\n"
                                       "Content-Length: 1048577\r\n\r\n{\"key\":\"up\"");
    QCOMPARE(RemoteTestClient::statusOf(res), 413);
    // The cap exactly is still accepted.
    QByteArray body = "{\"key\":\"up\",\"pad\":\"";
    body += QByteArray(1024 * 1024 - body.size() - 2, 'a');
    body += "\"}";
    QCOMPARE(body.size(), 1024 * 1024);
    QCOMPARE(m_http->post(QStringLiteral("/api/navigate"), body).status, 200);
    QCOMPARE(nav.size(), 1);
}

void WebRemoteServerTest::malformedContentLengthIsRefused()
{
    QSignalSpy nav(m_server, &WebRemoteServer::actionRequested);
    QCOMPARE(RemoteTestClient::statusOf(m_http->raw("POST /api/navigate HTTP/1.1\r\nHost: x\r\n"
                                                    "Content-Type: application/json\r\n"
                                                    "Content-Length: 99999999999999999999\r\n\r\n")),
             400);
    QCOMPARE(RemoteTestClient::statusOf(m_http->raw("POST /api/navigate HTTP/1.1\r\nHost: x\r\n"
                                                    "Content-Type: application/json\r\n"
                                                    "Content-Length: -5\r\n\r\n")),
             400);
    QCOMPARE(nav.size(), 0);
}

void WebRemoteServerTest::oversizedHeadersAre431()
{
    const QByteArray res = m_http->raw("GET / HTTP/1.1\r\nX-Pad: " + QByteArray(70 * 1024, 'a'));
    QCOMPARE(RemoteTestClient::statusOf(res), 431);
}

void WebRemoteServerTest::ambiguousFramingIsRefused()
{
    QSignalSpy nav(m_server, &WebRemoteServer::actionRequested);
    // Two lengths: which one frames the body is anyone's guess.
    QCOMPARE(RemoteTestClient::statusOf(m_http->raw("POST /api/navigate HTTP/1.1\r\nHost: x\r\n"
                                                    "Content-Type: application/json\r\n"
                                                    "Content-Length: 0\r\nContent-Length: 12\r\n\r\n"
                                                    "{\"key\":\"up\"}")),
             400);
    // Chunked bodies are not understood, so they are not guessed at.
    QCOMPARE(RemoteTestClient::statusOf(m_http->raw("POST /api/navigate HTTP/1.1\r\nHost: x\r\n"
                                                    "Content-Type: application/json\r\n"
                                                    "Transfer-Encoding: chunked\r\n\r\n"
                                                    "c\r\n{\"key\":\"up\"}\r\n0\r\n\r\n")),
             501);
    QCOMPARE(nav.size(), 0);
}

void WebRemoteServerTest::controlCharactersInPathAreRefused()
{
    QSignalSpy nav(m_server, &WebRemoteServer::actionRequested);
    QCOMPARE(RemoteTestClient::statusOf(m_http->raw("POST /api/navigate%0A HTTP/1.1\r\nHost: x\r\n"
                                                    "Content-Type: application/json\r\n"
                                                    "Content-Length: 12\r\n\r\n{\"key\":\"up\"}")),
             400);
    QCOMPARE(RemoteTestClient::statusOf(m_http->raw("GET /style.css%00 HTTP/1.1\r\nHost: x\r\n\r\n")), 400);
    QCOMPARE(RemoteTestClient::statusOf(m_http->raw("GET /style.css%7F HTTP/1.1\r\nHost: x\r\n\r\n")), 400);
    // A newline before the end of an id must not pass an id pattern either.
    QCOMPARE(RemoteTestClient::statusOf(m_http->raw("GET /api/item/123%0A HTTP/1.1\r\nHost: x\r\n\r\n")), 400);
    QCOMPARE(nav.size(), 0);
}

void WebRemoteServerTest::queryTokenIsGetOnly()
{
    m_settings->setWebRemoteRequirePin(true);
    const QString token = pairPhone();
    QVERIFY(!token.isEmpty());
    QSignalSpy nav(m_server, &WebRemoteServer::actionRequested);
    QCOMPARE(m_http->post(QStringLiteral("/api/navigate?token=") + token, R"({"key":"up"})").status,
             401);
    QCOMPARE(nav.size(), 0);
    // EventSource and <img> cannot set headers: on a GET the token may ride in the query.
    QCOMPARE(m_http->get(QStringLiteral("/api/nope?token=") + token).status, 404);
    QCOMPARE(m_http->get(QStringLiteral("/api/nope?token=wrong")).status, 401);
    m_http->setToken(token);
    QCOMPARE(m_http->post(QStringLiteral("/api/navigate"), R"({"key":"up"})").status, 200);
    QCOMPARE(nav.size(), 1);
}

void WebRemoteServerTest::pipelinedRequestsAreBackpressured()
{
    const qsizetype cssSize = m_http->get(QStringLiteral("/style.css")).body.size();
    QVERIFY(cssSize > 0);
    auto socket = m_http->connectTls();
    QVERIFY(socket);
    // Qt would otherwise drain the kernel buffer into memory on our behalf.
    socket->setReadBufferSize(4096);
    QByteArray burst;
    for (int i = 0; i < 2000; ++i)
        burst += "GET /style.css HTTP/1.1\r\nHost: x\r\n\r\n";
    socket->write(burst); // and never read a byte back
    // Plaintext not yet encrypted plus ciphertext not yet sent: on a TLS
    // socket bytesToWrite() alone drops to 0 once Qt has encrypted it.
    const auto backlog = [this] {
        qint64 most = 0;
        for (QSslSocket *s : m_server->findChildren<QSslSocket *>())
            most = qMax(most, s->bytesToWrite() + s->encryptedBytesToWrite());
        return most;
    };
    // The server fills the kernel buffers, then stops parsing: its own
    // backlog stays at the pause threshold plus at most one response.
    QTRY_VERIFY_WITH_TIMEOUT(backlog() > 256 * 1024, 10000);
    QTest::qWait(200);
    QVERIFY2(backlog() <= 256 * 1024 + cssSize + 4096, QByteArray::number(backlog()).constData());
}

void WebRemoteServerTest::slowAndIdleClientsAreClosed()
{
    m_server->setTimeoutsForTests(300, 600);
    const auto restore = qScopeGuard([this] { m_server->setTimeoutsForTests(20000, 60000); });
    // Headers trickling in never complete: closed on the request timeout,
    // however often a byte arrives.
    auto slow = m_http->connectTls();
    QVERIFY(slow);
    slow->write("GET / HTTP/1.1\r\n");
    QElapsedTimer timer;
    timer.start();
    while (slow->state() == QAbstractSocket::ConnectedState && timer.elapsed() < 5000) {
        slow->write("X");
        QTest::qWait(50);
    }
    QCOMPARE(slow->state(), QAbstractSocket::UnconnectedState);
    QVERIFY(timer.elapsed() < 2000);
    // A keep-alive connection with nothing to say is closed once idle.
    auto idle = m_http->connectTls();
    QVERIFY(idle);
    idle->write("GET /api/auth/pin HTTP/1.1\r\nHost: x\r\n\r\n");
    QTRY_VERIFY_WITH_TIMEOUT(idle->bytesAvailable() > 0, 5000);
    idle->readAll();
    QTRY_COMPARE_WITH_TIMEOUT(idle->state(), QAbstractSocket::UnconnectedState, 5000);
    // A body promised and never sent is a request that never completes.
    auto stalled = m_http->connectTls();
    QVERIFY(stalled);
    stalled->write("POST /api/navigate HTTP/1.1\r\nHost: x\r\nContent-Type: application/json\r\n"
                   "Content-Length: 65536\r\n\r\n{");
    QTRY_COMPARE_WITH_TIMEOUT(stalled->state(), QAbstractSocket::UnconnectedState, 5000);
}

void WebRemoteServerTest::connectionsAreCapped()
{
    // Both clients drop their keep-alive connections.
    m_http = std::make_unique<RemoteTestClient>(static_cast<quint16>(m_port));
    delete m_nam;
    m_nam = new QNetworkAccessManager(this);
    m_nam->setProxy(QNetworkProxy::NoProxy);
    const auto live = [this] {
        int n = 0;
        for (QSslSocket *s : m_server->findChildren<QSslSocket *>())
            n += s->state() == QAbstractSocket::ConnectedState ? 1 : 0;
        return n;
    };
    QTRY_COMPARE_WITH_TIMEOUT(live(), 0, 5000);
    const QByteArray ping = "GET /api/auth/pin HTTP/1.1\r\nHost: x\r\n\r\n";
    std::vector<std::unique_ptr<QSslSocket>> sockets;
    for (int i = 0; i < 32; ++i) {
        auto s = m_http->connectTls();
        QVERIFY(s);
        s->write(ping);
        QTRY_VERIFY_WITH_TIMEOUT(s->bytesAvailable() > 0, 5000);
        s->readAll();
        sockets.push_back(std::move(s));
    }
    auto extra = m_http->connectTls();
    if (extra) // the handshake may finish before the server drops it
        QTRY_COMPARE_WITH_TIMEOUT(extra->state(), QAbstractSocket::UnconnectedState, 5000);
    for (const auto &s : sockets)
        QCOMPARE(s->state(), QAbstractSocket::ConnectedState);
    // A slot frees up when a connection closes.
    sockets.pop_back();
    QTRY_COMPARE_WITH_TIMEOUT(live(), 31, 5000);
    auto next = m_http->connectTls();
    QVERIFY(next);
    next->write(ping);
    QTRY_VERIFY_WITH_TIMEOUT(next->bytesAvailable() > 0, 5000);
}

void WebRemoteServerTest::eventStreamIsExemptFromTimeouts()
{
    m_server->setTimeoutsForTests(200, 200);
    const auto restore = qScopeGuard([this] { m_server->setTimeoutsForTests(20000, 60000); });
    auto events = m_http->openEvents();
    QVERIFY(events);
    // Several idle and request timeouts pass with nothing but silence.
    RemoteTestClient::collect(events.get(), 1000);
    QCOMPARE(events->state(), QAbstractSocket::ConnectedState);
    QCOMPARE(m_server->connectedClientsCount(), 1);
    m_server->setInteractionContext(QStringLiteral("exempt-check"));
    const QByteArray got = RemoteTestClient::collect(events.get(), 600);
    QVERIFY2(got.contains("exempt-check"), got.constData());
    events->abort();
    QTRY_COMPARE_WITH_TIMEOUT(m_server->connectedClientsCount(), 0, 5000);
}

void WebRemoteServerTest::removedDeviceLosesItsEventStream()
{
    m_settings->setWebRemoteRequirePin(true);
    const QString token = pairPhone();
    QVERIFY(!token.isEmpty());
    auto events = m_http->openEvents(token);
    QVERIFY(events);
    QCOMPARE(m_server->connectedClientsCount(), 1);
    QSignalSpy changed(m_server, &WebRemoteServer::connectedClientsChanged);
    const QString id =
        m_server->pairedDevices().first().toMap().value(QStringLiteral("id")).toString();
    m_server->removePairedDevice(id);
    QCOMPARE(m_server->connectedClientsCount(), 0);
    QVERIFY(!changed.isEmpty());
    QCOMPARE(changed.last().first().toInt(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(events->state(), QAbstractSocket::UnconnectedState, 5000);
    QCOMPARE(m_http->get(QStringLiteral("/api/status?token=") + token).status, 401);
}

void WebRemoteServerTest::oneEventStreamPerToken()
{
    m_settings->setWebRemoteRequirePin(true);
    const auto signIn = [this] { return pairPhone(); };
    const QString token = signIn();
    const QString otherToken = signIn();
    QVERIFY(!token.isEmpty() && !otherToken.isEmpty());
    auto first = m_http->openEvents(token);
    QVERIFY(first);
    auto other = m_http->openEvents(otherToken);
    QVERIFY(other);
    QCOMPARE(m_server->connectedClientsCount(), 2);
    QSignalSpy changed(m_server, &WebRemoteServer::connectedClientsChanged);
    // The same phone again (a reload), this time with the header: the old stream goes.
    auto second = m_http->openEvents(token, true);
    QVERIFY(second);
    QTRY_COMPARE_WITH_TIMEOUT(first->state(), QAbstractSocket::UnconnectedState, 5000);
    QCOMPARE(m_server->connectedClientsCount(), 2);
    QCOMPARE(changed.size(), 0); // replaced in place: the count never moved
    QCOMPARE(second->state(), QAbstractSocket::ConnectedState);
    QCOMPARE(other->state(), QAbstractSocket::ConnectedState);
    // The replacement stream is live.
    m_server->setInteractionContext(QStringLiteral("replacement-live"));
    QVERIFY(RemoteTestClient::collect(second.get(), 600).contains("replacement-live"));
    other->abort();
    second->abort();
    QTRY_COMPARE_WITH_TIMEOUT(m_server->connectedClientsCount(), 0, 5000);
}

void WebRemoteServerTest::inputOnAnEventStreamIsIgnored()
{
    auto socket = m_http->connectTls();
    QVERIFY(socket);
    // A request pipelined behind the stream's own, and one sent later.
    socket->write("GET /api/events HTTP/1.1\r\nHost: x\r\n\r\n"
                  "GET /api/status HTTP/1.1\r\nHost: x\r\n\r\n");
    QByteArray got = RemoteTestClient::collect(socket.get(), 400);
    QVERIFY(got.contains("event: queue"));
    socket->write("GET /api/queue HTTP/1.1\r\nHost: x\r\n\r\n");
    got += RemoteTestClient::collect(socket.get(), 400);
    QCOMPARE(RemoteTestClient::countOf(got, "HTTP/1.1 "), 1);
    QCOMPARE(socket->state(), QAbstractSocket::ConnectedState);
    QCOMPARE(m_server->connectedClientsCount(), 1);
    socket->abort();
    QTRY_COMPARE_WITH_TIMEOUT(m_server->connectedClientsCount(), 0, 5000);
}

void WebRemoteServerTest::streamThatStopsReadingIsDropped()
{
    auto events = m_http->openEvents();
    QVERIFY(events);
    // The phone stops reading: Qt must not drain the kernel buffer for it.
    events->setReadBufferSize(4096);
    // Every context change is one more status event of about 1 MiB. Once the
    // kernel buffers are full, the server's own backlog grows past the cap
    // and the stream is dropped rather than buffered without bound.
    const QString big(1024 * 1024, QLatin1Char('x'));
    int step = 0;
    QTRY_VERIFY_WITH_TIMEOUT((m_server->setInteractionContext(big + QString::number(++step % 2)),
                              m_server->connectedClientsCount() == 0),
                             30000);
    m_server->setInteractionContext({});
}

void WebRemoteServerTest::stopClosesOpenConnections()
{
    auto socket = m_http->connectTls();
    QVERIFY(socket);
    socket->write("GET /api/auth/pin HTTP/1.1\r\nHost: x\r\n\r\n");
    QTRY_VERIFY_WITH_TIMEOUT(socket->bytesAvailable() > 0, 5000); // a live keep-alive connection
    socket->readAll();
    QSignalSpy running(m_server, &WebRemoteServer::runningChanged);
    m_server->stop();
    QVERIFY(!m_server->isRunning());
    QVERIFY(m_server->boundAddress().isNull());
    QCOMPARE(running.size(), 1);
    QCOMPARE(running.first().first().toBool(), false);
    QTRY_COMPARE_WITH_TIMEOUT(socket->state(), QAbstractSocket::UnconnectedState, 5000);
    m_server->stop(); // idempotent: no second signal
    QCOMPARE(running.size(), 1);
    QVERIFY(m_server->start());
    QCOMPARE(m_server->boundAddress(), QHostAddress(QHostAddress::LocalHost));
    QCOMPARE(m_http->get(QStringLiteral("/api/auth/pin")).status, 200);

    // A stop (a restart, logout, server switch) keeps paired devices paired,
    // but a code that was on the TV does not stay up.
    m_settings->setWebRemoteRequirePin(true);
    const QString token = pairPhone();
    QVERIFY(!token.isEmpty());
    QCOMPARE(m_http->get(QStringLiteral("/api/nope?token=") + token).status, 404);
    ensureCode(QStringLiteral("2468"));
    m_server->stop();
    QVERIFY(m_server->pairingCode().isEmpty());
    QVERIFY(m_server->start());
    QCOMPARE(m_http->get(QStringLiteral("/api/nope?token=") + token).status, 404);
}

QTEST_MAIN(WebRemoteServerTest)
#include "tst_web_remote_server.moc"
