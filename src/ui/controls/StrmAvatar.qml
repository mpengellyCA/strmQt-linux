import QtQuick
import StrmQt

// A person's or artist's picture with the missing-image case treated as the
// normal case it is (C7): on a typical server most of the billing order and
// most self-hosted artists have no Primary image at all. Initials on a tinted
// ground keep the geometry identical either way — a row of these never
// develops holes — and the photo, when there is one, crossfades in over them
// rather than over a hole. A nameless record still gets a glyph.
//
// The defaults are the hero shape (radius/border frame, type sized off the
// height); PersonCard's smaller, focus-reactive variant passes its own sizes
// and colours and turns the frame's chrome off, because its card frame already
// draws that.
Rectangle {
    id: avatar

    property string imageUrl: ""
    property string name: ""
    // The glyph a nameless record gets.
    property string iconName: "user"
    property int initialsPixelSize: Math.round(avatar.height * 0.34)
    property int iconSize: Math.round(avatar.height * 0.28)
    property color initialsColor: Theme.textTertiary
    property color iconColor: Theme.textTertiary

    readonly property string initials: {
        const parts = avatar.name.trim().split(/\s+/).filter(p => p.length > 0);
        if (parts.length === 0)
            return "";
        if (parts.length === 1)
            return parts[0].charAt(0).toUpperCase();
        // String() rather than a bare concatenation: charAt() types as a
        // QJSPrimitiveValue, which has no toUpperCase() as far as qmllint is
        // concerned, and a lint warning here is a real ambiguity.
        return String(parts[0].charAt(0)
                      + parts[parts.length - 1].charAt(0)).toUpperCase();
    }

    radius: Theme.radiusPanel
    color: Theme.surfaceColor
    border.width: 1
    border.color: Theme.hairline
    clip: true

    Behavior on initialsColor {
        ColorAnimation { duration: Theme.animInstant; easing.type: Theme.easeInstant }
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.surfaceRaisedColor }
            GradientStop { position: 1.0; color: Theme.surfaceColor }
        }

        Text {
            anchors.centerIn: parent
            visible: avatar.initials.length > 0
            text: avatar.initials
            color: avatar.initialsColor
            font.family: Theme.fontDisplay
            font.pixelSize: avatar.initialsPixelSize
            font.weight: Font.DemiBold
        }

        StrmIcon {
            anchors.centerIn: parent
            visible: avatar.initials.length === 0
            name: avatar.iconName
            size: avatar.iconSize
            color: avatar.iconColor
        }
    }

    StrmImage {
        anchors.fill: parent
        source: avatar.imageUrl
    }
}
