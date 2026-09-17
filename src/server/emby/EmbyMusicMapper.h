#pragma once

#include <QDateTime>
#include <QJsonArray>
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

music::Track parseTrack(const QJsonObject &json);
QList<music::Track> parseTracks(const QJsonArray &json);
music::Album parseAlbum(const QJsonObject &json);
QList<music::Album> parseAlbums(const QJsonArray &json);
music::Artist parseArtist(const QJsonObject &json);
QList<music::Artist> parseArtists(const QJsonArray &json);
music::GenreBin parseGenreBin(const QJsonObject &json);
music::Playlist parsePlaylist(const QJsonObject &json);
QList<music::Playlist> parsePlaylists(const QJsonArray &json);

} // namespace strmqt::emby
