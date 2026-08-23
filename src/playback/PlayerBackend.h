#pragma once

#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

namespace strmqt {

// Abstract seam for playback engines (libmpv first; libvlc and QtMultimedia later).
// Engines fetch their media over HTTP themselves; URLs are self-authenticating
// (see PlaybackTicket). All positions are milliseconds.
class PlayerBackend : public QObject
{
    Q_OBJECT
    // QML (PlayerPage) selects the matching video plane by engine name.
    Q_PROPERTY(QString engineName READ engineName CONSTANT)
    // e.g. "vaapi" while hardware decoding; empty when unknown/software (M6 AC).
    Q_PROPERTY(QString decoderInfo READ decoderInfo NOTIFY decoderInfoChanged)
    Q_PROPERTY(int volume READ volume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted NOTIFY mutedChanged)
    // Track surface for the OSD: the full list, plus which entry is live. One
    // signal covers all four because the engine only ever rebuilds them together
    // (a selection change also flips a "selected" flag inside the lists).
    Q_PROPERTY(QVariantList audioTracks READ audioTracks NOTIFY tracksChanged)
    Q_PROPERTY(QVariantList subtitleTracks READ subtitleTracks NOTIFY tracksChanged)
    Q_PROPERTY(int currentAudioTrackId READ currentAudioTrackId NOTIFY tracksChanged)
    Q_PROPERTY(int currentSubtitleTrackId READ currentSubtitleTrackId NOTIFY tracksChanged)
    // Milliseconds of media buffered *ahead of the current position*, for the
    // scrubber's second fill. Relative, not absolute: the scrubber draws
    // position → position + bufferedMs.
    Q_PROPERTY(qint64 bufferedMs READ bufferedMs NOTIFY bufferedMsChanged)
    Q_PROPERTY(qreal playbackSpeed READ playbackSpeed NOTIFY playbackSpeedChanged)
    // A/V and subtitle sync offsets in milliseconds; positive = later than video.
    Q_PROPERTY(int audioDelayMs READ audioDelayMs NOTIFY audioDelayChanged)
    Q_PROPERTY(int subtitleDelayMs READ subtitleDelayMs NOTIFY subtitleDelayChanged)
    Q_PROPERTY(QVariantMap videoStats READ videoStats NOTIFY videoStatsChanged)

public:
    enum class State
    {
        Idle,    // nothing loaded
        Loading, // load requested, first frame not yet up
        Playing,
        Paused,
        Ended, // reached end of media normally
        Error, // fatal failure; load() again to recover
    };
    Q_ENUM(State)

    explicit PlayerBackend(QObject *parent = nullptr);
    ~PlayerBackend() override;

    virtual QString engineName() const = 0;

    virtual void load(const QUrl &url, qint64 startMs = 0) = 0;
    virtual void setPaused(bool paused) = 0;
    virtual void stop() = 0;
    virtual void seekTo(qint64 positionMs) = 0;
    virtual void setVolume(int percent) = 0; // 0..130

    // Engine's *current* volume, read back rather than remembered: mpv and VLC
    // both let something else move it (a config binding, a system mixer), and a
    // cached copy would drift. Engines that cannot report leave the default.
    virtual int volume() const { return 100; }
    // A separate mute flag, where the engine has one — muting is not "volume 0"
    // because unmuting must restore the level the user chose. Engines that lack
    // one say so, and PlayerController emulates it by driving volume to zero.
    virtual bool supportsMute() const { return false; }
    virtual bool muted() const { return false; }
    virtual void setMuted(bool muted) { Q_UNUSED(muted); }

    virtual State state() const = 0;
    virtual qint64 positionMs() const = 0;
    virtual qint64 durationMs() const = 0;
    // True while the engine is starved for data (network cache empty).
    virtual bool buffering() const { return false; }
    virtual QString decoderInfo() const { return {}; }

    // Cycle through available audio / subtitle tracks (subtitles include "off").
    // Engines emit trackChanged with a human-readable description for the OSD.
    virtual void cycleAudioTrack() {}
    virtual void cycleSubtitleTrack() {}

    // ── Track lists ───────────────────────────────────────────────────────────
    // Each entry is a QVariantMap with these keys, so QML can bind straight to a
    // delegate without deriving anything itself:
    //   id             int     engine track id; what set*Track() takes back
    //   title          QString ready-to-display label (never empty)
    //   language       QString ISO code as the container spells it, may be empty
    //   codec          QString e.g. "eac3", "subrip"
    //   channels       int     audio channel count, 0 when not applicable
    //   channelLayout  QString e.g. "5.1(side)", may be empty
    //   isDefault      bool
    //   isForced       bool
    //   isExternal     bool    sidecar file rather than a container stream
    //   selected       bool    currently playing
    // Engines with no track surface return empty lists; the UI then falls back
    // to cycleAudioTrack()/cycleSubtitleTrack().
    virtual QVariantList audioTracks() const { return {}; }
    virtual QVariantList subtitleTracks() const { return {}; }
    // -1 when nothing is selected (subtitles off, or no such stream).
    virtual int currentAudioTrackId() const { return -1; }
    virtual int currentSubtitleTrackId() const { return -1; }
    virtual void setAudioTrack(int id) { Q_UNUSED(id); }    // -1 = off/none
    virtual void setSubtitleTrack(int id) { Q_UNUSED(id); } // -1 = off

