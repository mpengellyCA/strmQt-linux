#include "server/emby/EmbyMusicMapper.h"

#include <algorithm>

#include <QHash>
#include <QMap>
#include <QRegularExpression>
#include <QSet>

#include "server/dto/MediaItem.h"
#include "server/emby/MusicServerCapabilities.h"

namespace strmqt::emby {

using namespace music;

namespace {

int integer(const QJsonValue &value)
{
    return static_cast<int>(value.toVariant().toLongLong());
}

// "44.1" for 44100, "96" for 96000, "88.2" for 88200.
QString khz(int hz)
{
    const int tenths = qRound(hz / 100.0);
    if (tenths % 10 == 0)
        return QString::number(tenths / 10);
    return QStringLiteral("%1.%2").arg(tenths / 10).arg(tenths % 10);
}

bool isDsd(const QString &codec) { return codec.startsWith(QLatin1String("dsd")); }
bool isPcm(const QString &codec)
{
    return codec.startsWith(QLatin1String("pcm_")) || codec == QLatin1String("wav");
}

QString codecLabel(const QString &codec)
{
    if (isDsd(codec))
        return QStringLiteral("DSD");
    if (isPcm(codec))
        return QStringLiteral("PCM");
    if (codec == QLatin1String("wavpack"))
        return QStringLiteral("WV");
    if (codec == QLatin1String("vorbis"))
        return QStringLiteral("OGG");
    return codec.toUpper();
}

QString text(const QJsonValue &value)
{
    if (value.isString())
        return value.toString();
    if (value.isDouble())
        return value.toVariant().toString();
    return {};
}

qint64 ticksToMs(const QJsonValue &value)
{
    return qMax<qint64>(0, value.toVariant().toLongLong() / kTicksPerMs);
}

QList<NamedRef> refs(const QJsonValue &value)
{
    QList<NamedRef> list;
    for (const QJsonValue &entry : value.toArray()) {
        const QJsonObject object = entry.toObject();
        const QString name = text(object.value(QStringLiteral("Name")));
        if (!name.isEmpty())
            list.append({text(object.value(QStringLiteral("Id"))), name});
    }
    return list;
}

QList<NamedRef> namesOnly(const QJsonValue &value)
{
    QList<NamedRef> list;
    for (const QJsonValue &entry : value.toArray()) {
        const QString name = text(entry);
        if (!name.isEmpty())
            list.append({QString(), name});
    }
    return list;
}

QString primaryTag(const QJsonObject &json)
{
    return text(json.value(QStringLiteral("ImageTags")).toObject().value(QStringLiteral("Primary")));
}

ImageRef ownPrimary(const QJsonObject &json)
{
    const QString tag = primaryTag(json);
    if (tag.isEmpty())
        return {};
    return {text(json.value(QStringLiteral("Id"))), QStringLiteral("Primary"), tag};
}

bool sameArtist(const NamedRef &a, const NamedRef &b)
{
    if (!a.id.isEmpty() && !b.id.isEmpty())
        return a.id == b.id;
    return a.name.compare(b.name, Qt::CaseInsensitive) == 0;
}

bool containsArtist(const QList<NamedRef> &list, const NamedRef &artist)
{
    return std::any_of(list.cbegin(), list.cend(),
                       [&](const NamedRef &entry) { return sameArtist(entry, artist); });
}

struct UserDataFields
{
    bool favourite = false;
    bool played = false;
    int playCount = 0;
    qint64 positionMs = 0;
    QDateTime lastPlayed;
};

UserDataFields userData(const QJsonObject &json)
{
    const QJsonObject data = json.value(QStringLiteral("UserData")).toObject();
    UserDataFields fields;
    fields.favourite = data.value(QStringLiteral("IsFavorite")).toBool();
    fields.played = data.value(QStringLiteral("Played")).toBool();
    fields.playCount = qMax(0, integer(data.value(QStringLiteral("PlayCount"))));
    fields.positionMs = ticksToMs(data.value(QStringLiteral("PlaybackPositionTicks")));
    fields.lastPlayed = parseEmbyDate(text(data.value(QStringLiteral("LastPlayedDate"))));
    return fields;
}

std::optional<ReleaseType> serverReleaseType(const QJsonObject &json)
{
    const QString field = QString::fromLatin1(caps::kReleaseTypeField);
    if (field.isEmpty())
        return std::nullopt;
    const QJsonValue value = json.value(field);
    if (value.isArray()) {
        for (const QJsonValue &entry : value.toArray()) {
            if (auto type = releaseTypeFromTag(text(entry)))
                return type;
        }
        return std::nullopt;
    }
    return releaseTypeFromTag(text(value));
}

} // namespace

AudioFormat deriveAudioFormat(const QString &codecIn, int bitDepth, int sampleRate, int bitrate,
                              int channels)
{
    const QString codec = codecIn.trimmed().toLower();
    if (codec.isEmpty())
        return {};

    static const QStringList kLossless = {
        QStringLiteral("flac"), QStringLiteral("alac"), QStringLiteral("ape"),
        QStringLiteral("wavpack"), QStringLiteral("tta"), QStringLiteral("truehd"),
        QStringLiteral("mlp")};

    AudioFormat format;
    format.codec = codec;
    format.bitDepth = qMax(0, bitDepth);
    format.sampleRate = qMax(0, sampleRate);
    format.bitrate = qMax(0, bitrate);
    format.channels = qMax(0, channels);
    format.isLossless = isDsd(codec) || isPcm(codec) || kLossless.contains(codec);
    format.isHiRes = format.isLossless && (format.bitDepth > 16 || format.sampleRate > 48000);

    const QString label = codecLabel(codec);
    if (isDsd(codec)) {
        format.isHiRes = true;
        // The real server reports DSF files at ffmpeg's decimated rate (DSD64 is
        // sent as SampleRate=352800, i.e. the true DSD rate / 8). Scale up before
        // deriving the DSDn number when the reported rate looks decimated;
        // format.sampleRate itself keeps the value the server reported.
        qint64 dsdRate = format.sampleRate;
        if (dsdRate > 0 && dsdRate < 1'000'000)
            dsdRate *= 8;
        format.badge = dsdRate > 0
                           ? QStringLiteral("DSD%1").arg(qRound(dsdRate / 44100.0))
                           : label;
    } else if (format.isLossless) {
        format.badge = (format.bitDepth > 0 && format.sampleRate > 0)
                           ? QStringLiteral("%1 %2/%3").arg(label).arg(format.bitDepth).arg(khz(format.sampleRate))
                           : label;
    } else {
        format.badge = format.bitrate > 0
                           ? QStringLiteral("%1 %2").arg(label).arg(qRound(format.bitrate / 1000.0))
                           : label;
    }
    return format;
}

AudioFormat parseAudioFormat(const QJsonObject &item)
{
    auto fromStreams = [](const QJsonArray &streams, const QString &container) -> AudioFormat {
        for (const QJsonValue &value : streams) {
            const QJsonObject stream = value.toObject();
            if (stream.value(QStringLiteral("Type")).toString() != QLatin1String("Audio"))
                continue;
            QString codec = stream.value(QStringLiteral("Codec")).toString();
            if (codec.isEmpty())
                codec = container;
            return deriveAudioFormat(codec, integer(stream.value(QStringLiteral("BitDepth"))),
                                     integer(stream.value(QStringLiteral("SampleRate"))),
                                     integer(stream.value(QStringLiteral("BitRate"))),
                                     integer(stream.value(QStringLiteral("Channels"))));
        }
        return {};
    };

    const QString container = item.value(QStringLiteral("Container")).toString();
    AudioFormat format = fromStreams(item.value(QStringLiteral("MediaStreams")).toArray(), container);
    if (format.isValid())
        return format;
    const QJsonArray sources = item.value(QStringLiteral("MediaSources")).toArray();
    const QJsonObject source = sources.isEmpty() ? QJsonObject{} : sources.at(0).toObject();
    QString sourceContainer = source.value(QStringLiteral("Container")).toString();
    if (sourceContainer.isEmpty())
        sourceContainer = container;
    return fromStreams(source.value(QStringLiteral("MediaStreams")).toArray(), sourceContainer);
}

FeaturedSplit splitFeatured(const QString &title)
{
    static const QRegularExpression kFeat(
        QStringLiteral(R"(\s*[\(\[]\s*(?:feat\.?|ft\.?|featuring)\s+([^\)\]]+)[\)\]]\s*$|\s+(?:feat\.|ft\.|featuring)\s+(.+)$)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression kSeparators(QStringLiteral(R"(\s*(?:,|&)\s*)"));

    const QRegularExpressionMatch match = kFeat.match(title);
    if (!match.hasMatch())
        return {title, {}};
    const QString names = match.captured(1).isEmpty() ? match.captured(2) : match.captured(1);
    QStringList list;
    for (const QString &name : names.split(kSeparators, Qt::SkipEmptyParts)) {
        const QString trimmed = name.trimmed();
        if (!trimmed.isEmpty())
            list.append(trimmed);
    }
    return {title.left(match.capturedStart()).trimmed(), list};
}

QDateTime parseEmbyDate(const QString &input)
{
    if (input.isEmpty())
        return {};
    static const QRegularExpression kFraction(QStringLiteral(R"((\.\d{3})\d+)"));
    QString trimmed = input;
    trimmed.replace(kFraction, QStringLiteral("\\1"));
    return QDateTime::fromString(trimmed, Qt::ISODateWithMs);
}

std::optional<ReleaseType> releaseTypeFromTag(const QString &tag)
{
    const QString key = tag.trimmed().toLower();
    if (key == QLatin1String("album"))
        return ReleaseType::Album;
    if (key == QLatin1String("ep"))
        return ReleaseType::EP;
    if (key == QLatin1String("single"))
        return ReleaseType::Single;
    if (key == QLatin1String("compilation"))
        return ReleaseType::Compilation;
    return std::nullopt;
}

ReleaseType classifyRelease(const ReleaseEvidence &evidence)
{
    if (auto type = releaseTypeFromTag(evidence.serverTag))
        return *type;

    for (const QString &name : evidence.albumArtistNames) {
        if (name.compare(QLatin1String("Various Artists"), Qt::CaseInsensitive) == 0)
            return ReleaseType::Compilation;
    }

    QSet<QString> performers;
    for (const QString &name : evidence.trackPrimaryArtists) {
        if (!name.isEmpty())
            performers.insert(name.toLower());
    }
    bool albumArtistPerforms = false;
    for (const QString &name : evidence.albumArtistNames)
        albumArtistPerforms = albumArtistPerforms || performers.contains(name.toLower());
    if (performers.size() >= 4 && !albumArtistPerforms)
        return ReleaseType::Compilation;

    const qint64 minute = 60'000;
    if (evidence.runtimeMs > 0) {
        if (evidence.trackCount >= 1 && evidence.trackCount <= 3 && evidence.runtimeMs < 20 * minute)
            return ReleaseType::Single;
        if (evidence.trackCount >= 1 && evidence.trackCount <= 7 && evidence.runtimeMs < 35 * minute)
            return ReleaseType::EP;
    }
    return ReleaseType::Album;
}

QList<Disc> groupDiscs(const QList<Track> &tracks)
{
    QMap<int, Disc> byNumber;
    for (const Track &track : tracks) {
        const int number = track.discNumber > 0 ? track.discNumber : 1;
        Disc &disc = byNumber[number];
        disc.number = number;
        disc.runtimeMs += track.runtimeMs;
        disc.tracks.append(track);
    }
    return byNumber.values();
}

QString dominantFormat(const QList<Track> &tracks)
{
    QHash<QString, int> counts;
    QString best;
    int bestCount = 0;
    for (const Track &track : tracks) {
        if (track.format.badge.isEmpty())
            continue;
        const int count = ++counts[track.format.badge];
        if (count > bestCount) {
            best = track.format.badge;
            bestCount = count;
        }
    }
    return best;
}

void refineAlbumFromTracks(Album &album, const QList<Track> &tracks)
{
    if (tracks.isEmpty())
        return;
    album.trackCount = static_cast<int>(tracks.size());
    qint64 runtime = 0;
    QSet<int> discs;
    QStringList performers;
    for (const Track &track : tracks) {
        runtime += track.runtimeMs;
        discs.insert(track.discNumber > 0 ? track.discNumber : 1);
        if (!track.artists.isEmpty())
            performers.append(track.artists.first().name);
    }
    album.runtimeMs = runtime;
    album.discCount = static_cast<int>(discs.size());
    album.formatSummary = dominantFormat(tracks);
    if (album.releaseTypeFromServer)
        return;
    QStringList albumArtistNames;
    for (const NamedRef &artist : album.albumArtists)
        albumArtistNames.append(artist.name);
    album.releaseType = classifyRelease({QString(), albumArtistNames, album.trackCount,
                                         album.runtimeMs, performers});
}

Track parseTrack(const QJsonObject &json)
{
    Track track;
    track.id = text(json.value(QStringLiteral("Id")));
    track.title = text(json.value(QStringLiteral("Name")));
    track.artists = refs(json.value(QStringLiteral("ArtistItems")));
    if (track.artists.isEmpty())
        track.artists = namesOnly(json.value(QStringLiteral("Artists")));
    track.albumArtists = refs(json.value(QStringLiteral("AlbumArtists")));
    if (track.albumArtists.isEmpty()) {
        const QString name = text(json.value(QStringLiteral("AlbumArtist")));
        if (!name.isEmpty())
            track.albumArtists.append({QString(), name});
    }
    track.albumId = text(json.value(QStringLiteral("AlbumId")));
    track.albumTitle = text(json.value(QStringLiteral("Album")));
    track.discNumber = qMax(0, integer(json.value(QStringLiteral("ParentIndexNumber"))));
    track.trackNumber = qMax(0, integer(json.value(QStringLiteral("IndexNumber"))));
    track.runtimeMs = ticksToMs(json.value(QStringLiteral("RunTimeTicks")));
    track.dateAdded = parseEmbyDate(text(json.value(QStringLiteral("DateCreated"))));
    track.playlistItemId = text(json.value(QStringLiteral("PlaylistItemId")));

    const UserDataFields data = userData(json);
    track.favourite = data.favourite;
    track.played = data.played;
    track.playCount = data.playCount;
    track.positionMs = data.positionMs;
    track.lastPlayed = data.lastPlayed;

    track.format = parseAudioFormat(json);

    const QString albumTag = text(json.value(QStringLiteral("AlbumPrimaryImageTag")));
    const QString parentId = text(json.value(QStringLiteral("ParentPrimaryImageItemId")));
    const QString parentTag = text(json.value(QStringLiteral("ParentPrimaryImageTag")));
    if (!albumTag.isEmpty() && !track.albumId.isEmpty())
        track.coverRef = {track.albumId, QStringLiteral("Primary"), albumTag};
    else if (!parentTag.isEmpty() && !parentId.isEmpty())
        track.coverRef = {parentId, QStringLiteral("Primary"), parentTag};
    else
        track.coverRef = ownPrimary(json);

    const FeaturedSplit split = splitFeatured(track.title);
    track.displayTitle = split.title;
    for (const QString &name : split.names) {
        NamedRef ref{QString(), name};
        for (const NamedRef &artist : track.artists) {
            if (sameArtist(artist, ref))
                ref.id = artist.id;
        }
        if (!containsArtist(track.featured, ref))
            track.featured.append(ref);
    }
    const bool albumArtistPerforms = std::any_of(
        track.albumArtists.cbegin(), track.albumArtists.cend(),
        [&](const NamedRef &albumArtist) { return containsArtist(track.artists, albumArtist); });
    if (albumArtistPerforms) {
        for (const NamedRef &artist : track.artists) {
            if (!containsArtist(track.albumArtists, artist) && !containsArtist(track.featured, artist))
                track.featured.append(artist);
        }
    }
    track.differsFromAlbumArtist =
        !track.artists.isEmpty() && !track.albumArtists.isEmpty() && !albumArtistPerforms;
    return track;
}

QList<Track> parseTracks(const QJsonArray &json)
{
    QList<Track> list;
    list.reserve(json.size());
    for (const QJsonValue &value : json)
        list.append(parseTrack(value.toObject()));
    return list;
}

Album parseAlbum(const QJsonObject &json)
{
    Album album;
    album.id = text(json.value(QStringLiteral("Id")));
    album.title = text(json.value(QStringLiteral("Name")));
    album.albumArtists = refs(json.value(QStringLiteral("AlbumArtists")));
    if (album.albumArtists.isEmpty()) {
        const QString name = text(json.value(QStringLiteral("AlbumArtist")));
        if (!name.isEmpty())
            album.albumArtists.append({QString(), name});
    }
    album.premiereDate = parseEmbyDate(text(json.value(QStringLiteral("PremiereDate"))));
    album.year = integer(json.value(QStringLiteral("ProductionYear")));
    if (album.year <= 0 && album.premiereDate.isValid())
        album.year = album.premiereDate.toUTC().date().year();
    album.genres = refs(json.value(QStringLiteral("GenreItems")));
    if (album.genres.isEmpty())
        album.genres = namesOnly(json.value(QStringLiteral("Genres")));
    for (const NamedRef &studio : refs(json.value(QStringLiteral("Studios"))))
        album.studios.append(studio.name);
    album.dateAdded = parseEmbyDate(text(json.value(QStringLiteral("DateCreated"))));
    album.trackCount = qMax(0, integer(json.value(QStringLiteral("ChildCount"))));
    album.runtimeMs = ticksToMs(json.value(QStringLiteral("RunTimeTicks")));
    if (album.runtimeMs == 0)
        album.runtimeMs = ticksToMs(json.value(QStringLiteral("CumulativeRunTimeTicks")));
    const UserDataFields data = userData(json);
    album.favourite = data.favourite;
    album.playCount = data.playCount;
    album.lastPlayed = data.lastPlayed;
    album.coverRef = ownPrimary(json);
    if (const auto type = serverReleaseType(json)) {
        album.releaseType = *type;
        album.releaseTypeFromServer = true;
    }
    if (!album.releaseTypeFromServer) {
        QStringList names;
        for (const NamedRef &artist : album.albumArtists)
            names.append(artist.name);
        album.releaseType = classifyRelease({QString(), names, album.trackCount, album.runtimeMs, {}});
    }
    return album;
}

QList<Album> parseAlbums(const QJsonArray &json)
{
    QList<Album> list;
    list.reserve(json.size());
    for (const QJsonValue &value : json)
        list.append(parseAlbum(value.toObject()));
    return list;
}

Artist parseArtist(const QJsonObject &json)
{
    Artist artist;
    artist.id = text(json.value(QStringLiteral("Id")));
    artist.name = text(json.value(QStringLiteral("Name")));
    artist.coverRef = ownPrimary(json);
    const QJsonArray backdrops = json.value(QStringLiteral("BackdropImageTags")).toArray();
    const QString backdrop = backdrops.isEmpty() ? QString() : text(backdrops.at(0));
    if (!backdrop.isEmpty())
        artist.backdropRef = {artist.id, QStringLiteral("Backdrop"), backdrop};
    artist.albumCount = qMax(0, integer(json.value(QStringLiteral("AlbumCount"))));
    if (artist.albumCount == 0)
        artist.albumCount = qMax(0, integer(json.value(QStringLiteral("ChildCount"))));
    artist.trackCount = qMax(0, integer(json.value(QStringLiteral("SongCount"))));
    artist.favourite = userData(json).favourite;
    return artist;
}

QList<Artist> parseArtists(const QJsonArray &json)
{
    QList<Artist> list;
    list.reserve(json.size());
    for (const QJsonValue &value : json)
        list.append(parseArtist(value.toObject()));
    return list;
}

GenreBin parseGenreBin(const QJsonObject &json)
{
    GenreBin genre;
    genre.id = text(json.value(QStringLiteral("Id")));
    genre.name = text(json.value(QStringLiteral("Name")));
    genre.recordCount = qMax(0, integer(json.value(QStringLiteral("AlbumCount"))));
    if (genre.recordCount == 0)
        genre.recordCount = qMax(0, integer(json.value(QStringLiteral("ChildCount"))));
    return genre;
}

Playlist parsePlaylist(const QJsonObject &json)
{
    Playlist playlist;
    playlist.id = text(json.value(QStringLiteral("Id")));
    playlist.name = text(json.value(QStringLiteral("Name")));
    playlist.trackCount = qMax(0, integer(json.value(QStringLiteral("ChildCount"))));
    playlist.runtimeMs = ticksToMs(json.value(QStringLiteral("CumulativeRunTimeTicks")));
    if (playlist.runtimeMs == 0)
        playlist.runtimeMs = ticksToMs(json.value(QStringLiteral("RunTimeTicks")));
    playlist.dateAdded = parseEmbyDate(text(json.value(QStringLiteral("DateCreated"))));
    playlist.coverRef = ownPrimary(json);
    return playlist;
}

QList<Playlist> parsePlaylists(const QJsonArray &json)
{
    QList<Playlist> list;
    list.reserve(json.size());
    for (const QJsonValue &value : json)
        list.append(parsePlaylist(value.toObject()));
    return list;
}

} // namespace strmqt::emby
