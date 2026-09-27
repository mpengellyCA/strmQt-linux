import QtQuick

// Tier shim (spec 2026-09-27 §4.3): TextMetrics with tabular figures, so counters and
// times do not jitter. font.features is Qt 6.6+.
TextMetrics {
    font.features: ({ "tnum": 1 })
}
