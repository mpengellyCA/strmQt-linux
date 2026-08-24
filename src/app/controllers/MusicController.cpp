#include "MusicController.h"

#include "app/ItemActions.h"
#include "core/Log.h"
#include "server/dto/ItemsQuery.h"
#include "server/emby/EmbyClient.h"

#include <QVariantMap>

#include <utility>

namespace strmqt {

namespace {
constexpr int kPageSize = 100;
// /MusicGenres pages on the ARRAY's own size, not TotalRecordCount: the sibling
// /Genres reports 0 while returning rows (ARCHITECTURE.md §2), and although
// /MusicGenres was measured to report the truth on 4.9.5.0 (289, matching the
// rows), a StartIndex past the end still answers 0 with an empty array. 200 at a
// time covers the measured library in two round trips.
constexpr int kGenrePageSize = 200;
// A hard stop on the genre walk. 289 genres is the measured library; a server
// that answers a full page forever must not spin this loop.
constexpr int kGenrePageLimit = 25;

QVariantMap sortOption(const QString &key, const QString &label)
{
    QVariantMap map;
    map.insert(QStringLiteral("key"), key);
    map.insert(QStringLiteral("label"), label);
    return map;
}

// ── Random is a ONE-PAGE sort ────────────────────────────────────────────────
// All three tabs fetch with StartIndex/Limit, and Emby re-randomises SortBy=
// Random on every request with no seed parameter to pin the shuffle. So the
// second page, drawn at StartIndex = rowCount(), is a *fresh* shuffle: it
// repeats rows already on screen and silently omits others. On the Songs tab
// that is worse than a cosmetic glitch — playSongFrom() builds the play queue
// out of every loaded row, so the duplicates go into the queue.
//
// MUSIC.md §2.1 asks for Random on all three tabs, so the sort stays and the
// paging goes: canLoadMore* is false under it, which makes both the grid's and
// the table's prefetch inert and hides the "Load more" affordances. One honest
// shuffled page of 100 beats an infinite list that is quietly wrong. Shuffling
// the whole library, uncapped, is what the header's ▸ Shuffle button is for —
// that one is server-side and never pages.
bool isRandomSort(const QString &sortBy)
{
    return sortBy.compare(QLatin1String("Random"), Qt::CaseInsensitive) == 0;
}

// An album's whole track list in one request. The longest sets on the target
// library are box sets in the low hundreds, so paging tracks would add a
// loading seam to something that is read as one table.
constexpr int kTrackLimit = 500;
} // namespace

MusicController::MusicController(emby::EmbyClient *client, QObject *parent)
    : QObject(parent), m_client(client), m_albums(new MediaItemModel(this)),
      m_artists(new MediaItemModel(this)), m_tracks(new MediaItemModel(this)),
      m_songs(new MediaItemModel(this)), m_playlists(new MediaItemModel(this)),
      m_artistAlbums(new MediaItemModel(this)), m_artistTracks(new MediaItemModel(this)),
      m_playScratch(new MediaItemModel(this))
{
}

void MusicController::setActions(ItemActions *actions)
{
    m_actions = actions;
}

void MusicController::resetSessionState()
{
    ++m_generation;
    ++m_albumGeneration;
    ++m_artistGeneration;
    ++m_songGeneration;
    ++m_playlistGeneration;
    ++m_genreGeneration;
    ++m_playGeneration;
    ++m_collectGeneration;
    m_albumInFlight = 0;
    m_artistInFlight = 0;
    m_songInFlight = 0;
    m_playlistInFlight = 0;
    m_detailInFlight = 0;
    m_artistAlbumsInFlight = 0;
    m_artistTracksInFlight = 0;

    m_albums->clear();
    m_artists->clear();
    m_tracks->clear();
    m_songs->clear();
    m_playlists->clear();
    m_artistAlbums->clear();
    m_artistTracks->clear();
    m_playScratch->clear();

    m_libraryId.clear();
    m_albumId.clear();
    m_albumName.clear();
    m_artistId.clear();
    m_artistName.clear();
    m_detailKind.clear();
    m_detailId.clear();
    m_detailError.clear();
    m_artistMode = QStringLiteral("albumArtists");
    m_tab = QStringLiteral("albums");
    m_albumSortBy = QStringLiteral("SortName");
    m_albumSortDescending = false;
    m_artistSortBy = QStringLiteral("SortName");
    m_artistSortDescending = false;
    m_songSortBy = QStringLiteral("SortName");
    m_songSortDescending = false;
    m_playlistSortBy = QStringLiteral("SortName");
    m_playlistSortDescending = false;
    m_nameStartsWith.clear();
    m_genreIds.clear();
    m_favoritesOnly = false;
    m_genreOptions.clear();
    m_genreNextIndex = 0;
    m_genresComplete = false;
    m_genreWalkActive = false;
    m_genresFailed = false;
    m_started = false;

    updateLoading();
    setError({});
    emit detailStatusChanged();
    emit scopeChanged();
    emit albumChanged();
    emit artistChanged();
    emit albumsChanged();
    emit artistsChanged();
    emit songsChanged();
    emit playlistsChanged();
    emit queryChanged();
    emit tabChanged();
    emit genresChanged();
}

bool MusicController::canLoadMoreAlbums() const
{
    if (isRandomSort(m_albumSortBy))
        return false; // see isRandomSort(): a second page is a second shuffle
    return m_albums->rowCount() < m_albums->totalRecordCount();
}

bool MusicController::canLoadMoreArtists() const
{
    if (isRandomSort(m_artistSortBy))
        return false;
    return m_artists->rowCount() < m_artists->totalRecordCount();
}

bool MusicController::canLoadMoreSongs() const
{
    if (isRandomSort(m_songSortBy))
        return false;
    return m_songs->rowCount() < m_songs->totalRecordCount();
}

bool MusicController::canLoadMorePlaylists() const
{
    if (isRandomSort(m_playlistSortBy))
        return false;
    return m_playlists->rowCount() < m_playlists->totalRecordCount();
}

// ── The query surface (ARCHITECTURE.md) ──────────────────────────────────────

QString MusicController::sortBy() const
{
    if (m_tab == QLatin1String("artists"))
        return m_artistSortBy;
    if (m_tab == QLatin1String("songs"))
        return m_songSortBy;
    if (m_tab == QLatin1String("playlists"))
        return m_playlistSortBy;
    return m_albumSortBy;
}

bool MusicController::sortDescending() const
{
    if (m_tab == QLatin1String("artists"))
        return m_artistSortDescending;
    if (m_tab == QLatin1String("songs"))
        return m_songSortDescending;
    if (m_tab == QLatin1String("playlists"))
        return m_playlistSortDescending;
    return m_albumSortDescending;
}

QVariantList MusicController::availableSorts() const
{
    // Every key below was run against the live 4.9.5.0 server and answered with
    // a differently-ordered first row; an unaccepted sort key does not fail on
    // this server, it silently returns the default order and reads as a sort
    // that "did nothing".
    if (m_tab == QLatin1String("artists")) {
        // /Artists and /Artists/AlbumArtists take SortBy, SortOrder,
        // NameStartsWith and GenreIds — measured. There is no release year or
        // date-added for a person, so the list is genuinely this short.
        return {sortOption(QStringLiteral("SortName"), tr("Sort name")),
                sortOption(QStringLiteral("Random"), tr("Random")),
                sortOption(QStringLiteral("PlayCount"), tr("Most played"))};
    }
    if (m_tab == QLatin1String("songs")) {
        return {
            sortOption(QStringLiteral("SortName"), tr("Sort name")),
            sortOption(QStringLiteral("Album,SortName"), tr("Album")),
            sortOption(QStringLiteral("AlbumArtist,Album,SortName"), tr("Artist")),
            // Disc then track, and only here: this is the one list where the
            // user asked for track order explicitly, so the null-disc hazard
            // ItemActions documents is a chosen ordering rather than a silent
            // one imposed on a "play all".
            sortOption(QStringLiteral("ParentIndexNumber,IndexNumber,SortName"),
                       tr("Track number")),
            sortOption(QStringLiteral("DateCreated"), tr("Date added")),
            sortOption(QStringLiteral("Runtime"), tr("Runtime")),
            sortOption(QStringLiteral("PlayCount"), tr("Play count")),
            sortOption(QStringLiteral("Random"), tr("Random")),
        };
    }
    if (m_tab == QLatin1String("playlists")) {
        // Measured against the live server on the ParentId-scoped playlist
        // query: each of these reordered the first rows, and the two that are
        // NOT here did not. A playlist has no release year, and community
        // rating on a user's own list means nothing.
        return {
            sortOption(QStringLiteral("SortName"), tr("Sort name")),
            sortOption(QStringLiteral("DateCreated"), tr("Date added")),
            sortOption(QStringLiteral("Runtime"), tr("Length")),
            sortOption(QStringLiteral("PlayCount"), tr("Most played")),
            sortOption(QStringLiteral("Random"), tr("Random")),
        };
    }
    return {
        sortOption(QStringLiteral("SortName"), tr("Sort name")),
        sortOption(QStringLiteral("ProductionYear"), tr("Release year")),
        sortOption(QStringLiteral("DateCreated"), tr("Date added")),
        sortOption(QStringLiteral("Random"), tr("Random")),
        sortOption(QStringLiteral("CommunityRating"), tr("Community rating")),
        sortOption(QStringLiteral("PlayCount"), tr("Most played")),
    };
}

bool MusicController::filtered() const
{
    return !m_nameStartsWith.isEmpty() || !m_genreIds.isEmpty() || m_favoritesOnly;
}

void MusicController::setTab(const QString &tab)
{
    const QString wanted = (tab == QLatin1String("artists") || tab == QLatin1String("songs")
                            || tab == QLatin1String("playlists"))
                               ? tab
                               : QStringLiteral("albums");
    if (m_tab == wanted)
        return;
    m_tab = wanted;
    emit tabChanged();
    // The sort belongs to the tab, so moving between tabs moves the value every
    // sort control on screen is rendering.
    emit queryChanged();
    ensureCurrentTab();
}

void MusicController::setSort(const QString &key, bool descending)
{
    if (key.isEmpty() || (sortBy() == key && sortDescending() == descending))
        return;
    if (m_tab == QLatin1String("artists")) {
        m_artistSortBy = key;
        m_artistSortDescending = descending;
    } else if (m_tab == QLatin1String("songs")) {
        m_songSortBy = key;
        m_songSortDescending = descending;
    } else if (m_tab == QLatin1String("playlists")) {
        m_playlistSortBy = key;
        m_playlistSortDescending = descending;
    } else {
        m_albumSortBy = key;
        m_albumSortDescending = descending;
    }
    // A sort is per tab, so it invalidates only the tab it belongs to — unlike
    // every filter below, which invalidates all three.
    emit queryChanged();
    // The tab's own list signal goes with it, because canLoadMore* now reads the
    // sort (see isRandomSort()): picking Random has to retract "there is more"
    // straight away rather than at whatever moment the next page happens to land.
    switch (currentTabIndex()) {
    case 1:
        emit artistsChanged();
        if (m_started)
            fetchArtists(0);
        break;
    case 2:
        emit songsChanged();
        if (m_started)
            fetchSongs(0);
        break;
    case 3:
        emit playlistsChanged();
        if (m_started)
            fetchPlaylists(0);
        break;
    default:
        emit albumsChanged();
        if (m_started)
            fetchAlbums(0);
        break;
    }
}

void MusicController::setNameStartsWith(const QString &letter)
{
    // The bar toggles: tapping the active letter clears it rather than
    // re-running the identical query. Same contract as LibraryController's.
    const QString wanted = (letter == m_nameStartsWith) ? QString() : letter;
    if (m_nameStartsWith == wanted)
        return;
    m_nameStartsWith = wanted;
    applyQueryChange();
}

void MusicController::setGenreIds(const QStringList &genreIds)
{
    QStringList wanted = genreIds;
    wanted.removeAll(QString());
    if (m_genreIds == wanted)
        return;
    m_genreIds = wanted;
    applyQueryChange();
}

void MusicController::setFavoritesOnly(bool favoritesOnly)
{
    if (m_favoritesOnly == favoritesOnly)
        return;
    m_favoritesOnly = favoritesOnly;
    applyQueryChange();
}

void MusicController::clearFilters()
{
    if (!filtered())
        return;
    m_nameStartsWith.clear();
    m_genreIds.clear();
    m_favoritesOnly = false;
    applyQueryChange();
}

void MusicController::applyFilters(ItemsQuery &query) const
{
    query.nameStartsWith = m_nameStartsWith;
    query.genreIds = m_genreIds;
    if (m_favoritesOnly)
        query.filters.append(QStringLiteral("IsFavorite"));
}

int MusicController::currentTabIndex() const
{
    if (m_tab == QLatin1String("artists"))
        return 1;
    if (m_tab == QLatin1String("songs"))
        return 2;
    if (m_tab == QLatin1String("playlists"))
        return 3;
    return 0;
}

int MusicController::retire(int &generation, int &inFlight)
{
    inFlight = 0;
    return ++generation;
}

void MusicController::updateLoading()
{
    setLoading(m_albumInFlight != 0 || m_artistInFlight != 0 || m_songInFlight != 0
               || m_playlistInFlight != 0);
}

int MusicController::beginDetail(const QString &kind, const QString &id)
{
    const int generation = retire(m_generation, m_detailInFlight);
    m_artistAlbumsInFlight = 0;
    m_artistTracksInFlight = 0;
    m_detailKind = kind;
    m_detailId = id;
    m_detailError.clear();
    if (kind == QLatin1String("artist")) {
        m_artistAlbumsInFlight = generation;
        m_artistTracksInFlight = generation;
    } else {
        m_detailInFlight = generation;
    }
    emit detailStatusChanged();
    return generation;
}

void MusicController::finishDetail(int generation, const QString &error)
{
    if (generation != m_generation)
        return;
    m_detailInFlight = 0;
    m_detailError = error;
    if (!error.isEmpty())
        qCWarning(logApp) << "music detail:" << error;
    emit detailStatusChanged();
}

void MusicController::finishArtistAlbums(int generation, const QString &error)
{
    if (generation != m_generation || m_artistAlbumsInFlight != generation)
        return;
    m_artistAlbumsInFlight = 0;
    m_detailError = error;
    if (!error.isEmpty())
        qCWarning(logApp) << "music detail:" << error;
    emit detailStatusChanged();
}

void MusicController::finishArtistTracks(int generation)
{
    if (generation != m_generation || m_artistTracksInFlight != generation)
        return;
    m_artistTracksInFlight = 0;
    emit detailStatusChanged();
}

void MusicController::applyQueryChange()
{
    emit queryChanged();
    if (!m_started)
        return; // nothing has been asked for yet; the value is a preference so far

    // Clearing a model is not enough on its own — a page already in flight for
    // the old filter would land in the new one — so every generation moves.
    retire(m_albumGeneration, m_albumInFlight);
    retire(m_artistGeneration, m_artistInFlight);
    retire(m_songGeneration, m_songInFlight);
    retire(m_playlistGeneration, m_playlistInFlight);
    updateLoading();
    m_albums->clear();
    m_artists->clear();
    m_songs->clear();
    m_playlists->clear();
    emit albumsChanged();
    emit artistsChanged();
    emit songsChanged();
    emit playlistsChanged();
    // Only the visible tab refetches. The others are empty now, so the page's
    // own "load this tab if it is empty" path fills them when they are next
    // looked at, which is one request instead of four for a filter the user can
    // see the results of in one place.
    switch (currentTabIndex()) {
    case 1:
        fetchArtists(0);
        break;
    case 2:
        fetchSongs(0);
        break;
    case 3:
        fetchPlaylists(0);
        break;
    default:
        fetchAlbums(0);
        break;
    }
}

void MusicController::ensureCurrentTab()
{
    if (!m_started)
        return;
    switch (currentTabIndex()) {
    case 1:
        if (m_artists->rowCount() == 0 && m_artistInFlight == 0)
            fetchArtists(0);
        break;
    case 2:
        if (m_songs->rowCount() == 0 && m_songInFlight == 0)
            fetchSongs(0);
        break;
    case 3:
        if (m_playlists->rowCount() == 0 && m_playlistInFlight == 0)
            fetchPlaylists(0);
        break;
    default:
        if (m_albums->rowCount() == 0 && m_albumInFlight == 0)
            fetchAlbums(0);
        break;
    }
}

void MusicController::setArtistMode(const QString &mode)
{
    const QString wanted = mode == QLatin1String("artists") ? mode
                                                            : QStringLiteral("albumArtists");
    if (m_artistMode == wanted)
        return;
    m_artistMode = wanted;
    // `artistMode` notifies on artistsChanged, and the only other emit on this
    // path is inside fetchArtists()'s reply — which never runs on an error. The
    // two chips render this value, so without an emit here they sat on the old
    // mode until the network answered and stayed on it for good if it did not:
    // pressing "All artists" looked like it had done nothing.
    emit artistsChanged();
    // A mode chosen before the first list was asked for is a preference, not a
    // query (see m_started): loadArtists() would have set the flag and fired a
    // request, which is the one thing the contract says a setter must not do.
    if (!m_started)
        return;
    fetchArtists(0);
}

void MusicController::setLibrary(const QString &libraryId)
{
    if (m_libraryId == libraryId)
        return;
    m_libraryId = libraryId;
    emit scopeChanged();
    // Re-targeting invalidates the pages already in flight. Clearing the models
    // is not enough on its own: a page requested for the old library lands a
    // moment later and setItems() drops it straight into the new scope, so the
    // grid shows one library's albums under another library's name.
    //
    // Those dropped replies were the ones that would have cleared `loading`, and
    // nothing has been requested for the new scope yet — so retiring them lowers
    // the flag here rather than leaving the grid shimmering until some unrelated
    // fetch happens to finish. It used to take an explicit setLoading(false) at
    // the end of this function; the per-list in-flight markers make it
    // automatic. Album/artist detail has a separate status surface and cannot
    // hold this library-list flag up or have it cleared here.
    retire(m_albumGeneration, m_albumInFlight);
    retire(m_artistGeneration, m_artistInFlight);
    retire(m_songGeneration, m_songInFlight);
    retire(m_playlistGeneration, m_playlistInFlight);
    updateLoading();
    ++m_genreGeneration;
    m_albums->clear();
    m_artists->clear();
    m_songs->clear();
    // Four models cleared, four signals — applyQueryChange() does the same, and
    // for the same reason. canLoadMoreAlbums, canLoadMoreArtists and artistMode
    // all notify on these two, so leaving them out left "there is more" true for
    // the previous library's 5,037 rows in front of a grid holding nothing.
    emit albumsChanged();
    emit artistsChanged();
    emit songsChanged();
    // The playlist list is scoped by the library id too, and more literally
    // than the rest: ParentId is the ONLY thing separating an audio playlist
    // from a video one, so a page fetched for the old library is not merely
    // stale here, it is the wrong media type.
    m_playlists->clear();
    emit playlistsChanged();
    // The genre list belongs to the library, not to the session: /MusicGenres
    // is scoped by ParentId (measured), so another library's 289 genres are the
    // wrong 289.
    m_genreOptions.clear();
    m_genreNextIndex = 0;
    m_genresComplete = false;
    m_genreWalkActive = false;
    m_genresFailed = false;
    emit genresChanged();

    // ── The FILTERS belong to the library too ────────────────────────────────
    // A genre id is per-library — it is the id of a MusicGenre row scoped by
    // ParentId — so carrying the selection across a scope change sends library
    // A's GenreIds against library B's parent. Everything comes back empty,
    // filtered() is still true so the page says "Nothing matches these filters",
    // and the Genre select reads "1 selected" with no way to see WHICH, because
    // that id is not in the new options list at all.
    //
    // The other filters go with it rather than being kept "because a letter is a
    // letter": one rule ("filters belong to the scope") is what keeps the Clear
    // button, the alphabet strip, filtered() and the empty state telling the
    // same story. The per-tab SORTS are library-neutral and do survive.
    if (filtered()) {
        m_nameStartsWith.clear();
        m_genreIds.clear();
        m_favoritesOnly = false;
        emit queryChanged();
    }
}

void MusicController::loadAlbums()
{
    m_started = true;
    // Main and MusicPage may both ensure the restored default tab. A request
    // already owns this lane, even when another lane also contributes to the
    // aggregate loading flag; do not retire a useful page-0 request merely
    // because the page graph was reconstructed.
    if (m_albumInFlight != 0)
        return;
    fetchAlbums(0);
}

void MusicController::loadMoreAlbums()
{
    if (!canLoadMoreAlbums() || m_albumInFlight != 0)
        return;
    fetchAlbums(m_albums->rowCount());
}

void MusicController::fetchAlbums(int startIndex)
{
    const int generation = retire(m_albumGeneration, m_albumInFlight);
    m_albumInFlight = generation;
    updateLoading();

    ItemsQuery query;
    query.parentId = m_libraryId;
    query.includeItemTypes = {QStringLiteral("MusicAlbum")};
    query.recursive = true;
    query.sortBy = m_albumSortBy;
    query.sortDescending = m_albumSortDescending;
    query.startIndex = startIndex;
    query.limit = kPageSize;
    // ChildCount is the track count an album card prints; without asking, every
    // album claims zero tracks.
    query.fields = {QStringLiteral("ChildCount"), QStringLiteral("AlbumArtist")};
    applyFilters(query);

    m_client->items(query).then(
        this, [this, generation, startIndex](const Result<ItemsPage> &result) {
            if (generation != m_albumGeneration)
                return;
            m_albumInFlight = 0;
            updateLoading();
            if (!result.ok()) {
                setError(result.error);
                return;
            }
            setError(QString());
            if (startIndex == 0)
                m_albums->setItems(result.value.items, result.value.totalRecordCount);
            else
                m_albums->appendItems(result.value.items, result.value.totalRecordCount);
            emit albumsChanged();
        });
}

void MusicController::loadArtists()
{
    m_started = true;
    if (m_artistInFlight != 0)
        return;
    fetchArtists(0);
}

void MusicController::loadMoreArtists()
{
    if (!canLoadMoreArtists() || m_artistInFlight != 0)
        return;
    fetchArtists(m_artists->rowCount());
}

void MusicController::fetchArtists(int startIndex)
{
    const int generation = retire(m_artistGeneration, m_artistInFlight);
    m_artistInFlight = generation;
    updateLoading();

    ItemsQuery query;
    query.parentId = m_libraryId;
    query.sortBy = m_artistSortBy;
    query.sortDescending = m_artistSortDescending;
    query.startIndex = startIndex;
    query.limit = kPageSize;
    applyFilters(query);

    // /Artists and /Artists/AlbumArtists are different endpoints, not a filter
    // on one: on the target server they return 3,789 and 2,394 respectively.
    const bool albumArtists = m_artistMode == QLatin1String("albumArtists");
    auto future = albumArtists ? m_client->albumArtists(query) : m_client->musicArtists(query);
    future.then(this, [this, generation, startIndex](const Result<ItemsPage> &result) {
        if (generation != m_artistGeneration)
            return;
        m_artistInFlight = 0;
        updateLoading();
        if (!result.ok()) {
            setError(result.error);
            return;
        }
        setError(QString());
        if (startIndex == 0)
            m_artists->setItems(result.value.items, result.value.totalRecordCount);
        else
            m_artists->appendItems(result.value.items, result.value.totalRecordCount);
        emit artistsChanged();
    });
}

// ── Songs (the third tab) ────────────────────────────────────────────────────
// Its own model and its own generation counter, never `tracks`: that model is
// the open album's, and a Songs tab sharing it would refill the album page out
// from under itself the moment both were live.

void MusicController::loadSongs()
{
    m_started = true;
    if (m_songInFlight != 0)
        return;
    fetchSongs(0);
}

void MusicController::loadMoreSongs()
{
    if (!canLoadMoreSongs() || m_songInFlight != 0)
        return;
    fetchSongs(m_songs->rowCount());
}

void MusicController::fetchSongs(int startIndex)
{
    const int generation = retire(m_songGeneration, m_songInFlight);
    m_songInFlight = generation;
    updateLoading();

    ItemsQuery query;
    query.parentId = m_libraryId;
    query.includeItemTypes = {QStringLiteral("Audio")};
    // 56,283 tracks on the target library, so this pages like the grids do and
    // never asks for the lot.
    query.recursive = true;
    query.sortBy = m_songSortBy;
    query.sortDescending = m_songSortDescending;
    query.startIndex = startIndex;
    query.limit = kPageSize;
    // Genres lets a filtered list say why a row is in it; ParentIndexNumber is
    // consumed by TrackTable's disc pass. Playback always resolves its own
    // PlaybackInfo ticket, so list rows do not need the discarded media arrays.
    query.fields = {QStringLiteral("Genres"), QStringLiteral("ParentIndexNumber")};
    applyFilters(query);

    m_client->items(query).then(
        this, [this, generation, startIndex](const Result<ItemsPage> &result) {
            if (generation != m_songGeneration)
                return; // a newer filter, sort or library superseded this reply
            m_songInFlight = 0;
            updateLoading();
            if (!result.ok()) {
                setError(result.error);
                return;
            }
            setError(QString());
            if (startIndex == 0)
                m_songs->setItems(result.value.items, result.value.totalRecordCount);
            else
                m_songs->appendItems(result.value.items, result.value.totalRecordCount);
            emit songsChanged();
        });
}

// ── Playlists (the fourth tab) ───────────────────────────────────────────────
// See the header for the measurement this rests on: Emby publishes no media
// type on a playlist anywhere, but it filters by one — ParentId resolves to a
// library's content type and matches the playlist's own. One request, no
// per-playlist probe, no cache.

void MusicController::loadPlaylists()
{
    m_started = true;
    if (m_playlistInFlight != 0)
        return;
    fetchPlaylists(0);
}

void MusicController::loadMorePlaylists()
{
    if (!canLoadMorePlaylists() || m_playlistInFlight != 0)
        return;
    fetchPlaylists(m_playlists->rowCount());
}

void MusicController::invalidatePlaylists()
{
    // Retire whatever is in flight before emptying, or the page requested a
    // moment ago lands in the model this just cleared and the new playlist is
    // missing from it anyway.
    //
    // retire() rather than a bare ++: the refetch below only happens when the
    // Playlists tab is the one on screen, so from any other tab this call ends
    // with a playlist request that has been made stale and no replacement made
    // for it. Its reply returns above the marker, and a `loading` flag that
    // nothing else lowers stays true for the rest of the session — which is
    // exactly the strand review found here.
    retire(m_playlistGeneration, m_playlistInFlight);
    updateLoading();
    m_playlists->clear();
    emit playlistsChanged();
    if (m_started && currentTabIndex() == 3)
        fetchPlaylists(0);
}

void MusicController::fetchPlaylists(int startIndex)
{
    const int generation = retire(m_playlistGeneration, m_playlistInFlight);
    // No library, no audio scoping. An unscoped query would answer with every
    // playlist the user has, film lists included, under a heading that says
    // this is their music — which is exactly the thing this tab exists to stop.
    //
    // The retire() above has already made any playlist page in flight stale, so
    // this early return has to answer for the flag as well — the same strand
    // invalidatePlaylists() had.
    if (m_libraryId.isEmpty()) {
        updateLoading();
        if (m_playlists->rowCount() > 0) {
            m_playlists->clear();
            emit playlistsChanged();
        }
        return;
    }
    m_playlistInFlight = generation;
    updateLoading();

    ItemsQuery query;
    query.parentId = m_libraryId;
    query.includeItemTypes = {QStringLiteral("Playlist")};
    query.recursive = true;
    query.sortBy = m_playlistSortBy;
    query.sortDescending = m_playlistSortDescending;
    query.startIndex = startIndex;
    query.limit = kPageSize;
    applyFilters(query);
    // GENRES ARE A WORKING AXIS. Measured on
    // 4.9.5.0 against this library: a playlist DOES carry genres — Emby
    // aggregates them from its members and publishes both `Genres` and
    // `GenreItems` on the list payload — and GenreIds narrows the tab the way it
    // narrows the others (1,564 playlists → 191 for Rock, against 5,037 albums →
    // 627). It is a working axis, not a filter that empties the grid, so it
    // stays. `Genres` by NAME answers differently again (299) and is not what
    // this sends.
    m_client->items(query).then(
        this, [this, generation, startIndex](const Result<ItemsPage> &result) {
            if (generation != m_playlistGeneration)
                return;
            m_playlistInFlight = 0;
            updateLoading();
            if (!result.ok()) {
                setError(result.error);
                return;
            }
            setError(QString());
            if (startIndex == 0)
                m_playlists->setItems(result.value.items, result.value.totalRecordCount);
            else
                m_playlists->appendItems(result.value.items, result.value.totalRecordCount);
            emit playlistsChanged();
        });
}

// ── Genres ───────────────────────────────────────────────────────────────────

void MusicController::loadGenres()
{
    // NOT "the list is non-empty, so we are done". A walk that answered page 0
    // and then failed on page 1 leaves 200 of the measured 289 genres behind,
    // and an emptiness guard calls that finished for the life of the scope —
    // 89 genres unreachable with no way to ask again. Only a walk that actually
    // reached the end of the list is finished; anything else resumes from the
    // page it stopped on, which is what turns every repeat call into a retry.
    if (m_genresComplete || m_genreWalkActive)
        return;
    m_genreWalkActive = true;
    if (m_genresFailed) {
        m_genresFailed = false;
        emit genresChanged();
    }
    fetchGenrePage(m_genreNextIndex, ++m_genreGeneration);
}

void MusicController::fetchGenrePage(int startIndex, int generation)
{
    m_client->musicGenres(m_libraryId, startIndex, kGenrePageSize)
        .then(this, [this, startIndex, generation](const Result<ItemsPage> &result) {
            if (generation != m_genreGeneration)
                return; // the library moved under this walk
            if (!result.ok()) {
                // Deliberately not setError(): errorMessage is the state of the
                // LISTS, and MusicPage draws it as "Couldn't load this music
                // library" over the grid. A genre list that did not arrive
                // leaves the filter control empty and everything else working,
                // which is a smaller failure than claiming the library is
                // broken.
                //
                // The walk stops here, but it is not *complete*: m_genreNextIndex
                // still points at the page that failed, so the next loadGenres()
                // picks it up again. genresFailed is what lets the filter control
                // say why it has nothing to offer instead of just being greyed
                // out for reasons the user cannot see.
                qCWarning(logApp) << "music: genres failed at" << startIndex << ":"
                                  << result.error;
                m_genreWalkActive = false;
                m_genresFailed = true;
                emit genresChanged();
                return;
            }
            for (const MediaItem &genre : result.value.items) {
                if (genre.id.isEmpty() || genre.name.isEmpty())
                    continue;
                QVariantMap option;
                option.insert(QStringLiteral("key"), genre.id);
                option.insert(QStringLiteral("label"), genre.name);
                m_genreOptions.append(option);
            }
            // Page on the ARRAY's own size (ARCHITECTURE.md §2): a short page is
            // the end of the list, and TotalRecordCount is not to be trusted
            // across this family of endpoints. Advanced by what the SERVER
            // returned, never by how many options survived the id/name check, or
            // a row the mapper dropped would shift the walk over one genre.
            const int received = static_cast<int>(result.value.items.size());
            m_genreNextIndex = startIndex + received;
            emit genresChanged();
            if (received == kGenrePageSize
                && m_genreNextIndex < kGenrePageSize * kGenrePageLimit) {
                fetchGenrePage(m_genreNextIndex, generation);
                return;
            }
            // Either a short page (the end of the list) or the hard stop, which
            // is deliberately also "done": a server that answers a full page
            // forever must not be retried forever either.
            m_genresComplete = true;
            m_genreWalkActive = false;
        });
}

void MusicController::openAlbum(const QString &albumId, const QString &name)
{
    if (albumId.isEmpty())
        return;
    m_albumId = albumId;
    m_albumName = name;
    emit albumChanged();
    m_tracks->clear();
    const int generation = beginDetail(QStringLiteral("album"), albumId);
    ItemsQuery query;
    query.parentId = albumId;
    // NOT recursive and NOT sorted: an album's children are its tracks, and the
    // server already returns them in disc/track order. SortName would scramble
    // every record ever made.
    query.recursive = false;
    query.limit = kTrackLimit;

    m_client->items(query).then(this, [this, generation](const Result<ItemsPage> &result) {
        if (generation != m_generation)
            return;
        if (!result.ok()) {
            finishDetail(generation, result.error);
            return;
        }
        m_tracks->setItems(result.value.items, result.value.totalRecordCount);
        finishDetail(generation, {});
    });
}

void MusicController::expandAlbum(const QString &albumId, std::function<bool()> stillCurrent,
                                  std::function<void(const QList<MediaItem> &)> onItems,
                                  const QString &failure)
{
    ItemsQuery query;
    query.parentId = albumId;
    // The same query openAlbum() issues, and for the same reason: NOT recursive
    // and NOT sorted, because the server already returns an album's children in
    // disc then track order.
    query.recursive = false;
    query.limit = kTrackLimit;

    m_client->items(query).then(
        this, [this, stillCurrent = std::move(stillCurrent), onItems = std::move(onItems),
               failure](const Result<ItemsPage> &result) {
            if (!stillCurrent())
                return;
            // actionFailed(), never setError(): the error property is the state
            // of the album and artist *lists*, and MusicPage draws it as a
            // paging banner offering to retry loadMoreAlbums() — the wrong
            // message, the wrong retry, and one nothing here would ever clear.
            // A verb that happened once reports once, as a toast.
            if (!result.ok()) {
                emit actionFailed(failure.arg(result.error));
                return;
            }
            onItems(result.value.items);
        });
}

void MusicController::playAlbum(const QString &albumId)
{
    if (albumId.isEmpty())
        return;
    if (!m_actions) {
        qCWarning(logApp) << "music: no ItemActions; cannot queue an album";
        emit actionFailed(tr("Playback is not available."));
        return;
    }

    const int generation = ++m_playGeneration;
    const quint64 playbackIntent = m_actions->reservePlaybackIntent();
    expandAlbum(
        albumId,
        [this, generation, playbackIntent] {
            return generation == m_playGeneration
                   && m_actions->isPlaybackIntentCurrent(playbackIntent);
        },
        [this, playbackIntent](const QList<MediaItem> &items) {
            m_playScratch->setItems(items, static_cast<int>(items.size()));
            QVariantList maps;
            maps.reserve(m_playScratch->rowCount());
            for (int row = 0; row < m_playScratch->rowCount(); ++row)
                maps.append(m_playScratch->get(row));
            // playAllFrom() says its own piece when the list is empty.
            m_actions->playAllFromIfCurrent(maps, 0, playbackIntent);
        },
        tr("Could not play this album: %1"));
}

void MusicController::collectAlbumTracks(const QString &albumId, const QString &subject)
{
    if (albumId.isEmpty())
        return;

    const int generation = ++m_collectGeneration;
    expandAlbum(
        albumId, [this, generation] { return generation == m_collectGeneration; },
        [this, subject](const QList<MediaItem> &items) {
            QStringList ids;
            ids.reserve(items.size());
            for (const MediaItem &item : items) {
                if (!item.id.isEmpty())
                    ids.append(item.id);
            }
            // A picker over nothing is a dead panel the user has to dismiss.
            if (ids.isEmpty()) {
                emit actionFailed(tr("This album has no tracks to add."));
                return;
            }
            emit albumTracksCollected(subject, ids);
        },
        tr("Could not read this album's tracks: %1"));
}

void MusicController::openArtist(const QString &artistId, const QString &name)
{
    if (artistId.isEmpty())
        return;
    m_artistId = artistId;
    m_artistName = name;
    emit artistChanged();
    m_artistAlbums->clear();
    m_artistTracks->clear();
    const int generation = beginDetail(QStringLiteral("artist"), artistId);
    ItemsQuery query;
    // AlbumArtistIds, not ArtistIds: a discography is what someone released,
    // not everything they guested on. Measured on one artist: 5 albums either
    // way, but ArtistIds also matches 45 individual tracks from compilations.
    query.albumArtistIds = {artistId};
    query.includeItemTypes = {QStringLiteral("MusicAlbum")};
    query.recursive = true;
    // Newest first: a discography reads as a career.
    query.sortBy = QStringLiteral("PremiereDate,ProductionYear,SortName");
    query.sortDescending = true;
    query.limit = kPageSize;
    query.fields = {QStringLiteral("ChildCount"), QStringLiteral("AlbumArtist")};

    m_client->items(query).then(this, [this, generation](const Result<ItemsPage> &result) {
        if (generation != m_generation)
            return;
        if (!result.ok()) {
            finishArtistAlbums(generation, result.error);
            return;
        }
        m_artistAlbums->setItems(result.value.items, result.value.totalRecordCount);
        finishArtistAlbums(generation, {});
    });

    // Top tracks, a separate list from the discography.
    ItemsQuery tracks;
    tracks.artistIds = {artistId};
    tracks.includeItemTypes = {QStringLiteral("Audio")};
    tracks.recursive = true;
    tracks.sortBy = QStringLiteral("PlayCount,SortName");
    tracks.sortDescending = true;
    tracks.limit = 50;
    tracks.fields = {QStringLiteral("ArtistItems")};
    m_client->items(tracks).then(this, [this, generation](const Result<ItemsPage> &result) {
        if (generation != m_generation)
            return;
        if (result.ok())
            m_artistTracks->setItems(result.value.items, result.value.totalRecordCount);
        finishArtistTracks(generation);
    });
}

void MusicController::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void MusicController::setError(const QString &message)
{
    if (!message.isEmpty())
        qCWarning(logApp) << "music:" << message;
    if (m_error == message)
        return;
    m_error = message;
    emit errorChanged();
}

} // namespace strmqt
