import QtQuick
import QtQuick.Effects

// Tier shim (spec §4.3): shows `source` only where `maskSource` is opaque.
MultiEffect {
    maskEnabled: true
    maskThresholdMin: 0.5
    maskSpreadAtMin: 1.0
}
