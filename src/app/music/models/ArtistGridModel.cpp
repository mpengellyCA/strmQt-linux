#include "app/music/models/ArtistGridModel.h"

#include "app/models/MediaItemModel.h"
#include "app/music/MusicFormat.h"
#include "server/dto/music/MusicMediaItem.h"

namespace strmqt::music {

QHash<int, QByteArray> ArtistGridModel::roleNames() const
{
    static const QHash<int, QByteArray> names{
        {IdRole, "itemId"}, {NameRole, "name"}, {TitleRole, "title"}, {SubtitleRole, "subtitle"},
        {RecordCountRole, "recordCount"}, {CoverUrlRole, "coverUrl"}, {PosterUrlRole, "posterUrl"},
        {BackdropUrlRole, "backdropUrl"}, {FavouriteRole, "favourite"}, {FavoriteRole, "favorite"},
        {TypeRole, "type"}};
    return names;
}

QVariant ArtistGridModel::dataFor(const Artist &artist, int role)
{
    switch (role) {
    case IdRole:
        return artist.id;
    case NameRole:
    case TitleRole:
        return artist.name;
    case SubtitleRole:
        return formatRecordCount(artist.albumCount);
    case RecordCountRole:
        return artist.albumCount;
    case CoverUrlRole:
    case PosterUrlRole:
        return coverUrl(artist.coverRef);
    case BackdropUrlRole:
        return coverUrl(artist.backdropRef);
    case FavouriteRole:
    case FavoriteRole:
        return artist.favourite;
    case TypeRole:
        return QStringLiteral("MusicArtist");
    default:
        return {};
    }
}

QVariant ArtistGridModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= size())
        return {};
    return dataFor(m_items.at(index.row()), role);
}

QVariantMap ArtistGridModel::get(int row) const
{
    if (row < 0 || row >= size())
        return {};
    const Artist &artist = m_items.at(row);
    QVariantMap map = MediaItemModel::mapForItem(toMediaItem(artist));
    const auto names = roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it)
        map.insert(QString::fromLatin1(it.value()), dataFor(artist, it.key()));
    return map;
}

void ArtistGridModel::applyUserData(const QString &itemId, const UserDataPatch &patch)
{
    for (int row : rowsFor(itemId)) {
        if (patch.favourite)
            m_items[row].favourite = *patch.favourite;
        notifyRow(row, {FavouriteRole, FavoriteRole});
    }
}

} // namespace strmqt::music
