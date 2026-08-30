import QtQuick
import StrmQt

// The one asynchronous artwork Image (C7): every card, frame, backdrop and
// wash used to grow its own copy of this scaffold — async decode, a crossfade
// in once the pixels are Ready, and a device-pixel request so a scaled display
// is not handed a logical-pixel decode to upscale (the soft-card bug every one
// of those copies eventually relearned).
//
// Site-specific extras stay outside: the frame (radius/clip), placeholders and
// overlays are the caller's to draw under or over this. A fixed decode size
// (backdrops, washes) is assigned as sourceSize.width by the caller, which
// replaces the device-pixel default binding.
Image {
    id: img

    // What the fade arrives at; backdrops and washes pass less than 1.
    property real readyOpacity: 1.0
    property int fadeDuration: Theme.animNormalMs
    property int fadeEasing: Theme.easeStandard

    fillMode: Image.PreserveAspectCrop
    asynchronous: true
    cache: true
    // The pixels this actually draws, in device pixels. Math.max(1, …) because
    // a collapsed frame (width 0) is not a request the provider can honour.
    sourceSize.width: Math.round(Math.max(1, img.width) * Screen.devicePixelRatio)
    opacity: img.status === Image.Ready ? img.readyOpacity : 0

    Behavior on opacity {
        NumberAnimation {
            duration: img.fadeDuration
            easing.type: img.fadeEasing
        }
    }

    onStatusChanged: {
        // A dead URL must be loud, the way StrmIcon makes a typo'd name loud:
        // silently blank artwork is how a broken provider path ships.
        if (img.status === Image.Error)
            console.warn("StrmImage: failed to load " + img.source);
    }
}
