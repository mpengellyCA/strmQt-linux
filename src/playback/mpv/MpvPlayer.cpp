#include "MpvPlayer.h"

#include <QRegularExpression>

#include "core/Log.h"

#include <mpv/client.h>

#include <QCoreApplication>

#include <clocale>

namespace strmqt {

namespace {
constexpr qint64 msFromSeconds(double seconds)
{
    return static_cast<qint64>(seconds * 1000.0);
}

// PlayerBackend::setVolume contract.
constexpr int kMaxVolume = 130;
constexpr int kDefaultVolume = 100;

// Buffered-ahead moves continuously while streaming; only tell the scrubber
// about it when the figure moved enough to redraw differently.
constexpr qint64 kBufferedEpsilonMs = 250;
// mpv accepts anything, but a speed outside this is not a player feature.
constexpr qreal kMinSpeed = 0.25;
constexpr qreal kMaxSpeed = 4.0;

// mpv_node → QVariant. Only the formats mpv actually puts in a node tree.
QVariant variantFromNode(const mpv_node &node)
{
    switch (node.format) {
    case MPV_FORMAT_STRING:
        return QString::fromUtf8(node.u.string);
    case MPV_FORMAT_FLAG:
        return node.u.flag != 0;
    case MPV_FORMAT_INT64:
        return QVariant::fromValue<qint64>(node.u.int64);
    case MPV_FORMAT_DOUBLE:
        return node.u.double_;
    case MPV_FORMAT_NODE_ARRAY: {
        QVariantList list;
        list.reserve(node.u.list->num);
        for (int i = 0; i < node.u.list->num; ++i)
            list.append(variantFromNode(node.u.list->values[i]));
        return list;
    }
    case MPV_FORMAT_NODE_MAP: {
        QVariantMap map;
        for (int i = 0; i < node.u.list->num; ++i)
            map.insert(QString::fromUtf8(node.u.list->keys[i]),
                       variantFromNode(node.u.list->values[i]));
        return map;
    }
    default:
        return {};
    }
}

} // namespace

MpvPlayer::MpvPlayer(const QString &toneMapping, QObject *parent) : PlayerBackend(parent)
{
    // libmpv refuses to create a core unless LC_NUMERIC is "C"; QGuiApplication
    // resets the locale from the environment, so pin it back here.
    std::setlocale(LC_NUMERIC, "C");
    m_mpv = mpv_create();
    if (!m_mpv) {
        qCCritical(logPlayback) << "mpv_create failed";
        return;
    }

    mpv_set_option_string(m_mpv, "vo", "libmpv");
    mpv_set_option_string(m_mpv, "hwdec", "auto-safe");
    mpv_set_option_string(m_mpv, "terminal", "no");
    mpv_set_option_string(m_mpv, "input-default-bindings", "no");
    mpv_set_option_string(m_mpv, "audio-client-name", "StrmQt");
    // Do not freeze on a dead last frame; END_FILE must fire (PLAN §3.5).
    mpv_set_option_string(m_mpv, "keep-open", "no");
    // Network resilience basics; the full ladder/watchdog sits above (PlayerController).
    mpv_set_option_string(m_mpv, "cache", "yes");
    mpv_set_option_string(m_mpv, "demuxer-max-bytes", "256MiB");
    mpv_set_option_string(m_mpv, "user-agent", "StrmQt");
    // HDR: the embedded GL path always tone-maps to the SDR scene (PLAN M6 matrix).
    // High-quality libplacebo settings; dynamic peak detection tracks scene brightness.
    mpv_set_option_string(m_mpv, "tone-mapping",
                          toneMapping.isEmpty() ? "hable" : toneMapping.toUtf8().constData());
    mpv_set_option_string(m_mpv, "hdr-compute-peak", "yes");
    mpv_set_option_string(m_mpv, "target-colorspace-hint", "auto");

    if (mpv_initialize(m_mpv) < 0) {
        qCCritical(logPlayback) << "mpv_initialize failed";
        mpv_destroy(m_mpv);
        m_mpv = nullptr;
        return;
    }

    mpv_request_log_messages(m_mpv, "warn");
    mpv_observe_property(m_mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "paused-for-cache", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "hwdec-current", MPV_FORMAT_STRING);
    mpv_observe_property(m_mpv, 0, "volume", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "mute", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "aid", MPV_FORMAT_STRING);
    mpv_observe_property(m_mpv, 0, "sid", MPV_FORMAT_STRING);
    // Track surface: mpv notifies on track-list whenever streams appear, vanish
    // or change selection, so the lists are event-driven — never polled.
    mpv_observe_property(m_mpv, 0, "track-list", MPV_FORMAT_NODE);
    // Buffered-ahead for the scrubber. `demuxer-cache-time` is an *absolute*
    // media timestamp (the last buffered one), which is what we need: it is
    // directly comparable with time-pos, so the delta is genuinely "ahead of the
    // playhead". `demuxer-cache-duration` is measured from the demuxer read
    // head, which already leads playback by the decode queues, so on a network
    // stream it overstates the safe margin by exactly the amount that matters
    // when the cache is nearly empty.
    mpv_observe_property(m_mpv, 0, "demuxer-cache-time", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "speed", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "audio-delay", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "sub-delay", MPV_FORMAT_DOUBLE);
    // Stats: geometry changes rarely, and frame drops are the one counter worth
    // waking the OSD for. The continuously-moving figures (estimated fps,
    // bitrates) are read on demand by videoStats() instead of signalled.
    mpv_observe_property(m_mpv, 0, "video-params", MPV_FORMAT_NODE);
    mpv_observe_property(m_mpv, 0, "frame-drop-count", MPV_FORMAT_INT64);
    mpv_set_wakeup_callback(m_mpv, &MpvPlayer::wakeup, this);
}

MpvPlayer::~MpvPlayer()
{
    if (m_mpv) {
        mpv_set_wakeup_callback(m_mpv, nullptr, nullptr);
        // Render contexts (MpvVideoItem) must already be gone by now: the QML
        // scene is torn down before the Application object graph.
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
    }
}

bool MpvPlayer::command(const char *args[])
{
    if (!m_mpv)
        return false;
    const int rc = mpv_command(m_mpv, args);
    if (rc < 0)
        qCWarning(logPlayback) << "mpv command" << args[0] << "failed:" << mpv_error_string(rc);
    return rc >= 0;
}

void MpvPlayer::load(const QUrl &url, qint64 startMs, LoadId loadId, bool initiallyPaused)
{
    m_loadId = loadId;
    m_pendingLoadId = loadId;
    // Publish the requested position directly. A synthetic zero stamped with
    // the new load id would be accepted by PlayerController and could erase
    // the recovery point before this load produces its first time-pos event.
    resetPerLoadState(loadId, startMs);
    if (!m_mpv) {
        setState(State::Error, loadId);
        emit errorOccurred(QStringLiteral("mpv core unavailable"), loadId);
        return;
    }

    const QByteArray start = QByteArray::number(startMs / 1000.0, 'f', 3);
    mpv_set_option_string(m_mpv, "start", startMs > 0 ? start.constData() : "0");

    // Pause is persistent mpv state. Set it before loadfile so FILE_LOADED's
    // first ready state is Paused when requested, with no Playing interval.
    setPaused(initiallyPaused);
    m_loadInFlight = true;
    setState(State::Loading, loadId);
    const QByteArray urlUtf8 = url.toString(QUrl::FullyEncoded).toUtf8();
    const char *args[] = {"loadfile", urlUtf8.constData(), "replace", nullptr};
    command(args);
}

void MpvPlayer::setPaused(bool paused)
{
    if (!m_mpv)
        return;
    int flag = paused ? 1 : 0;
    mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &flag);
}

