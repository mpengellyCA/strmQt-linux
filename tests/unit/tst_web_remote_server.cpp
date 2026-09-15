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

private:
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

QTEST_MAIN(WebRemoteServerTest)
#include "tst_web_remote_server.moc"
