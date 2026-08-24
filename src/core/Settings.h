#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QPair>
#include <QSettings>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

namespace strmqt {

// Application settings, backed by QSettings. QML-facing via Q_PROPERTY.
// Secrets (tokens, passwords) never go here — they belong to platform/SecretsStore (M1).
//
// Note for anything that reaches around this class: `input/InputMap` opens its
// own QSettings view on the same file and owns the whole `[input]` group
// (`binding\<actionId>`, `lastDevice`). Qt shares one QConfFile per path inside
// a process, so the two views stay consistent — but nothing here may write into
// that group.
class Settings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QUrl serverUrl READ serverUrl WRITE setServerUrl NOTIFY serverUrlChanged)
    Q_PROPERTY(QString densityMode READ densityMode WRITE setDensityMode NOTIFY densityModeChanged)
    Q_PROPERTY(
        QString themeAccent READ themeAccent WRITE setThemeAccent NOTIFY themeAccentChanged)
    Q_PROPERTY(bool reducedMotion READ reducedMotion WRITE setReducedMotion NOTIFY
                   reducedMotionChanged)
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(bool liveUpdatesEnabled READ liveUpdatesEnabled WRITE setLiveUpdatesEnabled NOTIFY
                   liveUpdatesEnabledChanged)
    Q_PROPERTY(int pollIntervalSeconds READ pollIntervalSeconds WRITE setPollIntervalSeconds NOTIFY
                   pollIntervalSecondsChanged)
    // When an episode ends and nothing else is queued, play the next episode of
    // the same series. On by default — that is the behaviour of every TV app,
    // and of Emby's own web player.
    Q_PROPERTY(bool autoPlayNextEpisode READ autoPlayNextEpisode WRITE setAutoPlayNextEpisode
                   NOTIFY autoPlayNextEpisodeChanged)
    // ── Quality (ARCHITECTURE.md) ────────────────────────────────────────────
    // Ceiling handed to the server in the DeviceProfile. 0 = no cap ("auto"),
    // which is not the same as a very large number: a cap makes the server
    // transcode, and asking it to transcode to 200 Mbps is worse than asking
    // it not to.
    Q_PROPERTY(int maxBitrateKbps READ maxBitrateKbps WRITE setMaxBitrateKbps NOTIFY
                   maxBitrateKbpsChanged)
    // "auto" | "directPlay" | "transcode"
    Q_PROPERTY(QString playbackMode READ playbackMode WRITE setPlaybackMode NOTIFY
                   playbackModeChanged)
    // ── Volume normalisation (MUSIC.md §6.3) ─────────────────────────────────
    // ReplayGain tags, applied by the engine: "off" | "track" | "album".
    // Off by default: a gain the user did not ask for is a surprise, and a
    // library with no tags gains nothing from it.
    Q_PROPERTY(QString replayGainMode READ replayGainMode WRITE setReplayGainMode NOTIFY
                   replayGainModeChanged)
    // ── Backdrop (ARCHITECTURE.md) ──────────────────────────────────────
    Q_PROPERTY(bool backdropEnabled READ backdropEnabled WRITE setBackdropEnabled NOTIFY
                   backdropChanged)
    // 0-100. Default 18, matching §2.8's "a wash, not a wallpaper" — art at
    // full strength competes with the text sitting on it.
    Q_PROPERTY(int backdropOpacity READ backdropOpacity WRITE setBackdropOpacity NOTIFY
                   backdropChanged)
    Q_PROPERTY(bool backdropKenBurns READ backdropKenBurns WRITE setBackdropKenBurns NOTIFY
                   backdropChanged)
    // ── Subtitle appearance (ARCHITECTURE.md) ────────────────────────────────
    // Empty means "whatever the engine picked", which is the honest default:
    // naming a family we cannot guarantee is installed would silently fall back
    // to something else anyway.
    Q_PROPERTY(QString subtitleFont READ subtitleFont WRITE setSubtitleFont NOTIFY
                   subtitleStyleChanged)
    Q_PROPERTY(int subtitleScale READ subtitleScale WRITE setSubtitleScale NOTIFY
                   subtitleStyleChanged)
    Q_PROPERTY(QString subtitleColor READ subtitleColor WRITE setSubtitleColor NOTIFY
                   subtitleStyleChanged)
    // 0-100: 0 is a plain outline, 100 an opaque band behind the text.
    Q_PROPERTY(int subtitleBackground READ subtitleBackground WRITE setSubtitleBackground NOTIFY
                   subtitleStyleChanged)
    // 0 = bottom edge, 100 = top. mpv's own sub-pos scale.
    Q_PROPERTY(int subtitlePosition READ subtitlePosition WRITE setSubtitlePosition NOTIFY
                   subtitleStyleChanged)

