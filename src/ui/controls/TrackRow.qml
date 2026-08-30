pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import StrmQt

// TrackRow — one line of a track table (ARCHITECTURE.md).
//
// Four places in this app used to draw a track row and every one of them drew
// it differently: the album page, the playlist page's member pane, the queue
// panel and — once the artist page grew a top-tracks list — a fourth. They were
// never going to gain the artwork fix, the now-playing indicator or a music
// context menu at the same rate, so the row lives here once and the pages
// configure it.
//
// ── What it is NOT ─────────────────────────────────────────────────────────
// It is not a delegate. It carries no `index`, reads no model role and knows no
// controller: the call site's delegate declares the required `index`/`model`
// properties the view injects and hands this component finished strings. That
// is what lets one row serve a MediaItemModel, a PlayQueue and a playlist's
// members without a role-name translation table living in here.
//
// ── Columns ────────────────────────────────────────────────────────────────
// Laid out as a chain of slots from the left — playing marker, number, cover —
// each of which collapses to zero width when it is switched off, and a chain
// from the right — verbs, duration, artist. The label block takes what is left.
// Every column width is a property so one table's set of numbers is decided
// once, above the rows, and the columns cannot drift out of alignment with
// their own headings.
//
// The numerals are mono (`Theme.fontMono`) and right-aligned: a track number
// and a duration are readouts, and a proportional 1 next to a proportional 8
// makes a column that wobbles.
//
// ── Verbs ──────────────────────────────────────────────────────────────────
// The default property is the *extra* verb slot, so a page adds its own buttons
// (remove-from-queue, move up/down) by declaring them inline. Favourite and the
// overflow menu are built in because every music surface wants the same two,
// and they sit last so the shared pair is always in the same place.
//
// Every built-in verb sets `activeFocusOnTab: false`: the table is one tab stop
// and owns the arrow keys (ARCHITECTURE.md §4). N focusable buttons per row
// would make Tab walk a 200-track box set instead of leaving it.
Item {
    id: row

    // ── What the row says ──────────────────────────────────────────────────
    property string title: ""
    // A second line under the title. Present turns the row into the stacked
    // shape the queue and playlist panes use; absent leaves the table shape.
    property string secondary: ""
    // The artist column's text. Whether it is drawn at all is `artistColumn`,
    // which is a table-wide decision — see TrackTable.
    property string artist: ""
    property string durationText: ""
    // Track number or ordinal. Anything <= 0 draws `numberPlaceholder`.
    property int number: -1
    property string numberPlaceholder: "·"
    property string coverUrl: ""
    property bool showCover: false
    property bool showNumber: true
    // The 3 px amber spine the queue uses to mark the entry that is playing.
    // Separate from `playing` because the album table marks the same fact by
    // tinting its number and title instead.
    property bool showPlayingMarker: false
    // Under the pointer, the number column becomes a ▸: the row is clickable
    // and the column is where that is said.
    property bool hoverPlayGlyph: true

    property bool playing: false
    property bool current: false
    // In the table's multi-select set. Drawn as a fill rather than as a ring:
    // the ring is focus and only focus (ARCHITECTURE.md §4), and a row can be
    // selected, focused, hovered and playing all at once.
    property bool selected: false
    // Watched state, drawn on the cover. Meaningless for a track and false
    // there; a playlist holds episodes and films too, and it is kept off the
    // label so a long title never pushes it out of sight.
    property bool played: false
    property bool favorite: false
    property bool showFavorite: false
    property bool showMenu: false
    // Set by TrackTable call sites so row-level pointer verbs can retire an
    // in-progress semantic restore without relying on focus movement.
    property TrackTable navigationFocusOwner: null

    // > 0 draws a disc banner above this row and adds its height. The table
    // decides which rows begin a disc; the row only draws it.
    property int discNumber: -1

    // ── Metrics ────────────────────────────────────────────────────────────
    property int rowHeight: Theme.scale(38)
    property int discHeaderHeight: Theme.scale(32)
    property int numberColumn: Theme.scale(46)
    property int durationColumn: Theme.scale(64)
    property int verbsColumn: Theme.scale(72)
    // Zero means "this table has no artist column", which is the once-per-table
    // decision every row must agree on.
    property int artistColumn: 0
    property int coverSize: Theme.scale(40)
    property int surfaceTopMargin: 0
    property int surfaceBottomMargin: Theme.scale(2)
    property int surfaceRightMargin: 0

    readonly property bool startsDisc: row.discNumber > 0
    readonly property bool hovered: rowHover.hovered

    // Laid out always, faded in and out: verbs that appear and disappear would
    // reflow the columns beside them every time the pointer crossed a row.
    property bool verbsRevealed: row.hovered || row.current || row.favorite

    default property alias verbs: extraVerbs.data

    // `modifiers` is Qt::KeyboardModifiers as they were when the row was
    // clicked, so the TABLE can decide what Ctrl+Click and Shift+Click mean —
    // one place, not one per page. A keyboard activation passes Qt.NoModifier.
    signal activated(int modifiers)
    signal favoriteToggled()
    signal menuRequested(real sceneX, real sceneY)

    // Pointer verbs can keep focus inside the current row, so a focus-change
    // observer cannot distinguish them from restoration. Retire the owning
    // table explicitly before every user verb.
    function cancelNavigationRestore(): void {
        const owner = row.navigationFocusOwner
        if (owner)
            owner.cancelNavigationFocusRestore()
    }

    implicitHeight: row.rowHeight + (row.startsDisc ? row.discHeaderHeight : 0)
    height: row.implicitHeight

    Accessible.role: Accessible.ListItem
    Accessible.name: row.title
    Accessible.description: [row.secondary, row.artist, row.durationText]
                            .filter(part => part.length > 0).join(", ")
    Accessible.selectable: true
    Accessible.selected: row.selected || row.current || row.playing
    Accessible.focused: row.current
    Accessible.onPressAction: {
        row.cancelNavigationRestore()
        row.activated(Qt.NoModifier)
    }

    // Only a NUMBERED disc gets a banner. Measured on this server, a 52-track
    // deluxe edition comes back as discs [-1, 1, 2, 3]: the record proper
    // carries no ParentIndexNumber at all and the bonus discs do. Captioning
    // that first block would mean inventing a name for it, so it simply starts.
    Item {
        id: discBanner

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: row.startsDisc ? row.discHeaderHeight : 0
        visible: row.startsDisc

        Text {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.scale(4)
            text: qsTr("DISC %1").arg(row.discNumber)
            color: Theme.textTertiary
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontCaption
            font.letterSpacing: Theme.fontCaption * Theme.trackLabel
        }
    }

    Rectangle {
        id: surface

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.rightMargin: row.surfaceRightMargin
        anchors.top: discBanner.bottom
        anchors.topMargin: row.surfaceTopMargin
        height: Math.max(0, row.rowHeight - row.surfaceTopMargin - row.surfaceBottomMargin)
        radius: Theme.radiusChip
        // Focus wins visually over hover, and both may be true at once
        // (ARCHITECTURE.md §4).
        color: row.current ? Theme.surfaceRaisedColor
             : row.hovered ? Theme.hoverTint
             : "transparent"

        Behavior on color {
            ColorAnimation {
                duration: Theme.animInstant
                easing.type: Theme.easeInstant
            }
        }

        // Selection is a LAYER over whatever the row's own state painted, not a
        // fourth branch of the chain above. Selected-and-focused is the case
        // that decides it: a branch would have to pick one of the two, and then
        // the focused row would be the one row in a selection that did not look
        // selected.
        Rectangle {
            anchors.fill: parent
            radius: surface.radius
            visible: row.selected
            color: Theme.selectionTint
        }

        // Hover follows the cursor and never touches the keyboard's place.
        // Clicking is what moves focus, and the call site does that.
        HoverHandler {
            id: rowHover
            cursorShape: Qt.PointingHandCursor
        }

        TapHandler {
            id: rowTap

            acceptedButtons: Qt.LeftButton
            gesturePolicy: TapHandler.ReleaseWithinBounds
            // `rowTap.point`, NOT the `eventPoint` the signal carries: the
            // signal's argument is a QEventPoint, which has no modifier state
            // at all, while the handler's own `point` is a handlerPoint and
            // does. That is the only route from a pointer handler to
            // Ctrl+Click, and it is a live probe rather than a reading — see
            // tests/unit/tst_tap_modifiers.cpp, which fails without it.
            onTapped: {
                row.cancelNavigationRestore()
                row.activated(rowTap.point.modifiers)
            }
        }

        TapHandler {
            acceptedButtons: Qt.RightButton
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: eventPoint => {
                row.cancelNavigationRestore()
                const p = surface.mapToItem(null, eventPoint.position.x, eventPoint.position.y)
                row.menuRequested(p.x, p.y)
            }
        }

        // ── Left chain ─────────────────────────────────────────────────────
        Item {
            id: markerSlot

            anchors.left: parent.left
            anchors.leftMargin: row.showPlayingMarker ? Theme.spacingTight : 0
            anchors.verticalCenter: parent.verticalCenter
            width: row.showPlayingMarker ? Theme.scale(3) : 0
            height: parent.height * 0.55
            visible: row.showPlayingMarker

            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: row.playing ? Theme.accentColor : "transparent"
            }
        }

        Item {
            id: numberSlot

            anchors.left: markerSlot.right
            anchors.leftMargin: row.showNumber && row.showPlayingMarker ? Theme.spacingTight : 0
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: row.showNumber ? row.numberColumn : 0
            visible: row.showNumber

            Text {
                id: numberCell

                anchors.fill: parent
                anchors.rightMargin: Theme.spacingTight
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                visible: !row.selected && !(row.hoverPlayGlyph && row.hovered)
                text: row.number > 0 ? String(row.number) : row.numberPlaceholder
                color: row.playing ? Theme.accentColor : Theme.textTertiary
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontSmall
            }

            StrmIcon {
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacingTight
                anchors.verticalCenter: parent.verticalCenter
                visible: !row.selected && row.hoverPlayGlyph && row.hovered
                name: "play"
                size: Theme.scale(16)
                color: Theme.textPrimaryColor
            }

            // A tint alone is not an answer to "is this row in the set" on a
            // sleeve-lit page where every surface is already warm. The tick
            // takes the number's own column, so nothing reflows when a
            // selection is made and the tick lands where the eye already is.
            StrmIcon {
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacingTight
                anchors.verticalCenter: parent.verticalCenter
                visible: row.selected
                name: "check"
                size: Theme.scale(16)
                color: Theme.accentColor
            }
        }

        Rectangle {
            id: coverFrame

            anchors.left: numberSlot.right
            anchors.leftMargin: row.showCover ? Theme.spacingTight : 0
            anchors.verticalCenter: parent.verticalCenter
            width: row.showCover ? row.coverSize : 0
            height: row.showCover ? row.coverSize : 0
            radius: Theme.radiusChip
            color: Theme.surfaceColor
            clip: true
            visible: row.showCover

            // `posterUrl` straight from the model, never a hand-built image
            // URL: MediaItem::coverSource() already resolves an audio track to
            // its album's cover rather than the ripper's embedded art.
            StrmImage {
                anchors.fill: parent
                source: row.coverUrl
            }

            Rectangle {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                width: Theme.scale(16)
                height: Theme.scale(16)
                radius: height / 2
                visible: row.played
                color: Theme.positive

                StrmIcon {
                    anchors.centerIn: parent
                    name: "check"
                    size: Theme.scale(11)
                    color: Theme.accentText
                }
            }
        }

        // ── Right chain ────────────────────────────────────────────────────
        Item {
            id: verbsHolder

            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingTight
            anchors.verticalCenter: parent.verticalCenter
            width: verbRow.implicitWidth
            height: verbRow.implicitHeight
            opacity: row.verbsRevealed ? 1 : 0
            visible: verbsHolder.opacity > 0.01
            // Follows the fade, which is also what keeps a hidden verb out of
            // any traversal at all.
            enabled: row.verbsRevealed

            Behavior on opacity {
                NumberAnimation {
                    duration: Theme.animInstant
                    easing.type: Theme.easeInstant
                }
            }

            Row {
                id: verbRow
                spacing: Theme.scale(2)

                Row {
                    id: extraVerbs
                    spacing: Theme.scale(2)
                }

                StrmIconButton {
                    visible: row.showFavorite
                    iconName: row.favorite ? "heart-filled" : "heart"
                    size: Theme.scale(28)
                    checked: row.favorite
                    activeFocusOnTab: false
                    tooltip: row.favorite ? qsTr("Remove from favourites")
                                          : qsTr("Add to favourites")
                    onClicked: {
                        row.cancelNavigationRestore()
                        row.favoriteToggled()
                    }
                }

                StrmIconButton {
                    id: menuButton

                    visible: row.showMenu
                    iconName: "more-horizontal"
                    size: Theme.scale(28)
                    activeFocusOnTab: false
                    tooltip: qsTr("More…")
                    onClicked: {
                        row.cancelNavigationRestore()
                        const p = menuButton.mapToItem(null, menuButton.width / 2,
                                                       menuButton.height)
                        row.menuRequested(p.x, p.y)
                    }
                }
            }
        }

        Text {
            id: durationCell

            anchors.right: parent.right
            anchors.rightMargin: row.verbsColumn
            anchors.verticalCenter: parent.verticalCenter
            width: row.durationColumn
            horizontalAlignment: Text.AlignRight
            visible: row.durationText.length > 0
            text: row.durationText
            color: row.playing ? Theme.accentColor : Theme.textTertiary
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontSmall
        }

        Text {
            id: artistCell

            anchors.right: durationCell.left
            anchors.rightMargin: Theme.spacingValue
            anchors.verticalCenter: parent.verticalCenter
            width: row.artistColumn
            visible: row.artistColumn > 0
            text: row.artist
            color: Theme.textTertiary
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontSmall
            elide: Text.ElideRight
            maximumLineCount: 1
        }

        // ── The labels ─────────────────────────────────────────────────────
        Column {
            id: labels

            anchors.left: coverFrame.right
            anchors.leftMargin: Theme.spacingValue
            anchors.right: row.artistColumn > 0 ? artistCell.left
                         : durationCell.visible ? durationCell.left
                         : verbsHolder.left
            anchors.rightMargin: Theme.spacingValue
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.scale(2)

            Text {
                width: parent.width
                text: row.title
                color: row.playing ? Theme.accentColor
                     : (row.hovered || row.current) ? Theme.textPrimaryColor
                     : Theme.textSecondaryColor
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontBodySize
                elide: Text.ElideRight
                maximumLineCount: 1

                Behavior on color {
                    ColorAnimation {
                        duration: Theme.animInstant
                        easing.type: Theme.easeInstant
                    }
                }
            }

            Text {
                width: parent.width
                visible: row.secondary.length > 0
                text: row.secondary
                color: Theme.textTertiary
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideRight
                maximumLineCount: 1
            }
        }

        // Inset to zero, not outset: the table clips, and a ring drawn outside
        // the row would be sliced off against the viewport edge on the first
        // and last track.
        FocusRing {
            active: row.current
            radius: Theme.radiusChip
            inset: 0
        }
    }
}
