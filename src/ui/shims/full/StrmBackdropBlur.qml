import QtQuick
import QtQuick.Effects

// Tier shim (spec §4.3): the backdrop wash, a heavy blur with the colour
// pulled down. Used as a layer.effect root.
MultiEffect {
    autoPaddingEnabled: false
    blurEnabled: true
    blur: 1.0
    blurMax: 48
    saturation: -0.55
}
