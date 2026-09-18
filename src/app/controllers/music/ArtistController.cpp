#include "app/controllers/music/ArtistController.h"

#include <algorithm>

#include "app/models/MediaItemModel.h"
#include "app/music/MusicFormat.h"
#include "app/music/MusicPlayback.h"
#include "app/music/MusicRepository.h"
#include "server/dto/music/MusicMediaItem.h"

namespace strmqt::music {

namespace {

const QString kDot = QStringLiteral(" · ");

QVariantMap tab(const QString &key, const QString &label, int count)
{
    return {{QStringLiteral("key"), key},
            {QStringLiteral("label"), label},
            {QStringLiteral("count"), count}};
}

} // namespace

ArtistController::ArtistController(MusicRepository *repository, MusicPlayback *playback,
                                   QObject *parent)
    : QObject(parent)
    , m_repository(repository)
    , m_playback(playback)
    , m_albums(new AlbumGridModel(this))
    , m_epsAndSingles(new AlbumGridModel(this))
    , m_appearsOn(new AlbumGridModel(this))
    , m_topTracks(new TrackListModel(this))
    , m_similar(new ArtistGridModel(this))
{
}

QString ArtistController::kicker() const
{
    if (!m_loaded)
        return {};
    QStringList parts{tr("Artist")};
    const int records = m_albums->count() + m_epsAndSingles->count();
    if (records > 0)
        parts.append(formatRecordCount(records));
    if (m_artist.trackCount > 0)
        parts.append(formatTrackCount(m_artist.trackCount));
    return parts.join(kDot);
}

QString ArtistController::coverUrl() const
{
    return music::coverUrl(m_artist.coverRef);
}

QString ArtistController::backdropUrl() const
{
    return music::coverUrl(m_artist.backdropRef);
}

QVariantList ArtistController::tabs() const
{
    QVariantList rows;
    if (m_albums->count() > 0)
        rows.append(tab(QStringLiteral("albums"), tr("Albums"), m_albums->count()));
    if (m_epsAndSingles->count() > 0)
        rows.append(tab(QStringLiteral("epsAndSingles"), tr("EPs & Singles"), m_epsAndSingles->count()));
    if (m_appearsOn->count() > 0)
        rows.append(tab(QStringLiteral("appearsOn"), tr("Appears on"), m_appearsOn->count()));
    return rows;
}

QStringList ArtistController::topTrackCaptions() const
{
    QStringList captions;
    for (const Track &track : m_topTracks->items()) {
        const bool filedUnderArtist = std::any_of(
            track.albumArtists.cbegin(), track.albumArtists.cend(),
            [this](const ArtistRef &ref) { return ref.id == m_artist.id; });
        captions.append(filedUnderArtist || track.albumArtists.isEmpty()
                            ? track.albumTitle
                            : tr("Guest on %1").arg(track.albumTitle));
    }
    return captions;
}

QVariantMap ArtistController::artistItem() const
{
    if (m_artist.id.isEmpty())
        return {};
    return MediaItemModel::mapForItem(toMediaItem(m_artist));
}

void ArtistController::open(const QString &artistId, const QString &name, const QString &libraryId)
{
    if (artistId.isEmpty())
        return;
    // The library id belongs to the route, not to the artist, so it follows
    // every open — including a free reopen of the artist already shown, which
    // would otherwise keep publishing the library the page came from before
    // and refetch with it on the next retry() (fix round 1, FIX 4).
    const bool libraryChanged = libraryId != m_libraryId;
    m_libraryId = libraryId;
    if (artistId == m_artist.id && (m_loading || (m_loaded && m_error.isEmpty()))) {
        if (libraryChanged)
            emit artistChanged();
        return;
    }
    if (artistId != m_artist.id) {
        m_artist = Artist{};
        m_artist.id = artistId;
        m_artist.name = name;
        clearModels();
        m_loaded = false;
        emit artistChanged();
        emit favouriteChanged();
    } else if (libraryChanged) {
        emit artistChanged();
    }
    load();
}

void ArtistController::retry()
{
    if (!m_artist.id.isEmpty())
        load();
}

void ArtistController::load()
{
    const quint64 generation = ++m_generation;
    m_loading = true;
    m_error.clear();
    emit stateChanged();
    m_repository->artistProfile(m_libraryId, m_artist.id)
        .then(this, [this, generation](const Result<ArtistProfile> &result) {
            if (generation != m_generation)
                return;
            m_loading = false;
            if (!result.ok()) {
                m_error = result.error;
                emit stateChanged();
                return;
            }
            apply(result.value);
            emit stateChanged();
        });
}

void ArtistController::apply(const ArtistProfile &profile)
{
    m_artist = profile.artist;
    m_albums->setItems(profile.albums);
    m_epsAndSingles->setItems(profile.epsAndSingles);
    m_appearsOn->setItems(profile.appearsOn);
    m_topTracks->setItems(profile.topTracks);
    m_similar->setItems(profile.similar);
    m_loaded = true;
    emit artistChanged();
    emit favouriteChanged();
}

void ArtistController::clearModels()
{
    m_albums->clear();
    m_epsAndSingles->clear();
    m_appearsOn->clear();
    m_topTracks->clear();
    m_similar->clear();
}

void ArtistController::shuffle()
{
    if (!m_artist.id.isEmpty())
        m_playback->shuffleArtist(m_artist.id, m_artist.name);
}

void ArtistController::radio()
{
    if (!m_artist.id.isEmpty())
        m_playback->radio(m_artist.id, m_artist.name);
}

void ArtistController::playTopTrack(int row)
{
    const QList<Track> &tracks = m_topTracks->items();
    if (tracks.isEmpty())
        return;
    const int last = static_cast<int>(tracks.size()) - 1;
    m_playback->playTracks(tracks, std::clamp(row, 0, last), tr("Most played · %1").arg(m_artist.name));
}

void ArtistController::noteFavourite(const QString &itemId, bool favourite)
{
    if (itemId.isEmpty() || itemId != m_artist.id || m_artist.favourite == favourite)
        return;
    m_artist.favourite = favourite;
    emit favouriteChanged();
    emit artistChanged();
}

void ArtistController::resetSessionState()
{
    ++m_generation;
    m_artist = Artist{};
    m_libraryId.clear();
    clearModels();
    m_loaded = false;
    m_loading = false;
    m_error.clear();
    emit artistChanged();
    emit favouriteChanged();
    emit stateChanged();
}

} // namespace strmqt::music
