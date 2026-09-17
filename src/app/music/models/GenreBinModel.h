#pragma once

#include "app/music/models/MusicListModel.h"
#include "server/dto/music/MusicTypes.h"

namespace strmqt::music {

class GenreBinModel : public MusicListModel<GenreBin>
{
    Q_OBJECT

public:
    enum Role
    {
        IdRole = Qt::UserRole + 1,
        NameRole,
        TitleRole,
        RecordCountRole,
        SubtitleRole,
        CoversRole,
        CoverUrlRole,
        TypeRole,
    };

    explicit GenreBinModel(QObject *parent = nullptr) : MusicListModel<GenreBin>(parent) {}

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QVariantMap get(int row) const override;
    void applyUserData(const QString &, const UserDataPatch &) override {}

    static QVariant dataFor(const GenreBin &bin, int role);
};

} // namespace strmqt::music
