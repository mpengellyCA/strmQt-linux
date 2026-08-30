#include "MediaItemModel.h"

#include <QMutex>

namespace strmqt {

namespace {

struct ImageSourceState
{
    QMutex mutex;
    QString sourceNamespace;
};

ImageSourceState &imageSourceState()
{
    static ImageSourceState state;
    return state;
}

} // namespace

void setEmbyImageSourceNamespace(const QString &sourceNamespace)
{
    ImageSourceState &state = imageSourceState();
    QMutexLocker locker(&state.mutex);
    state.sourceNamespace = sourceNamespace;
}

QString embyImageSourceNamespace()
{
    ImageSourceState &state = imageSourceState();
    QMutexLocker locker(&state.mutex);
    return state.sourceNamespace;
}

QString embyImageSource(const QString &itemId, const QString &imageType, const QString &tag)
{
    if (tag.isEmpty())
        return {};
    const QString sourceNamespace = embyImageSourceNamespace();
    if (sourceNamespace.isEmpty())
        return {};
    return QStringLiteral("image://emby/%1/%2/%3/%4")
        .arg(sourceNamespace, itemId, imageType, tag);
}

QString embyImageProbeSource(const QString &itemId, const QString &imageType)
{
    const QString sourceNamespace = embyImageSourceNamespace();
    if (sourceNamespace.isEmpty() || itemId.isEmpty() || imageType.isEmpty())
        return {};
    return QStringLiteral("image://emby/%1/%2/%3/")
        .arg(sourceNamespace, itemId, imageType);
}

namespace {

// The one SxE rendering on the C++ side; QML uses MediaKinds.episodeCode()
// and the two must not drift apart. Unnumbered episodes (either index
// negative) have no code to show.
QString episodeCode(int season, int episode)
{
    if (season < 0 || episode < 0)
        return {};
    return QStringLiteral("S%1E%2").arg(season).arg(episode);
}

QString displayLabel(const MediaItem &item)
{
    if (item.type == QLatin1String("Episode") && !item.seriesName.isEmpty()) {
        const QString code = episodeCode(item.parentIndexNumber, item.indexNumber);
        if (code.isEmpty())
            return QStringLiteral("%1 — %2").arg(item.seriesName, item.name);
        return QStringLiteral("%1 — %2 — %3").arg(item.seriesName, code, item.name);
    }
    return item.name;
}

// Emby-web card subtitle: year for movies, year-range for series, SxxEyy for episodes.
QString subtitleOf(const MediaItem &item)
{
    if (item.type == QLatin1String("Episode")) {
        QString label = episodeCode(item.parentIndexNumber, item.indexNumber);
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

QString navigationIdentity(const MediaItem &item)
{
    if (!item.playlistItemId.isEmpty())
        return QStringLiteral("p:%1").arg(item.playlistItemId);
    if (!item.id.isEmpty())
        return QStringLiteral("i:%1").arg(item.id);
    return {};
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
    return dataForItem(m_items[index.row()], role);
}

QVariant MediaItemModel::dataForItem(const MediaItem &item, int role)
{
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
    case PlayCountRole:
        return item.playCount;
    case FavoriteRole:
        return item.favorite;
    case ResumableRole:
        return item.isResumable();
    case PosterUrlRole: {
        // Every square in the app comes through here — grid card, mini player,
        // now-playing hero, queue row — so routing it through coverSource() is
        // what makes a track show its album's cover rather than a hole.
        const MediaItem::ImageRef ref = item.coverSource();
        if (ref.isValid())
            return embyImageSource(ref.itemId, ref.imageType, ref.tag);
        return item.probePrimaryImage
                   ? embyImageProbeSource(item.id, QStringLiteral("Primary"))
                   : QString();
    }
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
    return mediaRoleNames();
}

const QHash<int, QByteArray> &MediaItemModel::mediaRoleNames()
{
    static const QHash<int, QByteArray> roles = {
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
        {PlayCountRole, "playCount"},
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
    return roles;
}

void MediaItemModel::rebuildItemIndex()
{
    m_rowsByItemId.clear();
    m_rowsByItemId.reserve(m_items.size());
    m_rowByNavigationIdentity.clear();
    m_rowByNavigationIdentity.reserve(m_items.size());
    for (int row = 0; row < m_items.size(); ++row) {
        m_rowsByItemId[m_items.at(row).id].append(row);
        const QString identity = navigationIdentity(m_items.at(row));
        if (!identity.isEmpty() && !m_rowByNavigationIdentity.contains(identity))
            m_rowByNavigationIdentity.insert(identity, row);
    }
}

void MediaItemModel::setItems(QList<MediaItem> items, int totalRecordCount)
{
    const int oldCount = static_cast<int>(m_items.size());
    const int oldTotalRecordCount = m_totalRecordCount;
    const int newTotalRecordCount =
        totalRecordCount >= 0 ? totalRecordCount : static_cast<int>(items.size());

    beginResetModel();
    m_items = std::move(items);
    rebuildItemIndex();
    m_totalRecordCount = newTotalRecordCount;
    endResetModel();
    if (oldCount != static_cast<int>(m_items.size()))
        emit countChanged();
    if (oldTotalRecordCount != m_totalRecordCount)
        emit totalRecordCountChanged();
}

void MediaItemModel::appendItems(const QList<MediaItem> &items, int totalRecordCount)
{
    if (items.isEmpty())
        return;
    const int first = static_cast<int>(m_items.size());
    beginInsertRows(QModelIndex(), first, first + static_cast<int>(items.size()) - 1);
    m_items.append(items);
    for (int row = first; row < m_items.size(); ++row) {
        m_rowsByItemId[m_items.at(row).id].append(row);
        const QString identity = navigationIdentity(m_items.at(row));
        if (!identity.isEmpty() && !m_rowByNavigationIdentity.contains(identity))
            m_rowByNavigationIdentity.insert(identity, row);
    }
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
    const auto &roles = mediaRoleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        map.insert(QString::fromLatin1(it.value()), data(index, it.key()));
    return map;
}

int MediaItemModel::indexOfNavigationIdentity(const QString &identity) const
{
    return m_rowByNavigationIdentity.value(identity, -1);
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
    updateUserData(itemId, played, favorite, playbackPositionTicks, -1);
}

void MediaItemModel::updateUserData(const QString &itemId, bool played, bool favorite,
                                    qint64 playbackPositionTicks, int playCount)
{
    const auto rows = m_rowsByItemId.constFind(itemId);
    if (rows == m_rowsByItemId.cend())
        return;
    for (const int row : rows.value()) {
        m_items[row].played = played;
        m_items[row].favorite = favorite;
        if (playbackPositionTicks >= 0)
            m_items[row].playbackPositionTicks = playbackPositionTicks;
        if (playCount >= 0)
            m_items[row].playCount = playCount;
        const QModelIndex idx = index(row);
        emit dataChanged(idx, idx,
                         {PlayedRole, PlayCountRole, FavoriteRole, ResumableRole, ProgressRole,
                          PositionMsRole});
    }
}

} // namespace strmqt
