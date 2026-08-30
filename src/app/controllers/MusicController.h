#pragma once

#include "app/models/MediaItemModel.h"
#include "server/dto/ItemsQuery.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <functional>

namespace strmqt {

class ItemActions;

namespace emby {
class EmbyClient;
}

// Music browsing (ARCHITECTURE.md): library albums, artists, songs and audio
// playlists, plus album/artist detail models.
//
// Separate from LibraryController rather than another scope on it, because
// music is not one grid with a filter. Its browse lanes have different shapes
// — square art and a track table — and the target library has 4,871 artists,
// 5,037 albums and 56,283 tracks, so every one of them pages.
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
    // The Playlists TAB: this music library's playlists, and only those.
    //
    // NOT PlaylistController's list, which is deliberately every playlist the
    // user has: that one feeds the "add to…" picker, and a picker raised from a
    // film has to keep offering film playlists. Narrowing the shared model to
    // audio for the sake of one tab would break the picker everywhere else, so
    // the tab gets its own model and its own generation counter — the same
    // reason `songs` is not `tracks`.
    Q_PROPERTY(strmqt::MediaItemModel *playlists READ playlists CONSTANT)
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
    // Browse requests are independent lanes. A hidden slow tab must not keep
    // the visible tab's focus restoration or empty/loading state pending.
    Q_PROPERTY(bool albumsLoading READ albumsLoading NOTIFY browseStatusChanged)
    Q_PROPERTY(bool artistsLoading READ artistsLoading NOTIFY browseStatusChanged)
    Q_PROPERTY(bool songsLoading READ songsLoading NOTIFY browseStatusChanged)
    Q_PROPERTY(bool playlistsLoading READ playlistsLoading NOTIFY browseStatusChanged)
    // Errors belong to the same independent lanes as their models and loading
    // markers. A hidden request must neither poison nor clear the visible tab.
    Q_PROPERTY(QString albumsErrorMessage READ albumsErrorMessage NOTIFY browseStatusChanged)
    Q_PROPERTY(QString artistsErrorMessage READ artistsErrorMessage NOTIFY browseStatusChanged)
    Q_PROPERTY(QString songsErrorMessage READ songsErrorMessage NOTIFY browseStatusChanged)
    Q_PROPERTY(QString playlistsErrorMessage READ playlistsErrorMessage NOTIFY browseStatusChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorChanged)
    // Album/artist pages share one retargetable detail lane. Keep its owner and
    // status separate from the library lists so unrelated replies cannot clear
    // a detail failure or keep a detail skeleton alive.
    Q_PROPERTY(QString detailKind READ detailKind NOTIFY detailStatusChanged)
    Q_PROPERTY(QString detailId READ detailId NOTIFY detailStatusChanged)
    Q_PROPERTY(bool detailLoading READ detailLoading NOTIFY detailStatusChanged)
    Q_PROPERTY(QString detailErrorMessage READ detailErrorMessage NOTIFY detailStatusChanged)
    // Artist pages have two independent reply lanes. Each virtualized owner
    // waits on the lane that populates its model; detailLoading remains their
    // aggregate for callers interested in the whole artist request.
    Q_PROPERTY(bool artistAlbumsLoading READ artistAlbumsLoading NOTIFY detailStatusChanged)
    Q_PROPERTY(bool artistTracksLoading READ artistTracksLoading NOTIFY detailStatusChanged)
    // False under SortBy=Random, whatever the totals say: Emby reshuffles per
    // request and has no seed, so a second page is a second shuffle. See
    // isRandomSort() in the .cpp for the whole reason.
    Q_PROPERTY(bool canLoadMoreAlbums READ canLoadMoreAlbums NOTIFY albumsChanged)
    Q_PROPERTY(bool canLoadMoreArtists READ canLoadMoreArtists NOTIFY artistsChanged)
    Q_PROPERTY(bool canLoadMoreSongs READ canLoadMoreSongs NOTIFY songsChanged)
    Q_PROPERTY(bool canLoadMorePlaylists READ canLoadMorePlaylists NOTIFY playlistsChanged)
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
    //     three.** Refetching all four would fire four requests for a filter the
    //     user can only see the results of one of; leaving them usable would
    //     show yesterday's albums under today's genre.
    //
    // "albums" | "artists" | "songs" | "playlists".
    Q_PROPERTY(QString tab READ tab WRITE setTab NOTIFY tabChanged)
    // Emby's own sort key for the current tab, e.g. "SortName" | "DateCreated".
    Q_PROPERTY(QString sortBy READ sortBy NOTIFY queryChanged)
    Q_PROPERTY(bool sortDescending READ sortDescending NOTIFY queryChanged)
    // The sort keys that make sense for the tab on screen. One QVariantMap per
    // option, {key, label} — the shape LibraryController uses.
    Q_PROPERTY(QVariantList availableSorts READ availableSorts NOTIFY tabChanged)
    // Single letter from the alphabet bar, or empty for no constraint. Matched
    // against the SORT name, so "The Beatles" is under B. Sent as an indexable
    // [NameStartsWithOrGreater, NameLessThan) range — see applyFilters.
    Q_PROPERTY(QString nameStartsWith READ nameStartsWith NOTIFY queryChanged)
    // MusicGenre ids. A music library has hundreds of genres (289 measured), so
    // this is a multi-select, never a row of chips.
    Q_PROPERTY(QStringList genreIds READ genreIds NOTIFY queryChanged)
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
    MediaItemModel *playlists() const { return m_playlists; }
    MediaItemModel *artistAlbums() const { return m_artistAlbums; }
    MediaItemModel *artistTracks() const { return m_artistTracks; }

