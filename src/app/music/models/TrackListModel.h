#pragma once

#include "app/music/models/MusicListModel.h"
#include "server/dto/MediaItem.h"
#include "server/dto/music/MusicTypes.h"

namespace strmqt::music {

// Tracks for TrackTable-based surfaces. Serves MediaItemModel's roles unchanged
// (TrackTable, ItemMenu and the queue read them) plus the music display roles.
class TrackListModel : public MusicListModel<Track>
{
    Q_OBJECT

public:
    enum MusicRole
    {
        DisplayTitleRole = Qt::UserRole + 200,
        FeaturedTextRole,
        ArtistTextRole,
        AlbumTitleRole,
        DiscNumberRole,
        TrackNumberRole,
        DurationTextRole,
        FormatBadgeRole,
        IsHiResRole,
        FavouriteRole,
        CoverUrlRole,
        DiffersFromAlbumArtistRole,
    };

    explicit TrackListModel(QObject *parent = nullptr) : MusicListModel<Track>(parent) {}

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QVariantMap get(int row) const override;
    void applyUserData(const QString &itemId, const UserDataPatch &patch) override;
    Q_INVOKABLE QVariantList mediaMaps() const;

protected:
    QString playlistItemIdOf(int row) const override { return m_items.at(row).playlistItemId; }
    void onItemsReset() override;
    void onItemsAppended(int fromRow) override;

private:
    static const QHash<int, QByteArray> &musicRoleNames();
    QVariant musicData(const Track &track, int role) const;

    QList<MediaItem> m_media;
};

} // namespace strmqt::music
