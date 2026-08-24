#include "LibraryListModel.h"

#include "MediaItemModel.h"

namespace strmqt {

LibraryListModel::LibraryListModel(QObject *parent) : QAbstractListModel(parent) {}

int LibraryListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_libraries.size());
}

QVariant LibraryListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_libraries.size())
        return {};
    const Library &library = m_libraries[index.row()];

    switch (role) {
    case IdRole:
        return library.id;
    case NameRole:
        return library.name;
    case CollectionTypeRole:
        return library.collectionType;
    case ImageUrlRole:
        return embyImageSource(library.id, QStringLiteral("Primary"), library.primaryImageTag);
    default:
        return {};
    }
}

QHash<int, QByteArray> LibraryListModel::roleNames() const
{
    return {
        {IdRole, "libraryId"},
        {NameRole, "name"},
        {CollectionTypeRole, "collectionType"},
        {ImageUrlRole, "imageUrl"},
    };
}

QVariantMap LibraryListModel::get(int row) const
{
    QVariantMap map;
    const QModelIndex modelIndex = index(row);
    if (!modelIndex.isValid())
        return map;
    const auto roles = roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        map.insert(QString::fromLatin1(it.value()), data(modelIndex, it.key()));
    return map;
}

int LibraryListModel::indexOfNavigationIdentity(const QString &identity) const
{
    if (!identity.startsWith(QLatin1String("i:")))
        return -1;
    const QString id = identity.sliced(2);
    for (int row = 0; row < m_libraries.size(); ++row) {
        if (m_libraries.at(row).id == id)
            return row;
    }
    return -1;
}

void LibraryListModel::setLibraries(QList<Library> libraries)
{
    const int oldCount = static_cast<int>(m_libraries.size());
    beginResetModel();
    m_libraries = std::move(libraries);
    endResetModel();
    if (oldCount != static_cast<int>(m_libraries.size()))
        emit countChanged();
}

} // namespace strmqt