void MpvPlayer::stop()
{
    if (m_mpv) {
        const char *args[] = {"stop", nullptr};
        command(args);
    }
    m_loadId = 0;
    m_pendingLoadId = 0;
    resetPerLoadState(0, 0);
    setState(State::Idle, 0);
}

void MpvPlayer::resetPerLoadState(LoadId loadId, qint64 positionMs)
{
    if (m_positionMs != positionMs) {
        m_positionMs = positionMs;
        emit positionChanged(positionMs, loadId);
    }
    if (m_durationMs != 0) {
        m_durationMs = 0;
        emit durationChanged(0, loadId);
    }
    m_cacheEndMs = -1;
    if (m_lastBufferedMs != 0) {
        m_lastBufferedMs = 0;
        emit bufferedMsChanged();
    }
    if (m_buffering) {
        m_buffering = false;
        emit bufferingChanged(false, loadId);
    }
    if (!m_audioTracks.isEmpty() || !m_subtitleTracks.isEmpty() || m_audioTrackId >= 0 ||
        m_subtitleTrackId >= 0) {
        m_audioTracks.clear();
        m_subtitleTracks.clear();
        m_audioTrackId = -1;
        m_subtitleTrackId = -1;
        emit tracksChanged();
    }
    if (!m_hwdecCurrent.isEmpty()) {
        m_hwdecCurrent.clear();
        emit decoderInfoChanged();
    }
    emit videoStatsChanged();
}

