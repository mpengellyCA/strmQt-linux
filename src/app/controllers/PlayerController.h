#pragma once

#include "app/PlayQueue.h"
#include "playback/PlayerBackend.h"
#include "server/dto/PlaybackTicket.h"

#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <optional>

namespace strmqt {

class Settings;
namespace emby {
class EmbyClient;
}

// Owns the playback session around a PlayerBackend (PLAN §3.5): fetches the
// stream ticket, walks the ladder (DirectPlay → DirectStream → Transcode) on
// startup failure, reports start/progress/stop to the server, and runs the
// robustness layer: stall watchdog (seek → reload → demote, never spin),
// reason-preserving EOF/error handling, mid-stream ticket refresh with backoff,
// and crash-resume persistence.
class PlayerController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *backend READ backendObject CONSTANT)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY pausedChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    // Is this session music rather than a picture (MUSIC.md §4)? The docked bar
    // and the player page both lay themselves out entirely differently on the
    // answer, and both used to derive it themselves from the queue entry's type
    // and the chosen source's streams — two derivations of one fact, which is
    // one more than the architecture allows. It lives here now; QML reads it.
    Q_PROPERTY(bool isAudio READ isAudio NOTIFY isAudioChanged)
    Q_PROPERTY(qint64 positionMs READ positionMs NOTIFY positionChanged)
    // Text clocks do not need the engine's frame-rate position stream. Keep a
    // whole-second snapshot beside the smooth scrubber property so changing a
    // digit does one QML update rather than 24-60 identical ones.
    Q_PROPERTY(qint64 positionSeconds READ positionSeconds NOTIFY positionSecondsChanged)
    // Absolute buffered endpoint sampled when the backend's buffered amount
    // changes. QML must not add relative bufferedMs to the per-frame playhead:
    // doing so animates a value whose source only changes every ~250 ms.
    Q_PROPERTY(qint64 bufferedEndMs READ bufferedEndMs NOTIFY bufferedEndChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY durationChanged)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(QString streamMethod READ streamMethod NOTIFY streamMethodChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(bool buffering READ buffering NOTIFY bufferingChanged)
    // Volume surface (ARCHITECTURE.md). 0–130, matching PlayerBackend's contract:
    // above 100 is mpv's software amplification, which is a real and wanted
    // feature on quiet sources. Persisted across sessions through Settings, so
    // the InputMap `player.volumeUp` / `player.volumeDown` bindings finally have
    // a verb behind them.
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(int maxVolume READ maxVolume CONSTANT)
    Q_PROPERTY(qreal playbackSpeed READ playbackSpeed NOTIFY playbackSpeedChanged)
    Q_PROPERTY(int audioDelayMs READ audioDelayMs NOTIFY audioDelayChanged)
    Q_PROPERTY(int subtitleDelayMs READ subtitleDelayMs NOTIFY subtitleDelayChanged)
    // Version (MediaSource) surface — a later wave builds the picker on top.
    Q_PROPERTY(int sourceCount READ sourceCount NOTIFY sourcesChanged)
    Q_PROPERTY(int sourceIndex READ sourceIndex NOTIFY sourceIndexChanged)
    Q_PROPERTY(QVariantList sources READ sources NOTIFY sourcesChanged)
    Q_PROPERTY(QVariantMap currentSource READ currentSource NOTIFY sourceIndexChanged)
    Q_PROPERTY(QVariantMap videoStream READ videoStream NOTIFY sourceIndexChanged)
    Q_PROPERTY(QVariantList audioStreams READ audioStreams NOTIFY sourceIndexChanged)
    Q_PROPERTY(QVariantList subtitleStreams READ subtitleStreams NOTIFY sourceIndexChanged)
    // Queue surface (ARCHITECTURE.md). The controller owns the queue for the
    // lifetime of the session object; QML binds straight to it for the queue
    // panel, and to the properties below for the OSD's prev/next and the Up
    // Next card.
    Q_PROPERTY(strmqt::PlayQueue *queue READ queue CONSTANT)
    Q_PROPERTY(bool hasNext READ hasNext NOTIFY queueStateChanged)
    Q_PROPERTY(bool hasPrevious READ hasPrevious NOTIFY queueStateChanged)
    Q_PROPERTY(QVariantMap nextItem READ nextItem NOTIFY queueStateChanged)
    Q_PROPERTY(bool upNextVisible READ upNextVisible NOTIFY upNextChanged)
    Q_PROPERTY(int upNextSecondsRemaining READ upNextSecondsRemaining NOTIFY upNextChanged)
    // Chapters for the current item: scrubber markers, PgUp/PgDn-class jumps and
    // the chapter panel. They come from /Users/{uid}/Items/{id} (Fields=Chapters),
    // NOT from PlaybackInfo, so they are fetched alongside the ticket rather than
    // extracted from it. Empty for items the server has no chapters for.
    Q_PROPERTY(QVariantList chapters READ chapters NOTIFY chaptersChanged)
    Q_PROPERTY(int currentChapter READ currentChapter NOTIFY currentChapterChanged)

public:
    PlayerController(emby::EmbyClient *client, PlayerBackend *backend, Settings *settings = nullptr,
                     QObject *parent = nullptr);

    QObject *backendObject() const;
    bool active() const { return m_active; }
    bool paused() const;
    bool busy() const { return m_busy; }
    bool isAudio() const { return m_isAudio; }
    qint64 positionMs() const { return m_backend->positionMs(); }
    qint64 positionSeconds() const { return m_positionSeconds; }
    qint64 bufferedEndMs() const { return m_bufferedEndMs; }
    qint64 durationMs() const;
    QString title() const { return m_title; }
    QString streamMethod() const;
    QString errorMessage() const { return m_errorMessage; }

    int sourceCount() const { return static_cast<int>(m_ticket.sourceCount()); }
    int sourceIndex() const { return static_cast<int>(m_sourceIndex); }
    QVariantList sources() const;
    QVariantMap currentSource() const;
    QVariantMap videoStream() const;
    QVariantList audioStreams() const;
    QVariantList subtitleStreams() const;

    PlayQueue *queue() const { return m_queue; }
    bool hasNext() const { return m_queue->hasNext(); }
    bool hasPrevious() const { return m_queue->hasPrevious(); }
    // The item the Up Next card offers; {} when the queue has nothing after this.
    QVariantMap nextItem() const;
    bool upNextVisible() const { return m_upNextVisible; }
    int upNextSecondsRemaining() const { return m_upNextSeconds; }

    // Replace the queue and start playing it (ARCHITECTURE.md).
    Q_INVOKABLE void playQueue(const QVariantList &items, int startIndex = 0);
    // Same, for C++ callers that already hold real items — ItemActions' fetches
    // go through here rather than round-tripping through QVariantMaps. Shuffling
    // is applied before the first item starts, so it is one deal, not a restart.
    void playQueueItems(QList<MediaItem> items, int startIndex = 0, bool shuffled = false);
    // Skip forward: reports the current item stopped, then plays the next one.
    Q_INVOKABLE void playNext();
    // Restarts the current item when more than 5 s in (the universal transport
    // convention), otherwise steps back a queue entry.
    Q_INVOKABLE void playPrevious();
    // Dismiss the card and suppress the auto-advance for *this* item only.
    Q_INVOKABLE void cancelUpNext();

    QVariantList chapters() const { return m_chapters; }
    // Index of the chapter containing the playhead, or -1 when there are none.
    int currentChapter() const { return m_currentChapter; }
    Q_INVOKABLE void seekToChapter(int index);
    Q_INVOKABLE void nextChapter();
    // Mirrors every player's "previous" behaviour: within the first few seconds
    // of a chapter it steps back one, otherwise it restarts the current chapter.
    Q_INVOKABLE void previousChapter();

    // `preferredSourceIndex` < 0 means "let the ticket decide" (server order).
    // `itemType` is the server's item type ("Audio", "Movie", ...) when the
    // caller knows it: this path seeds the queue with a single entry, and the
    // type is what `isAudio` reads first. Callers that genuinely have no type —
    // a crash resume, a remote client naming a bare id — leave it empty and the
    // answer waits for the ticket.
    Q_INVOKABLE void playItem(const QString &itemId, const QString &title,
                              qint64 startPositionMs = 0, int preferredSourceIndex = -1,
                              const QString &itemType = QString());
    // Pick a version. Applies immediately when a session is running (reloads at
    // the current position, ladder restarts at the top rung of the new source);
    // otherwise it is remembered for the next playItem() that does not override it.
    Q_INVOKABLE void setPreferredSource(int index);

    // Speed and A/V sync (ARCHITECTURE.md). The backend has carried these since
    // M3 and nothing exposed them, so no UI could reach them.
    Q_INVOKABLE void setPlaybackSpeed(qreal speed);
    Q_INVOKABLE void setAudioDelayMs(int ms);
    Q_INVOKABLE void setSubtitleDelayMs(int ms);
    // Re-applies the stored subtitle look to the engine. Called on start and
    // whenever Settings changes, so a tweak is visible on the frame in front of
    // the user rather than at the next play.
    Q_INVOKABLE void applySubtitleStyle();
    // Same contract for volume normalisation (MUSIC.md §6.3): pushed on start
    // and whenever the preference changes, because mpv resets the audio chain
    // per file and a shuffled library must not step between levels.
    Q_INVOKABLE void applyReplayGain();
    // Dev/test entry: play a URL directly, no server ticket or reporting.
    Q_INVOKABLE void playUrl(const QUrl &url, const QString &title);
    Q_INVOKABLE void togglePause();
    // Explicit pause intent, for callers that mean one specific state rather
    // than "the other one". MPRIS is the reason it exists: Play and Pause are
    // separate verbs there and routing Play through togglePause() pauses a
    // track that was already playing.
    Q_INVOKABLE void setPaused(bool paused);
    Q_INVOKABLE void stop();
    // Stronger than a user stop: an account/server boundary must discard every
    // deferred continuation and every queue/metadata reference to the outgoing
    // identity. Application calls this before credentials are replaced.
    void shutdownForSessionBoundary();
    Q_INVOKABLE void seekTo(qint64 positionMs);
    Q_INVOKABLE void seekRelative(qint64 deltaMs);
    Q_INVOKABLE void cycleAudioTrack();
    Q_INVOKABLE void cycleSubtitleTrack();
    // Explicit track selection. These exist so the *controller* sees the
    // choice: the track panel used to call the backend directly, which meant
    // nothing could remember what the user picked (ARCHITECTURE.md).
    Q_INVOKABLE void setAudioTrack(int id);
    Q_INVOKABLE void setSubtitleTrack(int id);
    // The ENGINE's track lists, as opposed to audioStreams()/subtitleStreams()
    // which are the SERVER's. The two describe the same media but number it
    // differently, and anything mapping between them needs both.
    // ── Frame stepping, screenshots, A–B loop (ARCHITECTURE.md) ──────────────
    Q_PROPERTY(bool canFrameStep READ canFrameStep CONSTANT)
    // -1 when unset. Both bounds are observable because the scrubber draws them.
    Q_PROPERTY(qint64 loopStartMs READ loopStartMs NOTIFY abLoopChanged)
    Q_PROPERTY(qint64 loopEndMs READ loopEndMs NOTIFY abLoopChanged)

    bool canFrameStep() const { return m_backend && m_backend->supportsFrameStep(); }
    qint64 loopStartMs() const { return m_loopStartMs; }
    qint64 loopEndMs() const { return m_loopEndMs; }

    // +1 forward, -1 back. Pausing is the engine's job and it does it.
    Q_INVOKABLE void frameStep(int direction);
    // One verb, three states: sets A, then B, then clears. A separate "clear"
    // button for something you reach by pressing the same key again is clutter.
    Q_INVOKABLE void markLoopPoint();
    Q_INVOKABLE void clearAbLoop();
    // Writes a PNG under Pictures/StrmQt and returns the path, or an empty
    // string. The path policy lives here rather than in QML: a page should not
    // be inventing filesystem locations.
    Q_INVOKABLE QString takeScreenshot();

    QVariantList backendAudioTracks() const;
    QVariantList backendSubtitleTracks() const;

    int volume() const { return m_volume; }
    qreal playbackSpeed() const { return m_backend ? m_backend->playbackSpeed() : 1.0; }
    int audioDelayMs() const { return m_backend ? m_backend->audioDelayMs() : 0; }
    int subtitleDelayMs() const { return m_backend ? m_backend->subtitleDelayMs() : 0; }
    bool muted() const { return m_muted; }
    static int maxVolume() { return kMaxVolume; }
    // Clamped to [0, maxVolume]; persisted immediately so a crash mid-session
    // does not lose the level.
    Q_INVOKABLE void setVolume(int percent);
    // Relative step for the volumeUp/volumeDown bindings and the wheel.
    Q_INVOKABLE void adjustVolume(int delta);
    Q_INVOKABLE void setMuted(bool muted);
    Q_INVOKABLE void toggleMute();

    bool buffering() const { return m_backend->buffering(); }

    // Crash resume: non-empty map {itemId, title, positionMs} when the previous
    // run died mid-playback (record persists every 5 s, cleared on clean end).
    Q_INVOKABLE QVariantMap crashResumeInfo() const;
    Q_INVOKABLE void clearCrashResume();

    // Shrinks watchdog/backoff timing so integration tests run in milliseconds.
    void setTimingForTests(int watchdogTickMs, int stallTicks, int backoffBaseMs);
    void setScreenshotDirectoryForTests(const QString &directory)
    {
        m_screenshotDirectoryOverride = directory;
    }

signals:
    void activeChanged();
    void pausedChanged();
    void busyChanged();
    void isAudioChanged();
    void positionChanged();
    void positionSecondsChanged();
    void bufferedEndChanged();
    void durationChanged();
    void titleChanged();
    void streamMethodChanged();
    void errorMessageChanged();
    void bufferingChanged();
    void sourcesChanged();
    void sourceIndexChanged();
    void volumeChanged();
    void playbackSpeedChanged();
    void audioDelayChanged();
    void subtitleDelayChanged();
    // A version switch was refused (the server offered no playable ladder for
    // it). The UI surfaces this as a toast; without it the picker just snaps
    // back and looks broken.
    void sourceSwitchFailed(const QString &reason);
    void abLoopChanged();
    // Path of a screenshot just written; the UI confirms with a toast.
    void screenshotSaved(const QString &path);
    void screenshotFailed(const QString &reason);
    void mutedChanged();
    // Session over (clean end with nothing left to play, stop, or fatal error) —
    // UI pops the player page. A clean end that auto-advances does NOT emit it.
    void chaptersChanged();
    void currentChapterChanged();
    void stopped();
    // Queue shape or cursor moved: hasNext / hasPrevious / nextItem.
    void queueStateChanged();
    void upNextChanged();
    // OSD toast for track switches ("Audio: eng — DTS 5.1").
    void trackChanged(const QString &description);
    // The playhead was MOVED, as opposed to having advanced on its own. Distinct
    // from positionChanged, which fires on every tick: a remote client that
    // extrapolates position between polls needs to be told when its extrapolation
    // became wrong, and MPRIS's Seeked signal is exactly that.
    void seeked(qint64 positionMs);
    // A new item was started because the user asked for it — as opposed to a
    // queue advancing, an episode auto-playing, or a record being put back
    // after a film. The shell uses it to decide whether a playback surface has
    // to come forward: a film started while a record is playing needs its
    // picture at the moment it is chosen, not the next time the session
    // happens to begin.
    void itemStarted();

private:
    // A record put aside because the user chose a film. Everything needed to
    // put it back exactly as it was, which is more than a position: the queue,
    // its play order, the shuffle and the repeat mode are the state.
    struct SuspendedAudio
    {
        PlayQueue::Snapshot queue;
        QString itemId;
        QString title;
        QString itemType;
        qint64 positionMs = 0;

        bool isValid() const { return queue.isValid() && !itemId.isEmpty(); }
    };

    enum class TerminationReason
    {
        CleanEnd,
        UserStop,
        Failure,
        SessionBoundary,
    };

    // PlayerBackend::setVolume() contract; mirrored in Settings.
    static constexpr int kMaxVolume = 130;

    // Every entry point into a new item funnels through here; `fromQueue` is
    // what stops a plain playItem() from being mistaken for queue playback (it
    // reseeds the queue as a one-item queue instead).
    void startItem(const QString &itemId, const QString &title, qint64 startPositionMs,
                   int preferredSourceIndex, bool fromQueue,
                   const QString &itemType = QString(), bool initiallyPaused = false);
    // Plays whatever the queue's cursor points at. Without `force`, an item that
    // is already the running one is left alone (a re-index is not a restart).
    bool startQueueCurrent(bool force);
    // Clean-end auto-advance; false when the queue is exhausted or the user
    // cancelled the Up Next card for this item.
    bool advanceToNext();
    void onQueueCurrentChanged();
    // The queue put a different item under the cursor. `displaced` marks the one
    // case that is not a request to play: the row that was current was removed.
    void onQueueItemChanged(bool displaced);
    void onQueueExhausted();
    void updateUpNext();
    void setUpNext(bool visible, int seconds);

    void startAttempt(qint64 startMs);
    // Push the current volume/mute intent at the engine. Called whenever either
    // changes and again on every load, because an engine that was recreated (or
    // an engine whose volume a ladder reload reset) must not come back loud.
    void applyVolume();
    void onBackendState(PlayerBackend::State state, PlayerBackend::LoadId loadId);
    void onBackendError(const QString &message, PlayerBackend::LoadId loadId);
    void onEndReached(PlayerBackend::LoadId loadId);
    void onWatchdogTick();
    void escalateStall();
    void recoverMidStream();
    void reportProgress();
    void report(int kind); // 0 start, 1 progress, 2 stopped
    // Ends the server-side session for whatever is playing *now*. Every path
    // that replaces the item under the playhead calls this first, or the server
    // is never told the old item stopped and the progress timer keeps running
    // into the next one (see the comment on the definition).
    static bool typeIsAudio(const QString &type);
    void closeCurrentSession();
    // Put the playing record aside / take it back out. Both no-ops unless
    // there is something to do.
    void suspendAudioSession();
    void resumeSuspendedAudio();
    void finishSession(TerminationReason reason);
    void persistResume();
    void setActive(bool active);
    void fetchChapters(const QString &itemId, int generation);
    void clearChapters();
    void updateCurrentChapter(qint64 positionMs);
    void updatePositionSnapshots(qint64 positionMs, bool forceInternal = false);
    void updateBufferedEnd(qint64 positionMs);
    // Continue a series when nothing else is queued. Returns true when it took
    // responsibility for what happens next.
    bool tryAutoPlayNextEpisode();
    // Writes the current pair to Settings, keyed by item AND media source.
    void rememberCurrentTracks();
    // Applies a remembered pair once the engine has published its track list.
    void restoreRememberedTracks();
    void setBusy(bool busy);
    // The answer isAudio() caches, and the one place that recomputes it. Both
    // inputs — the queue's cursor and the chosen media source — move on their
    // own signals, so the update hangs off those rather than being remembered
    // by hand on every path that could have moved one.
    bool computeIsAudio() const;
    void updateIsAudio();
    void setError(const QString &message);
    PlaybackProgress progressNow() const;
    // Current rung of the *current source*; nullptr when there is nothing to play.
    const StreamCandidate *currentCandidate() const
    {
        return m_ticket.candidate(m_sourceIndex, m_rung);
    }
    bool hasTicket() const { return currentCandidate() != nullptr; }
    // Rungs available below the current one, within the current source only.
    bool canDemote() const { return m_rung + 1 < m_ticket.rungCount(m_sourceIndex); }
    void selectSource(qsizetype index);

    emby::EmbyClient *m_client;
    PlayerBackend *m_backend;
    Settings *m_settings;
    PlayQueue *m_queue;
    QTimer m_progressTimer;
    QTimer m_watchdog;
    QTimer m_persistTimer;

    PlaybackTicket m_ticket;
    // Which item the ticket above was fetched for. A ticket outlives the item
    // it belongs to: it is only replaced when the async PlaybackInfo reply for
    // the NEXT item lands, so anything that reads the ticket to describe what
    // is playing has to be able to tell "not resolved yet" from "resolved".
    QString m_ticketItemId;
    QString m_itemId;
    // Remembered tracks are applied once per item, when the engine first
    // publishes its track list; re-applying would fight the user's own picks.
    bool m_tracksRestored = false;
    // Series context of the item playing, captured from the details fetch that
    // already runs for chapters. A bare playItem() seeds the queue with only an
    // id and a title, so this is the only place the series is known.
    QString m_currentSeriesId;
    QString m_currentItemType;
    qint64 m_loopStartMs = -1;
    qint64 m_loopEndMs = -1;
    QString m_title;
    qsizetype m_sourceIndex = -1;
    qsizetype m_rung = 0;
    // -1 = automatic (ticket default). Set by setPreferredSource()/playItem().
    int m_preferredSourceIndex = -1;
    qint64 m_lastPositionMs = 0;
    qint64 m_positionSeconds = 0;
    qint64 m_bufferedEndMs = 0;
    qint64 m_lastInternalPositionMs = -1;
    int m_volume = 100;
    bool m_muted = false;
    bool m_applyingVolume = false; // guards the engine → controller echo
    bool m_active = false;
    bool m_busy = false;
    bool m_isAudio = false;
    bool m_started = false;   // current rung got to Playing at least once
    bool m_reporting = false; // this session reports to the server
    int m_generation = 0;
    // True while a start comes from the queue moving rather than from someone
    // choosing an item: an auto-advance, an episode auto-play, a skip, or a
    // record being put back. Those must not announce itemStarted(), because a
    // shell that brings a playback surface forward for them would yank the
    // user off the page they are on for a button they pressed to stay there.
    bool m_transportStart = false;
    // A record put back after a film does not start playing on its own: the
    // user stopped watching something, they did not ask for music. It comes
    // back where it was and waits.
    bool m_initiallyPaused = false;
    SuspendedAudio m_suspendedAudio;
    int m_resumeToken = 0;
    PlayerBackend::LoadId m_nextLoadId = 0;
    PlayerBackend::LoadId m_expectedLoadId = 0;
    QString m_errorMessage;
    QString m_screenshotDirectoryOverride;
    QVariantList m_chapters;
    QList<qint64> m_chapterStarts;
    int m_currentChapter = -1;

    // Watchdog / recovery state
    qint64 m_watchdogLastPos = -1;
    int m_stallTicks = 0;
    int m_healthyTicks = 0;
    int m_stallStep = 0;      // 0 nudge-seek, 1 reload rung, 2 demote
    int m_recoverRetries = 0; // mid-stream ticket-refresh attempts this incident
    int m_recoveryToken = 0;
    bool m_recovering = false;
    int m_stallTicksLimit = 3;
    int m_backoffBaseMs = 1000;

    // Queue / Up Next state. m_queueDriving marks the window in which *we* are
    // moving the queue, so the queue's own signals do not re-enter and start
    // playback twice.
    bool m_queueDriving = false;
    bool m_upNextCancelled = false;
    bool m_upNextVisible = false;
    int m_upNextSeconds = 0;

    // Track setters are requests. Persistence happens only after the backend's
    // asynchronous readback publishes the selection that actually took effect.
    bool m_trackSelectionPending = false;
    bool m_restoringTracks = false;
    std::optional<int> m_pendingAudioTrack;
    std::optional<int> m_pendingSubtitleTrack;
};

} // namespace strmqt
