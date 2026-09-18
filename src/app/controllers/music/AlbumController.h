#pragma once

#include <QDateTime>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

#include "app/music/models/AlbumGridModel.h"
#include "app/music/models/TrackListModel.h"
#include "server/dto/music/MusicTypes.h"

namespace strmqt::music {

class MusicPlayback;
class MusicRepository;

// The album page's data (Crate spec §6.1), exposed to QML as AlbumCtl. Every
// display string is composed here from MusicRepository::albumSleeve, so the
// page only binds.
class AlbumController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString albumId READ albumId NOTIFY albumChanged)
    Q_PROPERTY(QString title READ title NOTIFY albumChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY albumChanged)
    Q_PROPERTY(QString artistId READ artistId NOTIFY albumChanged)
    Q_PROPERTY(QString kicker READ kicker NOTIFY albumChanged)
    Q_PROPERTY(QString coverUrl READ coverUrl NOTIFY albumChanged)
    Q_PROPERTY(QString formatBadge READ formatBadge NOTIFY albumChanged)
    Q_PROPERTY(bool isHiRes READ isHiRes NOTIFY albumChanged)
    Q_PROPERTY(int discCount READ discCount NOTIFY albumChanged)
    Q_PROPERTY(QString discBadge READ discBadge NOTIFY albumChanged)
    Q_PROPERTY(QString trackSummary READ trackSummary NOTIFY albumChanged)
    Q_PROPERTY(QVariantList discs READ discs NOTIFY albumChanged)
    Q_PROPERTY(bool showArtistColumn READ showArtistColumn NOTIFY albumChanged)
    Q_PROPERTY(QStringList trackIds READ trackIds NOTIFY albumChanged)
    Q_PROPERTY(QVariantList linerNotes READ linerNotes NOTIFY albumChanged)
    Q_PROPERTY(QVariantMap albumItem READ albumItem NOTIFY albumChanged)
    Q_PROPERTY(QString moreByTitle READ moreByTitle NOTIFY albumChanged)
    Q_PROPERTY(bool favourite READ favourite NOTIFY favouriteChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(strmqt::music::TrackListModel *tracks READ tracks CONSTANT)
    Q_PROPERTY(strmqt::music::AlbumGridModel *moreBy READ moreBy CONSTANT)

public:
    AlbumController(MusicRepository *repository, MusicPlayback *playback,
                    QObject *parent = nullptr);

    QString albumId() const { return m_album.id; }
    QString title() const { return m_album.title; }
    QString artist() const;
    QString artistId() const;
    QString kicker() const;
    QString coverUrl() const;
    QString formatBadge() const { return m_album.formatSummary; }
    bool isHiRes() const;
    int discCount() const { return static_cast<int>(m_discs.size()); }
    QString discBadge() const;
    QString trackSummary() const;
    QVariantList discs() const;
    bool showArtistColumn() const;
    QStringList trackIds() const;
    QVariantList linerNotes() const;
    QVariantMap albumItem() const;
    QString moreByTitle() const;
    bool favourite() const { return m_album.favourite; }
    bool loading() const { return m_loading; }
    QString error() const { return m_error; }
    TrackListModel *tracks() const { return m_tracks; }
    AlbumGridModel *moreBy() const { return m_moreBy; }

    Q_INVOKABLE void open(const QString &albumId, const QString &name);
    Q_INVOKABLE void retry();
    Q_INVOKABLE void play(int fromIndex);
    Q_INVOKABLE void shuffle();
    Q_INVOKABLE void radio();

    void resetSessionState();
    void setClockForTests(std::function<QDateTime()> clock) { m_clock = std::move(clock); }

public slots:
    // ItemActions::favoriteChanged. Only the open album's own id matters here;
    // the track rows are patched by MusicUserDataRelay.
    void noteFavourite(const QString &itemId, bool favourite);

signals:
    void albumChanged();
    void favouriteChanged();
    void stateChanged();

private:
    void load();
    void apply(const AlbumSleeve &sleeve);
    QDateTime now() const;
    static QString relativeAge(const QDateTime &then, const QDateTime &now);

    MusicRepository *m_repository = nullptr;
    MusicPlayback *m_playback = nullptr;
    TrackListModel *m_tracks = nullptr;
    AlbumGridModel *m_moreBy = nullptr;
    Album m_album;
    QList<Disc> m_discs;
    bool m_loaded = false;
    bool m_loading = false;
    QString m_error;
    quint64 m_generation = 0;
    std::function<QDateTime()> m_clock;
};

} // namespace strmqt::music
