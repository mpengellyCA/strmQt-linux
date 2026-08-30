#include "MockEmbyServer.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>
#include <QWebSocket>
#include <QWebSocketServer>

MockEmbyServer::MockEmbyServer(QObject *parent) : QObject(parent), m_server(new QTcpServer(this))
{
    connect(m_server, &QTcpServer::newConnection, this, &MockEmbyServer::handleConnection);
}

MockEmbyServer::~MockEmbyServer()
{
    dropWebSocketClients();
}

bool MockEmbyServer::start()
{
    return m_server->listen(QHostAddress::LocalHost, 0);
}

QUrl MockEmbyServer::baseUrl() const
{
    return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(m_server->serverPort()));
}

void MockEmbyServer::addRoute(const QString &method, const QString &path, int status,
                              const QByteArray &body, const QByteArray &contentType)
{
    const QString key = method.toUpper() + QLatin1Char(' ') + path;
    // Keep any delay already set for this key: tests arm the delay once and
    // then re-register the route to change its body.
    const int delayMs = m_routes.value(key).delayMs;
    m_routes.insert(key, Route{status, body, contentType, delayMs, false});
}

void MockEmbyServer::enqueueRoute(const QString &method, const QString &path, int status,
                                  const QByteArray &body, const QByteArray &contentType)
{
    const QString key = method.toUpper() + QLatin1Char(' ') + path;
    m_queuedRoutes[key].append(Route{status, body, contentType, 0, false});
}

void MockEmbyServer::addChunkedRoute(const QString &method, const QString &path, int status,
                                     const QByteArray &body, const QByteArray &contentType)
{
    const QString key = method.toUpper() + QLatin1Char(' ') + path;
    const int delayMs = m_routes.value(key).delayMs;
    m_routes.insert(key, Route{status, body, contentType, delayMs, true});
}

void MockEmbyServer::setRouteDelay(const QString &method, const QString &path, int ms)
{
    const QString key = method.toUpper() + QLatin1Char(' ') + path;
    Route route = m_routes.value(key);
    route.delayMs = ms;
    m_routes.insert(key, route);
}

