import QtQuick
import QtQuick.Effects

// Tier shim (spec 2026-09-27 §4.3): recolours a white glyph to `color`.
// Full tier: MultiEffect colorization, exactly what StrmIcon did before.
MultiEffect {
    property color color: "white"

    colorization: 1.0
    colorizationColor: color
}
