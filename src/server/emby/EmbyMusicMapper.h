#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

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

} // namespace strmqt::emby
