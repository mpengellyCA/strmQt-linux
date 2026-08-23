import QtQuick
import QtQuick.Effects
import StrmQt

// A recoloured SVG glyph from the bundled icon set (`assets/icons/ui` →
// `qrc:/icons`). Every icon in that set is authored as a white stroke-based
// 24×24 drawing, which is what makes a single tint pass exact:
// MultiEffect's colorization multiplies `colorizationColor` by the source
// luminance, so a white source comes out as precisely `color` with the SVG's
// antialiasing preserved. (Verified by pixel comparison against the mask-based
// alternative, which hard-edges the antialiasing.)
//
// Note for headless/offscreen runs: MultiEffect draws nothing under the "null"
// RHI backend used by `-platform offscreen`, so icons are invisible there. That
// is a property of that backend, not of this file — on a real compositor the
// tint is exact.
Item {
    id: icon

    // Icon file basename, e.g. "play" → qrc:/icons/play.svg.
    property string name: ""
    property int size: Theme.iconSize
    property color color: Theme.textPrimaryColor

    // True once the glyph has rasterised; useful for crossfades.
    readonly property bool ready: glyph.status === Image.Ready

    implicitWidth: icon.size
    implicitHeight: icon.size

    Image {
        id: glyph

        anchors.fill: parent
        source: icon.name.length > 0 ? "qrc:/icons/" + icon.name + ".svg" : ""
        // Rasterise the SVG at the size it is actually drawn at, so it stays
        // crisp instead of being scaled from some default bitmap size.
        sourceSize.width: Math.max(1, Math.ceil(icon.width))
        sourceSize.height: Math.max(1, Math.ceil(icon.height))
        fillMode: Image.PreserveAspectFit
        smooth: true
        // Icons are tiny and always on the critical path of a control's first
        // paint; a synchronous load avoids a one-frame hole.
        asynchronous: false
        // The tinted MultiEffect is what gets drawn; this is only its source.
        visible: false

        onStatusChanged: {
            // A typo'd name must be loud. Silently blank icons are how an
            // unreachable button ships.
            if (glyph.status === Image.Error)
                console.warn("StrmIcon: no icon named '" + icon.name + "' (" + glyph.source + ")");
        }
    }

    MultiEffect {
        anchors.fill: parent
        source: glyph
        colorization: 1.0
        colorizationColor: icon.color
        visible: glyph.status === Image.Ready

        Behavior on colorizationColor {
            ColorAnimation {
                duration: Theme.animInstant
                easing.type: Theme.easeInstant
            }
        }
    }
}
