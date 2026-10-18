#pragma once

#include <QByteArray>
#include <QtGlobal>

#include <atomic>

class QThread;

namespace strmqt {

// TimeService — the streaming host's clock, for StrmLights lining their sound
// and picture up on it (StrmLight's docs/synchronized-playback.md).
//
// A StrmLight sends a request and reads the reply: the four times of NTP. Its
// own send and receive times, t1 and t4, never leave it; the host adds t2, when
// the request arrived (the kernel's receive time where it gives one), and t3,
// just before the reply goes. offset = ((t2 - t1) + (t3 - t4)) / 2 to within
// half the difference between the two directions' delays, which is why a
// StrmLight keeps the exchanges with the shortest round trips.
//
// The host's times are CLOCK_MONOTONIC nanoseconds: the clock Sunshine stamps
// each captured frame with (std::chrono::steady_clock), so a frame's RTP
// timestamp and this clock are one timeline. `clockId` changes when the
// machine starts again, when that timeline starts over.
//
// Every datagram is kSize bytes, big-endian:
//   0 "STQT"   4 version (1)   5 kind (1 request, 2 reply)   6 flags (0)
//   8 sequence, echoed   12 reserved   16 t1, echoed   24 t2   32 t3
//   40 clockId
// A reply is never larger than its request, only peers on this network are
// answered (DiscoveryResponder::isNearby), and at most kMaxPerSenderPerSecond
// each and kMaxPerSecond in all. StrmLights answer the same requests, so two
// TVs can time each other too.
//
// It answers on a thread of its own: on the GUI thread a reply waits for
// whatever the interface is doing, and that wait would be in t3.
class TimeService
{
public:
    static constexpr int kSize = 48;
    static constexpr quint8 kVersion = 1;
    static constexpr quint8 kRequest = 1;
    static constexpr quint8 kReply = 2;
    static constexpr int kMaxPerSenderPerSecond = 100;
    static constexpr int kMaxPerSecond = 2000;

    TimeService();
    ~TimeService();
    TimeService(const TimeService &) = delete;
    TimeService &operator=(const TimeService &) = delete;

    bool start(quint16 port);
    void stop();
    bool isRunning() const;
    quint16 port() const;

    // CLOCK_MONOTONIC, in nanoseconds.
    static qint64 now();
    // This boot's clock: from the kernel's boot id, else random for this run.
    static quint64 clockId();
    // The reply to `request`, received at `received` and sent at `sent`; empty
    // for anything that is not a request.
    static QByteArray replyTo(const QByteArray &request, qint64 received, qint64 sent, quint64 clockId);

private:
    void serve();

    int m_fd = -1;
    quint16 m_port = 0;
    std::atomic_bool m_stop{false};
    QThread *m_thread = nullptr;
};

} // namespace strmqt
