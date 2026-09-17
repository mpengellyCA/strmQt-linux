#pragma once

#include <QDateTime>
#include <QFuture>
#include <QJsonDocument>
#include <QObject>
#include <QRandomGenerator>
#include <QUrlQuery>

#include <functional>

#include "app/music/TtlCache.h"
#include "core/Result.h"
#include "server/dto/ItemsQuery.h"
#include "server/dto/music/MusicQuery.h"
#include "server/dto/music/MusicTypes.h"

namespace strmqt::emby {
class EmbyClient;
}

namespace strmqt::music {

enum class Freshness { Listening, Favourites, Everything };

// The only music unit that talks to Emby (Crate spec §3.3). Composes several
// requests into one music DTO, caches per account, and resolves on this
// object's thread. Core failures fail the result; secondary failures come back
// empty and are logged.
class MusicRepository : public QObject
{
    Q_OBJECT

public:
    explicit MusicRepository(emby::EmbyClient *client, QObject *parent = nullptr);

    QFuture<Result<QList<Track>>> albumTracks(const QString &albumId);
    QFuture<Result<AlbumSleeve>> albumSleeve(const QString &albumId);
    QFuture<Result<ArtistProfile>> artistProfile(const QString &libraryId, const QString &artistId);

    void clear();
    void setClockForTests(std::function<QDateTime()> clock);
    void setShuffleSeedForTests(quint32 seed);

    static QStringList albumFields();
    static QStringList trackFields();

private:
    QFuture<Result<QJsonDocument>> fetchItems(const ItemsQuery &query);
    QFuture<Result<QJsonDocument>> fetchItem(const QString &itemId);
    QFuture<Result<QList<Album>>> fetchAlbums(const ItemsQuery &query);
    QFuture<Result<QList<Track>>> fetchTracks(const ItemsQuery &query);
    QDateTime now() const;
    bool epochIs(quint64 epoch) const { return epoch == m_epoch; }

    emby::EmbyClient *m_client = nullptr;
    std::function<QDateTime()> m_clock;
    QRandomGenerator m_rng;
    quint64 m_epoch = 0;
    TtlCache<QList<Track>> m_trackCache{std::chrono::minutes(10)};
    TtlCache<AlbumSleeve> m_sleeveCache{std::chrono::minutes(10)};
};

} // namespace strmqt::music
