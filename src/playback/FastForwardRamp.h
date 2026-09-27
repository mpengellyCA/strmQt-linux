#pragma once

#include <QtGlobal>

#include <array>

// Tap or hold, and how fast a hold goes — for a remote's ⏭.
//
// A tap is a skip (PlayerController::skipForward) and fires on the RELEASE,
// because until then a press is only a possible hold. A press still down after
// kHoldThresholdMs is a hold: fast-forward at 2×, doubling every kStepMs held,
// up to 32×. Letting go ends it and does not also skip.
//
//   · 400 ms: long enough that a brisk tap on a rubber remote button is never
//     read as a hold, short enough that a deliberate hold starts moving before
//     the user wonders whether it registered. A little under the pad's hold-A
//     (kSelectHoldMs, 500 ms): that hold opens a menu over the page, this one
//     only speeds the picture up, and a mistaken one costs nothing to undo.
//   · 2× → 4× → 8× → 16× → 32×: doubling is the scale every DVR and Kodi
//     taught, so each step is recognisably "faster" rather than a creep, and
//     five steps covers a two-hour film in minutes.
//   · 1 s per step: long enough to see a rate and let go at it, so 32× is
//     4.4 s of holding away.
//
// Pure state — the caller owns the clock and the timer and asks update() at
// its own cadence — because what makes this subtle is an ordering (a release
// arriving after the hold began must not also skip), and an ordering is what a
// real remote cannot be made to reproduce on demand. Same reasoning as
// input/GamepadDecision.h.
namespace strmqt {

class FastForwardRamp
{
public:
    static constexpr qint64 kHoldThresholdMs = 400;
    static constexpr qint64 kStepMs = 1000;
    static constexpr std::array<qreal, 5> kRates{2.0, 4.0, 8.0, 16.0, 32.0};
    // Some remotes do not hold a key down: held, they send a fresh press and
    // release every few tens of milliseconds. Each pair would be a tap, and a
    // tap is a chapter — so a held button raced through the film. A press this
    // soon after the last release is taken as that stutter and ignored: such a
    // remote gets one skip per hold, never a burst. 150 ms is several times
    // those remotes' repeat period and well under how fast a person taps.
    static constexpr qint64 kDebounceMs = 150;

    enum class Release {
        Ignored, // no press to end (a release after cancel(), or a stray one)
        Tap,     // released before the threshold: skip
        HoldEnded, // released after it: stop fast-forwarding, no skip
    };

    // The rate a press held for `heldMs` has reached; 0 while it could still be
    // a tap.
    static constexpr qreal rateAt(qint64 heldMs)
    {
        if (heldMs < kHoldThresholdMs)
            return 0.0;
        const qint64 step = (heldMs - kHoldThresholdMs) / kStepMs;
        const auto last = static_cast<qint64>(kRates.size()) - 1;
        return kRates[static_cast<std::size_t>(step < last ? step : last)];
    }

    // False when already down — a second press before a release (an
    // auto-repeat that slipped through) must not restart the ramp — and when
    // it follows the last release within kDebounceMs.
    bool press(qint64 nowMs)
    {
        if (m_down)
            return false;
        if (m_released && nowMs - m_lastReleaseMs < kDebounceMs)
            return false;
        m_down = true;
        m_pressedAtMs = nowMs;
        m_rate = 0.0;
        return true;
    }

    // Advances the ramp; true when the rate changed (including 0 → 2×, which
    // is the moment a press becomes a hold).
    bool update(qint64 nowMs)
    {
        if (!m_down)
            return false;
        const qreal rate = rateAt(nowMs - m_pressedAtMs);
        if (rate == m_rate)
            return false;
        m_rate = rate;
        return true;
    }

    Release release(qint64 nowMs)
    {
        // Every release restarts the debounce window, an ignored one included:
        // a stuttering remote's later pairs must keep falling inside it.
        m_released = true;
        m_lastReleaseMs = nowMs;
        if (!m_down)
            return Release::Ignored;
        // Judged on the time held as well as on update(): a timer that had not
        // yet ticked past the threshold must not turn a long press into a skip.
        const bool held = m_rate > 0.0 || nowMs - m_pressedAtMs >= kHoldThresholdMs;
        reset();
        return held ? Release::HoldEnded : Release::Tap;
    }

    // Abandon the press without a skip: the key's release will not come (the
    // window lost focus) or no longer means anything (the item changed).
    void cancel() { reset(); }

    bool isDown() const { return m_down; }
    bool isHolding() const { return m_rate > 0.0; }
    qreal rate() const { return m_rate; }

private:
    void reset()
    {
        m_down = false;
        m_pressedAtMs = 0;
        m_rate = 0.0;
    }

    bool m_down = false;
    bool m_released = false;
    qint64 m_lastReleaseMs = 0;
    qint64 m_pressedAtMs = 0;
    qreal m_rate = 0.0;
};

} // namespace strmqt
