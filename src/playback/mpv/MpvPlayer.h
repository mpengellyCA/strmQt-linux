#pragma once

#include "playback/PlayerBackend.h"

#include <QCoreApplication>
#include <QStringList>

struct mpv_handle;

namespace strmqt {

// Pure translation of mpv's `track-list` into the PlayerBackend track maps.
// Header-only and mpv-free on purpose: MpvPlayer converts the mpv_node to a
// QVariantList and hands it here, so the mapping can be unit-tested against a
// synthetic node without a running mpv core (AGENTS.md: src/playback needs tests).
namespace mpvtracks {

// Display label for one raw track-list entry, ordinal being its 1-based index
// within its own type. Fallback chain: the track's own title, else
// language/codec/layout, else "Track N". Never empty — the UI must not have to
// re-derive a name.
inline QString labelFor(const QVariantMap &raw, int ordinal)
{
    const QString title = raw.value(QStringLiteral("title")).toString().trimmed();
    if (!title.isEmpty())
        return title;

    QStringList parts;
    const QString language = raw.value(QStringLiteral("lang")).toString().trimmed();
    QString codec = raw.value(QStringLiteral("codec")).toString().trimmed();
    if (codec.isEmpty())
        codec = raw.value(QStringLiteral("codec-desc")).toString().trimmed();
    const QString layout = raw.value(QStringLiteral("demux-channels")).toString().trimmed();
    if (!language.isEmpty())
        parts << language;
    if (!codec.isEmpty())
        parts << codec;
    if (!layout.isEmpty())
        parts << layout;
    if (!parts.isEmpty())
        return parts.join(QStringLiteral(" · "));

    return QCoreApplication::translate("strmqt::mpvtracks", "Track %1").arg(ordinal);
}

// Filter one mpv track type ("audio" / "sub" / "video") out of the raw
// track-list and shape each entry to the PlayerBackend contract.
inline QVariantList buildTracks(const QVariantList &rawTrackList, const QString &type)
{
    QVariantList tracks;
    int ordinal = 0;
    for (const QVariant &entry : rawTrackList) {
        const QVariantMap raw = entry.toMap();
        if (raw.value(QStringLiteral("type")).toString() != type)
            continue;
        ++ordinal;

        QString codec = raw.value(QStringLiteral("codec")).toString();
        if (codec.isEmpty())
            codec = raw.value(QStringLiteral("codec-desc")).toString();

        QVariantMap track;
        track[QStringLiteral("id")] = raw.value(QStringLiteral("id")).toInt();
        track[QStringLiteral("title")] = labelFor(raw, ordinal);
        track[QStringLiteral("language")] = raw.value(QStringLiteral("lang")).toString();
        track[QStringLiteral("codec")] = codec;
        track[QStringLiteral("channels")] =
            raw.value(QStringLiteral("demux-channel-count")).toInt();
        track[QStringLiteral("channelLayout")] =
            raw.value(QStringLiteral("demux-channels")).toString();
        track[QStringLiteral("isDefault")] = raw.value(QStringLiteral("default")).toBool();
        track[QStringLiteral("isForced")] = raw.value(QStringLiteral("forced")).toBool();
        track[QStringLiteral("isExternal")] = raw.value(QStringLiteral("external")).toBool();
        track[QStringLiteral("selected")] = raw.value(QStringLiteral("selected")).toBool();
        tracks.append(track);
    }
    return tracks;
}

// Id of the selected entry in a list already shaped by buildTracks; -1 for none
// (mpv's `sid=no`, or a file with no such stream).
inline int selectedId(const QVariantList &tracks)
{
    for (const QVariant &entry : tracks) {
        const QVariantMap track = entry.toMap();
        if (track.value(QStringLiteral("selected")).toBool())
            return track.value(QStringLiteral("id")).toInt();
    }
    return -1;
}

} // namespace mpvtracks

// libmpv-backed engine (raw C client API, PLAN §3.2). Owns the mpv core; video
// output goes through MpvVideoItem, which creates a render context on this
// handle. Events arrive via mpv's wakeup callback and are drained on the GUI
// thread.
class MpvPlayer : public PlayerBackend
{
    Q_OBJECT

public:
    // toneMapping: libplacebo curve name ("bt.2446a", "spline", ...); empty = default.
    explicit MpvPlayer(const QString &toneMapping = {}, QObject *parent = nullptr);
    ~MpvPlayer() override;

