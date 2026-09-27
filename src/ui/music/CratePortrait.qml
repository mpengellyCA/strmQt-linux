import QtQuick
import StrmQt

// An artist as a round portrait with the name under it. StrmAvatar supplies the
// initials fallback; the circle is a mask because a clip cannot round an image.
Item {
    id: portrait

    property string imageUrl: ""
    property string name: ""
    property string subtitle: ""
    property int size: Theme.cratePortraitSize
    property bool current: false
    property bool hovered: hover.hovered

    signal activated()
    signal menuRequested(real x, real y)

    implicitWidth: portrait.size
    implicitHeight: portrait.size + Theme.spacingTight + caption.implicitHeight
    width: portrait.implicitWidth
    height: portrait.implicitHeight

    Accessible.role: Accessible.Button
    Accessible.name: portrait.subtitle.length > 0 ? portrait.name + ", " + portrait.subtitle : portrait.name
    Accessible.onPressAction: portrait.activated()

    function requestMenuAt(px: real, py: real): void {
        const p = portrait.mapToItem(null, px, py)
        portrait.menuRequested(p.x, p.y)
    }

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        acceptedButtons: Qt.LeftButton
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: portrait.activated()
        onLongPressed: portrait.requestMenuAt(frame.width / 2, frame.height * 0.75)
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: eventPoint => portrait.requestMenuAt(eventPoint.position.x, eventPoint.position.y)
    }

    Item {
        id: frame

        width: portrait.size
        height: portrait.size
        scale: portrait.current ? Theme.focusScale : (portrait.hovered ? Theme.hoverScale : 1.0)

        Behavior on scale {
            NumberAnimation {
                duration: portrait.current ? Theme.animFastMs : Theme.animInstant
                easing.type: portrait.current ? Theme.easeStandard : Theme.easeInstant
            }
        }

        StrmAvatar {
            id: avatar
            anchors.fill: parent
            imageUrl: portrait.imageUrl
            name: portrait.name
            iconName: "user"
            border.width: 0
            radius: 0
            visible: false
            layer.enabled: true
        }

        Rectangle {
            id: circle
            anchors.fill: parent
            radius: circle.width / 2
            visible: false
            layer.enabled: true
        }

        StrmMask {
            anchors.fill: parent
            source: avatar
            maskSource: circle
        }

        FocusRing {
            active: portrait.current
            radius: frame.width / 2
            inset: -Theme.scale(3)
        }
    }

    Column {
        id: caption

        anchors.top: frame.bottom
        anchors.topMargin: Theme.spacingTight
        width: portrait.size
        spacing: Theme.scale(2)

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: portrait.name
            color: Theme.textPrimaryColor
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontSmall
            font.weight: Font.DemiBold
            textFormat: Text.PlainText
            elide: Text.ElideRight
            maximumLineCount: 1
        }

        CrateKicker {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            visible: portrait.subtitle.length > 0
            text: portrait.subtitle
        }
    }
}
