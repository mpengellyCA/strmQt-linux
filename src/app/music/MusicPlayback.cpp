#include "app/music/MusicPlayback.h"

#include "app/ItemActions.h"
#include "app/models/MediaItemModel.h"
#include "app/music/MusicRepository.h"
#include "app/music/models/StationModel.h"
#include "server/dto/music/MusicMediaItem.h"

namespace strmqt::music {

MusicPlayback::MusicPlayback(MusicRepository *repository, ItemActions *actions, QObject *parent)
    : QObject(parent)
    , m_repository(repository)
    , m_actions(actions)
{
}

QVariantList MusicPlayback::toMaps(const QList<Track> &tracks)
{
    QVariantList maps;
    maps.reserve(tracks.size());
    for (const Track &track : tracks)
        maps.append(MediaItemModel::mapForItem(toMediaItem(track)));
    return maps;
}

void MusicPlayback::reportFailure(const QString &error)
{
    emit m_actions->actionFailed(tr("Couldn't start playback: %1").arg(error));
}

void MusicPlayback::playTracks(const QList<Track> &tracks, int startIndex, const QString &sourceLabel)
{
    const quint64 generation = m_actions->reservePlaybackIntent();
    m_actions->playAllFromIfCurrent(toMaps(tracks), startIndex, generation, sourceLabel);
}

void MusicPlayback::playResolved(QFuture<Result<QList<Track>>> future, quint64 generation,
                                 int startIndex, Order order, const QString &sourceLabel)
{
    future.then(this, [this, generation, startIndex, order, sourceLabel](Result<QList<Track>> result) {
        if (!m_actions->isPlaybackIntentCurrent(generation))
            return;
        if (!result.ok()) {
            reportFailure(result.error);
            return;
        }
        // A shuffle hands the tracks over in the order they were resolved in
        // and lets the queue do the shuffling (ItemActions::shuffleAllFrom):
        // shuffling here made the scrambled list the queue's "original" order,
        // so the shuffle indicator stayed dark and could not be turned off.
        if (order == Order::Shuffled)
            m_actions->shuffleAllFromIfCurrent(toMaps(result.value), generation, sourceLabel);
        else
            m_actions->playAllFromIfCurrent(toMaps(result.value), startIndex, generation,
                                            sourceLabel);
    });
}

void MusicPlayback::playAlbum(const QString &albumId, const QString &title, int startIndex)
{
    const quint64 generation = m_actions->reservePlaybackIntent();
    playResolved(m_repository->albumTracks(albumId), generation, startIndex, Order::AsGiven, title);
}

void MusicPlayback::shuffleAlbum(const QString &albumId, const QString &title)
{
    const quint64 generation = m_actions->reservePlaybackIntent();
    playResolved(m_repository->albumTracks(albumId), generation, 0, Order::Shuffled,
                 tr("Shuffle · %1").arg(title));
}

void MusicPlayback::shuffleArtist(const QString &artistId, const QString &name)
{
    const quint64 generation = m_actions->reservePlaybackIntent();
    // The server's draw is already random, and it is still a shuffle: the
    // queue is flagged as one, and the draw is the order it un-shuffles to
    // (there is no better one for a sample of a whole discography).
    playResolved(m_repository->artistTracks(artistId), generation, 0, Order::Shuffled,
                 tr("Shuffle · %1").arg(name));
}

void MusicPlayback::radio(const QString &seedId, const QString &seedName)
{
    const quint64 generation = m_actions->reservePlaybackIntent();
    Station station;
    station.kind = StationKind::MoreLike;
    station.seedId = seedId;
    playResolved(m_repository->resolveStation(QString(), station), generation, 0, Order::AsGiven,
                 tr("Radio · %1").arg(seedName));
}

void MusicPlayback::playStation(const QString &libraryId, const Station &station)
{
    const quint64 generation = m_actions->reservePlaybackIntent();
    // The repository already orders each station (heavy rotation is shuffled
    // there), so the list is queued as given.
    playResolved(m_repository->resolveStation(libraryId, station), generation, 0, Order::AsGiven,
                 tr("Station · %1").arg(station.label));
}

void MusicPlayback::shuffleStation(const QString &libraryId, const Station &station)
{
    const quint64 generation = m_actions->reservePlaybackIntent();
    playResolved(m_repository->resolveStation(libraryId, station), generation, 0, Order::Shuffled,
                 tr("Station · %1").arg(station.label));
}

void MusicPlayback::queueStation(const QString &libraryId, const Station &station)
{
    m_repository->resolveStation(libraryId, station).then(this, [this](Result<QList<Track>> result) {
        if (!result.ok()) {
            emit m_actions->actionFailed(tr("Couldn't add to the queue: %1").arg(result.error));
            return;
        }
        m_actions->addAllToQueue(toMaps(result.value));
    });
}

void MusicPlayback::playStationTile(const QString &libraryId, const QVariantMap &tile)
{
    const auto kind = StationModel::kindFromKey(tile.value(QStringLiteral("itemId")).toString());
    if (!kind) {
        reportFailure(tr("unknown station"));
        return;
    }
    Station station;
    station.kind = *kind;
    station.label = tile.value(QStringLiteral("label")).toString();
    station.seedId = tile.value(QStringLiteral("seedId")).toString();
    playStation(libraryId, station);
}

void MusicPlayback::shuffleQuery(const MusicQuery &query, const QString &label)
{
    const quint64 generation = m_actions->reservePlaybackIntent();
    // sampleTracks is already random server-side. As with shuffleArtist(), the
    // sample is the order the queue un-shuffles to.
    playResolved(m_repository->sampleTracks(query), generation, 0, Order::Shuffled,
                 tr("Shuffle · %1").arg(label));
}

void MusicPlayback::playQuery(const MusicQuery &query, const QString &label, int limit)
{
    const quint64 generation = m_actions->reservePlaybackIntent();
    MusicQuery scoped = query;
    scoped.section = Section::Songs;
    // The letter is a place to land, not part of what the view means.
    scoped.letter.clear();
    auto tracks = m_repository->browseTracks(scoped, 0, limit).then(this, [](Result<Page<Track>> r) {
        if (!r.ok())
            return Result<QList<Track>>::failure(r.error);
        return Result<QList<Track>>::success(r.value.items);
    });
    playResolved(tracks, generation, 0, Order::AsGiven, label);
}

} // namespace strmqt::music
