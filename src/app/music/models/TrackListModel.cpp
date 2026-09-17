#include "app/music/models/TrackListModel.h"

#include "app/models/MediaItemModel.h"
#include "app/music/MusicFormat.h"
#include "server/dto/music/MusicMediaItem.h"

namespace strmqt::music {

const QHash<int, QByteArray> &TrackListModel::musicRoleNames()
{
    static const QHash<int, QByteArray> names{
        {DisplayTitleRole, "displayTitle"}, {FeaturedTextRole, "featuredText"},
        {ArtistTextRole, "artistText"}, {AlbumTitleRole, "albumTitle"}, {DiscNumberRole, "discNumber"},
        {TrackNumberRole, "trackNumber"}, {DurationTextRole, "durationText"},
        {FormatBadgeRole, "formatBadge"}, {IsHiResRole, "isHiRes"}, {FavouriteRole, "favourite"},
        {CoverUrlRole, "coverUrl"}, {DiffersFromAlbumArtistRole, "differsFromAlbumArtist"}};
    return names;
}

QHash<int, QByteArray> TrackListModel::roleNames() const
{
    // MediaItemModel's media roles occupy Qt::UserRole+1 to about +40, so +200
    // cannot collide; verified at compile time rather than with a runtime
    // Q_ASSERT, since the two enums are unrelated types (ruling P1-2).
    static_assert(int(MediaItemModel::SubtitleRole) < int(TrackListModel::DisplayTitleRole));
    QHash<int, QByteArray> names = MediaItemModel::mediaRoleNames();
    names.insert(musicRoleNames());
    return names;
}

void TrackListModel::onItemsReset()
{
    m_media.clear();
    m_media.reserve(m_items.size());
    for (const Track &track : std::as_const(m_items))
        m_media.append(toMediaItem(track));
}

void TrackListModel::onItemsAppended(int fromRow)
{
    for (int row = fromRow; row < m_items.size(); ++row)
        m_media.append(toMediaItem(m_items.at(row)));
}

QVariant TrackListModel::musicData(const Track &track, int role) const
{
    switch (role) {
    case DisplayTitleRole:
        return track.displayTitle.isEmpty() ? track.title : track.displayTitle;
    case FeaturedTextRole:
        return track.featured.isEmpty() ? QString() : QStringLiteral("feat. ") + joinNames(track.featured);
    case ArtistTextRole:
        return joinNames(track.artists);
    case AlbumTitleRole:
        return track.albumTitle;
    case DiscNumberRole:
        return track.discNumber;
    case TrackNumberRole:
        return track.trackNumber;
    case DurationTextRole:
        return formatDuration(track.runtimeMs);
    case FormatBadgeRole:
        return track.format.badge;
    case IsHiResRole:
        return track.format.isHiRes;
    case FavouriteRole:
        return track.favourite;
    case CoverUrlRole:
        return coverUrl(track.coverRef);
    case DiffersFromAlbumArtistRole:
        return track.differsFromAlbumArtist;
    default:
        return {};
    }
}

QVariant TrackListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= size())
        return {};
    if (role >= DisplayTitleRole)
        return musicData(m_items.at(index.row()), role);
    return MediaItemModel::dataForItem(m_media.at(index.row()), role);
}

QVariantMap TrackListModel::get(int row) const
{
    if (row < 0 || row >= size())
        return {};
    QVariantMap map = MediaItemModel::mapForItem(m_media.at(row));
    const auto &names = musicRoleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it)
        map.insert(QString::fromLatin1(it.value()), musicData(m_items.at(row), it.key()));
    return map;
}

QVariantList TrackListModel::mediaMaps() const
{
    QVariantList list;
    list.reserve(size());
    for (int row = 0; row < size(); ++row)
        list.append(get(row));
    return list;
}

void TrackListModel::applyUserData(const QString &itemId, const UserDataPatch &patch)
{
    for (int row : rowsFor(itemId)) {
        Track &track = m_items[row];
        MediaItem &media = m_media[row];
        if (patch.favourite)
            track.favourite = media.favorite = *patch.favourite;
        if (patch.played)
            track.played = media.played = *patch.played;
        if (patch.playCount)
            track.playCount = media.playCount = *patch.playCount;
        if (patch.positionMs) {
            track.positionMs = *patch.positionMs;
            media.playbackPositionTicks = *patch.positionMs * kTicksPerMs;
        }
        notifyRow(row, {MediaItemModel::FavoriteRole, MediaItemModel::PlayedRole,
                        MediaItemModel::PlayCountRole, MediaItemModel::PositionMsRole,
                        MediaItemModel::ProgressRole, MediaItemModel::ResumableRole, FavouriteRole});
    }
}

} // namespace strmqt::music