void MpvPlayer::seekTo(qint64 positionMs)
{
    if (!m_mpv || m_state == State::Idle)
        return;
    const QByteArray target = QByteArray::number(positionMs / 1000.0, 'f', 3);
    const char *args[] = {"seek", target.constData(), "absolute", nullptr};
    command(args);
}

void MpvPlayer::cycleAudioTrack()
{
    const char *args[] = {"cycle", "audio", nullptr};
    command(args);
}

void MpvPlayer::cycleSubtitleTrack()
{
    const char *args[] = {"cycle", "sub", nullptr};
    command(args);
}

void MpvPlayer::refreshTracks()
{
    if (!m_mpv)
        return;

    mpv_node node;
    if (mpv_get_property(m_mpv, "track-list", MPV_FORMAT_NODE, &node) < 0)
        return;
    const QVariantList raw = variantFromNode(node).toList();
    mpv_free_node_contents(&node);

    const QVariantList audio = mpvtracks::buildTracks(raw, QStringLiteral("audio"));
    const QVariantList subtitles = mpvtracks::buildTracks(raw, QStringLiteral("sub"));
    const int audioId = mpvtracks::selectedId(audio);
    const int subtitleId = mpvtracks::selectedId(subtitles);

    if (audio == m_audioTracks && subtitles == m_subtitleTracks && audioId == m_audioTrackId &&
        subtitleId == m_subtitleTrackId) {
        return;
    }

    m_audioTracks = audio;
    m_subtitleTracks = subtitles;
    m_audioTrackId = audioId;
    m_subtitleTrackId = subtitleId;
    emit tracksChanged();
}

void MpvPlayer::setTrackProperty(const char *name, int id)
{
    if (!m_mpv)
        return;
    // -1 is the contract's "off"; mpv spells that "no" for both aid and sid.
    const QByteArray value = id < 0 ? QByteArrayLiteral("no") : QByteArray::number(id);
    const int rc = mpv_set_property_string(m_mpv, name, value.constData());
    if (rc < 0)
        qCWarning(logPlayback) << "mpv set" << name << "failed:" << mpv_error_string(rc);
    // No optimistic update: the aid/sid observers fire and refreshTracks() reads
    // back what mpv actually did, so a rejected id cannot desync the OSD.
}

void MpvPlayer::setAudioTrack(int id)
{
    setTrackProperty("aid", id);
}

void MpvPlayer::setSubtitleTrack(int id)
{
    setTrackProperty("sid", id);
}

qint64 MpvPlayer::bufferedMs() const
{
    if (m_cacheEndMs < 0)
        return 0;
    return qMax<qint64>(0, m_cacheEndMs - m_positionMs);
}

void MpvPlayer::setPlaybackSpeed(qreal speed)
{
    if (!m_mpv)
        return;
    double value = qBound(kMinSpeed, speed, kMaxSpeed);
    mpv_set_property(m_mpv, "speed", MPV_FORMAT_DOUBLE, &value);
}

