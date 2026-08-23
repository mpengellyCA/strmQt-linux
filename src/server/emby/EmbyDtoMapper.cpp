#include "EmbyDtoMapper.h"

#include <QUrlQuery>

#include <initializer_list>

namespace strmqt::emby {

namespace {

// Emby sends 64-bit tick counts as JSON numbers; toDouble is lossless far beyond
// any real runtime (2^53 ticks ≈ 28 years).
qint64 toTicks(const QJsonValue &value)
{
    return static_cast<qint64>(value.toDouble(0.0));
}

// Emby has drifted on the casing of a few keys between 4.7 and 4.9 (Bitrate vs
// BitRate). Take the first key that carries a usable value.
QJsonValue firstOf(const QJsonObject &json, std::initializer_list<QLatin1String> keys)
{
    for (const QLatin1String &key : keys) {
        const QJsonValue value = json.value(key);
        if (!value.isUndefined() && !value.isNull())
            return value;
    }
    return {};
}

} // namespace

UserProfile parseUser(const QJsonObject &json)
{
    UserProfile user;
    user.id = json.value(QLatin1String("Id")).toString();
    user.name = json.value(QLatin1String("Name")).toString();
    return user;
}

SessionInfo parseAuthResult(const QJsonObject &json)
{
    SessionInfo session;
    session.accessToken = json.value(QLatin1String("AccessToken")).toString();
    session.serverId = json.value(QLatin1String("ServerId")).toString();
    session.user = parseUser(json.value(QLatin1String("User")).toObject());
    return session;
}

Library parseLibrary(const QJsonObject &json)
{
    Library library;
    library.id = json.value(QLatin1String("Id")).toString();
    library.name = json.value(QLatin1String("Name")).toString();
    library.collectionType = json.value(QLatin1String("CollectionType")).toString();
    library.primaryImageTag = json.value(QLatin1String("ImageTags"))
                                  .toObject()
                                  .value(QLatin1String("Primary"))
                                  .toString();
    return library;
}

QList<Library> parseViews(const QJsonObject &json)
{
    QList<Library> views;
    const QJsonArray items = json.value(QLatin1String("Items")).toArray();
    views.reserve(items.size());
    for (const QJsonValue &item : items)
        views.append(parseLibrary(item.toObject()));
    return views;
}

MediaItem parseMediaItem(const QJsonObject &json)
{
    MediaItem item;
    item.id = json.value(QLatin1String("Id")).toString();
    item.name = json.value(QLatin1String("Name")).toString();
    item.type = json.value(QLatin1String("Type")).toString();
    item.overview = json.value(QLatin1String("Overview")).toString();

    item.seriesId = json.value(QLatin1String("SeriesId")).toString();
    item.seriesName = json.value(QLatin1String("SeriesName")).toString();
    item.seasonId = json.value(QLatin1String("SeasonId")).toString();
    item.seasonName = json.value(QLatin1String("SeasonName")).toString();
    item.indexNumber = json.value(QLatin1String("IndexNumber")).toInt(-1);
    item.parentIndexNumber = json.value(QLatin1String("ParentIndexNumber")).toInt(-1);

    item.productionYear = json.value(QLatin1String("ProductionYear")).toInt(0);
    item.status = json.value(QLatin1String("Status")).toString();
    item.officialRating = json.value(QLatin1String("OfficialRating")).toString();
    item.communityRating = json.value(QLatin1String("CommunityRating")).toDouble(0.0);

    item.runtimeTicks = toTicks(json.value(QLatin1String("RunTimeTicks")));

    const QJsonObject userData = json.value(QLatin1String("UserData")).toObject();
    item.playbackPositionTicks = toTicks(userData.value(QLatin1String("PlaybackPositionTicks")));
    item.playedPercentage = userData.value(QLatin1String("PlayedPercentage")).toDouble(0.0);
    item.played = userData.value(QLatin1String("Played")).toBool(false);
    item.unplayedItemCount = userData.value(QLatin1String("UnplayedItemCount")).toInt(0);
    item.playCount = userData.value(QLatin1String("PlayCount")).toInt(0);
    item.favorite = userData.value(QLatin1String("IsFavorite")).toBool(false);

    item.premiereDate = json.value(QLatin1String("PremiereDate")).toString();
    item.playlistItemId = json.value(QLatin1String("PlaylistItemId")).toVariant().toString();
    item.endDate = json.value(QLatin1String("EndDate")).toString();
    item.albumArtist = json.value(QLatin1String("AlbumArtist")).toString();
    item.album = json.value(QLatin1String("Album")).toString();
    item.albumId = json.value(QLatin1String("AlbumId")).toVariant().toString();
    item.childCount = json.value(QLatin1String("ChildCount")).toInt();
    for (const QJsonValue &artist : json.value(QLatin1String("Artists")).toArray()) {
        const QString name = artist.toString();
        if (!name.isEmpty())
            item.artists.append(name);
    }
    // ArtistItems carries the ids the names alone cannot be navigated by, and it
    // is the only place the name→id pairing exists at all: `Artists` is a bare
    // list of names, and the two arrays are NOT the same list. A performer the
    // server has no artist item for appears in one and not the other, so
    // appending ids in ArtistItems order does not align them — Artists
    // ["Featured Guest", "Main Band"] with ArtistItems [Main Band] pairs the
    // guest's name with the band's id, and a "go to artist" built on index 0
    // then reads one artist and navigates to another.
    //
    // Each id is therefore written AT the index of its own name, and a credited
    // name with no artist item keeps an empty id rather than borrowing the next
    // one. Index i means the same artist in both lists, or nothing at all.
    const QJsonArray artistItems = json.value(QLatin1String("ArtistItems")).toArray();
    for (const QJsonValue &entry : artistItems) {
        const QString name = entry.toObject().value(QLatin1String("Name")).toString();
        if (!name.isEmpty() && !item.artists.contains(name))
            item.artists.append(name);
    }
    if (!artistItems.isEmpty()) {
        item.artistIds.resize(item.artists.size());
        for (const QJsonValue &entry : artistItems) {
            const QJsonObject artist = entry.toObject();
            const qsizetype index =
                item.artists.indexOf(artist.value(QLatin1String("Name")).toString());
            if (index >= 0)
                item.artistIds[index] = artist.value(QLatin1String("Id")).toVariant().toString();
        }
    }
    for (const QJsonValue &place : json.value(QLatin1String("ProductionLocations")).toArray()) {
        const QString name = place.toString();
        if (!name.isEmpty())
            item.productionLocations.append(name);
    }
    item.primaryImageTag = json.value(QLatin1String("ImageTags"))
                               .toObject()
                               .value(QLatin1String("Primary"))
                               .toString();
    const QJsonArray backdrops = json.value(QLatin1String("BackdropImageTags")).toArray();
    for (const QJsonValue &tag : backdrops) {
        const QString tagString = tag.toString();
        if (!tagString.isEmpty())
            item.backdropImageTags.append(tagString);
    }

    // 16:9 sources for wide cards. Verified live: a Movie has ImageTags.Thumb,
    // an Episode has none of its own and inherits its series' backdrop.
    item.thumbImageTag = json.value(QLatin1String("ImageTags"))
                             .toObject()
                             .value(QLatin1String("Thumb"))
                             .toString();
    item.parentThumbImageTag = json.value(QLatin1String("ParentThumbImageTag")).toString();
    item.parentThumbItemId =
        json.value(QLatin1String("ParentThumbItemId")).toVariant().toString();
    item.parentBackdropImageTag =
        json.value(QLatin1String("ParentBackdropImageTags")).toArray().first().toString();
    item.parentBackdropItemId =
        json.value(QLatin1String("ParentBackdropItemId")).toVariant().toString();

    // 1:1 sources for square art. AlbumPrimaryImageTag is the tag of the
    // ALBUM's cover and has to be fetched from AlbumId, not from this track;
    // ParentPrimaryImageItemId names its own item for the same reason.
    item.albumPrimaryImageTag = json.value(QLatin1String("AlbumPrimaryImageTag")).toString();
    item.parentPrimaryImageItemId =
        json.value(QLatin1String("ParentPrimaryImageItemId")).toVariant().toString();
    item.parentPrimaryImageTag = json.value(QLatin1String("ParentPrimaryImageTag")).toString();
    return item;
}

ItemDetails parseItemDetails(const QJsonObject &json)
{
    ItemDetails details;
    details.item = parseMediaItem(json);
    details.tagline = json.value(QLatin1String("Taglines")).toArray().first().toString();
    for (const QJsonValue &genre : json.value(QLatin1String("Genres")).toArray())
        details.genres.append(genre.toString());
    for (const QJsonValue &value : json.value(QLatin1String("People")).toArray()) {
        const QJsonObject person = value.toObject();
        const QString name = person.value(QLatin1String("Name")).toString();
        if (name.isEmpty())
            continue;

        Person entry;
        entry.name = name;
        // Person ids arrive as a JSON string on 4.9 but as a number on some
        // older builds; toVariant().toString() takes both without a branch.
        entry.id = person.value(QLatin1String("Id")).toVariant().toString();
        entry.role = person.value(QLatin1String("Role")).toString();
        entry.type = person.value(QLatin1String("Type")).toString();
        entry.primaryImageTag = person.value(QLatin1String("PrimaryImageTag")).toString();
        details.people.append(entry);

        // The flat name lists stay populated: they are what the text lines and
        // the existing tests are built on.
        if (entry.type == QLatin1String("Actor"))
            details.cast.append(name);
        else if (entry.type == QLatin1String("Director"))
            details.directors.append(name);
    }

    // GenreItems/Studios carry ids; Genres is the name-only fallback for a
    // payload that predates them.
    for (const QJsonValue &value : json.value(QLatin1String("GenreItems")).toArray()) {
        const QJsonObject genre = value.toObject();
        NamedId entry;
        entry.id = genre.value(QLatin1String("Id")).toVariant().toString();
        entry.name = genre.value(QLatin1String("Name")).toString();
        if (!entry.name.isEmpty())
            details.genreItems.append(entry);
    }
    for (const QJsonValue &value : json.value(QLatin1String("Studios")).toArray()) {
        const QJsonObject studio = value.toObject();
        NamedId entry;
        entry.id = studio.value(QLatin1String("Id")).toVariant().toString();
        entry.name = studio.value(QLatin1String("Name")).toString();
        if (!entry.name.isEmpty())
            details.studios.append(entry);
    }
    for (const QJsonValue &value : json.value(QLatin1String("ExternalUrls")).toArray()) {
        const QJsonObject link = value.toObject();
        ExternalLink entry;
        entry.name = link.value(QLatin1String("Name")).toString();
        entry.url = link.value(QLatin1String("Url")).toString();
        if (!entry.name.isEmpty() && !entry.url.isEmpty())
            details.externalLinks.append(entry);
    }
    // RemoteTrailers rows carry a Url and often no Name; a trailer with no
    // label is still a trailer, so it is numbered rather than dropped.
    const QJsonArray trailerArray = json.value(QLatin1String("RemoteTrailers")).toArray();
    for (const QJsonValue &value : trailerArray) {
        const QJsonObject trailer = value.toObject();
        ExternalLink entry;
        entry.url = trailer.value(QLatin1String("Url")).toString();
        if (entry.url.isEmpty())
            continue;
        entry.name = trailer.value(QLatin1String("Name")).toString();
        if (entry.name.isEmpty()) {
            entry.name = trailerArray.size() > 1
                             ? QObject::tr("Trailer %1").arg(details.trailers.size() + 1)
                             : QObject::tr("Trailer");
        }
        details.trailers.append(entry);
    }

    const QJsonObject providers = json.value(QLatin1String("ProviderIds")).toObject();
    for (auto it = providers.constBegin(); it != providers.constEnd(); ++it) {
        const QString value = it.value().toVariant().toString();
        if (!value.isEmpty())
            details.providerIds.insert(it.key(), value);
    }
    details.premiereDate = json.value(QLatin1String("PremiereDate")).toString();
    details.criticRating = json.value(QLatin1String("CriticRating")).toDouble();
    // Present only when the request asked for them (EmbyClient::itemDetails
    // does); absent on any other payload, which is not an error.
    details.mediaSources = parseMediaSources(json.value(QLatin1String("MediaSources")).toArray());
    details.chapters = parseChapters(json.value(QLatin1String("Chapters")).toArray());
    return details;
}

Chapter parseChapter(const QJsonObject &json)
{
    Chapter chapter;
    chapter.name = json.value(QLatin1String("Name")).toString();
    chapter.startPositionTicks = toTicks(json.value(QLatin1String("StartPositionTicks")));
    // Emby 4.9 sends "ImageTag"; older builds sent the chapter image under
    // "ImageTags/Primary" like every other item.
    chapter.imageTag = json.value(QLatin1String("ImageTag")).toString();
    if (chapter.imageTag.isEmpty()) {
        chapter.imageTag = json.value(QLatin1String("ImageTags"))
                               .toObject()
                               .value(QLatin1String("Primary"))
                               .toString();
    }
    return chapter;
}

QList<Chapter> parseChapters(const QJsonArray &json)
{
    QList<Chapter> chapters;
    chapters.reserve(json.size());
    for (const QJsonValue &value : json) {
        if (!value.isObject())
            continue;
        chapters.append(parseChapter(value.toObject()));
    }
    return chapters;
}

QList<MediaItem> parseItemArray(const QJsonArray &json)
{
    QList<MediaItem> items;
    items.reserve(json.size());
    for (const QJsonValue &item : json)
        items.append(parseMediaItem(item.toObject()));
    return items;
}

ItemsPage parseItemsPage(const QJsonObject &json)
{
    ItemsPage page;
    page.items = parseItemArray(json.value(QLatin1String("Items")).toArray());
    page.totalRecordCount =
        json.value(QLatin1String("TotalRecordCount")).toInt(static_cast<int>(page.items.size()));
    page.startIndex = json.value(QLatin1String("StartIndex")).toInt(0);
    return page;
}

namespace {

// Resolve a server-relative path+query against the base URL, preserving any
// reverse-proxy base path, and guarantee an api_key query parameter.
QUrl resolveStreamUrl(const QUrl &baseUrl, const QString &pathAndQuery, const QString &accessToken)
{
    const QUrl relative(pathAndQuery);
    QUrl url = baseUrl;
    QString basePath = url.path();
    if (basePath.endsWith(QLatin1Char('/')))
        basePath.chop(1);
    url.setPath(basePath + relative.path());

    QUrlQuery query(relative.query());
    if (!query.hasQueryItem(QStringLiteral("api_key")))
        query.addQueryItem(QStringLiteral("api_key"), accessToken);
    url.setQuery(query);
    return url;
}

} // namespace

MediaStream parseMediaStream(const QJsonObject &json)
{
    MediaStream stream;
    stream.index = json.value(QLatin1String("Index")).toInt(-1);
    stream.type = json.value(QLatin1String("Type")).toString();
    stream.codec = json.value(QLatin1String("Codec")).toString();
    stream.language = json.value(QLatin1String("Language")).toString();
    stream.displayTitle = json.value(QLatin1String("DisplayTitle")).toString();
    stream.title = json.value(QLatin1String("Title")).toString();
    stream.isDefault = json.value(QLatin1String("IsDefault")).toBool(false);
    stream.isForced = json.value(QLatin1String("IsForced")).toBool(false);
    stream.isExternal = json.value(QLatin1String("IsExternal")).toBool(false);

    stream.width = json.value(QLatin1String("Width")).toInt(0);
    stream.height = json.value(QLatin1String("Height")).toInt(0);
    stream.videoRange = json.value(QLatin1String("VideoRange")).toString();
    stream.frameRate =
        firstOf(json, {QLatin1String("RealFrameRate"), QLatin1String("AverageFrameRate")})
            .toDouble(0.0);
    stream.profile = json.value(QLatin1String("Profile")).toString();
    stream.bitDepth = json.value(QLatin1String("BitDepth")).toInt(0);

    stream.channels = json.value(QLatin1String("Channels")).toInt(0);
    stream.channelLayout = json.value(QLatin1String("ChannelLayout")).toString();
    stream.sampleRate = json.value(QLatin1String("SampleRate")).toInt(0);

    stream.bitRate = static_cast<qint64>(
        firstOf(json, {QLatin1String("BitRate"), QLatin1String("Bitrate")}).toDouble(0.0));
    return stream;
}

MediaSource parseMediaSource(const QJsonObject &json)
{
    MediaSource source;
    source.id = json.value(QLatin1String("Id")).toString();
    source.name = json.value(QLatin1String("Name")).toString();
    source.container = json.value(QLatin1String("Container")).toString();
    source.size = static_cast<qint64>(json.value(QLatin1String("Size")).toDouble(0.0));
    source.bitrate = static_cast<qint64>(
        firstOf(json, {QLatin1String("Bitrate"), QLatin1String("BitRate")}).toDouble(0.0));
    source.runtimeTicks = toTicks(json.value(QLatin1String("RunTimeTicks")));
    source.protocol = json.value(QLatin1String("Protocol")).toString();

    source.supportsDirectPlay = json.value(QLatin1String("SupportsDirectPlay")).toBool(false);
    source.supportsDirectStream = json.value(QLatin1String("SupportsDirectStream")).toBool(false);
    source.supportsTranscoding = json.value(QLatin1String("SupportsTranscoding")).toBool(false);

    source.directStreamUrl = json.value(QLatin1String("DirectStreamUrl")).toString();
    source.transcodingUrl = json.value(QLatin1String("TranscodingUrl")).toString();
    source.transcodingContainer = json.value(QLatin1String("TranscodingContainer")).toString();

    // TranscodeReasons has shipped both as an array of strings and as a single
    // comma-separated string; accept either and ignore anything else.
    const QJsonValue reasons =
        firstOf(json, {QLatin1String("TranscodeReasons"), QLatin1String("TranscodeReason")});
    if (reasons.isArray()) {
        for (const QJsonValue &reason : reasons.toArray()) {
            const QString text = reason.toString();
            if (!text.isEmpty())
                source.transcodeReasons.append(text);
        }
    } else if (reasons.isString()) {
        for (const QString &text : reasons.toString().split(QLatin1Char(','), Qt::SkipEmptyParts))
            source.transcodeReasons.append(text.trimmed());
    }

    const QJsonArray streams = json.value(QLatin1String("MediaStreams")).toArray();
    source.streams.reserve(streams.size());
    for (const QJsonValue &value : streams) {
        if (!value.isObject())
            continue;
        source.streams.append(parseMediaStream(value.toObject()));
    }
    return source;
}

QList<MediaSource> parseMediaSources(const QJsonArray &json)
{
    QList<MediaSource> sources;
    sources.reserve(json.size());
    for (const QJsonValue &value : json) {
        if (!value.isObject())
            continue;
        sources.append(parseMediaSource(value.toObject()));
    }
    return sources;
}

namespace {

// Build the delivery ladder for a single source, best-first. Every candidate in
// the returned list belongs to `source` — demotion can therefore never cross
// versions (ARCHITECTURE.md).
QList<StreamCandidate> buildLadder(const MediaSource &source, const QUrl &baseUrl,
                                   const QString &itemId, const QString &accessToken,
                                   const QString &deviceId, const QString &playSessionId)
{
    QList<StreamCandidate> ladder;

    if (source.supportsDirectPlay && !source.container.isEmpty()) {
        StreamCandidate candidate;
        candidate.method = PlayMethod::DirectPlay;
        candidate.container = source.container;
        candidate.mediaSourceId = source.id;
        const QString path = QStringLiteral("/Videos/%1/stream.%2").arg(itemId, source.container);
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("static"), QStringLiteral("true"));
        query.addQueryItem(QStringLiteral("MediaSourceId"), source.id);
        query.addQueryItem(QStringLiteral("DeviceId"), deviceId);
        if (!playSessionId.isEmpty())
            query.addQueryItem(QStringLiteral("PlaySessionId"), playSessionId);
        candidate.url =
            resolveStreamUrl(baseUrl, path + QLatin1Char('?') + query.toString(), accessToken);
        ladder.append(candidate);
    }

