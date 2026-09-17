pragma ComponentBehavior: Bound
import QtQuick
import StrmQt

// HOME · ALBUMS · ARTISTS · SONGS · GENRES · PLAYLISTS (spec §5.1).
//
// One tab stop that owns Left/Right while it has somewhere to go. Left at the
// first section is declined, so the page's edge rule opens the navigation rail.
// Choosing a section only asks: `sectionChosen(key)`. The page that owns the
// route decides, and `currentKey` changes when a page says it is on screen.
FocusScope {
    id: strip

    property string currentKey: "home"
    property var keys: ["home", "albums", "artists", "songs", "genres", "playlists"]
    property int cursor: Math.max(0, strip.keys.indexOf(strip.currentKey))

    signal sectionChosen(string key)

    readonly property var labels: ({
        "home": qsTr("Home"),
        "albums": qsTr("Albums"),
        "artists": qsTr("Artists"),
        "songs": qsTr("Songs"),
        "genres": qsTr("Genres"),
        "playlists": qsTr("Playlists")
    })

    function labelFor(key: string): string {
        const label = strip.labels[key]
        return label === undefined ? key : String(label)
    }

    // The shoulders. Emits the next section and reports whether it did;
    // currentKey stays until the page for that section is on screen.
    function cycle(step): bool {
        const count = strip.keys.length
        if (count <= 1)
            return false
        const from = Math.max(0, strip.keys.indexOf(strip.currentKey))
        const next = ((from + Number(step)) % count + count) % count
        strip.sectionChosen(strip.keys[next])
        return true
    }

    function choose(index: int): void {
        if (index < 0 || index >= strip.keys.length)
            return
        strip.cursor = index
        if (strip.keys[index] !== strip.currentKey)
            strip.sectionChosen(strip.keys[index])
    }

    function resetCursor(): void {
        strip.cursor = Math.max(0, strip.keys.indexOf(strip.currentKey))
    }

    activeFocusOnTab: true
    implicitWidth: row.implicitWidth
    implicitHeight: row.implicitHeight
    width: implicitWidth
    height: implicitHeight

    onCurrentKeyChanged: strip.resetCursor()
    onActiveFocusChanged: {
        if (!strip.activeFocus)
            strip.resetCursor()
    }

    Accessible.role: Accessible.PageTabList
    Accessible.name: qsTr("Music sections")

    Keys.onLeftPressed: event => {
        if (strip.cursor > 0) {
            strip.cursor--
            event.accepted = true
        } else {
            event.accepted = false
        }
    }
    Keys.onRightPressed: event => {
        // No wrap, and no escape to the right either: the strip is the row.
        if (strip.cursor < strip.keys.length - 1)
            strip.cursor++
        event.accepted = true
    }
    Keys.onReturnPressed: event => { if (!event.isAutoRepeat) strip.choose(strip.cursor) }
    Keys.onEnterPressed: event => { if (!event.isAutoRepeat) strip.choose(strip.cursor) }
    Keys.onSpacePressed: event => { if (!event.isAutoRepeat) strip.choose(strip.cursor) }

    Row {
        id: row

        spacing: Theme.spacingTight

        Repeater {
            model: strip.keys

            delegate: Row {
                id: cell

                required property int index
                required property string modelData

                readonly property bool selected: cell.modelData === strip.currentKey
                readonly property bool cursorHere: strip.activeFocus && strip.cursor === cell.index

                spacing: Theme.spacingTight

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: cell.index > 0
                    text: "·"
                    color: Theme.textTertiary
                    font.family: Theme.fontDisplay
                    font.pixelSize: Theme.crateStripSize
                }

                Item {
                    id: tab

                    width: label.implicitWidth + Theme.spacingTight * 2
                    height: label.implicitHeight + Theme.spacingTight * 2

                    Accessible.role: Accessible.PageTab
                    Accessible.name: label.text
                    Accessible.checkable: true
                    Accessible.checked: cell.selected
                    Accessible.onPressAction: strip.choose(cell.index)

                    CrateHeading {
                        id: label
                        anchors.centerIn: parent
                        pixelSize: Theme.crateStripSize
                        text: strip.labelFor(cell.modelData)
                        color: cell.selected || tabHover.hovered ? Theme.textPrimaryColor
                                                                 : Theme.textSecondaryColor
                    }

                    // The inset underline marks the section on screen, not the cursor.
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.leftMargin: Theme.spacingTight
                        anchors.rightMargin: Theme.spacingTight
                        height: Theme.scale(2)
                        visible: cell.selected
                        color: Theme.accentColor
                    }

                    FocusRing {
                        active: cell.cursorHere
                        radius: Theme.radiusChip
                    }

                    HoverHandler {
                        id: tabHover
                        cursorShape: Qt.PointingHandCursor
                    }

                    TapHandler {
                        gesturePolicy: TapHandler.ReleaseWithinBounds
                        onTapped: strip.choose(cell.index)
                    }
                }
            }
        }
    }
}
