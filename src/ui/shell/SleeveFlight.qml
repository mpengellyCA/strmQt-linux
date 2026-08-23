import QtQuick
import QtQuick.Window
import StrmQt

// SleeveFlight — one continuous sleeve (MUSIC.md §4, "The signature").
//
// The cover is ONE object that grows out of the docked bar into the full-screen
// view and shrinks back. Not a cross-fade between two screens: the same square,
// travelling between two rectangles, with the surrounding chrome fading around
// it. It is the transition the user performs most, which is why it is the one
// place in the music plan that spends real animation budget — Theme.animSlow
// (420 ms) and Theme.easeEmphasis, which is what that easing token is for.
//
// ── Mechanics ────────────────────────────────────────────────────────────────
// This lives in Main.qml above the StackView, so it is not destroyed when the
// player page is pushed or popped — which is exactly what a cross-fade cannot
// avoid and what makes the square continuous. Both endpoints hide their own
// copy while it is `active`, so there is only ever one sleeve on screen.
//
// ── Nothing may be stranded ─────────────────────────────────────────────────
// A cover stuck invisible is a worse bug than no animation at all, so the
// endpoints do not get *told* to hide and later told to show: they BIND their
// opacity to `active`. There is no "unhide" call to miss. However this ends —
// finished, interrupted by the opposite gesture, the track changing underneath
// it, the player being closed mid-flight, or the arm timing out because the
// full-screen hero never laid out — `active` goes false and both copies come
// back on their own.
//
// The watchdog below is the last line of that guarantee: whatever happens,
// `active` is false again within three animation lengths.
Item {
    id: flight

    // The sleeve to draw. The owner binds it to the same posterUrl both
    // endpoints draw, so a track change mid-flight swaps the picture and
    // nothing else — the square keeps travelling.
    property string source: ""

    // True from the moment the sleeve is placed until it lands or is cancelled.
    // This is what the endpoints hide against.
    readonly property bool active: sleeve.visible

    // ── The sizes this flight is allowed to ask for ─────────────────────────
    // QQuickPixmapCache keys on (url, sourceSize) and the provider puts the
    // width straight into the request as `maxWidth`, so a size no endpoint
    // asks for is a cache miss AND a fresh download — and the square would fly
    // empty for most of its 420 ms while it arrived. Both endpoints size their
    // Image as `frame width x devicePixelRatio`, so the flight takes its sizes
    // from the very rectangles it is handed, by the same rule.
    //
    // Which one is resident depends on the direction of travel, so there are
    // two:
    //
    //   · takeoffWidth — the endpoint the square just LEFT. Whatever else is
    //     true, that copy was on screen a frame ago, so its pixmap is decoded
    //     and the first frame of the flight has something to draw.
    //   · sharpWidth — the largest endpoint this flight touches. Expanding,
    //     that is the hero, which is still loading when the square lifts off:
    //     it is drawn over the stand-in the moment it arrives, and because the
    //     hero's own Image asks for exactly this size, it is one shared pixmap
    //     rather than a third request. Minimising, it is the takeoff size
    //     itself and the second Image never loads at all.
    property int takeoffWidth: 0
    property int sharpWidth: 0

    // The sleeve has arrived (or been cancelled). The owner uses this to hand
    // focus on, not to un-hide anything.
    signal landed

    // Put the sleeve somewhere without animating — the start of a flight. On
    // expand this runs before the player page even exists, so the square is
    // already lifted off the bar while the page builds behind it.
    function place(frame: rect, radius: real): void {
        travel.stop();
        sleeve.x = frame.x;
        sleeve.y = frame.y;
        sleeve.width = frame.width;
        sleeve.height = frame.height;
        sleeve.radius = radius;
        sleeve.visible = frame.width > 0 && frame.height > 0;
        flight.takeoffWidth = Math.round(frame.width * Screen.devicePixelRatio);
        flight.sharpWidth = flight.takeoffWidth;
    }

    // Travel to the other rectangle. Safe to call while a flight is running:
    // the animations restart from wherever the square has got to, which is what
    // makes "expand, then immediately minimise" a turn rather than a jump.
    function flyTo(frame: rect, radius: real): void {
        if (!sleeve.visible || frame.width <= 0 || frame.height <= 0) {
            flight.cancel();
            return;
        }
        travel.stop();
        // Never shrinks: turning the square around mid-expand must not throw
        // away the large pixmap it has just been handed.
        flight.sharpWidth = Math.max(flight.sharpWidth,
                                     Math.round(frame.width * Screen.devicePixelRatio));
        toX.from = sleeve.x;
        toX.to = frame.x;
        toY.from = sleeve.y;
        toY.to = frame.y;
        toWidth.from = sleeve.width;
        toWidth.to = frame.width;
        toHeight.from = sleeve.height;
        toHeight.to = frame.height;
        toRadius.from = sleeve.radius;
        toRadius.to = radius;
        travel.start();
    }

    function cancel(): void {
        travel.stop();
        sleeve.visible = false;
        flight.landed();
    }

    // Three animation lengths. Long enough that it never truncates a real
    // flight, short enough that a stuck sleeve is a flicker rather than a bug
    // report. Restarts whenever the sleeve is placed, so an arm that never gets
    // its destination is covered by the same net as a flight that never ends.
    Timer {
        id: watchdog

        running: flight.active
        interval: Theme.animSlow * 3
        onTriggered: flight.cancel()
    }

    ParallelAnimation {
        id: travel

        NumberAnimation {
            id: toX
            target: sleeve
            property: "x"
            duration: Theme.animSlow
            easing.type: Theme.easeEmphasis
        }
        NumberAnimation {
            id: toY
            target: sleeve
            property: "y"
            duration: Theme.animSlow
            easing.type: Theme.easeEmphasis
        }
        NumberAnimation {
            id: toWidth
            target: sleeve
            property: "width"
            duration: Theme.animSlow
            easing.type: Theme.easeEmphasis
        }
        NumberAnimation {
            id: toHeight
            target: sleeve
            property: "height"
            duration: Theme.animSlow
            easing.type: Theme.easeEmphasis
        }
        NumberAnimation {
            id: toRadius
            target: sleeve
            property: "radius"
            duration: Theme.animSlow
            easing.type: Theme.easeEmphasis
        }

        onFinished: flight.cancel()
    }

    // ── No shadow while travelling, deliberately ────────────────────────────
    // The hero's sleeve carries Theme.elevation4 and the docked bar's does not,
    // so the obvious thing was to give the flying copy a shadow too. It was
    // tried and measured against a real cover, and both ways of writing it are
    // wrong here: a MultiEffect handed this rectangle as a plain `source` maps
    // its auto-padded texture back into the rectangle's own bounds, so the
    // cover travels inset inside a black frame; and layer.effect replaces the
    // rectangle's rendering, which puts the whole moving sleeve at the mercy of
    // an effect running. Either would also pay for one offscreen render target
    // re-rendered on every frame of a resize, which is the exact cost the
    // control library already refuses per item (see StrmCard).
    //
    // NowPlayingPanel's hero casts its shadow from a separate flat rectangle
    // behind the frame for the same reasons. Here there is nothing to cast
    // onto: the square travels flat, and the shadow belongs to the endpoint it
    // lands on — which reads as the sleeve settling onto the surface, and that
    // is what MUSIC.md asked the shadow for in the first place.
    //
    // ── The corners travel exactly as the endpoints draw them ───────────────
    // `clip` is a scissor rectangle, not a rounded mask: a Rectangle with a
    // radius clips a child that fills it to the plain bounding box, so an
    // opaque cover shows square corners whatever the radius says. Measured on
    // Qt 6.11.2 rather than assumed.
    //
    // That is true of BOTH endpoints — MiniPlayer's frame and the hero's frame
    // are the same radius-plus-clip — so nothing pops on landing, and the
    // radius animation above is not dead: it is the only thing that matters
    // while the cover has not decoded yet, which is exactly when the sleeve's
    // own rounded fill is what the eye sees. Dropping it would ADD a corner
    // pop on the one path where the corner is visible. A real mask is the
    // whole control library's business, not this one square's.
    Rectangle {
        id: sleeve

        visible: false
        color: Theme.surfaceColor
        clip: true

        // The size the endpoint the square just left is holding. Sized once
        // per flight, not per frame: the square is resized on every frame of
        // the trip, and a sourceSize that followed it would ask the provider
        // for a different image on every one of them.
        Image {
            anchors.fill: parent
            source: flight.source
            sourceSize.width: flight.takeoffWidth
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
        }

        // Drawn over the stand-in when the trip is towards the bigger
        // endpoint, and only then — minimising, this is the same size as the
        // stand-in and never loads.
        Image {
            anchors.fill: parent
            source: flight.sharpWidth > flight.takeoffWidth ? flight.source : ""
            sourceSize.width: flight.sharpWidth
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            // No fade. It is the same picture at a higher resolution, so a
            // cross-fade would only show the two of them disagreeing about
            // their edges.
            visible: status === Image.Ready
        }
    }
}
