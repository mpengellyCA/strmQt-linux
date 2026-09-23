#include <QCoreApplication>
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
#include <vector>

#include "MockEmbyServer.h"
#include "RemoteTestClient.h"
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
    void pinLengthMismatchIsRefused();
    void newPinSignsEveryPhoneOut();
    void keyNavigationSignal();
    void postWithoutJsonIsRefused();
    void navigationAllowlist();
    void navigationKeysMapToActions();
    void staticFilesAreWhitelisted();
    void apiSendsNoCorsHeaders();
    void statusCarriesQualityAndApp();
    void qualityWritesSettings();
    void subtitleStyleValidatesColour();
    void playbackWithoutPlayerIsUnavailable();
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
    void newPinClosesEventStreams();
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
    m_settings->setWebRemotePin(QStringLiteral("0000"));
    QCOMPARE(m_settings->webRemotePin(), QStringLiteral("0000"));
    const QString path = QStringLiteral("/api/auth/pin");

    // 5 failed attempts return 403 Forbidden
    for (int i = 0; i < 5; ++i)
        QCOMPARE(m_http->post(path, R"({"pin":"1234"})").status, 403);

    // The next attempt is refused outright, even with the right PIN.
    const RemoteResponse locked = m_http->post(path, R"({"pin":"1234"})");
    QCOMPARE(locked.status, 429);
    QCOMPARE(locked.json().value(QStringLiteral("error")).toString(),
             QStringLiteral("Too many failed attempts. Please wait."));
    QCOMPARE(m_http->post(path, R"({"pin":"0000"})").status, 429);
    now += 1999;
    QCOMPARE(m_http->post(path, R"({"pin":"0000"})").status, 429);
    now += 1;
    QCOMPARE(m_http->post(path, R"({"pin":"0000"})").status, 200);
}

void WebRemoteServerTest::pinLockoutEscalatesPerAddress()
{
    qint64 now = 5000000;
    m_server->setClockForTests([&now] { return now; });
    const auto restoreClock = qScopeGuard([this] { m_server->setClockForTests({}); });
    m_settings->setWebRemoteRequirePin(true);
    m_settings->setWebRemotePin(QStringLiteral("0000"));
    const QString path = QStringLiteral("/api/auth/pin");
    const QHostAddress other(QStringLiteral("127.0.0.2"));
    const auto fromOther = [this, &other](const QByteArray &pin) {
        const QByteArray body = "{\"pin\":\"" + pin + "\"}";
        return RemoteTestClient::statusOf(m_http->raw(
            "POST /api/auth/pin HTTP/1.1\r\nHost: x\r\nContent-Type: application/json\r\n"
            "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body,
            3000, other));
    };

    for (int i = 0; i < 5; ++i)
        QCOMPARE(m_http->post(path, R"({"pin":"1234"})").status, 403);
    QCOMPARE(m_http->post(path, R"({"pin":"1234"})").status, 429);
    // Another address is not locked out by this one's misses...
    QCOMPARE(fromOther("0000"), 200);
    // ...and its own misses count from zero.
    for (int i = 0; i < 4; ++i)
        QCOMPARE(fromOther("1234"), 403);
    QCOMPARE(fromOther("1234"), 403); // fifth miss: locked for 2 s
    QCOMPARE(fromOther("0000"), 429);

    // 127.0.0.1: each miss once a lockout has passed doubles it (2, 4, 8 s...).
    qint64 lock = 2000;
    for (int round = 0; round < 10; ++round) {
        now += lock - 1;
        QCOMPARE(m_http->post(path, R"({"pin":"0000"})").status, 429);
        now += 1;
        QCOMPARE(m_http->post(path, R"({"pin":"1234"})").status, 403);
        lock = std::min<qint64>(lock * 2, 5 * 60 * 1000);
    }
    QCOMPARE(lock, 5 * 60 * 1000); // capped at 5 min
    now += lock - 1;
    QCOMPARE(m_http->post(path, R"({"pin":"0000"})").status, 429);
    now += 1;
    // The right PIN resets the address: five fresh misses before the next lockout.
    QCOMPARE(m_http->post(path, R"({"pin":"0000"})").status, 200);
    for (int i = 0; i < 5; ++i)
        QCOMPARE(m_http->post(path, R"({"pin":"1234"})").status, 403);
    QCOMPARE(m_http->post(path, R"({"pin":"1234"})").status, 429);
    now += 2000;
    QCOMPARE(m_http->post(path, R"({"pin":"0000"})").status, 200);

    // An address idle for over 10 min is forgotten: 127.0.0.2 had five misses
    // and a lockout; after pruning it has five fresh ones.
    now += 10 * 60 * 1000 + 1;
    QCOMPARE(fromOther("1234"), 403);
    QCOMPARE(fromOther("1234"), 403);
    QCOMPARE(fromOther("0000"), 200);
}

