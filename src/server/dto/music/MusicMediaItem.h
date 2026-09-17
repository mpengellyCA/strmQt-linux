#pragma once

#include "server/dto/MediaItem.h"
#include "server/dto/music/MusicTypes.h"

// Bridge to the playback path (ItemActions, PlayQueue, MPRIS), which speaks
// MediaItem. Spec §3.6's Track::toMediaItem(), as free overloads so the DTOs
// stay plain structs.
namespace strmqt::music {

MediaItem toMediaItem(const Track &track);
MediaItem toMediaItem(const Album &album);
MediaItem toMediaItem(const Artist &artist);

} // namespace strmqt::music
