// No `pragma ComponentBehavior: Bound` here, deliberately: a bound inline
// component cannot be instantiated from another file ("Cannot instantiate bound
// inline component in different file"), which is the one thing every component
// below exists to do. Nothing here needs it — no component reaches out of its
// own scope, and the Repeaters that do live in SettingsPage.qml, which is bound.
import QtQuick
import StrmQt

// The settings page's row vocabulary (ARCHITECTURE.md).
//
// These are the shapes a preference can take — a read-only fact, a labelled
// control, a caveat, a colour swatch, a key binding — factored out of
// `SettingsPage.qml` so the page reads as a list of preferences rather than a
// list of Rectangles. They are deliberately NOT in `ui/controls/`: that library
// is the application-wide component language (§2.7), and a label/hint/control
// row is the settings page's own typographic idiom.
//
// Used from outside as inline components:
//
//     SettingsSections.SettingRow { label: "…"; StrmSwitch {} }
//
// The file's own root is never instantiated; it exists to carry the components.
Item {
    id: sections

    implicitWidth: 0
    implicitHeight: 0
    visible: false

    // ── A read-only fact: label on the left, value on the right ────────────
    component InfoRow: Item {
        id: infoRow

        property string label: ""
        property string value: ""
        // Technical readouts (URLs, versions, codecs) are set in mono, like a
        // projection booth's gear labels (ARCHITECTURE.md).
        property bool mono: false

        implicitHeight: Math.max(labelText.implicitHeight, valueText.implicitHeight)
                        + Theme.spacingTight

        Text {
            id: labelText

            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.scale(150)
            text: infoRow.label
            color: Theme.textTertiary
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontSmall
            elide: Text.ElideRight
        }

        Text {
            id: valueText

            anchors.left: labelText.right
            anchors.leftMargin: Theme.spacingValue
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: infoRow.value
            color: Theme.textPrimaryColor
            font.family: infoRow.mono ? Theme.fontMono : Theme.fontBody
            font.pixelSize: Theme.fontSmall
            elide: Text.ElideRight
        }
    }

    // ── A labelled control: label and hint on the left, controls right ─────
    // The controls are whatever the caller declares as children; they are laid
    // out in a Row, so a preference that needs a slider *and* a readout does
    // not need its own container.
    component SettingRow: Item {
        id: settingRow

        property string label: ""
        property string hint: ""

        default property alias controls: controlRow.data

        implicitHeight: Math.max(labelBlock.implicitHeight, controlRow.implicitHeight)
                        + Theme.spacingTight
        height: implicitHeight

        Column {
            id: labelBlock

            anchors.left: parent.left
            anchors.right: controlRow.left
            anchors.rightMargin: Theme.spacingValue
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.scale(2)

            Text {
                width: parent.width
                text: settingRow.label
                color: Theme.textPrimaryColor
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontBodySize
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                visible: settingRow.hint.length > 0
                text: settingRow.hint
                color: Theme.textTertiary
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontCaption
                lineHeight: Theme.lineNormal
                lineHeightMode: Text.ProportionalHeight
                wrapMode: Text.WordWrap
            }
        }

        Row {
            id: controlRow

            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingTight
        }
    }

    // ── A caveat that belongs to a group, not to one row ───────────────────
    // "This only takes effect on the next playback" is the kind of thing that
    // makes a control look broken if it is not said out loud.
    component SectionNote: Item {
        id: note

        property string text: ""
        property string iconName: "info"
        property color tone: Theme.textTertiary

        implicitHeight: noteText.implicitHeight + Theme.spacingTight

        StrmIcon {
            id: noteGlyph

            anchors.left: parent.left
            anchors.top: parent.top
            anchors.topMargin: Theme.spacingTight
            name: note.iconName
            size: Theme.scale(15)
            color: note.tone
        }

        Text {
            id: noteText

            anchors.left: noteGlyph.right
            anchors.leftMargin: Theme.spacingTight
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: note.text
            color: note.tone
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontCaption
            lineHeight: Theme.lineNormal
            lineHeightMode: Text.ProportionalHeight
            wrapMode: Text.WordWrap
        }
    }

    // ── A subtitle colour choice ───────────────────────────────────────────
    // A swatch rather than a colour dialog: mpv takes an #RRGGBB, and the six
    // colours anyone actually uses for subtitles fit in a row. Pointer- and
    // focus-capable like everything else, and `selected` is controlled by the
    // owner so it can never disagree with what is stored.
    component ColorSwatch: Item {
        id: swatch

        property color swatchColor: "#FFFFFF"
        property string label: ""
        property bool selected: false

        signal picked

        implicitWidth: Theme.scale(30)
        implicitHeight: Theme.scale(30)
        activeFocusOnTab: swatch.enabled
        opacity: swatch.enabled ? 1.0 : 0.45

        Accessible.role: Accessible.RadioButton
        Accessible.name: swatch.label
        Accessible.checked: swatch.selected
        Accessible.onPressAction: swatch.picked()

        scale: swatchTap.pressed ? Theme.pressScale
             : swatchHover.hovered ? Theme.hoverScale
             : 1.0

        Behavior on scale {
            NumberAnimation {
                duration: Theme.animInstant
                easing.type: Theme.easeInstant
            }
        }

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusChip
            color: swatch.swatchColor
            border.width: 1
            border.color: swatch.selected ? Theme.accentColor : Theme.hairline
        }

        // The tick is drawn in the swatch's own contrast, not in the accent:
        // an amber check on an amber-ish swatch would vanish.
        StrmIcon {
            anchors.centerIn: parent
            visible: swatch.selected
            name: "check"
            size: Theme.scale(16)
            color: (swatch.swatchColor.r * 0.299 + swatch.swatchColor.g * 0.587
                    + swatch.swatchColor.b * 0.114) > 0.6 ? Theme.ground : Theme.textPrimaryColor
        }

        FocusRing {
            active: swatch.activeFocus
            radius: Theme.radiusChip
            inset: -Theme.scale(3)
        }

        HoverHandler {
            id: swatchHover
            cursorShape: Qt.PointingHandCursor
        }

        TapHandler {
            id: swatchTap
            onTapped: {
                swatch.forceActiveFocus(Qt.MouseFocusReason);
                swatch.picked();
            }
        }

        Keys.onPressed: event => {
            if (event.isAutoRepeat)
                return;
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                    || event.key === Qt.Key_Space) {
                swatch.picked();
                event.accepted = true;
            }
        }
    }

    // ── The subtitle sample (ARCHITECTURE.md) ────────────────────────────────
    // Choosing a subtitle look by starting playback, watching, going back and
    // adjusting is not a design — it is a debugging session. This draws the
    // same four properties mpv is handed (MpvPlayer::setSubtitleStyle) against
    // a stand-in frame, so the choice is visible where it is made.
    //
    // The mapping is deliberately the one mpv uses, not a prettier one:
    //   scale      → font size multiplier
    //   background → sub-back-color alpha, and below 50 the outline comes back
    //   position   → sub-pos, percent of frame height *from the top* of the
    //                bottom edge of the text, so 100 sits at the bottom and
    //                values past it push the text out of the picture
    component SubtitlePreview: Rectangle {
        id: preview

        property int subtitleScale: 100
        property color subtitleColor: "#FFFFFF"
        property int backgroundOpacity: 0
        property int verticalPosition: 100
        property string sampleText: qsTr("The quick brown fox jumps over the lazy dog.")

        // A stand-in frame, not a black box: the sample has to sit on something
        // with light and dark in it or the outline and the band both look the
        // same as each other.
        implicitHeight: Theme.scale(168)
        radius: Theme.radiusCardValue
        color: Theme.surfaceColor
        clip: true

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop {
                    position: 0.0
                    color: "#2C3A4A"
                }
                GradientStop {
                    position: 0.55
                    color: "#6E6A5E"
                }
                GradientStop {
                    position: 1.0
                    color: "#141210"
                }
            }
        }

        // Something bright behind the text, so a zero-opacity background shows
        // why the outline exists — a blown-out highlight is exactly where real
        // subtitles disappear.
        Rectangle {
            x: parent.width * 0.10
            width: parent.width * 0.30
            y: parent.height * 0.42
            height: parent.height * 0.58
            radius: Theme.radiusChip
            color: "#E4DCCB"
            opacity: 0.42
        }

        Item {
            id: sampleFrame

            anchors.fill: parent

            Rectangle {
                id: sampleBand

                x: Math.round((sampleFrame.width - width) / 2)
                width: Math.min(sampleFrame.width - Theme.spacingValue,
                                sampleLabel.implicitWidth + Theme.spacingTight)
                height: sampleLabel.implicitHeight + Theme.scale(4)
                // sub-pos is where the *bottom* of the text sits, as a percent
                // of frame height measured from the top. The inset stands in
                // for mpv's own bottom margin, without which 100 draws the text
                // flush against the frame edge and looks like a clipping bug
                // rather than the default.
                readonly property int bottomInset: Theme.spacingTight

                y: Math.round(sampleFrame.height * preview.verticalPosition / 100 - height
                              - bottomInset)
                radius: Theme.scale(2)
                color: Qt.rgba(0, 0, 0, preview.backgroundOpacity / 100)

                Behavior on y {
                    NumberAnimation {
                        duration: Theme.animInstant
                        easing.type: Theme.easeInstant
                    }
                }

                Text {
                    id: sampleLabel

                    anchors.centerIn: parent
                    width: Math.min(sampleFrame.width - Theme.spacingLoose, implicitWidth)
                    text: preview.sampleText
                    color: preview.subtitleColor
                    horizontalAlignment: Text.AlignHCenter
                    font.family: Theme.fontBody
                    font.weight: Font.DemiBold
                    font.pixelSize: Math.round(Theme.fontSmall * preview.subtitleScale / 100)
                    style: preview.backgroundOpacity > 50 ? Text.Normal : Text.Outline
                    styleColor: "#000000"
                    wrapMode: Text.WordWrap
                }
            }
        }

        // Honest about the one case where the preview goes blank.
        Text {
            anchors.centerIn: parent
            visible: preview.verticalPosition > 100
            text: qsTr("Below the picture")
            color: Theme.warningColor
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontCaption
        }

        Text {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: Theme.spacingTight
            text: qsTr("PREVIEW")
            color: Theme.textTertiary
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontCaption
            font.letterSpacing: Theme.trackLabel * Theme.fontCaption
        }
    }

    // ── One remappable action (ARCHITECTURE.md) ───────────────────────────────
    // The keyboard binding is editable; the gamepad binding sits beside it,
    // read-only, because "which button is that on the couch" is exactly the
    // question this table exists to answer — and SDL button maps are not
    // user-editable here yet.
    component BindingRow: Item {
        id: bindingRow

        property string actionName: ""
        property string sequence: ""
        property string gamepad: ""
        property bool custom: false
        property bool capturing: false

        signal editRequested
        signal resetRequested

        implicitHeight: Theme.controlHeightLarge
        height: implicitHeight

        Rectangle {
            anchors.fill: parent
            anchors.leftMargin: -Theme.spacingTight
            anchors.rightMargin: -Theme.spacingTight
            radius: Theme.radiusChip
            color: bindingRow.capturing ? Theme.surfaceRaisedColor
                 : rowHover.hovered ? Theme.hoverTint
                 : "transparent"

            Behavior on color {
                ColorAnimation {
                    duration: Theme.animInstant
                    easing.type: Theme.easeInstant
                }
            }
        }

        Text {
            anchors.left: parent.left
            anchors.right: trailing.left
            anchors.rightMargin: Theme.spacingValue
            anchors.verticalCenter: parent.verticalCenter
            text: bindingRow.actionName
            color: Theme.textSecondaryColor
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontSmall
            elide: Text.ElideRight
        }

        Row {
            id: trailing

            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingTight

            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: bindingRow.gamepad.length > 0
                text: bindingRow.gamepad
                color: Theme.textTertiary
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontCaption
            }

            // The key chip is the control: clicking or activating it starts the
            // capture, which is one interaction instead of hunting for a
            // pencil.
            Item {
                id: keyChip

                anchors.verticalCenter: parent.verticalCenter
                implicitWidth: Math.max(Theme.scale(64),
                                        keyLabel.implicitWidth + Theme.spacingValue)
                implicitHeight: Theme.scale(26)
                width: implicitWidth
                height: implicitHeight
                activeFocusOnTab: true

                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Change the shortcut for %1").arg(bindingRow.actionName)
                Accessible.onPressAction: bindingRow.editRequested()

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusChip
                    color: chipHover.hovered ? Theme.surfaceRaisedColor : Theme.surfaceColor
                    border.width: 1
                    border.color: bindingRow.capturing ? Theme.accentColor
                                : bindingRow.custom ? Theme.accentMuted
                                : Theme.hairline

                    Behavior on color {
                        ColorAnimation {
                            duration: Theme.animInstant
                            easing.type: Theme.easeInstant
                        }
                    }
                }

                Text {
                    id: keyLabel

                    anchors.centerIn: parent
                    text: bindingRow.capturing ? qsTr("press a key…")
                        : bindingRow.sequence.length > 0 ? bindingRow.sequence
                        : qsTr("unbound")
                    color: bindingRow.capturing ? Theme.accentColor
                         : bindingRow.custom ? Theme.accentColor
                         : bindingRow.sequence.length > 0 ? Theme.textPrimaryColor
                         : Theme.textTertiary
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontCaption
                }

                FocusRing {
                    active: keyChip.activeFocus
                    radius: Theme.radiusChip
                    inset: -Theme.scale(3)
                }

                HoverHandler {
                    id: chipHover
                    cursorShape: Qt.PointingHandCursor
                }

                TapHandler {
                    onTapped: {
                        keyChip.forceActiveFocus(Qt.MouseFocusReason);
                        bindingRow.editRequested();
                    }
                }

                Keys.onPressed: event => {
                    if (event.isAutoRepeat)
                        return;
                    if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                            || event.key === Qt.Key_Space) {
                        bindingRow.editRequested();
                        event.accepted = true;
                    }
                }
            }

            // Reverting is only offered where there is something to revert; an
            // always-present disabled button would be noise on every row.
            StrmIconButton {
                anchors.verticalCenter: parent.verticalCenter
                visible: bindingRow.custom
                iconName: "refresh"
                size: Theme.scale(26)
                tooltip: qsTr("Restore the default")
                onClicked: bindingRow.resetRequested()
            }

            // Keeps the chip column aligned on rows without a reset button.
            Item {
                anchors.verticalCenter: parent.verticalCenter
                visible: !bindingRow.custom
                width: Theme.scale(26)
                height: Theme.scale(26)
            }
        }

        HoverHandler {
            id: rowHover
        }
    }

    // ── The capture sheet (ARCHITECTURE.md) ───────────────────────────────────
    // A modal scrim rather than an inline "press a key" state: while capturing,
    // every keystroke has to mean "this is the binding" and nothing else, which
    // is only true if nothing behind it can see the key.
    //
    // It reports raw key + modifiers and lets the owner call InputMap; the
    // sheet knows nothing about the input map, so a conflict is the owner's
    // message to write.
    component KeyCaptureSheet: Item {
        id: sheet

        property string actionName: ""
        property string currentSequence: ""
        property string message: ""
        property bool messageIsError: false

        signal captured(int key, int modifiers)
        signal cleared
        signal dismissed

        // Modifier-only presses are the user still assembling the chord, not a
        // binding: Ctrl alone would bind Ctrl and swallow every chord after it.
        function isModifierOnly(key: int): bool {
            return key === Qt.Key_Control || key === Qt.Key_Shift || key === Qt.Key_Alt
                || key === Qt.Key_Meta || key === Qt.Key_AltGr || key === Qt.Key_CapsLock
                || key === Qt.Key_NumLock || key === Qt.Key_ScrollLock;
        }

        function open(): void {
            sheet.visible = true;
            catcher.forceActiveFocus(Qt.OtherFocusReason);
        }

        visible: false
        z: 40

        Rectangle {
            anchors.fill: parent
            color: Theme.scrimColor

            // Clicking away is a cancel — the same as Escape.
            TapHandler {
                gesturePolicy: TapHandler.ReleaseWithinBounds
                onTapped: sheet.dismissed()
            }
        }

        // The panel is wrapped so the click-swallowing handler covers the whole
        // surface: StrmPanel's default property puts children in its body
        // column, which stops short of the header and the padding.
        Item {
            anchors.centerIn: parent
            width: capturePanel.width
            height: capturePanel.height

            // Swallows clicks so tapping the panel does not reach the scrim.
            TapHandler {
                gesturePolicy: TapHandler.ReleaseWithinBounds
            }

            StrmPanel {
                id: capturePanel

                width: Math.min(sheet.width - Theme.spacingLoose * 2, Theme.scale(420))
                height: implicitHeight
                elevation: 4
                title: qsTr("Press a key")
                subtitle: sheet.actionName

                Text {
                    width: parent.width
                    text: sheet.currentSequence.length > 0
                          ? qsTr("Currently %1. Press the new combination now.").arg(sheet.currentSequence)
                          : qsTr("Currently unbound. Press the combination now.")
                    color: Theme.textSecondaryColor
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontSmall
                    lineHeight: Theme.lineNormal
                    lineHeightMode: Text.ProportionalHeight
                    wrapMode: Text.WordWrap
                }

                Text {
                    width: parent.width
                    text: qsTr("Esc cancels · Backspace restores the default")
                    color: Theme.textTertiary
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontCaption
                }

                Text {
                    width: parent.width
                    visible: sheet.message.length > 0
                    text: sheet.message
                    color: sheet.messageIsError ? Theme.negative : Theme.textTertiary
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontCaption
                    lineHeight: Theme.lineNormal
                    lineHeightMode: Text.ProportionalHeight
                    wrapMode: Text.WordWrap
                }

                Item {
                    width: 1
                    height: Theme.spacingTight
                }

                Row {
                    spacing: Theme.spacingTight

                    StrmButton {
                        text: qsTr("Restore default")
                        iconName: "refresh"
                        onClicked: sheet.cleared()
                    }

                    StrmButton {
                        text: qsTr("Cancel")
                        variant: "ghost"
                        onClicked: sheet.dismissed()
                    }
                }
            }
        }

        // The key sink. It is an Item, not a control: it must never be reached
        // by Tab, and it must hold focus for exactly as long as the sheet is up.
        Item {
            id: catcher

            anchors.fill: parent
            focus: sheet.visible

            Keys.onPressed: event => {
                event.accepted = true;
                if (event.isAutoRepeat)
                    return;
                if (event.key === Qt.Key_Escape) {
                    sheet.dismissed();
                    return;
                }
                if (event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete) {
                    sheet.cleared();
                    return;
                }
                if (sheet.isModifierOnly(event.key))
                    return;
                sheet.captured(event.key, event.modifiers);
            }

            // Releases would otherwise reach the page behind the scrim.
            Keys.onReleased: event => {
                event.accepted = true;
            }
        }
    }
}
