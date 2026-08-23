#pragma once

#include "app/models/MediaItemModel.h"
#include "server/dto/ItemsQuery.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

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
    // The Songs TAB: every track in the library, paged and independently
    // sorted. Deliberately NOT `tracks` — that model is the open album's, and
    // the two would fight the moment the album page and the songs tab were
    // both live. Reusing it is what produced the playAlbum() side channel a
    // previous phase removed; this list gets its own model and its own
    // generation counter for the same reason.
    Q_PROPERTY(strmqt::MediaItemModel *songs READ songs CONSTANT)
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
    // False under SortBy=Random, whatever the totals say: Emby reshuffles per
    // request and has no seed, so a second page is a second shuffle. See
    // isRandomSort() in the .cpp for the whole reason.
    Q_PROPERTY(bool canLoadMoreAlbums READ canLoadMoreAlbums NOTIFY albumsChanged)
    Q_PROPERTY(bool canLoadMoreArtists READ canLoadMoreArtists NOTIFY artistsChanged)
    Q_PROPERTY(bool canLoadMoreSongs READ canLoadMoreSongs NOTIFY songsChanged)
    // "albumArtists" (the 2,394 an album is filed under) or "artists" (all
    // 3,789 who appear on anything). They are genuinely different lists and
    // which one a music app shows is a real choice.
    Q_PROPERTY(QString artistMode READ artistMode WRITE setArtistMode NOTIFY artistsChanged)

    // ── Sort and filter ───────────────────────────────────────────────────────
    // The same surface LibraryController publishes, deliberately: FilterBar is
    // one component pointed at either controller (ARCHITECTURE.md §4), and a
    // second dialect of "how do I ask for a sort" is exactly what would make it
    // two bars again.
    //
    // Two rules the film/TV controller does not need:
    //
    //  1. **Sort is per tab, filters are shared.** "Track number" is meaningless
    //     for an artist and "Release year" for a song, so each tab keeps its own
    //     field and direction and `availableSorts` answers for the tab on
    //     screen. A genre or a letter, by contrast, is a statement about the
    //     music and survives switching how you look at it.
    //  2. **A query change refetches the visible tab and invalidates the other
    //     two.** Refetching all three would fire three requests for a filter the
    //     user can only see the results of one of; leaving them alone would show
    //     yesterday's albums under today's genre.
    //
    // "albums" | "artists" | "songs".
    Q_PROPERTY(QString tab READ tab WRITE setTab NOTIFY tabChanged)
    // Emby's own sort key for the current tab, e.g. "SortName" | "DateCreated".
    Q_PROPERTY(QString sortBy READ sortBy NOTIFY queryChanged)
    Q_PROPERTY(bool sortDescending READ sortDescending NOTIFY queryChanged)
    // The sort keys that make sense for the tab on screen. One QVariantMap per
    // option, {key, label} — the shape LibraryController uses.
    Q_PROPERTY(QVariantList availableSorts READ availableSorts NOTIFY tabChanged)
    // Single letter from the alphabet bar, or empty for no constraint. Matched
    // against the SORT name, so "The Beatles" is under B.
    Q_PROPERTY(QString nameStartsWith READ nameStartsWith NOTIFY queryChanged)
    // MusicGenre ids. A music library has hundreds of genres (289 measured), so
    // this is a multi-select, never a row of chips.
    Q_PROPERTY(QStringList genreIds READ genreIds NOTIFY queryChanged)
    Q_PROPERTY(QStringList yearFilters READ yearFilters NOTIFY queryChanged)
    Q_PROPERTY(bool favoritesOnly READ favoritesOnly NOTIFY queryChanged)
    // True when anything narrows the view, so the UI can offer one "clear".
    Q_PROPERTY(bool filtered READ filtered NOTIFY queryChanged)
    // Genres available in this library, {key, label}, from /MusicGenres.
    Q_PROPERTY(QVariantList genreOptions READ genreOptions NOTIFY genresChanged)
    // The genre walk stopped on an error and the list is incomplete. Exists so a
    // filter control with nothing in it can say why rather than sitting greyed
    // out for a reason only the log knows. Cleared by the next loadGenres(),
    // which resumes the walk from the page that failed.
    Q_PROPERTY(bool genresFailed READ genresFailed NOTIFY genresChanged)

