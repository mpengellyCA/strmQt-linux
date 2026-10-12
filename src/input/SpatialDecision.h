#pragma once

#include <QList>
#include <QRectF>

namespace strmqt {

// Which control a direction key lands on, decided from rectangles alone.
//
// Every page used to answer this for itself — a KeyNavigation chain here, an
// index walk over a list of sections there — and each answer had to be kept
// true by hand as the page grew, hid a region or loaded late. The geometry
// already says where "down" is. This is the one place that reads it.
//
// Kept pure and free of QtQuick for the same reason StickDecision.h is: the
// interesting cases are layouts, and a layout written as six rectangles in a
// test is checkable where a page on a screen is not.
//
// The rule is Android's FocusFinder, which a decade of TV interfaces has
// shaken down: a candidate must lie in the direction asked for; one that
// overlaps the source's own row or column (its "beam") is preferred over one
// that does not; among equals the nearest wins, with distance along the
// direction of travel counting far more than distance off to the side.

enum class NavDirection
{
    Left,
    Right,
    Up,
    Down,
};

namespace spatial {

inline bool isHorizontal(NavDirection d)
{
    return d == NavDirection::Left || d == NavDirection::Right;
}

// Is `c` in direction `d` of `s` at all? Partly-overlapping rectangles count
// as long as the candidate's far edge is beyond the source's.
inline bool isCandidate(const QRectF &s, const QRectF &c, NavDirection d)
{
    switch (d) {
    case NavDirection::Left:
        return (s.right() > c.right() || s.left() >= c.right()) && s.left() > c.left();
    case NavDirection::Right:
        return (s.left() < c.left() || s.right() <= c.left()) && s.right() < c.right();
    case NavDirection::Up:
        return (s.bottom() > c.bottom() || s.top() >= c.bottom()) && s.top() > c.top();
    case NavDirection::Down:
        return (s.top() < c.top() || s.bottom() <= c.top()) && s.bottom() < c.bottom();
    }
    return false;
}

// Do the two overlap on the axis perpendicular to travel — is `c` in the row
// (for Left/Right) or the column (for Up/Down) the source sits in?
inline bool inBeam(const QRectF &s, const QRectF &c, NavDirection d)
{
    if (isHorizontal(d))
        return c.bottom() > s.top() && c.top() < s.bottom();
    return c.right() > s.left() && c.left() < s.right();
}

// Wholly past the source's leading edge, not merely further along than it.
inline bool isToDirectionOf(const QRectF &s, const QRectF &c, NavDirection d)
{
    switch (d) {
    case NavDirection::Left:
        return s.left() >= c.right();
    case NavDirection::Right:
        return s.right() <= c.left();
    case NavDirection::Up:
        return s.top() >= c.bottom();
    case NavDirection::Down:
        return s.bottom() <= c.top();
    }
    return false;
}

// Gap between the facing edges along the direction of travel, never negative.
inline qreal majorDistance(const QRectF &s, const QRectF &c, NavDirection d)
{
    qreal gap = 0;
    switch (d) {
    case NavDirection::Left:
        gap = s.left() - c.right();
        break;
    case NavDirection::Right:
        gap = c.left() - s.right();
        break;
    case NavDirection::Up:
        gap = s.top() - c.bottom();
        break;
    case NavDirection::Down:
        gap = c.top() - s.bottom();
        break;
    }
    return gap > 0 ? gap : 0;
}

// The same, measured to the candidate's FAR edge: how far one would have to
// travel to be clear of it.
inline qreal majorDistanceToFarEdge(const QRectF &s, const QRectF &c, NavDirection d)
{
    qreal gap = 0;
    switch (d) {
    case NavDirection::Left:
        gap = s.left() - c.left();
        break;
    case NavDirection::Right:
        gap = c.right() - s.right();
        break;
    case NavDirection::Up:
        gap = s.top() - c.top();
        break;
    case NavDirection::Down:
        gap = c.bottom() - s.bottom();
        break;
    }
    return gap > 1 ? gap : 1;
}

// How far off to the side the candidate sits. Zero for anything in the beam:
// a full-width shelf under a narrow button is straight below it, not "off to
// the side by half a screen" as a centre-to-centre measure would have it.
inline qreal minorDistance(const QRectF &s, const QRectF &c, NavDirection d)
{
    if (inBeam(s, c, d))
        return 0;
    if (isHorizontal(d))
        return c.bottom() <= s.top() ? s.top() - c.bottom() : c.top() - s.bottom();
    return c.right() <= s.left() ? s.left() - c.right() : c.left() - s.right();
}

inline qreal weightedDistance(const QRectF &s, const QRectF &c, NavDirection d)
{
    const qreal major = majorDistance(s, c, d);
    const qreal minor = minorDistance(s, c, d);
    return 13 * major * major + minor * minor;
}

// Does being in the beam settle it for `a` over `b`?
inline bool beamBeats(const QRectF &s, const QRectF &a, const QRectF &b, NavDirection d)
{
    const bool aInBeam = inBeam(s, a, d);
    const bool bInBeam = inBeam(s, b, d);
    if (bInBeam || !aInBeam)
        return false;
    // `b` is off the beam. If it is not even cleanly past the source it loses
    // outright; sideways travel always prefers the row it is in.
    if (!isToDirectionOf(s, b, d))
        return true;
    if (isHorizontal(d))
        return true;
    // Vertically, an off-beam candidate that is much nearer may still win.
    return majorDistance(s, a, d) < majorDistanceToFarEdge(s, b, d);
}

inline bool isBetter(const QRectF &s, const QRectF &a, const QRectF &b, NavDirection d)
{
    if (!isCandidate(s, a, d))
        return false;
    if (!isCandidate(s, b, d))
        return true;
    if (beamBeats(s, a, b, d))
        return true;
    if (beamBeats(s, b, a, d))
        return false;
    return weightedDistance(s, a, d) < weightedDistance(s, b, d);
}

} // namespace spatial

// Index of the candidate a step in `direction` from `source` lands on, or -1
// when there is nothing that way. Ties keep the earlier candidate, so the
// answer does not depend on anything but the rectangles and their order.
inline int pickSpatialTarget(const QRectF &source, const QList<QRectF> &candidates,
                             NavDirection direction)
{
    int best = -1;
    for (int i = 0; i < candidates.size(); ++i) {
        const QRectF &c = candidates.at(i);
        if (!spatial::isCandidate(source, c, direction))
            continue;
        if (best < 0 || spatial::isBetter(source, c, candidates.at(best), direction))
            best = i;
    }
    return best;
}

// Index of the candidate nearest `point`'s rectangle by centre, or -1 for an
// empty list. Used when focus has to be put *somewhere* — a control vanished
// under the cursor — and the kindest somewhere is the closest thing to where
// the user was.
inline int pickNearest(const QRectF &where, const QList<QRectF> &candidates)
{
    int best = -1;
    qreal bestDistance = 0;
    for (int i = 0; i < candidates.size(); ++i) {
        const QPointF delta = candidates.at(i).center() - where.center();
        const qreal distance = delta.x() * delta.x() + delta.y() * delta.y();
        if (best < 0 || distance < bestDistance) {
            best = i;
            bestDistance = distance;
        }
    }
    return best;
}

} // namespace strmqt
