#include "app/music/MusicRepository.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QPromise>
#include <QSet>

#include <algorithm>
#include <memory>

#include "app/music/Fanout.h"
#include "core/Log.h"
#include "server/emby/EmbyClient.h"
#include "server/emby/EmbyMusicMapper.h"
#include "server/emby/MusicServerCapabilities.h"

namespace strmqt::music {

namespace {

template<class T>
QFuture<Result<T>> ready(Result<T> result)
{
    return QtFuture::makeReadyValueFuture(std::move(result));
}

template<class T>
struct Pending
{
    std::shared_ptr<QPromise<Result<T>>> promise = std::make_shared<QPromise<Result<T>>>();
    Pending() { promise->start(); }
    QFuture<Result<T>> future() const { return promise->future(); }
    void resolve(Result<T> result) const
    {
        promise->addResult(std::move(result));
        promise->finish();
    }
};

QJsonArray itemsOf(const QJsonDocument &doc)
{
    return doc.object().value(QStringLiteral("Items")).toArray();
}

const QString kAudio = QStringLiteral("Audio");
const QString kAlbum = QStringLiteral("MusicAlbum");

template<class T>
QList<T> orderedByIds(const QList<T> &items, const QStringList &ids)
{
    QHash<QString, T> byId;
    for (const T &item : items)
        byId.insert(item.id, item);
    QList<T> ordered;
    for (const QString &id : ids) {
        if (const auto it = byId.constFind(id); it != byId.cend())
            ordered.append(*it);
    }
    return ordered;
}

ItemsQuery libraryQuery(const QString &libraryId, const QString &type)
{
    ItemsQuery query;
    query.parentId = libraryId;
    query.includeItemTypes = {type};
    query.recursive = true;
    return query;
}

} // namespace

MusicRepository::MusicRepository(emby::EmbyClient *client, QObject *parent)
    : QObject(parent), m_client(client), m_rng(QRandomGenerator::securelySeeded())
{
    connect(m_client, &emby::EmbyClient::identityChanged, this, &MusicRepository::clear);
}

QStringList MusicRepository::albumFields()
{
    return {QStringLiteral("ChildCount"), QStringLiteral("DateCreated"), QStringLiteral("PremiereDate"),
            QStringLiteral("ProductionYear"), QStringLiteral("Genres"), QStringLiteral("Studios"),
            QStringLiteral("CumulativeRunTimeTicks")};
}

QStringList MusicRepository::trackFields()
{
    // Ruling M1 (measured live): list queries return PlayCount 0 and no
    // LastPlayedDate unless explicitly asked for these two fields.
    return {emby::caps::kMediaStreamsOnLists ? QStringLiteral("MediaStreams")
                                             : QStringLiteral("MediaSources"),
            QStringLiteral("DateCreated"), QStringLiteral("UserDataPlayCount"),
            QStringLiteral("UserDataLastPlayedDate")};
}

void MusicRepository::clear()
{
    ++m_epoch;
    m_trackCache.clear();
    m_sleeveCache.clear();
    m_continueCache.clear();
    m_albumListCache.clear();
    m_newCache.clear();
    m_artistListCache.clear();
    m_genreCache.clear();
    m_coverCache.clear();
}

void MusicRepository::setClockForTests(std::function<QDateTime()> clock)
{
    m_clock = std::move(clock);
}

void MusicRepository::setShuffleSeedForTests(quint32 seed)
{
    m_rng.seed(seed);
}

QDateTime MusicRepository::now() const
{
    return m_clock ? m_clock() : QDateTime::currentDateTimeUtc();
}

QFuture<Result<QJsonDocument>> MusicRepository::fetchItems(const ItemsQuery &query)
{
    return m_client->getJson(QStringLiteral("/Users/{uid}/Items"), emby::EmbyClient::itemsParams(query));
}

QFuture<Result<QJsonDocument>> MusicRepository::fetchItem(const QString &itemId)
{
    return m_client->getJson(QStringLiteral("/Users/{uid}/Items/%1").arg(itemId), {});
}

QFuture<Result<QList<Album>>> MusicRepository::fetchAlbums(const ItemsQuery &query)
{
    return fetchItems(query).then(this, [](Result<QJsonDocument> result) {
        if (!result.ok())
            return Result<QList<Album>>::failure(result.error);
        return Result<QList<Album>>::success(emby::parseAlbums(itemsOf(result.value)));
    });
}

QFuture<Result<QList<Track>>> MusicRepository::fetchTracks(const ItemsQuery &query)
{
    return fetchItems(query).then(this, [](Result<QJsonDocument> result) {
        if (!result.ok())
            return Result<QList<Track>>::failure(result.error);
        return Result<QList<Track>>::success(emby::parseTracks(itemsOf(result.value)));
    });
}

QFuture<Result<QList<Track>>> MusicRepository::albumTracks(const QString &albumId)
{
    const QString key = QStringLiteral("tracks:") + albumId;
    if (auto cached = m_trackCache.get(key, now()))
        return ready(Result<QList<Track>>::success(*cached));

    ItemsQuery query;
    query.parentId = albumId;
    query.includeItemTypes = {kAudio};
    query.recursive = true;
    query.sortBy = QStringLiteral("ParentIndexNumber,IndexNumber,SortName");
    query.fields = trackFields();
    query.limit = 1000;
    const quint64 epoch = m_epoch;
    return fetchTracks(query).then(this, [this, key, epoch](Result<QList<Track>> result) {
        if (result.ok() && epochIs(epoch))
            m_trackCache.put(key, result.value, now());
        return result;
    });
}

QFuture<Result<AlbumSleeve>> MusicRepository::albumSleeve(const QString &albumId)
{
    const QString key = QStringLiteral("sleeve:") + albumId;
    if (auto cached = m_sleeveCache.get(key, now()))
        return ready(Result<AlbumSleeve>::success(*cached));

    struct State
    {
        Result<Album> album;
        Result<QList<Track>> tracks;
    };
    auto state = std::make_shared<State>();
    Pending<AlbumSleeve> pending;
    const quint64 epoch = m_epoch;

    auto fan = Fanout::create(this, [this, state, pending, albumId, key, epoch] {
        if (!state->album.ok())
            return pending.resolve(Result<AlbumSleeve>::failure(state->album.error));
        if (!state->tracks.ok())
            return pending.resolve(Result<AlbumSleeve>::failure(state->tracks.error));

        auto sleeve = std::make_shared<AlbumSleeve>();
        sleeve->album = state->album.value;
        emby::refineAlbumFromTracks(sleeve->album, state->tracks.value);
        sleeve->discs = emby::groupDiscs(state->tracks.value);

        auto finish = [this, sleeve, pending, key, epoch] {
            if (epochIs(epoch))
                m_sleeveCache.put(key, *sleeve, now());
            pending.resolve(Result<AlbumSleeve>::success(*sleeve));
        };
        const QString artistId =
            sleeve->album.albumArtists.isEmpty() ? QString() : sleeve->album.albumArtists.first().id;
        // Review fix (Task 10): if the identity changed while the core fetch was
        // in flight, don't attach the new account's "more by" list to a sleeve
        // built under the old one — just finish without fetching it.
        if (artistId.isEmpty() || !epochIs(epoch)) {
            finish();
            return;
        }

        ItemsQuery more;
        more.albumArtistIds = {artistId};
        more.includeItemTypes = {kAlbum};
        more.recursive = true;
        more.sortBy = QStringLiteral("ProductionYear,PremiereDate,SortName");
        more.sortDescending = true;
        more.fields = albumFields();
        more.limit = 13;
        fetchAlbums(more).then(this, [sleeve, albumId, finish](Result<QList<Album>> result) {
            if (!result.ok()) {
                qCWarning(logApp) << "music: more-by failed for album" << albumId << result.error;
            } else {
                for (const Album &album : std::as_const(result.value)) {
                    if (album.id != albumId && sleeve->moreByArtist.size() < 12)
                        sleeve->moreByArtist.append(album);
                }
            }
            finish();
        });
    });

    fan->add<Album>(fetchItem(albumId).then(this, [](Result<QJsonDocument> result) {
        if (!result.ok())
            return Result<Album>::failure(result.error);
        return Result<Album>::success(emby::parseAlbum(result.value.object()));
    }), [state](Result<Album> result) { state->album = std::move(result); });
    fan->add<QList<Track>>(albumTracks(albumId),
                           [state](Result<QList<Track>> result) { state->tracks = std::move(result); });
    fan->seal();
    return pending.future();
}

QFuture<Result<ArtistProfile>> MusicRepository::artistProfile(const QString &libraryId,
                                                              const QString &artistId)
{
    struct State
    {
        Result<Artist> artist;
        QList<Album> filed;
        QList<Album> appearing;
        QList<Track> top;
        QList<Artist> similar;
    };
    auto state = std::make_shared<State>();
    Pending<ArtistProfile> pending;

    auto fan = Fanout::create(this, [state, pending] {
        if (!state->artist.ok())
            return pending.resolve(Result<ArtistProfile>::failure(state->artist.error));
        ArtistProfile profile;
        profile.artist = state->artist.value;
        QSet<QString> filedIds;
        for (const Album &album : std::as_const(state->filed)) {
            filedIds.insert(album.id);
            if (album.releaseType == ReleaseType::EP || album.releaseType == ReleaseType::Single)
                profile.epsAndSingles.append(album);
            else
                profile.albums.append(album);
        }
        for (const Album &album : std::as_const(state->appearing)) {
            if (!filedIds.contains(album.id))
                profile.appearsOn.append(album);
        }
        profile.topTracks = state->top;
        profile.similar = state->similar;
        pending.resolve(Result<ArtistProfile>::success(profile));
    });

    auto secondary = [artistId](const char *what) {
        return [artistId, what](const QString &error) {
            qCWarning(logApp) << "music: artist" << what << "failed for" << artistId << error;
        };
    };

    fan->add<Artist>(fetchItem(artistId).then(this, [](Result<QJsonDocument> result) {
        if (!result.ok())
            return Result<Artist>::failure(result.error);
        return Result<Artist>::success(emby::parseArtist(result.value.object()));
    }), [state](Result<Artist> result) { state->artist = std::move(result); });

    ItemsQuery albums;
    albums.parentId = libraryId;
    albums.includeItemTypes = {kAlbum};
    albums.recursive = true;
    albums.sortBy = QStringLiteral("ProductionYear,PremiereDate,SortName");
    albums.sortDescending = true;
    albums.fields = albumFields();
    albums.limit = 200;

    ItemsQuery filed = albums;
    filed.albumArtistIds = {artistId};
    fan->add<QList<Album>>(fetchAlbums(filed), [state, log = secondary("albums")](Result<QList<Album>> r) {
        if (r.ok())
            state->filed = r.value;
        else
            log(r.error);
    });

    ItemsQuery appearing = albums;
    appearing.artistIds = {artistId};
    fan->add<QList<Album>>(fetchAlbums(appearing), [state, log = secondary("appears-on")](Result<QList<Album>> r) {
        if (r.ok())
            state->appearing = r.value;
        else
            log(r.error);
    });

    ItemsQuery top;
    top.parentId = libraryId;
    top.includeItemTypes = {kAudio};
    top.recursive = true;
    top.artistIds = {artistId};
    top.sortBy = QStringLiteral("PlayCount,SortName");
    top.sortDescending = true;
    top.fields = trackFields();
    top.limit = 5;
    fan->add<QList<Track>>(fetchTracks(top), [state, log = secondary("top tracks")](Result<QList<Track>> r) {
        if (r.ok())
            state->top = r.value;
        else
            log(r.error);
    });

    if (emby::caps::kSimilarArtists) {
        QUrlQuery params;
        params.addQueryItem(QStringLiteral("UserId"), QStringLiteral("{uid}"));
        params.addQueryItem(QStringLiteral("Limit"), QStringLiteral("8"));
        const QString path = QString::fromLatin1(emby::caps::kSimilarArtistsPath)
                                 .replace(QStringLiteral("{id}"), artistId);
        fan->add<QJsonDocument>(m_client->getJson(path, params),
                                [state, log = secondary("similar")](Result<QJsonDocument> r) {
                                    if (r.ok())
                                        state->similar = emby::parseArtists(itemsOf(r.value));
                                    else
                                        log(r.error);
                                });
    }
    fan->seal();
    return pending.future();
}

void MusicRepository::markStale(Freshness freshness)
{
    switch (freshness) {
    case Freshness::Listening:
        m_continueCache.markStale(QStringLiteral("home:"));
        m_albumListCache.markStale(QStringLiteral("home:"));
        m_artistListCache.markStale(QStringLiteral("home:"));
        break;
    case Freshness::Favourites:
        m_albumListCache.markStale(QStringLiteral("fav:"));
        break;
    case Freshness::Everything:
        m_trackCache.markStale();
        m_sleeveCache.markStale();
        m_continueCache.markStale();
        m_albumListCache.markStale();
        m_newCache.markStale();
        m_artistListCache.markStale();
        m_genreCache.markStale();
        break;
    }
}

QFuture<Result<QList<Track>>> MusicRepository::playedHistory(const QString &libraryId, int limit,
                                                              bool requireCompleted)
{
    ItemsQuery query = libraryQuery(libraryId, kAudio);
    query.sortBy = QStringLiteral("DatePlayed");
    query.sortDescending = true;
    // Ruling P1-9: only recent/top/history queries require a completed play.
    // The continue-listening hero query passes requireCompleted=false so a
    // partially played, never-finished track can still seed it.
    if (requireCompleted)
        query.filters = {QStringLiteral("IsPlayed")};
    // Ruling M1 (fix round 1): exactly these three fields. Not trackFields() —
    // that also adds MediaStreams/MediaSources, which none of this method's
    // callers (continueListening, recentAlbums, topArtists) read; asking for
    // it on 200/500-row queries would be pure waste.
    query.fields = {QStringLiteral("DateCreated"), QStringLiteral("UserDataPlayCount"),
                    QStringLiteral("UserDataLastPlayedDate")};
    query.limit = limit;
    return fetchTracks(query);
}

QFuture<Result<ContinueListening>> MusicRepository::continueListening(const QString &libraryId)
{
    const QString key = QStringLiteral("home:continue:") + libraryId;
    if (auto cached = m_continueCache.get(key, now()))
        return ready(Result<ContinueListening>::success(*cached));

    Pending<ContinueListening> pending;
    const quint64 epoch = m_epoch;
    playedHistory(libraryId, 1, /*requireCompleted=*/false)
        .then(this, [this, pending, key, epoch](Result<QList<Track>> history) {
        if (!history.ok())
            return pending.resolve(Result<ContinueListening>::failure(history.error));
        if (history.value.isEmpty() || history.value.first().albumId.isEmpty()
            || !history.value.first().lastPlayed.isValid()) {
            return pending.resolve(Result<ContinueListening>::success({}));
        }
        const Track last = history.value.first();
        // T10 epoch ruling: don't fetch the album/tracks for the hero under a
        // different identity than the one that started this request.
        if (!epochIs(epoch))
            return pending.resolve(Result<ContinueListening>::failure(QStringLiteral("request canceled")));

        struct State
        {
            Result<Album> album;
            Result<QList<Track>> tracks;
        };
        auto state = std::make_shared<State>();
        auto fan = Fanout::create(this, [this, state, pending, last, key, epoch] {
            if (!state->album.ok())
                return pending.resolve(Result<ContinueListening>::failure(state->album.error));
            if (!state->tracks.ok())
                return pending.resolve(Result<ContinueListening>::failure(state->tracks.error));
            const QList<Track> &tracks = state->tracks.value;
            if (tracks.isEmpty())
                return pending.resolve(Result<ContinueListening>::success({}));

            int index = 0;
            for (int i = 0; i < tracks.size(); ++i) {
                if (tracks.at(i).id == last.id)
                    index = i;
            }
            const bool partial = last.positionMs > 0 && !last.played;
            int resume = partial ? index : index + 1;
            if (resume >= tracks.size())
                resume = 0;

            ContinueListening result;
            result.album = state->album.value;
            emby::refineAlbumFromTracks(result.album, tracks);
            result.resumeIndex = resume;
            result.resumeTrack = tracks.at(resume);
            result.progress = double(resume) / double(tracks.size());
            if (epochIs(epoch))
                m_continueCache.put(key, result, now());
            pending.resolve(Result<ContinueListening>::success(result));
        });
        fan->add<Album>(fetchItem(last.albumId).then(this, [](Result<QJsonDocument> r) {
            if (!r.ok())
                return Result<Album>::failure(r.error);
            return Result<Album>::success(emby::parseAlbum(r.value.object()));
        }), [state](Result<Album> r) { state->album = std::move(r); });
        fan->add<QList<Track>>(albumTracks(last.albumId),
                               [state](Result<QList<Track>> r) { state->tracks = std::move(r); });
        fan->seal();
    });
    return pending.future();
}

QFuture<Result<QList<Album>>> MusicRepository::recentAlbums(const QString &libraryId, int limit)
{
    const QString key = QStringLiteral("home:recent:%1:%2").arg(libraryId).arg(limit);
    if (auto cached = m_albumListCache.get(key, now()))
        return ready(Result<QList<Album>>::success(*cached));

    Pending<QList<Album>> pending;
    const quint64 epoch = m_epoch;
    playedHistory(libraryId, 200).then(this, [this, pending, key, epoch, libraryId, limit](Result<QList<Track>> history) {
        if (!history.ok())
            return pending.resolve(Result<QList<Album>>::failure(history.error));
        QStringList ids;
        for (const Track &track : std::as_const(history.value)) {
            if (!track.albumId.isEmpty() && !ids.contains(track.albumId) && ids.size() < limit)
                ids.append(track.albumId);
        }
        if (ids.isEmpty())
            return pending.resolve(Result<QList<Album>>::success({}));
        // T10 epoch ruling: don't resolve the played albums' ids under a
        // different identity than the one that started this request.
        if (!epochIs(epoch))
            return pending.resolve(Result<QList<Album>>::failure(QStringLiteral("request canceled")));
        ItemsQuery query = libraryQuery(libraryId, kAlbum);
        query.ids = ids;
        query.fields = albumFields();
        query.limit = static_cast<int>(ids.size());
        fetchAlbums(query).then(this, [this, pending, key, epoch, ids](Result<QList<Album>> albums) {
            if (!albums.ok())
                return pending.resolve(albums);
            const QList<Album> ordered = orderedByIds(albums.value, ids);
            if (epochIs(epoch))
                m_albumListCache.put(key, ordered, now());
            pending.resolve(Result<QList<Album>>::success(ordered));
        });
    });
    return pending.future();
}

QFuture<Result<NewAlbums>> MusicRepository::newAlbums(const QString &libraryId, int limit)
{
    const QString key = QStringLiteral("lib:new:%1:%2").arg(libraryId).arg(limit);
    if (auto cached = m_newCache.get(key, now()))
        return ready(Result<NewAlbums>::success(*cached));

    auto state = std::make_shared<std::pair<Result<QList<Album>>, int>>(Result<QList<Album>>{}, -1);
    Pending<NewAlbums> pending;
    const quint64 epoch = m_epoch;
    auto fan = Fanout::create(this, [this, state, pending, key, epoch] {
        if (!state->first.ok())
            return pending.resolve(Result<NewAlbums>::failure(state->first.error));
        NewAlbums result{state->first.value, state->second};
        if (epochIs(epoch))
            m_newCache.put(key, result, now());
        pending.resolve(Result<NewAlbums>::success(result));
    });

    ItemsQuery albums = libraryQuery(libraryId, kAlbum);
    albums.sortBy = QStringLiteral("DateCreated");
    albums.sortDescending = true;
    albums.fields = albumFields();
    albums.limit = limit;
    fan->add<QList<Album>>(fetchAlbums(albums), [state](Result<QList<Album>> r) { state->first = std::move(r); });

    if (emby::caps::kMinDateCreated) {
        ItemsQuery count = libraryQuery(libraryId, kAlbum);
        count.minDateCreated = now().addDays(-7).toUTC().toString(Qt::ISODate);
        count.limit = 0;
        fan->add<QJsonDocument>(fetchItems(count), [state](Result<QJsonDocument> r) {
            if (r.ok())
                state->second = r.value.object().value(QStringLiteral("TotalRecordCount")).toInt(-1);
            else
                qCWarning(logApp) << "music: added-this-week count failed" << r.error;
        });
    }
    fan->seal();
    return pending.future();
}

QFuture<Result<QList<GenreBin>>> MusicRepository::allGenres(const QString &libraryId)
{
    const QString key = QStringLiteral("lib:genres:") + libraryId;
    if (auto cached = m_genreCache.get(key, now()))
        return ready(Result<QList<GenreBin>>::success(*cached));

    Pending<QList<GenreBin>> pending;
    const quint64 epoch = m_epoch;
    auto collected = std::make_shared<QList<GenreBin>>();
    auto step = std::make_shared<std::function<void(int)>>();
    *step = [this, pending, collected, step, libraryId, key, epoch](int startIndex) {
        const int pageSize = 200;
        QUrlQuery params;
        params.addQueryItem(QStringLiteral("UserId"), QStringLiteral("{uid}"));
        params.addQueryItem(QStringLiteral("ParentId"), libraryId);
        params.addQueryItem(QStringLiteral("StartIndex"), QString::number(startIndex));
        params.addQueryItem(QStringLiteral("Limit"), QString::number(pageSize));
        params.addQueryItem(QStringLiteral("SortBy"), QStringLiteral("SortName"));
        if (emby::caps::kGenreItemCounts)
            params.addQueryItem(QStringLiteral("Fields"), QStringLiteral("ItemCounts"));
        m_client->getJson(QStringLiteral("/MusicGenres"), params)
            .then(this, [this, pending, collected, step, startIndex, libraryId, key, epoch](Result<QJsonDocument> r) {
                if (!r.ok()) {
                    *step = nullptr; // break the self-reference
                    return pending.resolve(Result<QList<GenreBin>>::failure(r.error));
                }
                const QJsonArray items = itemsOf(r.value);
                for (const QJsonValue &value : items)
                    collected->append(emby::parseGenreBin(value.toObject()));
                // Page on the returned array's own size (ARCHITECTURE.md §2).
                if (items.size() == pageSize && startIndex / pageSize < 19) {
                    // T10 epoch ruling: don't page further under a different
                    // identity than the one that started this request.
                    if (!epochIs(epoch)) {
                        *step = nullptr;
                        return pending.resolve(Result<QList<GenreBin>>::failure(QStringLiteral("request canceled")));
                    }
                    return (*step)(startIndex + pageSize);
                }
                *step = nullptr;

                auto finish = [this, pending, key, epoch](QList<GenreBin> genres) {
                    std::stable_sort(genres.begin(), genres.end(), [](const GenreBin &a, const GenreBin &b) {
                        if (a.recordCount != b.recordCount)
                            return a.recordCount > b.recordCount;
                        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
                    });
                    if (epochIs(epoch))
                        m_genreCache.put(key, genres, now());
                    pending.resolve(Result<QList<GenreBin>>::success(genres));
                };
                if (emby::caps::kGenreItemCounts)
                    return finish(*collected);
                // T10 epoch ruling: don't start the album-walk count under a
                // different identity than the one that started this request.
                if (!epochIs(epoch))
                    return pending.resolve(Result<QList<GenreBin>>::failure(QStringLiteral("request canceled")));
                genreCountsFromAlbums(libraryId, *collected).then(this, [pending, finish](Result<QList<GenreBin>> counted) {
                    if (!counted.ok())
                        return pending.resolve(counted);
                    finish(counted.value);
                });
            });
    };
    (*step)(0);
    return pending.future();
}

QFuture<Result<QList<GenreBin>>> MusicRepository::genreCountsFromAlbums(const QString &libraryId,
                                                                        QList<GenreBin> genres)
{
    Pending<QList<GenreBin>> pending;
    auto counts = std::make_shared<QHash<QString, int>>(); // key: id, or "name:" + lower-case name
    auto shared = std::make_shared<QList<GenreBin>>(std::move(genres));
    auto step = std::make_shared<std::function<void(int)>>();
    *step = [this, pending, counts, shared, step, libraryId](int startIndex) {
        const int pageSize = 1000;
        ItemsQuery query = libraryQuery(libraryId, kAlbum);
        query.fields = {QStringLiteral("Genres")};
        query.startIndex = startIndex;
        query.limit = pageSize;
        fetchItems(query).then(this, [pending, counts, shared, step, startIndex](Result<QJsonDocument> r) {
            if (!r.ok()) {
                *step = nullptr;
                return pending.resolve(Result<QList<GenreBin>>::failure(r.error));
            }
            const QJsonArray items = itemsOf(r.value);
            for (const QJsonValue &value : items) {
                for (const GenreRef &genre : emby::parseAlbum(value.toObject()).genres) {
                    ++(*counts)[genre.id.isEmpty() ? QStringLiteral("name:") + genre.name.toLower() : genre.id];
                }
            }
            if (items.size() == pageSize && startIndex / pageSize < 19)
                return (*step)(startIndex + pageSize);
            *step = nullptr;
            for (GenreBin &genre : *shared)
                genre.recordCount = counts->value(genre.id) + counts->value(QStringLiteral("name:") + genre.name.toLower());
            pending.resolve(Result<QList<GenreBin>>::success(*shared));
        });
    };
    (*step)(0);
    return pending.future();
}

QFuture<Result<QList<ImageRef>>> MusicRepository::genreCovers(const QString &libraryId, const QString &genreId)
{
    const QString key = QStringLiteral("covers:") + genreId;
    if (auto cached = m_coverCache.get(key, now()))
        return ready(Result<QList<ImageRef>>::success(*cached));
    ItemsQuery query = libraryQuery(libraryId, kAlbum);
    query.genreIds = {genreId};
    query.sortBy = QStringLiteral("Random");
    query.limit = 3;
    const quint64 epoch = m_epoch;
    return fetchAlbums(query).then(this, [this, key, epoch](Result<QList<Album>> r) {
        if (!r.ok())
            return Result<QList<ImageRef>>::failure(r.error);
        QList<ImageRef> covers;
        for (const Album &album : std::as_const(r.value)) {
            if (album.coverRef.isValid())
                covers.append(album.coverRef);
        }
        if (epochIs(epoch))
            m_coverCache.put(key, covers, now());
        return Result<QList<ImageRef>>::success(covers);
    });
}

QFuture<Result<Page<GenreBin>>> MusicRepository::genreBins(const QString &libraryId, int limit)
{
    Pending<Page<GenreBin>> pending;
    const quint64 epoch = m_epoch;
    allGenres(libraryId).then(this, [this, pending, libraryId, limit, epoch](Result<QList<GenreBin>> all) {
        if (!all.ok())
            return pending.resolve(Result<Page<GenreBin>>::failure(all.error));
        // T10 epoch ruling: don't fetch genre covers under a different
        // identity than the one that started this request.
        if (!epochIs(epoch))
            return pending.resolve(Result<Page<GenreBin>>::failure(QStringLiteral("request canceled")));
        auto page = std::make_shared<Page<GenreBin>>();
        page->items = all.value.mid(0, limit);
        page->totalRecordCount = static_cast<int>(all.value.size());
        auto fan = Fanout::create(this, [pending, page] {
            pending.resolve(Result<Page<GenreBin>>::success(*page));
        });
        for (int i = 0; i < page->items.size(); ++i) {
            fan->add<QList<ImageRef>>(genreCovers(libraryId, page->items.at(i).id),
                                      [page, i](Result<QList<ImageRef>> r) {
                                          if (r.ok())
                                              page->items[i].covers = r.value;
                                          else
                                              qCWarning(logApp) << "music: genre covers failed" << r.error;
                                      });
        }
        fan->seal();
    });
    return pending.future();
}

QFuture<Result<QList<Artist>>> MusicRepository::topArtists(const QString &libraryId, int limit)
{
    const QString key = QStringLiteral("home:top:%1:%2").arg(libraryId).arg(limit);
    if (auto cached = m_artistListCache.get(key, now()))
        return ready(Result<QList<Artist>>::success(*cached));

    Pending<QList<Artist>> pending;
    const quint64 epoch = m_epoch;
    playedHistory(libraryId, 500).then(this, [this, pending, key, epoch, limit](Result<QList<Track>> history) {
        if (!history.ok())
            return pending.resolve(Result<QList<Artist>>::failure(history.error));
        QStringList order;
        QHash<QString, int> plays;
        for (const Track &track : std::as_const(history.value)) {
            if (track.artists.isEmpty() || track.artists.first().id.isEmpty())
                continue;
            const QString id = track.artists.first().id;
            if (!plays.contains(id))
                order.append(id);
            ++plays[id];
        }
        std::stable_sort(order.begin(), order.end(),
                         [&](const QString &a, const QString &b) { return plays.value(a) > plays.value(b); });
        const QStringList ids = order.mid(0, limit);
        if (ids.isEmpty())
            return pending.resolve(Result<QList<Artist>>::success({}));
        // T10 epoch ruling: don't resolve the top-played artists' ids under a
        // different identity than the one that started this request.
        if (!epochIs(epoch))
            return pending.resolve(Result<QList<Artist>>::failure(QStringLiteral("request canceled")));
        ItemsQuery query;
        query.ids = ids;
        query.limit = static_cast<int>(ids.size());
        fetchItems(query).then(this, [this, pending, key, epoch, ids](Result<QJsonDocument> r) {
            if (!r.ok())
                return pending.resolve(Result<QList<Artist>>::failure(r.error));
            const QList<Artist> ordered = orderedByIds(emby::parseArtists(itemsOf(r.value)), ids);
            if (epochIs(epoch))
                m_artistListCache.put(key, ordered, now());
            pending.resolve(Result<QList<Artist>>::success(ordered));
        });
    });
    return pending.future();
}

QFuture<Result<QList<Album>>> MusicRepository::forgottenFavourites(const QString &libraryId, int limit)
{
    const QString key = QStringLiteral("fav:forgotten:%1:%2").arg(libraryId).arg(limit);
    if (auto cached = m_albumListCache.get(key, now()))
        return ready(Result<QList<Album>>::success(*cached));
    ItemsQuery query = libraryQuery(libraryId, kAlbum);
    query.filters = {QStringLiteral("IsFavorite")};
    query.sortBy = QStringLiteral("DatePlayed");
    query.fields = albumFields();
    query.limit = limit;
    const quint64 epoch = m_epoch;
    return fetchAlbums(query).then(this, [this, key, epoch](Result<QList<Album>> r) {
        if (r.ok() && epochIs(epoch))
            m_albumListCache.put(key, r.value, now());
        return r;
    });
}

QFuture<Result<QList<Album>>> MusicRepository::randomAlbums(const QString &libraryId, int limit)
{
    ItemsQuery query = libraryQuery(libraryId, kAlbum);
    query.sortBy = QStringLiteral("Random");
    query.fields = albumFields();
    query.limit = limit;
    return fetchAlbums(query);
}

QList<Station> MusicRepository::stations(const QList<Album> &coverPool, const Artist &topArtist)
{
    QList<ImageRef> covers;
    for (const Album &album : coverPool) {
        if (album.coverRef.isValid())
            covers.append(album.coverRef);
    }
    QList<Station> list;
    auto add = [&](StationKind kind, const QString &label, const QString &seed = QString()) {
        Station station{kind, label, seed, {}};
        if (!covers.isEmpty()) {
            const qsizetype offset = list.size() * 4;
            for (qsizetype k = 0; k < 4; ++k)
                station.covers.append(covers.at((offset + k) % covers.size()));
        }
        list.append(station);
    };
    add(StationKind::HeavyRotation, QStringLiteral("Heavy rotation"));
    add(StationKind::Favourites, QStringLiteral("Favourites"));
    add(StationKind::DeepCuts, QStringLiteral("Deep cuts"));
    if (!topArtist.id.isEmpty())
        add(StationKind::MoreLike, QStringLiteral("More like %1").arg(topArtist.name), topArtist.id);
    add(StationKind::ShuffleAll, QStringLiteral("Shuffle all"));
    return list;
}

QFuture<Result<QList<Track>>> MusicRepository::resolveStation(const QString &libraryId, const Station &station)
{
    if (station.kind == StationKind::MoreLike) {
        if (station.seedId.isEmpty())
            return ready(Result<QList<Track>>::failure(QStringLiteral("no seed for station")));
        QUrlQuery params;
        params.addQueryItem(QStringLiteral("UserId"), QStringLiteral("{uid}"));
        params.addQueryItem(QStringLiteral("Limit"), QStringLiteral("200"));
        params.addQueryItem(QStringLiteral("Fields"), trackFields().join(QLatin1Char(',')));
        return m_client->getJson(QStringLiteral("/Items/%1/InstantMix").arg(station.seedId), params)
            .then(this, [](Result<QJsonDocument> r) {
                if (!r.ok())
                    return Result<QList<Track>>::failure(r.error);
                QList<Track> unique;
                QSet<QString> seen;
                for (const Track &track : emby::parseTracks(itemsOf(r.value))) {
                    if (!track.id.isEmpty() && !seen.contains(track.id)) {
                        seen.insert(track.id);
                        unique.append(track);
                    }
                }
                return Result<QList<Track>>::success(unique);
            });
    }

    ItemsQuery query = libraryQuery(libraryId, kAudio);
    query.fields = trackFields();
    query.limit = 200;
    query.sortBy = QStringLiteral("Random");
    switch (station.kind) {
    case StationKind::HeavyRotation:
        query.sortBy = QStringLiteral("PlayCount");
        query.sortDescending = true;
        query.filters = {QStringLiteral("IsPlayed")};
        break;
    case StationKind::Favourites:
        query.filters = {QStringLiteral("IsFavorite")};
        break;
    case StationKind::DeepCuts:
        query.filters = {QStringLiteral("IsUnplayed")};
        break;
    case StationKind::ShuffleAll:
    case StationKind::MoreLike:
        break;
    }
    const bool shuffle = station.kind == StationKind::HeavyRotation;
    return fetchTracks(query).then(this, [this, shuffle](Result<QList<Track>> r) {
        if (r.ok() && shuffle)
            std::shuffle(r.value.begin(), r.value.end(), m_rng);
        return r;
    });
}

} // namespace strmqt::music
