#pragma once

#include "app/models/MediaItemModel.h"

#include <QObject>
#include <QString>

namespace strmqt {

namespace emby {
class EmbyClient;
}

// Music browsing (ARCHITECTURE.md): artists, albums, and an album's tracks.
//
// Separate from LibraryController rather than another scope on it, because
// music is not one grid with a filter. It is three related lists with different
// shapes — square art, a track table, an artist's discography — and the target
// library has 4,871 artists, 5,037 albums and 56,283 tracks, so every one of
// them pages.
class MusicController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(strmqt::MediaItemModel *albums READ albums CONSTANT)
    Q_PROPERTY(strmqt::MediaItemModel *artists READ artists CONSTANT)
    // Tracks of the album currently open, in disc/track order.
    Q_PROPERTY(strmqt::MediaItemModel *tracks READ tracks CONSTANT)
    // Albums of the artist currently open.
    Q_PROPERTY(strmqt::MediaItemModel *artistAlbums READ artistAlbums CONSTANT)
    // The artist's most-played tracks. Uses ArtistIds rather than
    // AlbumArtistIds on purpose: top tracks SHOULD include what someone
    // guested on, which is exactly what a discography must exclude.
    Q_PROPERTY(strmqt::MediaItemModel *artistTracks READ artistTracks CONSTANT)

    Q_PROPERTY(QString libraryId READ libraryId NOTIFY scopeChanged)
    Q_PROPERTY(QString albumId READ albumId NOTIFY albumChanged)
    Q_PROPERTY(QString albumName READ albumName NOTIFY albumChanged)
    Q_PROPERTY(QString artistId READ artistId NOTIFY artistChanged)
    Q_PROPERTY(QString artistName READ artistName NOTIFY artistChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorChanged)
    Q_PROPERTY(bool canLoadMoreAlbums READ canLoadMoreAlbums NOTIFY albumsChanged)
    Q_PROPERTY(bool canLoadMoreArtists READ canLoadMoreArtists NOTIFY artistsChanged)
    // "albumArtists" (the 2,394 an album is filed under) or "artists" (all
    // 3,789 who appear on anything). They are genuinely different lists and
    // which one a music app shows is a real choice.
    Q_PROPERTY(QString artistMode READ artistMode WRITE setArtistMode NOTIFY artistsChanged)

public:
    explicit MusicController(emby::EmbyClient *client, QObject *parent = nullptr);

    MediaItemModel *albums() const { return m_albums; }
    MediaItemModel *artists() const { return m_artists; }
    MediaItemModel *tracks() const { return m_tracks; }
    MediaItemModel *artistAlbums() const { return m_artistAlbums; }
    MediaItemModel *artistTracks() const { return m_artistTracks; }

    QString libraryId() const { return m_libraryId; }
    QString albumId() const { return m_albumId; }
    QString albumName() const { return m_albumName; }
    QString artistId() const { return m_artistId; }
    QString artistName() const { return m_artistName; }
    bool loading() const { return m_loading; }
    QString errorMessage() const { return m_error; }
    bool canLoadMoreAlbums() const;
    bool canLoadMoreArtists() const;
    QString artistMode() const { return m_artistMode; }
    void setArtistMode(const QString &mode);

    // Scope every list to one music library. An empty id means "every music
    // library the server has", which is what search and Home want.
    Q_INVOKABLE void setLibrary(const QString &libraryId);

    Q_INVOKABLE void loadAlbums();
    Q_INVOKABLE void loadMoreAlbums();
    Q_INVOKABLE void loadArtists();
    Q_INVOKABLE void loadMoreArtists();

    // Tracks come back in the server's order, which for an album is disc then
    // track. Sorting by name here would scramble every record ever made.
    Q_INVOKABLE void openAlbum(const QString &albumId, const QString &name);
    Q_INVOKABLE void openArtist(const QString &artistId, const QString &name);

signals:
    void scopeChanged();
    void albumChanged();
    void artistChanged();
    void albumsChanged();
    void artistsChanged();
    void loadingChanged();
    void errorChanged();

private:
    void fetchAlbums(int startIndex);
    void fetchArtists(int startIndex);
    void setLoading(bool loading);
    void setError(const QString &message);

    emby::EmbyClient *m_client;
    MediaItemModel *m_albums;
    MediaItemModel *m_artists;
    MediaItemModel *m_tracks;
    MediaItemModel *m_artistAlbums;
    MediaItemModel *m_artistTracks;

    QString m_libraryId;
    QString m_albumId;
    QString m_albumName;
    QString m_artistId;
    QString m_artistName;
    QString m_error;
    QString m_artistMode = QStringLiteral("albumArtists");
    bool m_loading = false;
    int m_generation = 0;
    int m_albumGeneration = 0;
    int m_artistGeneration = 0;
};

} // namespace strmqt
