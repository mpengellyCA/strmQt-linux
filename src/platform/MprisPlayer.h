#pragma once

#include <QObject>
#include <QString>
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
    explicit MprisPlayer(QObject *parent = nullptr);
    ~MprisPlayer() override;

    bool registerOnBus(); // false when D-Bus is unavailable (degrade silently)

    void setPlaybackActive(bool active, bool paused);
    void setNowPlaying(const QString &title, const QString &artist, qint64 durationMs);
    void setPositionMs(qint64 positionMs);
    void notifySeeked(qint64 positionMs);

    // Getters used by the D-Bus adaptors.
    QString playbackStatus() const;
    QVariantMap metadata() const;
    qlonglong positionUs() const { return m_positionMs * 1000; }

signals:
    void playPauseRequested();
    void playRequested();
    void pauseRequested();
    void stopRequested();
    void seekRequested(qint64 deltaMs);
    void setPositionRequested(qint64 positionMs);

private:
    void emitPropertiesChanged(const QVariantMap &changed);

    bool m_registered = false;
    bool m_active = false;
    bool m_paused = false;
    QString m_title;
    QString m_artist;
    qint64 m_durationMs = 0;
    qint64 m_positionMs = 0;
};

} // namespace strmqt
