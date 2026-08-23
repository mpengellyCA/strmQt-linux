#include "MediaItemModel.h"

namespace strmqt {

QString embyImageSource(const QString &itemId, const QString &imageType, const QString &tag)
{
    if (tag.isEmpty())
        return {};
    return QStringLiteral("image://emby/%1/%2/%3").arg(itemId, imageType, tag);
}

namespace {

QString displayLabel(const MediaItem &item)
{
    if (item.type == QLatin1String("Episode") && !item.seriesName.isEmpty()) {
        return QStringLiteral("%1 — S%2E%3 — %4")
            .arg(item.seriesName)
            .arg(item.parentIndexNumber)
            .arg(item.indexNumber)
            .arg(item.name);
    }
    return item.name;
}

// Emby-web card subtitle: year for movies, year-range for series, SxxEyy for episodes.
QString subtitleOf(const MediaItem &item)
{
    if (item.type == QLatin1String("Episode")) {
        QString label;
        if (item.parentIndexNumber >= 0 && item.indexNumber >= 0)
            label = QStringLiteral("S%1E%2").arg(item.parentIndexNumber).arg(item.indexNumber);
        if (item.runtimeMs() > 0) {
            if (!label.isEmpty())
                label += QStringLiteral(" · ");
            label += QStringLiteral("%1 min").arg(item.runtimeMs() / 60000);
        }
        return label;
    }
    // An album is filed under its artist, and on this server every album
    // reports productionYear 0 — so without this 5,037 cards carry a title and
    // nothing else. Emby web puts the album artist here for the same reason.
    if (item.type.compare(QLatin1String("MusicAlbum"), Qt::CaseInsensitive) == 0
        && !item.albumArtist.isEmpty())
        return item.albumArtist;
    // A track's second line is the artist who performed it, not a year.
    if (item.type.compare(QLatin1String("Audio"), Qt::CaseInsensitive) == 0) {
        if (!item.artists.isEmpty())
            return item.artists.join(QStringLiteral(", "));
        return item.albumArtist;
    }
    if (item.productionYear <= 0)
        return {};
    if (item.type == QLatin1String("Series")) {
        return item.status == QLatin1String("Continuing")
                   ? QStringLiteral("%1 – Present").arg(item.productionYear)
                   : QString::number(item.productionYear);
    }
    return QString::number(item.productionYear);
}

double progressOf(const MediaItem &item)
{
    if (item.runtimeTicks > 0 && item.playbackPositionTicks > 0)
        return double(item.playbackPositionTicks) / double(item.runtimeTicks);
    if (item.playedPercentage > 0.0)
        return item.playedPercentage / 100.0;
    return 0.0;
}

} // namespace

MediaItemModel::MediaItemModel(QObject *parent) : QAbstractListModel(parent) {}

int MediaItemModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_items.size());
}

QVariant MediaItemModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return {};
    const MediaItem &item = m_items[index.row()];

    switch (role) {
    case IdRole:
        return item.id;
    case NameRole:
        return item.name;
    case LabelRole:
        return displayLabel(item);
    case TypeRole:
        return item.type;
    case OverviewRole:
        return item.overview;
    case SeriesNameRole:
        return item.seriesName;
    case IndexNumberRole:
        return item.indexNumber;
    case ParentIndexNumberRole:
        return item.parentIndexNumber;
    case YearRole:
        return item.productionYear;
    case RuntimeMsRole:
        return item.runtimeMs();
    case PositionMsRole:
        return item.positionMs();
    case ProgressRole:
        return progressOf(item);
    case PlayedRole:
        return item.played;
    case FavoriteRole:
        return item.favorite;
    case ResumableRole:
        return item.isResumable();
    case PosterUrlRole:
        return embyImageSource(item.id, QStringLiteral("Primary"), item.primaryImageTag);
    case BackdropUrlRole:
        return embyImageSource(item.id, QStringLiteral("Backdrop"),
                               item.backdropImageTags.value(0));
    case PremiereDateRole:
        return item.premiereDate;
    case StatusRole:
        return item.status;
    case PlaylistItemIdRole:
        return item.playlistItemId;
    case ArtistsRole:
        return item.artists;
    case ArtistIdsRole:
        return item.artistIds;
    case AlbumArtistRole:
        return item.albumArtist;
    case AlbumRole:
        return item.album;
    case AlbumIdRole:
        return item.albumId;
    case ChildCountRole:
        return item.childCount;
    case ThumbUrlRole: {
        const MediaItem::ImageRef ref = item.thumbSource();
        return ref.isValid() ? embyImageSource(ref.itemId, ref.imageType, ref.tag) : QString();
    }
    case CommunityRatingRole:
        return item.communityRating;
    case OfficialRatingRole:
        return item.officialRating;
    case UnplayedCountRole:
        return item.unplayedItemCount;
    case SubtitleRole:
        return subtitleOf(item);
    default:
        return {};
    }
}

