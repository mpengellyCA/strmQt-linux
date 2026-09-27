import QtQuick
import Qt5Compat.GraphicalEffects

// Tier shim (spec 2026-09-27 §4.3), compat tier (Qt < 6.8): ColorOverlay keeps
// the source's alpha, so a white antialiased glyph comes out as `color` with
// its antialiasing intact, which is what MultiEffect colorization gives.
ColorOverlay {
    // ColorOverlay's own property is `color`, so the contract is met as-is.
}
