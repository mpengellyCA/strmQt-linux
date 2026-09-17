import QtQuick
import StrmQt

// The small mono line above a heading (spec §2): Plex Mono, uppercase, wide
// tracking, tabular figures so "12 ADDED THIS WEEK" does not jitter as it counts.
Text {
    id: kicker

    color: Theme.textSecondaryColor
    font.family: Theme.fontMono
    font.pixelSize: Theme.crateKickerSize
    font.capitalization: Font.AllUppercase
    font.letterSpacing: Theme.crateKickerTracking * Theme.crateKickerSize
    font.features: ({ "tnum": 1 })
    textFormat: Text.PlainText
    maximumLineCount: 1
    elide: Text.ElideRight
}