public:
    explicit Settings(QObject *parent = nullptr);
    // Test constructor: back the store with an explicit INI file instead of the
    // platform-default location.
    explicit Settings(const QString &iniFilePath, QObject *parent = nullptr);
    ~Settings() override;

    QUrl serverUrl() const;
    void setServerUrl(const QUrl &url);

    QString username() const;
    void setUsername(const QString &name);

    // Server-side user id of the authenticated user (not a secret; the token is).
    QString userId() const;
    void setUserId(const QString &id);

    // Stable per-install device id for the Emby device identity; generated and
    // persisted on first call.
    QString deviceId();

    // Playback engine: "mpv" (default) or "vlc" (fallback escape hatch).
    QString playbackEngine() const;
    void setPlaybackEngine(const QString &engine);

    bool autoPlayNextEpisode() const;
    void setAutoPlayNextEpisode(bool enabled);

    int maxBitrateKbps() const;
    void setMaxBitrateKbps(int kbps);
    QString playbackMode() const;
    void setPlaybackMode(const QString &mode);

    // MUSIC.md §6.3. Anything else on disk reads back as "off" rather than
    // being handed to the engine, which would reject it and leave the gain in
    // whatever state the previous file left it.
    QString replayGainMode() const;
    void setReplayGainMode(const QString &mode);
    static QStringList replayGainModes();

    bool backdropEnabled() const;
    void setBackdropEnabled(bool enabled);
    int backdropOpacity() const;
    void setBackdropOpacity(int percent);
    bool backdropKenBurns() const;
    void setBackdropKenBurns(bool enabled);

    QString subtitleFont() const;
    void setSubtitleFont(const QString &family);
    int subtitleScale() const;
    void setSubtitleScale(int percent);
    QString subtitleColor() const;
    void setSubtitleColor(const QString &color);
    int subtitleBackground() const;
    void setSubtitleBackground(int percent);
    int subtitlePosition() const;
    void setSubtitlePosition(int position);

    // Per-library view preferences (ARCHITECTURE.md). Keyed by the library's
    // scope key rather than its title, so renaming a library on the server or
    // having two libraries share a name does not merge their settings.
    // An empty mode / a step of -1 means "nothing stored", which the UI reads
    // as "use the default" rather than as a value.
    Q_INVOKABLE QString libraryViewMode(const QString &libraryKey) const;
    Q_INVOKABLE void setLibraryViewMode(const QString &libraryKey, const QString &mode);
    Q_INVOKABLE int libraryCardSizeStep(const QString &libraryKey) const;
    Q_INVOKABLE void setLibraryCardSizeStep(const QString &libraryKey, int step);

    // Per-item track memory (ARCHITECTURE.md). Emby track ids are per media
    // source, so the key carries both: the same item played from a different
    // version has different track numbering.
    Q_INVOKABLE void rememberTracks(const QString &itemId, const QString &mediaSourceId, int audioId,
                        int subtitleId);
    // Whether anything was ever stored for this item+source. Needed because a
    // remembered subtitle of -1 means "the user turned subtitles OFF", which is
    // a real choice and indistinguishable from the not-stored default without
    // this.
    Q_INVOKABLE bool hasRememberedTracks(const QString &itemId,
                                         const QString &mediaSourceId) const;
    // -1 for either element means "off" when hasRememberedTracks() is true, and
    // "nothing remembered" when it is false.
    QPair<int, int> recalledTracks(const QString &itemId, const QString &mediaSourceId) const;
    // Version chosen for an item, or an empty string. Remembered by media
    // source ID, not index: the server can reorder sources between requests.
    Q_INVOKABLE QString rememberedVersion(const QString &itemId) const;
    Q_INVOKABLE void rememberVersion(const QString &itemId, const QString &mediaSourceId);

    // HDR→SDR tone-mapping curve for the mpv engine (libplacebo name).
    QString toneMapping() const;

    // ── Appearance (ARCHITECTURE.md) ───────────────────────────────────────
    // Theme.density multiplier selector: "compact" | "comfortable" | "tv".
    // Anything else on disk reads back as the default, "comfortable".
    QString densityMode() const;
    void setDensityMode(const QString &mode);
    static QStringList densityModes();

    // Accent identity: "projection" (default, amber) | "emby" | "jellyfin" |
    // "breeze". Unknown values read back as "projection".
    QString themeAccent() const;
    void setThemeAccent(const QString &accent);
    static QStringList themeAccents();
    bool reducedMotion() const;
    void setReducedMotion(bool reduced);

    // ── Playback volume (ARCHITECTURE.md) ────────────────────────────────────
    // 0–130, matching PlayerBackend::setVolume()'s contract; values outside the
    // range are clamped on the way in and on the way out.
    int volume() const;
    void setVolume(int percent);
    bool muted() const;
    void setMuted(bool muted);

    // ── Live updates (ARCHITECTURE.md) ─────────────────────────────────────
    // Master switch for the event socket and its polling fallback. Off means
    // the UI only refreshes when the user asks (LiveUpdateService reports
    // transport "off"). Default on.
    bool liveUpdatesEnabled() const;
    void setLiveUpdatesEnabled(bool enabled);

    // Polling-fallback period in seconds, used when the socket cannot connect.
    // The vocabulary below is what the settings UI offers; anything else on disk
    // is clamped into [kMinPollSeconds, kMaxPollSeconds] rather than rejected, so
    // a hand-edited INI still yields a sane timer instead of a busy loop.
    int pollIntervalSeconds() const;
    void setPollIntervalSeconds(int seconds);
    static QList<int> pollIntervalChoices();

    // Crash-resume record: written during playback, cleared on clean stop.
    // lastPlayback() returns {} when the previous session ended cleanly.
    void setLastPlayback(const QString &itemId, const QString &title, qint64 positionMs);
    // Force pending settings to durable storage at lifecycle boundaries. The
    // five-second playback checkpoint path is deliberately debounced.
    void flush();
    // Call once a server and user are both known: moves the pre-scoping keys
    // into that session's scope. A no-op afterwards, and for every session but
    // the first one to run it.
    void migrateLegacySessionData();
    QVariantMap lastPlayback() const;
    void clearLastPlayback();

signals:
    void serverUrlChanged();
    void autoPlayNextEpisodeChanged();
    void maxBitrateKbpsChanged();
    void playbackModeChanged();
    void replayGainModeChanged();
    void subtitleStyleChanged();
    void backdropChanged();
    void usernameChanged();
    void densityModeChanged();
    void themeAccentChanged();
    void reducedMotionChanged();
    void volumeChanged();
    void mutedChanged();
    void liveUpdatesEnabledChanged();
    void pollIntervalSecondsChanged();

private:
    struct PendingLastPlayback
    {
        QString itemKey;
        QString titleKey;
        QString positionKey;
        QString itemId;
        QString title;
        qint64 positionMs = 0;
        bool dirty = false;
    };

    QString sessionScope() const;
    QString scopedKey(const QString &key) const;
    void writePendingLastPlayback();
    void touchRetainedPlaybackKey(const QString &group, const QString &entry);
    void pruneRetainedPlaybackState();
    void pruneRetainedPlaybackGroup(const QString &group, const QString &touched = {});
    QSettings m_store;
    QElapsedTimer m_lastPlaybackSync;
    QString m_lastPlaybackSyncIdentity;
    PendingLastPlayback m_pendingLastPlayback;
};

} // namespace strmqt
