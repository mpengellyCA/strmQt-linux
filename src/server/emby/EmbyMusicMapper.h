#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <optional>

#include "server/dto/music/MusicTypes.h"

// Emby JSON → music DTOs (Crate spec §3.3). Tolerant like EmbyDtoMapper: unknown
// keys are ignored, wrong types read as defaults, nothing throws.
namespace strmqt::emby {

music::AudioFormat deriveAudioFormat(const QString &codec, int bitDepth, int sampleRate,
                                     int bitrate, int channels);
music::AudioFormat parseAudioFormat(const QJsonObject &item);

struct FeaturedSplit
{
    QString title;
    QStringList names;
};
FeaturedSplit splitFeatured(const QString &title);

QDateTime parseEmbyDate(const QString &text);
std::optional<music::ReleaseType> releaseTypeFromTag(const QString &tag);

struct ReleaseEvidence
{
    QString serverTag;
    QStringList albumArtistNames;
    int trackCount = 0;
    qint64 runtimeMs = 0;
    QStringList trackPrimaryArtists; // first performer of each track
};

// Crate spec §3.3: server tag → "Various Artists" → ≥4 other performers →
// Single (1–3 tracks, <20 min) → EP (≤7 tracks, <35 min) → Album.
music::ReleaseType classifyRelease(const ReleaseEvidence &evidence);
QList<music::Disc> groupDiscs(const QList<music::Track> &tracks);
QString dominantFormat(const QList<music::Track> &tracks);
// Fills trackCount, runtimeMs, discCount and formatSummary from the tracks, and
// re-classifies the release unless the server supplied its type.
void refineAlbumFromTracks(music::Album &album, const QList<music::Track> &tracks);

music::Track parseTrack(const QJsonObject &json);
QList<music::Track> parseTracks(const QJsonArray &json);
music::Album parseAlbum(const QJsonObject &json);
QList<music::Album> parseAlbums(const QJsonArray &json);
music::Artist parseArtist(const QJsonObject &json);
QList<music::Artist> parseArtists(const QJsonArray &json);
music::GenreBin parseGenreBin(const QJsonObject &json);
music::Playlist parsePlaylist(const QJsonObject &json);
QList<music::Playlist> parsePlaylists(const QJsonArray &json);

// A lyrics sidecar as Emby serves it: its subtitle JSON ({"TrackEvents":[…]})
// or the {"Lyrics":[{Start,Text}]} shape. Untimed when no line has a time.
// V4 (verifications file): an untimed sidecar sometimes arrives as a single
// TrackEvent whose Text joins the real lines with "; " — that case is split
// back into separate lines, keeping empty segments as stanza breaks.
QList<music::LyricLine> parseLyrics(const QJsonDocument &doc);

} // namespace strmqt::emby
