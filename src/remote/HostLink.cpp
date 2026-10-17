#include "HostLink.h"

#include "core/Log.h"

#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStandardPaths>

#include <utility>

namespace strmqt {

namespace {
// "pair 1234\n" with room to spare; anything longer is not this protocol.
constexpr qint64 kMaxLine = 64;
}

HostLink::HostLink(QString socketPath, QObject *parent) : QObject(parent), m_path(std::move(socketPath)) {}

HostLink::~HostLink() = default;

QString HostLink::defaultSocketPath()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (dir.isEmpty())
        dir = QDir::tempPath();
    return dir + QStringLiteral("/strmqt-host-link");
}

bool HostLink::listen(std::function<bool(const QString &)> take)
{
    m_take = std::move(take);
    if (!m_server) {
        m_server = new QLocalServer(this);
        m_server->setSocketOptions(QLocalServer::UserAccessOption);
        connect(m_server, &QLocalServer::newConnection, this, [this] {
            while (QLocalSocket *socket = m_server->nextPendingConnection()) {
                connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QLocalSocket::readyRead, socket, [this, socket] {
                    if (!socket->canReadLine()) {
                        if (socket->bytesAvailable() > kMaxLine)
                            socket->abort();
                        return;
                    }
                    const QString line = QString::fromUtf8(socket->readLine(kMaxLine)).trimmed();
                    const QString prefix = QStringLiteral("pair ");
                    const bool ok = line.startsWith(prefix) && m_take && m_take(line.mid(prefix.size()));
                    socket->write(ok ? "ok\n" : "no\n");
                    socket->disconnectFromServer();
                });
            }
        });
    }
    if (m_server->isListening())
        return true;
    // A socket left by a host that did not exit cleanly would refuse the bind.
    QLocalServer::removeServer(m_path);
    if (!m_server->listen(m_path)) {
        qCWarning(logApp) << "hostlink: cannot listen on" << m_path << m_server->errorString();
        return false;
    }
    return true;
}

bool HostLink::isListening() const
{
    return m_server && m_server->isListening();
}

bool HostLink::pass(const QString &code, int timeoutMs) const
{
    QLocalSocket socket;
    socket.connectToServer(m_path);
    if (!socket.waitForConnected(timeoutMs))
        return false;
    socket.write("pair " + code.toUtf8() + '\n');
    if (!socket.waitForBytesWritten(timeoutMs))
        return false;
    while (!socket.canReadLine()) {
        if (!socket.waitForReadyRead(timeoutMs))
            return false;
    }
    return socket.readLine(kMaxLine).trimmed() == "ok";
}

} // namespace strmqt
