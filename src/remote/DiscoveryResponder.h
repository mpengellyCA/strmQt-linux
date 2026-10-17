#pragma once

#include <QHash>
#include <QHostAddress>
#include <QJsonObject>
#include <QObject>

#include <functional>

class QUdpSocket;

namespace strmqt {

// DiscoveryResponder — how a StrmLight on the network finds the streaming
// host without being told its address (StrmLight's setup lists what answers).
//
// It sends the probe `kProbe` as a UDP broadcast to the host's remote port; the
// streaming host's StrmQt answers that address alone with a small JSON object:
// {"service":"strmqt","version":1,"name","remotePort","sunshinePort",
// "fingerprint"} (see `describe`). Nothing in it is secret, and nothing is
// trusted for it either: the StrmLight still pins the certificate it is shown
// and still pairs with a code.
//
// Only peers on this network are answered (private, link-local and loopback
// addresses), each at most every kMinReplyGapMs, and only kMaxRepliesPerSecond
// in all: a reply is larger than a probe, so it must not be anybody's
// amplifier.
class DiscoveryResponder : public QObject
{
    Q_OBJECT

public:
    static constexpr char kProbe[] = "STRMQT-DISCOVER 1";
    static constexpr int kMinReplyGapMs = 200;
    static constexpr int kMaxRepliesPerSecond = 20;

    // `describe` gives the fields of the reply after "service" and "version".
    explicit DiscoveryResponder(std::function<QJsonObject()> describe, QObject *parent = nullptr);

    bool start(quint16 port);
    void stop();
    bool isRunning() const;
    quint16 port() const;

    static bool isNearby(const QHostAddress &address);
    // The reply to a datagram, or empty for one that is not a probe.
    QByteArray replyTo(const QByteArray &datagram) const;

    void setClockForTests(std::function<qint64()> nowMs) { m_clock = std::move(nowMs); }

private:
    void readPending();
    bool allowed(const QHostAddress &sender);

    std::function<QJsonObject()> m_describe;
    QUdpSocket *m_socket = nullptr;
    QHash<QString, qint64> m_lastReply;
    qint64 m_secondStart = 0;
    int m_repliesThisSecond = 0;
    std::function<qint64()> m_clock;
};

} // namespace strmqt
