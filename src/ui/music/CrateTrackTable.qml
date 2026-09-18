pragma ComponentBehavior: Bound
import QtQuick
import StrmQt

// CrateTrackTable — TrackTable with the Crate row built in (spec §6.1–6.3).
//
// Everything a Crate row says comes from TrackListModel roles or from the
// controller: the display title with "feat." split off, the artist only where
// it differs from the album credit, disc sides placed at `discs[i].firstRow`.
// The table's own disc and artist walks are off, because the controller has
// already decided both.
//
// Configure this through its properties and NEVER set `delegate` on it: the
// Crate row below is the whole point of the type, and a delegate declared at
// the use site replaces it silently — the result is a plain TrackTable
// carrying a set of properties nothing reads any more.
TrackTable {
    id: crate

    // AlbumCtl.discs: [{number, title, detail, firstRow}]. Empty draws no
    // headings.
    property var discs: []
    property bool artistColumnShown: false
    property bool showCovers: false
    // One second line per row (ArtistCtl.topTrackCaptions). Empty for none.
    property var captions: []
    // Number rows 1…N (a chart) instead of by track number (a record).
    property bool numberFromIndex: false

    // ── Column metrics, shared by every row ────────────────────────────────
    property int numberColumn: Theme.scale(46)
    property int durationColumn: Theme.scale(64)
    property int verbsColumn: Theme.scale(72)
    property int artistColumnWidth: Theme.scale(200)
    property int discHeaderHeight: Theme.scale(46)

    readonly property var discStarts: {
        const starts = {}
        const list = crate.discs ? crate.discs : []
        for (let i = 0; i < list.length; ++i)
            starts[list[i].firstRow] = list[i]
        return starts
    }

    // Reading queue.currentIndex makes this re-evaluate; currentItem() alone
    // would never notify.
    readonly property string nowPlayingId: {
        const queue = PlayerCtl.queue
        if (!queue || queue.currentIndex < 0)
            return ""
        const current = queue.currentItem()
        return (current && current.itemId !== undefined) ? String(current.itemId) : ""
    }

    discGrouping: false
    artistRule: false
    multiSelect: true
    jumpRole: "name"
    rowHeight: crate.showCovers ? Theme.scale(52) : Theme.scale(38)

    delegate: TrackRow {
        id: trackRow

        required property int index
        required property var model

        readonly property string trackId: trackRow.model.itemId !== undefined
                                          ? String(trackRow.model.itemId) : ""
        readonly property var disc: crate.discStarts[trackRow.index]

        width: crate.width
        navigationFocusOwner: crate

        rowHeight: crate.rowHeight
        discHeaderHeight: crate.discHeaderHeight
        numberColumn: crate.numberColumn
        durationColumn: crate.durationColumn
        verbsColumn: crate.verbsColumn
        artistColumn: crate.artistColumnShown ? crate.artistColumnWidth : 0
        coverSize: Theme.scale(40)

        title: {
            const shown = trackRow.model.displayTitle
            return shown !== undefined && String(shown).length > 0
                   ? String(shown)
                   : (trackRow.model.name !== undefined ? String(trackRow.model.name) : "")
        }
        titleSuffix: trackRow.model.featuredText !== undefined ? String(trackRow.model.featuredText) : ""
        secondary: crate.captions && trackRow.index < crate.captions.length
                   ? String(crate.captions[trackRow.index]) : ""
        artist: crate.artistColumnShown && trackRow.model.differsFromAlbumArtist === true
                ? String(trackRow.model.artistText) : ""
        durationText: trackRow.model.durationText !== undefined ? String(trackRow.model.durationText) : ""
        number: crate.numberFromIndex
                ? trackRow.index + 1
                : (trackRow.model.trackNumber !== undefined ? Number(trackRow.model.trackNumber) : -1)
        discNumber: trackRow.disc ? Number(trackRow.disc.number) : -1
        discTitle: trackRow.disc ? String(trackRow.disc.title) : ""
        discDetail: trackRow.disc ? String(trackRow.disc.detail) : ""
        showCover: crate.showCovers
        coverUrl: trackRow.model.coverUrl !== undefined ? String(trackRow.model.coverUrl) : ""

        current: crate.currentIndex === trackRow.index && crate.activeFocus
        selected: crate.isSelected(trackRow.index)
        playing: trackRow.trackId.length > 0 && trackRow.trackId === crate.nowPlayingId
        favorite: trackRow.model.favourite === true
        showFavorite: true
        showMenu: true
        verbsRevealed: trackRow.hovered || trackRow.favorite

        onActivated: modifiers => {
            crate.forceActiveFocus(Qt.MouseFocusReason)
            crate.activateAt(trackRow.index, modifiers)
        }
        onFavoriteToggled: {
            const item = crate.rowAt(trackRow.index)
            if (item)
                Actions.toggleFavorite(item)
        }
        onMenuRequested: (sceneX, sceneY) => crate.menuRequested(trackRow.index, sceneX, sceneY)
    }
}
