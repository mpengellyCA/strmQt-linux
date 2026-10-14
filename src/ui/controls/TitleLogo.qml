import QtQuick
import StrmQt

// TitleLogo — an item's name, set as its ClearLogo when the server has one.
//
// One implementation for every place a title can become a logo: the Home
// stage, the details and series headers and the player's top bar. Each of
// them gives it the title it already showed, the item's `logoUrl` role, a box
// to fit the logo into, and the type its text used to be set in.
//
// ── Fallback is the default case ───────────────────────────────────────────
// Most items have no logo, and every logo arrives asynchronously. So the
// title is drawn as text until the logo has actually decoded, and again if it
// fails. The two share one box and crossfade inside it: the box's height is
// decided by whether there is a logo URL at all — known with the item, never
// later — so nothing under it moves when the pixels land, or when they never
// do. The text is the accessible name either way.
//
// ── Sizing ─────────────────────────────────────────────────────────────────
// Logos come in every proportion, from a 10:1 wordmark to a square badge.
// Fitting each to the same box makes the badge enormous and the wordmark a
// thread, so the drawn size aims at a constant AREA (`logoArea` of the box)
// and is then clamped to the box: a square stops at the box's height, a
// wordmark at its width, and the shapes in between read at a similar weight.
// It sits bottom-left, where the text it replaces sits.
//
// ── Ink ────────────────────────────────────────────────────────────────────
// A logo is drawn as the artist made it, never tinted. Most are light and are
// lost over a bright patch of fanart, so a soft Theme.elevation1 shadow sits
// under them (StrmShadow, so it builds on both QML tiers). A dark logo on the
// dark ground is the case it does not solve, and nothing short of recolouring
// someone else's artwork would.
Item {
    id: root

    property string title: ""
    property string logoUrl: ""

    // The box the logo is fitted into. The decode is asked for at this size
    // (times the device pixel ratio), so a logo is never fetched larger than it
    // can be drawn, and not re-fetched when the layout around it settles.
    property int logoMaxWidth: Theme.scale(420)
    property int logoMaxHeight: Theme.scale(120)
    // The decode's height bound. A caller whose box changes size (a header
    // that folds, a stage that follows the window) sets this to the largest
    // box it will draw, so a change of box is a re-layout of pixels already
    // decoded rather than a reload that drops back to the text meanwhile.
    property int decodeMaxHeight: root.logoMaxHeight
    // Fraction of the box's area a logo aims to cover before the clamp.
    property real logoArea: 0.5
    // Whether the box keeps its full height whatever is drawn in it. On by
    // default, so nothing under the title moves when the logo lands. A caller
    // that lays out upward from a bottom edge (the Home stage) turns it off:
    // nothing under the title can move there anyway, and a held box would
    // leave a band of nothing between a short wordmark and what sits above.
    property bool reserveBox: true
    property bool shadow: true

    // The text the title is set in while there is no logo to show.
    property string fontFamily: Theme.fontDisplay
    property int pixelSize: Theme.fontDisplaySize
    property int fontWeight: Font.DemiBold
    property color color: Theme.textPrimaryColor
    property int maximumLineCount: 2

    readonly property bool hasLogo: root.logoUrl.length > 0
    readonly property bool logoShown: root.hasLogo && logo.status === Image.Ready
                                      && logo.implicitWidth > 0 && logo.implicitHeight > 0

    // Drawn size: constant area, then clamped to the box (see above).
    readonly property real _boxWidth: Math.max(1, Math.min(root.logoMaxWidth,
                                                           root.width > 0 ? root.width
                                                                          : root.logoMaxWidth))
    readonly property real _aspect: root.logoShown ? logo.implicitWidth / logo.implicitHeight : 1
    readonly property size _drawn: {
        let h = Math.min(root.logoMaxHeight,
                         Math.sqrt(root._boxWidth * root.logoMaxHeight * root.logoArea
                                   / root._aspect))
        let w = h * root._aspect
        if (w > root._boxWidth) {
            w = root._boxWidth
            h = w / root._aspect
        }
        return Qt.size(Math.round(w), Math.round(h))
    }

    implicitWidth: root.hasLogo ? Math.max(root.logoMaxWidth, label.implicitWidth)
                                : label.implicitWidth
    implicitHeight: !root.hasLogo ? label.implicitHeight
                    : root.reserveBox ? Math.max(root.logoMaxHeight, label.implicitHeight)
                    : root.logoShown ? root._drawn.height
                    : label.implicitHeight

    Accessible.role: Accessible.Heading
    Accessible.name: root.title

    Text {
        id: label

        objectName: "titleLogoText"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        text: root.title
        color: root.color
        font.family: root.fontFamily
        font.pixelSize: root.pixelSize
        font.weight: root.fontWeight
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        maximumLineCount: root.maximumLineCount
        elide: Text.ElideRight
        opacity: root.logoShown ? 0 : 1
        Accessible.ignored: true

        Behavior on opacity {
            NumberAnimation { duration: Theme.animNormalMs; easing.type: Theme.easeStandard }
        }
    }

    Image {
        id: logo

        objectName: "titleLogoImage"
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        width: root._drawn.width
        height: root._drawn.height
        visible: root.hasLogo
        source: root.logoUrl
        asynchronous: true
        cache: true
        // A logo is transparent PNG; fit it, never crop it.
        fillMode: Image.PreserveAspectFit
        horizontalAlignment: Image.AlignLeft
        verticalAlignment: Image.AlignBottom
        sourceSize.width: Math.round(root.logoMaxWidth * Screen.devicePixelRatio)
        sourceSize.height: Math.round(root.decodeMaxHeight * Screen.devicePixelRatio)
        opacity: root.logoShown ? 1 : 0
        Accessible.ignored: true

        Behavior on opacity {
            NumberAnimation { duration: Theme.animNormalMs; easing.type: Theme.easeStandard }
        }

        // Only while there is something to cast it: a layer on an empty image
        // is an offscreen pass for nothing.
        layer.enabled: root.shadow && root.logoShown
        layer.effect: StrmShadow {
            elevation: Theme.elevation1
            shadowColor: Theme.shadowColor
        }
    }
}
