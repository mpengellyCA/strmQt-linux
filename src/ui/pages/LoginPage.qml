import QtQuick
import StrmQt

// First-run / logged-out state: server + credentials.
//
// Credentials are never stored. `Session.login()` exchanges them for a token
// and only the token reaches SecretsStore; nothing here writes either field
// anywhere, and the password field is cleared the moment a sign-in is issued.
//
// This is the first screen anyone sees, so it gets the projection-booth
// treatment in full (ARCHITECTURE.md): warm near-black ground, one amber beam
// falling from the top, the wordmark set in the display face, and every
// technical readout in mono.
FocusScope {
    id: page

    objectName: "loginPage"

    function submit() {
        if (Session.busy)
            return;
        Session.serverUrl = serverField.text.trim();
        Session.login(userField.text.trim(), passwordField.text);
        // The password lives exactly as long as the call that consumes it.
        passwordField.text = "";
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.ground
    }

    // The beam. One gradient, no shader: amber bleeding down out of the top of
    // the frame the way a projector lamp bleeds into a dark room.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: parent.height * 0.55

        gradient: Gradient {
            GradientStop {
                position: 0.0
                color: Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.10)
            }
            GradientStop {
                position: 1.0
                color: "transparent"
            }
        }
    }

    Column {
        id: layout

        anchors.centerIn: parent
        width: Math.min(Theme.scale(440), page.width - 2 * Theme.pageMarginValue)
        spacing: Theme.spacingLoose

        Column {
            width: parent.width
            spacing: Theme.spacingTight

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "StrmQt"
                color: Theme.textPrimaryColor
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.fontDisplaySize
                font.weight: Font.DemiBold
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Sign in to your Emby server")
                color: Theme.textSecondaryColor
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontBodySize
            }
        }

        StrmPanel {
            id: panel

            width: parent.width
            elevation: 3
            padding: Theme.spacingLoose

            LoginField {
                id: serverField

                width: parent.width
                label: qsTr("Server")
                placeholder: qsTr("https://emby.example.org")
                text: Session.serverUrl
                mono: true
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
                KeyNavigation.down: userField
                KeyNavigation.tab: userField
                onAccepted: userField.forceActiveFocus()
            }

            LoginField {
                id: userField

                width: parent.width
                focus: true
                label: qsTr("Username")
                placeholder: qsTr("Your Emby user")
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                KeyNavigation.up: serverField
                KeyNavigation.down: passwordField
                KeyNavigation.tab: passwordField
                onAccepted: passwordField.forceActiveFocus()
            }

            LoginField {
                id: passwordField

                width: parent.width
                label: qsTr("Password")
                placeholder: qsTr("Never stored")
                secret: true
                KeyNavigation.up: userField
                KeyNavigation.down: signInButton
                KeyNavigation.tab: signInButton
                onAccepted: page.submit()
            }

            // Breathing room between the last field and the commit, so Return
            // never feels like it might have hit the wrong thing.
            Item {
                width: 1
                height: Theme.spacingTight
            }

            StrmButton {
                id: signInButton

                width: parent.width
                variant: "primary"
                busy: Session.busy
                text: Session.busy ? qsTr("Signing in…") : qsTr("Sign in")
                iconName: Session.busy ? "" : "user"
                KeyNavigation.up: passwordField
                onClicked: page.submit()
            }

            // Error surface. Kept inside the panel so the form does not jump on
            // the page when a sign-in fails.
            Row {
                width: parent.width
                spacing: Theme.spacingTight
                visible: Session.errorMessage.length > 0

                StrmIcon {
                    name: "info"
                    color: Theme.negative
                    size: Theme.iconSize
                }

                Text {
                    width: parent.width - Theme.iconSize - Theme.spacingTight
                    text: Session.errorMessage
                    color: Theme.negative
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("StrmQt %1 · credentials are never stored").arg(Qt.application.version)
            color: Theme.textTertiary
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontCaption
        }
    }

    // A labelled text field, styled from tokens only. Not in the shared control
    // library because a login form is the only place in the app that takes free
    // text that is not a search — StrmSearchField is the shared one.
    component LoginField: FocusScope {
        id: field

        property alias text: input.text
        property string label: ""
        property string placeholder: ""
        property bool secret: false
        property bool mono: false
        property alias inputMethodHints: input.inputMethodHints

        signal accepted

        readonly property bool hovered: hover.hovered

        implicitHeight: caption.height + Theme.scale(4) + box.height
        height: implicitHeight
        activeFocusOnTab: true

        Text {
            id: caption

            anchors.left: parent.left
            anchors.top: parent.top
            text: field.label
            color: field.activeFocus ? Theme.accentColor : Theme.textSecondaryColor
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontCaption
            font.capitalization: Font.AllUppercase
            font.letterSpacing: Theme.trackLabel * Theme.fontCaption

            Behavior on color {
                ColorAnimation {
                    duration: Theme.animFastMs
                    easing.type: Theme.easeStandard
                }
            }
        }

        Rectangle {
            id: box

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: caption.bottom
            anchors.topMargin: Theme.scale(4)
            height: Theme.controlHeightLarge
            radius: Theme.radiusChip
            color: field.hovered ? Theme.surfaceRaisedColor : Theme.surfaceColor
            border.width: 1
            border.color: Theme.hairline

            // Hover tracks the cursor; focus glides (ARCHITECTURE.md).
            Behavior on color {
                ColorAnimation {
                    duration: Theme.animInstant
                    easing.type: Theme.easeInstant
                }
            }

            TextInput {
                id: input

                anchors.fill: parent
                anchors.leftMargin: Theme.spacingTight * 1.5
                anchors.rightMargin: Theme.spacingTight * 1.5
                verticalAlignment: TextInput.AlignVCenter
                focus: true
                color: Theme.textPrimaryColor
                selectionColor: Theme.accentColor
                selectedTextColor: Theme.accentText
                font.family: field.mono ? Theme.fontMono : Theme.fontBody
                font.pixelSize: Theme.fontBodySize
                echoMode: field.secret ? TextInput.Password : TextInput.Normal
                selectByMouse: true
                clip: true

                onAccepted: field.accepted()

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    visible: input.text.length === 0
                    text: field.placeholder
                    color: Theme.textTertiary
                    font: input.font
                }
            }
        }

        FocusRing {
            active: field.activeFocus
            anchors.fill: box
            radius: Theme.radiusChip
            inset: -Theme.focusRingWidth
        }

        HoverHandler {
            id: hover
            cursorShape: Qt.IBeamCursor
            // Never forceActiveFocus(): pointing at a field must not move the
            // keyboard's idea of where it is.
        }

        // Clicking is a deliberate act, so it may take focus — and it should
        // land the caret, not just the focus.
        TapHandler {
            onTapped: eventPoint => {
                field.forceActiveFocus(Qt.MouseFocusReason);
                input.forceActiveFocus(Qt.MouseFocusReason);
                input.cursorPosition = input.positionAt(input.mapFromItem(field,
                                                                         eventPoint.position).x,
                                                        input.height / 2);
            }
        }
    }
}
