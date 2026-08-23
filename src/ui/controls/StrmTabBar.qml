pragma ComponentBehavior: Bound
import QtQuick
import StrmQt

// StrmTabBar — section switcher for details pages and settings (ARCHITECTURE.md).
//
// `tabs` is a list of { text, badge } objects (badge optional, 0 hides it).
// Left/Right move the selection; hover PREVIEWS a tab — the label brightens and
// the underline does not move — because a tab bar that switched content on
// mouse-over would make the page unusable while reaching for anything past the
// first tab. Committing is a click or a Return, never a hover.
FocusScope {
    id: bar

    property var tabs: []
    property int currentIndex: 0

    signal tabSelected(int index)

    property int hoveredIndex: -1

    // Published by the current tab's Bindings below; the indicator and the
    // focus ring both track these.
    property real indicatorX: 0
    property real indicatorWidth: 0

    implicitHeight: Theme.controlHeightLarge
    implicitWidth: row.implicitWidth
    height: implicitHeight

    activeFocusOnTab: true

    function select(index) {
        if (index < 0 || index >= bar.tabs.length || index === bar.currentIndex)
            return
        bar.currentIndex = index
        bar.tabSelected(index)
    }

    // Arrows may auto-repeat — holding Right should walk the bar. Only
    // activation is auto-repeat guarded, and there is no separate activation
    // key here because moving the selection *is* the activation.
    Keys.onLeftPressed: bar.select(Math.max(0, bar.currentIndex - 1))
    Keys.onRightPressed: bar.select(Math.min(bar.tabs.length - 1, bar.currentIndex + 1))

    Row {
        id: row
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        spacing: Theme.spacingLoose

        Repeater {
            model: bar.tabs

            delegate: Item {
                id: tab

                required property var modelData
                required property int index

                readonly property bool current: bar.currentIndex === tab.index
                readonly property bool hovered: bar.hoveredIndex === tab.index
                readonly property int badgeValue: modelData.badge !== undefined
                                                  ? Number(modelData.badge) : 0

                width: tabLabel.implicitWidth + (badge.visible ? badge.width + Theme.spacingTight : 0)
                height: bar.height

                Text {
                    id: tabLabel
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: tab.modelData.text !== undefined ? tab.modelData.text : String(tab.modelData)
                    color: tab.current ? Theme.textPrimaryColor
                         : tab.hovered ? Theme.textPrimaryColor
                         : Theme.textSecondaryColor
                    font.family: Theme.fontDisplay
                    font.pixelSize: Theme.fontBodyLarge
                    font.weight: tab.current ? Font.DemiBold : Font.Normal

                    Behavior on color {
                        ColorAnimation { duration: Theme.animInstant; easing.type: Theme.easeInstant }
                    }
                }

                Rectangle {
                    id: badge
                    anchors.left: tabLabel.right
                    anchors.leftMargin: Theme.spacingTight
                    anchors.verticalCenter: parent.verticalCenter
                    visible: tab.badgeValue > 0
                    width: Math.max(Theme.scale(20), badgeText.implicitWidth + Theme.spacingTight)
                    height: Theme.scale(20)
                    radius: height / 2
                    color: tab.current ? Theme.accentColor : Theme.surfaceRaisedColor

                    Text {
                        id: badgeText
                        anchors.centerIn: parent
                        text: tab.badgeValue
                        color: tab.current ? Theme.accentText : Theme.textSecondaryColor
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontCaption
                    }
                }

                HoverHandler {
                    cursorShape: Qt.PointingHandCursor
                    // Preview: never forceActiveFocus(), never select().
                    onHoveredChanged: bar.hoveredIndex = hovered ? tab.index : -1
                }

                // The indicator's geometry is published by the current tab
                // itself rather than read out of `row.children`, whose ordering
                // relative to the Repeater is an implementation detail.
                Binding {
                    target: bar
                    property: "indicatorX"
                    value: tab.x
                    when: tab.current
                    restoreMode: Binding.RestoreNone
                }
                Binding {
                    target: bar
                    property: "indicatorWidth"
                    value: tab.width
                    when: tab.current
                    restoreMode: Binding.RestoreNone
                }

                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: {
                        bar.forceActiveFocus(Qt.MouseFocusReason)
                        bar.select(tab.index)
                    }
                }
            }
        }
    }

    // Underline indicator. Bound to the CURRENT tab only, so it slides on a
    // commit and stays put under a hover.
    Rectangle {
        id: indicator
        anchors.bottom: parent.bottom
        height: Theme.scale(3)
        radius: height / 2
        color: Theme.accentColor
        visible: bar.tabs.length > 0

        x: row.x + bar.indicatorX
        width: bar.indicatorWidth

        Behavior on x {
            NumberAnimation { duration: Theme.animFastMs; easing.type: Theme.easeStandard }
        }
        Behavior on width {
            NumberAnimation { duration: Theme.animFastMs; easing.type: Theme.easeStandard }
        }
    }

    // The bar as a whole owns the focus ring, drawn around the current tab:
    // individual tabs are not separate tab stops, which is what keeps Tab
    // traversal across a page sane. FocusRing anchors itself to its parent, so
    // it gets a parent whose geometry tracks the indicator.
    Item {
        x: indicator.x - Theme.spacingTight / 2
        y: 0
        width: indicator.width + Theme.spacingTight
        height: bar.height

        FocusRing {
            active: bar.activeFocus
            radius: Theme.radiusChip
            inset: 0
        }
    }
}
