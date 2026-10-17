#include "DiscoveryResponder.h"

#include "core/Log.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QNetworkDatagram>
#include <QUdpSocket>

#include <algorithm>
#include <iterator>
#include <utility>

namespace strmqt {

namespace {
// Senders remembered for the per-peer gap; past it the table starts over.
constexpr qsizetype kMaxSenders = 256;
}

DiscoveryResponder::DiscoveryResponder(std::function<QJsonObject()> describe, QObject *parent)
    : QObject(parent), m_describe(std::move(describe))
{
}

bool DiscoveryResponder::start(quint16 port)
{
    stop();
    m_socket = new QUdpSocket(this);
    // The desktop StrmQt never binds it, but a restarting host may still hold it.
    if (!m_socket->bind(QHostAddress::AnyIPv4, port,
                        QAbstractSocket::ShareAddress | QAbstractSocket::ReuseAddressHint)) {
        qCWarning(logApp) << "discovery: cannot listen on UDP" << port << m_socket->errorString();
        delete m_socket;
        m_socket = nullptr;
        return false;
    }
    connect(m_socket, &QUdpSocket::readyRead, this, &DiscoveryResponder::readPending);
    qCInfo(logApp) << "discovery: answering on UDP" << m_socket->localPort();
    return true;
}

void DiscoveryResponder::stop()
{
    if (!m_socket)
        return;
    m_socket->close();
    m_socket->deleteLater();
    m_socket = nullptr;
}

bool DiscoveryResponder::isRunning() const
{
    return m_socket && m_socket->state() == QAbstractSocket::BoundState;
}

quint16 DiscoveryResponder::port() const
{
    return m_socket ? m_socket->localPort() : 0;
}

bool DiscoveryResponder::isNearby(const QHostAddress &address)
{
    bool isV4 = false;
    const QHostAddress v4(address.toIPv4Address(&isV4));
    const QHostAddress &a = isV4 ? v4 : address;
    // The RFC 1918 ranges by hand: QHostAddress::isPrivateUse is Qt 6.6.
    static const QPair<QHostAddress, int> privateV4[] = {
        QHostAddress::parseSubnet(QStringLiteral("10.0.0.0/8")),
        QHostAddress::parseSubnet(QStringLiteral("172.16.0.0/12")),
        QHostAddress::parseSubnet(QStringLiteral("192.168.0.0/16")),
    };
    const bool privateUse = std::any_of(std::begin(privateV4), std::end(privateV4),
                                        [&a](const auto &net) { return a.isInSubnet(net); });
    return a.isLoopback() || a.isLinkLocal() || privateUse || a.isUniqueLocalUnicast();
}

QByteArray DiscoveryResponder::replyTo(const QByteArray &datagram) const
{
    if (datagram.trimmed() != QByteArray(kProbe))
        return {};
    QJsonObject reply = m_describe ? m_describe() : QJsonObject();
    reply.insert(QStringLiteral("service"), QStringLiteral("strmqt"));
    reply.insert(QStringLiteral("version"), 1);
    return QJsonDocument(reply).toJson(QJsonDocument::Compact);
}

bool DiscoveryResponder::allowed(const QHostAddress &sender)
{
    if (!isNearby(sender))
        return false;
    const qint64 now = m_clock ? m_clock() : QDateTime::currentMSecsSinceEpoch();
    if (now - m_secondStart >= 1000 || now < m_secondStart) {
        m_secondStart = now;
        m_repliesThisSecond = 0;
    }
    if (m_repliesThisSecond >= kMaxRepliesPerSecond)
        return false;
    const QString key = sender.toString();
    const auto last = m_lastReply.constFind(key);
    if (last != m_lastReply.cend() && now - *last < kMinReplyGapMs && now >= *last)
        return false;
    if (m_lastReply.size() >= kMaxSenders)
        m_lastReply.clear();
    m_lastReply.insert(key, now);
    ++m_repliesThisSecond;
    return true;
}

void DiscoveryResponder::readPending()
{
    while (m_socket && m_socket->hasPendingDatagrams()) {
        const QNetworkDatagram datagram = m_socket->receiveDatagram(512);
        const QByteArray reply = replyTo(datagram.data());
        if (reply.isEmpty() || !allowed(datagram.senderAddress()))
            continue;
        m_socket->writeDatagram(reply, datagram.senderAddress(), quint16(datagram.senderPort()));
    }
}

} // namespace strmqt
