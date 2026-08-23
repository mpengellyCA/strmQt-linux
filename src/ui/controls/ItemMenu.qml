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

    // Fired after the verb has been handed to Actions, so a page can react
    // (scroll to something, confirm a queue add) without re-deriving the click.
    signal verbTriggered(string verb, var item)

    // ── Item classification ────────────────────────────────────────────────
    // Emby item types, from MediaItemModel's `type` role.
    function typeOf(item) {
        return (item && item.type) ? String(item.type) : ""
    }

    // Containers are played as a set: they have no playable stream of their
    // own, so they get Play all / Shuffle where a movie gets Play. A MusicAlbum
    // is one of these and used not to be — asking Emby for PlaybackInfo on an
    // album is an HTTP 500, so "Play" on an album row did nothing but log.
    function isContainer(item) {
        const t = root.typeOf(item)
        return t === "Series" || t === "Season" || t === "BoxSet" || t === "MusicAlbum"
    }

    // Music item kinds, which differ from video in three places: what "played"
    // is called, where the play verbs point, and whether "Details" — the video
    // details page — is a destination that makes any sense.
    function isMusic(item) {
        const t = root.typeOf(item)
        return t === "Audio" || t === "MusicAlbum" || t === "MusicArtist"
    }

    // What Actions.playAll()/shuffle() need to know to pick the child item
    // types for the query. Series/Season yield episodes; a BoxSet yields
    // whatever the collection holds; a MusicAlbum yields its tracks, and
    // "music" is what makes the query ask for Audio rather than for the
    // {Movie, Episode, Video} an unknown kind falls back to — which matches
    // nothing at all on a music album.
    function collectionTypeFor(item) {
        const t = root.typeOf(item)
        if (t === "Series" || t === "Season")
            return "tvshows"
        if (t === "BoxSet")
            return "boxsets"
        if (t === "MusicAlbum")
            return "music"
        return ""
    }

    // ── Music targets ──────────────────────────────────────────────────────
    // Both return a synthetic item map — {itemId, name, type} — rather than a
    // pair of strings, because that is exactly what Actions.openDetails() takes
    // and what the shell routes on. Building the map here means the trigger
    // handler and the row-should-exist test cannot disagree about whether the
    // destination is reachable: no map, no row.

    // A track's album. Albums are the only music container an id is carried
    // for on the item itself (`albumId`), so this needs no lookup.
    function albumTargetOf(item) {
        if (!item || root.typeOf(item) !== "Audio")
            return null
        const id = (item.albumId !== undefined && item.albumId !== null)
                   ? String(item.albumId) : ""
        if (id.length === 0)
            return null
        const name = (item.album !== undefined && item.album !== null)
                     ? String(item.album) : ""
        return { itemId: id, name: name, type: "MusicAlbum" }
    }

    // A track's or an album's artist, by id — `artists` alone is a list of
    // names and a name is not navigable. The two roles run in the same order
    // (MediaItemModel keeps them parallel from ArtistItems), so the index found
    // in one indexes the other.
    //
    // The ALBUM artist is preferred over the first performer when both are
    // present: an artist page is a discography, and a discography is filed
    // under AlbumArtistIds. Landing a compilation track's "Go to artist" on the
    // guest vocalist would open a page with no albums on it.
    function artistTargetOf(item) {
        if (!item)
            return null
        const type = root.typeOf(item)
        if (type !== "Audio" && type !== "MusicAlbum")
            return null
        const ids = item.artistIds
        if (!ids || ids.length === 0)
            return null
        const names = item.artists
        const albumArtist = (item.albumArtist !== undefined && item.albumArtist !== null)
                            ? String(item.albumArtist) : ""
        var index = 0
        if (albumArtist.length > 0 && names) {
            for (var i = 0; i < names.length && i < ids.length; ++i) {
                if (String(names[i]) === albumArtist) {
                    index = i
                    break
                }
            }
        }
        const id = String(ids[index])
        if (id.length === 0)
            return null
        var name = (names && index < names.length) ? String(names[index]) : ""
        if (name.length === 0)
            name = albumArtist
        return { itemId: id, name: name, type: "MusicArtist" }
    }

    // ── Building the list ──────────────────────────────────────────────────
    // Actions and verbs are appended in lockstep; `sep()` refuses to open the
    // list with a separator or to emit two in a row, so a shape that happens to
    // omit a whole group (a movie has no "Go to series") cannot leave a stray
    // rule behind.
    function buildFor(item) {
        if (!item || !item.itemId)
            return false

        const id = String(item.itemId)
        const type = root.typeOf(item)
        const played = Actions.isPlayed(id)
        const favorite = Actions.isFavorite(id)
        const acts = []
        const vs = []

        function push(action, verb) {
            acts.push(action)
            vs.push(verb)
        }
        function sep() {
            if (acts.length === 0 || acts[acts.length - 1].separator === true)
                return
            acts.push({ separator: true })
            vs.push("")
        }

        const music = root.isMusic(item)
        const isArtist = type === "MusicArtist"

        if (isArtist) {
            // No play verbs, deliberately. Every one of them addresses a parent
            // by ParentId, and an artist is not a folder: Emby files a track
            // under its album, and an artist is reached through ArtistIds /
            // AlbumArtistIds instead. Until there is an artist-scoped query,
            // "Play artist" would be a row that logs and does nothing —
            // reported rather than shipped. Favourite and navigation below are
            // the verbs an artist row can honestly carry out.
        } else if (root.isContainer(item)) {
            push({ text: type === "MusicAlbum" ? qsTr("Play album") : qsTr("Play all"),
                   iconName: "play" }, "playAll")
            push({ text: type === "MusicAlbum" ? qsTr("Shuffle album") : qsTr("Shuffle"),
                   iconName: "shuffle" }, "shuffle")
            if (type === "Series") {
                sep()
                push({ text: qsTr("Episodes"), iconName: "list" }, "series")
            }
        } else if (item.resumable === true) {
            push({ text: qsTr("Resume"), iconName: "play" }, "resume")
            push({ text: qsTr("Play from start"), iconName: "skip-previous" }, "playFromStart")
        } else {
            push({ text: qsTr("Play"), iconName: "play" }, "play")
        }

        // Queue verbs are offered for playable items only: Actions.playNext /
        // addToQueue take one item, and "add a whole series after the current
        // episode" is Play all's job, not theirs. An artist is neither — it has
        // no stream of its own and no query to expand it into one.
        if (!root.isContainer(item) && !isArtist) {
            sep()
            push({ text: qsTr("Play next"), iconName: "skip-next" }, "playNext")
            push({ text: qsTr("Add to queue"), iconName: "queue" }, "addToQueue")
        }

        sep()
        // Music is *played*, not watched, and an artist is neither: Emby keeps
        // a play count on a track and on an album, and marking a whole artist
        // played is not a thing anyone means by right-clicking one.
        if (!isArtist) {
            push({ text: music ? (played ? qsTr("Mark unplayed") : qsTr("Mark played"))
                               : (played ? qsTr("Mark unwatched") : qsTr("Mark watched")),
                   iconName: played ? "eye-off" : "check", checked: played }, "played")
        }
        push({ text: favorite ? qsTr("Remove from favourites") : qsTr("Add to favourites"),
               iconName: favorite ? "heart-filled" : "heart", checked: favorite }, "favorite")

        // Playlist membership. "Remove" is offered only when this row actually
        // IS a playlist entry: the id is what the verb addresses, so an item
        // that arrived from anywhere else has nothing to remove.
        const entryId = (item.playlistItemId !== undefined && item.playlistItemId !== null)
                        ? String(item.playlistItemId) : ""
        if (root.allowAddToPlaylist || (root.allowRemoveFromPlaylist && entryId.length > 0))
            sep()
        if (root.allowAddToPlaylist)
            push({ text: qsTr("Add to playlist"), iconName: "playlist" }, "addToPlaylist")
        if (root.allowRemoveFromPlaylist && entryId.length > 0) {
            push({ text: qsTr("Remove from this playlist"), iconName: "trash",
                   destructive: true }, "removeFromPlaylist")
        }

        sep()
        if (type === "Episode")
            push({ text: qsTr("Go to series"), iconName: "library" }, "series")
        // The music equivalents of "Go to series". Both are built from the item
        // itself, so a track whose server record carries no AlbumId — or an
        // album the query did not ask ArtistItems for — simply has no row,
        // rather than one that opens an empty page.
        if (root.allowMusicNavigation) {
            if (root.albumTargetOf(item))
                push({ text: qsTr("Go to album"), iconName: "lib-music" }, "goToAlbum")
            if (root.artistTargetOf(item))
                push({ text: qsTr("Go to artist"), iconName: "user" }, "goToArtist")
        }
        // "Details" is the *video* details page — a backdrop, a cast list and a
        // runtime. A track and an album have their own destinations above, and
        // for an artist the page the user wants is the one they are being
        // offered, so music never grows this row.
        if (root.allowDetails && !music)
            push({ text: qsTr("Details"), iconName: "info" }, "details")
        push({ text: qsTr("Refresh metadata"), iconName: "refresh" }, "refresh")

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
        const id = String(item.itemId)

        switch (verb) {
        case "play":          Actions.play(item); break
        case "resume":        Actions.resume(item); break
        case "playFromStart": Actions.playFromStart(item); break
        case "playNext":      Actions.playNext(item); break
        case "addToQueue":    Actions.addToQueue(item); break
        case "playAll":       Actions.playAll(id, root.collectionTypeFor(item)); break
        case "shuffle":
            // A series has a dedicated verb because shuffling one means
            // shuffling its episodes across every season, which a parentId
            // query for a folder does not give you.
            if (root.typeOf(item) === "Series")
                Actions.shuffleSeries(id)
            else
                Actions.shuffle(id, root.collectionTypeFor(item))
            break
        // The setters, not the toggles: the value shown in the row is the one
        // being inverted, so a menu built from a stale read cannot double-flip.
        case "played":        Actions.setPlayed(id, !Actions.isPlayed(id)); break
        case "favorite":      Actions.setFavorite(id, !Actions.isFavorite(id)); break
        // The two verbs Actions cannot carry out: the page that opted in owns
        // the surface they need, so the menu only reports what was chosen.
        case "addToPlaylist":      root.addToPlaylistRequested(item); break
        case "removeFromPlaylist": root.removeFromPlaylistRequested(item); break
        case "series":        Actions.openSeries(item); break
        // Navigation for music goes through the same verb the cards use, with a
        // synthetic item map. `type` is what the shell routes on, and
        // ItemActions.resolve() passes a caller's map through untouched, so the
        // album/artist page is reached without a second navigation contract and
        // without C++ learning two more verbs.
        case "goToAlbum": {
            const album = root.albumTargetOf(item)
            if (!album)
                return
            Actions.openDetails(album)
            break
        }
        case "goToArtist": {
            const artist = root.artistTargetOf(item)
            if (!artist)
                return
            Actions.openDetails(artist)
            break
        }
        case "details":       Actions.openDetails(item); break
        case "refresh":       Actions.refreshMetadata(id); break
        default: return
        }
        root.verbTriggered(verb, item)
    }
}
