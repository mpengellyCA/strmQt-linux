#include "MusicController.h"

#include "app/ItemActions.h"
#include "core/Log.h"
#include "server/dto/ItemsQuery.h"
#include "server/emby/EmbyClient.h"

#include <QVariantMap>

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
      m_songs(new MediaItemModel(this)), m_artistAlbums(new MediaItemModel(this)),
      m_artistTracks(new MediaItemModel(this)), m_playScratch(new MediaItemModel(this))
{
}

void MusicController::setActions(ItemActions *actions)
{
    m_actions = actions;
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

// ── The query surface (ARCHITECTURE.md) ──────────────────────────────────────

QString MusicController::sortBy() const
{
    if (m_tab == QLatin1String("artists"))
        return m_artistSortBy;
    if (m_tab == QLatin1String("songs"))
        return m_songSortBy;
    return m_albumSortBy;
}

bool MusicController::sortDescending() const
{
    if (m_tab == QLatin1String("artists"))
        return m_artistSortDescending;
    if (m_tab == QLatin1String("songs"))
        return m_songSortDescending;
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
    return !m_nameStartsWith.isEmpty() || !m_genreIds.isEmpty() || !m_yearFilters.isEmpty()
           || m_favoritesOnly;
}

void MusicController::setTab(const QString &tab)
{
    const QString wanted = (tab == QLatin1String("artists") || tab == QLatin1String("songs"))
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

void MusicController::setYearFilters(const QStringList &years)
{
    QStringList wanted = years;
    wanted.removeAll(QString());
    if (m_yearFilters == wanted)
        return;
    m_yearFilters = wanted;
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
    m_yearFilters.clear();
    m_favoritesOnly = false;
    applyQueryChange();
}

void MusicController::applyFilters(ItemsQuery &query) const
{
    query.nameStartsWith = m_nameStartsWith;
    query.genreIds = m_genreIds;
    query.yearFilters = m_yearFilters;
    if (m_favoritesOnly)
        query.filters.append(QStringLiteral("IsFavorite"));
}

int MusicController::currentTabIndex() const
{
    if (m_tab == QLatin1String("artists"))
        return 1;
    if (m_tab == QLatin1String("songs"))
        return 2;
    return 0;
}

void MusicController::applyQueryChange()
{
    emit queryChanged();
    if (!m_started)
        return; // nothing has been asked for yet; the value is a preference so far

    // Clearing a model is not enough on its own — a page already in flight for
    // the old filter would land in the new one — so every generation moves.
    ++m_albumGeneration;
    ++m_artistGeneration;
    ++m_songGeneration;
    m_albums->clear();
    m_artists->clear();
    m_songs->clear();
    emit albumsChanged();
    emit artistsChanged();
    emit songsChanged();
    // Only the visible tab refetches. The other two are empty now, so the page's
    // own "load this tab if it is empty" path fills them when they are next
    // looked at, which is one request instead of three for a filter the user can
    // see the results of in one place.
    switch (currentTabIndex()) {
    case 1:
        fetchArtists(0);
        break;
    case 2:
        fetchSongs(0);
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
        if (m_artists->rowCount() == 0)
            fetchArtists(0);
        break;
    case 2:
        if (m_songs->rowCount() == 0)
            fetchSongs(0);
        break;
    default:
        if (m_albums->rowCount() == 0)
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
    loadArtists();
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
    ++m_albumGeneration;
    ++m_artistGeneration;
    ++m_songGeneration;
    ++m_genreGeneration;
    m_albums->clear();
    m_artists->clear();
    m_songs->clear();
    emit songsChanged();
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
    // The other three go with it rather than being kept "because a letter is a
    // letter": one rule ("filters belong to the scope") is what keeps the Clear
    // button, the alphabet strip, filtered() and the empty state telling the
    // same story. The per-tab SORTS are library-neutral and do survive.
    if (filtered()) {
        m_nameStartsWith.clear();
        m_genreIds.clear();
        m_yearFilters.clear();
        m_favoritesOnly = false;
        emit queryChanged();
    }
    // Those dropped replies were the ones that would have cleared `loading`,
    // and nothing has been requested for the new scope yet — so clear it here
    // or the grid shimmers until some unrelated fetch happens to finish.
    setLoading(false);
}

void MusicController::loadAlbums()
{
    m_started = true;
    fetchAlbums(0);
}

void MusicController::loadMoreAlbums()
{
    if (!canLoadMoreAlbums() || m_loading)
        return;
    fetchAlbums(m_albums->rowCount());
}

void MusicController::fetchAlbums(int startIndex)
{
    const int generation = ++m_albumGeneration;
    setLoading(true);

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
            setLoading(false);
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
    fetchArtists(0);
}

void MusicController::loadMoreArtists()
{
    if (!canLoadMoreArtists() || m_loading)
        return;
    fetchArtists(m_artists->rowCount());
}

void MusicController::fetchArtists(int startIndex)
{
    const int generation = ++m_artistGeneration;
    setLoading(true);

    ItemsQuery query;
    query.parentId = m_libraryId;
    query.sortBy = m_artistSortBy;
    query.sortDescending = m_artistSortDescending;
    query.startIndex = startIndex;
    query.limit = kPageSize;
    applyFilters(query);
    // Years is not among the axes the artist endpoints honour (measured: it
    // returns nothing rather than everything), and a release year is not a
    // property of a person anyway — so a year filter narrows albums and songs
    // and leaves the artist list alone rather than silently emptying it.
    query.yearFilters.clear();

    // /Artists and /Artists/AlbumArtists are different endpoints, not a filter
    // on one: on the target server they return 3,789 and 2,394 respectively.
    const bool albumArtists = m_artistMode == QLatin1String("albumArtists");
    auto future = albumArtists ? m_client->albumArtists(query) : m_client->musicArtists(query);
    future.then(this, [this, generation, startIndex](const Result<ItemsPage> &result) {
        if (generation != m_artistGeneration)
            return;
        setLoading(false);
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
    fetchSongs(0);
}

void MusicController::loadMoreSongs()
{
    if (!canLoadMoreSongs() || m_loading)
        return;
    fetchSongs(m_songs->rowCount());
}

void MusicController::fetchSongs(int startIndex)
{
    const int generation = ++m_songGeneration;
    setLoading(true);

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
    // MediaSources so a row can be played without a second round trip, Genres
    // because a genre-filtered list should be able to say why a row is in it,
    // and ParentIndexNumber because TrackTable's disc pass reads it.
    query.fields = {QStringLiteral("MediaSources"), QStringLiteral("Genres"),
                    QStringLiteral("ParentIndexNumber")};
    applyFilters(query);

    m_client->items(query).then(
        this, [this, generation, startIndex](const Result<ItemsPage> &result) {
            if (generation != m_songGeneration)
                return; // a newer filter, sort or library superseded this reply
            setLoading(false);
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
    // Same gap openArtist() had: without these a failed track fetch is
    // indistinguishable from a slow one, and the page shimmers forever.
    setLoading(true);
    setError(QString());

    const int generation = ++m_generation;
    ItemsQuery query;
    query.parentId = albumId;
    // NOT recursive and NOT sorted: an album's children are its tracks, and the
    // server already returns them in disc/track order. SortName would scramble
    // every record ever made.
    query.recursive = false;
    query.limit = kTrackLimit;
    query.fields = {QStringLiteral("MediaSources")};

    m_client->items(query).then(this, [this, generation](const Result<ItemsPage> &result) {
        if (generation != m_generation)
            return;
        setLoading(false);
        if (!result.ok()) {
            setError(result.error);
            return;
        }
        m_tracks->setItems(result.value.items, result.value.totalRecordCount);
    });
}

void MusicController::playAlbum(const QString &albumId)
{
    if (albumId.isEmpty())
        return;

    const int generation = ++m_playGeneration;
    ItemsQuery query;
    query.parentId = albumId;
    // The same query openAlbum() issues, and for the same reason: NOT recursive
    // and NOT sorted, because the server already returns an album's children in
    // disc then track order.
    query.recursive = false;
    query.limit = kTrackLimit;
    query.fields = {QStringLiteral("MediaSources")};

    m_client->items(query).then(this, [this, generation](const Result<ItemsPage> &result) {
        if (generation != m_playGeneration)
            return;
        // actionFailed(), never setError(): the error property is the state of
        // the album and artist *lists*, and MusicPage draws it as a paging
        // banner offering to retry loadMoreAlbums() — the wrong message, the
        // wrong retry, and one nothing here would ever clear. A verb that
        // happened once reports once, as a toast.
        if (!result.ok()) {
            emit actionFailed(tr("Could not play this album: %1").arg(result.error));
            return;
        }
        if (!m_actions) {
            qCWarning(logApp) << "music: no ItemActions; cannot queue an album";
            emit actionFailed(tr("Playback is not available."));
            return;
        }
        m_playScratch->setItems(result.value.items, result.value.totalRecordCount);
        QVariantList items;
        items.reserve(m_playScratch->rowCount());
        for (int row = 0; row < m_playScratch->rowCount(); ++row)
            items.append(m_playScratch->get(row));
        // playAllFrom() says its own piece when the list is empty.
        m_actions->playAllFrom(items, 0);
    });
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
    // Gap found in review: this fetch reported neither progress nor failure, so
    // a failed discography was indistinguishable from an artist with no albums.
    setLoading(true);
    setError(QString());

    const int generation = ++m_generation;
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
        setLoading(false);
        if (!result.ok()) {
            setError(result.error);
            return;
        }
        m_artistAlbums->setItems(result.value.items, result.value.totalRecordCount);
    });

    // Top tracks, a separate list from the discography.
    ItemsQuery tracks;
    tracks.artistIds = {artistId};
    tracks.includeItemTypes = {QStringLiteral("Audio")};
    tracks.recursive = true;
    tracks.sortBy = QStringLiteral("PlayCount,SortName");
    tracks.sortDescending = true;
    tracks.limit = 50;
    tracks.fields = {QStringLiteral("MediaSources"), QStringLiteral("ArtistItems")};
    m_client->items(tracks).then(this, [this, generation](const Result<ItemsPage> &result) {
        if (generation != m_generation || !result.ok())
            return;
        m_artistTracks->setItems(result.value.items, result.value.totalRecordCount);
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
