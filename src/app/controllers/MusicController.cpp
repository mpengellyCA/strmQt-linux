#include "MusicController.h"

#include "app/ItemActions.h"
#include "core/Log.h"
#include "server/dto/ItemsQuery.h"
#include "server/emby/EmbyClient.h"

namespace strmqt {

namespace {
constexpr int kPageSize = 100;
// An album's whole track list in one request. The longest sets on the target
// library are box sets in the low hundreds, so paging tracks would add a
// loading seam to something that is read as one table.
constexpr int kTrackLimit = 500;
} // namespace

MusicController::MusicController(emby::EmbyClient *client, QObject *parent)
    : QObject(parent), m_client(client), m_albums(new MediaItemModel(this)),
      m_artists(new MediaItemModel(this)), m_tracks(new MediaItemModel(this)),
      m_artistAlbums(new MediaItemModel(this)), m_artistTracks(new MediaItemModel(this)),
      m_playScratch(new MediaItemModel(this))
{
}

void MusicController::setActions(ItemActions *actions)
{
    m_actions = actions;
}

bool MusicController::canLoadMoreAlbums() const
{
    return m_albums->rowCount() < m_albums->totalRecordCount();
}

bool MusicController::canLoadMoreArtists() const
{
    return m_artists->rowCount() < m_artists->totalRecordCount();
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
    m_albums->clear();
    m_artists->clear();
    // Those dropped replies were the ones that would have cleared `loading`,
    // and nothing has been requested for the new scope yet — so clear it here
    // or the grid shimmers until some unrelated fetch happens to finish.
    setLoading(false);
}

void MusicController::loadAlbums()
{
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
    query.sortBy = QStringLiteral("SortName");
    query.startIndex = startIndex;
    query.limit = kPageSize;
    // ChildCount is the track count an album card prints; without asking, every
    // album claims zero tracks.
    query.fields = {QStringLiteral("ChildCount"), QStringLiteral("AlbumArtist")};

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

    // /Artists and /Artists/AlbumArtists are different endpoints, not a filter
    // on one: on the target server they return 3,789 and 2,394 respectively.
    const bool albumArtists = m_artistMode == QLatin1String("albumArtists");
    auto future = albumArtists ? m_client->albumArtists(m_libraryId, startIndex, kPageSize)
                               : m_client->musicArtists(m_libraryId, startIndex, kPageSize);
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
        if (!result.ok()) {
            setError(result.error);
            return;
        }
        if (!m_actions) {
            qCWarning(logApp) << "music: no ItemActions; cannot queue an album";
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
