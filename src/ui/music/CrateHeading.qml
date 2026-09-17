import QtQuick
import StrmQt

// Crate display type (spec §2): wide Archivo caps. The section strip, shelf
// headings, hero titles and genre bins all speak in this one voice, so its
// settings live here and nowhere else.
Text {
    id: heading

    property int pixelSize: Theme.crateShelfHeading

    color: Theme.textPrimaryColor
    font.family: Theme.fontDisplay
    font.pixelSize: heading.pixelSize
    font.weight: Theme.crateDisplayWeight
    font.variableAxes: Theme.crateDisplayAxes
    font.capitalization: Font.AllUppercase
    // Tracking is an em value; Qt wants pixels.
    font.letterSpacing: Theme.crateDisplayTracking * heading.pixelSize
    textFormat: Text.PlainText
    maximumLineCount: 1
    elide: Text.ElideRight
}