QHash<int, QByteArray> MediaItemModel::roleNames() const
{
    return {
        {IdRole, "itemId"},
        {NameRole, "name"},
        {LabelRole, "label"},
        {TypeRole, "type"},
        {OverviewRole, "overview"},
        {SeriesNameRole, "seriesName"},
        {IndexNumberRole, "indexNumber"},
        {ParentIndexNumberRole, "parentIndexNumber"},
        {YearRole, "year"},
        {RuntimeMsRole, "runtimeMs"},
        {PositionMsRole, "positionMs"},
        {ProgressRole, "progress"},
        {PlayedRole, "played"},
        {FavoriteRole, "favorite"},
        {ResumableRole, "resumable"},
        {PosterUrlRole, "posterUrl"},
        {BackdropUrlRole, "backdropUrl"},
        {ThumbUrlRole, "thumbUrl"},
        {PremiereDateRole, "premiereDate"},
        {StatusRole, "status"},
        {PlaylistItemIdRole, "playlistItemId"},
        {ArtistsRole, "artists"},
        {ArtistIdsRole, "artistIds"},
        {AlbumArtistRole, "albumArtist"},
        {AlbumRole, "album"},
        {AlbumIdRole, "albumId"},
        {ChildCountRole, "childCount"},
        {CommunityRatingRole, "communityRating"},
        {OfficialRatingRole, "officialRating"},
        {UnplayedCountRole, "unplayedCount"},
        {SubtitleRole, "subtitle"},
    };
}

void MediaItemModel::setItems(QList<MediaItem> items, int totalRecordCount)
{
    beginResetModel();
    m_items = std::move(items);
    m_totalRecordCount =
        totalRecordCount >= 0 ? totalRecordCount : static_cast<int>(m_items.size());
    endResetModel();
    emit countChanged();
    emit totalRecordCountChanged();
}

void MediaItemModel::appendItems(const QList<MediaItem> &items, int totalRecordCount)
{
    if (items.isEmpty())
        return;
    const int first = static_cast<int>(m_items.size());
    beginInsertRows(QModelIndex(), first, first + static_cast<int>(items.size()) - 1);
    m_items.append(items);
    endInsertRows();

    // Keep the total coherent with the rows: see the header for why a stale
    // total is worse than an approximate one.
    const int rows = static_cast<int>(m_items.size());
    const int total = qMax(totalRecordCount >= 0 ? totalRecordCount : m_totalRecordCount, rows);
    if (total != m_totalRecordCount) {
        m_totalRecordCount = total;
        emit totalRecordCountChanged();
    }
    emit countChanged();
}

void MediaItemModel::clear()
{
    setItems({}, 0);
}

QVariantMap MediaItemModel::get(int row) const
{
    QVariantMap map;
    const QModelIndex index = this->index(row);
    if (!index.isValid())
        return map;
    const auto roles = roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        map.insert(QString::fromLatin1(it.value()), data(index, it.key()));
    return map;
}

void MediaItemModel::updateUserData(const QString &itemId, bool played, bool favorite)
{
    // -1 = "leave the resume position alone", which is what a local played or
    // favourite toggle means: it says nothing about where the user is in the item.
    updateUserData(itemId, played, favorite, -1);
}

void MediaItemModel::updateUserData(const QString &itemId, bool played, bool favorite,
                                    qint64 playbackPositionTicks)
{
    for (int row = 0; row < m_items.size(); ++row) {
        if (m_items[row].id != itemId)
            continue;
        m_items[row].played = played;
        m_items[row].favorite = favorite;
        if (playbackPositionTicks >= 0)
            m_items[row].playbackPositionTicks = playbackPositionTicks;
        const QModelIndex idx = index(row);
        emit dataChanged(idx, idx,
                         {PlayedRole, FavoriteRole, ResumableRole, ProgressRole, PositionMsRole});
    }
}

} // namespace strmqt