    QString libraryId() const { return m_libraryId; }
    QString albumId() const { return m_albumId; }
    QString albumName() const { return m_albumName; }
    QString artistId() const { return m_artistId; }
    QString artistName() const { return m_artistName; }
    bool loading() const { return m_loading; }
    bool albumsLoading() const { return m_albumInFlight != 0; }
    bool artistsLoading() const { return m_artistInFlight != 0; }
    bool songsLoading() const { return m_songInFlight != 0; }
    bool playlistsLoading() const { return m_playlistInFlight != 0; }
    QString albumsErrorMessage() const { return m_albumError; }
    QString artistsErrorMessage() const { return m_artistError; }
    QString songsErrorMessage() const { return m_songError; }
    QString playlistsErrorMessage() const { return m_playlistError; }
    // Compatibility surface for controller callers: the active tab's error.
    // MusicPage binds the explicit lane properties so a tab switch cannot
    // accidentally display another lane's terminal state.
    QString errorMessage() const;
    QString detailKind() const { return m_detailKind; }
    QString detailId() const { return m_detailId; }
    bool detailLoading() const
    {
        return m_detailKind == QLatin1String("artist")
                   ? (m_artistAlbumsInFlight != 0 || m_artistTracksInFlight != 0)
                   : m_detailInFlight != 0;
    }
    QString detailErrorMessage() const { return m_detailError; }
    bool artistAlbumsLoading() const { return m_artistAlbumsInFlight != 0; }
    bool artistTracksLoading() const { return m_artistTracksInFlight != 0; }
    bool canLoadMoreAlbums() const;
    bool canLoadMoreArtists() const;
    bool canLoadMoreSongs() const;
    bool canLoadMorePlaylists() const;
    QString artistMode() const { return m_artistMode; }
    void setArtistMode(const QString &mode);

    QString tab() const { return m_tab; }
    void setTab(const QString &tab);
    QString sortBy() const;
    bool sortDescending() const;
    QVariantList availableSorts() const;
    QString nameStartsWith() const { return m_nameStartsWith; }
    QStringList genreIds() const { return m_genreIds; }
    bool favoritesOnly() const { return m_favoritesOnly; }
    bool filtered() const;
    QVariantList genreOptions() const { return m_genreOptions; }
    bool genresFailed() const { return m_genresFailed; }

    void resetSessionState();

    // Each shared filter re-runs the visible tab from page 0 and marks the
    // hidden lanes dirty, and each no-ops when the value is unchanged so a menu
    // that re-emits on open does not refetch — the same contract
    // LibraryController's setters have.
    Q_INVOKABLE void setSort(const QString &key, bool descending);
    Q_INVOKABLE void setNameStartsWith(const QString &letter);
    Q_INVOKABLE void setGenreIds(const QStringList &genreIds);
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
    // there. See setLibrary() for why the other narrowing axes go with it. The
    // per-tab sorts, which are library-neutral, do survive.
    Q_INVOKABLE void setLibrary(const QString &libraryId);

    Q_INVOKABLE void loadAlbums();
    Q_INVOKABLE void loadMoreAlbums();
    Q_INVOKABLE void loadArtists();
    Q_INVOKABLE void loadMoreArtists();
    Q_INVOKABLE void loadSongs();
    Q_INVOKABLE void loadMoreSongs();

