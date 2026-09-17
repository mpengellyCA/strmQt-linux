#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include "server/dto/MediaItem.h"

// Music DTOs (Crate spec §3.2). Value types shaped for the music UI, not for the
// wire: EmbyMusicMapper builds them, MusicRepository composes them. Every field
// has a default so a partial server answer still yields a drawable record.
namespace strmqt::music {

using ImageRef = MediaItem::ImageRef;

struct NamedRef
{
    QString id;
    QString name;
    bool operator==(const NamedRef &) const = default;
};
using ArtistRef = NamedRef;
using GenreRef = NamedRef;

struct AudioFormat
{
    QString codec;      // lower-case wire codec: "flac", "mp3", "dsd_lsbf"
    int bitDepth = 0;
    int sampleRate = 0; // Hz
    int bitrate = 0;    // bits per second
    int channels = 0;
    QString badge;      // "FLAC 24/96", "MP3 320", "DSD64"; empty = draw nothing
    bool isLossless = false;
    bool isHiRes = false;

    bool isValid() const { return !codec.isEmpty(); }
    bool operator==(const AudioFormat &) const = default;
};

enum class ReleaseType { Album, EP, Single, Compilation };

inline QString releaseTypeName(ReleaseType type)
{
    switch (type) {
    case ReleaseType::EP: return QStringLiteral("EP");
    case ReleaseType::Single: return QStringLiteral("Single");
    case ReleaseType::Compilation: return QStringLiteral("Compilation");
    case ReleaseType::Album: break;
    }
    return QStringLiteral("Album");
}

struct Track
{
    QString id;
    QString title;
    QList<ArtistRef> artists;
    QList<ArtistRef> albumArtists;
    QString albumId;
    QString albumTitle;
    int discNumber = 0;
    int trackNumber = 0;
    qint64 runtimeMs = 0;
    qint64 positionMs = 0;
    bool favourite = false;
    bool played = false;
    int playCount = 0;
    QDateTime lastPlayed;
    QDateTime dateAdded;
    AudioFormat format;
    ImageRef coverRef;
    QString playlistItemId; // set only on playlist entries
    // Derived by the mapper:
    QList<ArtistRef> featured;         // "feat." names and extra track artists
    QString displayTitle;              // title with the "(feat. …)" suffix removed
    bool differsFromAlbumArtist = false;
};

struct Album
{
    QString id;
    QString title;
    QList<ArtistRef> albumArtists;
    int year = 0;
    QDateTime premiereDate;
    QList<GenreRef> genres;
    QStringList studios;
    QDateTime dateAdded;
    int trackCount = 0;
    qint64 runtimeMs = 0;
    bool favourite = false;
    int playCount = 0;
    QDateTime lastPlayed;
    ImageRef coverRef;
    ReleaseType releaseType = ReleaseType::Album;
    bool releaseTypeFromServer = false; // true: never re-classified
    int discCount = 0;                  // 0 until tracks are known
    QString formatSummary;              // dominant badge, once tracks are known
};

struct Disc
{
    int number = 1;
    qint64 runtimeMs = 0;
    QList<Track> tracks;
};

struct AlbumSleeve
{
    Album album;
    QList<Disc> discs;
    QList<Album> moreByArtist;
};

struct Artist
{
    QString id;
    QString name;
    ImageRef coverRef;
    ImageRef backdropRef;
    int albumCount = 0;
    int trackCount = 0;
    bool favourite = false;
};

struct ArtistProfile
{
    Artist artist;
    QList<Album> albums;        // Album and Compilation releases filed under the artist
    QList<Album> epsAndSingles;
    QList<Album> appearsOn;     // albums they perform on but are not filed under
    QList<Track> topTracks;
    QList<Artist> similar;
};

struct GenreBin
{
    QString id;
    QString name;
    int recordCount = 0;
    QList<ImageRef> covers; // up to 3
};

enum class StationKind { HeavyRotation, Favourites, DeepCuts, MoreLike, ShuffleAll };

struct Station
{
    StationKind kind = StationKind::ShuffleAll;
    QString label;
    QString seedId; // MoreLike only: the artist id
    QList<ImageRef> covers;
};

struct ContinueListening
{
    Album album;
    Track resumeTrack;
    int resumeIndex = -1;
    double progress = 0.0; // 0..1 through the album
    bool isValid() const { return !album.id.isEmpty() && resumeIndex >= 0; }
};

struct Playlist
{
    QString id;
    QString name;
    int trackCount = 0;
    qint64 runtimeMs = 0;
    QDateTime dateAdded;
    ImageRef coverRef; // Emby renders a collage Primary for playlists
};

template<class T>
struct Page
{
    QList<T> items;
    int totalRecordCount = 0;
    int startIndex = 0;
};

} // namespace strmqt::music
