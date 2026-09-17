#pragma once

#include "app/music/models/MusicListModel.h"
#include "server/dto/music/MusicTypes.h"

namespace strmqt::music {

class ArtistGridModel : public MusicListModel<Artist>
{
    Q_OBJECT

public:
    enum Role
    {
        IdRole = Qt::UserRole + 1,
        NameRole,
        TitleRole,
        SubtitleRole,
        RecordCountRole,
        CoverUrlRole,
        PosterUrlRole,
        BackdropUrlRole,
        FavouriteRole,
        FavoriteRole,
        TypeRole,
    };

    explicit ArtistGridModel(QObject *parent = nullptr) : MusicListModel<Artist>(parent) {}

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QVariantMap get(int row) const override;
    void applyUserData(const QString &itemId, const UserDataPatch &patch) override;

    static QVariant dataFor(const Artist &artist, int role);
};

} // namespace strmqt::music
