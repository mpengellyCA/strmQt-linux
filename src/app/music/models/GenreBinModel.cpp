#include "app/music/models/GenreBinModel.h"

#include "app/music/MusicFormat.h"

namespace strmqt::music {

QHash<int, QByteArray> GenreBinModel::roleNames() const
{
    static const QHash<int, QByteArray> names{
        {IdRole, "itemId"}, {NameRole, "name"}, {TitleRole, "title"}, {RecordCountRole, "recordCount"},
        {SubtitleRole, "subtitle"}, {CoversRole, "covers"}, {CoverUrlRole, "coverUrl"}, {TypeRole, "type"}};
    return names;
}

QVariant GenreBinModel::dataFor(const GenreBin &bin, int role)
{
    switch (role) {
    case IdRole:
        return bin.id;
    case NameRole:
    case TitleRole:
        return bin.name;
    case RecordCountRole:
        return bin.recordCount;
    case SubtitleRole:
        return formatRecordCount(bin.recordCount);
    case CoversRole: {
        QStringList urls;
        for (const ImageRef &ref : bin.covers)
            urls.append(coverUrl(ref));
        return urls;
    }
    case CoverUrlRole:
        return bin.covers.isEmpty() ? QString() : coverUrl(bin.covers.first());
    case TypeRole:
        return QStringLiteral("MusicGenre");
    default:
        return {};
    }
}

QVariant GenreBinModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= size())
        return {};
    return dataFor(m_items.at(index.row()), role);
}

QVariantMap GenreBinModel::get(int row) const
{
    if (row < 0 || row >= size())
        return {};
    QVariantMap map;
    const auto names = roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it)
        map.insert(QString::fromLatin1(it.value()), dataFor(m_items.at(row), it.key()));
    return map;
}

} // namespace strmqt::music