void MpvPlayer::setSubtitleStyle(const QString &font, int scale, const QString &color,
                                 int background, int position)
{
    if (!m_mpv)
        return;

    // Empty means "leave mpv's own choice alone" — sub-font does not accept an
    // empty string, and writing one would drop styling entirely.
    if (!font.isEmpty()) {
        const QByteArray family = font.toUtf8();
        mpv_set_property_string(m_mpv, "sub-font", family.constData());
    }

    double sub = qBound(0.5, scale / 100.0, 3.0);
    mpv_set_property(m_mpv, "sub-scale", MPV_FORMAT_DOUBLE, &sub);

    // mpv takes colours as "#RRGGBB" or "#AARRGGBB"; anything else makes it
    // reject the property and keep the previous look, so the value is checked
    // rather than passed through.
    static const QRegularExpression hex(QStringLiteral("^#[0-9A-Fa-f]{6}$"));
    const QByteArray rgb =
        (hex.match(color).hasMatch() ? color : QStringLiteral("#FFFFFF")).toUtf8();
    mpv_set_property_string(m_mpv, "sub-color", rgb.constData());

    // Two different mpv knobs render a "background": sub-back-color fills the
    // glyph box, and sub-border-size draws an outline. At zero opacity the box
    // must disappear AND the outline come back, or the text is unreadable over
    // a bright frame.
    const int alpha = qBound(0, background, 100) * 255 / 100;
    const QByteArray back =
        QStringLiteral("#%1000000").arg(alpha, 2, 16, QLatin1Char('0')).toUpper().toUtf8();
    mpv_set_property_string(m_mpv, "sub-back-color", back.constData());
    double border = background > 50 ? 0.0 : 3.0;
    mpv_set_property(m_mpv, "sub-border-size", MPV_FORMAT_DOUBLE, &border);

    int64_t pos = qBound(0, position, 150);
    mpv_set_property(m_mpv, "sub-pos", MPV_FORMAT_INT64, &pos);
}

void MpvPlayer::setAudioDelayMs(int ms)
{
    if (!m_mpv)
        return;
    double seconds = ms / 1000.0;
    mpv_set_property(m_mpv, "audio-delay", MPV_FORMAT_DOUBLE, &seconds);
}

void MpvPlayer::setSubtitleDelayMs(int ms)
{
    if (!m_mpv)
        return;
    double seconds = ms / 1000.0;
    mpv_set_property(m_mpv, "sub-delay", MPV_FORMAT_DOUBLE, &seconds);
}

void MpvPlayer::setReplayGain(const QString &mode)
{
    if (!m_mpv)
        return;
    // mpv's vocabulary is no|track|album; ours spells the first one "off".
    // Anything unrecognised is treated as off rather than passed through:
    // mpv rejects an unknown choice and leaves the previous gain in place,
    // which is the one outcome the user cannot see and cannot undo.
    const char *value = "no";
    if (mode == QLatin1String("track"))
        value = "track";
    else if (mode == QLatin1String("album"))
        value = "album";
    const int rc = mpv_set_property_string(m_mpv, "replaygain", value);
    if (rc < 0)
        qCWarning(logPlayback) << "mpv set replaygain failed:" << mpv_error_string(rc);
}

QVariantMap MpvPlayer::videoStats() const
{
    QVariantMap stats;
    if (!m_mpv)
        return stats;

    // Every read is allowed to fail: mpv answers "property unavailable" for
    // anything the current file or decoder cannot supply, and an absent key is
    // an honest "unknown" where a zero would read as a measurement.
    const auto putInt = [&](const char *property, const char *key) {
        int64_t value = 0;
        if (mpv_get_property(m_mpv, property, MPV_FORMAT_INT64, &value) >= 0)
            stats.insert(QString::fromLatin1(key), QVariant::fromValue<qint64>(value));
    };
    const auto putRate = [&](const char *property, const char *key) {
        double value = 0.0;
        if (mpv_get_property(m_mpv, property, MPV_FORMAT_DOUBLE, &value) >= 0 && value > 0.0)
            stats.insert(QString::fromLatin1(key), value);
    };
    // Bitrates are floats in mpv (a 10-second rolling average) but are only ever
    // shown as whole bits per second.
    const auto putBitrate = [&](const char *property, const char *key) {
        double value = 0.0;
        if (mpv_get_property(m_mpv, property, MPV_FORMAT_DOUBLE, &value) >= 0 && value > 0.0)
            stats.insert(QString::fromLatin1(key), static_cast<qint64>(qRound64(value)));
    };
    const auto putString = [&](const char *property, const char *key) {
        if (stats.contains(QString::fromLatin1(key)))
            return;
        char *value = mpv_get_property_string(m_mpv, property);
        if (!value)
            return;
        const QString text = QString::fromUtf8(value);
        mpv_free(value);
        if (!text.isEmpty())
            stats.insert(QString::fromLatin1(key), text);
    };

    putInt("width", "width");
    putInt("height", "height");
    putString("current-tracks/video/codec", "codec");
    putString("video-codec", "codec"); // descriptive fallback, e.g. "H.264 (High)"
    putBitrate("video-bitrate", "videoBitrate");

    putString("current-tracks/audio/codec", "audioCodec");
    putString("audio-codec-name", "audioCodec");
    putInt("audio-params/channel-count", "audioChannels");
    putBitrate("audio-bitrate", "audioBitrate");

    putRate("current-tracks/video/demux-fps", "fps");
    putRate("container-fps", "containerFps");
    putRate("estimated-vf-fps", "estimatedFps");
    putInt("frame-drop-count", "droppedFrames");

    // Same source as decoderInfo(): mpv's hwdec-current, cached by the observer.
    // Present-but-empty means "decoding in software", which is a real answer.
    stats.insert(QStringLiteral("hwdec"), m_hwdecCurrent);
    return stats;
}

