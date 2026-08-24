#include "SearchSectionModel.h"

namespace strmqt {

SearchSectionModel::SearchSectionModel(Section section, MediaItemModel *source, QObject *parent)
    : QSortFilterProxyModel(parent), m_section(section), m_source(source)
{
    setDynamicSortFilter(true);
    setSourceModel(source);

    // QAbstractItemModel has no count property. Publish every structural change
    // that can alter a section's QML-visible row count; resets also matter when
    // the source changes taxonomy while keeping the same total cardinality.
    connect(this, &QAbstractItemModel::modelReset, this, &SearchSectionModel::countChanged);
    connect(this, &QAbstractItemModel::rowsInserted, this, &SearchSectionModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &SearchSectionModel::countChanged);
}

QVariant SearchSectionModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};

    switch (role) {
    case SourceRowRole:
        return mapToSource(index).row();
    case ArtistTextRole: {
        const QStringList artists =
            QSortFilterProxyModel::data(index, MediaItemModel::ArtistsRole).toStringList();
        if (!artists.isEmpty())
            return artists.join(QStringLiteral(", "));
        return QSortFilterProxyModel::data(index, MediaItemModel::AlbumArtistRole);
    }
    case AlbumTextRole:
        return QSortFilterProxyModel::data(index, MediaItemModel::AlbumRole);
    default:
        return QSortFilterProxyModel::data(index, role);
    }
}

QHash<int, QByteArray> SearchSectionModel::roleNames() const
{
    QHash<int, QByteArray> roles = MediaItemModel::mediaRoleNames();
    roles.insert(SourceRowRole, QByteArrayLiteral("sourceRow"));
    roles.insert(ArtistTextRole, QByteArrayLiteral("artistText"));
    roles.insert(AlbumTextRole, QByteArrayLiteral("albumText"));
    return roles;
}

QVariantMap SearchSectionModel::get(int row) const
{
    QVariantMap record;
    const QModelIndex proxyIndex = index(row, 0);
    if (!proxyIndex.isValid())
        return record;

    const QHash<int, QByteArray> roles = roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        record.insert(QString::fromLatin1(it.value()), data(proxyIndex, it.key()));
    return record;
}

int SearchSectionModel::sourceRow(int row) const
{
    const QModelIndex proxyIndex = index(row, 0);
    return proxyIndex.isValid() ? mapToSource(proxyIndex).row() : -1;
}

int SearchSectionModel::indexOfNavigationIdentity(const QString &identity) const
{
    const int row = m_source->indexOfNavigationIdentity(identity);
    if (row < 0)
        return -1;
    const QModelIndex proxyIndex = mapFromSource(m_source->index(row, 0));
    return proxyIndex.isValid() ? proxyIndex.row() : -1;
}

bool SearchSectionModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QModelIndex sourceIndex = m_source->index(sourceRow, 0, sourceParent);
    const QString type = m_source->data(sourceIndex, MediaItemModel::TypeRole).toString();
    return typeBelongsToSection(type, m_section);
}

bool SearchSectionModel::isKnownType(const QString &type)
{
    return type == QLatin1String("Movie") || type == QLatin1String("Series") ||
           type == QLatin1String("Episode") || type == QLatin1String("BoxSet") ||
           type == QLatin1String("MusicArtist") || type == QLatin1String("MusicAlbum") ||
           type == QLatin1String("Audio");
}

bool SearchSectionModel::typeBelongsToSection(const QString &type, Section section)
{
    switch (section) {
    case Section::Movies:
        return type == QLatin1String("Movie");
    case Section::Series:
        return type == QLatin1String("Series");
    case Section::Episodes:
        return type == QLatin1String("Episode");
    case Section::Collections:
        return type == QLatin1String("BoxSet");
    case Section::Artists:
        return type == QLatin1String("MusicArtist");
    case Section::Albums:
        return type == QLatin1String("MusicAlbum");
    case Section::Tracks:
        return type == QLatin1String("Audio");
    case Section::Other:
        return !isKnownType(type);
    }
    return false;
}

} // namespace strmqt
