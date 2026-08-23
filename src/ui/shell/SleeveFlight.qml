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
    Rectangle {
        id: sleeve

        visible: false
        color: Theme.surfaceColor
        clip: true

        Image {
            anchors.fill: parent
            source: flight.source
            // Sized from the WINDOW, not from the square. The square is
            // resized on every frame of the flight, and a sourceSize that
            // followed it would ask the provider for a different image on
            // every one of them; this asks once, at a size no endpoint can
            // exceed, and lets the GPU scale it for the trip.
            sourceSize.width: Math.round(Math.max(flight.width, flight.height) *
                                         Screen.devicePixelRatio / 2)
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
        }
    }
}