    // ── Scrubber / stats surface ──────────────────────────────────────────────
    virtual qint64 bufferedMs() const { return 0; }
    virtual qreal playbackSpeed() const { return 1.0; }
    virtual void setPlaybackSpeed(qreal speed) { Q_UNUSED(speed); }
    virtual int audioDelayMs() const { return 0; }
    virtual void setAudioDelayMs(int ms) { Q_UNUSED(ms); }
    virtual int subtitleDelayMs() const { return 0; }

    // Subtitle appearance (ARCHITECTURE.md). One call rather than four so an
    // engine applies the whole look atomically; a partial style is a flicker.
    //   scale       percent of the engine's default size (50-300)
    //   color       "#RRGGBB"
    //   background  0 = outline only, 100 = opaque band behind the text
    //   position    0 = bottom edge, 100 = default, up to 150
    // Engines that cannot style subtitles ignore it; nothing depends on it
    // having happened.
    //   font        family name, or empty for the engine's own choice
    virtual void setSubtitleStyle(const QString &font, int scale, const QString &color,
                                  int background, int position)
    {
        Q_UNUSED(font);
        Q_UNUSED(scale);
        Q_UNUSED(color);
        Q_UNUSED(background);
        Q_UNUSED(position);
    }
    virtual void setSubtitleDelayMs(int ms) { Q_UNUSED(ms); }

    // Volume normalisation from ReplayGain tags (MUSIC.md §6.3).
    //   mode  "off" | "track" | "album"
    // This is a *tag* reader, not an analyser: a file without ReplayGain
    // metadata plays at its own level whatever this says, which is why the
    // control cannot promise loudness matching for an untagged library.
    // Engines without the feature ignore it.
    virtual void setReplayGain(const QString &mode) { Q_UNUSED(mode); }
    // Best-effort decode statistics. Keys the engine cannot answer are *absent*
    // rather than zero, so the panel can hide a row instead of printing a lie:
    //   width, height (int), codec (QString), videoBitrate (qint64, bits/s),
    //   audioCodec (QString), audioChannels (int), audioBitrate (qint64),
    //   fps (qreal, the stream's own rate), containerFps (qreal),
    //   estimatedFps (qreal, measured output rate), droppedFrames (qint64),
    //   hwdec (QString, empty when decoding in software).
    virtual QVariantMap videoStats() const { return {}; }
    // Write a frame of the decoded video to path. No-op where unsupported.
    virtual void screenshotToFile(const QString &path) { Q_UNUSED(path); }

    // ── Frame stepping and A–B loop (ARCHITECTURE.md) ────────────────────────
    // Engines that cannot do these say so, and the controller hides the verbs
    // rather than offering a control that does nothing.
    virtual bool supportsFrameStep() const { return false; }
    // direction: +1 forward, -1 back. Stepping back is expensive on long GOPs
    // and is why this is asked about rather than assumed.
    virtual void frameStep(int direction) { Q_UNUSED(direction); }
    // -1 for either bound clears that end. Kept as engine state rather than a
    // fire-and-forget command, because the UI has to draw the range.
    virtual void setAbLoop(qint64 aMs, qint64 bMs)
    {
        Q_UNUSED(aMs);
        Q_UNUSED(bMs);
    }

signals:
    void stateChanged(strmqt::PlayerBackend::State state);
    void positionChanged(qint64 positionMs);
    void durationChanged(qint64 durationMs);
    // Media ended without a fatal error (State::Ended is set first).
    void endReached();
    // Fatal playback failure; message is human-readable (State::Error is set first).
    void errorOccurred(const QString &message);
    void bufferingChanged(bool buffering);
    void decoderInfoChanged();
    void trackChanged(const QString &description);
    // The engine's volume/mute moved, whoever moved it.
    void volumeChanged(int percent);
    void mutedChanged(bool muted);
    // The track lists or the selection within them changed.
    void tracksChanged();
    void bufferedMsChanged();
    void playbackSpeedChanged();
    void audioDelayChanged();
    void subtitleDelayChanged();
    void videoStatsChanged();
};

} // namespace strmqt