void MpvPlayer::frameStep(int direction)
{
    if (!m_mpv || m_state == State::Idle)
        return;
    // Stepping implies stopping: every player behaves this way, and mpv's own
    // frame-step pauses as a side effect, so making it explicit keeps our
    // `paused` state honest rather than letting it drift from the engine's.
    if (m_state != State::Paused)
        setPaused(true);
    const char *forward[] = {"frame-step", nullptr};
    const char *backward[] = {"frame-back-step", nullptr};
    command(direction < 0 ? backward : forward);
}

void MpvPlayer::setAbLoop(qint64 aMs, qint64 bMs)
{
    if (!m_mpv)
        return;
    // mpv takes seconds, and the string "no" to clear a bound. Writing a
    // negative number instead would set a loop point before the file starts.
    const auto apply = [this](const char *name, qint64 ms) {
        if (ms < 0) {
            mpv_set_property_string(m_mpv, name, "no");
            return;
        }
        double seconds = ms / 1000.0;
        mpv_set_property(m_mpv, name, MPV_FORMAT_DOUBLE, &seconds);
    };
    apply("ab-loop-a", aMs);
    apply("ab-loop-b", bMs);
}

bool MpvPlayer::screenshotToFile(const QString &path)
{
    const QByteArray target = path.toUtf8();
    // "video" = the decoded frame without OSD or subtitles; the "window" mode
    // has nothing to grab under vo=libmpv.
    const char *args[] = {"screenshot-to-file", target.constData(), "video", nullptr};
    return command(args);
}

void MpvPlayer::setVolume(int percent)
{
    if (!m_mpv)
        return;
    double volume = qBound(0, percent, kMaxVolume);
    mpv_set_property(m_mpv, "volume", MPV_FORMAT_DOUBLE, &volume);
}

int MpvPlayer::volume() const
{
    if (!m_mpv)
        return kDefaultVolume;
    double volume = 0.0;
    if (mpv_get_property(m_mpv, "volume", MPV_FORMAT_DOUBLE, &volume) < 0)
        return kDefaultVolume;
    return qBound(0, static_cast<int>(qRound(volume)), kMaxVolume);
}

bool MpvPlayer::muted() const
{
    if (!m_mpv)
        return false;
    int flag = 0;
    if (mpv_get_property(m_mpv, "mute", MPV_FORMAT_FLAG, &flag) < 0)
        return false;
    return flag != 0;
}

void MpvPlayer::setMuted(bool muted)
{
    if (!m_mpv)
        return;
    // mpv's own mute flag, not volume 0: unmuting has to come back to the level
    // the user picked, and only the engine's flag preserves that for free.
    int flag = muted ? 1 : 0;
    mpv_set_property(m_mpv, "mute", MPV_FORMAT_FLAG, &flag);
}

void MpvPlayer::setState(State state, LoadId loadId)
{
    if (loadId != m_loadId) {
        emit stateChanged(state, loadId);
        return;
    }
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(state, loadId);
}