    // ── The Playlists tab ─────────────────────────────────────────────────────
    // Emby 4.9.5.0 publishes NO media type on a playlist. Measured, three ways:
    // it is absent from the /Items list payload, absent from the item detail
    // payload, and `Fields=MediaType` does not add it. The `MediaTypes` query
    // parameter is worse than absent — asked alongside IncludeItemTypes=Playlist
    // it DISCARDS the type constraint and answers with the whole library
    // (204,528 rows of albums and tracks), and Audio and Video return the same
    // thing.
    //
    // What does work, in ONE request and with no per-playlist probe: ParentId.
    // Emby resolves a library id to that library's content type and matches the
    // playlist's own media type against it. Proven with a purpose-built triple —
    // an audio playlist, a video playlist and an untyped one holding a track —
    // all three stored under /config/data/userplaylists and physically outside
    // every library folder: the music library's id returned the two audio ones,
    // the movie library's id returned the video one.
    //
    // So there is no N+1 walk and no cache to keep, and the whole audio scoping
    // is the ParentId. Without one there is nothing separating a video playlist
    // from an audio one, so an unscoped controller fetches nothing here rather
    // than filling a music tab with the user's film lists.
    Q_INVOKABLE void loadPlaylists();
    Q_INVOKABLE void loadMorePlaylists();
    // The set of playlists changed somewhere else in the app — PlaylistController
    // made, renamed or deleted one. Empty this list so it is asked for again,
    // and refetch now if it is the tab on screen. Same shape as a query change,
    // for the same reason: a playlist the user just created from a track has to
    // appear in the tab that is supposed to list it.
    Q_INVOKABLE void invalidatePlaylists();

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

    // ▸ Shuffle over the library AS NARROWED. The query is the Songs tab's own
    // shape — recursive Audio under the library — with the shared filter axes
    // applied (letter, genres, favourites); ItemActions owns the Random sort
    // and the cap. A shuffle that ignores the genre you filtered to is not
    // the shuffle you asked for.
    //
    // The filters' "not before anything loads" rule does not apply here:
    // shuffle is a one-shot verb, not a query, so asking before the first list
    // fetch still plays — with whatever filters are set.
    Q_INVOKABLE void shuffleFiltered();

    // An album id expanded into the ids of its tracks, for "Add to playlist" on
    // an album card (MUSIC.md §3's gap). A playlist holds playable items, not
    // containers, and only the server can say which tracks an album has — so
    // the grid's context menu asks for them and files them when they arrive.
    //
    // The SAME machinery playAlbum() uses, not a second copy of it: one
    // private expandAlbum() runs the one query (an album's children, unsorted,
    // in the server's disc-then-track order) and hands the result to whichever
    // verb asked. They keep separate generation counters, because they are two
    // verbs and a ▸ pressed while a picker request is in flight must not cancel
    // it.
    //
    // `subject` is echoed back untouched: it is what the picker's create row
    // offers to name a new playlist after, and only the caller knows it.
    Q_INVOKABLE void collectAlbumTracks(const QString &albumId, const QString &subject);

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
    void playlistsChanged();
    // The query moved (sort, letter, genre, favourites): a list is being
    // refilled. Named for LibraryController's signal so FilterBar connects to
    // one name whichever controller it is pointed at.
    void queryChanged();
    // The tab moved: the set of sensible sort keys, and the remembered sort,
    // may differ.
    void tabChanged();
    void genresChanged();
    void loadingChanged();
    void browseStatusChanged();
    void errorChanged();
    void detailStatusChanged();
    // A one-shot verb failed. Separate from the per-lane browse errors, which
    // MusicPage renders as "Couldn't load this music
    // library" or, once a page is on screen, as a paging banner with a Retry
    // that calls loadMoreAlbums(). A failed ▸ is neither of those: it has
    // nothing to retry and nothing to keep showing, so it goes out as a toast,
    // the way every other one-shot verb reports failure (ItemActions and
    // PlaylistController both name the signal this).
    void actionFailed(const QString &message);
    // collectAlbumTracks() came back. Empty `trackIds` never reaches here — an
    // album with no children is an actionFailed(), not a picker over nothing.
    void albumTracksCollected(const QString &subject, const QStringList &trackIds);

private:
    void fetchAlbums(int startIndex);
    void fetchArtists(int startIndex);
    void fetchSongs(int startIndex);
    void fetchPlaylists(int startIndex);
    void fetchGenrePage(int startIndex, int generation);
    // The one album-children query, shared by playAlbum() and
    // collectAlbumTracks(). `stillCurrent` is the caller's own generation
    // guard, asked when the reply lands; `onItems` receives the album's rows in
    // the server's order; `failure` is the one-line toast a failed reply gets.
    void expandAlbum(const QString &albumId, std::function<bool()> stillCurrent,
                     std::function<void(const QList<MediaItem> &)> onItems,
                     const QString &failure);
    void setLoading(bool loading);
    static void setBrowseError(QString &laneError, const QString &message);
    void clearBrowseErrors();
    int beginDetail(const QString &kind, const QString &id);
    void finishDetail(int generation, const QString &error);
    void finishArtistAlbums(int generation, const QString &error);
    void finishArtistTracks(int generation);
    // Applies the shared filter axes (letter, genres, favourites) to a
    // query. One place, so the four tabs cannot drift apart on what "filtered"
    // means.
    void applyFilters(ItemsQuery &query) const;
    // Refetch the tab on screen; hidden lanes keep their model storage but are
    // marked dirty so revisiting one clears it before asking the server again.
    // MusicPage has no hidden delegates, so those retained rows are never drawn.
    void applyQueryChange();
    // Retire one lane and invalidate its error/paging state. `clearModel` is
    // true for the active lane and false for a hidden lane whose view does not
    // exist. `dirty` means it must clear/refetch before it can be used again.
    void invalidateBrowseLane(int lane, bool clearModel, bool dirty);
    bool browseLaneDirty(int lane) const;
    void prepareBrowseLaneForLoad(int lane);
    // Fetch the current tab's first page if its model is empty.
    void ensureCurrentTab();
    // 0 albums · 1 artists · 2 songs · 3 playlists.
    int currentTabIndex() const;
    // Retire whatever is in flight for one list: bump its generation so the
    // reply is dropped, and clear the in-flight marker in the same breath,
    // because that reply is now the thing that will never clear it. Returns the
    // new generation, which is what a fetch about to be issued carries.
    static int retire(int &generation, int &inFlight);
    // `loading` is the OR of the library-list markers. The detail lane has its
    // own owner-scoped state. Publishes one coherent snapshot of lane loading
    // and errors after every list start, retire and reply.
    void updateLoading();

