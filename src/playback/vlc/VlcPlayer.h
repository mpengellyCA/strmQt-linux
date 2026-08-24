#pragma once

#include "playback/PlayerBackend.h"

#include <QImage>
#include <QMutex>

#include <atomic>

struct libvlc_instance_t;
struct libvlc_media_player_t;
struct libvlc_event_t;

namespace strmqt {

namespace vlcframes {

// libVLC writes one image while Qt paints another. Swapping the two pre-sized
// images publishes a frame without an 8 MB deep copy at 1080p. The scratch
// image deliberately survives clear(): a late decoder lock during teardown can
// always receive a valid full-sized plane and its display is then discarded.
class Buffers
{
public:
    void configure(const QSize &size)
    {
        QMutexLocker lock(&m_mutex);
        m_publishToken = 0;
        m_hasFront = false;
        m_front = QImage(size, QImage::Format_RGB32);
        m_back = QImage(size, QImage::Format_RGB32);
        m_scratch = QImage(size, QImage::Format_RGB32);
    }

    void clear()
    {
        QMutexLocker lock(&m_mutex);
        m_publishToken = 0;
        m_hasFront = false;
        m_front = {};
        m_back = {};
    }

    void *lock(void **planes)
    {
        m_mutex.lock();
        QImage *target = !m_back.isNull() ? &m_back : &m_scratch;
        // formatCb always precedes decoder locks, so scratch is full-sized. A
        // defensive one-pixel allocation still makes a contract violation
        // non-null rather than handing VLC a null plane immediately.
        if (target->isNull())
            m_scratch = QImage(1, 1, QImage::Format_RGB32);
        planes[0] = target->bits();
        do {
            ++m_nextToken;
        } while (m_nextToken == 0);
        m_publishToken = target == &m_back ? m_nextToken : 0;
        // libVLC treats the returned picture as an opaque cookie. A unique
        // integer token prevents a delayed display from an earlier configure()
        // from publishing the new load's back buffer merely because the member
        // QImage occupies the same address.
        return reinterpret_cast<void *>(m_nextToken);
    }

    void unlock() { m_mutex.unlock(); }

    bool display(void *picture)
    {
        QMutexLocker lock(&m_mutex);
        const auto token = reinterpret_cast<quintptr>(picture);
        if (token == 0 || token != m_publishToken || m_back.isNull())
            return false;
        m_front.swap(m_back);
        m_publishToken = 0;
        m_hasFront = true;
        return true;
    }

    QImage current() const
    {
        QMutexLocker lock(&m_mutex);
        return m_hasFront ? m_front : QImage{}; // implicit sharing: no pixel copy
    }

private:
    mutable QMutex m_mutex;
    QImage m_front;
    QImage m_back;
    QImage m_scratch;
    quintptr m_nextToken = 0;
    quintptr m_publishToken = 0;
    bool m_hasFront = false;
};

} // namespace vlcframes

// libvlc-backed fallback engine (raw C API, PLAN §3.2 decision 1: VLC is the
// escape hatch when the mpv path misbehaves). Video is delivered via vmem
// callbacks as BGRA frames; VlcVideoItem paints the latest frame. Slower than
// the mpv GL path by design — correctness over speed for a fallback.
class VlcPlayer : public PlayerBackend
{
    Q_OBJECT

public:
    explicit VlcPlayer(QObject *parent = nullptr);
    ~VlcPlayer() override;

    QString engineName() const override { return QStringLiteral("vlc"); }

    void load(const QUrl &url, qint64 startMs, LoadId loadId,
              bool initiallyPaused = false) override;
    void setPaused(bool paused) override;
    void stop() override;
    void seekTo(qint64 positionMs) override;
    void setVolume(int percent) override;

    State state() const override { return m_state; }
    qint64 positionMs() const override { return m_positionMs; }
    qint64 durationMs() const override { return m_durationMs; }
    bool buffering() const override { return m_buffering; }

    // Latest decoded frame (shallow QImage snapshot under lock); null when
    // nothing decoded yet.
    QImage currentFrame() const;

signals:
    void frameReady();

private:
    // vmem callbacks (libvlc decoding threads)
    static void *lockCb(void *opaque, void **planes);
    static void unlockCb(void *opaque, void *picture, void *const *planes);
    static void displayCb(void *opaque, void *picture);
    static unsigned formatCb(void **opaque, char *chroma, unsigned *width, unsigned *height,
                             unsigned *pitches, unsigned *lines);

    // libvlc event thread → GUI thread marshalling
    Q_INVOKABLE void handleEvent(int type, qint64 value, quint64 loadId);
    static void eventCb(const libvlc_event_t *event, void *opaque);

    void setState(State state, LoadId loadId);
    void resetPerLoadState(LoadId loadId, qint64 positionMs);

    libvlc_instance_t *m_vlc = nullptr;
    libvlc_media_player_t *m_player = nullptr;

    State m_state = State::Idle;
    qint64 m_positionMs = 0;
    qint64 m_durationMs = 0;
    qint64 m_pendingStartMs = 0;
    bool m_buffering = false;
    bool m_sawFirstFrame = false;
    // Written by load()/stop() on the GUI thread and read by eventCb() on
    // libvlc's event thread, which is the whole point of stamping events.
    std::atomic<LoadId> m_loadId = 0;

    vlcframes::Buffers m_frames;
};

} // namespace strmqt
