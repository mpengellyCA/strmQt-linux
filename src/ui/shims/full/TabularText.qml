import QtQuick

// Tier shim (spec 2026-09-27 §4.3): Text with tabular figures, so counters and
// times do not jitter. font.features is Qt 6.6+.
Text {
    font.features: ({ "tnum": 1 })
}
