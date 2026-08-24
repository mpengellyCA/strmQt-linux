#include "PlayerController.h"

#include "core/Log.h"
#include "core/Settings.h"
#include "server/dto/MediaItem.h" // kTicksPerMs
#include "server/emby/EmbyClient.h"

#include <QDir>
#include <QRegularExpression>
#include <QStandardPaths>

#include <QScopedValueRollback>

#include <utility>

namespace strmqt {

namespace {
constexpr int kProgressIntervalMs = 10'000;
constexpr int kWatchdogTickMs = 2'000;
constexpr int kPersistIntervalMs = 5'000;
constexpr qint64 kTailEpsilonMs = 5'000;
constexpr int kMaxRecoverRetries = 3;
// ARCHITECTURE.md: the Up Next card lives in the last 30 s of an item.
constexpr qint64 kUpNextWindowMs = 30'000;
// Below this, "previous" means the previous item; above it, it means "restart".
constexpr qint64 kRestartThresholdMs = 5'000;
constexpr qint64 msToTicks(qint64 ms)
{
    return ms * kTicksPerMs;
}
} // namespace

PlayerController::PlayerController(emby::EmbyClient *client, PlayerBackend *backend,
                                   Settings *settings, QObject *parent)
    : QObject(parent), m_client(client), m_backend(backend), m_settings(settings),
      m_queue(new PlayQueue(this))
{
    // The queue is the only thing that knows what plays next; the controller is
    // the only thing that starts playback. These four connections are the whole
    // contract between them.
    // Subtitle look follows Settings live: a size or colour tweak has to be
    // visible on the frame in front of the user, not at the next play.
    if (m_settings) {
        connect(m_settings, &Settings::subtitleStyleChanged, this,
                &PlayerController::applySubtitleStyle);
        connect(m_settings, &Settings::replayGainModeChanged, this,
                &PlayerController::applyReplayGain);
    }
    // Applied on every load too, because mpv resets these when a file changes.
    connect(this, &PlayerController::activeChanged, this, [this] {
        if (active())
            applySubtitleStyle();
    });
    // Volume normalisation is re-asserted per load (startAttempt/playUrl) rather
    // than per session, and once here so the very first file already carries it.
    applyReplayGain();

    // The engine publishes its track list asynchronously after a load, so a
    // remembered choice can only be applied once that arrives.
    if (m_backend) {
        connect(m_backend, &PlayerBackend::tracksChanged, this, [this] {
            if (m_expectedLoadId == 0 || m_restoringTracks || m_itemId.isEmpty())
                return;
            if (!m_tracksRestored) {
                if (m_backend->audioTracks().isEmpty() && m_backend->subtitleTracks().isEmpty())
                    return; // list not populated yet
                m_tracksRestored = true;
                m_restoringTracks = true;
                restoreRememberedTracks();
                m_restoringTracks = false;
                return;
            }
            // A pending id survives until the engine actually reports it.
            // Clearing it on the first tracksChanged regardless of content lost
            // the user's choice whenever the engine re-emitted the list for an
            // unrelated reason before the selection took effect; the request is
            // retired instead by the next load, stop, or session boundary.
            bool confirmed = m_trackSelectionPending;
            if (m_pendingAudioTrack &&
                m_backend->currentAudioTrackId() == *m_pendingAudioTrack) {
                m_pendingAudioTrack.reset();
                confirmed = true;
            }
            if (m_pendingSubtitleTrack &&
                m_backend->currentSubtitleTrackId() == *m_pendingSubtitleTrack) {
                m_pendingSubtitleTrack.reset();
                confirmed = true;
            }
            if (confirmed) {
                m_trackSelectionPending = false;
                rememberCurrentTracks();
            }
        });
        connect(m_backend, &PlayerBackend::trackChanged, this, [this](const QString &description) {
            if (m_expectedLoadId != 0 && m_active)
                emit trackChanged(description);
        });
        connect(m_backend, &PlayerBackend::playbackSpeedChanged, this, [this] {
            if (m_expectedLoadId != 0 && m_active)
                emit playbackSpeedChanged();
        });
        connect(m_backend, &PlayerBackend::audioDelayChanged, this, [this] {
            if (m_expectedLoadId != 0 && m_active)
                emit audioDelayChanged();
        });
        connect(m_backend, &PlayerBackend::subtitleDelayChanged, this, [this] {
            if (m_expectedLoadId != 0 && m_active)
                emit subtitleDelayChanged();
        });
    }

    connect(m_queue, &PlayQueue::currentChanged, this, &PlayerController::onQueueCurrentChanged);
    connect(m_queue, &PlayQueue::currentItemChanged, this,
            [this] { onQueueItemChanged(false); });
    connect(m_queue, &PlayQueue::currentItemDisplaced, this,
            [this] { onQueueItemChanged(true); });
    connect(m_queue, &PlayQueue::exhausted, this, &PlayerController::onQueueExhausted);
    connect(m_queue, &PlayQueue::queueChanged, this, [this] {
        emit queueStateChanged();
        updateUpNext();
    });
    connect(m_queue, &PlayQueue::repeatModeChanged, this, &PlayerController::queueStateChanged);

    // isAudio is a function of three things — whether a session is live, what
    // the queue's cursor points at, and which media source was chosen — so it
    // is recomputed whenever one of them moves. Cheaper than it looks: the
    // computation is a string compare and a pointer test, and the signal only
    // fires when the answer actually changes.
    connect(this, &PlayerController::activeChanged, this, &PlayerController::updateIsAudio);
    connect(this, &PlayerController::sourcesChanged, this, &PlayerController::updateIsAudio);
    connect(this, &PlayerController::sourceIndexChanged, this, &PlayerController::updateIsAudio);
    connect(m_queue, &PlayQueue::currentChanged, this, &PlayerController::updateIsAudio);
    connect(m_queue, &PlayQueue::queueChanged, this, &PlayerController::updateIsAudio);

    connect(m_backend, &PlayerBackend::stateChanged, this, &PlayerController::onBackendState);
    connect(m_backend, &PlayerBackend::errorOccurred, this, &PlayerController::onBackendError);
    connect(m_backend, &PlayerBackend::endReached, this, &PlayerController::onEndReached);
    connect(m_backend, &PlayerBackend::positionChanged, this,
            [this](qint64 ms, PlayerBackend::LoadId loadId) {
        if (loadId != m_expectedLoadId)
            return;
        m_lastPositionMs = ms;
        emit positionChanged();
        updateUpNext();
    });
    connect(m_backend, &PlayerBackend::durationChanged, this,
            [this](qint64, PlayerBackend::LoadId loadId) {
        if (loadId != m_expectedLoadId)
            return;
        emit durationChanged();
        updateUpNext();
    });

    connect(m_backend, &PlayerBackend::bufferingChanged, this,
            [this](bool, PlayerBackend::LoadId loadId) {
        if (loadId == m_expectedLoadId)
            emit bufferingChanged();
    });

    m_progressTimer.setInterval(kProgressIntervalMs);
    connect(&m_progressTimer, &QTimer::timeout, this, &PlayerController::reportProgress);

    m_watchdog.setInterval(kWatchdogTickMs);
    connect(&m_watchdog, &QTimer::timeout, this, &PlayerController::onWatchdogTick);

    m_persistTimer.setInterval(kPersistIntervalMs);
    connect(&m_persistTimer, &QTimer::timeout, this, &PlayerController::persistResume);

    // Restore the level from the previous session before anything can play
    // (ARCHITECTURE.md), then push it at the engine so the first frame is already
    // at the right volume rather than snapping to it a moment later.
    if (m_settings) {
        m_volume = qBound(0, m_settings->volume(), kMaxVolume);
        m_muted = m_settings->muted();
    }
    applyVolume();

    // Something other than us can move the engine's volume — an mpv config
    // binding, a script, the OSD. Adopt it rather than fighting it.
    connect(m_backend, &PlayerBackend::volumeChanged, this, [this](int percent) {
        if (m_applyingVolume)
            return;
        const int clamped = qBound(0, percent, kMaxVolume);
        if (clamped == m_volume)
            return;
        m_volume = clamped;
        if (m_settings)
            m_settings->setVolume(m_volume);
        emit volumeChanged();
    });
    connect(m_backend, &PlayerBackend::mutedChanged, this, [this](bool muted) {
        if (m_applyingVolume || muted == m_muted)
            return;
        m_muted = muted;
        if (m_settings)
            m_settings->setMuted(m_muted);
        emit mutedChanged();
    });
}

void PlayerController::applyVolume()
{
    // The echo guard is not optional: mpv reports every property write straight
    // back, and without it the adopt-external-change path above would re-enter
    // on our own writes.
    m_applyingVolume = true;
    if (m_backend->supportsMute()) {
        m_backend->setVolume(m_volume);
        m_backend->setMuted(m_muted);
    } else {
        // No engine-side mute: emulate it, keeping m_volume as the level to
        // come back to.
        m_backend->setVolume(m_muted ? 0 : m_volume);
    }
    m_applyingVolume = false;
}

void PlayerController::setVolume(int percent)
{
    const int clamped = qBound(0, percent, kMaxVolume);
    if (clamped == m_volume)
        return;
    m_volume = clamped;
    // Raising the volume is an unmute in every player anyone has used.
    const bool wasMuted = m_muted;
    if (m_muted && m_volume > 0)
        m_muted = false;
    if (m_settings) {
        m_settings->setVolume(m_volume);
        if (wasMuted != m_muted)
            m_settings->setMuted(m_muted);
    }
    applyVolume();
    emit volumeChanged();
    if (wasMuted != m_muted)
        emit mutedChanged();
}

void PlayerController::adjustVolume(int delta)
{
    setVolume(m_volume + delta);
}

void PlayerController::setMuted(bool muted)
{
    if (muted == m_muted)
        return;
    m_muted = muted;
    if (m_settings)
        m_settings->setMuted(m_muted);
    applyVolume();
    emit mutedChanged();
}

void PlayerController::toggleMute()
{
    setMuted(!m_muted);
}

void PlayerController::setTimingForTests(int watchdogTickMs, int stallTicks, int backoffBaseMs)
{
    m_watchdog.setInterval(watchdogTickMs);
    m_stallTicksLimit = stallTicks;
    m_backoffBaseMs = backoffBaseMs;
}

QObject *PlayerController::backendObject() const
{
    return m_backend;
}

bool PlayerController::paused() const
{
    return m_backend->state() == PlayerBackend::State::Paused;
}

qint64 PlayerController::durationMs() const
{
    const qint64 fromBackend = m_backend->durationMs();
    if (fromBackend > 0)
        return fromBackend;
    return m_ticket.runtimeTicks(m_sourceIndex) / kTicksPerMs;
}

QString PlayerController::streamMethod() const
{
    if (!m_reporting || !hasTicket())
        return {};
    return playMethodName(currentCandidate()->method);
}

QVariantList PlayerController::sources() const
{
    QVariantList list;
    list.reserve(m_ticket.sourceCount());
    for (qsizetype i = 0; i < m_ticket.sourceCount(); ++i) {
        QVariantMap map = m_ticket.sources[i].source.toVariantMap();
        map.insert(QStringLiteral("index"), static_cast<int>(i));
        // A source the server offers no delivery method for cannot be selected.
        map.insert(QStringLiteral("playable"), m_ticket.sources[i].isValid());
        list.append(map);
    }
    return list;
}

QVariantMap PlayerController::currentSource() const
{
    const MediaSourceCandidates *entry = m_ticket.source(m_sourceIndex);
    if (!entry)
        return {};
    QVariantMap map = entry->source.toVariantMap();
    map.insert(QStringLiteral("index"), static_cast<int>(m_sourceIndex));
    map.insert(QStringLiteral("playable"), entry->isValid());
    return map;
}

QVariantMap PlayerController::videoStream() const
{
    const MediaSourceCandidates *entry = m_ticket.source(m_sourceIndex);
    if (!entry)
        return {};
    const MediaStream *video = entry->source.videoStream();
    return video ? video->toVariantMap() : QVariantMap();
}

QVariantList PlayerController::audioStreams() const
{
    QVariantList list;
    const MediaSourceCandidates *entry = m_ticket.source(m_sourceIndex);
    if (!entry)
        return list;
    for (const MediaStream &stream : entry->source.audioStreams())
        list.append(stream.toVariantMap());
    return list;
}

QVariantList PlayerController::subtitleStreams() const
{
    QVariantList list;
    const MediaSourceCandidates *entry = m_ticket.source(m_sourceIndex);
    if (!entry)
        return list;
    for (const MediaStream &stream : entry->source.subtitleStreams())
        list.append(stream.toVariantMap());
    return list;
}

// Central place that moves the "which version" cursor, so every path that
// changes it also refreshes the QML-facing surface.
void PlayerController::selectSource(qsizetype index)
{
    if (m_sourceIndex == index)
        return;
    m_sourceIndex = index;
    emit sourceIndexChanged();
}

void PlayerController::playItem(const QString &itemId, const QString &title,
                                qint64 startPositionMs, int preferredSourceIndex,
                                const QString &itemType)
{
    startItem(itemId, title, startPositionMs, preferredSourceIndex, false, itemType);
}

void PlayerController::startItem(const QString &itemId, const QString &title,
                                 qint64 startPositionMs, int preferredSourceIndex, bool fromQueue,
                                 const QString &itemType)
{
    // Resolve starts from a quiescent engine. This makes the intermediate
    // snapshot coherent (pending metadata plus an empty timeline) and means a
    // failed handoff cannot leave the outgoing file playing behind an inactive
    // controller.
    closeCurrentSession();
    m_expectedLoadId = 0;
    if (m_backend->state() != PlayerBackend::State::Idle)
        m_backend->stop();
    clearAbLoop();

    if (!fromQueue) {
        // A bare play verb replaces the queue with just this item, so that a
        // later "play next" lands *after* what is on screen instead of ahead of
        // it, and so a stale queue cannot auto-advance into unrelated content.
        MediaItem seed;
        seed.id = itemId;
        seed.name = title;
        // Carried whenever the caller has it, because the seed is what
        // computeIsAudio() reads first and the ticket that would answer for it
        // otherwise is still the OUTGOING item's until the reply below lands.
        seed.type = itemType;
        const QScopedValueRollback<bool> guard(m_queueDriving, true);
        m_queue->setItems({seed}, 0);
        emit queueStateChanged();
    }

    ++m_generation;
    // A source index only means something within one item's ticket, so an
    // explicit argument wins and otherwise the preference resets to automatic.
    m_preferredSourceIndex = preferredSourceIndex;
    const int generation = m_generation;

    m_itemId = itemId;
    m_ticket = {};
    m_ticketItemId.clear();
    m_sourceIndex = -1;
    m_rung = 0;
    emit sourcesChanged();
    emit sourceIndexChanged();
    emit streamMethodChanged();
    m_title = title;
    emit titleChanged();
    if (!m_chapters.isEmpty()) {
        m_chapters.clear();
        emit chaptersChanged();
    }
    // playUrl() passes no item id (dev/test path, no server session).
    // A new item gets a fresh chance to restore its remembered tracks.
    m_tracksRestored = false;
    m_trackSelectionPending = false;
    m_pendingAudioTrack.reset();
    m_pendingSubtitleTrack.reset();
    // Cleared before the fetch that refills them, so a failed lookup cannot
    // leave the previous episode's series attached to this one.
    m_currentSeriesId.clear();
    m_currentItemType = itemType;
    updateIsAudio();
    if (!itemId.isEmpty())
        fetchChapters(itemId, generation);
    setError({});
    setBusy(true);
    setActive(true);
    updateIsAudio();
    m_reporting = true;
    m_stallStep = 0;
    m_healthyTicks = 0;
    m_recoverRetries = 0;
    m_recovering = false;
    ++m_recoveryToken;
    // A fresh item gets a fresh Up Next card, whatever the user did about the
    // previous one.
    m_upNextCancelled = false;
    setUpNext(false, 0);
    // Until playback actually starts, "last position" is the requested start —
    // a pre-start ladder demotion must not lose the resume point.
    m_lastPositionMs = startPositionMs;

    m_client->playbackInfo(itemId, msToTicks(startPositionMs))
        .then(this, [this, generation, startPositionMs, itemId](const Result<PlaybackTicket> &result) {
            if (generation != m_generation)
                return;
            if (!result.ok()) {
                setBusy(false);
                setError(result.error);
                finishSession(TerminationReason::Failure);
                return;
            }
            m_ticket = result.value;
            m_ticketItemId = itemId;
            m_rung = 0;
            qsizetype index = m_ticket.defaultSourceIndex();
            if (m_preferredSourceIndex >= 0 && m_ticket.source(m_preferredSourceIndex) &&
                m_ticket.source(m_preferredSourceIndex)->isValid())
                index = m_preferredSourceIndex;
            m_sourceIndex = -1; // force the change signal for the new ticket
            selectSource(index);
            emit sourcesChanged();
            emit streamMethodChanged();
            startAttempt(startPositionMs);
        });
}

void PlayerController::setPlaybackSpeed(qreal speed)
{
    if (!m_backend)
        return;
    m_backend->setPlaybackSpeed(speed);
}

void PlayerController::setAudioDelayMs(int ms)
{
    if (!m_backend)
        return;
    m_backend->setAudioDelayMs(ms);
}

void PlayerController::setSubtitleDelayMs(int ms)
{
    if (!m_backend)
        return;
    m_backend->setSubtitleDelayMs(ms);
}

void PlayerController::applySubtitleStyle()
{
    if (!m_backend || !m_settings)
        return;
    m_backend->setSubtitleStyle(m_settings->subtitleFont(), m_settings->subtitleScale(),
                                m_settings->subtitleColor(), m_settings->subtitleBackground(),
                                m_settings->subtitlePosition());
}

void PlayerController::applyReplayGain()
{
    if (!m_backend || !m_settings)
        return;
    m_backend->setReplayGain(m_settings->replayGainMode());
}

void PlayerController::setPreferredSource(int index)
{
    m_preferredSourceIndex = index;
    if (!m_active || !m_reporting)
        return; // remembered for the next playItem()
    if (index < 0 || index == m_sourceIndex)
        return;
    const MediaSourceCandidates *entry = m_ticket.source(index);
    if (!entry || !entry->isValid()) {
        // A version the server listed but offered no playable ladder for. The
        // picker snaps back on its own, which without this reads as a control
        // that does nothing.
        qCWarning(logPlayback) << "ignoring unplayable source index" << index;
        emit sourceSwitchFailed(tr("That version cannot be played from this server."));
        return;
    }

    qCInfo(logPlayback) << "switching to source" << index << entry->source.displayName();
    // Remembered per item, by source id rather than index: the server can
    // reorder its sources between requests (ARCHITECTURE.md).
    if (m_settings && !m_itemId.isEmpty())
        m_settings->rememberVersion(m_itemId, entry->source.id);
    selectSource(index);
    // A different version is a fresh ladder and a fresh recovery budget.
    m_rung = 0;
    m_stallStep = 0;
    m_healthyTicks = 0;
    m_recoverRetries = 0;
    m_recovering = false;
    ++m_recoveryToken;
    emit streamMethodChanged();
    startAttempt(m_lastPositionMs);
}

void PlayerController::playUrl(const QUrl &url, const QString &title)
{
    // A raw URL replaces the playing item like any other start does, and the
    // server session it replaces still has to be closed.
    closeCurrentSession();
    m_expectedLoadId = 0;
    if (m_backend->state() != PlayerBackend::State::Idle)
        m_backend->stop();
    clearAbLoop();
    {
        // Nothing about a raw URL belongs in a server-item queue, and a stale
        // queue must not auto-advance out of it.
        const QScopedValueRollback<bool> guard(m_queueDriving, true);
        m_queue->clear();
    }
    emit queueStateChanged();
    m_upNextCancelled = false;
    setUpNext(false, 0);
    ++m_generation;
    m_recovering = false;
    ++m_recoveryToken;
    m_title = title;
    emit titleChanged();
    setError({});
    m_reporting = false;
    m_ticket = {};
    m_ticketItemId.clear();
    m_itemId.clear();
    m_currentSeriesId.clear();
    m_currentItemType.clear();
    m_tracksRestored = false;
    m_trackSelectionPending = false;
    m_pendingAudioTrack.reset();
    m_pendingSubtitleTrack.reset();
    if (!m_chapters.isEmpty()) {
        m_chapters.clear();
        emit chaptersChanged();
    }
    m_rung = 0;
    m_sourceIndex = -1;
    m_preferredSourceIndex = -1;
    emit sourcesChanged();
    emit sourceIndexChanged();
    setActive(true);
    updateIsAudio();
    setBusy(true);
    m_started = false;
    m_lastPositionMs = 0;
    m_expectedLoadId = ++m_nextLoadId;
    m_backend->load(url, 0, m_expectedLoadId);
    applyVolume();
    applyReplayGain();
}

void PlayerController::startAttempt(qint64 startMs)
{
    if (!hasTicket()) {
        setBusy(false);
        setError(QStringLiteral("no playable stream"));
        finishSession(TerminationReason::Failure);
        return;
    }

    const StreamCandidate &candidate = *currentCandidate();
    qCInfo(logPlayback) << "starting rung" << playMethodName(candidate.method) << "of source"
                        << m_sourceIndex << "for item" << m_itemId << "at" << startMs << "ms";
    m_started = false;
    m_tracksRestored = false;
    m_trackSelectionPending = false;
    m_pendingAudioTrack.reset();
    m_pendingSubtitleTrack.reset();
    m_watchdogLastPos = -1;
    m_stallTicks = 0;
    // m_stallStep intentionally survives watchdog-triggered reloads: the
    // escalation ladder must keep climbing, not restart at "nudge".
    setBusy(true);
    emit streamMethodChanged();
    m_expectedLoadId = ++m_nextLoadId;
    m_backend->load(candidate.url, startMs, m_expectedLoadId);
    // Some engines reset their audio state per media; re-assert ours.
    applyVolume();
    applyReplayGain();
}

void PlayerController::onBackendState(PlayerBackend::State state, PlayerBackend::LoadId loadId)
{
    if (loadId != m_expectedLoadId)
        return;
    emit pausedChanged();

    if (state == PlayerBackend::State::Playing && !m_started) {
        m_recovering = false;
        m_started = true;
        setBusy(false);
        updateUpNext();
        m_watchdog.start();
        if (m_reporting) {
            report(0);
            m_progressTimer.start();
            m_persistTimer.start();
            persistResume();
        }
    } else if (state == PlayerBackend::State::Paused && m_started && m_reporting) {
        reportProgress();
    }
}

bool PlayerController::nearEnd() const
{
    const qint64 duration = durationMs();
    return duration > 0 && m_lastPositionMs >= duration - kTailEpsilonMs;
}

void PlayerController::onWatchdogTick()
{
    if (!m_active || !m_started)
        return;
    if (paused() || m_backend->buffering()) {
        // Legitimate non-progress; do not count as a stall.
        m_stallTicks = 0;
        return;
    }

    const qint64 pos = m_backend->positionMs();
    if (pos != m_watchdogLastPos) {
        m_watchdogLastPos = pos;
        m_stallTicks = 0;
        // A single moving sample can be our own nudge-seek; require sustained
        // progress before declaring the incident over (never nudge-loop).
        if (++m_healthyTicks >= 2) {
            m_stallStep = 0;
            m_recoverRetries = 0;
        }
        return;
    }

    m_healthyTicks = 0;
    if (++m_stallTicks >= m_stallTicksLimit) {
        m_stallTicks = 0;
        escalateStall();
    }
}

void PlayerController::escalateStall()
{
    // PLAN §3.5: escalate, never spin — (1) nudge seek, (2) reload rung, (3) demote.
    switch (m_stallStep++) {
    case 0:
        qCWarning(logPlayback) << "watchdog: position stalled, nudge-seeking +1s";
        m_backend->seekTo(m_lastPositionMs + 1000);
        break;
    case 1:
        qCWarning(logPlayback) << "watchdog: still stalled, reloading current rung";
        startAttempt(m_lastPositionMs);
        break;
    default:
        // Demotion stays inside the selected source: the next rung is a weaker
        // delivery of the *same* version, never a different version.
        if (m_reporting && canDemote()) {
            ++m_rung;
            qCWarning(logPlayback)
                << "watchdog: demoting to" << playMethodName(currentCandidate()->method);
            startAttempt(m_lastPositionMs);
        } else {
            m_watchdog.stop();
            setError(QStringLiteral("Playback stalled and could not recover"));
            closeCurrentSession();
            m_backend->stop();
            finishSession(TerminationReason::Failure);
        }
        break;
    }
}

void PlayerController::recoverMidStream()
{
    // Tickets go stale (auth/HLS URLs expire): re-fetch and resume, with capped
    // exponential backoff. Retries reset once playback runs healthily again.
    ++m_recoverRetries;
    m_recovering = true;
    const int recoveryToken = ++m_recoveryToken;
    m_watchdog.stop();
    const int delay = qMin(m_backoffBaseMs * (1 << (m_recoverRetries - 1)), 8 * m_backoffBaseMs);
    const int generation = m_generation;
    qCWarning(logPlayback) << "mid-stream failure; refreshing ticket in" << delay << "ms (attempt"
                           << m_recoverRetries << ")";

    QTimer::singleShot(delay, this, [this, generation, recoveryToken] {
        if (generation != m_generation || recoveryToken != m_recoveryToken || !m_active)
            return;
        m_client->playbackInfo(m_itemId, m_lastPositionMs * kTicksPerMs)
            .then(this, [this, generation, recoveryToken](const Result<PlaybackTicket> &result) {
                if (generation != m_generation || recoveryToken != m_recoveryToken || !m_active)
                    return;
                if (!result.ok()) {
                    m_recovering = false;
                    onBackendError(result.error, m_expectedLoadId);
                    return;
                }
                const QString previousSourceId = hasTicket() ? currentCandidate()->mediaSourceId
                                                            : QString();
                m_ticket = result.value;
                m_ticketItemId = m_itemId; // the same item, a fresh ticket for it
                // Re-bind to the same version by id: source order is not
                // guaranteed stable across PlaybackInfo calls.
                qsizetype index = m_ticket.indexOfSourceId(previousSourceId);
                if (index < 0 || !m_ticket.sources[index].isValid())
                    index = m_ticket.defaultSourceIndex();
                m_sourceIndex = -1;
                selectSource(index);
                emit sourcesChanged();
                m_rung = qMin(m_rung, m_ticket.rungCount(m_sourceIndex) - 1);
                if (m_rung < 0)
                    m_rung = 0;
                startAttempt(m_lastPositionMs);
            });
    });
}


void PlayerController::fetchChapters(const QString &itemId, int generation)
{
    if (!m_client || itemId.isEmpty())
        return;
    // One extra request per playback start. Chapters are not in the PlaybackInfo
    // response, and the alternative — requiring a visit to the details page
    // first — would leave every item played straight from a rail without them.
    m_client->itemDetails(itemId).then(this, [this, generation](const Result<ItemDetails> &result) {
        if (generation != m_generation || !m_active)
            return; // a newer item superseded this reply
        if (!result.ok()) {
            qCDebug(logPlayback) << "chapters unavailable:" << result.error;
            return;
        }
        // The same reply carries the series context auto-play needs, so it
        // costs no extra request.
        m_currentSeriesId = result.value.item.seriesId;
        m_currentItemType = result.value.item.type;

        QVariantList list;
        list.reserve(result.value.chapters.size());
        for (const Chapter &chapter : result.value.chapters)
            list.append(chapter.toVariantMap());
        if (list == m_chapters)
            return;
        m_chapters = list;
        emit chaptersChanged();
    });
}

int PlayerController::currentChapter() const
{
    if (m_chapters.isEmpty())
        return -1;
    const qint64 position = positionMs();
    int found = -1;
    for (int i = 0; i < m_chapters.size(); ++i) {
        if (m_chapters.at(i).toMap().value(QStringLiteral("startMs")).toLongLong() <= position)
            found = i;
        else
            break;
    }
    return found;
}

void PlayerController::seekToChapter(int index)
{
    if (index < 0 || index >= m_chapters.size())
        return;
    seekTo(m_chapters.at(index).toMap().value(QStringLiteral("startMs")).toLongLong());
}

void PlayerController::nextChapter()
{
    const int next = currentChapter() + 1;
    if (next > 0 && next < m_chapters.size())
        seekToChapter(next);
}

void PlayerController::previousChapter()
{
    const int current = currentChapter();
    if (current < 0)
        return;
    constexpr qint64 kRestartWindowMs = 3000;
    const qint64 start =
        m_chapters.at(current).toMap().value(QStringLiteral("startMs")).toLongLong();
    seekToChapter(positionMs() - start > kRestartWindowMs ? current : qMax(0, current - 1));
}

void PlayerController::onBackendError(const QString &message, PlayerBackend::LoadId loadId)
{
    // Same rule as onEndReached(): an engine that reports a failure *after* the
    // session was torn down must not restart the ladder into a stopped player.
    if (!m_active || loadId != m_expectedLoadId)
        return;
    if (m_recovering && m_started)
        return; // one delayed ticket refresh owns this recovery incident
    m_progressTimer.stop();

    // Broken tail (the Emby-web-player bug class): an error at effectively the
    // end of the file is a clean end, never a frozen failure (PLAN §3.5).
    if (m_started && nearEnd()) {
        qCInfo(logPlayback) << "error within tail epsilon of EOF; treating as clean end";
        onEndReached(loadId);
        return;
    }

    // Startup failure: demote one rung, preserving position (PLAN §3.5 ladder).
    if (m_reporting && !m_started && canDemote()) {
        ++m_rung;
        qCWarning(logPlayback) << "rung failed before start, demoting to"
                               << playMethodName(currentCandidate()->method);
        startAttempt(m_lastPositionMs);
        return;
    }

    // Mid-stream failure: refresh the ticket and resume (network recovery).
    if (m_reporting && m_started && m_recoverRetries < kMaxRecoverRetries) {
        recoverMidStream();
        return;
    }

    setBusy(false);
    setError(QStringLiteral("Playback failed: %1").arg(message));
    finishSession(TerminationReason::Failure);
}

// A clean end is now a queue event, not the end of the session: the item is
// reported stopped at its full runtime exactly as before, and only an exhausted
// queue (or a cancelled Up Next card) still emits stopped(). Every caller of
// this — the engine's endReached, and the broken-tail branch of onBackendError
// that treats an error within the tail epsilon as a clean end — gets the
// auto-advance for free.
void PlayerController::onEndReached(PlayerBackend::LoadId loadId)
{
    // An engine that reports the end of a file *after* the session was torn
    // down (some do, on stop()) must not resurrect it into the next item.
    if (!m_active || loadId != m_expectedLoadId)
        return;
    // Report the full runtime so the server marks the item played.
    if (m_reporting)
        m_lastPositionMs = qMax(m_lastPositionMs, durationMs());
    closeCurrentSession();
    setUpNext(false, 0);
    if (advanceToNext())
        return;
    // Nothing queued after this one. If it was an episode, keep the series
    // going — but only then: an explicit queue or a shuffle has already been
    // consumed by advanceToNext(), and continuing past it would override a
    // choice the user made.
    if (tryAutoPlayNextEpisode())
        return;
    finishSession(TerminationReason::CleanEnd);
}

// ── Queue ─────────────────────────────────────────────────────────────────────

QVariantMap PlayerController::nextItem() const
{
    const int next = m_queue->nextIndex();
    if (next < 0 || next == m_queue->currentIndex())
        return {}; // RepeatOne offers no *next* item, it re-plays this one
    return m_queue->itemAt(next);
}

bool PlayerController::startQueueCurrent(bool force)
{
    const QVariantMap item = m_queue->currentItem();
    const QString itemId = item.value(QStringLiteral("itemId")).toString();
    if (itemId.isEmpty())
        return false;
    // A row shifting under the cursor (a removal above it) is not a request to
    // restart what is already playing.
    if (!force && m_active && itemId == m_itemId)
        return false;

    QString title = item.value(QStringLiteral("label")).toString();
    if (title.isEmpty())
        title = item.value(QStringLiteral("name")).toString();
    const qint64 startMs = item.value(QStringLiteral("resumable")).toBool()
                               ? item.value(QStringLiteral("positionMs")).toLongLong()
                               : 0;
    startItem(itemId, title, qMax<qint64>(0, startMs), -1, true,
              item.value(QStringLiteral("type")).toString());
    return true;
}

bool PlayerController::tryAutoPlayNextEpisode()
{
    if (!m_settings || !m_settings->autoPlayNextEpisode())
        return false;
    if (m_upNextCancelled)
        return false; // the user dismissed the card for this item
    if (m_currentItemType.compare(QLatin1String("Episode"), Qt::CaseInsensitive) != 0)
        return false;
    if (m_currentSeriesId.isEmpty() || m_itemId.isEmpty())
        return false;
    // An explicit queue governs. A queue of more than one item was built by
    // "play all", a shuffle, or the user adding to it, and its end is a
    // deliberate end — chaining past it would ignore what they asked for.
    if (m_queue->rowCount() > 1)
        return false;

    const int generation = m_generation;
    const QString seriesId = m_currentSeriesId;
    qCInfo(logPlayback) << "auto-play: looking for the episode after" << m_itemId;
    m_client->nextEpisode(seriesId, m_itemId)
        .then(this, [this, generation](const Result<QList<MediaItem>> &result) {
            // The user may have started something else while this was in
            // flight; the generation check is what stops it hijacking that.
            if (generation != m_generation)
                return;
            if (!result.ok() || result.value.isEmpty()) {
                qCInfo(logPlayback) << "auto-play: end of series";
                finishSession(result.ok() ? TerminationReason::CleanEnd
                                          : TerminationReason::Failure);
                return;
            }
            const MediaItem next = result.value.first();
            qCInfo(logPlayback) << "auto-play: continuing with" << next.name;
            // Seeded as a queue item rather than a bare playItem() so the next
            // episode carries its own artwork and metadata into the mini
            // player and the OSD, and so THIS runs again when it ends.
            playQueueItems({next}, 0, false);
        });
    // Responsibility taken: finishSession() is now this callback's to call.
    return true;
}

bool PlayerController::advanceToNext()
{
    if (m_upNextCancelled)
        return false; // the user said no for this item
    bool moved = false;
    {
        const QScopedValueRollback<bool> guard(m_queueDriving, true);
        moved = m_queue->advance();
    }
    emit queueStateChanged();
    return moved && startQueueCurrent(true);
}

void PlayerController::playQueue(const QVariantList &items, int startIndex)
{
    QList<MediaItem> media;
    media.reserve(items.size());
    for (const QVariant &value : items) {
        MediaItem item = PlayQueue::itemFromVariant(value);
        if (!item.id.isEmpty())
            media.append(std::move(item));
    }
    playQueueItems(std::move(media), startIndex);
}

void PlayerController::playQueueItems(QList<MediaItem> items, int startIndex, bool shuffled)
{
    if (items.isEmpty()) {
        setError(QStringLiteral("Nothing to play"));
        return;
    }
    {
        const QScopedValueRollback<bool> guard(m_queueDriving, true);
        m_queue->setItems(std::move(items), startIndex);
        m_queue->setShuffled(shuffled);
    }
    emit queueStateChanged();
    startQueueCurrent(true);
}

void PlayerController::playNext()
{
    if (!m_queue->hasNext())
        return;
    // The stop report is startItem()'s closeCurrentSession(), which runs while
    // this item, its ticket and its position are still current: a skip is a stop
    // at the real position, not at the runtime, so the server must not mark a
    // half-watched episode played because it was skipped.
    setUpNext(false, 0);
    {
        const QScopedValueRollback<bool> guard(m_queueDriving, true);
        m_queue->advance();
    }
    emit queueStateChanged();
    startQueueCurrent(true);
}

void PlayerController::playPrevious()
{
    if (m_active && m_lastPositionMs > kRestartThresholdMs) {
        seekTo(0);
        return;
    }
    if (!m_queue->hasPrevious()) {
        if (m_active)
            seekTo(0);
        return;
    }
    setUpNext(false, 0);
    {
        const QScopedValueRollback<bool> guard(m_queueDriving, true);
        m_queue->goBack();
    }
    emit queueStateChanged();
    startQueueCurrent(true);
}

void PlayerController::cancelUpNext()
{
    m_upNextCancelled = true;
    setUpNext(false, 0);
}

// The property side of every queue signal: hasNext / hasPrevious / nextItem and
// the Up Next card. Starting playback is NOT done here — an insert or a removal
// above the cursor reaches this too, and neither is a request to play.
void PlayerController::onQueueCurrentChanged()
{
    emit queueStateChanged();
    updateUpNext();
    if (m_queueDriving)
        return;
    if (m_queue->currentIndex() < 0 && m_active)
        stop();
}

// The queue moved without us: a jumpTo() from the queue panel, an item dropped
// into an empty queue, or the row that was playing being removed.
void PlayerController::onQueueItemChanged(bool displaced)
{
    if (m_queueDriving)
        return;
    // A queue edit is not a play verb. stop() leaves the queue intact, so
    // removing the row that was playing promotes another one — and starting it
    // would resurrect a session the user had just ended. Playback follows the
    // promotion only when there is playback to follow it with.
    if (displaced && !m_active)
        return;
    startQueueCurrent(false);
}

void PlayerController::onQueueExhausted()
{
    if (m_queueDriving)
        return; // advanceToNext() handles its own exhaustion
    if (m_active)
        stop();
}

void PlayerController::updateUpNext()
{
    const qint64 duration = durationMs();
    const qint64 remaining = duration - m_lastPositionMs;
    const bool eligible = m_active && m_started && m_reporting && !m_upNextCancelled &&
                          m_queue->hasNext() && duration > 0 && remaining <= kUpNextWindowMs;
    if (!eligible) {
        setUpNext(false, 0);
        return;
    }
    const int seconds = static_cast<int>(qBound<qint64>(0, (remaining + 999) / 1000,
                                                        kUpNextWindowMs / 1000));
    setUpNext(true, seconds);
}

void PlayerController::setUpNext(bool visible, int seconds)
{
    if (visible == m_upNextVisible && seconds == m_upNextSeconds)
        return;
    m_upNextVisible = visible;
    m_upNextSeconds = seconds;
    emit upNextChanged();
}

void PlayerController::togglePause()
{
    setPaused(m_backend->state() != PlayerBackend::State::Paused);
}

void PlayerController::setPaused(bool paused)
{
    m_backend->setPaused(paused);
}

void PlayerController::stop()
{
    if (!m_active && !m_busy && m_backend->state() == PlayerBackend::State::Idle)
        return;
    finishSession(TerminationReason::UserStop);
}

void PlayerController::seekTo(qint64 positionMs)
{
    // Clamped at BOTH ends. The floor has always been here; the ceiling became
    // load-bearing the moment m_lastPositionMs started adopting the target
    // below, because that field is no longer only an observation: nearEnd()
    // reads it to decide whether a later engine error is a clean end (stop
    // report at full runtime, auto-advance) or a mid-stream stall to recover
    // from. Without a ceiling, holding skip-forward past the end walks
    // m_lastPositionMs beyond the runtime and reports a position the engine
    // never reached.
    const qint64 duration = durationMs();
    qint64 target = qMax<qint64>(0, positionMs);
    if (duration > 0)
        target = qMin(target, duration);
    m_backend->seekTo(target);
    // The engine publishes the new position asynchronously, so until it lands
    // m_lastPositionMs is where the playhead *was*. Adopting the target now is
    // what makes the report below carry the seek rather than the position it
    // seeked away from — and it is also what stops a fast repeat of
    // seekRelative() (a held key or a gamepad shoulder, 38 ms apart) from
    // recomputing every step off the same stale base: two taps of skip-forward
    // move 20 s, not 10.
    m_lastPositionMs = target;
    // Announced from here rather than from each caller so that every way of
    // moving the playhead — the scrubber, a skip binding, a chapter jump, a
    // remote SetPosition — reaches a listening MPRIS client as one Seeked.
    emit seeked(target);
    if (m_reporting && m_started)
        reportProgress();
}

void PlayerController::seekRelative(qint64 deltaMs)
{
    seekTo(m_lastPositionMs + deltaMs);
}

void PlayerController::cycleAudioTrack()
{
    m_trackSelectionPending = true;
    m_backend->cycleAudioTrack();
}

void PlayerController::cycleSubtitleTrack()
{
    m_trackSelectionPending = true;
    m_backend->cycleSubtitleTrack();
}

void PlayerController::setAudioTrack(int id)
{
    if (!m_backend)
        return;
    m_pendingAudioTrack = id;
    m_backend->setAudioTrack(id);
}

void PlayerController::setSubtitleTrack(int id)
{
    if (!m_backend)
        return;
    m_pendingSubtitleTrack = id;
    m_backend->setSubtitleTrack(id);
}

void PlayerController::frameStep(int direction)
{
    if (!m_backend || !m_backend->supportsFrameStep() || !m_active)
        return;
    m_backend->frameStep(direction);
}

void PlayerController::markLoopPoint()
{
    if (!m_backend || !m_active)
        return;
    const qint64 now = positionMs();
    if (m_loopStartMs < 0) {
        m_loopStartMs = now;
    } else if (m_loopEndMs < 0) {
        // B before A is a slip, not an instruction: swap rather than refuse, so
        // marking the end first still produces the range the user drew.
        if (now < m_loopStartMs) {
            m_loopEndMs = m_loopStartMs;
            m_loopStartMs = now;
        } else {
            m_loopEndMs = now;
        }
    } else {
        clearAbLoop();
        return;
    }
    m_backend->setAbLoop(m_loopStartMs, m_loopEndMs);
    emit abLoopChanged();
}

void PlayerController::clearAbLoop()
{
    if (m_loopStartMs < 0 && m_loopEndMs < 0)
        return;
    m_loopStartMs = -1;
    m_loopEndMs = -1;
    if (m_backend)
        m_backend->setAbLoop(-1, -1);
    emit abLoopChanged();
}

QString PlayerController::takeScreenshot()
{
    if (!m_backend || !m_active)
        return {};

    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
        + QStringLiteral("/StrmQt");
    if (!QDir().mkpath(dir)) {
        qCWarning(logPlayback) << "screenshot: cannot create" << dir;
        return {};
    }

    // Named from the item and its timecode, so a folder of these is readable
    // rather than a pile of timestamps. Colons are illegal on some filesystems
    // and confusing on the rest.
    const qint64 seconds = positionMs() / 1000;
    const QString stamp = QStringLiteral("%1-%2-%3")
                              .arg(seconds / 3600, 2, 10, QLatin1Char('0'))
                              .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
                              .arg(seconds % 60, 2, 10, QLatin1Char('0'));
    QString name = m_title.isEmpty() ? QStringLiteral("StrmQt") : m_title;
    static const QRegularExpression illegal(QStringLiteral(R"([/\:*?"<>|])"));
    name.replace(illegal, QStringLiteral("_"));
    const QString path = QStringLiteral("%1/%2 - %3.png").arg(dir, name, stamp);

    m_backend->screenshotToFile(path);
    qCInfo(logPlayback) << "screenshot:" << path;
    emit screenshotSaved(path);
    return path;
}

QVariantList PlayerController::backendAudioTracks() const
{
    return m_backend ? m_backend->audioTracks() : QVariantList();
}

QVariantList PlayerController::backendSubtitleTracks() const
{
    return m_backend ? m_backend->subtitleTracks() : QVariantList();
}

void PlayerController::rememberCurrentTracks()
{
    if (!m_settings || !m_backend || m_itemId.isEmpty())
        return;
    // Keyed by media source too: Emby numbers tracks per source, so the same
    // item played from a different version has different ids.
    const MediaSourceCandidates *entry = m_ticket.source(m_sourceIndex);
    const QString sourceId = entry ? entry->source.id : QString();
    m_settings->rememberTracks(m_itemId, sourceId, m_backend->currentAudioTrackId(),
                               m_backend->currentSubtitleTrackId());
}

void PlayerController::restoreRememberedTracks()
{
    if (!m_settings || !m_backend || m_itemId.isEmpty())
        return;
    const MediaSourceCandidates *entry = m_ticket.source(m_sourceIndex);
    const QString sourceId = entry ? entry->source.id : QString();
    // Nothing stored: keep whatever the server and the engine chose. Forcing
    // the {-1,-1} default onto the engine would turn off a subtitle the server
    // marked as default.
    if (!m_settings->hasRememberedTracks(m_itemId, sourceId))
        return;
    const QPair<int, int> remembered = m_settings->recalledTracks(m_itemId, sourceId);

    const QVariantList audio = m_backend->audioTracks();
    const QVariantList subtitles = m_backend->subtitleTracks();
    const auto known = [](const QVariantList &tracks, int id) {
        for (const QVariant &track : tracks) {
            if (track.toMap().value(QStringLiteral("id")).toInt() == id)
                return true;
        }
        return false;
    };

    if (remembered.first >= 0 && known(audio, remembered.first)
        && remembered.first != m_backend->currentAudioTrackId())
        m_backend->setAudioTrack(remembered.first);
    // A remembered -1 for subtitles is "the user turned them off" and is
    // applied; that is why hasRememberedTracks() had to exist.
    if ((remembered.second < 0 || known(subtitles, remembered.second))
        && remembered.second != m_backend->currentSubtitleTrackId())
        m_backend->setSubtitleTrack(remembered.second);
}

PlaybackProgress PlayerController::progressNow() const
{
    PlaybackProgress progress;
    progress.itemId = m_itemId;
    progress.playSessionId = m_ticket.playSessionId;
    if (const StreamCandidate *candidate = currentCandidate()) {
        progress.mediaSourceId = candidate->mediaSourceId;
        progress.method = candidate->method;
    }
    progress.positionTicks = msToTicks(m_lastPositionMs);
    progress.paused = paused();
    return progress;
}

void PlayerController::reportProgress()
{
    report(1);
}

void PlayerController::report(int kind)
{
    if (!m_reporting)
        return;
    const PlaybackProgress progress = progressNow();
    switch (kind) {
    case 0:
        m_client->reportPlaybackStart(progress);
        break;
    case 1:
        m_client->reportPlaybackProgress(progress);
        break;
    default:
        m_client->reportPlaybackStopped(progress);
        break;
    }
}

// Two things go wrong when an item is replaced without this. The server never
// hears that the old one stopped, so its session stays open and the item keeps
// its stale resume point; and the 10 s progress timer keeps running across the
// swap, so the next tick reports the NEW item id against the OLD playSessionId
// and mediaSourceId — a progress record for a pairing that never played.
// It runs before startItem() touches any of that state, so the report it sends
// still carries the outgoing item, its ticket and its real position.
void PlayerController::closeCurrentSession()
{
    m_progressTimer.stop();
    if (!m_active || !m_reporting || !m_started)
        return;
    report(2);
    // Idempotent on purpose: a clean end reports the full runtime and *then*
    // advances, so the startItem() behind the advance must not send a second
    // stop for the item it just closed.
    m_started = false;
}

void PlayerController::finishSession(TerminationReason reason)
{
    if (reason == TerminationReason::Failure && m_started)
        persistResume();
    closeCurrentSession();
    ++m_generation;
    m_expectedLoadId = 0;
    m_backend->stop();
    setUpNext(false, 0);
    m_progressTimer.stop();
    m_watchdog.stop();
    m_persistTimer.stop();
    if (reason != TerminationReason::Failure)
        clearCrashResume();
    m_started = false;
    m_reporting = false;
    m_ticket = {};
    m_ticketItemId.clear();
    m_itemId.clear();
    if (!m_title.isEmpty()) {
        m_title.clear();
        emit titleChanged();
    }
    m_currentSeriesId.clear();
    m_currentItemType.clear();
    m_tracksRestored = false;
    m_trackSelectionPending = false;
    m_pendingAudioTrack.reset();
    m_pendingSubtitleTrack.reset();
    m_sourceIndex = -1;
    m_rung = 0;
    m_preferredSourceIndex = -1;
    m_recovering = false;
    ++m_recoveryToken;
    m_lastPositionMs = 0;
    if (!m_chapters.isEmpty()) {
        m_chapters.clear();
        emit chaptersChanged();
    }
    emit sourcesChanged();
    emit sourceIndexChanged();
    emit streamMethodChanged();
    setBusy(false);
    clearAbLoop();
    setActive(false);
    emit stopped();
}

void PlayerController::persistResume()
{
    if (!m_settings || !m_reporting || !m_active)
        return;
    m_settings->setLastPlayback(m_itemId, m_title, m_lastPositionMs);
}

QVariantMap PlayerController::crashResumeInfo() const
{
    if (!m_settings)
        return {};
    return m_settings->lastPlayback();
}

void PlayerController::clearCrashResume()
{
    if (m_settings)
        m_settings->clearLastPlayback();
}

void PlayerController::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    emit activeChanged();
}