public:
    explicit MusicController(emby::EmbyClient *client, QObject *parent = nullptr);

    MediaItemModel *albums() const { return m_albums; }
    MediaItemModel *artists() const { return m_artists; }
    MediaItemModel *tracks() const { return m_tracks; }
    MediaItemModel *songs() const { return m_songs; }
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
    bool canLoadMoreSongs() const;
    QString artistMode() const { return m_artistMode; }
    void setArtistMode(const QString &mode);

    QString tab() const { return m_tab; }
    void setTab(const QString &tab);
    QString sortBy() const;
    bool sortDescending() const;
    QVariantList availableSorts() const;
    QString nameStartsWith() const { return m_nameStartsWith; }
    QStringList genreIds() const { return m_genreIds; }
    QStringList yearFilters() const { return m_yearFilters; }
    bool favoritesOnly() const { return m_favoritesOnly; }
    bool filtered() const;
    QVariantList genreOptions() const { return m_genreOptions; }
    bool genresFailed() const { return m_genresFailed; }

    // Each of these re-runs the visible tab from page 0 and invalidates the
    // other two, and each no-ops when the value is unchanged so a menu that
    // re-emits on open does not refetch — the same contract LibraryController's
    // setters have.
    Q_INVOKABLE void setSort(const QString &key, bool descending);
    Q_INVOKABLE void setNameStartsWith(const QString &letter);
    Q_INVOKABLE void setGenreIds(const QStringList &genreIds);
    Q_INVOKABLE void setYearFilters(const QStringList &years);
    Q_INVOKABLE void setFavoritesOnly(bool favoritesOnly);
    Q_INVOKABLE void clearFilters();

    // /MusicGenres for the current library, paged on the array's own size.
    //
    // Idempotent AND retryable: a call while the walk is running or once it has
    // reached the end of the list does nothing, but a walk that stopped on an
    // error resumes from the page that failed. A partial genre list is not a
    // finished one, so callers may ask again whenever the list is about to be
    // looked at.
    Q_INVOKABLE void loadGenres();

    // Scope every list to one music library. An empty id means "every music
    // library the server has", which is what search and Home want.
    //
    // Filters do not survive it: a genre id is a ParentId-scoped MusicGenre row,
    // so carrying it into another library queries an id that does not exist
    // there. See setLibrary() for why the other three axes go with it. The
    // per-tab sorts, which are library-neutral, do survive.
    Q_INVOKABLE void setLibrary(const QString &libraryId);

    Q_INVOKABLE void loadAlbums();
    Q_INVOKABLE void loadMoreAlbums();
    Q_INVOKABLE void loadArtists();
    Q_INVOKABLE void loadMoreArtists();
    Q_INVOKABLE void loadSongs();
    Q_INVOKABLE void loadMoreSongs();

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
    void songsChanged();
    // The query moved (sort, letter, genre, year, favourites): a list is being
    // refilled. Named for LibraryController's signal so FilterBar connects to
    // one name whichever controller it is pointed at.
    void queryChanged();
    // The tab moved: the set of sensible sort keys, and the remembered sort,
    // may differ.
    void tabChanged();
    void genresChanged();
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
    void fetchSongs(int startIndex);
    void fetchGenrePage(int startIndex, int generation);
    void setLoading(bool loading);
    void setError(const QString &message);
    // Applies the shared filter axes (letter, genres, years, favourites) to a
    // query. One place, so the three tabs cannot drift apart on what "filtered"
    // means.
    void applyFilters(ItemsQuery &query) const;
    // Refetch the tab on screen; clear the other two so revisiting one asks the
    // server again rather than showing the previous query's results.
    void applyQueryChange();
    // Fetch the current tab's first page if its model is empty.
    void ensureCurrentTab();
    // 0 albums · 1 artists · 2 songs.
    int currentTabIndex() const;

    emby::EmbyClient *m_client;
    ItemActions *m_actions = nullptr;
    MediaItemModel *m_albums;
    MediaItemModel *m_artists;
    MediaItemModel *m_tracks;
    MediaItemModel *m_songs;
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

    QString m_tab = QStringLiteral("albums");
    // Per tab, in tab order: albums, artists, songs. Seeded the way
    // FilterBar.defaultDescendingFor() would: a name sort reads ascending.
    QString m_albumSortBy = QStringLiteral("SortName");
    bool m_albumSortDescending = false;
    QString m_artistSortBy = QStringLiteral("SortName");
    bool m_artistSortDescending = false;
    QString m_songSortBy = QStringLiteral("SortName");
    bool m_songSortDescending = false;

    QString m_nameStartsWith;
    QStringList m_genreIds;
    QStringList m_yearFilters;
    bool m_favoritesOnly = false;
    QVariantList m_genreOptions;
    // The genre walk's own state, so a walk that broke in the middle can be told
    // apart from one that finished — the difference between "289 genres" and
    // "the 200 that arrived before page 1 timed out".
    int m_genreNextIndex = 0;
    bool m_genresComplete = false;
    bool m_genreWalkActive = false;
    bool m_genresFailed = false;

    // Has anything been asked of this controller yet? A sort or a filter set
    // before the first list was requested is a preference, not a query, and
    // must not fire a request of its own — the same guard LibraryController's
    // hasQuery() is. Empty libraryId cannot serve: it legitimately means "every
    // music library".
    bool m_started = false;
    bool m_loading = false;
    int m_generation = 0;
    int m_albumGeneration = 0;
    int m_artistGeneration = 0;
    int m_songGeneration = 0;
    int m_genreGeneration = 0;
    int m_playGeneration = 0;
};

} // namespace strmqt
