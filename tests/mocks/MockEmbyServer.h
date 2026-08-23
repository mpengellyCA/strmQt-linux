#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

class QTcpServer;
class QWebSocket;
class QWebSocketServer;

// Minimal single-threaded HTTP/1.1 replay server for integration tests.
// Routes are matched on "METHOD /path" (query string stripped); every request
// received is recorded so tests can assert on headers and bodies.
// Not a general HTTP server — just enough for QNetworkAccessManager.
class MockEmbyServer : public QObject
{
    Q_OBJECT

public:
    struct ReceivedRequest
    {
        QString method;
        QString path; // without query
        QString query;
        QHash<QByteArray, QByteArray> headers; // lower-cased keys
        QByteArray body;
    };

    explicit MockEmbyServer(QObject *parent = nullptr);
    ~MockEmbyServer() override;

    bool start(); // listens on 127.0.0.1, random port
    QUrl baseUrl() const;

    void addRoute(const QString &method, const QString &path, int status, const QByteArray &body,
                  const QByteArray &contentType = "application/json");
    bool addRouteFromFile(const QString &method, const QString &path,
                          const QString &fixtureFilePath, int status = 200);
    // Hold a route's reply back by `ms` before writing it. The request is
    // recorded when it arrives, so ordering assertions still see it.
    //
    // Exists because a generation counter is only interesting when replies land
    // out of order, and without this the mock answers so fast that "a create
    // while the open playlist is still loading" resolves before it can strand
    // anything — the test would pass against the bug it is guarding.
    void setRouteDelay(const QString &method, const QString &path, int ms);

    const QList<ReceivedRequest> &requests() const { return m_requests; }
    ReceivedRequest lastRequestFor(const QString &method, const QString &path) const;
    int requestCount() const { return static_cast<int>(m_requests.size()); }

    // ── Event socket (Emby's /embywebsocket) ──────────────────────────────────
    // Listens on its own port; EmbyWebSocket derives ws:// from the base URL
    // below and appends /embywebsocket, which this server accepts on any path.
    bool startWebSocket();
    void stopWebSocket();        // stops listening; existing clients are dropped
    QUrl webSocketBaseUrl() const; // http://127.0.0.1:<ws port>
    int webSocketClientCount() const;
    // Broadcasts a raw frame to every connected client.
    void sendWebSocketMessage(const QString &text);
    void dropWebSocketClients(); // close from the server side, keep listening
    const QStringList &webSocketMessagesReceived() const { return m_wsReceived; }

signals:
    void webSocketClientConnected();

private:
    struct Route
    {
        int status = 200;
        QByteArray body;
        QByteArray contentType;
        int delayMs = 0;
    };

    void handleConnection();

    void handleWebSocketConnection();

    QTcpServer *m_server = nullptr;
    QHash<QString, Route> m_routes; // key: "METHOD /path"
    QList<ReceivedRequest> m_requests;

    QWebSocketServer *m_wsServer = nullptr;
    QList<QWebSocket *> m_wsClients;
    QStringList m_wsReceived;
    quint16 m_wsPort = 0;
};
