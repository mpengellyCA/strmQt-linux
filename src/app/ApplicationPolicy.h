#pragma once

namespace strmqt {

inline bool shouldSuspendLiveUpdates(bool foreground, bool playbackActive, bool isAudio)
{
    return !foreground || (playbackActive && !isAudio);
}

inline bool shouldInhibitDisplay(bool playbackActive, bool paused, bool isAudio)
{
    return playbackActive && !paused && !isAudio;
}

} // namespace strmqt
