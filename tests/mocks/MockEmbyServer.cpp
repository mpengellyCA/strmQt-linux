#include "MockEmbyServer.h"

#include <QFile>
#include <QTcpServer>
#include <QTcpSocket>
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
    m_routes.insert(method.toUpper() + QLatin1Char(' ') + path, Route{status, body, contentType});
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
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
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

            const QString key = request.method + QLatin1Char(' ') + request.path;
            QByteArray response;
            if (m_routes.contains(key)) {
                const Route &route = m_routes.value(key);
                response = "HTTP/1.1 " + QByteArray::number(route.status) +
                           " Status\r\n"
                           "Content-Type: " +
                           route.contentType +
                           "\r\n"
                           "Content-Length: " +
                           QByteArray::number(route.body.size()) +
                           "\r\n"
                           "Connection: close\r\n\r\n" +
                           route.body;
            } else {
                response = "HTTP/1.1 404 Not Found\r\n"
                           "Content-Length: 0\r\nConnection: close\r\n\r\n";
            }
            socket->write(response);
            socket->disconnectFromHost();
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
