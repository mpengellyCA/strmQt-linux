import QtQuick
import StrmQt

// Logged-out state: a profile picker when accounts are saved, otherwise (or on
// request) the server + credentials form.
//
// The password is never stored. `Session.login()` exchanges it for a token;
// KWallet persists that token when available, otherwise it falls back to the
// vault file — lower security, and the banner below says so. The password
// field is cleared as sign-in is issued.
//
// This is the first screen anyone sees, so it gets the projection-booth
// treatment in full (ARCHITECTURE.md): warm near-black ground, one amber beam
// falling from the top, the wordmark set in the display face, and every
// technical readout in mono.
FocusScope {
    id: page

    objectName: "loginPage"

    // The picker is the default while accounts are saved; the form is a
    // request ("Use a different account") and the only state when none are.
    property bool formRequested: false
    // Manage mode turns tile taps into removals (Netflix's edit flow).
    property bool manageMode: false
    // One width for the tile and the Flow's row math, so they cannot drift.
    readonly property int profileTileWidth: Theme.scale(118)

    readonly property bool showForm: formRequested || Session.profiles.length === 0
    readonly property bool multipleServers: {
        const seen = [];
        for (const profile of Session.profiles) {
            if (!seen.includes(profile.serverUrl))
                seen.push(profile.serverUrl);
        }
        return seen.length > 1;
    }

    function submit() {
        if (Session.busy)
            return;
        Session.serverUrl = serverField.text.trim();
        Session.login(userField.text.trim(), passwordField.text);
        // The password lives exactly as long as the call that consumes it.
        passwordField.text = "";
    }

    // Prefill imperatively, not via a binding: Session.username has no change
    // signal of its own, and a stale-token handoff from the picker sets it
    // after this page already exists.
    Component.onCompleted: userField.text = Session.username

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
                text: page.showForm ? qsTr("Sign in to your Emby server") : qsTr("Who's watching?")
                color: Theme.textSecondaryColor
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontBodySize
            }
        }

        // ── Profile picker ─────────────────────────────────────────────────
        Column {
            id: picker

            width: parent.width
            height: visible ? implicitHeight : 0
            visible: !page.showForm
            spacing: Theme.spacingLoose
            clip: true

            Flow {
                // Center the tiles as a block: a Flow fills rows left to
                // right, so the block is exactly as wide as its widest row.
                readonly property int tileStep: page.profileTileWidth + Theme.spacingLoose
                readonly property int perRow: Math.max(
                                                  1, Math.floor((picker.width + Theme.spacingLoose) /
                                                                tileStep))
                readonly property int widestRow: Math.min(Session.profiles.length, perRow)

                anchors.horizontalCenter: parent.horizontalCenter
                width: widestRow > 0 ? widestRow * tileStep - Theme.spacingLoose : 0
                spacing: Theme.spacingLoose

                Repeater {
                    model: Session.profiles

                    delegate: ProfileTile {
                        id: tile

                        required property var modelData

                        profile: tile.modelData
                        manage: page.manageMode
                    }
                }
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.spacingTight

                StrmButton {
                    text: qsTr("Use a different account")
                    iconName: "user"
                    onClicked: {
                        // A stale-token error hands off here; prefill the name
                        // of the profile that just failed.
                        userField.text = Session.username;
                        page.formRequested = true;
                    }
                }

                StrmButton {
                    variant: "ghost"
                    text: page.manageMode ? qsTr("Done") : qsTr("Manage profiles")
                    onClicked: page.manageMode = !page.manageMode
                }
            }

            // Selecting a profile whose token is gone surfaces this error; the
            // recovery is "Use a different account", which prefills the name.
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

        // ── Credentials form ───────────────────────────────────────────────
        StrmPanel {
            id: panel

            width: parent.width
            height: visible ? implicitHeight : 0
            visible: page.showForm
            elevation: 3
            padding: Theme.spacingLoose
            clip: true

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

            StrmButton {
                width: parent.width
                variant: "ghost"
                visible: Session.profiles.length > 0
                text: qsTr("Back to profiles")
                iconName: "arrow-left"
                onClicked: page.formRequested = false
            }
        }

        // ── Storage-mode warning ───────────────────────────────────────────
        // The vault file is a deliberate fallback, not a silent one: when the
        // token will not live in KWallet, the login screen says so.
        Row {
            width: parent.width
            spacing: Theme.spacingTight
            visible: Session.secretStorage === "vault"

            StrmIcon {
                name: "info"
                color: Theme.accentColor
                size: Theme.iconSize
            }

            Text {
                width: parent.width - Theme.iconSize - Theme.spacingTight
                text: qsTr("KWallet is unavailable, so your sign-in will be stored in a vault file with lower security — anyone who can read your home folder could take it.")
                color: Theme.textSecondaryColor
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: qsTr("StrmQt %1 · passwords are never stored · access tokens use KWallet, or a vault file when none is available")
                      .arg(Qt.application.version)
            color: Theme.textTertiary
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontCaption
        }
    }

    // A saved account. Tap/Enter selects it (restore the token); in manage mode
    // the same gesture forgets it, marked by the corner badge.
    component ProfileTile: FocusScope {
        id: tile

        required property var profile
        property bool manage: false

        width: page.profileTileWidth
        implicitHeight: tileColumn.implicitHeight
        height: implicitHeight
        activeFocusOnTab: true

        function activate() {
            if (tile.manage)
                Session.removeProfile(tile.profile.serverUrl, tile.profile.userId);
            else
                Session.selectProfile(tile.profile.serverUrl, tile.profile.userId);
        }

        Column {
            id: tileColumn

            width: parent.width
            spacing: Theme.spacingTight

            Item {
                id: avatarBox

                anchors.horizontalCenter: parent.horizontalCenter
                width: Theme.scale(96)
                height: width

                StrmAvatar {
                    anchors.fill: parent
                    name: tile.profile.username
                    imageUrl: Session.profileAvatarUrl(tile.profile.serverUrl,
                                                       tile.profile.userId)
                    opacity: tile.manage ? 0.45 : 1.0
                    border.width: 1
                    border.color: tile.activeFocus ? Theme.accentColor : Theme.hairline

                    Behavior on opacity {
                        NumberAnimation {
                            duration: Theme.animFastMs
                            easing.type: Theme.easeStandard
                        }
                    }
                }

                // Manage-mode remove marker.
                Rectangle {
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: -Theme.scale(6)
                    width: Theme.scale(24)
                    height: width
                    radius: width / 2
                    visible: tile.manage
                    color: Theme.negative

                    StrmIcon {
                        anchors.centerIn: parent
                        name: "close"
                        color: Theme.accentText
                        size: Theme.scale(14)
                    }
                }
            }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: tile.profile.username
                color: tile.activeFocus ? Theme.accentColor : Theme.textPrimaryColor
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontBodySize
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                visible: page.multipleServers
                text: tile.profile.serverUrl
                color: Theme.textTertiary
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideMiddle
            }
        }

        FocusRing {
            active: tile.activeFocus
            anchors.fill: parent
            radius: Theme.radiusPanel
            inset: -Theme.focusRingWidth
        }

        Keys.onReturnPressed: event => {
            if (!event.isAutoRepeat)
                tile.activate();
        }
        Keys.onSpacePressed: event => {
            if (!event.isAutoRepeat)
                tile.activate();
        }

        TapHandler {
            onTapped: tile.activate()
        }

        HoverHandler {
            cursorShape: Qt.PointingHandCursor
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
