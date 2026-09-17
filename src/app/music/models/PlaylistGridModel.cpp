#include "app/music/models/PlaylistGridModel.h"

#include "app/models/MediaItemModel.h"
#include "app/music/MusicFormat.h"
#include "server/dto/music/MusicMediaItem.h"

namespace strmqt::music {

QVariant PlaylistGridModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= size())
        return {};
    return dataFor(m_items.at(index.row()), role);
}

QHash<int, QByteArray> PlaylistGridModel::roleNames() const
{
    static const QHash<int, QByteArray> names{
        {IdRole, "itemId"}, {NameRole, "name"}, {TitleRole, "title"}, {SubtitleRole, "subtitle"},
        {TrackCountRole, "trackCount"}, {CoverUrlRole, "coverUrl"}, {PosterUrlRole, "posterUrl"},
        {TypeRole, "type"}};
    return names;
}

QVariant PlaylistGridModel::dataFor(const Playlist &playlist, int role)
{
    switch (role) {
    case IdRole:
        return playlist.id;
    case NameRole:
    case TitleRole:
        return playlist.name;
    case SubtitleRole: {
        const QString runtime = formatRuntime(playlist.runtimeMs);
        const QString tracks = formatTrackCount(playlist.trackCount);
        return runtime.isEmpty() ? tracks : QStringLiteral("%1 · %2").arg(tracks, runtime);
    }
    case TrackCountRole:
        return playlist.trackCount;
    case CoverUrlRole:
    case PosterUrlRole:
        return coverUrl(playlist.coverRef);
    case TypeRole:
        return QStringLiteral("Playlist");
    default:
        return {};
    }
}

QVariantMap PlaylistGridModel::get(int row) const
{
    if (row < 0 || row >= size())
        return {};
    const Playlist &playlist = m_items.at(row);
    MediaItem item;
    item.id = playlist.id;
    item.name = playlist.name;
    item.type = QStringLiteral("Playlist");
    item.childCount = playlist.trackCount;
    item.runtimeTicks = playlist.runtimeMs * kTicksPerMs;
    if (playlist.coverRef.itemId == playlist.id)
        item.primaryImageTag = playlist.coverRef.tag;
    QVariantMap map = MediaItemModel::mapForItem(item);
    const auto names = roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it)
        map.insert(QString::fromLatin1(it.value()), dataFor(playlist, it.key()));
    return map;
}

void PlaylistGridModel::applyUserData(const QString &, const UserDataPatch &) {}

} // namespace strmqt::music
