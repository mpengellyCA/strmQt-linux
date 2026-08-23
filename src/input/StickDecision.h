#pragma once

#include <cstdlib>

namespace strmqt {

// Which way a thumbstick is being pushed, decided from BOTH axes at once.
//
// Kept as a pure function, free of SDL and of GamepadManager's state, because
// it is the one piece of gamepad handling that is easy to get subtly wrong and
// impossible to check by reading: the interesting cases are diagonals, and a
// diagonal is not a request to move two ways at once — it is an imprecise
// request to move one way.
//
// Treating X and Y as independent axes with one threshold is what made sideways
// scrolling drift vertically: a push toward 2 o'clock crossed both.
enum class StickAxis
{
    None = -1,
    Horizontal = 0,
    Vertical = 1,
};

struct StickTuning
{
    int threshold = 12'000;  // horizontal activation, of ±32767
    int release = 8'000;     // lower than activation, so a held edge does not stutter
    // Vertical starts higher: a hand rolls up-down far more easily than it rolls
    // left-right, so up/down is the direction that gets hit by accident.
    int verticalThreshold = 16'000;
    double dominance = 1.5;  // how far an axis must lead to claim a still stick
    double takeover = 2.2;   // how far the other must lead to steal it mid-gesture
};

// `owned` is the axis currently driving, or None. Returning None means "inside
// the deadzone, or in the diagonal wedge where the user has not said which way
// they mean" — in both cases nothing should move.
inline StickAxis decideStickAxis(int x, int y, StickAxis owned,
                                 const StickTuning &tuning = StickTuning())
{
    const int magX = std::abs(x);
    const int magY = std::abs(y);

    const bool ownedByX = owned == StickAxis::Horizontal;
    const bool ownedByY = owned == StickAxis::Vertical;

    const int enterX = ownedByX ? tuning.release : tuning.threshold;
    const int enterY = ownedByY ? tuning.release : tuning.verticalThreshold;

    // The owning axis proves nothing to keep going — it only has to stay out of
    // the deadzone. Requiring it to keep out-leading the other made the stick go
    // DEAD mid-gesture whenever the hand drifted: neither axis qualified, and a
    // held stick simply stopped moving. Ownership changes hands only when the
    // other axis clears `takeover` on its own.
    const double ratioX = ownedByX ? 0.0 : (ownedByY ? tuning.takeover : tuning.dominance);
    const double ratioY = ownedByY ? 0.0 : (ownedByX ? tuning.takeover : tuning.dominance);

    const bool wantX = magX >= enterX && magX >= magY * ratioX;
    const bool wantY = magY >= enterY && magY >= magX * ratioY;

    if (wantX && (!wantY || magX >= magY))
        return StickAxis::Horizontal;
    if (wantY)
        return StickAxis::Vertical;
    return StickAxis::None;
}

} // namespace strmqt
