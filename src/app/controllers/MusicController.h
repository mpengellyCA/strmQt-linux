#pragma once

#include "app/models/MediaItemModel.h"

#include <QObject>
#include <QString>

namespace strmqt {

class ItemActions;

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

    // Play a whole record from a card, without opening it.
    //
    // A real verb rather than the side channel this used to be: MusicPage
    // called openAlbum() and watched the shared `tracks` model fill, so playing
    // an album *navigated controller state* and would have fought the album
    // page the moment both were live. This fetches into a scratch model of its
    // own, with its own generation counter so a second ▸ cannot strand the
    // first, and hands ItemActions the ordered items.
    //
    // Not Actions.playAll(albumId, "music"): that verb sorts, and for music it
    // sorts by SortBy=IndexNumber,SortName (ItemActions' playAllSortFor). That
    // is right for a single-disc album and wrong for a box set — track 1 of
    // disc 1 and track 1 of disc 2 share IndexNumber == 1, so the discs come
    // back interleaved. An album's own children, unsorted, are already in the
    // server's disc-then-track order, so the fix is to not sort at all rather
    // than to sort better. (ItemActions cannot simply switch to
    // ParentIndexNumber,IndexNumber: the comment there records why — most of
    // the library has no disc number, and a null disc sorts ahead of disc 1.)
    Q_INVOKABLE void playAlbum(const QString &albumId);

    // The queue verbs live in ItemActions (ARCHITECTURE.md rule 3), so this
    // controller has to be able to reach them.
    void setActions(ItemActions *actions);

signals:
    void scopeChanged();
    void albumChanged();
    void artistChanged();
    void albumsChanged();
    void artistsChanged();
    void loadingChanged();
    void errorChanged();
    // A one-shot verb failed. Separate from errorMessage, which is the state of
    // the *lists* — MusicPage renders that as "Couldn't load this music
    // library" or, once a page is on screen, as a paging banner with a Retry
    // that calls loadMoreAlbums(). A failed ▸ is neither of those: it has
    // nothing to retry and nothing to keep showing, so it goes out as a toast,
    // the way every other one-shot verb reports failure (ItemActions and
    // PlaylistController both name the signal this).
    void actionFailed(const QString &message);

private:
    void fetchAlbums(int startIndex);
    void fetchArtists(int startIndex);
    void setLoading(bool loading);
    void setError(const QString &message);

    emby::EmbyClient *m_client;
    ItemActions *m_actions = nullptr;
    MediaItemModel *m_albums;
    MediaItemModel *m_artists;
    MediaItemModel *m_tracks;
    MediaItemModel *m_artistAlbums;
    MediaItemModel *m_artistTracks;
    // Never published to QML. playAlbum() must not touch `tracks`: that model
    // is what the album page is reading.
    MediaItemModel *m_playScratch;

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
    int m_playGeneration = 0;
};

} // namespace strmqt
