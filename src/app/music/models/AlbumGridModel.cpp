#include "app/music/models/AlbumGridModel.h"

#include "app/models/MediaItemModel.h"
#include "app/music/MusicFormat.h"
#include "server/dto/music/MusicMediaItem.h"

namespace strmqt::music {

QHash<int, QByteArray> AlbumGridModel::roleNames() const
{
    static const QHash<int, QByteArray> names{
        {IdRole, "itemId"}, {TitleRole, "title"}, {NameRole, "name"}, {ArtistRole, "artist"},
        {ArtistIdRole, "artistId"}, {SubtitleRole, "subtitle"}, {YearRole, "year"},
        {ReleaseTypeRole, "releaseType"}, {ReleaseBadgeRole, "releaseBadge"}, {CoverUrlRole, "coverUrl"},
        {PosterUrlRole, "posterUrl"}, {FormatBadgeRole, "formatBadge"}, {TrackCountRole, "trackCount"},
        {DurationTextRole, "durationText"}, {FavouriteRole, "favourite"}, {FavoriteRole, "favorite"},
        {PlayCountRole, "playCount"}, {TypeRole, "type"}};
    return names;
}

QVariant AlbumGridModel::dataFor(const Album &album, int role)
{
    switch (role) {
    case IdRole:
        return album.id;
    case TitleRole:
    case NameRole:
        return album.title;
    case ArtistRole:
        return joinNames(album.albumArtists);
    case ArtistIdRole:
        return album.albumArtists.isEmpty() ? QString() : album.albumArtists.first().id;
    case SubtitleRole: {
        const QString artist = joinNames(album.albumArtists);
        if (album.year <= 0)
            return artist;
        return artist.isEmpty() ? QString::number(album.year)
                                : QStringLiteral("%1 · %2").arg(artist).arg(album.year);
    }
    case YearRole:
        return album.year;
    case ReleaseTypeRole:
        return releaseTypeName(album.releaseType);
    case ReleaseBadgeRole:
        return album.releaseType == ReleaseType::Album ? QString() : releaseTypeName(album.releaseType);
    case CoverUrlRole:
    case PosterUrlRole:
        return coverUrl(album.coverRef);
    case FormatBadgeRole:
        return album.formatSummary;
    case TrackCountRole:
        return album.trackCount;
    case DurationTextRole:
        return formatRuntime(album.runtimeMs);
    case FavouriteRole:
    case FavoriteRole:
        return album.favourite;
    case PlayCountRole:
        return album.playCount;
    case TypeRole:
        return QStringLiteral("MusicAlbum");
    default:
        return {};
    }
}

QVariant AlbumGridModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= size())
        return {};
    return dataFor(m_items.at(index.row()), role);
}

QVariantMap AlbumGridModel::get(int row) const
{
    if (row < 0 || row >= size())
        return {};
    const Album &album = m_items.at(row);
    QVariantMap map = MediaItemModel::mapForItem(toMediaItem(album));
    const auto names = roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it)
        map.insert(QString::fromLatin1(it.value()), dataFor(album, it.key()));
    return map;
}

void AlbumGridModel::applyUserData(const QString &itemId, const UserDataPatch &patch)
{
    for (int row : rowsFor(itemId)) {
        Album &album = m_items[row];
        if (patch.favourite)
            album.favourite = *patch.favourite;
        if (patch.playCount)
            album.playCount = *patch.playCount;
        notifyRow(row, {FavouriteRole, FavoriteRole, PlayCountRole});
    }
}

} // namespace strmqt::music
