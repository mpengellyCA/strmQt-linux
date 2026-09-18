#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include "app/music/models/AlbumGridModel.h"
#include "app/music/models/ArtistGridModel.h"
#include "app/music/models/TrackListModel.h"
#include "server/dto/music/MusicTypes.h"

namespace strmqt::music {

class MusicPlayback;
class MusicRepository;

// The artist page's data (Crate spec §6.2), exposed to QML as ArtistCtl. Built
// from MusicRepository::artistProfile: one model per filed-release tab, the
// top tracks with their captions, and similar artists.
class ArtistController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString artistId READ artistId NOTIFY artistChanged)
    Q_PROPERTY(QString libraryId READ libraryId NOTIFY artistChanged)
    Q_PROPERTY(QString name READ name NOTIFY artistChanged)
    Q_PROPERTY(QString kicker READ kicker NOTIFY artistChanged)
    Q_PROPERTY(QString coverUrl READ coverUrl NOTIFY artistChanged)
    Q_PROPERTY(QString backdropUrl READ backdropUrl NOTIFY artistChanged)
    Q_PROPERTY(QVariantList tabs READ tabs NOTIFY artistChanged)
    Q_PROPERTY(QStringList topTrackCaptions READ topTrackCaptions NOTIFY artistChanged)
    Q_PROPERTY(QVariantMap artistItem READ artistItem NOTIFY artistChanged)
    Q_PROPERTY(bool favourite READ favourite NOTIFY favouriteChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(strmqt::music::AlbumGridModel *albums READ albums CONSTANT)
    Q_PROPERTY(strmqt::music::AlbumGridModel *epsAndSingles READ epsAndSingles CONSTANT)
    Q_PROPERTY(strmqt::music::AlbumGridModel *appearsOn READ appearsOn CONSTANT)
    Q_PROPERTY(strmqt::music::TrackListModel *topTracks READ topTracks CONSTANT)
    Q_PROPERTY(strmqt::music::ArtistGridModel *similar READ similar CONSTANT)

public:
    ArtistController(MusicRepository *repository, MusicPlayback *playback,
                     QObject *parent = nullptr);

    QString artistId() const { return m_artist.id; }
    QString libraryId() const { return m_libraryId; }
    QString name() const { return m_artist.name; }
    QString kicker() const;
    QString coverUrl() const;
    QString backdropUrl() const;
    QVariantList tabs() const;
    QStringList topTrackCaptions() const;
    QVariantMap artistItem() const;
    bool favourite() const { return m_artist.favourite; }
    bool loading() const { return m_loading; }
    QString error() const { return m_error; }
    AlbumGridModel *albums() const { return m_albums; }
    AlbumGridModel *epsAndSingles() const { return m_epsAndSingles; }
    AlbumGridModel *appearsOn() const { return m_appearsOn; }
    TrackListModel *topTracks() const { return m_topTracks; }
    ArtistGridModel *similar() const { return m_similar; }

    Q_INVOKABLE void open(const QString &artistId, const QString &name,
                          const QString &libraryId = QString());
    Q_INVOKABLE void retry();
    Q_INVOKABLE void shuffle();
    Q_INVOKABLE void radio();
    Q_INVOKABLE void playTopTrack(int row);

    void resetSessionState();

public slots:
    void noteFavourite(const QString &itemId, bool favourite);

signals:
    void artistChanged();
    void favouriteChanged();
    void stateChanged();

private:
    void load();
    void apply(const ArtistProfile &profile);
    void clearModels();

    MusicRepository *m_repository = nullptr;
    MusicPlayback *m_playback = nullptr;
    AlbumGridModel *m_albums = nullptr;
    AlbumGridModel *m_epsAndSingles = nullptr;
    AlbumGridModel *m_appearsOn = nullptr;
    TrackListModel *m_topTracks = nullptr;
    ArtistGridModel *m_similar = nullptr;
    Artist m_artist;
    QString m_libraryId;
    bool m_loaded = false;
    bool m_loading = false;
    QString m_error;
    quint64 m_generation = 0;
};

} // namespace strmqt::music
