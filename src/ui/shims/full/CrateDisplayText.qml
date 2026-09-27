import QtQuick
import StrmQt

// Tier shim (spec §4.3): Text in Archivo at the crate display width.
// font.variableAxes is Qt 6.7+.
Text {
    font.variableAxes: Theme.crateDisplayAxes
}
