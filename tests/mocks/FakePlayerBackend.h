#pragma once

#include "playback/PlayerBackend.h"

#include <QList>
#include <QStringList>

// Scriptable engine for PlayerController tests: records calls, lets the test
// drive state transitions.
class FakePlayerBackend : public strmqt::PlayerBackend
{
    Q_OBJECT

public:
    using strmqt::PlayerBackend::PlayerBackend;

    QString engineName() const override { return QStringLiteral("fake"); }

    void load(const QUrl &url, qint64 startMs, LoadId loadId,
              bool initiallyPaused = false) override
    {
        loadedUrls.append(url);
        loadedStarts.append(startMs);
        loadedIds.append(loadId);
        loadedInitiallyPaused.append(initiallyPaused);
        m_loadId = loadId;
        resetPerLoadState(loadId, startMs);
        m_state = State::Loading;
        emit stateChanged(m_state, loadId);
    }

    void setPaused(bool paused) override
    {
        if (m_state != State::Playing && m_state != State::Paused)
            return;
        simulateState(paused ? State::Paused : State::Playing);
    }

    void stop() override
    {
        stopCalls++;
        m_loadId = 0;
        resetPerLoadState(0, 0);
        simulateState(State::Idle, 0);
    }

    void seekTo(qint64 positionMs) override
    {
        seeks.append(positionMs);
        simulatePosition(positionMs);
    }

    void setVolume(int) override {}

    // ── Track surface ─────────────────────────────────────────────────────────
    QVariantList audioTracks() const override { return m_audioTracks; }
    QVariantList subtitleTracks() const override { return m_subtitleTracks; }
    int currentAudioTrackId() const override { return m_audioTrackId; }
    int currentSubtitleTrackId() const override { return m_subtitleTrackId; }

    void setAudioTrack(int id) override
    {
        audioTrackRequests.append(id);
        if (deferTrackReadback)
            return;
        m_audioTrackId = selectIn(m_audioTracks, id);
        emit tracksChanged();
    }

    void setSubtitleTrack(int id) override
    {
        subtitleTrackRequests.append(id);
        if (deferTrackReadback)
            return;
        m_subtitleTrackId = selectIn(m_subtitleTracks, id);
        emit tracksChanged();
    }

    qint64 bufferedMs() const override { return m_bufferedMs; }
    qreal playbackSpeed() const override { return m_speed; }
    void setPlaybackSpeed(qreal speed) override
    {
        speedRequests.append(speed);
        if (deferPlaybackSettingsReadback)
            return;
        m_speed = speed;
        emit playbackSpeedChanged();
    }
    int audioDelayMs() const override { return m_audioDelayMs; }
    void setAudioDelayMs(int ms) override
    {
        audioDelayRequests.append(ms);
        if (deferPlaybackSettingsReadback)
            return;
        m_audioDelayMs = ms;
        emit audioDelayChanged();
    }
    int subtitleDelayMs() const override { return m_subtitleDelayMs; }
    void setSubtitleDelayMs(int ms) override
    {
        subtitleDelayRequests.append(ms);
        if (deferPlaybackSettingsReadback)
            return;
        m_subtitleDelayMs = ms;
        emit subtitleDelayChanged();
    }
    void setReplayGain(const QString &mode) override { replayGainModes.append(mode); }
    QVariantMap videoStats() const override { return m_videoStats; }
    bool screenshotToFile(const QString &path) override
    {
        screenshots.append(path);
        return screenshotSucceeds;
    }

    // Build a track map shaped exactly like a real engine's, so tests and the
    // OSD see the same keys without hand-writing them each time.
    static QVariantMap makeTrack(int id, const QString &title, const QString &language = {},
                                 const QString &codec = {}, bool selected = false)
    {
        QVariantMap track;
        track[QStringLiteral("id")] = id;
        track[QStringLiteral("title")] = title;
        track[QStringLiteral("language")] = language;
        track[QStringLiteral("codec")] = codec;
        track[QStringLiteral("channels")] = 0;
        track[QStringLiteral("channelLayout")] = QString();
        track[QStringLiteral("isDefault")] = false;
        track[QStringLiteral("isForced")] = false;
        track[QStringLiteral("isExternal")] = false;
        track[QStringLiteral("selected")] = selected;
        return track;
    }

    State state() const override { return m_state; }
    bool buffering() const override { return m_buffering; }
    qint64 positionMs() const override { return m_positionMs; }
    qint64 durationMs() const override { return m_durationMs; }

