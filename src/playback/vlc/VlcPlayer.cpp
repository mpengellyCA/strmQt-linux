#include "VlcPlayer.h"

#include "core/Log.h"

#include <vlc/vlc.h>

#include <QCoreApplication>

namespace strmqt {

namespace {
// Compact event codes for the queued hop to the GUI thread.
enum EventCode
{
    EvPlaying = 1,
    EvPaused,
    EvStopped,
    EvEnd,
    EvError,
    EvTime,
    EvLength,
    EvBuffering,
    EvBufferingDone,
};
} // namespace

VlcPlayer::VlcPlayer(QObject *parent) : PlayerBackend(parent)
{
    const char *args[] = {"--no-xlib", "--quiet"};
    m_vlc = libvlc_new(2, args);
    if (!m_vlc) {
        qCCritical(logPlayback) << "libvlc_new failed";
        return;
    }
    m_player = libvlc_media_player_new(m_vlc);
    if (!m_player) {
        qCCritical(logPlayback) << "libvlc_media_player_new failed";
        return;
    }

    libvlc_video_set_format_callbacks(m_player, formatCb, nullptr);
    libvlc_video_set_callbacks(m_player, lockCb, unlockCb, displayCb, this);

    auto *events = libvlc_media_player_event_manager(m_player);
    for (auto type : {libvlc_MediaPlayerPlaying, libvlc_MediaPlayerPaused,
                      libvlc_MediaPlayerStopped, libvlc_MediaPlayerEndReached,
                      libvlc_MediaPlayerEncounteredError, libvlc_MediaPlayerTimeChanged,
                      libvlc_MediaPlayerLengthChanged, libvlc_MediaPlayerBuffering})
        libvlc_event_attach(events, type, eventCb, this);
}

VlcPlayer::~VlcPlayer()
{
    if (m_player) {
        libvlc_media_player_stop(m_player);
        libvlc_media_player_release(m_player);
    }
    if (m_vlc)
        libvlc_release(m_vlc);
}

unsigned VlcPlayer::formatCb(void **opaque, char *chroma, unsigned *width, unsigned *height,
                             unsigned *pitches, unsigned *lines)
{
    auto *self = static_cast<VlcPlayer *>(*opaque);
    qCInfo(logPlayback) << "vlc vmem format:" << *width << "x" << *height;
    unsigned pitch = 0;
    if (!self->m_frames.configure(*width, *height, &pitch)) {
        qCWarning(logPlayback) << "vlc vmem rejected unsafe frame dimensions" << *width << "x"
                               << *height;
        return 0;
    }
    qstrcpy(chroma, "RV32"); // BGRA on little-endian → QImage::Format_RGB32
    pitches[0] = pitch;
    lines[0] = *height;
    return 1;
}

void *VlcPlayer::lockCb(void *opaque, void **planes)
{
    auto *self = static_cast<VlcPlayer *>(opaque);
    return self->m_frames.lock(planes);
}

void VlcPlayer::unlockCb(void *opaque, void *, void *const *)
{
    static_cast<VlcPlayer *>(opaque)->m_frames.unlock();
}

void VlcPlayer::displayCb(void *opaque, void *picture)
{
    auto *self = static_cast<VlcPlayer *>(opaque);
    if (!self->m_frames.display(picture))
        return; // scratch frame from a retired load
    if (!self->m_sawFirstFrame.exchange(true, std::memory_order_acq_rel)) {
        qCInfo(logPlayback) << "vlc vmem: first frame displayed";
    }
    if (self->m_frameNotifications.request()) {
        QMetaObject::invokeMethod(
            self,
            [self] {
                self->m_frameNotifications.release();
                emit self->frameReady();
            },
            Qt::QueuedConnection);
    }
}

QImage VlcPlayer::currentFrame() const
{
    return m_frames.current();
}

void VlcPlayer::eventCb(const libvlc_event_t *event, void *opaque)
{
    auto *self = static_cast<VlcPlayer *>(opaque);
    int code = 0;
    qint64 value = 0;
    switch (event->type) {
    case libvlc_MediaPlayerPlaying:
        code = EvPlaying;
        break;
    case libvlc_MediaPlayerPaused:
        code = EvPaused;
        break;
    case libvlc_MediaPlayerStopped:
        code = EvStopped;
        break;
    case libvlc_MediaPlayerEndReached:
        code = EvEnd;
        break;
    case libvlc_MediaPlayerEncounteredError:
        code = EvError;
        break;
    case libvlc_MediaPlayerTimeChanged:
        code = EvTime;
        value = event->u.media_player_time_changed.new_time;
        break;
    case libvlc_MediaPlayerLengthChanged:
        code = EvLength;
        value = event->u.media_player_length_changed.new_length;
        break;
    case libvlc_MediaPlayerBuffering:
        code = event->u.media_player_buffering.new_cache >= 100.0f ? EvBufferingDone : EvBuffering;
        break;
    default:
        return;
    }
    const quint64 loadId = self->m_loadId;
    QMetaObject::invokeMethod(self, "handleEvent", Qt::QueuedConnection, Q_ARG(int, code),
                              Q_ARG(qint64, value), Q_ARG(quint64, loadId));
}

void VlcPlayer::handleEvent(int type, qint64 value, quint64 loadId)
{
    if (loadId != m_loadId)
        return;
    switch (type) {
    case EvPlaying:
        if (m_pendingStartMs > 0) {
            libvlc_media_player_set_time(m_player, m_pendingStartMs);
            m_pendingStartMs = 0;
        }
        setState(State::Playing, loadId);
        break;
    case EvPaused:
        setState(State::Paused, loadId);
        break;
    case EvStopped:
        if (m_state != State::Ended && m_state != State::Error)
            setState(State::Idle, loadId);
        break;
    case EvEnd:
        setState(State::Ended, loadId);
        emit endReached(loadId);
        break;
    case EvError:
        setState(State::Error, loadId);
        emit errorOccurred(QStringLiteral("libvlc: playback error"), loadId);
        break;
    case EvTime:
        m_positionMs = value;
        emit positionChanged(value, loadId);
        break;
    case EvLength:
        m_durationMs = value;
        emit durationChanged(value, loadId);
        break;
    case EvBuffering:
    case EvBufferingDone: {
        const bool buffering = type == EvBuffering;
        if (buffering != m_buffering) {
            m_buffering = buffering;
            emit bufferingChanged(buffering, loadId);
        }
        break;
    }
    default:
        break;
    }
}

void VlcPlayer::load(const QUrl &url, qint64 startMs, LoadId loadId, bool initiallyPaused)
{
    // Stop the outgoing decoder before reset clears its write buffer. libVLC's
    // vmem callback runs on a decoder thread; clearing first left a window in
    // which lockCb returned nullptr and the decoder wrote through it.
    m_loadId = 0;
    if (m_player)
        libvlc_media_player_stop(m_player);
    m_loadId = loadId;
    resetPerLoadState(loadId, startMs);
    if (!m_player) {
        setState(State::Error, loadId);
        emit errorOccurred(QStringLiteral("libvlc unavailable"), loadId);
        return;
    }

    libvlc_media_t *media =
        libvlc_media_new_location(m_vlc, url.toString(QUrl::FullyEncoded).toUtf8().constData());
    if (!media) {
        setState(State::Error, loadId);
        emit errorOccurred(QStringLiteral("libvlc: cannot open media"), loadId);
        return;
    }
    libvlc_media_add_option(media, ":http-user-agent=StrmQt");
    if (initiallyPaused) {
        libvlc_media_add_option(media, ":start-paused");
        if (startMs > 0) {
            const QByteArray startOption =
                QByteArrayLiteral(":start-time=") + QByteArray::number(startMs / 1000.0, 'f', 3);
            libvlc_media_add_option(media, startOption.constData());
        }
    }

    m_pendingStartMs = startMs;
    setState(State::Loading, loadId);
    libvlc_media_player_set_media(m_player, media);
    libvlc_media_release(media);
    libvlc_media_player_play(m_player);
}

void VlcPlayer::setPaused(bool paused)
{
    if (m_player)
        libvlc_media_player_set_pause(m_player, paused ? 1 : 0);
}

void VlcPlayer::stop()
{
    m_loadId = 0;
    if (m_player)
        libvlc_media_player_stop(m_player);
    resetPerLoadState(0, 0);
    setState(State::Idle, 0);
}

void VlcPlayer::resetPerLoadState(LoadId loadId, qint64 positionMs)
{
    m_pendingStartMs = 0;
    m_sawFirstFrame.store(false, std::memory_order_release);
    if (m_positionMs != positionMs) {
        m_positionMs = positionMs;
        emit positionChanged(positionMs, loadId);
    }
    if (m_durationMs != 0) {
        m_durationMs = 0;
        emit durationChanged(0, loadId);
    }
    if (m_buffering) {
        m_buffering = false;
        emit bufferingChanged(false, loadId);
    }
    m_frames.clear();
    emit frameReady();
}

void VlcPlayer::seekTo(qint64 positionMs)
{
    if (m_player && libvlc_media_player_is_seekable(m_player))
        libvlc_media_player_set_time(m_player, positionMs);
}

void VlcPlayer::setVolume(int percent)
{
    if (m_player)
        libvlc_audio_set_volume(m_player, qBound(0, percent, 130));
}

void VlcPlayer::setState(State state, LoadId loadId)
{
    if (loadId != m_loadId)
        return;
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(state, loadId);
}

} // namespace strmqt
