import QtQuick
import Qt5Compat.GraphicalEffects

// Tier shim (spec §4.3), compat tier: FastBlur at MultiEffect's blurMax, then
// Desaturate by the same amount MultiEffect's saturation removes.
FastBlur {
    radius: 48
    transparentBorder: false
    layer.enabled: true
    layer.effect: Desaturate {
        desaturation: 0.55
    }
}
