#pragma once

#include <optional>

#include "app/music/models/MusicModelBase.h"
#include "server/dto/music/MusicTypes.h"

namespace strmqt::music {

class StationModel : public MusicModelBase
{
    Q_OBJECT

public:
    enum Role
    {
        IdRole = Qt::UserRole + 1,
        KindRole,
        LabelRole,
        NameRole,
        SeedIdRole,
        CoversRole,
    };

    explicit StationModel(QObject *parent = nullptr) : MusicModelBase(parent) {}

    void setStations(QList<Station> stations);
    const QList<Station> &stations() const { return m_stations; }
    std::optional<Station> stationFor(const QString &key) const;

    static QString kindKey(StationKind kind);
    static std::optional<StationKind> kindFromKey(const QString &key);

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QVariantMap get(int row) const override;
    void applyUserData(const QString &, const UserDataPatch &) override {}

protected:
    int size() const override { return static_cast<int>(m_stations.size()); }
    QString idOf(int row) const override { return kindKey(m_stations.at(row).kind); }

private:
    QList<Station> m_stations;
};

} // namespace strmqt::music
