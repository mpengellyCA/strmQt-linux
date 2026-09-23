#pragma once

// HTTPS client for the phone-remote tests. Talks to WebRemoteServer on
// 127.0.0.1 with certificate checks off (the certificate is self-signed).
// Everything here spins the event loop instead of blocking, because the
// server under test runs on the same thread.

#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QtTest>

#include <memory>

struct RemoteResponse {
    int status = -1; // -1: no reply within the timeout
    QByteArray body;
    QHash<QByteArray, QByteArray> headers; // names lower-cased
    QJsonObject json() const { return QJsonDocument::fromJson(body).object(); }
};

class RemoteTestClient
{
public:
    explicit RemoteTestClient(quint16 port) : m_port(port)
    {
        m_nam.setProxy(QNetworkProxy::NoProxy);
    }
    void setToken(const QString &token) { m_token = token; }

    RemoteResponse get(const QString &path) { return request("GET", path, {}, {}); }
    RemoteResponse post(const QString &path, const QByteArray &body,
                        const QByteArray &contentType = "application/json")
    {
        return request("POST", path, body, contentType);
    }

    RemoteResponse request(const QByteArray &method, const QString &path, const QByteArray &body,
                           const QByteArray &contentType)
    {
        QNetworkRequest req(QUrl(QStringLiteral("https://127.0.0.1:%1%2").arg(m_port).arg(path)));
        QSslConfiguration ssl = QSslConfiguration::defaultConfiguration();
        ssl.setPeerVerifyMode(QSslSocket::VerifyNone);
        req.setSslConfiguration(ssl);
        req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
        if (!m_token.isEmpty())
            req.setRawHeader("Authorization", "Bearer " + m_token.toUtf8());
        if (!contentType.isEmpty())
            req.setRawHeader("Content-Type", contentType);
        QNetworkReply *reply = method == "GET"    ? m_nam.get(req)
                               : method == "POST" ? m_nam.post(req, body)
                                                  : m_nam.sendCustomRequest(req, method, body);
        reply->ignoreSslErrors();
        RemoteResponse res;
        QSignalSpy finished(reply, &QNetworkReply::finished);
        if (!reply->isFinished() && !finished.wait(10000)) {
            reply->abort();
            reply->deleteLater();
            return res;
        }
        res.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        res.body = reply->readAll();
        for (const auto &pair : reply->rawHeaderPairs())
            res.headers.insert(pair.first.toLower(), pair.second);
        reply->deleteLater();
        return res;
    }

    // Sends bytes exactly as given (no client-side URL normalisation) and
    // returns what the server wrote until the response is complete, the
    // server closes, or waitMs passes. `from` binds the client end to another
    // loopback address (127.0.0.2), so the server sees a second peer.
    QByteArray raw(const QByteArray &bytes, int waitMs = 3000, const QHostAddress &from = {})
    {
        auto socket = connectTls(from);
        if (!socket)
            return {};
        socket->write(bytes);
        QByteArray out;
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < waitMs) {
            QTest::qWait(20);
            out += socket->readAll();
            if (responseComplete(out) || socket->state() != QAbstractSocket::ConnectedState)
                break;
        }
        return out + socket->readAll();
    }
    static int statusOf(const QByteArray &raw)
    {
        return raw.startsWith("HTTP/1.1 ") ? raw.mid(9, 3).toInt() : -1;
    }

    // An open TLS connection to the server (nothing sent yet).
    std::unique_ptr<QSslSocket> connectTls(const QHostAddress &from = {})
    {
        auto socket = std::make_unique<QSslSocket>();
        socket->setPeerVerifyMode(QSslSocket::VerifyNone);
        socket->setProxy(QNetworkProxy::NoProxy);
        if (!from.isNull() && !socket->bind(from, 0))
            return nullptr;
        QSignalSpy encrypted(socket.get(), &QSslSocket::encrypted);
        socket->connectToHostEncrypted(QStringLiteral("127.0.0.1"), m_port);
        if (!encrypted.wait(5000))
            return nullptr;
        return socket;
    }

    // Opens /api/events and returns the socket once the first `queue` event
    // (sent right after the first `status`) has arrived. With `asHeader`, the
    // token goes in an Authorization header, not the query.
    std::unique_ptr<QSslSocket> openEvents(const QString &token = {}, bool asHeader = false)
    {
        auto socket = connectTls();
        if (!socket)
            return nullptr;
        const QByteArray path = token.isEmpty() || asHeader
                                    ? QByteArray("/api/events")
                                    : "/api/events?token=" + token.toUtf8();
        const QByteArray auth =
            asHeader ? "Authorization: Bearer " + token.toUtf8() + "\r\n" : QByteArray();
        socket->write("GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\n" + auth
                      + "Accept: text/event-stream\r\n\r\n");
        QByteArray preamble;
        QElapsedTimer timer;
        timer.start();
        while (!preamble.contains("event: queue") && timer.elapsed() < 5000) {
            QTest::qWait(20);
            preamble += socket->readAll();
        }
        return preamble.contains("event: queue") ? std::move(socket) : nullptr;
    }

    // What an open event stream receives over the next `ms` milliseconds.
    static QByteArray collect(QSslSocket *socket, int ms)
    {
        QByteArray got;
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < ms) {
            QTest::qWait(20);
            got += socket->readAll();
        }
        return got + socket->readAll();
    }
    static int countOf(const QByteArray &hay, const QByteArray &needle)
    {
        int n = 0;
        for (qsizetype at = hay.indexOf(needle); at >= 0;
             at = hay.indexOf(needle, at + needle.size()))
            ++n;
        return n;
    }

private:
    static bool responseComplete(const QByteArray &out)
    {
        const qsizetype end = out.indexOf("\r\n\r\n");
        if (end < 0)
            return false;
        static const QRegularExpression length(QStringLiteral("(?i)content-length:\\s*(\\d+)"));
        const QRegularExpressionMatch m = length.match(QString::fromLatin1(out.left(end)));
        return m.hasMatch() && out.size() - (end + 4) >= m.captured(1).toLongLong();
    }

    quint16 m_port;
    QString m_token;
    QNetworkAccessManager m_nam;
};
