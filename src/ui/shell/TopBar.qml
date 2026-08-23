import QtQuick
import StrmQt

// TopBar — the persistent application header (ARCHITECTURE.md).
//
// Everything here was previously reachable only by a key nobody was told about:
// back was Esc, search was `/`, settings was F2, and signing out had no path at
// all. This bar is the mouse's copy of all of it, and it teaches the keyboard
// route at the same time — every icon-only control carries a tooltip showing
// the binding, read live from InputMap rather than typed in as a string.
//
// It decides nothing. Every affordance emits a signal and Main.qml navigates,
// so the bar has no idea what a stack is.
//
// In `tv` density the bar auto-hides: at ten feet a persistent header is chrome
// stealing from the artwork, and the pointer that would need it is not there.
// It slides back the moment a pointer approaches the top edge, or when anything
// in it takes keyboard focus — a hidden bar that cannot be tabbed to would be a
// keyboard regression to buy a pointer feature.
Item {
    id: topBar

    property string title: ""
    property bool canGoBack: false
    property bool canGoForward: false
    property string userName: ""
    property alias searchText: field.text

    // Hidden until the pointer comes near, on the couch only.
    property bool autoHide: Theme.densityMode === "tv"

    signal backRequested
    signal forwardRequested
    signal searchRequested(string text)
    signal searchSubmitted(string text)
    signal searchDismissed
    signal settingsRequested
    signal signOutRequested

    // What the page area below must leave clear. Zero while auto-hiding, since
    // the bar then floats over the content instead of displacing it.
    readonly property int reservedHeight: topBar.autoHide ? 0 : Theme.topBarHeight

    readonly property bool childFocused: backButton.activeFocus || forwardButton.activeFocus
                                         || field.activeFocus || settingsButton.activeFocus
                                         || avatar.activeFocus
    readonly property bool shown: !topBar.autoHide || hover.hovered || topBar.childFocused
                                  || userMenu.opened || dwell.running

    // Input.actions is the notifying property that makes the invokable lookups
    // below live: rebind a key and every tooltip here updates.
    readonly property var inputRevision: Input.actions

    function shortcutFor(actionId) {
        return topBar.inputRevision.length > 0 ? Input.binding(actionId) : "";
    }

    function focusSearch(): void {
        field.forceActiveFocus(Qt.OtherFocusReason);
        field.selectAll();
    }

    function clearSearch(): void {
        field.clear();
    }

    implicitHeight: Theme.topBarHeight
    height: Theme.topBarHeight
    clip: true

    // The reveal zone is the strip itself: a plain Item with only a
    // HoverHandler accepts no presses, so content underneath stays clickable
    // while the bar is hidden.
    HoverHandler {
        id: hover
        onHoveredChanged: if (hovered) dwell.restart()
    }

    // Keeps the bar up briefly after the pointer leaves, so reaching for a
    // control does not race the animation.
    Timer {
        id: dwell
        interval: 1200
        repeat: false
    }

    Rectangle {
        id: bar

        anchors.left: parent.left
        anchors.right: parent.right
        height: parent.height
        y: topBar.shown ? 0 : -height
        color: Theme.surfaceColor

        Behavior on y {
            NumberAnimation { duration: Theme.animNormalMs; easing.type: Theme.easeStandard }
        }

        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: Theme.hairline
        }

        Row {
            id: navRow

            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingValue
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingTight / 2

            StrmIconButton {
                id: backButton

                anchors.verticalCenter: parent.verticalCenter
                enabled: topBar.canGoBack
                iconName: "arrow-left"
                tooltip: qsTr("Back")
                shortcut: topBar.shortcutFor("nav.back")
                onClicked: topBar.backRequested()
            }

            StrmIconButton {
                id: forwardButton

                anchors.verticalCenter: parent.verticalCenter
                enabled: topBar.canGoForward
                iconName: "arrow-right"
                tooltip: qsTr("Forward")
                onClicked: topBar.forwardRequested()
            }
        }

        Text {
            id: titleLabel

            anchors.left: navRow.right
            anchors.leftMargin: Theme.spacingValue
            anchors.right: actionRow.left
            anchors.rightMargin: Theme.spacingValue
            anchors.verticalCenter: parent.verticalCenter
            text: topBar.title
            color: Theme.textPrimaryColor
            font.family: Theme.fontDisplay
            font.pixelSize: Theme.fontBodyLarge
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        Row {
            id: actionRow

            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingValue
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingTight

            StrmSearchField {
                id: field

                anchors.verticalCenter: parent.verticalCenter
                implicitWidth: Theme.scale(260)
                placeholderText: qsTr("Search")

                onTextEdited: topBar.searchRequested(field.text)
                onAccepted: topBar.searchSubmitted(field.text)
                onCleared: topBar.searchRequested("")
                onEscapePressed: topBar.searchDismissed()
            }

            StrmIconButton {
                id: settingsButton

                anchors.verticalCenter: parent.verticalCenter
                iconName: "settings"
                tooltip: qsTr("Settings")
                shortcut: topBar.shortcutFor("app.settings")
                onClicked: topBar.settingsRequested()
            }

            // Avatar: the only place the signed-in user is named anywhere in
            // the app, and the only route to signing out.
            Item {
                id: avatar

                anchors.verticalCenter: parent.verticalCenter
                implicitWidth: Theme.controlHeight
                implicitHeight: Theme.controlHeight
                activeFocusOnTab: true

                readonly property string initial: topBar.userName.length > 0
                                                  ? topBar.userName.charAt(0).toUpperCase() : "?"

                function openMenu(): void {
                    const p = avatar.mapToItem(null, 0, avatar.height + Theme.spacingTight);
                    userMenu.popupAt(p.x, p.y);
                }

                scale: avatarTap.pressed ? Theme.pressScale
                     : avatar.activeFocus ? Theme.focusScale
                     : avatarHover.hovered ? Theme.hoverScale
                     : 1.0

                Behavior on scale {
                    NumberAnimation {
                        duration: avatar.activeFocus ? Theme.animFastMs : Theme.animInstant
                        easing.type: avatar.activeFocus ? Theme.easeStandard : Theme.easeInstant
                    }
                }

                Rectangle {
                    id: avatarDisc

                    anchors.fill: parent
                    radius: width / 2
                    color: avatarHover.hovered || avatar.activeFocus
                           ? Theme.accentColor : Theme.surfaceRaisedColor
                    border.width: 1
                    border.color: Theme.hairline

                    Behavior on color {
                        ColorAnimation { duration: Theme.animInstant; easing.type: Theme.easeInstant }
                    }

                    Text {
                        anchors.centerIn: parent
                        text: avatar.initial
                        color: avatarHover.hovered || avatar.activeFocus
                               ? Theme.accentText : Theme.textPrimaryColor
                        font.family: Theme.fontDisplay
                        font.pixelSize: Theme.fontBodySize
                        font.weight: Font.DemiBold
                    }
                }

                FocusRing {
                    active: avatar.activeFocus
                    radius: avatarDisc.radius
                }

                StrmTooltip {
                    id: avatarTip
                    target: avatar
                    text: topBar.userName.length > 0 ? topBar.userName : qsTr("Account")
                }

                HoverHandler {
                    id: avatarHover
                    cursorShape: Qt.PointingHandCursor
                    onHoveredChanged: {
                        if (avatarHover.hovered)
                            avatarTip.requestShow();
                        else
                            avatarTip.requestHide();
                    }
                }

                TapHandler {
                    id: avatarTap
                    onTapped: {
                        avatarTip.requestHide();
                        avatar.forceActiveFocus(Qt.MouseFocusReason);
                        avatar.openMenu();
                    }
                }

                Keys.onReturnPressed: event => { if (!event.isAutoRepeat) avatar.openMenu(); }
                Keys.onEnterPressed: event => { if (!event.isAutoRepeat) avatar.openMenu(); }
                Keys.onSpacePressed: event => { if (!event.isAutoRepeat) avatar.openMenu(); }
            }
        }
    }

    StrmMenu {
        id: userMenu

        actions: [
            { "text": qsTr("Settings"), "iconName": "settings" },
            { "separator": true },
            { "text": qsTr("Sign out"), "iconName": "logout", "destructive": true }
        ]

        onTriggered: index => {
            if (index === 0)
                topBar.settingsRequested();
            else if (index === 2)
                topBar.signOutRequested();
        }
    }
}