bool MockEmbyServer::addRouteFromFile(const QString &method, const QString &path,
                                      const QString &fixtureFilePath, int status)
{
    QFile file(fixtureFilePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    addRoute(method, path, status, file.readAll());
    return true;
}

void MockEmbyServer::addFieldsGatedRoute(const QString &method, const QString &path,
                                         const QByteArray &fullBody,
                                         const QStringList &gatedKeys, int status)
{
    const QString key = method.toUpper() + QLatin1Char(' ') + path;
    m_fieldsGatedRoutes.insert(key, FieldsGatedRoute{status, fullBody, gatedKeys});
}

MockEmbyServer::ReceivedRequest MockEmbyServer::lastRequestFor(const QString &method,
                                                               const QString &path) const
{
    for (auto it = m_requests.crbegin(); it != m_requests.crend(); ++it) {
        if (it->method == method.toUpper() && it->path == path)
            return *it;
    }
    return {};
}

void MockEmbyServer::handleConnection()
{
    while (QTcpSocket *socket = m_server->nextPendingConnection()) {
        auto buffer = std::make_shared<QByteArray>();
        struct ResponseState
        {
            QString path;
            bool pending = false;
        };
        auto responseState = std::make_shared<ResponseState>();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer, responseState] {
            buffer->append(socket->readAll());

            const int headerEnd = static_cast<int>(buffer->indexOf("\r\n\r\n"));
            if (headerEnd < 0)
                return; // headers incomplete, keep reading

            const QByteArray headerBlock = buffer->left(headerEnd);
            const QList<QByteArray> lines = headerBlock.split('\r');

            ReceivedRequest request;
            qsizetype contentLength = 0;
            {
                const QList<QByteArray> requestLine = lines.value(0).trimmed().split(' ');
                request.method = QString::fromLatin1(requestLine.value(0)).toUpper();
                const QUrl target =
                    QUrl(QString::fromLatin1(requestLine.value(1)), QUrl::TolerantMode);
                request.path = target.path();
                request.query = target.query();
            }
            for (qsizetype i = 1; i < lines.size(); ++i) {
                const QByteArray line = lines[i].trimmed();
                const int colon = static_cast<int>(line.indexOf(':'));
                if (colon <= 0)
                    continue;
                const QByteArray key = line.left(colon).toLower();
                const QByteArray value = line.mid(colon + 1).trimmed();
                request.headers.insert(key, value);
                if (key == "content-length")
                    contentLength = value.toLongLong();
            }

            const qsizetype bodyStart = headerEnd + 4;
            if (buffer->size() - bodyStart < contentLength)
                return; // body incomplete, keep reading

            request.body = buffer->mid(bodyStart, contentLength);
            m_requests.append(request);
            responseState->path = request.path;
            responseState->pending = true;

            const QString key = request.method + QLatin1Char(' ') + request.path;
            QByteArray response;
            int delayMs = 0;
            if (!m_queuedRoutes.value(key).isEmpty() || m_routes.contains(key) ||
                m_fieldsGatedRoutes.contains(key)) {
                Route route;
                if (!m_queuedRoutes.value(key).isEmpty()) {
                    route = m_queuedRoutes[key].takeFirst();
                } else if (m_fieldsGatedRoutes.contains(key)) {
                    const FieldsGatedRoute gated = m_fieldsGatedRoutes.value(key);
                    QJsonObject body = QJsonDocument::fromJson(gated.fullBody).object();
                    const QStringList requested =
                        QUrlQuery(request.query)
                            .queryItemValue(QStringLiteral("Fields"))
                            .split(QLatin1Char(','), Qt::SkipEmptyParts);
                    for (const QString &gatedKey : gated.gatedKeys) {
                        if (!requested.contains(gatedKey, Qt::CaseInsensitive))
                            body.remove(gatedKey);
                    }
                    route = Route{gated.status,
                                  QJsonDocument(body).toJson(QJsonDocument::Compact),
                                  "application/json", 0, false};
                } else {
                    route = m_routes.value(key);
                }
                delayMs = route.delayMs;
                response = "HTTP/1.1 " + QByteArray::number(route.status) +
                           " Status\r\nContent-Type: " + route.contentType + "\r\n";
                if (route.chunked) {
                    response += "Transfer-Encoding: chunked\r\nConnection: close\r\n\r\n" +
                                QByteArray::number(route.body.size(), 16) + "\r\n" +
                                route.body + "\r\n0\r\n\r\n";
                } else {
                    response += "Content-Length: " + QByteArray::number(route.body.size()) +
                                "\r\nConnection: close\r\n\r\n" + route.body;
                }
            } else {
                response = "HTTP/1.1 404 Not Found\r\n"
                           "Content-Length: 0\r\nConnection: close\r\n\r\n";
            }
            const auto reply = [socket, response, responseState] {
                responseState->pending = false;
                socket->write(response);
                socket->disconnectFromHost();
            };
            // `socket` as the timer's context object, so a connection that goes
            // away while its reply is held back takes the timer with it.
            if (delayMs > 0)
                QTimer::singleShot(delayMs, socket, reply);
            else
                reply();
        });
        connect(socket, &QTcpSocket::disconnected, this, [this, responseState] {
            if (responseState->pending) {
                ++m_abortedResponses[responseState->path];
                responseState->pending = false;
            }
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
    }
}

// ── Event socket ─────────────────────────────────────────────────────────────

bool MockEmbyServer::startWebSocket()
{
    if (!m_wsServer) {
        m_wsServer = new QWebSocketServer(QStringLiteral("mock-emby"),
                                          QWebSocketServer::NonSecureMode, this);
        connect(m_wsServer, &QWebSocketServer::newConnection, this,
                &MockEmbyServer::handleWebSocketConnection);
    }
    if (m_wsServer->isListening())
        return true;
    // Reuse the port across stop/start so a reconnect test can bring the same
    // endpoint back up; 0 on the first call lets the OS pick one.
    if (!m_wsServer->listen(QHostAddress::LocalHost, m_wsPort))
        return false;
    m_wsPort = m_wsServer->serverPort();
    return true;
}

void MockEmbyServer::stopWebSocket()
{
    dropWebSocketClients();
    if (m_wsServer)
        m_wsServer->close();
}

QUrl MockEmbyServer::webSocketBaseUrl() const
{
    return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(m_wsPort));
}

int MockEmbyServer::webSocketClientCount() const
{
    return static_cast<int>(m_wsClients.size());
}

void MockEmbyServer::sendWebSocketMessage(const QString &text)
{
    for (QWebSocket *client : std::as_const(m_wsClients))
        client->sendTextMessage(text);
}

void MockEmbyServer::dropWebSocketClients()
{
    const QList<QWebSocket *> clients = m_wsClients;
    m_wsClients.clear();
    for (QWebSocket *client : clients) {
        client->disconnect(this);
        client->abort();
        client->deleteLater();
    }
}

void MockEmbyServer::handleWebSocketConnection()
{
    while (QWebSocket *client = m_wsServer->nextPendingConnection()) {
        client->setParent(this);
        m_wsClients.append(client);
        connect(client, &QWebSocket::textMessageReceived, this,
                [this](const QString &text) { m_wsReceived.append(text); });
        connect(client, &QWebSocket::disconnected, this, [this, client] {
            m_wsClients.removeAll(client);
            client->deleteLater();
        });
        emit webSocketClientConnected();
    }
}
