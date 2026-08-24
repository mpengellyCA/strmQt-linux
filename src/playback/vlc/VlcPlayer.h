#pragma once

#include "playback/PlayerBackend.h"

#include <QImage>
#include <QMutex>

struct libvlc_instance_t;
struct libvlc_media_player_t;
struct libvlc_event_t;

namespace strmqt {

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

    void load(const QUrl &url, qint64 startMs, LoadId loadId) override;
    void setPaused(bool paused) override;
    void stop() override;
    void seekTo(qint64 positionMs) override;
    void setVolume(int percent) override;

    State state() const override { return m_state; }
    qint64 positionMs() const override { return m_positionMs; }
    qint64 durationMs() const override { return m_durationMs; }
    bool buffering() const override { return m_buffering; }

    // Latest decoded frame (copy under lock); null when nothing decoded yet.
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
    void resetPerLoadState(LoadId loadId);

    libvlc_instance_t *m_vlc = nullptr;
    libvlc_media_player_t *m_player = nullptr;

    State m_state = State::Idle;
    qint64 m_positionMs = 0;
    qint64 m_durationMs = 0;
    qint64 m_pendingStartMs = 0;
    bool m_buffering = false;
    bool m_sawFirstFrame = false;
    LoadId m_loadId = 0;

    mutable QMutex m_frameMutex;
    QImage m_frontFrame; // last displayed frame
    QImage m_backFrame;  // vlc writes here
};

} // namespace strmqt
