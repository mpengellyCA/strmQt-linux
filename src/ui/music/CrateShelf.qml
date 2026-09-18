pragma ComponentBehavior: Bound
import QtQuick
import StrmQt

// One Home shelf (spec §4): a Crate heading over a rail of Crate controls.
//
// Each shelf is an independent lane. It hides while its lane is empty, heading
// included; it shows a skeleton in its real shape while the first load runs;
// a failure collapses it to one line with Retry. The other shelves never notice.
// A lane that goes straight from error to empty (a retry that comes back with
// nothing) hides the shelf entirely with nothing left inside it to hold focus,
// so the page must move focus on itself.
//
// Focus: one tab stop. Up from the rail reaches the heading's action (↻) when
// there is one; everything else vertical is declined, so the page moves on.
FocusScope {
    id: shelf

    property string title: ""
    property string kicker: ""
    // A MusicLane. `var`, because MusicLane is a context-property object, not a
    // registered type, and a QtObject type would make every read a lint warning.
    property var lane: null
    property Component delegate: null
    property string actionText: ""
    property string actionIcon: ""
    property string navigationFocusKey: ""
    property int cardWidth: Theme.crateSleeveSize
    property int cardHeight: Theme.crateSleeveSize
    property string skeletonShape: "square"
    property int skeletonCount: 6

    signal actionTriggered()
    signal itemActivated(int index)
    signal itemPlayRequested(int index)
    signal menuRequested(int index, real x, real y)

    readonly property Item rail: railView
    readonly property bool loading: shelf.lane !== null && shelf.lane.loading === true
    readonly property string errorText: shelf.lane !== null && shelf.lane.error ? String(shelf.lane.error) : ""
    readonly property bool showError: shelf.errorText.length > 0
    readonly property bool showSkeleton: !shelf.showError && shelf.loading && railView.count === 0
    readonly property bool focusable: shelf.visible && !shelf.showSkeleton

    visible: shelf.lane !== null && shelf.lane.empty !== true
    activeFocusOnTab: shelf.focusable
    width: parent ? parent.width : implicitWidth
    implicitWidth: Theme.scale(800)
    height: header.height + Theme.spacingTight + body.height

    Accessible.role: Accessible.Grouping
    Accessible.name: shelf.title

    function focusRail(): void {
        if (shelf.showError)
            errorLine.forceActiveFocus(Qt.OtherFocusReason)
        else
            railView.forceActiveFocus(Qt.OtherFocusReason)
    }

    Keys.onUpPressed: event => {
        if (actionButton.visible && !actionButton.activeFocus && !shelf.showError) {
            actionButton.forceActiveFocus(Qt.TabFocusReason)
            event.accepted = true
            return
        }
        event.accepted = false
    }
    Keys.onDownPressed: event => {
        if (actionButton.activeFocus) {
            shelf.focusRail()
            event.accepted = true
            return
        }
        event.accepted = false
    }

    // ── Heading ────────────────────────────────────────────────────────────
    Item {
        id: header

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.pageMarginValue
        anchors.rightMargin: Theme.pageMarginValue
        height: Math.max(heading.implicitHeight, actionButton.visible ? actionButton.implicitHeight : 0)

        CrateHeading {
            id: heading
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(heading.implicitWidth, header.width * 0.6)
            text: shelf.title
        }

        CrateKicker {
            anchors.left: heading.right
            anchors.leftMargin: Theme.spacingValue
            anchors.baseline: heading.baseline
            anchors.right: actionButton.visible ? actionButton.left : parent.right
            anchors.rightMargin: Theme.spacingValue
            visible: shelf.kicker.length > 0
            text: shelf.kicker
        }

        StrmButton {
            id: actionButton
            objectName: "crateShelfAction"
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: shelf.actionText.length > 0 && !shelf.showError && !shelf.showSkeleton
            // Reached with Up from the rail, never with Tab: the shelf is one stop.
            activeFocusOnTab: false
            text: shelf.actionText
            iconName: shelf.actionIcon
            variant: "ghost"
            onClicked: shelf.actionTriggered()

            // `forceActiveFocus()` (Up, above) sets this button's `focus` as a
            // hard value, so it stays the scope's remembered focus child even
            // after the page moves focus elsewhere entirely. Once that happens
            // — the button and the whole shelf both lose active focus — hand
            // the memory back to the rail and rebind it, so the shelf's next
            // forceActiveFocus() (Tab, or the page restoring a place) lands on
            // the rail again instead of reopening on ↻.
            onActiveFocusChanged: {
                if (!actionButton.activeFocus && !shelf.activeFocus && !shelf.showError) {
                    railView.focus = true
                    railView.focus = Qt.binding(() => !shelf.showError)
                }
            }
        }
    }

    // ── Body: exactly one of skeleton, error line, rail ────────────────────
    Item {
        id: body

        anchors.top: header.bottom
        anchors.topMargin: Theme.spacingTight
        anchors.left: parent.left
        anchors.right: parent.right
        height: shelf.showError ? errorLine.height
              : shelf.showSkeleton ? skeletonRow.height
              : railView.height

        Row {
            id: skeletonRow

            visible: shelf.showSkeleton
            x: Theme.pageMarginValue
            spacing: Theme.spacingValue
            height: shelf.cardHeight

            Repeater {
                model: shelf.showSkeleton ? shelf.skeletonCount : 0

                delegate: Column {
                    id: ghost

                    required property int index

                    spacing: Theme.spacingTight

                    StrmSkeleton {
                        objectName: "crateShelfSkeleton-" + ghost.index
                        width: shelf.cardWidth
                        height: shelf.cardWidth
                        radius: shelf.skeletonShape === "round" ? shelf.cardWidth / 2 : Theme.crateSleeveRadius
                    }

                    // The caption bar: centred under a portrait, flush under a sleeve.
                    // A Column child takes x, not anchors.
                    StrmSkeleton {
                        x: shelf.skeletonShape === "round" ? shelf.cardWidth * 0.15 : 0
                        width: shelf.cardWidth * 0.7
                        height: Theme.fontSmall
                        radius: Theme.radiusChip
                    }
                }
            }
        }

        ShelfError {
            id: errorLine
            objectName: "crateShelfError"

            x: Theme.pageMarginValue
            visible: shelf.showError
            focus: shelf.showError
            message: shelf.errorText
            onRetry: {
                if (shelf.lane !== null)
                    shelf.lane.retry()
            }
        }

        StrmRail {
            id: railView

            width: body.width
            visible: !shelf.showError && !shelf.showSkeleton
            focus: !shelf.showError
            showHeading: false
            title: shelf.title
            railModel: shelf.lane !== null ? shelf.lane.model : null
            cardComponent: shelf.delegate
            customCardWidth: shelf.cardWidth
            customCardHeight: shelf.cardHeight
            navigationFocusKey: shelf.navigationFocusKey

            onItemActivated: index => shelf.itemActivated(index)
            onItemPlayRequested: index => shelf.itemPlayRequested(index)
            onMenuRequested: (index, mx, my) => shelf.menuRequested(index, mx, my)
        }
    }
}