    // Test drivers
    void simulateBuffering(bool buffering, LoadId loadId = 0)
    {
        loadId = resolvedLoadId(loadId);
        if (loadId == m_loadId)
            m_buffering = buffering;
        emit bufferingChanged(buffering, loadId);
    }
    void simulateState(State state, LoadId loadId = 0)
    {
        loadId = resolvedLoadId(loadId);
        if (loadId == m_loadId)
            m_state = state;
        emit stateChanged(state, loadId);
    }
    void simulatePosition(qint64 ms, LoadId loadId = 0)
    {
        loadId = resolvedLoadId(loadId);
        if (loadId == m_loadId)
            m_positionMs = ms;
        emit positionChanged(ms, loadId);
    }
    void simulateDuration(qint64 ms, LoadId loadId = 0)
    {
        loadId = resolvedLoadId(loadId);
        if (loadId == m_loadId)
            m_durationMs = ms;
        emit durationChanged(ms, loadId);
    }
    void simulateError(const QString &message, LoadId loadId = 0)
    {
        loadId = resolvedLoadId(loadId);
        if (loadId == m_loadId)
            m_state = State::Error;
        emit stateChanged(State::Error, loadId);
        emit errorOccurred(message, loadId);
    }
    void simulateEnd(LoadId loadId = 0)
    {
        loadId = resolvedLoadId(loadId);
        if (loadId == m_loadId)
            m_state = State::Ended;
        emit stateChanged(State::Ended, loadId);
        emit endReached(loadId);
    }

    void simulateTracks(const QVariantList &audio, const QVariantList &subtitles)
    {
        m_audioTracks = audio;
        m_subtitleTracks = subtitles;
        m_audioTrackId = selectedIdOf(audio);
        m_subtitleTrackId = selectedIdOf(subtitles);
        emit tracksChanged();
    }
    // The engine re-publishing its track list without the requested selection
    // having taken effect yet: mpv does this whenever the list itself changes.
    void republishTracksUnchanged() { emit tracksChanged(); }
    void confirmAudioTrack(int id)
    {
        m_audioTrackId = selectIn(m_audioTracks, id);
        emit tracksChanged();
    }
    void confirmSubtitleTrack(int id)
    {
        m_subtitleTrackId = selectIn(m_subtitleTracks, id);
        emit tracksChanged();
    }
    void confirmPlaybackSpeed(qreal speed)
    {
        m_speed = speed;
        emit playbackSpeedChanged();
    }
    void confirmAudioDelay(int ms)
    {
        m_audioDelayMs = ms;
        emit audioDelayChanged();
    }
    void confirmSubtitleDelay(int ms)
    {
        m_subtitleDelayMs = ms;
        emit subtitleDelayChanged();
    }
    void simulateBufferedMs(qint64 ms)
    {
        m_bufferedMs = ms;
        emit bufferedMsChanged();
    }
    void simulateVideoStats(const QVariantMap &stats)
    {
        m_videoStats = stats;
        emit videoStatsChanged();
    }
    void simulateTrackDescription(const QString &description) { emit trackChanged(description); }

    QList<QUrl> loadedUrls;
    QList<qint64> loadedStarts;
    QList<LoadId> loadedIds;
    QList<bool> loadedInitiallyPaused;
    QList<qint64> seeks;
    QList<int> audioTrackRequests;
    QList<int> subtitleTrackRequests;
    QList<qreal> speedRequests;
    QList<int> audioDelayRequests;
    QList<int> subtitleDelayRequests;
    QStringList screenshots;
    bool screenshotSucceeds = true;
    QStringList replayGainModes;
    int stopCalls = 0;
    bool deferTrackReadback = false;
    bool deferPlaybackSettingsReadback = false;

private:
    LoadId resolvedLoadId(LoadId loadId) const { return loadId == 0 ? m_loadId : loadId; }

    void resetPerLoadState(LoadId loadId, qint64 positionMs)
    {
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
        if (!m_audioTracks.isEmpty() || !m_subtitleTracks.isEmpty()) {
            m_audioTracks.clear();
            m_subtitleTracks.clear();
            m_audioTrackId = -1;
            m_subtitleTrackId = -1;
            emit tracksChanged();
        }
        if (m_bufferedMs != 0) {
            m_bufferedMs = 0;
            emit bufferedMsChanged();
        }
        if (!m_videoStats.isEmpty()) {
            m_videoStats.clear();
            emit videoStatsChanged();
        }
    }

    static int selectedIdOf(const QVariantList &tracks)
    {
        for (const QVariant &entry : tracks) {
            const QVariantMap track = entry.toMap();
            if (track.value(QStringLiteral("selected")).toBool())
                return track.value(QStringLiteral("id")).toInt();
        }
        return -1;
    }

    // Move the "selected" flag onto id (or nowhere, for -1), the way a real
    // engine's read-back would; returns the id that actually took effect.
    static int selectIn(QVariantList &tracks, int id)
    {
        int applied = -1;
        for (QVariant &entry : tracks) {
            QVariantMap track = entry.toMap();
            const bool selected = id >= 0 && track.value(QStringLiteral("id")).toInt() == id;
            if (selected)
                applied = id;
            track[QStringLiteral("selected")] = selected;
            entry = track;
        }
        return applied;
    }

    State m_state = State::Idle;
    LoadId m_loadId = 0;
    bool m_buffering = false;
    qint64 m_positionMs = 0;
    qint64 m_durationMs = 0;

    QVariantList m_audioTracks;
    QVariantList m_subtitleTracks;
    int m_audioTrackId = -1;
    int m_subtitleTrackId = -1;
    qint64 m_bufferedMs = 0;
    qreal m_speed = 1.0;
    int m_audioDelayMs = 0;
    int m_subtitleDelayMs = 0;
    QVariantMap m_videoStats;
};
