import QtQuick
import StrmQt

// One cast or crew member: headshot, name, character (ARCHITECTURE.md).
//
// Not a StrmCard variant: a person has no played state, no progress, no
// favourite, and no overlay verbs — the whole card is one target that opens
// that person's filmography. Bending StrmCard's media contract around a
// {id, name, role, type, primaryImageTag} record would have meant six dead
// properties and a fifth `variant` branch.
//
// The missing-headshot case is the common case, not the edge case: on a
// typical film the server has a Primary image for most of the billing order
// and nothing at all for the rest. So the fallback is designed rather than
// defaulted — initials on a tinted ground, the same footprint as a photo, so
// a row of cards never develops holes.
//
// Focus model, as everywhere else: `highlighted` is driven by the containing
// view from `ListView.isCurrentItem && view.activeFocus`, hover is this card's
// own HoverHandler, both may be true at once, and hovering never takes the
// keyboard's place.
Item {
    id: card

    // {id, name, role, type, primaryImageTag}
    property var person: null
    property bool highlighted: false

    signal activated()

    readonly property string personId: (card.person && card.person.id) ? card.person.id : ""
    readonly property string personName: (card.person && card.person.name) ? card.person.name : ""
    readonly property string personType: (card.person && card.person.type) ? card.person.type : ""
    readonly property string imageTag: (card.person && card.person.primaryImageTag)
                                       ? card.person.primaryImageTag : ""

    // The character for an actor; the job for everyone else. An actor with no
    // character recorded gets nothing rather than the word "Actor", which the
    // photograph already says.
    readonly property string roleText: {
        const role = (card.person && card.person.role) ? card.person.role : "";
        if (role.length > 0)
            return role;
        return card.personType === "Actor" ? "" : card.personType;
    }

    // Images.sourceNamespace is read explicitly so an identity reset
    // re-evaluates this binding even when id and tag happen to be unchanged.
    readonly property string imageUrl: (card.personId.length > 0 && card.imageTag.length > 0)
                                       && Images.sourceNamespace.length > 0
                                       ? Images.sourceFor(card.personId, "Primary", card.imageTag)
                                       : ""

    readonly property bool hovered: hover.hovered
    readonly property bool raised: card.hovered || card.highlighted

    readonly property int portraitWidth: Theme.scale(132)
    readonly property int portraitHeight: Math.round(card.portraitWidth * 4 / 3)

    implicitWidth: card.portraitWidth
    implicitHeight: card.portraitHeight + Theme.spacingTight
                    + nameLabel.implicitHeight + roleLabel.height

    function activate(): void {
        card.activated();
    }

    Item {
        id: visual

        anchors.fill: parent
        transformOrigin: Item.Center
        // Focus wins when both are true: the larger raise, the slower curve.
        scale: card.highlighted ? Theme.focusScale
             : card.hovered ? Theme.hoverScale : 1.0

        Behavior on scale {
            NumberAnimation {
                duration: card.highlighted ? Theme.animFastMs : Theme.animInstant
                easing.type: card.highlighted ? Theme.easeStandard : Theme.easeInstant
            }
        }

        Rectangle {
            id: frame

            width: card.portraitWidth
            height: card.portraitHeight
            radius: Theme.radiusCardValue
            clip: true
            color: card.raised ? Theme.surfaceRaisedColor : Theme.surfaceColor
            border.width: 1
            border.color: card.raised ? Theme.hairline : "transparent"

            Behavior on color {
                ColorAnimation { duration: Theme.animInstant; easing.type: Theme.easeInstant }
            }

            // ── Fallback: initials on a tinted ground ──────────────────────
            // Drawn underneath the photo rather than instead of it, so the
            // crossfade has something to arrive over instead of a black hole.
            // The frame above owns the card's chrome, so the avatar's own
            // frame colour and border are off.
            StrmAvatar {
                anchors.fill: parent
                imageUrl: card.imageUrl
                name: card.personName
                radius: Theme.radiusCardValue
                color: "transparent"
                border.width: 0
                initialsPixelSize: Theme.fontHeading
                iconSize: Theme.scale(32)
                initialsColor: card.raised ? Theme.textSecondaryColor : Theme.textTertiary
            }
        }

        // Amber ring, drawn outside the frame so it never covers the headshot.
        FocusRing {
            active: card.highlighted
            anchors.fill: frame
            radius: Theme.radiusCardValue
            inset: -Theme.focusRingWidth
        }

        Text {
            id: nameLabel

            anchors.top: frame.bottom
            anchors.topMargin: Theme.spacingTight
            width: card.portraitWidth
            text: card.personName
            color: card.raised ? Theme.textPrimaryColor : Theme.textSecondaryColor
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontSmall
            font.weight: Font.Medium
            elide: Text.ElideRight
            maximumLineCount: 1

            Behavior on color {
                ColorAnimation { duration: Theme.animInstant; easing.type: Theme.easeInstant }
            }
        }

        Text {
            id: roleLabel

            anchors.top: nameLabel.bottom
            width: card.portraitWidth
            visible: card.roleText.length > 0
            height: visible ? implicitHeight : 0
            text: card.roleText
            color: Theme.textTertiary
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontCaption
            elide: Text.ElideRight
            maximumLineCount: 1
        }
    }

    // Both labels elide, so the tooltip is where the full credit lives.
    StrmTooltip {
        id: tip
        target: card
        text: card.roleText.length > 0 ? card.personName + " — " + card.roleText
                                       : card.personName
    }

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
        onHoveredChanged: {
            if (hover.hovered)
                tip.requestShow();
            else
                tip.requestHide();
        }
    }

    TapHandler {
        acceptedButtons: Qt.LeftButton
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: {
            tip.requestHide();
            card.activated();
        }
    }

    // Only reached when a card is used standalone; inside a view the view owns
    // the keys and drives `highlighted`.
    Keys.onReturnPressed: event => { if (!event.isAutoRepeat) card.activate() }
    Keys.onEnterPressed: event => { if (!event.isAutoRepeat) card.activate() }
}
