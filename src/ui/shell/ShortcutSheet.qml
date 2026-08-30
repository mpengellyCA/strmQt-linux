pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import StrmQt

// ShortcutSheet — every binding in the app, on one screen (ARCHITECTURE.md).
//
// Rendered straight from `Input.actions`, never from a hand-maintained list:
// the sheet and the shortcuts that actually fire cannot drift apart, and a
// rebound key shows its new sequence here the moment InputMap emits
// actionsChanged. Rows marked with a dot are user-customised.
//
// Read-only by design — remapping is a later milestone, and a sheet that half
// edits is worse than one that clearly only tells you.
//
//   ShortcutSheet { id: sheet; anchors.fill: parent }
//   ...
//   sheet.toggle()
Item {
    id: sheet

    property bool opened: false

    signal closed

    function open(): void {
        sheet.opened = true;
        scope.forceActiveFocus(Qt.OtherFocusReason);
    }

    function close(): void {
        if (!sheet.opened)
            return;
        sheet.opened = false;
        sheet.closed();
    }

    function toggle(): void {
        if (sheet.opened)
            sheet.close();
        else
            sheet.open();
    }

    // One record per category: { name, actions }. Re-derived whenever InputMap
    // reports a change — `Input.actions` is the notifying property that makes
    // the invokable lookups below live rather than one-shot.
    readonly property var catalogue: {
        const revision = Input.actions;
        const out = [];
        if (revision.length === 0)
            return out;
        const names = Input.categories();
        for (let i = 0; i < names.length; ++i)
            out.push({ "name": names[i], "actions": Input.actionsForCategory(names[i]) });
        return out;
    }

    visible: opacity > 0.01
    enabled: sheet.opened
    opacity: sheet.opened ? 1.0 : 0.0

    Behavior on opacity {
        NumberAnimation { duration: Theme.animFastMs; easing.type: Theme.easeStandard }
    }

    // A key cap: mono, boxed, the way the key is actually printed.
    component KeyCap: Rectangle {
        id: cap

        property string label: ""

        implicitWidth: capText.implicitWidth + Theme.spacingTight * 2
        implicitHeight: Theme.scale(24)
        radius: Theme.radiusChip
        color: Theme.surfaceRaisedColor
        border.width: 1
        border.color: Theme.hairline

        Text {
            id: capText
            anchors.centerIn: parent
            text: cap.label
            color: Theme.textPrimaryColor
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontCaption
        }
    }

    // Click-away dismiss; the scrim is also what stops clicks reaching the page.
    Rectangle {
        anchors.fill: parent
        color: Theme.scrimColor

        TapHandler {
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: sheet.close()
        }
    }

    FocusScope {
        id: scope

        anchors.fill: parent

        Keys.onEscapePressed: event => {
            event.accepted = true;
            sheet.close();
        }

        // A Flickable answers the pointer and nothing else, so this sheet could
        // be opened and not read: the catalogue is longer than the panel on any
        // window, and the input most likely to be consulting it — a gamepad,
        // whose Guide button opens it — has no wheel and no scrollbar to drag.
        // The arrows, the paging keys and Home/End all move it here.
        function scrollBy(dy) {
            const maximum = Math.max(0, flick.contentHeight - flick.height);
            flick.contentY = Math.max(0, Math.min(maximum, flick.contentY + dy));
        }

        Keys.onPressed: event => {
            const line = Theme.controlHeight;
            switch (event.key) {
            case Qt.Key_Down:
                scope.scrollBy(line);
                break;
            case Qt.Key_Up:
                scope.scrollBy(-line);
                break;
            case Qt.Key_PageDown:
                scope.scrollBy(flick.height * 0.9);
                break;
            case Qt.Key_PageUp:
                scope.scrollBy(-flick.height * 0.9);
                break;
            case Qt.Key_Home:
                flick.contentY = 0;
                break;
            case Qt.Key_End:
                flick.contentY = Math.max(0, flick.contentHeight - flick.height);
                break;
            default:
                return;
            }
            event.accepted = true;
        }

        Rectangle {
            id: surface

            anchors.centerIn: parent
            width: Math.min(parent.width - Theme.pageMarginValue * 2, Theme.scale(880))
            height: Math.min(parent.height - Theme.pageMarginValue * 2, Theme.scale(720))
            radius: Theme.radiusPanel
            color: Theme.surfaceOverlay
            border.width: 1
            border.color: Theme.hairline

            // Swallow clicks on the sheet itself so they do not reach the scrim.
            TapHandler { gesturePolicy: TapHandler.ReleaseWithinBounds }

            Text {
                id: heading

                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: closeButton.left
                anchors.margins: Theme.spacingLoose
                text: qsTr("Keyboard shortcuts")
                color: Theme.textPrimaryColor
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            StrmIconButton {
                id: closeButton

                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: Theme.spacingValue
                iconName: "close"
                tooltip: qsTr("Close")
                shortcut: "Esc"
                onClicked: sheet.close()
            }

            Rectangle {
                id: headingRule

                anchors.top: heading.bottom
                anchors.topMargin: Theme.spacingValue
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: Theme.spacingLoose
                anchors.rightMargin: Theme.spacingLoose
                height: 1
                color: Theme.hairline
            }

            Flickable {
                id: flick

                anchors.top: headingRule.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: footer.top
                anchors.margins: Theme.spacingLoose
                clip: true
                contentWidth: width
                contentHeight: body.implicitHeight
                boundsBehavior: Flickable.StopAtBounds

                ScrollBar.vertical: StrmScrollBar {}

                Column {
                    id: body

                    width: flick.width
                    spacing: Theme.spacingLoose

                    Repeater {
                        model: sheet.catalogue

                        Column {
                            id: group

                            required property var modelData

                            width: body.width
                            spacing: Theme.spacingTight

                            Text {
                                text: group.modelData.name
                                color: Theme.textTertiary
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.fontCaption
                                font.letterSpacing: Theme.fontCaption * Theme.trackLabel
                                font.capitalization: Font.AllUppercase
                            }

                            Repeater {
                                model: group.modelData.actions

                                Item {
                                    id: row

                                    required property var modelData

                                    readonly property var sequenceList: row.modelData.sequences !== undefined
                                                                        ? row.modelData.sequences : []
                                    readonly property string gamepadHint: row.modelData.gamepad !== undefined
                                                                          ? String(row.modelData.gamepad) : ""

                                    width: group.width
                                    height: Theme.controlHeight

                                    Text {
                                        anchors.left: parent.left
                                        anchors.right: keys.left
                                        anchors.rightMargin: Theme.spacingValue
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: row.modelData.custom === true
                                              ? qsTr("%1 · customised").arg(row.modelData.name)
                                              : row.modelData.name
                                        color: Theme.textPrimaryColor
                                        font.family: Theme.fontBody
                                        font.pixelSize: Theme.fontBodySize
                                        elide: Text.ElideRight
                                    }

                                    Row {
                                        id: keys

                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: Theme.spacingTight

                                        Repeater {
                                            model: row.sequenceList

                                            KeyCap {
                                                required property string modelData
                                                label: modelData
                                            }
                                        }

                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            visible: row.gamepadHint.length > 0
                                            text: row.gamepadHint
                                            color: Theme.textTertiary
                                            font.family: Theme.fontMono
                                            font.pixelSize: Theme.fontCaption
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Text {
                id: footer

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: Theme.spacingLoose
                text: qsTr("Rebinding lives in Settings. Esc closes this sheet.")
                color: Theme.textTertiary
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontSmall
                elide: Text.ElideRight
            }
        }
    }
}
