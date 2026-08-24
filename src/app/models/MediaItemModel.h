#pragma once

#include "server/dto/MediaItem.h"

#include <QAbstractListModel>
#include <QList>
#include <QVariantMap>

namespace strmqt {

// List model over backend-neutral MediaItems for rails and grids. Image roles
// resolve to image://emby/... URLs handled by the async image provider, so the
// model has no network dependency.
class MediaItemModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int totalRecordCount READ totalRecordCount NOTIFY totalRecordCountChanged)

public:
    enum Role
    {
        IdRole = Qt::UserRole + 1,
        NameRole,
        LabelRole, // display label: "Series — S5E14 — Name" for episodes
        TypeRole,
        OverviewRole,
        SeriesNameRole,
        IndexNumberRole,
        ParentIndexNumberRole,
        YearRole,
        RuntimeMsRole,
        PositionMsRole,
        ProgressRole, // 0.0–1.0 for progress bars
        PlayedRole,
        // Times the user has finished this item. Carried as a role so it
        // survives the trip through QML into the queue, where MPRIS reads it as
        // xesam:useCount.
        PlayCountRole,
        FavoriteRole,
        ResumableRole,
        PosterUrlRole,
        BackdropUrlRole,
        // Best 16:9 art for a wide card, or empty when the item has none.
        ThumbUrlRole,
        // ISO-8601 air/release date, empty unless the query asked for it.
        PremiereDateRole,
        // "Continuing" | "Ended" | "" — series only.
        StatusRole,
        // Playlist entry id; empty outside a playlist.
        PlaylistItemIdRole,
        // Music
        ArtistsRole,      // QStringList of performer names
        ArtistIdsRole,    // their ids, same order
        AlbumArtistRole,
        AlbumRole,
        AlbumIdRole,
        ChildCountRole,   // tracks on an album, episodes in a season
        CommunityRatingRole,
        OfficialRatingRole,
        UnplayedCountRole,
        SubtitleRole, // "1999" / "2013 – Present" / "S2E4 · 47 min"
    };
    Q_ENUM(Role)

    explicit MediaItemModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setItems(QList<MediaItem> items, int totalRecordCount = -1);
    // `totalRecordCount` is the count the appended page itself reported; -1
    // means "the caller has none". A count arriving with a page is newer than
    // the one setItems() recorded, so it wins; and either way the total ends up
    // at least the number of rows now held, because canLoadMore() is
    // `rowCount < totalRecordCount` and a stale total left behind by a growing
    // library reads as "there is nothing more" while a page is already sitting
    // in the model unpaged.
    //
    // The floor is NOT what rescues /Persons and /Genres, which report
    // TotalRecordCount = 0 while returning rows: setItems() records that 0, so
    // canLoadMore() is already false and appendItems() is never reached. Those
    // callers have to use the returned list's own size (§ server behaviours),
    // and this floor cannot substitute for that.
    void appendItems(const QList<MediaItem> &items, int totalRecordCount = -1);
    void clear();

    int totalRecordCount() const { return m_totalRecordCount; }
    const QList<MediaItem> &items() const { return m_items; }

    // Convenience for pushing a whole item into a Details page.
    Q_INVOKABLE QVariantMap get(int row) const;

    // In-place user-data update after a played/favorite toggle; no-op when the
    // item is not in this model.
    void updateUserData(const QString &itemId, bool played, bool favorite);
    // Overload carrying the resume position. The server's UserDataChanged event
    // delivers PlaybackPositionTicks, so watching something on a phone can move
    // the progress bar here — the three-argument form throws that away and
    // leaves a stale bar under a correct watched state.
    void updateUserData(const QString &itemId, bool played, bool favorite,
                        qint64 playbackPositionTicks);
    // Complete UserDataChanged record. Negative optional values mean "leave
    // unchanged" for local optimistic toggles that do not carry those fields.
    void updateUserData(const QString &itemId, bool played, bool favorite,
                        qint64 playbackPositionTicks, int playCount);

signals:
    void countChanged();
    void totalRecordCountChanged();

private:
    QList<MediaItem> m_items;
    int m_totalRecordCount = 0;
};

// Builds the image://emby/... source for an item image; empty when there is no image.
QString embyImageSource(const QString &itemId, const QString &imageType, const QString &tag);

} // namespace strmqt
