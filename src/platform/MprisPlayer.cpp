#include "MprisPlayer.h"

#include "core/Log.h"

#include <QCoreApplication>
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>

namespace strmqt {

namespace {
const auto kServiceName = QStringLiteral("org.mpris.MediaPlayer2.strmqt");
const auto kObjectPath = QStringLiteral("/org/mpris/MediaPlayer2");
const auto kPlayerInterface = QStringLiteral("org.mpris.MediaPlayer2.Player");
} // namespace

// org.mpris.MediaPlayer2 (root)
class MprisRootAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ canQuit)
    Q_PROPERTY(bool CanRaise READ canRaise)
    Q_PROPERTY(bool HasTrackList READ hasTrackList)
    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes)
    Q_PROPERTY(QStringList SupportedMimeTypes READ supportedMimeTypes)

public:
    explicit MprisRootAdaptor(MprisPlayer *parent) : QDBusAbstractAdaptor(parent) {}

    bool canQuit() const { return false; }
    bool canRaise() const { return false; }
    bool hasTrackList() const { return false; }
    QString identity() const { return QStringLiteral("StrmQt"); }
    QStringList supportedUriSchemes() const { return {}; }
    QStringList supportedMimeTypes() const { return {}; }

public slots:
    void Raise() {}
    void Quit() {}
};

// org.mpris.MediaPlayer2.Player
class MprisPlayerAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(double Rate READ rate WRITE setRate)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(double MinimumRate READ minimumRate)
    Q_PROPERTY(double MaximumRate READ maximumRate)
    Q_PROPERTY(bool CanGoNext READ canGoNext)
    Q_PROPERTY(bool CanGoPrevious READ canGoPrevious)
    Q_PROPERTY(bool CanPlay READ canPlay)
    Q_PROPERTY(bool CanPause READ canPause)
    Q_PROPERTY(bool CanSeek READ canSeek)
    Q_PROPERTY(bool CanControl READ canControl)

public:
    explicit MprisPlayerAdaptor(MprisPlayer *parent)
        : QDBusAbstractAdaptor(parent), m_player(parent)
    {
    }

    QString playbackStatus() const { return m_player->playbackStatus(); }
    double rate() const { return 1.0; }
    void setRate(double) {}
    QVariantMap metadata() const { return m_player->metadata(); }
    double volume() const { return 1.0; }
    void setVolume(double) {}
    qlonglong position() const { return m_player->positionUs(); }
    double minimumRate() const { return 1.0; }
    double maximumRate() const { return 1.0; }
    bool canGoNext() const { return false; }
    bool canGoPrevious() const { return false; }
    bool canPlay() const { return true; }
    bool canPause() const { return true; }
    bool canSeek() const { return true; }
    bool canControl() const { return true; }

signals:
    void Seeked(qlonglong positionUs);

public slots:
    void PlayPause() { emit m_player->playPauseRequested(); }
    void Play() { emit m_player->playRequested(); }
    void Pause() { emit m_player->pauseRequested(); }
    void Stop() { emit m_player->stopRequested(); }
    void Next() {}
    void Previous() {}
    void Seek(qlonglong offsetUs) { emit m_player->seekRequested(offsetUs / 1000); }
    void SetPosition(const QDBusObjectPath &, qlonglong positionUs)
    {
        emit m_player->setPositionRequested(positionUs / 1000);
    }
    void OpenUri(const QString &) {}

private:
    MprisPlayer *m_player;
};

MprisPlayer::MprisPlayer(QObject *parent) : QObject(parent)
{
    new MprisRootAdaptor(this);
    new MprisPlayerAdaptor(this);
}

MprisPlayer::~MprisPlayer()
{
    if (m_registered) {
        QDBusConnection::sessionBus().unregisterObject(kObjectPath);
        QDBusConnection::sessionBus().unregisterService(kServiceName);
    }
}

bool MprisPlayer::registerOnBus()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qCInfo(logApp) << "session bus unavailable; MPRIS disabled";
        return false;
    }
    if (!bus.registerService(kServiceName)) {
        // Another instance holds the well-known name; the MPRIS spec allows a
        // .instance-suffixed name so controllers can still find us.
        const QString fallback = kServiceName + QStringLiteral(".instance") +
                                 QString::number(QCoreApplication::applicationPid());
        if (!bus.registerService(fallback)) {
            qCWarning(logApp) << "could not register MPRIS service";
            return false;
        }
    }
    if (!bus.registerObject(kObjectPath, this)) {
        qCWarning(logApp) << "could not register MPRIS object";
        return false;
    }
    m_registered = true;
    qCInfo(logApp) << "MPRIS registered as" << kServiceName;
    return true;
}

QString MprisPlayer::playbackStatus() const
{
    if (!m_active)
        return QStringLiteral("Stopped");
    return m_paused ? QStringLiteral("Paused") : QStringLiteral("Playing");
}

QVariantMap MprisPlayer::metadata() const
{
    QVariantMap map;
    if (!m_active)
        return map;
    map.insert(QStringLiteral("mpris:trackid"),
               QVariant::fromValue(QDBusObjectPath(QStringLiteral("/ca/mikesdev/strmqt/track"))));
    if (m_durationMs > 0)
        map.insert(QStringLiteral("mpris:length"), qlonglong(m_durationMs) * 1000);
    if (!m_title.isEmpty())
        map.insert(QStringLiteral("xesam:title"), m_title);
    if (!m_artist.isEmpty())
        map.insert(QStringLiteral("xesam:artist"), QStringList{m_artist});
    return map;
}

void MprisPlayer::setPlaybackActive(bool active, bool paused)
{
    if (m_active == active && m_paused == paused)
        return;
    const bool metadataChanged = m_active != active;
    m_active = active;
    m_paused = paused;
    QVariantMap changed;
    changed.insert(QStringLiteral("PlaybackStatus"), playbackStatus());
    if (metadataChanged)
        changed.insert(QStringLiteral("Metadata"), metadata());
    emitPropertiesChanged(changed);
}

void MprisPlayer::setNowPlaying(const QString &title, const QString &artist, qint64 durationMs)
{
    m_title = title;
    m_artist = artist;
    m_durationMs = durationMs;
    QVariantMap changed;
    changed.insert(QStringLiteral("Metadata"), metadata());
    emitPropertiesChanged(changed);
}

void MprisPlayer::setPositionMs(qint64 positionMs)
{
    // Position is polled by clients, not signalled.
    m_positionMs = positionMs;
}

void MprisPlayer::notifySeeked(qint64 positionMs)
{
    m_positionMs = positionMs;
    if (!m_registered)
        return;
    QDBusMessage signal =
        QDBusMessage::createSignal(kObjectPath, kPlayerInterface, QStringLiteral("Seeked"));
    signal << qlonglong(positionMs) * 1000;
    QDBusConnection::sessionBus().send(signal);
}

void MprisPlayer::emitPropertiesChanged(const QVariantMap &changed)
{
    if (!m_registered || changed.isEmpty())
        return;
    QDBusMessage signal =
        QDBusMessage::createSignal(kObjectPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                   QStringLiteral("PropertiesChanged"));
    signal << kPlayerInterface << changed << QStringList();
    QDBusConnection::sessionBus().send(signal);
}

} // namespace strmqt

#include "MprisPlayer.moc"
