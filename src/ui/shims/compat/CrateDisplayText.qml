import QtQuick

// Tier shim (spec §4.3), compat tier: no font.variableAxes before Qt 6.7, so
// Archivo renders at its default width (about 17% narrower); the weight still
// comes from font.weight (spec §3).
Text {
}
