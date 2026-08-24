#pragma once

#include "playback/PlayerBackend.h"

#include <QImage>
#include <QMutex>

#include <atomic>
#include <cstdlib>
#include <limits>
#include <utility>

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
    static constexpr unsigned kPlaneAlignment = 32;
    static constexpr quint64 kMaxFramePixels = 4096ULL * 4096ULL;
    static constexpr unsigned kMaxFrameDimension = 16384;

    bool configure(unsigned width, unsigned height, unsigned *pitchOut = nullptr)
    {
        QMutexLocker lock(&m_mutex);
        m_publishToken = 0;
        m_hasFront = false;
        const quint64 pixels = quint64(width) * height;
        const quint64 rowBytes = quint64(width) * 4;
        const quint64 alignedPitch =
            (rowBytes + kPlaneAlignment - 1) & ~(quint64(kPlaneAlignment) - 1);
        if (width == 0 || height == 0 || width > kMaxFrameDimension ||
            height > kMaxFrameDimension || pixels > kMaxFramePixels ||
            alignedPitch > quint64(std::numeric_limits<int>::max())) {
            invalidateFrames();
            return false;
        }

        const QSize size{int(width), int(height)};
        QImage front = allocateFrame(size, unsigned(alignedPitch));
        QImage back = allocateFrame(size, unsigned(alignedPitch));
        QImage scratch = allocateFrame(size, unsigned(alignedPitch));
        if (front.isNull() || back.isNull() || scratch.isNull()) {
            invalidateFrames();
            return false;
        }
        m_size = size;
        m_pitch = unsigned(alignedPitch);
        m_front = std::move(front);
        m_back = std::move(back);
        m_scratch = std::move(scratch);
        if (pitchOut)
            *pitchOut = m_pitch;
        return true;
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
        QImage *target = &m_back;
        // A GUI paint can still hold the previously published backing store
        // after it rotates into m_back. Allocate a fresh aligned plane instead
        // of asking QImage::bits() to deep-detach/copy the obsolete pixels.
        if (!m_back.isNull() && !m_back.isDetached()) {
            QImage replacement = allocateFrame(m_size, m_pitch);
            if (!replacement.isNull())
                m_back = std::move(replacement);
            else
                target = &m_scratch; // full-sized, aligned, and never published
        }
        if (target->isNull())
            target = &m_scratch;
        if (target->isNull()) {
            planes[0] = nullptr;
            m_publishToken = 0;
            m_mutex.unlock();
            return nullptr;
        }
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
    static QImage allocateFrame(const QSize &size, unsigned pitch)
    {
        if (!size.isValid() || pitch == 0)
            return {};
        const size_t byteCount = size_t(pitch) * size_t(size.height());
        if (byteCount == 0 || byteCount % kPlaneAlignment != 0)
            return {};
        void *storage = std::aligned_alloc(kPlaneAlignment, byteCount);
        if (!storage)
            return {};
        return QImage(static_cast<uchar *>(storage), size.width(), size.height(), int(pitch),
                      QImage::Format_RGB32, [](void *pointer) { std::free(pointer); }, storage);
    }

    void invalidateFrames()
    {
        m_size = {};
        m_pitch = 0;
        m_front = {};
        m_back = {};
        m_scratch = {};
    }

    mutable QMutex m_mutex;
    QSize m_size;
    unsigned m_pitch = 0;
    QImage m_front;
    QImage m_back;
    QImage m_scratch;
    quintptr m_nextToken = 0;
    quintptr m_publishToken = 0;
    bool m_hasFront = false;
};

// A decoder can outrun Qt's render loop, but the UI only ever needs the newest
// published frame. At most one queued delivery is outstanding; all intervening
// displays collapse into it.
class NotificationGate
{
public:
    bool request()
    {
        bool expected = false;
        return m_pending.compare_exchange_strong(expected, true, std::memory_order_acq_rel);
    }
    void release() { m_pending.store(false, std::memory_order_release); }
    bool pending() const { return m_pending.load(std::memory_order_acquire); }

private:
    std::atomic_bool m_pending = false;
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
    std::atomic_bool m_sawFirstFrame = false;
    // Written by load()/stop() on the GUI thread and read by eventCb() on
    // libvlc's event thread, which is the whole point of stamping events.
    std::atomic<LoadId> m_loadId = 0;

    vlcframes::Buffers m_frames;
    vlcframes::NotificationGate m_frameNotifications;
};

} // namespace strmqt
