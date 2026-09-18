pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import StrmQt

// GenrePicker — the Genre pill's chooser (Crate spec §5.2).
//
// An overlay and not a menu, for the reason PlaylistPicker is one: the
// measured library has 289 genres. Type to narrow, Up/Down to move, Return or
// a click to tick, Apply to commit. The ticks are local until Apply, so trying
// three genres costs one query, not six.
//
// It builds no strings: every row arrives from MusicBrowseCtl.genreOptions
// with its name, its count and its subtitle ("40 records").
//
// Ruling P2-R2: a row's hover only previews it (`list.currentIndex`), never
// takes the caret out of the search field, and the delegate never sets
// `activeFocusOnTab` — so, like `StrmRail`'s hover chevrons, 289 rows never
// add 289 Tab stops. Keyboard focus always lives on `field` or one of the
// two `StrmButton`s below, and both already show it (the field's own border,
// the buttons' own `FocusRing`), so this control adds none of its own.
FocusScope {
    id: picker

    property var options: []
    property bool loading: false
    property bool failed: false
    property bool opened: false
    property var picked: []
    property var rows: []

    signal genresChosen(var ids)
    signal dismissed
    signal retryRequested

    function open(): void {
        const selected = [];
        const source = picker.options || [];
        for (let i = 0; i < source.length; ++i) {
            if (source[i].selected === true)
                selected.push(String(source[i].id));
        }
        picker.picked = selected;
        picker.opened = true;
        field.text = "";
        picker.rebuild();
        field.forceActiveFocus(Qt.OtherFocusReason);
    }

    function close(): void {
        if (!picker.opened)
            return;
        picker.opened = false;
        picker.dismissed();
    }

    function rebuild(): void {
        const needle = field.text.trim().toLowerCase();
        const source = picker.options || [];
        const out = [];
        for (let i = 0; i < source.length; ++i) {
            if (needle.length === 0 || String(source[i].name).toLowerCase().indexOf(needle) >= 0)
                out.push(source[i]);
        }
        picker.rows = out;
        list.currentIndex = out.length > 0 ? Math.min(Math.max(list.currentIndex, 0), out.length - 1) : -1;
    }

    function isPicked(id): bool {
        return picker.picked.indexOf(String(id)) >= 0;
    }

    function toggle(index): void {
        if (index < 0 || index >= picker.rows.length)
            return;
        const id = String(picker.rows[index].id);
        const next = picker.picked.slice();
        const at = next.indexOf(id);
        if (at >= 0)
            next.splice(at, 1);
        else
            next.push(id);
        picker.picked = next;
    }

    function apply(): void {
        picker.genresChosen(picker.picked.slice());
        picker.close();
    }

    onOptionsChanged: {
        if (picker.opened)
            picker.rebuild();
    }

    anchors.fill: parent
    // `opened` as well as the opacity: open() focuses the field in the same
    // call, and an item that is still invisible then does not take focus.
    visible: picker.opened || picker.opacity > 0.01
    enabled: picker.opened
    opacity: picker.opened ? 1.0 : 0.0

    Behavior on opacity {
        NumberAnimation { duration: Theme.animFastMs; easing.type: Theme.easeStandard }
    }

    Keys.onEscapePressed: event => {
        picker.close();
        event.accepted = true;
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.scrimColor

        TapHandler {
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: picker.close()
        }
    }

    Rectangle {
        id: surface

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Math.round(parent.height * 0.12)
        width: Math.min(parent.width - Theme.pageMarginValue * 2, Theme.scale(560))
        height: head.height + list.height + hint.height + actions.height + Theme.spacingValue * 2
        radius: Theme.radiusPanel
        color: Theme.surfaceOverlay
        border.width: 1
        border.color: Theme.hairline

        TapHandler { gesturePolicy: TapHandler.ReleaseWithinBounds }

        Column {
            id: head

            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: Theme.spacingTight
            spacing: Theme.spacingTight

            Text {
                width: parent.width
                leftPadding: Theme.spacingTight
                topPadding: Theme.spacingTight
                text: picker.picked.length > 0
                      ? qsTr("Genres · %1 selected").arg(picker.picked.length)
                      : qsTr("Genres")
                color: Theme.textPrimaryColor
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            StrmSearchField {
                id: field
                objectName: "genrePickerField"

                width: parent.width
                implicitHeight: Theme.controlHeightLarge
                placeholderText: qsTr("Find a genre…")

                onTextEdited: picker.rebuild()
                onCleared: picker.rebuild()
                onEscapePressed: picker.close()

                KeyNavigation.tab: applyButton

                Keys.onUpPressed: {
                    if (list.count > 0)
                        list.currentIndex = Math.max(0, list.currentIndex - 1);
                }
                Keys.onDownPressed: {
                    if (list.count > 0)
                        list.currentIndex = Math.min(list.count - 1, list.currentIndex + 1);
                }
                Keys.onReturnPressed: event => {
                    if (!event.isAutoRepeat)
                        picker.toggle(list.currentIndex);
                }
                Keys.onEnterPressed: event => {
                    if (!event.isAutoRepeat)
                        picker.toggle(list.currentIndex);
                }
            }
        }

        ListView {
            id: list

            anchors.top: head.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: Theme.spacingTight
            height: Math.min(contentHeight, Theme.scale(400))
            clip: true
            model: picker.rows
            currentIndex: -1
            keyNavigationEnabled: false
            boundsBehavior: Flickable.StopAtBounds
            reuseItems: true

            ScrollBar.vertical: StrmScrollBar {}

            delegate: Item {
                id: row

                required property int index
                required property var modelData

                readonly property bool current: list.currentIndex === row.index
                readonly property bool ticked: picker.isPicked(row.modelData.id)

                objectName: "genrePickerRow-" + row.modelData.id
                // A row previews on hover only (`list.currentIndex`); it never
                // takes a Tab stop of its own (ruling P2-R2), so this is left
                // at its Item default rather than joining the field/buttons'
                // focus chain.
                activeFocusOnTab: false
                width: list.width
                height: Theme.controlHeightLarge

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: Theme.scale(2)
                    radius: Theme.radiusChip
                    color: row.current ? Theme.hoverTint : "transparent"
                }

                Rectangle {
                    id: box

                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacingValue
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.scale(18)
                    height: width
                    radius: Theme.scale(3)
                    color: row.ticked ? Theme.accentColor : "transparent"
                    border.width: row.ticked ? 0 : 1
                    border.color: row.current ? Theme.textSecondaryColor : Theme.hairline

                    StrmIcon {
                        anchors.centerIn: parent
                        visible: row.ticked
                        name: "check"
                        size: Theme.scale(14)
                        color: Theme.accentText
                    }
                }

                Text {
                    anchors.left: box.right
                    anchors.leftMargin: Theme.spacingValue
                    anchors.right: count.left
                    anchors.rightMargin: Theme.spacingValue
                    anchors.verticalCenter: parent.verticalCenter
                    text: String(row.modelData.name)
                    color: row.ticked ? Theme.accentColor : Theme.textPrimaryColor
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontBodySize
                    elide: Text.ElideRight
                }

                Text {
                    id: count

                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingValue
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.modelData.subtitle !== undefined ? String(row.modelData.subtitle) : ""
                    color: Theme.textTertiary
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontCaption
                }

                // Hover previews the row and never takes the caret out of the field.
                HoverHandler {
                    id: rowHover
                    cursorShape: Qt.PointingHandCursor
                    onHoveredChanged: {
                        if (rowHover.hovered)
                            list.currentIndex = row.index;
                    }
                }

                ListView.onReused: {
                    if (rowHover.hovered)
                        list.currentIndex = row.index;
                }

                TapHandler {
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: picker.toggle(row.index)
                }
            }
        }

        Row {
            id: hint

            anchors.top: list.bottom
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingValue
            height: visible ? Theme.controlHeightLarge : 0
            visible: picker.rows.length === 0
            spacing: Theme.spacingValue

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: picker.loading ? qsTr("Loading genres…")
                    : picker.failed ? qsTr("Couldn't load genres.")
                    : (picker.options || []).length === 0 ? qsTr("This library has no genres.")
                    : qsTr("No genre matches.")
                color: Theme.textTertiary
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontSmall
            }

            StrmButton {
                id: retryButton
                objectName: "genrePickerRetry"

                anchors.verticalCenter: parent.verticalCenter
                visible: picker.failed && !picker.loading
                // StrmButton ties activeFocusOnTab to `interactive`, never to
                // `visible`, so without this the hidden Retry keeps a Tab stop
                // of its own — the bug fixed in 6f6b120 for the rail chevrons.
                activeFocusOnTab: retryButton.visible
                text: qsTr("Retry")
                iconName: "refresh"
                variant: "ghost"
                onClicked: picker.retryRequested()
            }
        }

        Row {
            id: actions

            anchors.top: hint.bottom
            anchors.topMargin: Theme.spacingTight
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingValue
            height: Theme.controlHeight
            spacing: Theme.spacingTight

            StrmButton {
                id: clearButton
                objectName: "genrePickerClear"

                text: qsTr("Clear")
                variant: "ghost"
                enabled: picker.picked.length > 0
                onClicked: picker.picked = []

                KeyNavigation.right: applyButton
                KeyNavigation.up: field
            }

            StrmButton {
                id: applyButton
                objectName: "genrePickerApply"

                text: qsTr("Apply")
                iconName: "check"
                variant: "primary"
                onClicked: picker.apply()

                KeyNavigation.left: clearButton
                KeyNavigation.up: field
                KeyNavigation.tab: field
            }
        }
    }
}