void PlayerController::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

// Music, or a picture? The type the server put on the queue entry answers it
// outright whenever there is one, and that is the common case: every queue verb
// carries the type, and so does a bare play — ItemActions hands the type it
// already has to playItem(), which seeds it onto the one-item queue.
//
// What is left with no type is a play-by-id from a caller that never had one: a
// crash resume (only the id, the title and a position are persisted) and a
// remote client naming an item id. There the media source is the only answer
// there is — a source the server offered with no video stream in it is music —
// but it is only an answer once the ticket for THIS item has arrived. A ticket
// outlives its item: it is replaced when the next PlaybackInfo reply lands, so
// between seeding the queue and that reply the ticket in hand still describes
// the item that just stopped. Consulting it there reported a film as audio for
// a whole round trip because the track before it was audio, which is the flash
// this ordering exists to prevent, in the other direction.
//
// So the fallback answers only for its own item, and "nothing known yet" reads
// as video: guessing audio would put the now-playing panel over the first
// second of every film and then take it back.
bool PlayerController::computeIsAudio() const
{
    if (!m_active)
        return false;
    const QString type = m_currentItemType;
    if (type.compare(QLatin1String("Audio"), Qt::CaseInsensitive) == 0
        || type.compare(QLatin1String("AudioBook"), Qt::CaseInsensitive) == 0)
        return true;
    if (!type.isEmpty())
        return false;
    if (m_ticketItemId.isEmpty() || m_ticketItemId != m_itemId)
        return false;
    const MediaSourceCandidates *entry = m_ticket.source(m_sourceIndex);
    return entry != nullptr && entry->source.videoStream() == nullptr;
}

void PlayerController::updateIsAudio()
{
    const bool value = computeIsAudio();
    if (m_isAudio == value)
        return;
    m_isAudio = value;
    emit isAudioChanged();
}

void PlayerController::setError(const QString &message)
{
    if (m_errorMessage == message)
        return;
    m_errorMessage = message;
    emit errorMessageChanged();
    if (!message.isEmpty())
        qCWarning(logPlayback) << message;
}

} // namespace strmqt
