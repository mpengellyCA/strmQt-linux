#pragma once

#include "app/music/models/MusicListModel.h"
#include "server/dto/music/MusicTypes.h"

namespace strmqt::music {

class PlaylistGridModel : public MusicListModel<Playlist>
{
    Q_OBJECT

public:
    enum Role
    {
        IdRole = Qt::UserRole + 1,
        NameRole,
        TitleRole,
        SubtitleRole,
        TrackCountRole,
        CoverUrlRole,
        PosterUrlRole,
        TypeRole,
    };

    explicit PlaylistGridModel(QObject *parent = nullptr) : MusicListModel<Playlist>(parent) {}

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QVariantMap get(int row) const override;
    void applyUserData(const QString &itemId, const UserDataPatch &patch) override;

    static QVariant dataFor(const Playlist &playlist, int role);
};

} // namespace strmqt::music
