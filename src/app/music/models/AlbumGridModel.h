#pragma once

#include "app/music/models/MusicListModel.h"
#include "server/dto/music/MusicTypes.h"

namespace strmqt::music {

class AlbumGridModel : public MusicListModel<Album>
{
    Q_OBJECT

public:
    enum Role
    {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        NameRole,
        ArtistRole,
        ArtistIdRole,
        SubtitleRole,
        YearRole,
        ReleaseTypeRole,
        ReleaseBadgeRole,
        CoverUrlRole,
        PosterUrlRole,
        FormatBadgeRole,
        TrackCountRole,
        DurationTextRole,
        FavouriteRole,
        FavoriteRole,
        PlayCountRole,
        TypeRole,
    };

    explicit AlbumGridModel(QObject *parent = nullptr) : MusicListModel<Album>(parent) {}

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QVariantMap get(int row) const override;
    void applyUserData(const QString &itemId, const UserDataPatch &patch) override;

    static QVariant dataFor(const Album &album, int role);
};

} // namespace strmqt::music
