import QtQuick
import StrmQt

// A format or release badge (spec §2): mono type in a hairline box. The hi-res
// variant is the accent, which is the only colour a badge may change to.
Rectangle {
    id: badge

    property string text: ""
    property bool hiRes: false

    readonly property color tone: badge.hiRes ? Theme.crateBadgeHiRes : Theme.crateBadgeBorder

    visible: badge.text.length > 0
    implicitWidth: label.implicitWidth + Theme.scale(10)
    implicitHeight: label.implicitHeight + Theme.scale(4)
    width: badge.implicitWidth
    height: badge.implicitHeight
    radius: Theme.crateBadgeRadius
    color: "transparent"
    border.width: Theme.crateBadgeBorderWidth
    border.color: badge.tone

    Accessible.role: Accessible.StaticText
    Accessible.name: badge.text

    TabularText {
        id: label

        anchors.centerIn: parent
        text: badge.text
        color: badge.hiRes ? Theme.crateBadgeHiRes : Theme.textSecondaryColor
        font.family: Theme.fontMono
        font.pixelSize: Theme.crateBadgeSize
        font.capitalization: Font.AllUppercase
        textFormat: Text.PlainText
    }
}
