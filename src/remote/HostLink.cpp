#include "HostLink.h"

#include "core/Log.h"

#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStandardPaths>

#include <memory>
#include <utility>

namespace strmqt {

namespace {
// "pair 1234\n" with room to spare; anything longer is not this protocol.
constexpr qint64 kMaxLine = 64;
// An answer's sentence.
constexpr qint64 kMaxAnswer = 4096;

// A request read so far: its first line, then what it said would follow.
struct Pending
{
    QByteArray verb;
    qint64 length = -1;
    bool answered = false;
};

QByteArray answerLine(const HostLink::Answer &answer)
{
    QString text = answer.text.simplified();
    QByteArray line = answer.ok ? "ok" : "no";
    if (!text.isEmpty())
        line += ' ' + text.toUtf8().left(kMaxAnswer - 8);
    return line + '\n';
}
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
                auto pending = std::make_shared<Pending>();
                connect(socket, &QLocalSocket::readyRead, socket, [this, socket, pending] {
                    if (pending->answered)
                        return;
                    const auto reply = [socket, pending](const QByteArray &line) {
                        pending->answered = true;
                        socket->write(line);
                        socket->disconnectFromServer();
                    };
                    if (pending->length < 0) {
                        if (!socket->canReadLine()) {
                            if (socket->bytesAvailable() > kMaxLine)
                                socket->abort();
                            return;
                        }
                        const QString line = QString::fromUtf8(socket->readLine(kMaxLine)).trimmed();
                        const QString prefix = QStringLiteral("pair ");
                        if (line.startsWith(prefix)) {
                            reply(m_take && m_take(line.mid(prefix.size())) ? "ok\n" : "no\n");
                            return;
                        }
                        const QStringList words = line.split(QLatin1Char(' '));
                        bool counted = false;
                        const qint64 length = words.value(1).toLongLong(&counted);
                        pending->verb = words.value(0).toUtf8();
                        if (words.size() != 2 || !counted || length < 0 || length > kMaxBody
                            || !m_handlers.contains(pending->verb)) {
                            reply(answerLine({false, tr("The streaming host does not know that request.")}));
                            return;
                        }
                        pending->length = length;
                    }
                    if (socket->bytesAvailable() < pending->length)
                        return;
                    const QByteArray body = socket->read(pending->length);
                    reply(answerLine(m_handlers.value(pending->verb)(body)));
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

void HostLink::handle(const QByteArray &verb, Handler handler)
{
    m_handlers.insert(verb, std::move(handler));
}

HostLink::Answer HostLink::ask(const QByteArray &verb, const QByteArray &body, int timeoutMs) const
{
    const Answer nobody{false, tr("The streaming host's StrmQt is not running.")};
    if (body.size() > kMaxBody)
        return {false, tr("That is more than the streaming host takes.")};
    QLocalSocket socket;
    socket.connectToServer(m_path);
    if (!socket.waitForConnected(timeoutMs))
        return nobody;
    socket.write(verb + ' ' + QByteArray::number(body.size()) + '\n' + body);
    while (socket.bytesToWrite() > 0) {
        if (!socket.waitForBytesWritten(timeoutMs))
            return nobody;
    }
    while (!socket.canReadLine()) {
        if (!socket.waitForReadyRead(timeoutMs))
            return {false, tr("The streaming host's StrmQt did not answer.")};
    }
    const QString line = QString::fromUtf8(socket.readLine(kMaxAnswer)).trimmed();
    return {line == QLatin1String("ok") || line.startsWith(QLatin1String("ok ")), line.mid(3)};
}

} // namespace strmqt
