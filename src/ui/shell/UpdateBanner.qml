pragma ComponentBehavior: Bound
import QtQuick
import StrmQt

// "N new items" (ARCHITECTURE.md).
//
// Live updates deliberately do NOT reload a grid the user has paged into —
// content must never move under the cursor or the scroll position. The cost of
// that decision is that new items are withheld, so there has to be a way to see
// them. Without this the withholding is silent: LibraryController and
// HomeController have carried updatesPending/pendingNewCount/applyPending since
// M10 and nothing in the UI referenced any of them.
//
// It is an offer, not a notification: it appears in the corner, never steals
// focus, and applying is an explicit act.
Item {
    id: banner

    // The controller to read and act on (LibraryCtl or HomeCtl). Anything with
    // updatesPending / pendingNewCount / applyPending() / discardPending().
    property var controller: null

    readonly property bool pending: banner.controller !== null
                                    && banner.controller.updatesPending === true
    readonly property int count: banner.controller !== null
                                 && banner.controller.pendingNewCount !== undefined
                                 ? banner.controller.pendingNewCount : 0

    implicitWidth: pill.implicitWidth
    implicitHeight: pill.implicitHeight
    visible: opacity > 0.01
    opacity: banner.pending ? 1.0 : 0.0
    // Not `enabled: pending` alone: an invisible pill must not stay in the tab
    // chain or a keyboard user tabs into nothing.
    enabled: banner.pending

    Behavior on opacity {
        NumberAnimation { duration: Theme.animNormalMs; easing.type: Theme.easeStandard }
    }

    Rectangle {
        id: pill

        implicitWidth: row.implicitWidth + Theme.spacingValue * 2
        implicitHeight: Math.max(Theme.touchTarget, row.implicitHeight + Theme.spacingTight)
        radius: height / 2
        color: Theme.surfaceRaisedColor
        border.width: 1
        border.color: Theme.accentColor

        // A shift upward on arrival reads as "something came in" without motion
        // loud enough to pull the eye off the grid.
        y: banner.pending ? 0 : Theme.spacingValue
        Behavior on y {
            NumberAnimation { duration: Theme.animNormalMs; easing.type: Theme.easeStandard }
        }

        Row {
            id: row

            anchors.centerIn: parent
            spacing: Theme.spacingTight

            StrmIcon {
                anchors.verticalCenter: parent.verticalCenter
                name: "refresh"
                color: Theme.accentColor
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: banner.count > 0 ? qsTr("%n new item(s)", "", banner.count)
                                       : qsTr("Updated")
                color: Theme.textPrimaryColor
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontBodySize
            }

            StrmButton {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Show")
                variant: "ghost"
                onClicked: if (banner.controller !== null) banner.controller.applyPending()
            }

            StrmIconButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: "close"
                tooltip: qsTr("Dismiss")
                onClicked: if (banner.controller !== null) banner.controller.discardPending()
            }
        }
    }
}
