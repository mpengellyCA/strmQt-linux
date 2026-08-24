#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

namespace strmqt {

// org.mpris.MediaPlayer2.strmqt — the standard media-control surface. This is
// what makes KDE Connect's phone controls, Plasma's media applet, and playerctl
// work with zero pairing (PLAN §3.2 decision 6).
//
// State flows in via the set* methods (wired from PlayerController); remote
// commands flow out as the *Requested signals.
class MprisPlayer : public QObject
{
    Q_OBJECT

public:
    // Everything MPRIS is willing to say about the item under the playhead.
    //
    // A field left at its default means UNKNOWN and is left OUT of Metadata
    // rather than published empty. That distinction is not pedantry: a client
    // renders an empty xesam:album as a visible blank line and a 0.0
    // xesam:userRating as "rated zero stars", so absent is strictly better than
    // empty for every key here.
    struct TrackInfo
    {
        QString itemId; // namespaces mpris:trackid; not published on its own
        QString title;
        QStringList artists;
        QString album;
        QStringList albumArtists;
        QUrl artUrl; // must be a URI a *foreign process* can open (file://)
        qint64 durationMs = 0;
        int trackNumber = -1;     // < 1 is unknown; there is no track zero
        int useCount = -1;        // < 0 is unknown
        double userRating = -1.0; // [0.0, 1.0]; < 0 is unrated

        bool operator==(const TrackInfo &) const = default;
    };

    explicit MprisPlayer(QObject *parent = nullptr);
    ~MprisPlayer() override;

    bool registerOnBus(); // false when D-Bus is unavailable (degrade silently)

    void setPlaybackActive(bool active, bool paused);
    // Resolving/loading cannot accept transport or seek commands yet even
    // though a playback session exists. Keep that state separate from active
    // so desktop clients do not offer controls that target the outgoing load.
    void setBusy(bool busy);
    void setNowPlaying(const TrackInfo &track);
    // Artwork is fetched asynchronously and lands after the rest of the track,
    // so it gets its own setter instead of forcing the caller to hold a whole
    // TrackInfo just to fill one field in later.
    void setArtUrl(const QUrl &artUrl);
    // CanGoNext / CanGoPrevious. Hardcoding these to false made Plasma's applet
    // draw both transport buttons dead; not re-announcing them on change is the
    // same bug one step later, because an applet reads the property once.
    void setQueueState(bool hasNext, bool hasPrevious);
    void setPositionMs(qint64 positionMs);
    void notifySeeked(qint64 positionMs);
    // MPRIS uses 1.0 as nominal volume and permits amplification above it.
    // PlayerController's 0..130 surface maps to 0.0..1.3 here.
    void setVolume(double volume);
    void requestVolumeChange(double volume);
    // MPRIS SetPosition carries the track id the client observed. Reject a
    // delayed command after the queue has advanced to a different item.
    void requestSetPosition(const QString &trackPath, qint64 positionMs);

    // Getters used by the D-Bus adaptors.
    QString playbackStatus() const;
    QVariantMap metadata() const;
    qlonglong positionUs() const { return m_positionMs * 1000; }
    bool canGoNext() const { return m_hasNext; }
    bool canGoPrevious() const { return m_hasPrevious; }
    bool canPlay() const { return m_active && !m_busy; }
    bool canPause() const { return m_active && !m_busy; }
    bool canSeek() const { return m_active && !m_busy; }
    bool playbackActive() const { return m_active; }
    double volume() const { return m_volume; }

signals:
    void playPauseRequested();
    void playRequested();
    void pauseRequested();
    void stopRequested();
    void nextRequested();
    void previousRequested();
    void seekRequested(qint64 deltaMs);
    void setPositionRequested(qint64 positionMs);
    void volumeRequested(double volume);

private:
    void emitPropertiesChanged(const QVariantMap &changed);
    void emitMetadataChanged();

    bool m_registered = false;
    bool m_active = false;
    bool m_paused = false;
    bool m_busy = false;
    bool m_hasNext = false;
    bool m_hasPrevious = false;
    TrackInfo m_track;
    qint64 m_positionMs = 0;
    double m_volume = 1.0;
    QString m_serviceName;
};

} // namespace strmqt