void WebRemoteServerTest::pinLengthMismatchIsRefused()
{
    m_settings->setWebRemoteRequirePin(true);
    m_settings->setWebRemotePin(QStringLiteral("1357"));
    const QString path = QStringLiteral("/api/auth/pin");
    QCOMPARE(m_http->post(path, R"({"pin":"135"})").status, 403);
    QCOMPARE(m_http->post(path, R"({"pin":"13570"})").status, 403);
    QCOMPARE(m_http->post(path, R"({"pin":"1357"})").status, 200);
}

void WebRemoteServerTest::newPinSignsEveryPhoneOut()
{
    m_settings->setWebRemoteRequirePin(true);
    m_settings->setWebRemotePin(QStringLiteral("4321"));
    QCOMPARE(m_http->get(QStringLiteral("/api/status")).status, 401);

    const RemoteResponse auth = m_http->post(QStringLiteral("/api/auth/pin"), R"({"pin":"4321"})");
    QCOMPARE(auth.status, 200);
    const QString token = auth.json().value(QStringLiteral("token")).toString();
    QVERIFY(!token.isEmpty());
    m_http->setToken(token);
    QCOMPARE(m_http->get(QStringLiteral("/api/status")).status, 200);

    m_settings->setWebRemotePin(QStringLiteral("8765"));
    QCOMPARE(m_http->get(QStringLiteral("/api/status")).status, 401);
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
}

void WebRemoteServerTest::playbackWithoutPlayerIsUnavailable()
{
    QCOMPARE(request("POST", QStringLiteral("/api/playback"), R"({"action":"togglePause"})").status, 503);
    QCOMPARE(request("GET", QStringLiteral("/api/libraries")).status, 503);
    QCOMPARE(request("GET", QStringLiteral("/api/unknown")).status, 404);
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
    m_settings->setWebRemotePin(QStringLiteral("1122"));
    const RemoteResponse auth =
        m_http->post(QStringLiteral("/api/auth/pin"), R"({"pin":"1122"})");
    QCOMPARE(auth.status, 200);
    const QString token = auth.json().value(QStringLiteral("token")).toString();
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

void WebRemoteServerTest::newPinClosesEventStreams()
{
    m_settings->setWebRemoteRequirePin(true);
    m_settings->setWebRemotePin(QStringLiteral("4321"));
    const RemoteResponse auth = m_http->post(QStringLiteral("/api/auth/pin"), R"({"pin":"4321"})");
    QCOMPARE(auth.status, 200);
    const QString token = auth.json().value(QStringLiteral("token")).toString();
    auto events = m_http->openEvents(token);
    QVERIFY(events);
    QCOMPARE(m_server->connectedClientsCount(), 1);
    QSignalSpy changed(m_server, &WebRemoteServer::connectedClientsChanged);
    m_settings->setWebRemotePin(QStringLiteral("8765"));
    QCOMPARE(m_server->connectedClientsCount(), 0);
    QVERIFY(!changed.isEmpty());
    QCOMPARE(changed.last().first().toInt(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(events->state(), QAbstractSocket::UnconnectedState, 5000);
    QCOMPARE(m_http->get(QStringLiteral("/api/status?token=") + token).status, 401);
}

void WebRemoteServerTest::oneEventStreamPerToken()
{
    m_settings->setWebRemoteRequirePin(true);
    m_settings->setWebRemotePin(QStringLiteral("4321"));
    const auto signIn = [this] {
        return m_http->post(QStringLiteral("/api/auth/pin"), R"({"pin":"4321"})")
            .json()
            .value(QStringLiteral("token"))
            .toString();
    };
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

    // A stop (a restart, logout, server switch) signs every phone out: tokens
    // do not survive it.
    m_settings->setWebRemoteRequirePin(true);
    m_settings->setWebRemotePin(QStringLiteral("2468"));
    const RemoteResponse auth = m_http->post(QStringLiteral("/api/auth/pin"), R"({"pin":"2468"})");
    QCOMPARE(auth.status, 200);
    const QString token = auth.json().value(QStringLiteral("token")).toString();
    QVERIFY(!token.isEmpty());
    QCOMPARE(m_http->get(QStringLiteral("/api/nope?token=") + token).status, 404);
    m_server->stop();
    QVERIFY(m_server->start());
    QCOMPARE(m_http->get(QStringLiteral("/api/nope?token=") + token).status, 401);
}

QTEST_MAIN(WebRemoteServerTest)
#include "tst_web_remote_server.moc"
