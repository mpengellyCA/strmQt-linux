#include "app/music/models/StationModel.h"

#include "app/music/MusicFormat.h"

namespace strmqt::music {

QString StationModel::kindKey(StationKind kind)
{
    switch (kind) {
    case StationKind::HeavyRotation: return QStringLiteral("heavyRotation");
    case StationKind::Favourites: return QStringLiteral("favourites");
    case StationKind::DeepCuts: return QStringLiteral("deepCuts");
    case StationKind::MoreLike: return QStringLiteral("moreLike");
    case StationKind::ShuffleAll: break;
    }
    return QStringLiteral("shuffleAll");
}

std::optional<StationKind> StationModel::kindFromKey(const QString &key)
{
    for (StationKind kind : {StationKind::HeavyRotation, StationKind::Favourites, StationKind::DeepCuts,
                             StationKind::MoreLike, StationKind::ShuffleAll}) {
        if (kindKey(kind) == key)
            return kind;
    }
    return std::nullopt;
}

void StationModel::setStations(QList<Station> stations)
{
    beginResetModel();
    m_stations = std::move(stations);
    endResetModel();
    rebuildIndex();
    setTotal(size());
    emit countChanged();
}

std::optional<Station> StationModel::stationFor(const QString &key) const
{
    for (const Station &station : m_stations) {
        if (kindKey(station.kind) == key)
            return station;
    }
    return std::nullopt;
}

QHash<int, QByteArray> StationModel::roleNames() const
{
    static const QHash<int, QByteArray> names{{IdRole, "itemId"}, {KindRole, "kind"}, {LabelRole, "label"},
                                              {NameRole, "name"}, {SeedIdRole, "seedId"}, {CoversRole, "covers"}};
    return names;
}

QVariant StationModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= size())
        return {};
    const Station &station = m_stations.at(index.row());
    switch (role) {
    case IdRole:
    case KindRole:
        return kindKey(station.kind);
    case LabelRole:
    case NameRole:
        return station.label;
    case SeedIdRole:
        return station.seedId;
    case CoversRole: {
        QStringList urls;
        for (const ImageRef &ref : station.covers)
            urls.append(coverUrl(ref));
        return urls;
    }
    default:
        return {};
    }
}

QVariantMap StationModel::get(int row) const
{
    if (row < 0 || row >= size())
        return {};
    QVariantMap map;
    const auto names = roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it)
        map.insert(QString::fromLatin1(it.value()), data(index(row), it.key()));
    return map;
}

} // namespace strmqt::music
