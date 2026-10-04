#pragma once

#include <algorithm>
#include <cstdint>

namespace strmqt {

// The clock a frame's MPV_RENDER_PARAM_NEXT_FRAME_INFO target_time is in.
//
// render.h says "the same unit and base as mpv_get_time_us()", but libmpv 2.5
// delivers it in the mpv_get_time_ns() base (measured on libmpv 2.5 during
// implementation): waiting on it as microseconds holds every frame ~1000x too
// long. The 0.37 floor is unverified either way, so the base is read off the
// value itself.
enum class MpvClockBase { Unknown, Microseconds, Nanoseconds };

// mpv's own wait is bounded by the frame duration; this only guards against a
// target time that has gone wrong (a clock jump).
inline constexpr int64_t kMaxTargetWaitUs = 1'000'000;

// The clock `targetTime` lies nearer to. `decisive` is false when the other
// clock is not at least ten times farther away, which happens only within a
// frame or so of mpv's clock origin; such a choice is fine for one frame but
// not worth keeping.
inline MpvClockBase nearerMpvClock(int64_t targetTime, int64_t nowNs, int64_t nowUs,
                                   bool *decisive = nullptr)
{
    const auto distance = [](int64_t a, int64_t b) { return a > b ? a - b : b - a; };
    const int64_t toNs = distance(targetTime, nowNs);
    const int64_t toUs = distance(targetTime, nowUs);
    const bool ns = toNs <= toUs;
    if (decisive)
        *decisive = ns ? toNs <= toUs / 10 : toUs <= toNs / 10;
    return ns ? MpvClockBase::Nanoseconds : MpvClockBase::Microseconds;
}

// Microseconds from now until `targetTime`, read in `base`: 0 for no target or
// one already past, capped at kMaxTargetWaitUs.
inline int64_t targetWaitUs(int64_t targetTime, MpvClockBase base, int64_t nowNs, int64_t nowUs)
{
    if (targetTime <= 0 || base == MpvClockBase::Unknown)
        return 0;
    const int64_t waitUs = base == MpvClockBase::Nanoseconds ? (targetTime - nowNs) / 1000
                                                             : targetTime - nowUs;
    return std::clamp<int64_t>(waitUs, 0, kMaxTargetWaitUs);
}

} // namespace strmqt