    if (source.supportsDirectStream && !source.directStreamUrl.isEmpty()) {
        StreamCandidate candidate;
        candidate.method = PlayMethod::DirectStream;
        candidate.container = source.container;
        candidate.mediaSourceId = source.id;
        candidate.url = resolveStreamUrl(baseUrl, source.directStreamUrl, accessToken);
        ladder.append(candidate);
    }

    if (!source.transcodingUrl.isEmpty()) {
        StreamCandidate candidate;
        candidate.method = PlayMethod::Transcode;
        candidate.container = source.transcodingContainer;
        candidate.mediaSourceId = source.id;
        candidate.url = resolveStreamUrl(baseUrl, source.transcodingUrl, accessToken);
        ladder.append(candidate);
    }

    return ladder;
}

} // namespace

PlaybackTicket parsePlaybackTicket(const QJsonObject &json, const QUrl &baseUrl,
                                   const QString &itemId, const QString &accessToken,
                                   const QString &deviceId)
{
    PlaybackTicket ticket;
    ticket.playSessionId = json.value(QLatin1String("PlaySessionId")).toString();

    const QList<MediaSource> sources =
        parseMediaSources(json.value(QLatin1String("MediaSources")).toArray());
    ticket.sources.reserve(sources.size());
    for (const MediaSource &source : sources) {
        MediaSourceCandidates entry;
        entry.source = source;
        entry.candidates =
            buildLadder(source, baseUrl, itemId, accessToken, deviceId, ticket.playSessionId);
        ticket.sources.append(entry);
    }
    return ticket;
}

} // namespace strmqt::emby
