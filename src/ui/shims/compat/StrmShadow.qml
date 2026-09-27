import QtQuick
import Qt5Compat.GraphicalEffects

// Tier shim (spec §4.3), compat tier. MultiEffect's shadowBlur is a 0..1
// fraction of blurMax (default 32 px); DropShadow wants a radius in pixels.
DropShadow {
    property var elevation: ({ blur: 0, y: 0, opacity: 0 })
    property color shadowColor: "black"

    transparentBorder: true
    horizontalOffset: 0
    verticalOffset: elevation.y
    radius: elevation.blur * 32
    samples: Math.min(64, 2 * Math.ceil(radius) + 1)
    color: Qt.rgba(shadowColor.r, shadowColor.g, shadowColor.b, shadowColor.a * elevation.opacity)
}
