import QtQuick
import StrmQt

// ItemMenu — THE item context menu (ARCHITECTURE.md).
//
// Home, Library, Search, Details and Series each used to hand-build the same
// ~40-line action list, which is exactly how five menus drift apart: the queue
// verbs would have had to be added five times, and "Play from start" already
// read differently on Details than it did on Home. This is that list, once.
//
// Usage is a single declaration plus a one-line handler:
//
//   ItemMenu { id: itemMenu }
//   ...
//   onMenuRequested: (index, mx, my) => itemMenu.popupForItem(model.get(index), mx, my)
//
// `sceneX`/`sceneY` are SCENE coordinates — what StrmCard.menuRequested and
// StrmRail/StrmGrid already emit — because StrmMenu.popupAt() flips rather than
// clamps against the window, and only scene coordinates let it.
//
// The action list is derived from the item, so the menu is never a superset
// with half its rows disabled: an episode gets "Go to series" and a movie does
// not, a resumable item gets "Play from start", a series or a collection gets
// "Play all" and "Shuffle" instead of "Play", a track gets "Go to album" where
// an episode gets "Go to series", and watched/favourite read their
// current value through Actions.isPlayed / isFavorite rather than from a stale
// copy in the item map.
//
// `verbs` runs parallel to `actions` and the trigger handler indexes it, so the
// menu NEVER recovers what was clicked by matching translated label text — the
// technique the pages already used, kept.
//
// Keyboard navigation, Esc, hover-previews-without-committing and the overlay
// layering are all inherited from StrmMenu unchanged.
StrmMenu {
    id: root

    // ── State ──────────────────────────────────────────────────────────────
    // The item the open menu acts on. Set by popupForItem(); pages never
    // assign it.
    property var target: ({})
    // Parallel to `actions`; "" on separators.
    property var verbs: []

    // The details page opens this menu for the item it is already showing,
    // where a "Details" row would navigate to where the user already is.
    property bool allowDetails: true

    // ── Playlist verbs (ARCHITECTURE.md) ─────────────────────────────────
    // Opt-in, and false by default, because unlike every other row in this menu
    // these two are not verbs `Actions` can carry out on its own: both need a
    // surface the *page* owns — a picker over the user's playlists, or the open
    // playlist itself. A page that cannot show that surface must not offer the
    // row, so the menu never grows an entry that does nothing when clicked.
    property bool allowAddToPlaylist: false
    property bool allowRemoveFromPlaylist: false

    signal addToPlaylistRequested(var item)
    // Carries the whole item, not the entry id: the receiving page is the one
    // that knows which playlist it is looking at, and it re-reads
    // `playlistItemId` (the ENTRY, never the item id — the same track may sit in
    // one playlist twice) from the map it is handed.
    signal removeFromPlaylistRequested(var item)

    // ── Music navigation (ARCHITECTURE.md) ────────────────────────────────────
    // "Go to album" and "Go to artist" — the two verbs a music collection is
    // actually browsed by, and the reason a track in a search result is not a
    // dead end. Opt-in and false by default for the same reason the playlist
    // rows are: these navigate to the album and artist pages, and a page whose
    // shell cannot reach those pages must not offer a row that lands nowhere.
    //
    // The rows are ALSO type-gated — only Audio and MusicAlbum ever grow them —
    // so Home, Library and Series would not see them even set true. The flag is
    // about the destination existing; the type test is about the row making
    // sense for the item.
    property bool allowMusicNavigation: false

    // A presentation profile changes which centrally-owned descriptors are
    // rendered and in what order. The default is the cross-application menu;
    // musicBrowse is the intentionally narrower album/artist/playlist grid.
    property string profile: ""

    // Fired after the verb has been handed to Actions, so a page can react
    // (scroll to something, confirm a queue add) without re-deriving the click.
    signal verbTriggered(string verb, var item)

    // ── Presentation ───────────────────────────────────────────────────────
    // C++ returns stable semantic presentation keys. Translation and icon
    // choice remain visual concerns, so no localized string crosses the API.
    function presentationFor(key) {
        switch (key) {
        case "play":               return { text: qsTr("Play"), icon: "play" }
        case "resume":             return { text: qsTr("Resume"), icon: "play" }
        case "playFromStart":      return { text: qsTr("Play from start"), icon: "skip-previous" }
        case "playAll":            return { text: qsTr("Play all"), icon: "play" }
        case "playAlbum":          return { text: qsTr("Play album"), icon: "play" }
        case "shuffle":            return { text: qsTr("Shuffle"), icon: "shuffle" }
        case "shuffleAlbum":       return { text: qsTr("Shuffle album"), icon: "shuffle" }
        case "instantMix":         return { text: qsTr("Instant mix"), icon: "shuffle" }
        case "episodes":           return { text: qsTr("Episodes"), icon: "list" }
        case "playNext":           return { text: qsTr("Play next"), icon: "skip-next" }
        case "addToQueue":         return { text: qsTr("Add to queue"), icon: "queue" }
        case "markPlayed":         return { text: qsTr("Mark played"), icon: "check" }
        case "markUnplayed":       return { text: qsTr("Mark unplayed"), icon: "eye-off" }
        case "markWatched":        return { text: qsTr("Mark watched"), icon: "check" }
        case "markUnwatched":      return { text: qsTr("Mark unwatched"), icon: "eye-off" }
        case "addFavorite":        return { text: qsTr("Add to favourites"), icon: "heart" }
        case "removeFavorite":     return { text: qsTr("Remove from favourites"), icon: "heart-filled" }
        case "addToPlaylist":      return { text: qsTr("Add to playlist"), icon: "playlist" }
        case "removeFromPlaylist": return { text: qsTr("Remove from this playlist"), icon: "trash" }
        case "goToSeries":         return { text: qsTr("Go to series"), icon: "library" }
        case "goToAlbum":          return { text: qsTr("Go to album"), icon: "lib-music" }
        case "goToArtist":         return { text: qsTr("Go to artist"), icon: "user" }
        case "openAlbum":          return { text: qsTr("Open album"), icon: "lib-music" }
        case "openArtist":         return { text: qsTr("Open artist"), icon: "user" }
        case "openPlaylist":       return { text: qsTr("Open playlist"), icon: "playlist" }
        case "details":            return { text: qsTr("Details"), icon: "info" }
        case "refreshMetadata":    return { text: qsTr("Refresh metadata"), icon: "refresh" }
        default:                    return { text: "", icon: "" }
        }
    }

    // ── Building the list ──────────────────────────────────────────────────
    function buildFor(item) {
        if (!item || !item.itemId)
            return false
        const policy = Actions.itemMenuPolicy(item, root.allowDetails,
                                              root.allowAddToPlaylist,
                                              root.allowRemoveFromPlaylist,
                                              root.allowMusicNavigation,
                                              root.profile)
        if (!policy || policy.length === 0)
            return false
        const acts = []
        const vs = []

        for (let i = 0; i < policy.length; ++i) {
            const descriptor = policy[i]
            if (descriptor.separator === true) {
                acts.push({ separator: true })
                vs.push("")
                continue
            }
            const presentation = root.presentationFor(String(descriptor.presentation || ""))
            if (presentation.text.length === 0)
                continue
            acts.push({ text: presentation.text,
                        iconName: presentation.icon,
                        checked: descriptor.checked === true,
                        destructive: descriptor.destructive === true })
            vs.push(String(descriptor.verb || ""))
        }

        root.target = item
        root.verbs = vs
        root.actions = acts
        return true
    }

    // ── Opening ────────────────────────────────────────────────────────────
    function popupForItem(item, sceneX, sceneY) {
        if (!root.buildFor(item))
            return
        root.popupAt(sceneX, sceneY)
    }

    // Same, for the one caller that must suppress "Details" (the details page's
    // own item). Kept as a second entry point rather than a fourth positional
    // argument so the common call stays three arguments long.
    function popupForItemNoDetails(item, sceneX, sceneY) {
        const previous = root.allowDetails
        root.allowDetails = false
        root.popupForItem(item, sceneX, sceneY)
        root.allowDetails = previous
    }

    // ── Running ────────────────────────────────────────────────────────────
    onTriggered: index => {
        const item = root.target
        const verb = (index >= 0 && index < root.verbs.length) ? root.verbs[index] : ""
        if (!verb || !item || !item.itemId)
            return
        if (verb === "addToPlaylist")
            root.addToPlaylistRequested(item)
        else if (verb === "removeFromPlaylist")
            root.removeFromPlaylistRequested(item)
        else
            Actions.performItemVerb(verb, item)
        root.verbTriggered(verb, item)
    }
}
