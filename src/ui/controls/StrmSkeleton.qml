import QtQuick
import StrmQt

// StrmSkeleton — one shimmer placeholder block (ARCHITECTURE.md).
//
// Deliberately a single primitive rather than a family of "poster skeleton" /
// "row skeleton" components: pages compose rows and grids of these, so the
// shimmer stays one implementation and one animation clock.
//
// Reduced motion: `reduceMotion` collapses the sweep to a flat tint. There is
// no platform token for this yet (Theme has no reducedMotion role), so it is a
// property the caller sets; wiring it to a settings/portal signal is a one-line
// change here once that token exists.
Rectangle {
    id: skeleton

    property bool active: true
    property bool reduceMotion: false

    radius: Theme.radiusCardValue
    color: Theme.surfaceColor
    clip: true
    visible: active

    // The sweep is a wide band of the raised surface travelling left to right.
    // Width 2× so the highlight is fully off-canvas at both ends of the cycle
    // and the loop has no visible seam.
    //
    // Verified in an offscreen run rather than assumed, because the two ways
    // this could have been a static block both look correct in the source:
    // a skeleton whose width is 0 at construction (an anchored or Loader-hosted
    // one) and a skeleton resized mid-cycle both keep sweeping the *new* range —
    // the bindings on `from`/`to` are live and the running animation picks them
    // up. The shimmer also stops on its own when the page is covered, because
    // `running` follows `sweep.visible` and Item visibility is effective, not
    // local.
    Rectangle {
        id: sweep
        width: skeleton.width * 2
        height: skeleton.height
        visible: skeleton.active && !skeleton.reduceMotion
        x: -width

        // The band fades to its own hue at zero alpha, never to `"transparent"`
        // — that constant is transparent *black*, and a gradient interpolated in
        // straight (non-premultiplied) alpha drags the midpoints towards black
        // on the way, which shows up as a grey fringe on both edges of the
        // highlight. Same colour, zero alpha, clean in either interpolation
        // space.
        readonly property color bandEdge: Qt.rgba(0.96, 0.945, 0.918, 0.0)

        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: sweep.bandEdge }
            GradientStop { position: 0.45; color: Theme.hoverTint }
            GradientStop { position: 0.55; color: Theme.hoverTint }
            GradientStop { position: 1.0; color: sweep.bandEdge }
        }

        NumberAnimation on x {
            running: sweep.visible
            loops: Animation.Infinite
            from: -sweep.width
            to: skeleton.width
            duration: Theme.animAmbient
            easing.type: Theme.easeAmbient
        }
    }

    // Static fallback so a reduced-motion skeleton still reads as "content
    // pending" rather than as an empty card.
    Rectangle {
        anchors.fill: parent
        visible: skeleton.active && skeleton.reduceMotion
        color: Theme.hoverTint
    }
}