    QString engineName() const override { return QStringLiteral("mpv"); }

    void load(const QUrl &url, qint64 startMs, LoadId loadId,
              bool initiallyPaused = false) override;
    void setPaused(bool paused) override;
    void stop() override;
    void seekTo(qint64 positionMs) override;
    void setVolume(int percent) override;
    // Read straight off the mpv core, not off a member we set earlier: an mpv
    // config file, a script, or the OSD can move "volume" behind our back.
    int volume() const override;
    bool supportsMute() const override { return true; }
    bool muted() const override;
    void setMuted(bool muted) override;
    void cycleAudioTrack() override;
    void cycleSubtitleTrack() override;

    QVariantList audioTracks() const override { return m_audioTracks; }
    QVariantList subtitleTracks() const override { return m_subtitleTracks; }
    int currentAudioTrackId() const override { return m_audioTrackId; }
    int currentSubtitleTrackId() const override { return m_subtitleTrackId; }
    void setAudioTrack(int id) override;
    void setSubtitleTrack(int id) override;

    // Cached, not read live: the value is a *delta* against positionMs, and
    // recomputing it from the stored cache end keeps it correct between mpv's
    // own cache notifications without a blocking property read per binding pass.
    qint64 bufferedMs() const override;
    qreal playbackSpeed() const override { return m_speed; }
    void setPlaybackSpeed(qreal speed) override;
    int audioDelayMs() const override { return m_audioDelayMs; }
    void setAudioDelayMs(int ms) override;
    int subtitleDelayMs() const override { return m_subtitleDelayMs; }
    void setSubtitleStyle(const QString &font, int scale, const QString &color, int background,
                          int position) override;
    void setSubtitleDelayMs(int ms) override;
    void setReplayGain(const QString &mode) override;
    QVariantMap videoStats() const override;
    bool screenshotToFile(const QString &path) override;
    bool supportsFrameStep() const override { return true; }
    void frameStep(int direction) override;
    void setAbLoop(qint64 aMs, qint64 bMs) override;

    State state() const override { return m_state; }
    bool buffering() const override { return m_buffering; }
    QString decoderInfo() const override { return m_hwdecCurrent; }
    qint64 positionMs() const override { return m_positionMs; }
    qint64 durationMs() const override { return m_durationMs; }

    // For MpvVideoItem's render context; never used to bypass this wrapper.
    mpv_handle *handle() const { return m_mpv; }

private:
    Q_INVOKABLE void drainEvents();
    void setState(State state, LoadId loadId);
    void resetPerLoadState(LoadId loadId, qint64 positionMs);
    bool command(const char *args[]);
    // Re-read `track-list` and rebuild both lists; emits tracksChanged on change.
    void refreshTracks();
    void setTrackProperty(const char *name, int id);

    mpv_handle *m_mpv = nullptr;
    State m_state = State::Idle;
    qint64 m_positionMs = 0;
    qint64 m_durationMs = 0;
    bool m_loadInFlight = false;
    LoadId m_loadId = 0;
    LoadId m_eventLoadId = 0;
    LoadId m_pendingLoadId = 0;
    bool m_buffering = false;
    QString m_hwdecCurrent;

    QVariantList m_audioTracks;
    QVariantList m_subtitleTracks;
    int m_audioTrackId = -1;
    int m_subtitleTrackId = -1;
    // Absolute media timestamp of the end of the demuxer cache, in ms; -1 when
    // mpv has not reported one (nothing cached / no file loaded).
    qint64 m_cacheEndMs = -1;
    qint64 m_lastBufferedMs = 0;
    qreal m_speed = 1.0;
    int m_audioDelayMs = 0;
    int m_subtitleDelayMs = 0;
};

} // namespace strmqt
