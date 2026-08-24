import QtQuick
import StrmQt

// A chip that *goes somewhere*: a genre, a studio, a crew credit, or an
// off-site provider link (ARCHITECTURE.md).
//
// Why this is not StrmChip: StrmChip is a *toggle* (`checked` + `toggled()`),
// which is the right model for a filter pill and the wrong one for a link.
// A link chip has no on/off state, it commits, and — the part that matters —
// it does not take keyboard focus itself. Like StrmCard, it is designed to sit
// inside a view that owns the arrow keys and drives `highlighted`; giving each
// chip its own tab stop is exactly what SeriesPage.qml documents as the way to
// break a row's own Left/Right navigation.
//
// `linked: false` is a first-class state, not an error state. The server can
// hand back a genre or studio with no id (an older payload), and a chip that
// looks clickable and does nothing is worse than plain text — so it renders as
// plain text: no pill, no hover, no pointer cursor, no ring.
Item {
    id: chip

    property string label: ""
    // Secondary text inside the same pill: a crew member's role, mostly.
    property string sublabel: ""
    property string iconName: ""
    // False → this is not a link. Render the label, drop every affordance.
    property bool linked: true
    // Keyboard/gamepad focus, driven by the containing view. Never by hover.
    property bool highlighted: false

    signal activated()

    readonly property bool interactive: chip.linked && chip.enabled
    readonly property bool hovered: hover.hovered
    readonly property bool pressed: tap.pressed

    readonly property color labelColor: {
        if (!chip.interactive)
            return Theme.textTertiary;
        if (chip.hovered || chip.highlighted)
            return Theme.textPrimaryColor;
        return Theme.textSecondaryColor;
    }

    implicitHeight: Theme.scale(32)
    // Unlinked chips keep the linked padding so a mixed row still lines up.
    implicitWidth: row.implicitWidth + 2 * Theme.spacingValue

    Accessible.role: chip.interactive ? Accessible.Link : Accessible.StaticText
    Accessible.name: chip.sublabel.length > 0
                     ? chip.label + ", " + chip.sublabel : chip.label
    Accessible.focusable: chip.interactive && (chip.activeFocusOnTab || chip.highlighted)
    Accessible.focused: chip.highlighted || chip.activeFocus
    Accessible.onPressAction: chip.activate()

    // Press < hover < focus, exactly as StrmButton and StrmCard order them.
    scale: !chip.interactive ? 1.0
         : chip.pressed ? Theme.pressScale
         : chip.highlighted ? Theme.focusScale
         : chip.hovered ? Theme.hoverScale
         : 1.0

    Behavior on scale {
        NumberAnimation {
            duration: chip.highlighted ? Theme.animFastMs : Theme.animInstant
            easing.type: chip.highlighted ? Theme.easeStandard : Theme.easeInstant
        }
    }

    function activate(): void {
        if (chip.interactive)
            chip.activated();
    }

    Rectangle {
        id: bg

        anchors.fill: parent
        radius: Theme.radiusPill
        color: chip.interactive ? Theme.surfaceColor : "transparent"
        border.width: chip.interactive ? 1 : 0
        border.color: chip.highlighted ? Theme.accentMuted : Theme.hairline

        Behavior on border.color {
            ColorAnimation {
                duration: Theme.animFastMs
                easing.type: Theme.easeStandard
            }
        }
    }

    Rectangle {
        anchors.fill: bg
        radius: bg.radius
        color: !chip.interactive ? "transparent"
             : chip.pressed ? Theme.pressTint
             : chip.hovered ? Theme.hoverTint
             : "transparent"

        Behavior on color {
            ColorAnimation {
                duration: Theme.animInstant
                easing.type: Theme.easeInstant
            }
        }
    }

    Row {
        id: row

        anchors.centerIn: parent
        spacing: Theme.scale(6)

        StrmIcon {
            anchors.verticalCenter: parent.verticalCenter
            visible: chip.iconName.length > 0 && chip.interactive
            name: chip.iconName
            color: chip.labelColor
            size: Theme.scale(14)
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: chip.label
            color: chip.labelColor
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontSmall

            Behavior on color {
                ColorAnimation {
                    duration: Theme.animInstant
                    easing.type: Theme.easeInstant
                }
            }
        }

        // The role on a crew credit. Quieter than the name it qualifies, so a
        // row of credits reads as names first.
        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: chip.sublabel.length > 0
            text: chip.sublabel
            color: Theme.textTertiary
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontCaption
        }
    }

    FocusRing {
        active: chip.highlighted
        radius: Theme.radiusPill
    }

    HoverHandler {
        id: hover
        enabled: chip.interactive
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        id: tap
        enabled: chip.interactive
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: chip.activated()
    }

    // Only reached when a chip is used standalone; inside a view the view owns
    // the keys and drives `highlighted`. Guarded against auto-repeat like every
    // other activation path in the app.
    Keys.onReturnPressed: event => {
        if (!event.isAutoRepeat)
            chip.activate();
    }
    Keys.onEnterPressed: event => {
        if (!event.isAutoRepeat)
            chip.activate();
    }
}
