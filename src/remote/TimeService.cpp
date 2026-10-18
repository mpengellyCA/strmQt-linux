#include "TimeService.h"

#include "DiscoveryResponder.h"
#include "core/Log.h"

#include <QCryptographicHash>
#include <QFile>
#include <QHash>
#include <QHostAddress>
#include <QRandomGenerator>
#include <QThread>
#include <QtEndian>

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace strmqt {

namespace {

constexpr char kMagic[4] = {'S', 'T', 'Q', 'T'};
// How long a blocked receive waits before looking at the stop flag.
constexpr int kPollMs = 200;
// Senders counted for the per-peer limit; past it the table starts over.
constexpr qsizetype kMaxSenders = 256;

qint64 nanos(const timespec &t)
{
    return qint64(t.tv_sec) * 1000000000 + t.tv_nsec;
}

qint64 clockNow(clockid_t clock)
{
    timespec t{};
    clock_gettime(clock, &t);
    return nanos(t);
}

// The kernel stamps a datagram's arrival on the real-time clock. The two
// clocks are read back to back here, so the stamp moves to CLOCK_MONOTONIC to
// within their reading, and a step of the real-time clock between the arrival
// and now is the only thing that can spoil it.
qint64 monotonicFromRealtime(qint64 realtime)
{
    const qint64 rt = clockNow(CLOCK_REALTIME);
    const qint64 mono = clockNow(CLOCK_MONOTONIC);
    return realtime - (rt - mono);
}

} // namespace

TimeService::TimeService() = default;

TimeService::~TimeService()
{
    stop();
}

qint64 TimeService::now()
{
    return clockNow(CLOCK_MONOTONIC);
}

quint64 TimeService::clockId()
{
    static const quint64 id = [] {
        QFile bootId(QStringLiteral("/proc/sys/kernel/random/boot_id"));
        if (bootId.open(QIODevice::ReadOnly)) {
            const QByteArray digest =
                QCryptographicHash::hash(bootId.readAll().trimmed(), QCryptographicHash::Sha256);
            return qFromBigEndian<quint64>(digest.constData());
        }
        return QRandomGenerator::system()->generate64();
    }();
    return id;
}

QByteArray TimeService::replyTo(const QByteArray &request, qint64 received, qint64 sent, quint64 clockId)
{
    if (request.size() != kSize || std::memcmp(request.constData(), kMagic, 4) != 0
        || quint8(request.at(4)) != kVersion || quint8(request.at(5)) != kRequest)
        return {};
    QByteArray reply = request;
    char *p = reply.data();
    p[5] = char(kReply);
    qToBigEndian<quint16>(0, p + 6);
    qToBigEndian<quint32>(0, p + 12);
    qToBigEndian<qint64>(received, p + 24);
    qToBigEndian<qint64>(sent, p + 32);
    qToBigEndian<quint64>(clockId, p + 40);
    return reply;
}

bool TimeService::start(quint16 port)
{
    stop();
    const int fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        qCWarning(logApp) << "time: no socket:" << std::strerror(errno);
        return false;
    }
    const int on = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    // Where the kernel gives it, the arrival time is its own, not when this
    // thread woke up to read it.
    ::setsockopt(fd, SOL_SOCKET, SO_TIMESTAMPNS, &on, sizeof on);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    socklen_t length = sizeof address;
    if (::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof address) != 0
        || ::getsockname(fd, reinterpret_cast<sockaddr *>(&address), &length) != 0) {
        qCWarning(logApp) << "time: cannot listen on UDP" << port << std::strerror(errno);
        ::close(fd);
        return false;
    }
    m_fd = fd;
    m_port = ntohs(address.sin_port);
    m_stop = false;
    m_thread = QThread::create([this] { serve(); });
    m_thread->setObjectName(QStringLiteral("strmqt-time"));
    m_thread->start(QThread::TimeCriticalPriority);
    qCInfo(logApp) << "time: answering on UDP" << m_port;
    return true;
}

void TimeService::stop()
{
    if (!m_thread)
        return;
    m_stop = true;
    m_thread->wait();
    delete m_thread;
    m_thread = nullptr;
    ::close(m_fd);
    m_fd = -1;
    m_port = 0;
}

bool TimeService::isRunning() const
{
    return m_thread && m_thread->isRunning();
}

quint16 TimeService::port() const
{
    return m_port;
}

void TimeService::serve()
{
    const quint64 id = clockId();
    QHash<quint32, QPair<qint64, int>> perSender; // address -> (second, replies)
    qint64 second = 0;
    int thisSecond = 0;

    while (!m_stop) {
        pollfd waiting{m_fd, POLLIN, 0};
        if (::poll(&waiting, 1, kPollMs) <= 0)
            continue;

        char buffer[kSize + 1];
        char control[CMSG_SPACE(sizeof(timespec))];
        sockaddr_in sender{};
        iovec io{buffer, sizeof buffer};
        msghdr message{};
        message.msg_name = &sender;
        message.msg_namelen = sizeof sender;
        message.msg_iov = &io;
        message.msg_iovlen = 1;
        message.msg_control = control;
        message.msg_controllen = sizeof control;
        const ssize_t size = ::recvmsg(m_fd, &message, MSG_DONTWAIT);
        qint64 received = now();
        if (size != kSize)
            continue;
        for (cmsghdr *c = CMSG_FIRSTHDR(&message); c; c = CMSG_NXTHDR(&message, c)) {
            if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_TIMESTAMPNS) {
                timespec stamp{};
                std::memcpy(&stamp, CMSG_DATA(c), sizeof stamp);
                const qint64 kernel = monotonicFromRealtime(nanos(stamp));
                // A real-time step since the arrival would put it after now.
                if (kernel <= received)
                    received = kernel;
            }
        }

        const quint32 peer = ntohl(sender.sin_addr.s_addr);
        if (!DiscoveryResponder::isNearby(QHostAddress(peer)))
            continue;
        const qint64 nowSecond = received / 1000000000;
        if (nowSecond != second) {
            second = nowSecond;
            thisSecond = 0;
        }
        if (thisSecond >= kMaxPerSecond)
            continue;
        if (perSender.size() >= kMaxSenders && !perSender.contains(peer))
            perSender.clear();
        auto &[peerSecond, peerReplies] = perSender[peer];
        if (peerSecond != nowSecond) {
            peerSecond = nowSecond;
            peerReplies = 0;
        }
        if (peerReplies >= kMaxPerSenderPerSecond)
            continue;

        QByteArray reply = replyTo(QByteArray(buffer, kSize), received, 0, id);
        if (reply.isEmpty())
            continue;
        ++peerReplies;
        ++thisSecond;
        qToBigEndian<qint64>(now(), reply.data() + 32);
        ::sendto(m_fd, reply.constData(), size_t(reply.size()), 0, reinterpret_cast<sockaddr *>(&sender),
                 message.msg_namelen);
    }
}

} // namespace strmqt
