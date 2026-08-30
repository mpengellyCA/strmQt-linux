pragma Singleton
import QtQuick

// Vocabulary shared by anything that has to render an Emby *kind* — a library's
// CollectionType, or an item's Type. Kept in one place because the nav rail, the
// page header and the empty states all had to answer the same question and would
// otherwise each grow their own half-complete `if` chain.
//
// Not every library has a collection type: a mixed-content library (this server
// has one, "Fan Cuts") reports an empty string, so every lookup falls back to a
// neutral folder rather than assuming a type is always present.
QtObject {
    // CollectionType → icon name in qrc:/icons.
    readonly property var _libraryIcons: ({
        "movies":      "lib-movies",
        "tvshows":     "lib-tvshows",
        "boxsets":     "lib-collections",
        "music":       "lib-music",
        "musicvideos": "lib-musicvideos",
        "homevideos":  "lib-homevideos",
        "photos":      "lib-photos",
        "books":       "lib-books",
        "livetv":      "lib-livetv",
        "playlists":   "playlist",
        "folders":     "lib-folder"
    })

    // Icon for a library of this collection type. Unknown or absent → folder.
    function libraryIcon(collectionType) {
        if (!collectionType)
            return "lib-folder";
        const key = String(collectionType).toLowerCase();
        return _libraryIcons[key] !== undefined ? _libraryIcons[key] : "lib-folder";
    }

    // Human-readable name for a collection type, for headers and empty states.
    readonly property var _libraryLabels: ({
        "movies":      qsTr("Movies"),
        "tvshows":     qsTr("TV shows"),
        "boxsets":     qsTr("Collections"),
        "music":       qsTr("Music"),
        "musicvideos": qsTr("Music videos"),
        "homevideos":  qsTr("Home videos"),
        "photos":      qsTr("Photos"),
        "books":       qsTr("Books"),
        "livetv":      qsTr("Live TV"),
        "playlists":   qsTr("Playlists")
    })

    function libraryLabel(collectionType) {
        if (!collectionType)
            return qsTr("Media");
        const key = String(collectionType).toLowerCase();
        return _libraryLabels[key] !== undefined ? _libraryLabels[key] : qsTr("Media");
    }

    // "S2E5" for an episode's season and episode numbers; "" when either is
    // absent or negative, because an episode the server has not numbered yet
    // has no code to show. MediaItemModel renders the same code in C++ (its
    // file-local episodeCode()); the two must not drift apart.
    function episodeCode(season, episode) {
        const s = Number(season);
        const e = Number(episode);
        if (!isFinite(s) || !isFinite(e) || s < 0 || e < 0)
            return "";
        return "S" + s + "E" + e;
    }
}
