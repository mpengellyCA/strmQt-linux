#include "app/music/MusicRepository.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QPromise>
#include <QSet>

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
        if (artistId.isEmpty()) {
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

} // namespace strmqt::music
