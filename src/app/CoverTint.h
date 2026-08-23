#pragma once

#include <QColor>
#include <QImage>

// The sleeve lights the room (MUSIC.md §4, Rule 2): one accent colour sampled
// from a cover, washed behind the now-playing view and the album/artist page
// headers as a soft vertical gradient into Theme.ground.
//
// ── Why the clamp is the whole design decision ────────────────────────────────
// Projection-booth amber exists because it sits outside the hue range of most
// poster art, so a focus ring is never lost against the artwork it frames
// (ARCHITECTURE.md §4). A wash sampled straight off a cover would put the ring
// back inside that range — a saturated red sleeve behind an amber ring is
// exactly the failure the accent was chosen to avoid.
//
// So the sampled colour is forced into a box before anything draws it:
//
//     HSL saturation ≤ 0.55
//     HSL lightness  ∈ [0.10, 0.22]
//     drawn at no more than kMaxWashOpacity
//
// and a cover with no colour worth washing with — a greyscale sleeve, a pure
// black one, a transparent PNG — yields an INVALID colour, which callers render
// as Theme.surfaceColor. No wash is always better than an illegible one.
//
// Everything here is pure: a QImage in, a QColor out, no network and no cache.
// That is what makes the clamp testable, and tests/unit/tst_cover_tint.cpp is
// where the box is actually held shut — including the adversarial covers.
namespace strmqt::covertint {

// ── The box ──────────────────────────────────────────────────────────────────
inline constexpr qreal kMaxSaturation = 0.55;
inline constexpr qreal kMinLightness = 0.10;
inline constexpr qreal kMaxLightness = 0.22;
// The ceiling the wash is drawn at. Lives here rather than in Theme.qml because
// it is one of the four numbers the clamp is made of and it is tested with the
// other three; Theme re-exports it so QML still reads one token source.
inline constexpr qreal kMaxWashOpacity = 0.22;

// Below this HSL saturation there is no hue to carry — the "wash" would be a
// grey rectangle, which is the fallback with extra steps.
inline constexpr qreal kMinSaturation = 0.08;

// 8-bit round-tripping through RGB moves S and L by well under this; the
// tolerance exists so a post-condition check is not a coin toss on rounding.
inline constexpr qreal kClampEpsilon = 0.01;

// ── The clamp ────────────────────────────────────────────────────────────────
// Forces `raw` into the box, preserving its hue. Returns an INVALID colour when
// there is nothing to preserve: an invalid input, or one with too little
// saturation to read as anything but grey.
QColor clampWashTint(const QColor &raw);

// True when `colour` is valid and inside the box. The post-condition of
// clampWashTint(), asserted by the tests on every input they can invent.
bool withinClamp(const QColor &colour);

// Samples `image` for its dominant colour and clamps it. Returns an INVALID
// colour — the Theme.surfaceColor fallback — when the image is null, fully
// transparent, or carries no usable hue.
QColor dominantWashTint(const QImage &image);

} // namespace strmqt::covertint
