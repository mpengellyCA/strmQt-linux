import QtQuick
import Qt5Compat.GraphicalEffects

// Tier shim (spec §4.3), compat tier. The edge is the mask item's own
// antialiasing rather than MultiEffect's spread: marginally harder.
OpacityMask {
}
