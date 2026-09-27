import QtQuick
import QtQuick.Effects

// Tier shim (spec §4.3): a drop shadow described by a Theme.elevationN
// ({blur, y, opacity}). Usable as a sibling or as a layer.effect root.
MultiEffect {
    property var elevation: ({ blur: 0, y: 0, opacity: 0 })
    // `shadowColor` is MultiEffect's own property, so it is the contract name.

    autoPaddingEnabled: true
    shadowEnabled: true
    shadowBlur: elevation.blur
    shadowVerticalOffset: elevation.y
    shadowOpacity: elevation.opacity
}
