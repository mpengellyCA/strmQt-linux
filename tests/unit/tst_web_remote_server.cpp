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
#include <QTemporaryDir>
#include <QtTest>

#include "core/Settings.h"
#include "remote/TlsCertificateGenerator.h"
#include "remote/WebRemoteServer.h"

using namespace strmqt;

class WebRemoteServerTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void serverStartsAndListens();
    void servesStaticHtml();
    void apiStatusReturnsValidJson();
    void pinRequirementCheck();
    void pinAuthAndRateLimiting();
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
    QNetworkAccessManager *m_nam = nullptr;
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

    m_server = new WebRemoteServer(m_settings, nullptr, nullptr, nullptr, nullptr, nullptr, this);
    QVERIFY(m_server->start());
    QVERIFY(m_server->isRunning());

    QSslKey key;
    QVERIFY(TlsCertificateGenerator::load(m_cert, key));

    m_nam = new QNetworkAccessManager(this);
    m_nam->setProxy(QNetworkProxy::NoProxy);
    connect(m_nam, &QNetworkAccessManager::sslErrors, this, [](QNetworkReply *reply, const QList<QSslError> &) {
        reply->ignoreSslErrors();
    });
}

void WebRemoteServerTest::cleanupTestCase()
{
    if (m_server) {
        m_server->stop();
    }
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

void WebRemoteServerTest::pinAuthAndRateLimiting()
{
    m_settings->setWebRemotePin(QStringLiteral("0000"));
    QCOMPARE(m_settings->webRemotePin(), QStringLiteral("0000"));

    QSslConfiguration sslConf = QSslConfiguration::defaultConfiguration();
    sslConf.setCaCertificates({m_cert});
    sslConf.setPeerVerifyMode(QSslSocket::VerifyNone);

    auto sendPin = [&](const QString &pin) -> int {
        QNetworkRequest req(QUrl(QStringLiteral("https://127.0.0.1:%1/api/auth/pin").arg(m_port)));
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        req.setSslConfiguration(sslConf);

        QJsonObject body;
        body.insert(QStringLiteral("pin"), pin);
        QNetworkReply *reply = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
        reply->ignoreSslErrors();
        QSignalSpy spy(reply, &QNetworkReply::finished);
        if (!spy.wait(5000)) {
            reply->deleteLater();
            return -1;
        }
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        return statusCode;
    };

    // 5 failed attempts return 403 Forbidden
    for (int i = 0; i < 5; ++i) {
        QCOMPARE(sendPin(QStringLiteral("1234")), 403);
    }

    // 6th attempt immediately returns 429 Too Many Requests
    QCOMPARE(sendPin(QStringLiteral("1234")), 429);

    // Wait 2100 ms for the rate limit window to expire
    QTest::qWait(2100);

    // Valid PIN "0000" succeeds with 200 OK
    QCOMPARE(sendPin(QStringLiteral("0000")), 200);
}

void WebRemoteServerTest::keyNavigationSignal()
{
    QSignalSpy navSpy(m_server, &WebRemoteServer::keyNavigationRequested);

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
    QCOMPARE(navSpy.first().first().toString(), QStringLiteral("down"));
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
    QSignalSpy navSpy(m_server, &WebRemoteServer::keyNavigationRequested);
    const Response res = request("POST", QStringLiteral("/api/navigate"),
                                 R"({"key":"select"})", "text/plain");
    QCOMPARE(res.status, 415);
    QCOMPARE(navSpy.count(), 0);
}

void WebRemoteServerTest::navigationAllowlist()
{
    QSignalSpy keySpy(m_server, &WebRemoteServer::keyNavigationRequested);
    QSignalSpy destSpy(m_server, &WebRemoteServer::navigationRequested);
    QSignalSpy osdSpy(m_server, &WebRemoteServer::osdToggleRequested);

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

    QCOMPARE(request("POST", QStringLiteral("/api/navigate"), R"({"destination":"osd"})").status, 200);
    QCOMPARE(osdSpy.count(), 1);
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

QTEST_MAIN(WebRemoteServerTest)
#include "tst_web_remote_server.moc"
