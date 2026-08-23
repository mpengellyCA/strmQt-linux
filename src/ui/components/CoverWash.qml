import QtQuick
import StrmQt

// CoverWash — the sleeve lights the room (MUSIC.md §4, Rule 2).
//
// One accent colour sampled from a cover, clamped, drawn as a soft vertical
// gradient into Theme.ground. Behind the now-playing view, and behind the album
// and artist page headers.
//
// ── What this file is NOT allowed to decide ────────────────────────────────
// The colour. It arrives from CoverTint already inside the box — saturation
// ≤ 0.55, lightness ∈ [0.10, 0.22] — because an unclamped wash would put a
// focus ring back inside the hue range projection-booth amber was chosen to sit
// outside of (ARCHITECTURE.md §4). Nothing here brightens, saturates or
// re-mixes what it is handed, and the opacity ceiling is Theme.washOpacity,
// which is the tested constant re-exported. If any of that ever wants tuning it
// is tuned in src/app/CoverTint.h, where a test is watching.
//
// A cover with no colour that can meet the clamp comes back transparent, and
// the wash falls back to Theme.surfaceColor: over the ground that is barely a
// lift, which is the point. No wash is always better than an illegible one.
Item {
    id: wash

    // The provider URL of the cover to take the colour from — `posterUrl`
    // straight off the model or the queue entry, never a hand-built image URL,
    // so the bar, the hero and the page header all agree on one sleeve.
    property string source: ""

    // Transparent means "no sampled tint", covering both "the cover has not
    // decoded yet" and "it has no colour that can meet the clamp". They render
    // the same, deliberately: neither one draws a sampled wash.
    readonly property color sampled: {
        // Touching `revision` is what makes this a live binding rather than a
        // snapshot. The tint lands when the cover finishes decoding, which is
        // after this first runs.
        const generation = CoverTint.revision;
        return generation < 0 ? Qt.rgba(0, 0, 0, 0) : CoverTint.tintFor(wash.source);
    }

    readonly property bool tinted: wash.sampled.a > 0.5

    property color tint: wash.tinted ? wash.sampled : Theme.surfaceColor

    // A track change should carry the room with it rather than cutting to a new
    // colour — the same 420 ms the sleeve itself travels in.
    Behavior on tint {
        ColorAnimation {
            duration: Theme.animSlow
            easing.type: Theme.easeStandard
        }
    }

    // A wash is decorative background colour, and the app has exactly one
    // switch for decorative backgrounds. A user who turned backdrops off does
    // not get one here either.
    //
    // Its *opacity* preference is deliberately not applied: backdropOpacity
    // scales photographic art, and at its default of 18 % it would take this to
    // 4 %, which is not a quieter wash but no wash. The ceiling here is a
    // legibility bound, not a taste setting.
    visible: Prefs.backdropEnabled && wash.source.length > 0

    Rectangle {
        anchors.fill: parent
        opacity: Theme.washOpacity

        gradient: Gradient {
            GradientStop { position: 0.0; color: wash.tint }
            GradientStop { position: 1.0; color: Theme.ground }
        }
    }
}