    emby::EmbyClient *m_client;
    ItemActions *m_actions = nullptr;
    MediaItemModel *m_albums;
    MediaItemModel *m_artists;
    MediaItemModel *m_tracks;
    MediaItemModel *m_songs;
    MediaItemModel *m_playlists;
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
    QString m_albumError;
    QString m_artistError;
    QString m_songError;
    QString m_playlistError;
    QString m_detailKind;
    QString m_detailId;
    QString m_detailError;
    QString m_artistMode = QStringLiteral("albumArtists");

    QString m_tab = QStringLiteral("albums");
    // Per tab, in tab order: albums, artists, songs, playlists. Seeded the way
    // FilterBar.defaultDescendingFor() would: a name sort reads ascending.
    QString m_albumSortBy = QStringLiteral("SortName");
    bool m_albumSortDescending = false;
    QString m_artistSortBy = QStringLiteral("SortName");
    bool m_artistSortDescending = false;
    QString m_songSortBy = QStringLiteral("SortName");
    bool m_songSortDescending = false;
    QString m_playlistSortBy = QStringLiteral("SortName");
    bool m_playlistSortDescending = false;

    QString m_nameStartsWith;
    QStringList m_genreIds;
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
    int m_playlistGeneration = 0;
    int m_genreGeneration = 0;
    int m_playGeneration = 0;
    int m_collectGeneration = 0;

    // ── What `loading` actually is ────────────────────────────────────────────
    // The generation of the request in flight for each list, or 0 for none —
    // and `loading` is derived from the set of them rather than being a bool
    // that every fetch raises and every reply is trusted to lower.
    //
    // A single bool cannot survive this controller's shape, because a request
    // can be retired without ever being answered: a generation bump makes the
    // reply return above setLoading(false), and if no replacement request is
    // issued nothing lowers the flag again. Review found the live instance —
    // invalidatePlaylists() (a playlist created from a track) bumps the playlist
    // generation and only refetches when the Playlists tab is the one on screen,
    // so a playlist page in flight from any other tab left `loading` stuck true
    // for the rest of the session: every loadMore* early-returns on it and every
    // ensure*() in MusicPage is guarded on it, so scroll paging died everywhere
    // and only a sort or filter change brought it back. fetchPlaylists() has the
    // same shape at its no-library early return.
    //
    // setLibrary() papered over its own instance of this with an explicit
    // setLoading(false), which is right only because it retires ALL of them.
    // Deriving the flag makes that unnecessary and makes the next retire path
    // correct by construction rather than by remembering.
    int m_albumInFlight = 0;
    int m_artistInFlight = 0;
    int m_songInFlight = 0;
    int m_playlistInFlight = 0;
    // Shared filters invalidate all four queries, but only MusicPage's active
    // lane owns delegates. Hidden models stay allocated until activation and
    // are never exposed as current while these flags are true.
    bool m_albumDirty = false;
    bool m_artistDirty = false;
    bool m_songDirty = false;
    bool m_playlistDirty = false;
    // openAlbum() and openArtist() share m_generation and an explicit kind/id
    // owner. Artist discography and top tracks settle independently because
    // they populate different focus-restoration owners.
    int m_detailInFlight = 0;
    int m_artistAlbumsInFlight = 0;
    int m_artistTracksInFlight = 0;
};

} // namespace strmqt
