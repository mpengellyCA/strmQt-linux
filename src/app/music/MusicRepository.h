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

struct NewAlbums
{
    QList<Album> albums;
    int addedThisWeek = -1; // -1: the server cannot say
};

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
    // A random draw of the artist's tracks everywhere they perform (ArtistIds),
    // for ⇄ Shuffle artist. SortBy=Random cannot page, so this is one request,
    // and it is never cached: each press is a new draw.
    QFuture<Result<QList<Track>>> artistTracks(const QString &artistId, int limit = 200);

    QFuture<Result<ContinueListening>> continueListening(const QString &libraryId);
    QFuture<Result<QList<Album>>> recentAlbums(const QString &libraryId, int limit = 20);
    QFuture<Result<NewAlbums>> newAlbums(const QString &libraryId, int limit = 20);
    QFuture<Result<QList<GenreBin>>> allGenres(const QString &libraryId);
    QFuture<Result<Page<GenreBin>>> genreBins(const QString &libraryId, int limit);
    // Samples up to three album covers for every bin that has none (session
    // cache per genre). Order is kept; a failed sample leaves that bin bare.
    // T10/P3-R1 epoch ruling: refuses to resolve covers into a session
    // different from the one that started this call.
    QFuture<Result<QList<GenreBin>>> coverGenres(const QString &libraryId, QList<GenreBin> genres);
    QFuture<Result<QList<Artist>>> topArtists(const QString &libraryId, int limit = 20);
    QFuture<Result<QList<Album>>> forgottenFavourites(const QString &libraryId, int limit = 20);
    QFuture<Result<QList<Album>>> randomAlbums(const QString &libraryId, int limit = 20);
    static QList<Station> stations(const QList<Album> &coverPool, const Artist &topArtist);
    QFuture<Result<QList<Track>>> resolveStation(const QString &libraryId, const Station &station);
    void markStale(Freshness freshness);

    QFuture<Result<Page<Album>>> browseAlbums(const MusicQuery &query, int startIndex, int limit);
    QFuture<Result<Page<Artist>>> browseArtists(const MusicQuery &query, int startIndex, int limit);
    QFuture<Result<Page<Track>>> browseTracks(const MusicQuery &query, int startIndex, int limit);
    QFuture<Result<Page<Playlist>>> browsePlaylists(const MusicQuery &query, int startIndex, int limit);
    QFuture<Result<QList<Track>>> sampleTracks(const MusicQuery &query, int limit = 200);
    void noteUserDataChanged(const QString &itemId);

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
    // requireCompleted selects Filters=IsPlayed. Ruling P1-9: the continue-
    // listening hero query (limit 1) must NOT require a completed play — a
    // partially played, never-finished track still has to be able to seed it.
    // Recent albums / top artists keep the filter (default true).
    QFuture<Result<QList<Track>>> playedHistory(const QString &libraryId, int limit,
                                                bool requireCompleted = true);
    QFuture<Result<QList<GenreBin>>> genreCountsFromAlbums(const QString &libraryId,
                                                           QList<GenreBin> genres);
    QFuture<Result<QList<ImageRef>>> genreCovers(const QString &libraryId, const QString &genreId);
    QDateTime now() const;
    bool epochIs(quint64 epoch) const { return epoch == m_epoch; }

    emby::EmbyClient *m_client = nullptr;
    std::function<QDateTime()> m_clock;
    QRandomGenerator m_rng;
    quint64 m_epoch = 0;
    TtlCache<QList<Track>> m_trackCache{std::chrono::minutes(10)};
    TtlCache<AlbumSleeve> m_sleeveCache{std::chrono::minutes(10)};
    TtlCache<ContinueListening> m_continueCache{std::chrono::minutes(5)};
    TtlCache<QList<Album>> m_albumListCache{std::chrono::minutes(5)};
    TtlCache<NewAlbums> m_newCache{std::chrono::minutes(5)};
    TtlCache<QList<Artist>> m_artistListCache{std::chrono::minutes(5)};
    TtlCache<QList<GenreBin>> m_genreCache{std::chrono::minutes(5)};
    // perf-fix-b (2026-09-16): was milliseconds(-1) (never expires), so a
    // 289-genre library (the owner's) pinned 289 cover-sample entries for the
    // life of the process. 5 minutes matches m_genreCache and the other
    // home:*/lib:* list caches: covers:<genreId> is catalog data of the same
    // kind and staleness tolerance as the genre bins it decorates, so it
    // should invalidate on the same clock rather than never.
    TtlCache<QList<ImageRef>> m_coverCache{std::chrono::minutes(5)};
};

} // namespace strmqt::music
