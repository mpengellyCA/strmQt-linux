pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import StrmQt

// PlaylistPicker — the type-and-pick "add to playlist" panel (ARCHITECTURE.md).
//
// A panel and not a menu, because this server has ~1,500 playlists and a menu
// with 1,500 rows is a scrollbar with words on it. Type to narrow, Up/Down to
// choose, Return to file; a name that matches nothing offers to create it, so
// "new playlist from this" is the same gesture as "add to an existing one".
//
// Generalised on one axis only — it files a LIST of ids, so the same panel
// serves one track, a whole record, and a selection.
//
// A registered control rather than the inline `component` it began as: it was
// copied into a second page as soon as a second page needed it, and the Songs
// tab was about to make that a third. Consumers own the surface it reports
// through — `pending` tells a page's PlaylistCtl handlers that the result
// belongs to this panel and not to an edit made somewhere else, and `dismissed`
// is where the keyboard goes back to whatever opened it.
FocusScope {
    id: picker

    property string subject: ""
    property var targetIds: []
    // Emby's MediaType for a playlist this panel CREATES: "Audio" when the
    // panel was raised from a music surface, empty everywhere else so a film
    // keeps making the untyped list it always made.
    //
    // It matters even though the ids being filed are audio. The server derives
    // "which library does this playlist belong to" from the playlist's media
    // type, and that is the only thing the music library's Playlists tab has to
    // filter on — Emby publishes no media type on the playlist itself
    // (measured; see MusicController::loadPlaylists). Consumers set it once,
    // declaratively, rather than passing it per show().
    property string mediaType: ""
    property bool opened: false
    property var records: []
    property var rows: []
    // A write of this page's is in flight, so PlaylistCtl's global results
    // can be told apart from an edit made somewhere else.
    property bool pending: false

    signal dismissed

    function show(subject, ids): void {
        if (!ids || ids.length === 0)
            return
        picker.subject = subject ? subject : ""
        picker.targetIds = ids
        picker.opened = true
        pickerField.text = ""
        if (PlaylistCtl.playlists.count === 0)
            PlaylistCtl.refresh()
        else
            picker.rebuildRecords()
        pickerField.forceActiveFocus(Qt.OtherFocusReason)
    }

    function dismiss(): void {
        if (!picker.opened)
            return
        picker.opened = false
        picker.dismissed()
    }

    function rebuildRecords(): void {
        const model = PlaylistCtl.playlists
        const out = []
        for (let i = 0; i < model.count; ++i) {
            const entry = model.get(i)
            const name = entry.name !== undefined ? String(entry.name) : ""
            out.push({
                "create": false,
                "id": entry.itemId !== undefined ? String(entry.itemId) : "",
                "name": name,
                "lower": name.toLowerCase()
            })
        }
        picker.records = out
        picker.rebuild()
    }

    function rebuild(): void {
        const typed = pickerField.text.trim()
        const needle = typed.toLowerCase()
        const source = picker.records
        const out = []
        let exact = false
        for (let i = 0; i < source.length; ++i) {
            if (source[i].lower === needle)
                exact = true
            if (needle.length === 0 || source[i].lower.indexOf(needle) >= 0)
                out.push(source[i])
        }
        if (typed.length > 0 && !exact)
            out.unshift({ "create": true, "id": "", "name": typed, "lower": needle })
        picker.rows = out
        pickerList.currentIndex = out.length > 0 ? 0 : -1
    }

    function activate(index): void {
        if (index < 0 || index >= picker.rows.length || picker.targetIds.length === 0)
            return
        const row = picker.rows[index]
        picker.pending = true
        if (row.create)
            PlaylistCtl.create(row.name, picker.targetIds, picker.mediaType)
        else
            PlaylistCtl.addItems(row.id, picker.targetIds)
        picker.dismiss()
    }

    anchors.fill: parent
    // `opened` as well as the animated opacity: forceActiveFocus() runs in
    // the same call as show(), and an item that is still invisible at that
    // moment does not take focus.
    visible: picker.opened || picker.opacity > 0.01
    enabled: picker.opened
    opacity: picker.opened ? 1.0 : 0.0

    Behavior on opacity {
        NumberAnimation { duration: Theme.animFastMs; easing.type: Theme.easeStandard }
    }

    Connections {
        target: PlaylistCtl.playlists
        function onCountChanged() {
            if (picker.opened)
                picker.rebuildRecords()
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.scrimColor

        TapHandler {
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: picker.dismiss()
        }
    }

    Rectangle {
        id: pickerSurface

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Math.round(parent.height * 0.14)
        width: Math.min(parent.width - Theme.pageMarginValue * 2, Theme.scale(620))
        height: pickerHead.height + pickerList.height + pickerHint.height
                + Theme.spacingValue
        radius: Theme.radiusPanel
        color: Theme.surfaceOverlay
        border.width: 1
        border.color: Theme.hairline

        // Keeps clicks inside the panel off the scrim behind it.
        TapHandler { gesturePolicy: TapHandler.ReleaseWithinBounds }

        Column {
            id: pickerHead

            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: Theme.spacingTight
            spacing: Theme.spacingTight

            Text {
                width: parent.width
                leftPadding: Theme.spacingTight
                topPadding: Theme.spacingTight
                text: picker.subject.length > 0
                      ? qsTr("Add “%1” to…").arg(picker.subject)
                      : qsTr("Add to playlist")
                color: Theme.textPrimaryColor
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                maximumLineCount: 1
            }

            StrmSearchField {
                id: pickerField

                width: parent.width
                implicitHeight: Theme.controlHeightLarge
                placeholderText: qsTr("Find a playlist, or type a new name…")

                onTextEdited: picker.rebuild()
                onCleared: picker.rebuild()
                onEscapePressed: picker.dismiss()
                onAccepted: picker.activate(pickerList.currentIndex)

                Keys.onUpPressed: {
                    if (pickerList.count > 0)
                        pickerList.currentIndex = Math.max(0, pickerList.currentIndex - 1)
                }
                Keys.onDownPressed: {
                    if (pickerList.count > 0)
                        pickerList.currentIndex = Math.min(pickerList.count - 1,
                                                           pickerList.currentIndex + 1)
                }
                Keys.onReturnPressed: event => {
                    if (!event.isAutoRepeat)
                        picker.activate(pickerList.currentIndex)
                }
                Keys.onEnterPressed: event => {
                    if (!event.isAutoRepeat)
                        picker.activate(pickerList.currentIndex)
                }
            }
        }

        ListView {
            id: pickerList

            anchors.top: pickerHead.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: Theme.spacingTight
            anchors.rightMargin: Theme.spacingTight
            anchors.topMargin: Theme.spacingTight
            height: Math.min(contentHeight, Theme.scale(380))
            clip: true
            model: picker.rows
            currentIndex: -1
            keyNavigationEnabled: false
            highlightMoveDuration: Theme.animFastMs
            boundsBehavior: Flickable.StopAtBounds
            cacheBuffer: Theme.controlHeightLarge * 6

            ScrollBar.vertical: StrmScrollBar {}

            delegate: Item {
                id: pickerRow

                required property int index
                required property var modelData

                readonly property bool current: pickerList.currentIndex === pickerRow.index

                width: pickerList.width
                height: Theme.controlHeightLarge

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: Theme.scale(2)
                    radius: Theme.radiusChip
                    color: pickerRow.current ? Theme.hoverTint : "transparent"

                    Behavior on color {
                        ColorAnimation {
                            duration: Theme.animInstant
                            easing.type: Theme.easeInstant
                        }
                    }
                }

                StrmIcon {
                    id: pickerGlyph

                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacingValue
                    anchors.verticalCenter: parent.verticalCenter
                    name: pickerRow.modelData.create ? "plus" : "playlist"
                    color: pickerRow.current ? Theme.accentColor : Theme.textTertiary
                }

                Text {
                    anchors.left: pickerGlyph.right
                    anchors.leftMargin: Theme.spacingValue
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingValue
                    anchors.verticalCenter: parent.verticalCenter
                    // "New playlist from this" is not a second gesture — it is
                    // this row. Saying how many items go into it is what makes
                    // that readable when the panel was raised from a whole
                    // record rather than from one track.
                    text: !pickerRow.modelData.create
                          ? pickerRow.modelData.name
                          : picker.targetIds.length > 1
                          ? qsTr("Create “%1” from %n track(s)", "",
                                 picker.targetIds.length).arg(pickerRow.modelData.name)
                          : qsTr("Create “%1”").arg(pickerRow.modelData.name)
                    color: Theme.textPrimaryColor
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontBodySize
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }

                // Hover previews the row; it never commits, and it never
                // takes the caret out of the field being typed in.
                HoverHandler {
                    cursorShape: Qt.PointingHandCursor
                    onHoveredChanged: {
                        if (hovered)
                            pickerList.currentIndex = pickerRow.index
                    }
                }

                TapHandler {
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: picker.activate(pickerRow.index)
                }
            }
        }

        Text {
            id: pickerHint

            anchors.top: pickerList.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: Theme.spacingValue
            anchors.rightMargin: Theme.spacingValue
            height: pickerHint.visible ? Theme.controlHeightLarge : Theme.spacingTight
            verticalAlignment: Text.AlignVCenter
            visible: picker.rows.length === 0
            text: picker.records.length === 0
                  ? qsTr("You have no playlists yet — type a name to make one.")
                  : qsTr("Nothing matches. Keep typing to create a new playlist.")
            color: Theme.textTertiary
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.WordWrap
        }
    }
}