void MpvPlayer::wakeup(void *ctx)
{
    // Called from mpv's internal thread — hop to the GUI thread. A single
    // drain consumes the complete mpv event queue, so posting another task
    // while one is already pending only grows Qt's queue without doing work.
    auto *player = static_cast<MpvPlayer *>(ctx);
    if (!player->m_wakeupGate.requestDrain())
        return;
    QMetaObject::invokeMethod(player, "drainEvents", Qt::QueuedConnection);
}

void MpvPlayer::drainEvents()
{
    m_wakeupGate.beginDrain();
    if (!m_mpv)
        return;

    while (true) {
        mpv_event *event = mpv_wait_event(m_mpv, 0);
        if (event->event_id == MPV_EVENT_NONE)
            break;

        switch (event->event_id) {
        case MPV_EVENT_START_FILE:
            m_eventLoadId = m_pendingLoadId;
            break;
        case MPV_EVENT_FILE_LOADED: {
            if (m_eventLoadId != m_loadId)
                break;
            m_loadInFlight = false;
            int paused = 0;
            mpv_get_property(m_mpv, "pause", MPV_FORMAT_FLAG, &paused);
            setState(paused ? State::Paused : State::Playing, m_eventLoadId);
            refreshTracks();
            emit videoStatsChanged();
            break;
        }
        case MPV_EVENT_END_FILE: {
            const auto *end = static_cast<mpv_event_end_file *>(event->data);
            if (end->reason == MPV_END_FILE_REASON_ERROR) {
                const QString message = QString::fromUtf8(mpv_error_string(end->error));
                qCWarning(logPlayback) << "playback failed:" << message;
                setState(State::Error, m_eventLoadId);
                emit errorOccurred(message, m_eventLoadId);
            } else if (end->reason == MPV_END_FILE_REASON_EOF) {
                setState(State::Ended, m_eventLoadId);
                emit endReached(m_eventLoadId);
            }
            // STOP/REDIRECT/QUIT: state handled by the caller (stop()).
            break;
        }
        case MPV_EVENT_PROPERTY_CHANGE: {
            if (m_eventLoadId != m_loadId)
                break;
            const auto *change = static_cast<mpv_event_property *>(event->data);
            if (qstrcmp(change->name, "time-pos") == 0 && change->format == MPV_FORMAT_DOUBLE) {
                m_positionMs = msFromSeconds(*static_cast<double *>(change->data));
                emit positionChanged(m_positionMs, m_eventLoadId);
            } else if (qstrcmp(change->name, "duration") == 0 &&
                       change->format == MPV_FORMAT_DOUBLE) {
                m_durationMs = msFromSeconds(*static_cast<double *>(change->data));
                emit durationChanged(m_durationMs, m_eventLoadId);
            } else if (qstrcmp(change->name, "hwdec-current") == 0 &&
                       change->format == MPV_FORMAT_STRING) {
                const QString hwdec = QString::fromUtf8(*static_cast<char **>(change->data));
                if (hwdec != m_hwdecCurrent) {
                    m_hwdecCurrent = hwdec == QLatin1String("no") ? QString() : hwdec;
                    qCInfo(logPlayback) << "hwdec-current:"
                                        << (m_hwdecCurrent.isEmpty() ? "software" : m_hwdecCurrent);
                    emit decoderInfoChanged();
                    emit videoStatsChanged();
                }
            } else if (qstrcmp(change->name, "volume") == 0 &&
                       change->format == MPV_FORMAT_DOUBLE) {
                const int percent = qBound(
                    0, static_cast<int>(qRound(*static_cast<double *>(change->data))), kMaxVolume);
                emit volumeChanged(percent);
            } else if (qstrcmp(change->name, "mute") == 0 && change->format == MPV_FORMAT_FLAG) {
                emit mutedChanged(*static_cast<int *>(change->data) != 0);
            } else if ((qstrcmp(change->name, "aid") == 0 || qstrcmp(change->name, "sid") == 0) &&
                       change->format == MPV_FORMAT_STRING) {
                // The list surface follows every selection change, including the
                // implicit ones during load; the toast keeps its old guard so it
                // still only fires for user-visible switches.
                refreshTracks();
                if (m_state == State::Idle || m_state == State::Loading)
                    break;
                const bool audio = change->name[0] == 'a';
                const QByteArray id = *static_cast<char **>(change->data);
                QString label;
                if (id == "no" || id == "auto") {
                    label = QStringLiteral("off");
                } else {
                    const QByteArray base = (audio ? QByteArrayLiteral("current-tracks/audio/")
                                                   : QByteArrayLiteral("current-tracks/sub/"));
                    char *title = mpv_get_property_string(m_mpv, (base + "title").constData());
                    char *lang = mpv_get_property_string(m_mpv, (base + "lang").constData());
                    if (lang)
                        label = QString::fromUtf8(lang);
                    if (title)
                        label += (label.isEmpty() ? QString() : QStringLiteral(" — ")) +
                                 QString::fromUtf8(title);
                    if (label.isEmpty())
                        label = QStringLiteral("track %1").arg(QString::fromUtf8(id));
                    mpv_free(title);
                    mpv_free(lang);
                }
                emit trackChanged((audio ? tr("Audio: %1") : tr("Subtitles: %1")).arg(label));
            } else if (qstrcmp(change->name, "track-list") == 0) {
                refreshTracks();
            } else if (qstrcmp(change->name, "demuxer-cache-time") == 0) {
                // Absent (MPV_FORMAT_NONE) whenever nothing is cached — that is
                // "unknown", not "zero ahead", so the bar draws no second fill.
                m_cacheEndMs = change->format == MPV_FORMAT_DOUBLE
                    ? msFromSeconds(*static_cast<double *>(change->data))
                    : -1;
                const qint64 ahead = bufferedMs();
                if (qAbs(ahead - m_lastBufferedMs) >= kBufferedEpsilonMs ||
                    (ahead == 0 && m_lastBufferedMs != 0)) {
                    m_lastBufferedMs = ahead;
                    emit bufferedMsChanged();
                }
            } else if (qstrcmp(change->name, "speed") == 0 &&
                       change->format == MPV_FORMAT_DOUBLE) {
                const qreal speed = *static_cast<double *>(change->data);
                if (!qFuzzyCompare(speed, m_speed)) {
                    m_speed = speed;
                    emit playbackSpeedChanged();
                }
            } else if (qstrcmp(change->name, "audio-delay") == 0 &&
                       change->format == MPV_FORMAT_DOUBLE) {
                const int ms = static_cast<int>(
                    qRound(*static_cast<double *>(change->data) * 1000.0));
                if (ms != m_audioDelayMs) {
                    m_audioDelayMs = ms;
                    emit audioDelayChanged();
                }
            } else if (qstrcmp(change->name, "sub-delay") == 0 &&
                       change->format == MPV_FORMAT_DOUBLE) {
                const int ms = static_cast<int>(
                    qRound(*static_cast<double *>(change->data) * 1000.0));
                if (ms != m_subtitleDelayMs) {
                    m_subtitleDelayMs = ms;
                    emit subtitleDelayChanged();
                }
            } else if (qstrcmp(change->name, "video-params") == 0 ||
                       qstrcmp(change->name, "frame-drop-count") == 0) {
                emit videoStatsChanged();
            } else if (qstrcmp(change->name, "paused-for-cache") == 0 &&
                       change->format == MPV_FORMAT_FLAG) {
                const bool buffering = *static_cast<int *>(change->data) != 0;
                if (buffering != m_buffering) {
                    m_buffering = buffering;
                    emit bufferingChanged(buffering, m_eventLoadId);
                }
            } else if (qstrcmp(change->name, "pause") == 0 && change->format == MPV_FORMAT_FLAG &&
                       !m_loadInFlight && (m_state == State::Playing || m_state == State::Paused)) {
                setState(*static_cast<int *>(change->data) ? State::Paused : State::Playing,
                         m_eventLoadId);
            }
            break;
        }
        case MPV_EVENT_LOG_MESSAGE: {
            const auto *msg = static_cast<mpv_event_log_message *>(event->data);
            qCWarning(logPlayback).noquote()
                << "mpv:" << redactSensitiveText(QString::fromUtf8(msg->prefix))
                << redactSensitiveText(QString::fromUtf8(msg->text).trimmed());
            break;
        }
        default:
            break;
        }
    }
}

} // namespace strmqt
