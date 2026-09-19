#include <QDir>
#include <QFile>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QPointer>
#include <QTest>

#include <algorithm>
#include <memory>

class NavigationHistoryTest : public QObject
{
    Q_OBJECT

private slots:
    void capsGraphsAndReconstructsMetadata();
    void forwardWalkBoundsLivePagesNotRoutes();
    void trimEvictsTheOldestLivePageAndNotTheCurrentOne();
    void backBeyondTheLiveWindowRebuildsAndRestoresFocus();
    void restoresForwardFocusAndReplacesBranches();
    void restoresPerEntrySearchAndPreparesRouteKinds();
    void searchEscapeClearsQueryThenGoesBackThroughTransaction();
    void restoresPerEntryMusicBrowseState();
    void reconstructsMusicHomeAfterEviction();
    void restoresPerEntrySeriesSeasonAndAcceptsLaterSelection();
    void productionRetargetOrderingRetainsDepartingScopes();
    void itemPolicyIsCentralizedAcrossQmlSurfaces();
    void musicBrowsePageInstantiatesOnlyTheActiveSection();
    void musicInteractionContextNamesPagesThatExist();
    void searchTrackOwnerRestoresAcrossResultLifecycle();
    void restoresVirtualFocusAcrossDelayedRefill();
    void pendingBackRestoreHonorsUserOverride();
    void progressExtendsRefillWithoutAdmittingStaleRows();
    void stableOwnersAndPlaylistIdentitySurviveReorder();
    void terminalFallbackAndUserOverride();
    void pendingRestoreIsNotResurrected();
    void preservesFavoriteStateAcrossReconstruction();
    void preservesBaseAndKeepsTransientPagesOutOfHistory();
    void audioPlaylistRouteSelectsTheMusicPlaylistPage();
    void playlistRouteFollowsTheSurfaceItWasOpenedFrom();
    void evictedAudioPlaylistIsRebuiltFromItsRoute();
    void artistEntryRetainsTheLibraryItWasOpenedUnder();
};

namespace {

const char *kProbe = R"QML(
import QtQuick
import QtQuick.Controls.Basic
import StrmQt
import "."

Item {
    id: root
    width: 480
    height: 320
    focus: true

    property int createdCount: 0
    property int destroyedCount: 0
    property var destroyedIds: []
    property var preparedRoutes: []
    property string preparedDetailsId: ""
    property string preparedAlbumId: ""
    property string searchQuery: ""
    property string musicBrowseSection: "albums"
    property string musicBrowseState: ""
    property string seriesSeasonId: ""
    property string preparedSeriesSeasonId: ""
    property int refillBatch: 0
    property int virtualNearEndCount: 0
    property bool virtualRefillActive: false
    property bool twinSwapped: false
    property bool delayTwinOwners: false
    readonly property int virtualCount: virtualRows.count
    property bool progressRefillActive: false
    property int progressFocusedIndex: -1
    property int progressFallbackCount: 0
    readonly property bool progressRestorePending: progressRestorer.pending
    property bool searchTrackRefillActive: false
    property bool searchTrackOwnerEnabled: true
    property int searchTrackResolutionCount: 0
    readonly property int searchTrackCount: searchTrackRows.count

    function itemFor(id): var {
        const text = String(id);
        return {
            "itemId": text,
            "name": "Item " + text,
            "type": "Movie",
            "posterUrl": "poster://" + text,
            "backdropUrl": "backdrop://" + text,
            "overview": "Overview " + text,
            "year": 2000 + Number(id),
            "officialRating": "PG-" + text,
            "communityRating": 8.25,
            "resumable": true,
            "positionMs": 1234,
            "runtimeMs": 5678,
            "seriesName": "Series " + text,
            "parentIndexNumber": 2,
            "indexNumber": 3,
            "albumArtist": "Artist " + text,
            "artistIds": ["artist-" + text, "guest-" + text],
            "childCount": 12,
            "favorite": true
        };
    }

    function routeFor(kind, item): var {
        const prefix = kind === "album" ? "album:"
                     : kind === "artist" ? "artist:" : "details:";
        return {
            "kind": kind,
            "id": item.itemId,
            "name": item.name,
            "itemType": kind === "album" ? "MusicAlbum"
                        : kind === "artist" ? "MusicArtist" : item.type,
            "key": prefix + item.itemId,
            "title": "Title " + item.itemId,
            // Neither field is part of the retained descriptor whitelist.
            "arbitraryMap": { "large": "not history" },
            "prepare": () => root.preparedRoutes.push("closure")
        };
    }

    function pushRoute(id): void {
        const item = root.itemFor(id);
        history.pushRoute(root.routeFor("details", item), { "item": item });
    }

    function pushAlbum(id): void {
        const item = root.itemFor(id);
        item.type = "MusicAlbum";
        history.pushRoute(root.routeFor("album", item), { "albumItem": item });
    }

    function pushAudioPlaylist(id): void {
        const text = String(id);
        history.pushRoute({ "kind": "playlist", "mode": "audio", "id": text,
                            "name": "Playlist " + text, "key": "musicPlaylist:" + text,
                            "title": "Playlist " + text },
                          { "playlistId": text, "playlistName": "Playlist " + text });
    }

    function pushVideoPlaylist(id): void {
        const text = String(id);
        history.pushRoute({ "kind": "playlist", "id": text, "name": "Playlist " + text,
                            "key": "playlists", "title": "Playlists" });
    }

    function pushArtistInLibrary(id, libraryId): void {
        const item = root.itemFor(id);
        item.type = "MusicArtist";
        const route = root.routeFor("artist", item);
        route.libraryId = String(libraryId);
        history.pushRoute(route, { "artistItem": item, "libraryId": String(libraryId) });
    }

    function pushAlbumUnfavorite(id): void {
        const item = root.itemFor(id);
        item.type = "MusicAlbum";
        item.favorite = false;
        history.pushRoute(root.routeFor("album", item), { "albumItem": item });
    }

    function pushArtistUnfavorite(id): void {
        const item = root.itemFor(id);
        item.type = "MusicArtist";
        item.favorite = false;
        history.pushRoute(root.routeFor("artist", item), { "artistItem": item });
    }

    function markFavorite(id): void { history.updateFavorite(String(id), true); }

    function pushVirtual(id): void {
        history.pushRoute({ "kind": "library", "id": String(id),
                            "name": "Virtual " + id, "key": "virtual:" + id,
                            "title": "Virtual " + id });
    }

    function focusVirtual(index): void {
        if (history.currentItem && history.currentItem.focusRow)
            history.currentItem.focusRow(Number(index));
    }

    function restoreMissingVirtual(index): void {
        if (history.currentItem && history.currentItem.restoreMissing)
            history.currentItem.restoreMissing(Number(index));
    }

    function clearVirtualWithoutRefill(): void {
        refillTimer.stop();
        root.virtualRefillActive = false;
        virtualRows.clear();
    }

    function appendVirtualRows(count): void {
        for (let i = 0; i < Number(count); ++i)
            virtualRows.append({ "itemId": "fallback-" + i, "name": "Fallback " + i });
    }

    function appendLateVirtual(): void {
        virtualRows.append({ "itemId": "late-row", "name": "Late row" });
    }

    function beginProgressRestore(): void {
        progressRows.clear();
        progressRows.append({ "itemId": "target", "name": "Retained target" });
        root.progressFocusedIndex = -1;
        root.progressFallbackCount = 0;
        root.progressRefillActive = true;
        progressRestorer.restore("i:target", 0);
    }

    function appendProgressRow(id): void {
        progressRows.append({ "itemId": String(id), "name": "Progress " + String(id) });
    }

    function replaceProgressRows(): void {
        progressRows.clear();
        progressRows.append({ "itemId": "other", "name": "Other" });
        progressRows.append({ "itemId": "target", "name": "Fresh target" });
    }

    function finishProgressRestore(): void { root.progressRefillActive = false; }

    function focusVirtualOverride(): void {
        if (history.currentItem && history.currentItem.focusOverride)
            history.currentItem.focusOverride();
    }

    function focusVirtualSameOwnerAction(): void {
        if (history.currentItem && history.currentItem.focusSameOwnerAction)
            history.currentItem.focusSameOwnerAction();
    }

    function pushTwin(): void {
        history.pushRoute({ "kind": "person", "id": "twins", "name": "Twins",
                            "key": "person:twins", "title": "Twins" });
    }

    function focusTwinDuplicate(): void {
        if (history.currentItem && history.currentItem.focusDuplicate)
            history.currentItem.focusDuplicate();
    }

    function swapTwinOwnersAndRows(): void {
        root.twinSwapped = true;
        root.delayTwinOwners = true;
        duplicateRows.clear();
        duplicateRows.append({ "playlistItemId": "entry-b", "itemId": "same-media",
                               "name": "Second occurrence" });
        duplicateRows.append({ "playlistItemId": "entry-a", "itemId": "same-media",
                               "name": "First occurrence" });
    }

    function refillVirtualRows(): void {
        virtualRows.clear();
        root.refillBatch = 0;
        root.virtualRefillActive = true;
        refillTimer.restart();
    }

    function appendVirtualBatch(): void {
        const start = root.refillBatch * 6;
        const end = Math.min(30, start + 6);
        for (let i = start; i < end; ++i)
            virtualRows.append({ "itemId": "row-" + i, "name": "Row " + i });
        ++root.refillBatch;
        if (end >= 30) {
            refillTimer.stop();
            root.virtualRefillActive = false;
        }
    }

    function resetBase(kind): void {
        history.resetToRoute({ "kind": String(kind), "key": String(kind),
                               "title": String(kind) });
    }

    function pushTransient(): void {
        history.push(transientComponent, {}, StackView.Immediate);
    }
    function popTransient(): void { history.pop(StackView.Immediate); }

    function pushSearch(query): void {
        root.searchQuery = String(query);
        history.pushRoute({
            "kind": "search", "query": root.searchQuery,
            "key": "search", "title": "Search"
        });
    }

    function setSearchQuery(query): void { root.searchQuery = String(query); }
    function seedSearchTracks(): void {
        searchTrackRows.clear();
        searchTrackRows.append({ "itemId": "track-a", "name": "Track A" });
        searchTrackRows.append({ "itemId": "track-b", "name": "Track B" });
        searchTrackRows.append({ "itemId": "track-c", "name": "Track C" });
    }
    function replaceSearchTracksReordered(): void {
        searchTrackRows.clear();
        searchTrackRows.append({ "itemId": "track-b", "name": "Track B fresh" });
        searchTrackRows.append({ "itemId": "track-c", "name": "Track C fresh" });
        searchTrackRows.append({ "itemId": "track-a", "name": "Track A fresh" });
    }
    function clearSearchTracks(): void { searchTrackRows.clear(); }
    function setSearchTrackRefill(active): void { root.searchTrackRefillActive = active === true; }
    function setSearchTrackOwnerEnabled(active): void {
        root.searchTrackOwnerEnabled = active === true;
    }
    function applySearchQueryFromUser(query): void {
        if (history.currentItem && history.currentItem.applyUserQuery)
            history.currentItem.applyUserQuery(String(query));
    }
    function focusSearchTrack(index): void {
        if (history.currentItem && history.currentItem.focusTrack)
            history.currentItem.focusTrack(Number(index));
    }
    function focusSearchTrackOverride(): void {
        if (history.currentItem && history.currentItem.focusOverride)
            history.currentItem.focusOverride();
    }
    function pushMusicBrowse(section): void {
        root.musicBrowseSection = String(section);
        root.musicBrowseState = "";
        history.pushRoute({ "kind": "musicBrowse", "id": "music-1", "name": "Music",
                            "key": "musicBrowse:music-1", "title": "Music",
                            "tab": root.musicBrowseSection, "query": root.musicBrowseState });
    }
    function setMusicBrowseSection(section): void {
        root.musicBrowseSection = String(section);
        if (history.currentItem && history.currentItem.selectedSection !== undefined)
            history.currentItem.selectedSection = root.musicBrowseSection;
    }
    function setMusicBrowseState(state): void {
        root.musicBrowseState = String(state);
    }
    // The production Main.qml transaction: capture A, retarget the shared
    // controller to Albums in B, then construct B without re-snapshotting A.
    function openMusicBrowseFromMain(libraryId): void {
        history.rememberFocus();
        root.musicBrowseSection = "albums";
        root.musicBrowseState = "";
        history.pushRoute({ "kind": "musicBrowse", "id": String(libraryId), "name": "Music B",
                            "key": "musicBrowse:" + String(libraryId), "title": "Music B",
                            "tab": "albums", "query": "" },
                          undefined, true);
    }
    function pushMusicHome(libraryId): void {
        history.pushRoute({ "kind": "musicHome", "id": String(libraryId), "name": "Music Home",
                            "key": "musicHome:" + libraryId, "title": "Music Home" });
    }
    function pushSeries(seasonId): void {
        root.seriesSeasonId = String(seasonId);
        history.pushRoute({ "kind": "series", "id": "series-1", "name": "Series",
                            "key": "series", "title": "Series",
                            "seasonId": root.seriesSeasonId });
    }
    function setSeriesSeason(seasonId): void {
        root.seriesSeasonId = String(seasonId);
    }
    // SeriesController::open clears currentSeasonId while B loads. The push
    // must retain A first but must still construct B after that transition.
    function openSeriesFromMain(seriesId): void {
        history.rememberFocus();
        root.seriesSeasonId = "";
        history.pushRoute({ "kind": "series", "id": String(seriesId), "name": "Series B",
                            "key": "series", "title": "Series B", "seasonId": "" },
                          undefined, true);
    }
    function goBack(): void { history.goBack(); }
    function goForward(): void { history.goForward(); }
    function goHome(): void { history.goHome(); }
    function focusSecond(): void {
        if (history.currentItem && history.currentItem.focusB)
            history.currentItem.focusB.forceActiveFocus(Qt.OtherFocusReason);
    }
    function resetRoute(id): void {
        history.resetToRoute({
            "kind": "details", "id": String(id), "name": "Session " + id,
            "itemType": "Movie", "key": "details:" + id, "title": "Session " + id
        });
    }

    function prepareRoute(route): void {
        root.preparedRoutes = root.preparedRoutes.concat(
                    [route.kind + ":" + (route.kind === "search" ? route.query : route.id)]);
        if (route.kind === "details")
            root.preparedDetailsId = route.id;
        else if (route.kind === "album")
            root.preparedAlbumId = route.id;
        else if (route.kind === "search")
            root.searchQuery = route.query;
        else if (route.kind === "musicBrowse") {
            root.musicBrowseSection = route.tab;
            root.musicBrowseState = route.query;
        }
        else if (route.kind === "series") {
            root.seriesSeasonId = route.seasonId;
            root.preparedSeriesSeasonId = route.seasonId;
        }
        else if (route.kind === "library")
            root.refillVirtualRows();
    }

    component DetailsProbe: FocusScope {
        property var item: ({
            "itemId": "0", "name": "Item 0", "type": "Movie",
            "posterUrl": "poster://0", "backdropUrl": "backdrop://0",
            "overview": "Overview 0", "year": 2000,
            "officialRating": "PG-0", "communityRating": 8.25,
            "resumable": true, "positionMs": 1234, "runtimeMs": 5678,
            "seriesName": "Series 0", "parentIndexNumber": 2, "indexNumber": 3,
            "albumArtist": "Artist 0", "artistIds": ["artist-0", "guest-0"],
            "childCount": 12, "favorite": true
        })
        readonly property string routeId: String(item.itemId)
        property alias focusA: firstFocus
        property alias focusB: secondFocus
        objectName: "details-" + routeId
        focus: true

        Component.onCompleted: root.createdCount += 1
        Component.onDestruction: {
            root.destroyedCount += 1;
            root.destroyedIds = root.destroyedIds.concat(["details:" + routeId]);
        }

        Column {
            TextInput {
                id: firstFocus
                objectName: "focus-a-" + parent.parent.routeId
                text: parent.parent.item.name
                focus: true
            }
            TextInput {
                id: secondFocus
                objectName: "focus-b-" + parent.parent.routeId
                text: parent.parent.item.overview
            }
        }
    }

    component AlbumProbe: FocusScope {
        property var albumItem: ({})
        readonly property string routeId: String(albumItem.itemId)
        property alias focusA: albumFirst
        property alias focusB: albumSecond
        objectName: "album-" + routeId
        focus: true

        Component.onCompleted: root.createdCount += 1
        Component.onDestruction: {
            root.destroyedCount += 1;
            root.destroyedIds = root.destroyedIds.concat(["album:" + routeId]);
        }

        Column {
            TextInput { id: albumFirst; text: parent.parent.albumItem.name; focus: true }
            TextInput { id: albumSecond; text: parent.parent.albumItem.albumArtist }
        }
    }

    component PlaylistProbe: FocusScope {
        objectName: "playlistPage"
        focus: true
    }

    component MusicPlaylistProbe: FocusScope {
        property string playlistId: ""
        property string playlistName: ""
        objectName: "musicPlaylistPage"
        focus: true
    }

    component ArtistProbe: FocusScope {
        property var artistItem: ({})
        // Mirrors MusicArtistPage's contract: the library the entry was opened
        // under, handed in by the route rather than read off the shell.
        property string libraryId: ""
        readonly property string routeId: String(artistItem.itemId)
        objectName: "artist-" + routeId
        focus: true
    }

    component VirtualProbe: FocusScope {
        readonly property int focusedIndex: virtualGrid.currentIndex
        readonly property bool restorePending: virtualGrid.navigationFocusRestorePending
        readonly property bool emptyViewFocused: virtualRows.count === 0 && virtualGrid.activeFocus
        objectName: "virtual-page"
        focus: true

        function focusRow(index): void {
            virtualGrid.restoreNavigationFocus("i:row-" + index, index);
        }
        function restoreMissing(index): void {
            virtualGrid.restoreNavigationFocus("i:missing-row", index);
        }
        function focusOverride(): void { virtualOverride.forceActiveFocus(Qt.TabFocusReason); }
        function focusSameOwnerAction(): void {
            // Mirrors StrmCard/TrackTable's pointer-action funnel: the action
            // explicitly retires restoration even when focus stays in the
            // same delegate/owner.
            virtualGrid._cancelNavigationFocusForUser();
            virtualSameOwnerAction.forceActiveFocus(Qt.MouseFocusReason);
        }

        StrmGrid {
            id: virtualGrid
            navigationFocusKey: "virtual-primary"
            navigationFocusFallbackItem: virtualOverride
            width: 220
            height: 90
            gridModel: virtualRows
            navigationFocusRefillActive: root.virtualRefillActive
            cellsAcross: 1
            prefetchThreshold: 0
            onNearEnd: root.virtualNearEndCount += 1

            TextInput {
                id: virtualSameOwnerAction
                objectName: "virtual-same-owner-action"
                text: "Cell action"
            }
        }
        TextInput {
            id: virtualOverride
            objectName: "virtual-override"
            anchors.top: virtualGrid.bottom
            text: "Override"
        }
    }

    component TwinOwnerA: StrmGrid {
        navigationFocusKey: "twin-a"
        width: 220
        height: 90
        gridModel: primaryRows
        cellsAcross: 1
        prefetchThreshold: 0
    }

    component TwinOwnerB: StrmGrid {
        navigationFocusKey: "twin-b"
        width: 220
        height: 90
        gridModel: duplicateRows
        cellsAcross: 1
        prefetchThreshold: 0
    }

    component TwinProbe: FocusScope {
        property string personId: ""
        property string personName: ""
        readonly property string focusedOwnerKey: first.item && first.item.activeFocus
                                                   ? first.item.navigationFocusKey
                                                   : second.item && second.item.activeFocus
                                                     ? second.item.navigationFocusKey : ""
        readonly property int focusedIndex: first.item && first.item.activeFocus
                                            ? first.item.currentIndex
                                            : second.item && second.item.activeFocus
                                              ? second.item.currentIndex : -1
        objectName: "twin-page"
        focus: true
        property bool ownersReady: !root.delayTwinOwners

        function owner(key): var {
            if (first.item && first.item.navigationFocusKey === key)
                return first.item;
            if (second.item && second.item.navigationFocusKey === key)
                return second.item;
            return null;
        }
        function focusDuplicate(): void {
            const target = owner("twin-b");
            if (target)
                target.restoreNavigationFocus("p:entry-b", 1);
        }

        Row {
            Loader {
                id: first
                active: parent.parent.ownersReady
                sourceComponent: root.twinSwapped ? twinOwnerBComponent : twinOwnerAComponent
            }
            Loader {
                id: second
                active: parent.parent.ownersReady
                sourceComponent: root.twinSwapped ? twinOwnerAComponent : twinOwnerBComponent
            }
        }

        Timer {
            interval: 2300
            running: root.delayTwinOwners && !parent.ownersReady
            onTriggered: parent.ownersReady = true
        }
    }

    component SearchProbe: FocusScope {
        id: searchPageProbe
        property string queryAtCreation: ""
        readonly property string routeId: "search:" + queryAtCreation
        property alias focusA: searchFirst
        property alias focusB: searchSecond
        readonly property int focusedTrackIndex: searchTrackList.currentIndex
        readonly property bool trackRestorePending: searchTrackFocus.pending
        readonly property bool trackFocused: searchTrackList.activeFocus
        readonly property bool trackOwnerVisible: searchTracks.visible
        readonly property var navSections: [searchTracks]
        objectName: routeId
        focus: true
        signal backRequested()
        signal focusRestoreOverrideRequested()

        function focusTrack(index): void { searchTracks._applyNavigationFocus(index); }
        function focusOverride(): void { searchFirst.forceActiveFocus(Qt.TabFocusReason); }
        function cancelResultFocusRestoresForUserQuery(): void {
            const count = Math.min(navSections.length, 32);
            for (let i = 0; i < count; ++i) {
                const section = navSections[i];
                if (section
                        && typeof section["cancelNavigationFocusRestore"] === "function")
                    section["cancelNavigationFocusRestore"]();
            }
            focusRestoreOverrideRequested();
        }
        function applyUserQuery(query): void {
            cancelResultFocusRestoresForUserQuery();
            root.searchQuery = String(query);
            searchFirst.text = root.searchQuery;
            searchFirst.forceActiveFocus(Qt.OtherFocusReason);
        }
        onFocusRestoreOverrideRequested: StackView.view.retireFocusRestoreOwnership()

        Component.onCompleted: {
            queryAtCreation = root.searchQuery;
            root.createdCount += 1;
        }
        Component.onDestruction: {
            root.destroyedCount += 1;
            root.destroyedIds = root.destroyedIds.concat([routeId]);
        }

        Column {
            TextInput {
                id: searchFirst
                objectName: "search-track-fallback"
                text: parent.parent.queryAtCreation
                focus: true
                // Mirrors SearchPage.qml's onEscapePressed: the field consumes
                // Esc, the first press drops the query, and a second one asks
                // Main to leave the page through its navigation transaction.
                Keys.onEscapePressed: event => {
                    event.accepted = true;
                    if (searchFirst.text.length > 0) {
                        searchPageProbe.applyUserQuery("");
                        return;
                    }
                    searchPageProbe.backRequested();
                }
            }
            FocusScope {
                id: searchTracks
                property string navigationFocusKey: "search-tracks"
                property Item navigationFocusFallbackItem: searchFirst
                property bool navigationFocusRefillActive: root.searchTrackRefillActive
                readonly property bool navigationFocusRestorePending: searchTrackFocus.pending
                property bool _navigationFocusWriting: false
                enabled: root.searchTrackOwnerEnabled
                width: 300
                height: searchTrackRows.count > 0 ? 100 : 0
                visible: searchTrackRows.count > 0

                function navigationFocusSnapshot(): var { return searchTrackFocus.snapshot(); }
                function restoreNavigationFocus(identity, index): bool {
                    return searchTrackFocus.restore(identity, index);
                }
                function cancelNavigationFocusRestore(): void { searchTrackFocus.cancel(); }
                function _cancelNavigationFocusForUser(): void {
                    if (!searchTracks._navigationFocusWriting)
                        searchTrackFocus.cancel();
                }
                function _applyNavigationFocus(index): void {
                    searchTracks._navigationFocusWriting = true;
                    searchTrackList.currentIndex = Number(index);
                    searchTrackList.positionViewAtIndex(Number(index), ListView.Contain);
                    searchTrackList.forceActiveFocus(Qt.OtherFocusReason);
                    searchTracks._navigationFocusWriting = false;
                }
                function _applyNavigationFallback(): void {
                    searchFirst.forceActiveFocus(Qt.OtherFocusReason);
                }

                NavigationFocusRestorer {
                    id: searchTrackFocus
                    model: searchTrackRows
                    count: searchTrackRows.count
                    currentIndex: searchTrackList.currentIndex
                    refillActive: searchTracks.navigationFocusRefillActive
                    settleInterval: 30
                    stallInterval: 250
                    onFocusRequested: index => {
                        root.searchTrackResolutionCount += 1;
                        searchTracks._applyNavigationFocus(index);
                    }
                    onFallbackRequested: searchTracks._applyNavigationFallback()
                }

                Connections {
                    target: searchTracks
                    function onActiveFocusChanged() {
                        if (!searchTracks.activeFocus)
                            searchTracks._cancelNavigationFocusForUser();
                    }
                }

                ListView {
                    id: searchTrackList
                    objectName: "search-track-list"
                    anchors.fill: parent
                    focus: true
                    model: searchTrackRows
                    currentIndex: 0
                    delegate: TextInput {
                        required property string itemId
                        required property string name
                        objectName: "search-row-" + itemId
                        text: name
                    }
                    Keys.onPressed: searchTracks._cancelNavigationFocusForUser()
                }
            }
            TextInput { id: searchSecond; text: "second" }
        }
    }

    component MusicBrowseProbe: FocusScope {
        property string libraryId: ""
        property string libraryName: ""
        property string initialSection: "albums"
        property string selectedSection: initialSection
        property string controllerSectionAtCreation: ""
        objectName: "musicBrowse-" + selectedSection
        focus: true
        Component.onCompleted: controllerSectionAtCreation = root.musicBrowseSection
    }

    component MusicHomeProbe: FocusScope {
        property string libraryId: ""
        property string libraryName: ""
        objectName: "musicHome-" + libraryId
        focus: true
    }

    component SeriesProbe: FocusScope {
        readonly property string selectedSeasonId: root.seriesSeasonId
        property string controllerSeasonAtCreation: ""
        objectName: "series-" + selectedSeasonId
        focus: true
        Component.onCompleted: controllerSeasonAtCreation = root.seriesSeasonId
    }

    Component { id: detailsComponent; DetailsProbe {} }
    Component { id: albumComponent; AlbumProbe {} }
    Component { id: playlistComponent; PlaylistProbe {} }
    Component { id: musicPlaylistComponent; MusicPlaylistProbe {} }
    Component { id: artistComponent; ArtistProbe {} }
    Component { id: twinOwnerAComponent; TwinOwnerA {} }
    Component { id: twinOwnerBComponent; TwinOwnerB {} }
    Component { id: personComponent; TwinProbe {} }
    Component { id: libraryComponent; VirtualProbe {} }
    Component { id: searchComponent; SearchProbe {} }
    Component { id: musicBrowseComponent; MusicBrowseProbe {} }
    Component { id: musicHomeComponent; MusicHomeProbe {} }
    Component { id: seriesComponent; SeriesProbe {} }
    Component { id: loginComponent; FocusScope { objectName: "login-base"; focus: true } }
    Component { id: homeComponent; FocusScope { objectName: "home-base"; focus: true } }
    Component { id: transientComponent; FocusScope { objectName: "playerPage"; focus: true } }

    ListModel { id: virtualRows }
    ListModel { id: searchTrackRows }
    ListModel { id: progressRows }
    NavigationFocusRestorer {
        id: progressRestorer
        model: progressRows
        count: progressRows.count
        currentIndex: -1
        refillActive: root.progressRefillActive
        settleInterval: 30
        stallInterval: 120
        onFocusRequested: index => root.progressFocusedIndex = index
        onFallbackRequested: root.progressFallbackCount += 1
    }
    ListModel {
        id: primaryRows
        ListElement { itemId: "primary"; name: "Primary" }
    }
    ListModel {
        id: duplicateRows
        ListElement { playlistItemId: "entry-a"; itemId: "same-media"; name: "First occurrence" }
        ListElement { playlistItemId: "entry-b"; itemId: "same-media"; name: "Second occurrence" }
    }
    Timer {
        id: refillTimer
        interval: 225
        repeat: true
        onTriggered: root.appendVirtualBatch()
    }

    BoundedNavigationStack {
        id: history
        objectName: "history"
        anchors.fill: parent
        historyLimit: 4
        // Two live page graphs: the page on screen plus one instant Back hop.
        // Small on purpose, so a five-push sequence exercises BOTH branches of
        // goBack() — the pop inside the window and the reconstruction past it.
        livePageLimit: 2
        focusItem: root.Window.window ? root.Window.window.activeFocusItem : null
        currentSearchQuery: root.searchQuery
        currentMusicBrowseSection: root.musicBrowseSection
        currentMusicBrowseState: root.musicBrowseState
        currentSeriesSeasonId: root.seriesSeasonId
        initialRoute: ({
            "kind": "details", "id": "0", "name": "Item 0", "itemType": "Movie",
            "key": "details:0", "title": "Title 0",
            "posterUrl": "poster://0", "backdropUrl": "backdrop://0",
            "overview": "Overview 0", "year": 2000,
            "officialRating": "PG-0", "communityRating": 8.25,
            "resumable": true, "positionMs": 1234, "runtimeMs": 5678,
            "seriesName": "Series 0", "parentIndexNumber": 2, "indexNumber": 3,
            "albumArtist": "Artist 0", "artistIds": ["artist-0", "guest-0"],
            "childCount": 12, "favorite": true
        })
        // No initialItem: adoptInitialRoute pushes the base page from
        // initialRoute, so the stack owns no page it cannot evict.
        detailsPageComponent: detailsComponent
        albumPageComponent: albumComponent
        playlistPageComponent: playlistComponent
        musicPlaylistPageComponent: musicPlaylistComponent
        artistPageComponent: artistComponent
        personPageComponent: personComponent
        libraryPageComponent: libraryComponent
        searchPageComponent: searchComponent
        musicBrowsePageComponent: musicBrowseComponent
        musicHomePageComponent: musicHomeComponent
        seriesPageComponent: seriesComponent
        loginPageComponent: loginComponent
        homePageComponent: homeComponent
        onPrepareRequested: route => root.prepareRoute(route)
    }

    // Mirrors Main.qml: a page's back intent goes through the same navigation
    // transaction as the rail, mouse and input-map paths.
    Connections {
        target: history.currentItem
        ignoreUnknownSignals: true
        function onBackRequested() { root.goBack(); }
    }

    Component.onCompleted: {
        root.refillBatch = 0;
        while (virtualRows.count < 30)
            root.appendVirtualBatch();
        root.virtualRefillActive = false;
    }
}
)QML";

QObject *createProbe(QTemporaryDir &dir, QQuickView &view)
{
    const QString helperSource =
        QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/shell/BoundedNavigationStack.qml");
    const QString helperTarget = dir.filePath(QStringLiteral("BoundedNavigationStack.qml"));
    if (!QFile::copy(helperSource, helperTarget))
        return nullptr;

    const QString modulePath = dir.filePath(QStringLiteral("StrmQt"));
    if (!QDir().mkpath(modulePath))
        return nullptr;
    const QStringList moduleFiles = {
        QStringLiteral("Theme.qml"),          QStringLiteral("FocusRing.qml"),
        QStringLiteral("StrmIcon.qml"),       QStringLiteral("StrmTooltip.qml"),
        QStringLiteral("StrmIconButton.qml"), QStringLiteral("StrmCard.qml"),
        QStringLiteral("StrmScrollBar.qml"),  QStringLiteral("NavigationFocusRestorer.qml"),
        QStringLiteral("StrmGrid.qml"),       QStringLiteral("StrmImage.qml"),
        // StrmGrid publishes the focused column through this singleton on a
        // vertical step. It resolves without being staged — the engine finds it
        // on the ambient import path — so this is not fixing a failure; it is
        // removing the reliance on that accident. The module is written out
        // file by file precisely so it is self-contained, and a member that
        // reaches outside it only shows up as "Type StrmGrid unavailable" on
        // whichever machine happens not to have the real module in reach.
        QStringLiteral("NavigationColumn.qml"),
    };
    for (const QString &name : moduleFiles) {
        const QString sourceRoot = name == QStringLiteral("Theme.qml")
                                       ? QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/")
                                       : QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/controls/");
        if (!QFile::copy(sourceRoot + name, modulePath + QLatin1Char('/') + name))
            return nullptr;
    }
    QFile qmldir(modulePath + QStringLiteral("/qmldir"));
    if (!qmldir.open(QIODevice::WriteOnly))
        return nullptr;
    qmldir.write("module StrmQt\n"
                 "singleton Theme 1.0 Theme.qml\n"
                 "FocusRing 1.0 FocusRing.qml\n"
                 "StrmIcon 1.0 StrmIcon.qml\n"
                 "StrmTooltip 1.0 StrmTooltip.qml\n"
                 "StrmIconButton 1.0 StrmIconButton.qml\n"
                 "StrmCard 1.0 StrmCard.qml\n"
                 "StrmScrollBar 1.0 StrmScrollBar.qml\n"
                 "NavigationFocusRestorer 1.0 NavigationFocusRestorer.qml\n"
                 "StrmGrid 1.0 StrmGrid.qml\n"
                 "StrmImage 1.0 StrmImage.qml\n"
                 "singleton NavigationColumn 1.0 NavigationColumn.qml\n");
    qmldir.close();

    QFile probe(dir.filePath(QStringLiteral("Probe.qml")));
    if (!probe.open(QIODevice::WriteOnly))
        return nullptr;
    probe.write(kProbe);
    probe.close();

    view.engine()->addImportPath(dir.path());
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.setSource(QUrl::fromLocalFile(probe.fileName()));
    if (view.status() != QQuickView::Ready)
        return nullptr;
    view.resize(480, 320);
    view.show();
    if (!QTest::qWaitForWindowExposed(&view))
        return nullptr;
    return view.rootObject();
}

bool invoke(QObject *object, const char *method, const QVariant &argument = {})
{
    if (!argument.isValid())
        return QMetaObject::invokeMethod(object, method);
    return QMetaObject::invokeMethod(object, method, Q_ARG(QVariant, argument));
}

bool invoke2(QObject *object, const char *method, const QVariant &first, const QVariant &second)
{
    return QMetaObject::invokeMethod(object, method, Q_ARG(QVariant, first),
                                     Q_ARG(QVariant, second));
}

QVariantList listProperty(QObject *object, const char *name)
{
    return object->property(name).toList();
}

QObject *currentItem(QObject *history)
{
    return history->property("currentItem").value<QObject *>();
}

QPair<QObject *, QObject *> createHistoryProbe(QTemporaryDir &dir, QQuickView &view)
{
    QObject *root = createProbe(dir, view);
    if (!root)
        return {};
    return {root, root->findChild<QObject *>(QStringLiteral("history"))};
}

// Lifts a named function's source out of a QML file, matched up to its "(" so
// the name is exact (see functionBody's note for what a prefix match costs).
QByteArray functionSource(const QByteArray &qml, const QByteArray &name, const QByteArray &next)
{
    const qsizetype begin = qml.indexOf("function " + name + "(");
    const qsizetype end = qml.indexOf("function " + next + "(", begin + 1);
    if (begin < 0 || end <= begin)
        return {};
    const QByteArray slice = qml.mid(begin, end - begin);
    // Trim back to the function's own closing brace so the slice cannot carry
    // a trailing comment that belongs to the next declaration.
    const qsizetype close = slice.lastIndexOf('}');
    return close >= 0 ? slice.left(close + 1) : slice;
}

// Builds Main.qml's REAL openRoute body into a component that records which
// helper it calls, so the routing decision is evaluated rather than grepped.
// A substring assertion cannot tell `=== "music"` from `!== "music"`; this can.
QObject *createRoutingProbe(QTemporaryDir &dir, QQmlEngine &engine,
                            std::unique_ptr<QObject> &owner)
{
    QFile main(QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/Main.qml"));
    if (!main.open(QIODevice::ReadOnly))
        return nullptr;
    const QByteArray openRoute = functionSource(main.readAll(), "openRoute", "openSeries");
    if (openRoute.isEmpty())
        return nullptr;

    QByteArray source =
        "import QtQuick\n"
        "Item {\n"
        "    id: root\n"
        "    property string interactionContext: \"\"\n"
        "    property var calls: []\n"
        "    function record(what) { root.calls = root.calls.concat([what]); }\n"
        "    function openMusicPlaylist(id, name) { root.record(\"musicPlaylist:\" + id); }\n"
        "    function openPlaylist(id, name) { root.record(\"playlist:\" + id); }\n"
        "    function openAlbum(target) { root.record(\"album\"); }\n"
        "    function openArtist(target) { root.record(\"artist\"); }\n"
        "    function openDetails(target) { root.record(\"details\"); }\n"
        "    function openSeries(a, b, c) { root.record(\"series\"); }\n"
        "    function route(kind, id, name) {\n"
        "        root.openRoute(kind, { \"itemId\": id, \"name\": name });\n"
        "    }\n";
    source += "    " + openRoute + "\n}\n";

    const QString path = dir.filePath(QStringLiteral("RoutingProbe.qml"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return nullptr;
    file.write(source);
    file.close();

    QQmlComponent component(&engine, QUrl::fromLocalFile(path));
    if (component.isError()) {
        qWarning("%s", qPrintable(component.errorString()));
        return nullptr;
    }
    owner.reset(component.create());
    return owner.get();
}

} // namespace

void NavigationHistoryTest::capsGraphsAndReconstructsMetadata()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QQuickView view;
    const auto [root, history] = createHistoryProbe(dir, view);
    QVERIFY2(root, qPrintable(view.errors().isEmpty() ? QStringLiteral("failed to create probe")
                                                      : view.errors().first().toString()));
    QVERIFY(history);
    QTRY_COMPARE(root->property("createdCount").toInt(), 1);

    const int destroyedBeforeOverflow = root->property("destroyedCount").toInt();
    for (int id = 1; id <= 7; ++id)
        QVERIFY(invoke(root, "pushRoute", id));

    QTRY_COMPARE(history->property("retainedRouteCount").toInt(), 4);
    QCOMPARE(listProperty(history, "navTrail").size(), 4);
    QCOMPARE(listProperty(history, "navForward").size(), 0);
    // Seven forward navigations leave the live window's worth of page graphs —
    // not seven, and not one: the trim evicts the oldest and keeps the tail.
    QCOMPARE(history->property("pageGraphCount").toInt(), 2);
    QCOMPARE(history->property("depth").toInt(), 2);
    QVERIFY(history->property("focusMemoryCount").toInt() <= 4);
    QTRY_VERIFY(root->property("destroyedCount").toInt() > destroyedBeforeOverflow);
    QCOMPARE(root->property("createdCount").toInt() - root->property("destroyedCount").toInt(), 2);

    const QVariantMap retained = listProperty(history, "navTrail").constLast().toMap();
    QCOMPARE(retained.value(QStringLiteral("id")).toString(), QStringLiteral("7"));
    QCOMPARE(retained.value(QStringLiteral("posterUrl")).toString(), QStringLiteral("poster://7"));
    QCOMPARE(retained.value(QStringLiteral("overview")).toString(), QStringLiteral("Overview 7"));
    QCOMPARE(retained.value(QStringLiteral("year")).toInt(), 2007);
    QVERIFY(!retained.contains(QStringLiteral("arbitraryMap")));
    QVERIFY(!retained.contains(QStringLiteral("prepare")));
    for (const QVariant &value : history->property("focusMemory").toMap())
        QCOMPARE(value.metaType().id(), QMetaType::QString);

    // Route 6 is still inside the live window, so Back to it is a pop of a page
    // that already exists — and it still re-arms its controller.
    QVERIFY(invoke(root, "goBack"));
    QTRY_COMPARE(currentItem(history)->property("routeId").toString(), QStringLiteral("6"));
    QCOMPARE(root->property("preparedDetailsId").toString(), QStringLiteral("6"));

    // Route 5 is past the window and has no page graph. Back must reconstruct
    // the honest Details header from the retained scalar DTO and re-arm its
    // controller.
    QVERIFY(invoke(root, "goBack"));
    QTRY_COMPARE(currentItem(history)->property("routeId").toString(), QStringLiteral("5"));
    const QVariantMap details = currentItem(history)->property("item").toMap();
    QCOMPARE(details.value(QStringLiteral("posterUrl")).toString(), QStringLiteral("poster://5"));
    QCOMPARE(details.value(QStringLiteral("backdropUrl")).toString(),
             QStringLiteral("backdrop://5"));
    QCOMPARE(details.value(QStringLiteral("overview")).toString(), QStringLiteral("Overview 5"));
    QCOMPARE(details.value(QStringLiteral("year")).toInt(), 2005);
    QCOMPARE(details.value(QStringLiteral("officialRating")).toString(), QStringLiteral("PG-5"));
    QCOMPARE(details.value(QStringLiteral("communityRating")).toDouble(), 8.25);
    QVERIFY(details.value(QStringLiteral("resumable")).toBool());
    QCOMPARE(details.value(QStringLiteral("positionMs")).toLongLong(), 1234);
    QCOMPARE(root->property("preparedDetailsId").toString(), QStringLiteral("5"));

    // Put an Album outside the live window, then walk back to it. Its header
    // DTO must survive its page graph too.
    QVERIFY(invoke(root, "resetRoute", QStringLiteral("base")));
    QVERIFY(invoke(root, "pushRoute", 1));
    QVERIFY(invoke(root, "pushAlbum", 42));
    QVERIFY(invoke(root, "pushRoute", 3));
    QVERIFY(invoke(root, "pushRoute", 4));
    QTRY_COMPARE(history->property("depth").toInt(), 2);
    QVERIFY(invoke(root, "goBack"));
    QVERIFY(invoke(root, "goBack"));
    QTRY_COMPARE(currentItem(history)->property("routeId").toString(), QStringLiteral("42"));
    const QVariantMap album = currentItem(history)->property("albumItem").toMap();
    QCOMPARE(album.value(QStringLiteral("posterUrl")).toString(), QStringLiteral("poster://42"));
    QCOMPARE(album.value(QStringLiteral("year")).toInt(), 2042);
    QCOMPARE(album.value(QStringLiteral("albumArtist")).toString(), QStringLiteral("Artist 42"));
    QCOMPARE(album.value(QStringLiteral("artistIds")).toStringList(),
             QStringList({QStringLiteral("artist-42"), QStringLiteral("guest-42")}));
    QVERIFY(album.value(QStringLiteral("favorite")).toBool());
    QCOMPARE(root->property("preparedAlbumId").toString(), QStringLiteral("42"));
}

// Remedy 1 of the memory fix: N forward navigations leave the LIVE WINDOW's
// worth of page graphs, not N of them. The route history is a different currency
// and keeps its own, larger limit, and every route the window has dropped is
// still reconstructable from its descriptor.
void NavigationHistoryTest::forwardWalkBoundsLivePagesNotRoutes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QQuickView view;
    const auto [root, history] = createHistoryProbe(dir, view);
    QVERIFY(root);
    QVERIFY(history);
    QTRY_COMPARE(root->property("createdCount").toInt(), 1);

    for (int id = 1; id <= 6; ++id)
        QVERIFY(invoke(root, "pushRoute", id));

    // Six constructions, a two-page window: two graphs alive, four already gone.
    QTRY_COMPARE(history->property("pageGraphCount").toInt(), 2);
    QCOMPARE(history->property("depth").toInt(), 2);
    QTRY_COMPARE(root->property("createdCount").toInt() - root->property("destroyedCount").toInt(),
                 2);
    QCOMPARE(root->property("createdCount").toInt(), 7);
    // The live trim does not touch the route history, which keeps its own limit.
    QCOMPARE(listProperty(history, "navTrail").size(), 4);

    // instantiatedTokens must be the TAIL of navTrail. If it drifts, goBack's
    // fast path pops to a page that belongs to a different route.
    const QVariantList trail = listProperty(history, "navTrail");
    const QVariantList tokens = listProperty(history, "instantiatedTokens");
    QCOMPARE(tokens.size(), 2);
    QCOMPARE(tokens.at(0).toInt(),
             trail.at(2).toMap().value(QStringLiteral("token")).toInt());
    QCOMPARE(tokens.at(1).toInt(),
             trail.at(3).toMap().value(QStringLiteral("token")).toInt());

    // Walk the whole trail back. Every entry still produces its own page; the
    // ones the window dropped do it from the retained scalar descriptor.
    QVERIFY(invoke(root, "goBack"));
    QTRY_COMPARE(currentItem(history)->property("routeId").toString(), QStringLiteral("5"));
    QVERIFY(invoke(root, "goBack"));
    QTRY_COMPARE(currentItem(history)->property("routeId").toString(), QStringLiteral("4"));
    QCOMPARE(currentItem(history)->property("item").toMap().value(QStringLiteral("overview")).toString(),
             QStringLiteral("Overview 4"));
    QVERIFY(invoke(root, "goBack"));
    QTRY_COMPARE(currentItem(history)->property("routeId").toString(), QStringLiteral("0"));
    QVERIFY(!history->property("canGoBack").toBool());
}

// The trim takes the OLDEST live page. Removing the top instead would leave the
// route the user just opened off screen; removing the wrong middle index would
// take the page the next Back needs. Both are the same off-by-one, and the
// StackView cannot remove a page from under the ones above it, so this also
// pins that the retained tail is kept rather than reconstructed.
void NavigationHistoryTest::trimEvictsTheOldestLivePageAndNotTheCurrentOne()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QQuickView view;
    const auto [root, history] = createHistoryProbe(dir, view);
    QVERIFY(root);
    QVERIFY(history);

    QVERIFY(invoke(root, "resetRoute", QStringLiteral("base")));
    QTRY_COMPARE(currentItem(history)->property("routeId").toString(), QStringLiteral("base"));
    QVERIFY(invoke(root, "pushRoute", 1));
    QTRY_COMPARE(currentItem(history)->property("routeId").toString(), QStringLiteral("1"));
    const QPointer<QObject> pageOne(currentItem(history));
    QVERIFY(!pageOne.isNull());

    // The push that overflows the two-page window.
    QVERIFY(invoke(root, "pushRoute", 2));
    QTRY_COMPARE(currentItem(history)->property("routeId").toString(), QStringLiteral("2"));
    QCOMPARE(history->property("pageGraphCount").toInt(), 2);
    QCOMPARE(history->property("depth").toInt(), 2);

    // The base page was the oldest and is the one that went.
    QTRY_VERIFY(root->property("destroyedIds").toStringList().contains(
        QStringLiteral("details:base")));
    const QStringList destroyed = root->property("destroyedIds").toStringList();
    QVERIFY(!destroyed.contains(QStringLiteral("details:2")));
    QVERIFY(!destroyed.contains(QStringLiteral("details:1")));

    // Route 1 came through the rebuild as the SAME object, so Back into the
    // window costs no construction at all.
    QVERIFY(invoke(root, "goBack"));
    QTRY_COMPARE(currentItem(history)->property("routeId").toString(), QStringLiteral("1"));
    QVERIFY(!pageOne.isNull());
    QCOMPARE(currentItem(history), pageOne.data());
}

// Past the live window Back is a reconstruction rather than a reveal, so the
// route's focus locator has to land on the remembered control on a page graph
// that did not exist a moment ago. That is the whole risk of a small window.
void NavigationHistoryTest::backBeyondTheLiveWindowRebuildsAndRestoresFocus()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QQuickView view;
    const auto [root, history] = createHistoryProbe(dir, view);
    QVERIFY(root);
    QVERIFY(history);

    QVERIFY(invoke(root, "resetRoute", QStringLiteral("base")));
    QVERIFY(invoke(root, "pushRoute", 1));
    QTRY_COMPARE(currentItem(history)->property("routeId").toString(), QStringLiteral("1"));
    QVERIFY(invoke(root, "focusSecond"));
    QTRY_VERIFY(view.activeFocusItem());
    QCOMPARE(view.activeFocusItem()->objectName(), QStringLiteral("focus-b-1"));
    const QPointer<QObject> firstGraph(currentItem(history));

    // Two more pushes put route 1 outside the two-page window: its graph is
    // destroyed while its descriptor stays in navTrail.
    QVERIFY(invoke(root, "pushRoute", 2));
    QVERIFY(invoke(root, "pushRoute", 3));
    QTRY_VERIFY(firstGraph.isNull());
    QCOMPARE(listProperty(history, "navTrail").size(), 4);

    QVERIFY(invoke(root, "goBack"));
    QTRY_VERIFY(currentItem(history));
    QTRY_COMPARE(currentItem(history)->property("routeId").toString(), QStringLiteral("2"));
    QVERIFY(invoke(root, "goBack"));
    // Back past the window must land on a page, not on an empty stack.
    QTRY_VERIFY(currentItem(history));
    QTRY_COMPARE(currentItem(history)->property("routeId").toString(), QStringLiteral("1"));
    // A different object from the one that held the keyboard, and the keyboard
    // is back on the same control.
    QTRY_COMPARE(view.activeFocusItem()->objectName(), QStringLiteral("focus-b-1"));
}

void NavigationHistoryTest::restoresForwardFocusAndReplacesBranches()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QQuickView view;
    const auto [root, history] = createHistoryProbe(dir, view);
    QVERIFY(root);
    QVERIFY(history);

    QVERIFY(invoke(root, "focusSecond"));
    QTRY_VERIFY(view.activeFocusItem());
    QCOMPARE(view.activeFocusItem()->objectName(), QStringLiteral("focus-b-0"));
    QVERIFY(invoke(root, "pushRoute", 1));
    QTRY_COMPARE(view.activeFocusItem()->objectName(), QStringLiteral("focus-a-1"));
    QVERIFY(invoke(root, "focusSecond"));
    QTRY_COMPARE(view.activeFocusItem()->objectName(), QStringLiteral("focus-b-1"));

    QVERIFY(invoke(root, "goBack"));
    QTRY_COMPARE(view.activeFocusItem()->objectName(), QStringLiteral("focus-b-0"));
    QVERIFY(invoke(root, "goForward"));
    // Forward constructs a new page-1 graph. Its scalar child-path locator,
    // not the destroyed QObject, must restore the second eligible child.
    QTRY_COMPARE(view.activeFocusItem()->objectName(), QStringLiteral("focus-b-1"));
    for (const QVariant &value : history->property("focusMemory").toMap())
        QCOMPARE(value.metaType().id(), QMetaType::QString);

    QVERIFY(invoke(root, "pushRoute", 2));
    QCoreApplication::processEvents();
    QVERIFY(invoke(root, "pushRoute", 3));
    QCoreApplication::processEvents();
    QVERIFY(invoke(root, "goBack"));
    QCoreApplication::processEvents();
    QVERIFY(invoke(root, "goBack"));
    QCoreApplication::processEvents();
    QCOMPARE(listProperty(history, "navTrail").size(), 2);
    QCOMPARE(listProperty(history, "navForward").size(), 2);

    // A branch replacement with both sides populated must destroy the Forward
    // branch, retain the current trail and prune its focus locators.
    QVERIFY(invoke(root, "pushRoute", 9));
    QCOMPARE(listProperty(history, "navTrail").size(), 3);
    QCOMPARE(listProperty(history, "navForward").size(), 0);
    QCOMPARE(history->property("retainedRouteCount").toInt(), 3);
    // Three retained ROUTES, but only the live window's worth of page graphs:
    // the trail behind the current page was walked back past the window.
    QCOMPARE(history->property("pageGraphCount").toInt(), 2);
    QCOMPARE(history->property("depth").toInt(), 2);
    QVERIFY(history->property("focusMemoryCount").toInt() <= 3);

    const QVariantMap focus = history->property("focusMemory").toMap();
    const QVariantList trail = listProperty(history, "navTrail");
    for (auto it = focus.cbegin(); it != focus.cend(); ++it) {
        const QString token = it.key();
        const bool retained =
            std::any_of(trail.cbegin(), trail.cend(), [&token](const QVariant &route) {
                return QString::number(route.toMap().value(QStringLiteral("token")).toInt()) ==
                       token;
            });
        QVERIFY(retained);
    }

    // Identity reset still clears descriptors, Forward, graphs and locators.
    QVERIFY(invoke(root, "resetRoute", QStringLiteral("session-b")));
    QTRY_COMPARE(history->property("depth").toInt(), 1);
    QCOMPARE(history->property("retainedRouteCount").toInt(), 1);
    QCOMPARE(listProperty(history, "navForward").size(), 0);
    QCOMPARE(history->property("focusMemoryCount").toInt(), 0);
    QCOMPARE(history->property("pageGraphCount").toInt(), 1);
}

void NavigationHistoryTest::restoresPerEntrySearchAndPreparesRouteKinds()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QQuickView view;
    const auto [root, history] = createHistoryProbe(dir, view);
    QVERIFY(root);
    QVERIFY(history);

    QVERIFY(invoke(root, "pushSearch", QStringLiteral("alpha")));
    QTRY_COMPARE(currentItem(history)->property("routeId").toString(),
                 QStringLiteral("search:alpha"));
    QVERIFY(invoke(root, "pushRoute", 1));
    QVERIFY(invoke(root, "pushSearch", QStringLiteral("beta")));
    QTRY_COMPARE(history->property("currentEntry").toMap().value(QStringLiteral("kind")).toString(),
                 QStringLiteral("search"));

    // Editing happens after the route was pushed. The transition must update
    // only this Search descriptor, leaving the earlier alpha entry untouched.
    QVERIFY(invoke(root, "setSearchQuery", QStringLiteral("beta edited")));
    QVERIFY(invoke(root, "goBack"));
    QTRY_COMPARE(root->property("preparedDetailsId").toString(), QStringLiteral("1"));
    QVERIFY(invoke(root, "goBack"));
    QTRY_COMPARE(root->property("searchQuery").toString(), QStringLiteral("alpha"));
    QTRY_COMPARE(currentItem(history)->property("routeId").toString(),
                 QStringLiteral("search:alpha"));

    QVERIFY(invoke(root, "goForward"));
    QTRY_COMPARE(root->property("preparedDetailsId").toString(), QStringLiteral("1"));
    QVERIFY(invoke(root, "goForward"));
    QTRY_COMPARE(root->property("searchQuery").toString(), QStringLiteral("beta edited"));
    QTRY_COMPARE(currentItem(history)->property("routeId").toString(),
                 QStringLiteral("search:beta edited"));
    const QVariantMap entry = history->property("currentEntry").toMap();
    QCOMPARE(entry.value(QStringLiteral("kind")).toString(), QStringLiteral("search"));
    QCOMPARE(entry.value(QStringLiteral("query")).toString(), QStringLiteral("beta edited"));

    const QStringList prepared = root->property("preparedRoutes").toStringList();
    QVERIFY(prepared.contains(QStringLiteral("details:1")));
    QVERIFY(prepared.contains(QStringLiteral("search:alpha")));
    QVERIFY(prepared.contains(QStringLiteral("search:beta edited")));
}

void NavigationHistoryTest::searchEscapeClearsQueryThenGoesBackThroughTransaction()
{
    // SOL-16: SearchPage's first Escape drops the query in place; the second
    // one must reach Main's navigation transaction (goBack) rather than pop
    // the StackView directly. The probe wires the page's backRequested signal
    // through the same Connections Main.qml uses and drives both presses as
    // real key events on the offscreen window.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QQuickView view;
    const auto [root, history] = createHistoryProbe(dir, view);
    QVERIFY(root);
    QVERIFY(history);

    // Focus a specific control on the base page so the return trip has a
    // focus locator to restore.
    QVERIFY(invoke(root, "focusSecond"));
    QTRY_COMPARE(view.activeFocusItem()->objectName(), QStringLiteral("focus-b-0"));

    QVERIFY(invoke(root, "pushSearch", QStringLiteral("alpha")));
    QTRY_COMPARE(currentItem(history)->property("routeId").toString(),
                 QStringLiteral("search:alpha"));
    QTRY_COMPARE(view.activeFocusItem()->objectName(),
                 QStringLiteral("search-track-fallback"));
    QCOMPARE(history->property("depth").toInt(), 2);
    QCOMPARE(history->property("pageGraphCount").toInt(), 2);

    // First Escape: the page consumes it. The query drops and the field keeps
    // focus, but nothing in the navigation state may move.
    QTest::keyClick(&view, Qt::Key_Escape);
    QCOMPARE(root->property("searchQuery").toString(), QString());
    QTRY_COMPARE(view.activeFocusItem()->objectName(),
                 QStringLiteral("search-track-fallback"));
    QCOMPARE(history->property("depth").toInt(), 2);
    QCOMPARE(listProperty(history, "navTrail").size(), 2);
    QCOMPARE(listProperty(history, "navForward").size(), 0);
    QCOMPARE(history->property("currentEntry").toMap().value(QStringLiteral("key")).toString(),
             QStringLiteral("search"));

    // Second Escape: backRequested -> Main's goBack -> the full transaction.
    QTest::keyClick(&view, Qt::Key_Escape);
    QTRY_COMPARE(history->property("depth").toInt(), 1);
    QCOMPARE(history->property("pageGraphCount").toInt(), 1);
    QCOMPARE(listProperty(history, "navTrail").size(), 1);
    QCOMPARE(currentItem(history)->property("routeId").toString(), QStringLiteral("0"));

    // Key and title describe the restored route, not Search; the search route
    // moved to Forward with the query as captured at Back time (already
    // cleared by the first Escape).
    const QVariantMap current = history->property("currentEntry").toMap();
    QCOMPARE(current.value(QStringLiteral("kind")).toString(), QStringLiteral("details"));
    QCOMPARE(current.value(QStringLiteral("key")).toString(), QStringLiteral("details:0"));
    QCOMPARE(current.value(QStringLiteral("title")).toString(), QStringLiteral("Title 0"));
    const QVariantList forward = listProperty(history, "navForward");
    QCOMPARE(forward.size(), 1);
    QCOMPARE(forward.constFirst().toMap().value(QStringLiteral("kind")).toString(),
             QStringLiteral("search"));
    QCOMPARE(forward.constFirst().toMap().value(QStringLiteral("key")).toString(),
             QStringLiteral("search"));
    QCOMPARE(forward.constFirst().toMap().value(QStringLiteral("query")).toString(), QString());
    QVERIFY(history->property("canGoForward").toBool());
    QVERIFY(!history->property("canGoBack").toBool());

    // The restored controller scope was re-prepared for the details route.
    QCOMPARE(root->property("preparedDetailsId").toString(), QStringLiteral("0"));

    // Focus returned to the control the user was on before opening Search.
    QTRY_COMPARE(view.activeFocusItem()->objectName(), QStringLiteral("focus-b-0"));

    // The transaction is live in both directions: Forward returns to Search.
    QVERIFY(invoke(root, "goForward"));
    QTRY_COMPARE(history->property("currentEntry")
                     .toMap()
                     .value(QStringLiteral("key"))
                     .toString(),
                 QStringLiteral("search"));
    QCOMPARE(currentItem(history)->property("routeId").toString(),
             QStringLiteral("search:"));
}

void NavigationHistoryTest::restoresPerEntryMusicBrowseState()
{
    const QString state = QStringLiteral(
        "{\"v\":1,\"g\":[\"genre-1\"],\"d\":1970,\"f\":\"any\",\"fav\":true}");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QQuickView view;
    const auto [root, history] = createHistoryProbe(dir, view);
    QVERIFY(root);
    QVERIFY(history);

    QVERIFY(invoke(root, "pushMusicBrowse", QStringLiteral("albums")));
    QVERIFY(invoke(root, "setMusicBrowseSection", QStringLiteral("songs")));
    QVERIFY(invoke(root, "setMusicBrowseState", state));
    QVERIFY(invoke(root, "pushRoute", 91));

    const QVariantMap retained = listProperty(history, "navTrail").at(1).toMap();
    QCOMPARE(retained.value(QStringLiteral("tab")).toString(), QStringLiteral("songs"));
    QCOMPARE(retained.value(QStringLiteral("query")).toString(), state);

    // Another scope moves the shared controller on; Back must put it back.
    QVERIFY(invoke(root, "setMusicBrowseState", QString()));
    QVERIFY(invoke(root, "goBack"));
    QTRY_COMPARE(currentItem(history)->property("selectedSection").toString(),
                 QStringLiteral("songs"));
    QTRY_COMPARE(root->property("musicBrowseState").toString(), state);
    QVERIFY(invoke(root, "goBack"));
    QVERIFY(invoke(root, "goForward"));
    QTRY_COMPARE(currentItem(history)->property("initialSection").toString(),
                 QStringLiteral("songs"));
    QTRY_COMPARE(currentItem(history)->property("selectedSection").toString(),
                 QStringLiteral("songs"));
    QTRY_COMPARE(root->property("musicBrowseState").toString(), state);
}

void NavigationHistoryTest::reconstructsMusicHomeAfterEviction()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QQuickView view;
    const auto [root, history] = createHistoryProbe(dir, view);
    QVERIFY(root);
    QVERIFY(history);

    QVERIFY(invoke(root, "resetRoute", QStringLiteral("base")));
    // One extra route push so the sequence below actually exceeds historyLimit
    // 4 and exercises eviction (P2-R2); without it navTrail only ever reaches
    // exactly 4 entries and nothing is evicted.
    QVERIFY(invoke(root, "pushRoute", 1));
    QVERIFY(invoke(root, "pushMusicHome", QStringLiteral("lib-1")));
    QTRY_COMPARE(currentItem(history)->objectName(), QStringLiteral("musicHome-lib-1"));
    QCOMPARE(currentItem(history)->property("libraryId").toString(), QStringLiteral("lib-1"));
    const QVariantMap entry = history->property("currentEntry").toMap();
    QCOMPARE(entry.value(QStringLiteral("key")).toString(), QStringLiteral("musicHome:lib-1"));

    // Push Home out of the live window, then walk back to it.
    QVERIFY(invoke(root, "pushRoute", 3));
    QVERIFY(invoke(root, "pushRoute", 4));
    QTRY_COMPARE(history->property("depth").toInt(), 2);
    QVERIFY(invoke(root, "goBack"));
    QVERIFY(invoke(root, "goBack"));
    QTRY_COMPARE(currentItem(history)->objectName(), QStringLiteral("musicHome-lib-1"));
    QCOMPARE(currentItem(history)->property("libraryId").toString(), QStringLiteral("lib-1"));
    QCOMPARE(currentItem(history)->property("libraryName").toString(), QStringLiteral("Music Home"));
    QVERIFY(root->property("preparedRoutes").toStringList().contains(QStringLiteral("musicHome:lib-1")));
}

void NavigationHistoryTest::restoresPerEntrySeriesSeasonAndAcceptsLaterSelection()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QQuickView view;
    const auto [root, history] = createHistoryProbe(dir, view);
    QVERIFY(root);
    QVERIFY(history);

    QVERIFY(invoke(root, "pushSeries", QStringLiteral("season-a")));
    QTRY_COMPARE(currentItem(history)->property("selectedSeasonId").toString(),
                 QStringLiteral("season-a"));
    QVERIFY(invoke(root, "setSeriesSeason", QStringLiteral("season-b")));
    QVERIFY(invoke(root, "pushRoute", 91));

    const QVariantMap retainedSeries = listProperty(history, "navTrail").at(1).toMap();
    QCOMPARE(retainedSeries.value(QStringLiteral("seasonId")).toString(),
             QStringLiteral("season-b"));

    QVERIFY(invoke(root, "goBack"));
    QTRY_COMPARE(root->property("preparedSeriesSeasonId").toString(),
                 QStringLiteral("season-b"));
    QTRY_COMPARE(currentItem(history)->property("selectedSeasonId").toString(),
                 QStringLiteral("season-b"));

    // Put the route in Forward so its live graph is gone, then reconstruct it
    // from the bounded scalar descriptor.
    QVERIFY(invoke(root, "goBack"));
    QVERIFY(invoke(root, "goForward"));
    QTRY_COMPARE(currentItem(history)->property("selectedSeasonId").toString(),
                 QStringLiteral("season-b"));

    // A season picked after restoration is the new route state; the restored
    // identity is not a permanent lock on subsequent user choices.
    QVERIFY(invoke(root, "setSeriesSeason", QStringLiteral("season-c")));
    QVERIFY(invoke(root, "pushRoute", 92));
    const QVariantMap updatedSeries = listProperty(history, "navTrail").at(1).toMap();
    QCOMPARE(updatedSeries.value(QStringLiteral("seasonId")).toString(),
             QStringLiteral("season-c"));
}

void NavigationHistoryTest::productionRetargetOrderingRetainsDepartingScopes()
{
    // Pin the real Main call sites to the same capture-prepare-push transaction
    // the behavioral probe below exercises. This catches a production reorder
    // rather than proving only the stack API.
    QFile main(QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/Main.qml"));
    QVERIFY(main.open(QIODevice::ReadOnly));
    const QByteArray source = main.readAll();
    // The trailing "(" matters: without it these are plain substring searches,
    // so a helper named openMusicGenreFromPage declared before openMusicGenre
    // would start the openMusicGenre slice at the WRONG function and swallow
    // whatever lies between, inverting the orderings asserted below. Matching
    // up to the parameter list makes the name exact and kills that whole class
    // of silent mis-slicing for every future openXSomething.
    const auto functionBody = [&source](const QByteArray &name, const QByteArray &next) {
        const qsizetype begin = source.indexOf("function " + name + "(");
        const qsizetype end = source.indexOf("function " + next + "(", begin + 1);
        return begin >= 0 && end > begin ? source.mid(begin, end - begin) : QByteArray{};
    };
    const QByteArray seriesBody = functionBody("openSeries", "openFavorites");
    QVERIFY(!seriesBody.isEmpty());
    const qsizetype seriesCapture = seriesBody.indexOf("root.capturePageDeparture");
    const qsizetype seriesPrepare = seriesBody.indexOf("SeriesCtl.open");
    const qsizetype seriesPush = seriesBody.indexOf("root.pushCapturedPage");
    QVERIFY(seriesCapture >= 0);
    QVERIFY(seriesPrepare >= 0);
    QVERIFY(seriesPush >= 0);
    QVERIFY(seriesCapture < seriesPrepare);
    QVERIFY(seriesPrepare < seriesPush);
    // A music library lands on its Home; Home's strip and bins open Browse.
    const QByteArray libraryBody = functionBody("openLibrary", "openMusicHome");
    QVERIFY(!libraryBody.isEmpty());
    QVERIFY(libraryBody.contains("root.openMusicHome(libraryId, name)"));
    QVERIFY(!libraryBody.contains("MusicCtl."));

    const QByteArray homeBody = functionBody("openMusicHome", "openPlaylists");
    QVERIFY(!homeBody.isEmpty());
    const qsizetype homeCapture = homeBody.indexOf("root.capturePageDeparture");
    const qsizetype homeOpen = homeBody.indexOf("MusicHomeCtl.open");
    const qsizetype homePush = homeBody.indexOf("root.pushCapturedPage");
    QVERIFY(homeCapture >= 0);
    QVERIFY(homeOpen >= 0);
    QVERIFY(homePush >= 0);
    QVERIFY(homeCapture < homeOpen);
    QVERIFY(homeOpen < homePush);

    // Capture the departing route before the shared controller moves, and
    // push only once it has: the page is built against the new scope.
    const QByteArray browseBody = functionBody("openMusicBrowse", "openMusicGenre");
    QVERIFY(!browseBody.isEmpty());
    const qsizetype browseCapture = browseBody.indexOf("root.capturePageDeparture");
    const qsizetype browsePrepare = browseBody.lastIndexOf("MusicBrowseCtl.open(");
    const qsizetype browsePush = browseBody.indexOf("root.pushCapturedPage");
    QVERIFY(browseCapture >= 0);
    QVERIFY(browsePrepare >= 0);
    QVERIFY(browsePush >= 0);
    QVERIFY(browseCapture < browsePrepare);
    QVERIFY(browsePrepare < browsePush);
    QVERIFY(browseBody.contains("\"query\": MusicBrowseCtl.routeState"));

    const QByteArray genreBody = functionBody("openMusicGenre", "openSearch");
    QVERIFY(!genreBody.isEmpty());
    const qsizetype genreCapture = genreBody.indexOf("root.capturePageDeparture");
    const qsizetype genrePrepare = genreBody.lastIndexOf("MusicBrowseCtl.openGenre(");
    const qsizetype genrePush = genreBody.indexOf("root.pushCapturedPage");
    QVERIFY(genreCapture >= 0);
    QVERIFY(genrePrepare >= 0);
    QVERIFY(genrePush >= 0);
    QVERIFY(genreCapture < genrePrepare);
    QVERIFY(genrePrepare < genrePush);

    // A liner-notes genre link keeps BOTH branches. root.musicLibraryId() is
    // empty until Music Home or Browse has been visited, so an album or artist
    // opened from Search or a deep link early has no music library to filter,
    // and the link must still reach the generic genre destination rather than
    // doing nothing. Nothing exercises the empty case at runtime — no test
    // builds the shell with both music controllers unscoped — so the branch is
    // pinned here by shape, in the order it has to run in. Like every pin in
    // this test, it cannot tell production code from a comment; it exists to
    // stop the fallback being deleted as dead, not to prove it fires.
    const QByteArray genreFromPage = functionBody("openMusicGenreFromPage", "openSearch");
    QVERIFY(!genreFromPage.isEmpty());
    const qsizetype fromPageScope = genreFromPage.indexOf("root.musicLibraryId()");
    const qsizetype fromPageFilter = genreFromPage.indexOf("root.openMusicGenre(libraryId,");
    const qsizetype fromPageFallback = genreFromPage.indexOf("Actions.browseGenre(genreId,");
    QVERIFY(fromPageScope >= 0);
    QVERIFY(fromPageFilter > fromPageScope);
    QVERIFY(fromPageFallback > fromPageFilter);

    QFile browsePage(QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/pages/MusicBrowsePage.qml"));
    QVERIFY(browsePage.open(QIODevice::ReadOnly));
    const QByteArray browseSource = browsePage.readAll();
    QVERIFY(!browseSource.contains("MusicCtl."));
    for (const QByteArray &lane : {QByteArrayLiteral("albums"), QByteArrayLiteral("artists"),
                                   QByteArrayLiteral("songs"), QByteArrayLiteral("genres"),
                                   QByteArrayLiteral("playlists")}) {
        QVERIFY(browseSource.contains("navigationFocusRefillActive: MusicBrowseCtl." + lane
                                      + "Lane.loading"));
    }
    QVERIFY(!QFile::exists(QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/pages/MusicPage.qml")));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QQuickView view;
    const auto [root, history] = createHistoryProbe(dir, view);
    QVERIFY(root);
    QVERIFY(history);

    QVERIFY(invoke(root, "pushSeries", QStringLiteral("season-a")));
    QVERIFY(invoke(root, "setSeriesSeason", QStringLiteral("season-b")));
    QVERIFY(invoke(root, "openSeriesFromMain", QStringLiteral("series-2")));
    QVariantList trail = listProperty(history, "navTrail");
    QCOMPARE(trail.size(), 3);
    QCOMPARE(trail.at(1).toMap().value(QStringLiteral("seasonId")).toString(),
             QStringLiteral("season-b"));
    QCOMPARE(currentItem(history)->property("controllerSeasonAtCreation").toString(), QString{});

    QVERIFY(invoke(root, "resetRoute", QStringLiteral("between-scopes")));
    const QString departingState = QStringLiteral("{\"v\":1,\"fav\":true}");
    QVERIFY(invoke(root, "pushMusicBrowse", QStringLiteral("albums")));
    QVERIFY(invoke(root, "setMusicBrowseSection", QStringLiteral("songs")));
    QVERIFY(invoke(root, "setMusicBrowseState", departingState));
    QVERIFY(invoke(root, "openMusicBrowseFromMain", QStringLiteral("music-2")));
    trail = listProperty(history, "navTrail");
    QCOMPARE(trail.size(), 3);
    QCOMPARE(trail.at(1).toMap().value(QStringLiteral("tab")).toString(),
             QStringLiteral("songs"));
    QCOMPARE(trail.at(1).toMap().value(QStringLiteral("query")).toString(), departingState);
    QCOMPARE(currentItem(history)->property("controllerSectionAtCreation").toString(),
             QStringLiteral("albums"));
}

void NavigationHistoryTest::itemPolicyIsCentralizedAcrossQmlSurfaces()
{
    const auto sourceFor = [](const QString &relativePath) {
        QFile file(QStringLiteral(STRMQT_SOURCE_DIR "/") + relativePath);
        if (!file.open(QIODevice::ReadOnly))
            return QByteArray{};
        return file.readAll();
    };

    const QByteArray menu = sourceFor(QStringLiteral("src/ui/controls/ItemMenu.qml"));
    QVERIFY(!menu.isEmpty());
    QVERIFY(menu.contains("Actions.itemMenuPolicy("));
    QVERIFY(menu.contains("Actions.performItemVerb(verb, item)"));
    QVERIFY(menu.contains("root.removeFromPlaylistRequested(item)"));
    for (const QByteArray &removed :
         {QByteArrayLiteral("function typeOf("), QByteArrayLiteral("function isContainer("),
          QByteArrayLiteral("function collectionTypeFor("),
          QByteArrayLiteral("type === \"Series\""), QByteArrayLiteral("type === \"Episode\""),
          QByteArrayLiteral("Actions.shuffleSeries(")}) {
        QVERIFY2(!menu.contains(removed), removed.constData());
    }

    const QByteArray main = sourceFor(QStringLiteral("src/ui/Main.qml"));
    QVERIFY(!main.isEmpty());
    QVERIFY(main.contains("function onRouteRequested(kind, target)"));
    QVERIFY(main.contains("function openRoute(kind, target)"));
    QVERIFY(main.contains("root.openMusicPlaylist(id, name)"));
    QVERIFY(main.contains("root.openPlaylist(id, name)"));
    QVERIFY(main.contains("AlbumCtl.open(route.id, route.name)"));
    // An artist reopened from history takes the library the ENTRY was pushed
    // under, falling back to today's only when the entry carries none. Reading
    // musicLibraryId() unconditionally here is the bug this replaced.
    //
    // The direction pin below cannot see WHICH fields are passed, and a
    // `contains` on an expression is satisfied by that expression sitting in a
    // comment — measured: prepareRoute passing route.key/route.title, and
    // prepareRoute's artist case deleted with the ternary left in a comment,
    // both passed the whole suite. The call-shape pin is what closes that, so
    // the two assertions are kept together and neither supersedes the other.
    QVERIFY(main.contains("ArtistCtl.open(route.id, route.name,"));
    QVERIFY(main.contains("route.libraryId.length > 0 ? route.libraryId"));
    QVERIFY(main.contains("ArtistCtl.open(id, name, libraryId)"));
    QVERIFY(!main.contains("function openMusic("));
    QVERIFY(!main.contains("item.type"));
    QVERIFY(!main.contains("onDetailsRequested"));
    QVERIFY(!main.contains("onSeriesRequested"));

    const QByteArray details = sourceFor(QStringLiteral("src/ui/pages/DetailsPage.qml"));
    QVERIFY(details.contains("Actions.itemCapabilities(page.item)"));
    QVERIFY(details.contains("Actions.performItemVerb(\"shuffle\", page.item)"));
    QVERIFY(details.contains("Actions.performItemVerb(\"browseCollection\", page.item)"));
    QVERIFY(!details.contains(".type ==="));
    QVERIFY(!details.contains("Actions.shuffleSeries("));
    QVERIFY(!details.contains("Actions.playAll(page.itemId"));

    const QByteArray album = sourceFor(QStringLiteral("src/ui/pages/MusicAlbumPage.qml"));
    QVERIFY(!album.isEmpty());
    QVERIFY(album.contains("Actions.openArtist(AlbumCtl.artistId, AlbumCtl.artist)"));
    QVERIFY(!album.contains("\"type\": \"MusicArtist\""));
    QVERIFY(!album.contains("MusicCtl"));

    const QByteArray mini = sourceFor(QStringLiteral("src/ui/shell/MiniPlayer.qml"));
    QVERIFY(mini.contains("Actions.openArtist(mini.artistId, mini.artistText)"));
    QVERIFY(mini.contains("Actions.openAlbum(mini.albumId, mini.albumText)"));
    QVERIFY(!mini.contains("artistRequested("));
    QVERIFY(!mini.contains("albumRequested("));

    const QByteArray music = sourceFor(QStringLiteral("src/ui/pages/MusicBrowsePage.qml"));
    QVERIFY(!music.isEmpty());
    QVERIFY(music.contains("profile: \"musicBrowse\""));
    QCOMPARE(music.count("musicMenu.popupForItem("), 3);
    QVERIFY(!music.contains("musicMenu.popupFor("));
    QVERIFY(!music.contains("property string mode: \"album\""));
    QVERIFY(music.contains("MusicPlay.playAlbum("));
    QVERIFY(!music.contains("MusicCtl."));

    const QByteArray search = sourceFor(QStringLiteral("src/ui/pages/SearchPage.qml"));
    QVERIFY(!search.contains("function playAlbumResult("));
    QVERIFY(search.contains("page.playResult(page.albumModel, index)"));

    const QByteArray artist = sourceFor(QStringLiteral("src/ui/pages/MusicArtistPage.qml"));
    QVERIFY(!artist.isEmpty());
    QVERIFY(artist.contains("MusicPlay.playAlbum("));
    QVERIFY(!artist.contains("MusicCtl"));

    const QByteArray musicPlaylist = sourceFor(QStringLiteral("src/ui/pages/MusicPlaylistPage.qml"));
    QVERIFY(!musicPlaylist.isEmpty());
    QVERIFY(musicPlaylist.contains(
        "onRemoveFromPlaylistRequested: item => PlaylistCtl.removeItem(item)"));

    const QByteArray playlist = sourceFor(QStringLiteral("src/ui/pages/PlaylistPage.qml"));
    QVERIFY(
        playlist.contains("onRemoveFromPlaylistRequested: item => PlaylistCtl.removeItem(item)"));
}

// Only the visible section is a live tree: five Components, one Loader. The
// swap follows the controller's sectionChanged, after the departing view's
// cursor is captured, and a rebuilt view restores that cursor.
void NavigationHistoryTest::musicBrowsePageInstantiatesOnlyTheActiveSection()
{
    QFile file(QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/pages/MusicBrowsePage.qml"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray source = file.readAll();

    QCOMPARE(source.count("Loader {"), 1);
    for (const QByteArray &component : {QByteArrayLiteral("albumsComponent"),
                                        QByteArrayLiteral("artistsComponent"),
                                        QByteArrayLiteral("songsComponent"),
                                        QByteArrayLiteral("genresComponent"),
                                        QByteArrayLiteral("playlistsComponent")}) {
        QCOMPARE(source.count("id: " + component), 1);
    }
    // SOURCE-SHAPE PIN. The handler's first act is to decline the change while
    // the page is covered — a covered page rebuilding a view over the shared
    // controller model is the fan-out this page was measured causing — and the
    // capture-before-swap ordering now lives in adoptSection(), which both the
    // controller's signal and the covered-page reload go through.
    const qsizetype handler = source.indexOf("function onSectionChanged()");
    QVERIFY(handler >= 0);
    const qsizetype guard = source.indexOf("if (!page.pageShown)", handler);
    QVERIFY(guard > handler);
    QVERIFY(source.indexOf("page.adoptSection();", guard) > guard);

    const qsizetype adopt = source.indexOf("function adoptSection()");
    QVERIFY(adopt >= 0);
    const qsizetype capture = source.indexOf("page.captureActiveView();", adopt);
    const qsizetype swapSection =
        source.indexOf("page.loadedSection = MusicBrowseCtl.section;", adopt);
    QVERIFY(capture > adopt);
    QVERIFY(swapSection > capture);
    QVERIFY(source.contains("onLoaded: Qt.callLater(page.restoreActiveView)"));
    QVERIFY(source.contains(
        "view.restoreNavigationFocus(String(state.identity), Number(state.index))"));
}

void NavigationHistoryTest::musicInteractionContextNamesPagesThatExist()
{
    // Main.qml decides `interactionContext` by matching objectName strings, and
    // every music shortcut on the browse page is gated on the answer being
    // "music". Nothing links the two halves, so a typo on either side silently
    // disables Space / S / L / R on a page that still looks completely right —
    // which is why the objectNames are pinned against their own declarations
    // rather than trusted to stay spelled the same.
    const auto sourceFor = [](const QString &relativePath) {
        QFile file(QStringLiteral(STRMQT_SOURCE_DIR "/") + relativePath);
        if (!file.open(QIODevice::ReadOnly))
            return QByteArray{};
        return file.readAll();
    };

    const QByteArray main = sourceFor(QStringLiteral("src/ui/Main.qml"));
    QVERIFY(!main.isEmpty());

    const qsizetype contextAt = main.indexOf("readonly property string interactionContext:");
    QVERIFY(contextAt >= 0);
    const qsizetype musicAt = main.indexOf("? \"music\" : \"browse\"", contextAt);
    QVERIFY2(musicAt > contextAt,
             "the music branch of interactionContext has moved or changed shape");
    const QByteArray branch = main.mid(contextAt, musicAt - contextAt);

    // Every objectName the branch names must be one Main.qml actually sets.
    const QRegularExpression named(QStringLiteral("objectName === \"([A-Za-z]+)\""));
    auto it = named.globalMatch(QString::fromUtf8(branch));
    QStringList pages;
    while (it.hasNext())
        pages << it.next().captured(1);
    QVERIFY2(pages.contains(QStringLiteral("musicBrowsePage")),
             "the browse page is no longer part of the music interaction context");
    // The loop below only checks names the branch already carries, so a page
    // dropped OUT of the chain passes it vacuously. The Crate pages take the
    // same music shortcuts as Browse, and a missing name here disables them
    // with no warning and no other failing assertion, so each is pinned.
    for (const QString &crate : {QStringLiteral("musicHomePage"), QStringLiteral("albumPage"),
                                 QStringLiteral("artistPage"),
                                 QStringLiteral("musicPlaylistPage")}) {
        QVERIFY2(pages.contains(crate),
                 qPrintable(QStringLiteral("interactionContext no longer names %1, so its music "
                                           "shortcuts are silently dead")
                                .arg(crate)));
    }
    for (const QString &page : std::as_const(pages)) {
        const QByteArray declaration = "objectName: \"" + page.toUtf8() + "\"";
        QVERIFY2(main.contains(declaration),
                 qPrintable(QStringLiteral("interactionContext names %1, which Main.qml never sets")
                                .arg(page)));
    }

    // And the page's own shortcuts must be asking for that same answer.
    const QByteArray browse = sourceFor(QStringLiteral("src/ui/pages/MusicBrowsePage.qml"));
    QVERIFY(!browse.isEmpty());
    QVERIFY(browse.contains("App.interactionContext === \"music\""));
}

void NavigationHistoryTest::searchTrackOwnerRestoresAcrossResultLifecycle()
{
    // Pin the real custom track section. Unlike the other Search sections it is
    // not a StrmRail, so the semantic-owner API has to live on TrackList itself.
    QFile searchPage(QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/pages/SearchPage.qml"));
    QVERIFY(searchPage.open(QIODevice::ReadOnly));
    const QByteArray source = searchPage.readAll();
    for (const QByteArray &queryOverrideContract : {
             QByteArrayLiteral("function cancelResultFocusRestoresForUserQuery()"),
             QByteArrayLiteral("Math.min(page.navSections.length, 32)"),
             QByteArrayLiteral("section[\"cancelNavigationFocusRestore\"]()"),
             QByteArrayLiteral("page.focusRestoreOverrideRequested()"),
             QByteArrayLiteral("onTextEdited: page.applyQuery(searchField.text)"),
             QByteArrayLiteral("onCleared: page.applyQuery(\"\")"),
             QByteArrayLiteral("page.applyQuery(String(chip.name))"),
             QByteArrayLiteral("onActionTriggered: page.applyQuery(\"\")")}) {
        QVERIFY2(source.contains(queryOverrideContract), queryOverrideContract.constData());
    }
    QCOMPARE(source.count(QByteArrayLiteral("SearchCtl.query =")), qsizetype{1});
    QVERIFY(source.count(QByteArrayLiteral("page.applyQuery(\"\")")) >= 3);

    QFile mainPage(QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/Main.qml"));
    QVERIFY(mainPage.open(QIODevice::ReadOnly));
    const QByteArray mainSource = mainPage.readAll();
    QVERIFY(mainSource.contains(
        "onFocusRestoreOverrideRequested: stack.retireFocusRestoreOwnership()"));

    const qsizetype componentBegin = source.indexOf("component TrackList: FocusScope");
    const qsizetype instanceBegin = source.indexOf("            TrackList {", componentBegin + 1);
    QVERIFY(componentBegin >= 0);
    QVERIFY(instanceBegin > componentBegin);
    const QByteArray component = source.mid(componentBegin, instanceBegin - componentBegin);
    for (const QByteArray &contract : {
             QByteArrayLiteral("property string navigationFocusKey"),
             QByteArrayLiteral("readonly property bool navigationFocusRestorePending"),
             QByteArrayLiteral("function navigationFocusSnapshot()"),
             QByteArrayLiteral("function restoreNavigationFocus(identity, index)"),
             QByteArrayLiteral("function cancelNavigationFocusRestore()"),
             QByteArrayLiteral("NavigationFocusRestorer {")}) {
        QVERIFY2(component.contains(contract), contract.constData());
    }
    const QByteArray instance = source.mid(instanceBegin, 1200);
    QVERIFY(instance.contains("navigationFocusKey: \"search-tracks\""));
    QVERIFY(instance.contains("navigationFocusFallbackItem: searchField"));
    QVERIFY(instance.contains("navigationFocusRefillActive: SearchCtl.searching"));

    // Artist top tracks use the same initially-empty active-refill handoff.
    // Visibility follows content, but enabled must remain the default so an
    // active empty owner can consume its terminal edge.
    QFile artistPage(QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/pages/MusicArtistPage.qml"));
    QVERIFY(artistPage.open(QIODevice::ReadOnly));
    const QByteArray artistSource = artistPage.readAll();
    const qsizetype topTracksBegin =
        artistSource.indexOf("navigationFocusKey: \"artist-top-tracks\"");
    const qsizetype topTracksModel =
        artistSource.indexOf("model: ArtistCtl.topTracks", topTracksBegin);
    QVERIFY(topTracksBegin >= 0);
    QVERIFY(topTracksModel > topTracksBegin);
    const QByteArray topTracksOwner =
        artistSource.mid(topTracksBegin, topTracksModel - topTracksBegin);
    QVERIFY(topTracksOwner.contains("navigationFocusRefillActive: ArtistCtl.loading"));
    QVERIFY(topTracksOwner.contains("visible: page.hasTopTracks"));
    QVERIFY(!topTracksOwner.contains("enabled:"));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QQuickView view;
    const auto [root, history] = createHistoryProbe(dir, view);
    QVERIFY(root);
    QVERIFY(history);

    QVERIFY(invoke(root, "seedSearchTracks"));
    QVERIFY(invoke(root, "pushSearch", QStringLiteral("tracks")));
    QTRY_COMPARE(root->property("searchTrackCount").toInt(), 3);
    QVERIFY(invoke(root, "focusSearchTrack", 1));
    QTRY_VERIFY(currentItem(history)->property("trackFocused").toBool());
    QCOMPARE(currentItem(history)->property("focusedTrackIndex").toInt(), 1);

    QVERIFY(invoke(root, "pushRoute", 91));
    const QVariantMap searchRoute = listProperty(history, "navTrail").at(1).toMap();
    const QString locator = history->property("focusMemory")
                                .toMap()
                                .value(QString::number(
                                    searchRoute.value(QStringLiteral("token")).toInt()))
                                .toString();
    QVERIFY(locator.startsWith(
        QStringLiteral("[\"semantic\",\"search-tracks\",\"i:track-b\",1")));

    // Match Main's production order: the controller enters its replacement
    // state and clears the rows before Back reconstructs/uncovers SearchPage.
    // The semantic owner is therefore initially invisible. Its explicit active
    // refill transfers ownership immediately from the stack retry to the
    // control restorer, which keeps it pending until the terminal edge
    // certifies the new model.
    QVERIFY(invoke(root, "setSearchTrackRefill", true));
    QVERIFY(invoke(root, "clearSearchTracks"));
    QVERIFY(invoke(root, "goBack"));
    QTRY_VERIFY(!currentItem(history)->property("trackOwnerVisible").toBool());
    QTRY_VERIFY(currentItem(history)->property("trackRestorePending").toBool());
    QCOMPARE(root->property("searchTrackResolutionCount").toInt(), 0);
    QCOMPARE(history->property("_focusRetryToken").toInt(), -1);
    QCOMPARE(history->property("_pendingSemanticToken").toInt(),
             searchRoute.value(QStringLiteral("token")).toInt());

    QVERIFY(invoke(root, "replaceSearchTracksReordered"));
    QTRY_VERIFY(currentItem(history)->property("trackOwnerVisible").toBool());
    QTRY_VERIFY(currentItem(history)->property("trackRestorePending").toBool());
    QCOMPARE(root->property("searchTrackResolutionCount").toInt(), 0);

    QVERIFY(invoke(root, "setSearchTrackRefill", false));
    QTRY_VERIFY(!currentItem(history)->property("trackRestorePending").toBool());
    QTRY_COMPARE(root->property("searchTrackResolutionCount").toInt(), 1);
    QTRY_VERIFY(currentItem(history)->property("trackFocused").toBool());
    QCOMPARE(currentItem(history)->property("focusedTrackIndex").toInt(), 0);
    QCOMPARE(view.activeFocusItem()->objectName(), QStringLiteral("search-row-track-b"));

    // Editing the query is a user override even when the fallback field is
    // already focused, so there is no focus transition for the stack to infer
    // it from. The page must cancel both halves of semantic ownership before
    // the controller mutation can publish a matching row for the new query.
    QVERIFY(invoke(root, "pushRoute", 92));
    QVERIFY(invoke(root, "setSearchTrackRefill", true));
    QVERIFY(invoke(root, "clearSearchTracks"));
    QVERIFY(invoke(root, "goBack"));
    // Plant the production fallback before the deferred stack restore runs;
    // the subsequent query edit must therefore be observable without relying
    // on another focus transition.
    QVERIFY(invoke(root, "focusSearchTrackOverride"));
    QTRY_VERIFY(currentItem(history)->property("trackRestorePending").toBool());
    QTRY_COMPARE(view.activeFocusItem()->objectName(), QStringLiteral("search-track-fallback"));
    QCOMPARE(history->property("_pendingSemanticToken").toInt(),
             searchRoute.value(QStringLiteral("token")).toInt());
    const int resolutionCountBeforeQueryEdit =
        root->property("searchTrackResolutionCount").toInt();
    QVERIFY(invoke(root, "applySearchQueryFromUser", QStringLiteral("edited query")));
    QCOMPARE(view.activeFocusItem()->objectName(), QStringLiteral("search-track-fallback"));
    QTRY_VERIFY(!currentItem(history)->property("trackRestorePending").toBool());
    QCOMPARE(history->property("_pendingSemanticToken").toInt(), -1);
    QCOMPARE(history->property("_focusRetryToken").toInt(), -1);
    QVERIFY(invoke(root, "replaceSearchTracksReordered"));
    QVERIFY(invoke(root, "setSearchTrackRefill", false));
    QTest::qWait(100);
    QCOMPARE(root->property("searchTrackResolutionCount").toInt(),
             resolutionCountBeforeQueryEdit);
    QCOMPARE(view.activeFocusItem()->objectName(), QStringLiteral("search-track-fallback"));

    // A terminal empty result has no eligible row. Start from the same
    // production ordering so the invisible owner, not the stack retry, owns
    // the terminal edge. It must retire the locator and hand focus to the
    // SearchPage field.
    QVERIFY(invoke(root, "seedSearchTracks"));
    QVERIFY(invoke(root, "focusSearchTrack", 1));
    QTRY_VERIFY(currentItem(history)->property("trackFocused").toBool());
    QVERIFY(invoke(root, "pushRoute", 93));
    QVERIFY(invoke(root, "setSearchTrackRefill", true));
    QVERIFY(invoke(root, "clearSearchTracks"));
    QVERIFY(invoke(root, "goBack"));
    QTRY_VERIFY(currentItem(history)->property("trackRestorePending").toBool());
    QCOMPARE(history->property("_focusRetryToken").toInt(), -1);
    QVERIFY(invoke(root, "setSearchTrackRefill", false));
    QTRY_VERIFY(!currentItem(history)->property("trackRestorePending").toBool());
    QTRY_COMPARE(view.activeFocusItem()->objectName(), QStringLiteral("search-track-fallback"));
    QCOMPARE(history->property("_focusRetryToken").toInt(), -1);
    QCOMPARE(history->property("_pendingSemanticToken").toInt(), -1);

    // A later, unrelated refill may make the old id visible again. The empty
    // terminal retired its locator, so this must not resolve or steal focus.
    const int resolutionCountAfterEmpty = root->property("searchTrackResolutionCount").toInt();
    QVERIFY(invoke(root, "setSearchTrackRefill", true));
    QVERIFY(invoke(root, "replaceSearchTracksReordered"));
    QVERIFY(invoke(root, "setSearchTrackRefill", false));
    QTest::qWait(100);
    QCOMPARE(root->property("searchTrackResolutionCount").toInt(), resolutionCountAfterEmpty);
    QCOMPARE(view.activeFocusItem()->objectName(), QStringLiteral("search-track-fallback"));

    // A user's newer focus choice wins while the replacement is live. A later
    // matching row and terminal edge must not steal focus back.
    QVERIFY(invoke(root, "seedSearchTracks"));
    QVERIFY(invoke(root, "focusSearchTrack", 1));
    QTRY_VERIFY(currentItem(history)->property("trackFocused").toBool());
    QVERIFY(invoke(root, "pushRoute", 94));
    QVERIFY(invoke(root, "setSearchTrackRefill", true));
    QVERIFY(invoke(root, "goBack"));
    QTRY_VERIFY(currentItem(history)->property("trackRestorePending").toBool());
    QVERIFY(invoke(root, "focusSearchTrackOverride"));
    QTRY_COMPARE(view.activeFocusItem()->objectName(), QStringLiteral("search-track-fallback"));
    QTRY_VERIFY(!currentItem(history)->property("trackRestorePending").toBool());
    QVERIFY(invoke(root, "replaceSearchTracksReordered"));
    QVERIFY(invoke(root, "setSearchTrackRefill", false));
    QTest::qWait(100);
    QCOMPARE(view.activeFocusItem()->objectName(), QStringLiteral("search-track-fallback"));

    // A disabled semantic owner remains ineligible even while it reports an
    // active refill. The Artist top-tracks repair removes a redundant
    // content-coupled disable; this negative case ensures the generic stack
    // does not relax the disabled-control rule itself.
    QVERIFY(invoke(root, "seedSearchTracks"));
    QVERIFY(invoke(root, "focusSearchTrack", 1));
    QTRY_VERIFY(currentItem(history)->property("trackFocused").toBool());
    QVERIFY(invoke(root, "pushRoute", 95));
    QVERIFY(invoke(root, "setSearchTrackRefill", true));
    QVERIFY(invoke(root, "clearSearchTracks"));
    QVERIFY(invoke(root, "setSearchTrackOwnerEnabled", false));
    QVERIFY(invoke(root, "goBack"));
    QTRY_VERIFY(!currentItem(history)->property("trackOwnerVisible").toBool());
    QVERIFY(!currentItem(history)->property("trackRestorePending").toBool());
    QTRY_COMPARE(history->property("_focusRetryToken").toInt(),
                 searchRoute.value(QStringLiteral("token")).toInt());
    QVERIFY(invoke(root, "focusSearchTrackOverride"));
    QTRY_COMPARE(history->property("_focusRetryToken").toInt(), -1);
    QVERIFY(invoke(root, "setSearchTrackOwnerEnabled", true));
    QVERIFY(invoke(root, "setSearchTrackRefill", false));
}

void NavigationHistoryTest::restoresVirtualFocusAcrossDelayedRefill()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QQuickView view;
    const auto [root, history] = createHistoryProbe(dir, view);
    QVERIFY(root);
    QVERIFY(history);

    QVERIFY(invoke(root, "pushVirtual", QStringLiteral("library")));
    QTRY_COMPARE(root->property("virtualCount").toInt(), 30);
    QVERIFY(invoke(root, "focusVirtual", 17));
    QTRY_COMPARE(currentItem(history)->property("focusedIndex").toInt(), 17);

    // Back to the still-instantiated virtual page re-prepares its controller,
    // which clears the model and refills it in delayed six-row batches.
    QVERIFY(invoke(root, "pushRoute", 91));
    const QVariantMap virtualRoute = listProperty(history, "navTrail").at(1).toMap();
    const QString virtualLocator =
        history->property("focusMemory")
            .toMap()
            .value(QString::number(virtualRoute.value(QStringLiteral("token")).toInt()))
            .toString();
    QVERIFY(virtualLocator.startsWith(
        QStringLiteral("[\"semantic\",\"virtual-primary\",\"i:row-17\",")));
    QVERIFY(invoke(root, "goBack"));
    // A normal controller response may take longer than the old 400 ms grace.
    // While its explicit loading state is true, no fallback may retire the
    // exact identity or trigger pagination from growing counts.
    QTest::qWait(500);
    QVERIFY(currentItem(history)->property("restorePending").toBool());
    QVERIFY(currentItem(history)->property("focusedIndex").toInt() != 17);
    QTRY_COMPARE(root->property("virtualCount").toInt(), 30);
    QTRY_COMPARE(currentItem(history)->property("focusedIndex").toInt(), 17);

    // Put that page in Forward, destroying its graph, then reconstruct it while
    // the same delayed refill is in progress. Index 17 is outside the initial
    // viewport and does not exist until the third batch.
    QVERIFY(invoke(root, "goBack"));
    QVERIFY(invoke(root, "goForward"));
    QTRY_COMPARE(root->property("virtualCount").toInt(), 30);
    QTRY_COMPARE(currentItem(history)->property("focusedIndex").toInt(), 17);
    QVERIFY(view.activeFocusItem());
}

void NavigationHistoryTest::pendingBackRestoreHonorsUserOverride()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QQuickView view;
    const auto [root, history] = createHistoryProbe(dir, view);
    QVERIFY(root);
    QVERIFY(history);

    QVERIFY(invoke(root, "pushVirtual", QStringLiteral("override")));
    QTRY_COMPARE(root->property("virtualCount").toInt(), 30);
    QVERIFY(invoke(root, "focusVirtual", 17));
    QTRY_COMPARE(currentItem(history)->property("focusedIndex").toInt(), 17);
    QVERIFY(invoke(root, "pushRoute", 91));
    QVERIFY(invoke(root, "goBack"));
    QTRY_VERIFY(currentItem(history)->property("restorePending").toBool());

    // The semantic owner is not focused while its controller refill is live.
    // Moving to an ordinary page control must still retire the stack-owned
    // locator before the exact row arrives.
    QVERIFY(invoke(root, "focusVirtualOverride"));
    QTRY_COMPARE(view.activeFocusItem()->objectName(), QStringLiteral("virtual-override"));
    QTRY_VERIFY(!currentItem(history)->property("restorePending").toBool());
    QTRY_COMPARE(root->property("virtualCount").toInt(), 30);
    QTest::qWait(450);
    QCOMPARE(view.activeFocusItem()->objectName(), QStringLiteral("virtual-override"));
}

void NavigationHistoryTest::progressExtendsRefillWithoutAdmittingStaleRows()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QQuickView view;
    const auto [root, history] = createHistoryProbe(dir, view);
    QVERIFY(root);
    QVERIFY(history);

    QVERIFY(invoke(root, "beginProgressRestore"));
    QTRY_VERIFY(root->property("progressRestorePending").toBool());
    // The retained model already contains the exact identity, but it is not a
    // coherent answer while its controller says replacement is active.
    QCOMPARE(root->property("progressFocusedIndex").toInt(), -1);

    // Each page advances inside the short inactivity window while the complete
    // walk takes several times longer. Total elapsed time must not retire it.
    for (int page = 0; page < 5; ++page) {
        QTest::qWait(80);
        QVERIFY(invoke(root, "appendProgressRow", QStringLiteral("page-%1").arg(page)));
        QVERIFY(root->property("progressRestorePending").toBool());
        QCOMPARE(root->property("progressFocusedIndex").toInt(), -1);
    }

    QVERIFY(invoke(root, "replaceProgressRows"));
    QVERIFY(root->property("progressRestorePending").toBool());
    QCOMPARE(root->property("progressFocusedIndex").toInt(), -1);
    QVERIFY(invoke(root, "finishProgressRestore"));
    QTRY_COMPARE(root->property("progressFocusedIndex").toInt(), 1);
    QTRY_VERIFY(!root->property("progressRestorePending").toBool());

    // A controller that never advances and never publishes a terminal edge is
    // still bounded by inactivity rather than hanging for the page lifetime.
    // Its retained row is not a coherent fallback: leave the virtual owner via
    // its explicit external fallback without focusing any stale index.
    QVERIFY(invoke(root, "beginProgressRestore"));
    QTRY_VERIFY(root->property("progressRestorePending").toBool());
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("progressRestorePending").toBool(), 1000);
    QCOMPARE(root->property("progressFocusedIndex").toInt(), -1);
    QCOMPARE(root->property("progressFallbackCount").toInt(), 1);
}

void NavigationHistoryTest::stableOwnersAndPlaylistIdentitySurviveReorder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QQuickView view;
    const auto [root, history] = createHistoryProbe(dir, view);
    QVERIFY(root);
    QVERIFY(history);

    QVERIFY(invoke(root, "pushTwin"));
    QVERIFY(invoke(root, "focusTwinDuplicate"));
    QTRY_COMPARE(currentItem(history)->property("focusedOwnerKey").toString(),
                 QStringLiteral("twin-b"));
    QTRY_COMPARE(currentItem(history)->property("focusedIndex").toInt(), 1);

    QVERIFY(invoke(root, "pushRoute", 91));
    const QVariantMap twinRoute = listProperty(history, "navTrail").at(1).toMap();
    const QString locator = history->property("focusMemory")
                                .toMap()
                                .value(QString::number(twinRoute.value(QStringLiteral("token")).toInt()))
                                .toString();
    QVERIFY(locator.contains(QStringLiteral("\"twin-b\"")));
    QVERIFY(locator.contains(QStringLiteral("\"p:entry-b\"")));

    // Visit it once while still instantiated, then pop it into Forward so its
    // graph is destroyed. Reorder both same-kind owners and the duplicate media
    // entries before reconstruction.
    QVERIFY(invoke(root, "goBack"));
    QTRY_COMPARE(currentItem(history)->property("focusedOwnerKey").toString(),
                 QStringLiteral("twin-b"));
    QVERIFY(invoke(root, "goBack"));
    QVERIFY(invoke(root, "swapTwinOwnersAndRows"));
    QVERIFY(invoke(root, "goForward"));
    // Reconstructed owners may be Loader-backed and appear after the former
    // two-second object-tree retry window. The route locator must remain live
    // within the bounded controller window.
    QTest::qWait(2100);
    QCOMPARE(currentItem(history)->property("focusedOwnerKey").toString(), QString());
    QTRY_COMPARE(currentItem(history)->property("focusedOwnerKey").toString(),
                 QStringLiteral("twin-b"));
    QTRY_COMPARE(currentItem(history)->property("focusedIndex").toInt(), 0);
}

void NavigationHistoryTest::terminalFallbackAndUserOverride()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QQuickView view;
    const auto [root, history] = createHistoryProbe(dir, view);
    QVERIFY(root);
    QVERIFY(history);

    QVERIFY(invoke(root, "pushVirtual", QStringLiteral("terminal")));
    QTRY_COMPARE(root->property("virtualCount").toInt(), 30);
    QVERIFY(invoke(root, "clearVirtualWithoutRefill"));
    QVERIFY(invoke(root, "appendVirtualRows", 5));
    const int nearEndBeforeRestore = root->property("virtualNearEndCount").toInt();
    QVERIFY(invoke(root, "restoreMissingVirtual", 17));
    QTRY_VERIFY(currentItem(history)->property("restorePending").toBool());

    // Rediscovering the same target must not restart its bounded settle timer.
    for (int repeat = 0; repeat < 4; ++repeat) {
        QTest::qWait(70);
        QVERIFY(invoke(root, "restoreMissingVirtual", 17));
    }
    QTRY_VERIFY(!currentItem(history)->property("restorePending").toBool());
    QCOMPARE(currentItem(history)->property("focusedIndex").toInt(), 4);
    QCOMPARE(root->property("virtualNearEndCount").toInt(), nearEndBeforeRestore);

    // With no eligible row, terminal fallback must leave the empty view and
    // move through the page's ordinary focus chain.
    QVERIFY(invoke(root, "clearVirtualWithoutRefill"));
    QVERIFY(invoke(root, "restoreMissingVirtual", 3));
    QTRY_VERIFY(currentItem(history)->property("restorePending").toBool());
    QTest::qWait(450);
    QTRY_VERIFY(!currentItem(history)->property("restorePending").toBool());
    QVERIFY(view.activeFocusItem());
    QVERIFY(!currentItem(history)->property("emptyViewFocused").toBool());

    // A pointer action inside the same semantic owner is still an explicit
    // override even though neither owner key nor cursor changed.
    QVERIFY(invoke(root, "clearVirtualWithoutRefill"));
    QVERIFY(invoke(root, "restoreMissingVirtual", 3));
    QTRY_VERIFY(currentItem(history)->property("restorePending").toBool());
    QVERIFY(invoke(root, "focusVirtualSameOwnerAction"));
    QTRY_VERIFY(!currentItem(history)->property("restorePending").toBool());
    QVERIFY(view.activeFocusItem());
    const QString sameOwnerFocus = view.activeFocusItem()->objectName();
    QVERIFY(invoke(root, "appendLateVirtual"));
    QTest::qWait(450);
    QCOMPARE(view.activeFocusItem()->objectName(), sameOwnerFocus);

    // The external page override follows the same rule.
    QVERIFY(invoke(root, "restoreMissingVirtual", 3));
    QTRY_VERIFY(currentItem(history)->property("restorePending").toBool());
    QVERIFY(invoke(root, "focusVirtualOverride"));
    QTRY_COMPARE(view.activeFocusItem()->objectName(), QStringLiteral("virtual-override"));
    QTRY_VERIFY(!currentItem(history)->property("restorePending").toBool());
}

void NavigationHistoryTest::pendingRestoreIsNotResurrected()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QQuickView view;
    const auto [root, history] = createHistoryProbe(dir, view);
    QVERIFY(root);
    QVERIFY(history);

    QVERIFY(invoke(root, "pushVirtual", QStringLiteral("cancelled")));
    QVERIFY(invoke(root, "focusVirtual", 17));
    QVERIFY(invoke(root, "pushRoute", 91));
    QVERIFY(invoke(root, "goBack"));
    QTRY_VERIFY(currentItem(history)->property("restorePending").toBool());

    const QVariantMap route = history->property("currentEntry").toMap();
    const QString token = QString::number(route.value(QStringLiteral("token")).toInt());
    QVERIFY(history->property("focusMemory").toMap().contains(token));

    // Leaving while the old locator is still waiting is an explicit
    // cancellation. It must delete, rather than retain, the previous visit's
    // token or a later Back would revive a choice the user abandoned.
    QVERIFY(invoke(root, "pushRoute", 92));
    QVERIFY(!history->property("focusMemory").toMap().contains(token));
    QVERIFY(invoke(root, "goBack"));
    QTest::qWait(50);
    QVERIFY(!currentItem(history)->property("restorePending").toBool());
}

void NavigationHistoryTest::preservesFavoriteStateAcrossReconstruction()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QQuickView view;
    const auto [root, history] = createHistoryProbe(dir, view);
    QVERIFY(root);
    QVERIFY(history);

    QVERIFY(invoke(root, "pushAlbumUnfavorite", 42));
    QVERIFY(invoke(root, "markFavorite", 42));
    QVERIFY(invoke(root, "goBack"));
    QVERIFY(invoke(root, "goForward"));
    QTRY_VERIFY(currentItem(history)
                    ->property("albumItem")
                    .toMap()
                    .value(QStringLiteral("favorite"))
                    .toBool());

    QVERIFY(invoke(root, "goBack"));
    QVERIFY(invoke(root, "pushArtistUnfavorite", 7));
    QVERIFY(invoke(root, "markFavorite", 7));
    QVERIFY(invoke(root, "goBack"));
    QVERIFY(invoke(root, "goForward"));
    QTRY_VERIFY(currentItem(history)
                    ->property("artistItem")
                    .toMap()
                    .value(QStringLiteral("favorite"))
                    .toBool());
}

void NavigationHistoryTest::preservesBaseAndKeepsTransientPagesOutOfHistory()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QQuickView view;
    const auto [root, history] = createHistoryProbe(dir, view);
    QVERIFY(root);
    QVERIFY(history);

    QVERIFY(invoke(root, "resetBase", QStringLiteral("home")));
    for (int id = 1; id <= 3; ++id)
        QVERIFY(invoke(root, "pushRoute", id));
    QVERIFY(invoke(root, "goHome"));
    QCOMPARE(history->property("currentEntry").toMap().value(QStringLiteral("kind")).toString(),
             QStringLiteral("home"));
    QCOMPARE(listProperty(history, "navTrail").size(), 1);
    QCOMPARE(listProperty(history, "navForward").size(), 3);
    QCOMPARE(history->property("retainedRouteCount").toInt(), 4);

    const int retained = history->property("retainedRouteCount").toInt();
    const int graphs = history->property("pageGraphCount").toInt();
    QVERIFY(invoke(root, "pushTransient"));
    QCOMPARE(history->property("retainedRouteCount").toInt(), retained);
    QCOMPARE(history->property("pageGraphCount").toInt(), graphs);
    QCOMPARE(history->property("depth").toInt(), graphs + 1);
    QVERIFY(invoke(root, "popTransient"));

    QVERIFY(invoke(root, "resetBase", QStringLiteral("login")));
    for (int id = 1; id <= 7; ++id)
        QVERIFY(invoke(root, "pushRoute", id));
    QCOMPARE(listProperty(history, "navTrail")
                 .constFirst()
                 .toMap()
                 .value(QStringLiteral("kind"))
                 .toString(),
             QStringLiteral("login"));
    QVERIFY(invoke(root, "goHome"));
    QCOMPARE(history->property("currentEntry").toMap().value(QStringLiteral("kind")).toString(),
             QStringLiteral("login"));
}

void NavigationHistoryTest::audioPlaylistRouteSelectsTheMusicPlaylistPage()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QQuickView view;
    const auto [root, history] = createHistoryProbe(dir, view);
    QVERIFY(root);
    QVERIFY(history);

    QVERIFY(invoke(root, "pushAudioPlaylist", QStringLiteral("pl-a")));
    QTRY_COMPARE(currentItem(history)->objectName(), QStringLiteral("musicPlaylistPage"));
    QCOMPARE(currentItem(history)->property("playlistId").toString(), QStringLiteral("pl-a"));

    QVERIFY(invoke(root, "pushVideoPlaylist", QStringLiteral("pl-v")));
    QTRY_COMPARE(currentItem(history)->objectName(), QStringLiteral("playlistPage"));

    QVERIFY(invoke(root, "goBack"));
    QTRY_COMPARE(currentItem(history)->objectName(), QStringLiteral("musicPlaylistPage"));
    const QVariantMap entry = history->property("currentEntry").toMap();
    QCOMPARE(entry.value(QStringLiteral("kind")).toString(), QStringLiteral("playlist"));
    QCOMPARE(entry.value(QStringLiteral("mode")).toString(), QStringLiteral("audio"));
    QCOMPARE(currentItem(history)->property("playlistId").toString(), QStringLiteral("pl-a"));
    QVERIFY(root->property("preparedRoutes").toStringList().contains(QStringLiteral("playlist:pl-a")));

    // The history key is built by concatenation, so an empty id degenerates to
    // the bare prefix and every audio playlist then shares one entry. Only the
    // source check below has force here: the key this probe pushed is the
    // probe's own literal, so comparing it against itself would assert the
    // fixture, not the production concatenation.
    QFile mainFile(QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/Main.qml"));
    QVERIFY(mainFile.open(QIODevice::ReadOnly));
    const QByteArray mainSource = mainFile.readAll();
    const qsizetype openAt = mainSource.indexOf("function openMusicPlaylist(playlistId, name)");
    QVERIFY(openAt >= 0);
    const qsizetype keyAt = mainSource.indexOf("\"musicPlaylist:\" + playlistId", openAt);
    const qsizetype guardAt = mainSource.indexOf("if (!playlistId)", openAt);
    QVERIFY(keyAt > openAt);
    QVERIFY2(guardAt > openAt && guardAt < keyAt,
             "openMusicPlaylist builds its key before rejecting an empty id");
}

// Task 9's headline decision: the SURFACE a playlist was opened from chooses
// the page, because a generic playlist map carries no reliable media type.
// This evaluates Main.qml's own openRoute body rather than grepping for it —
// a substring assertion is satisfied just as well by the inverted condition.
//
// Scope, so the neighbouring greps are not later deleted as superseded: this
// pins WHICH helper the dispatch chooses, not the arguments it passes. Adding
// or dropping an argument in openRoute leaves this test green and is caught
// only by `main.contains("root.openMusicPlaylist(id, name)")` in
// itemPolicyIsCentralizedAcrossQmlSurfaces. The two are complementary.
void NavigationHistoryTest::playlistRouteFollowsTheSurfaceItWasOpenedFrom()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QQmlEngine engine;
    std::unique_ptr<QObject> owner;
    QObject *probe = createRoutingProbe(dir, engine, owner);
    QVERIFY(probe);

    const auto routed = [&](const QString &context, const QString &kind, const QString &id) {
        probe->setProperty("calls", QVariantList{});
        probe->setProperty("interactionContext", context);
        const bool ok = QMetaObject::invokeMethod(probe, "route", Q_ARG(QVariant, QVariant(kind)),
                                                  Q_ARG(QVariant, QVariant(id)),
                                                  Q_ARG(QVariant, QVariant(QStringLiteral("N"))));
        return ok ? probe->property("calls").toStringList() : QStringList{};
    };

    // From music, an audio playlist becomes the Crate record page.
    QCOMPARE(routed(QStringLiteral("music"), QStringLiteral("playlist"), QStringLiteral("pl-a")),
             QStringList{QStringLiteral("musicPlaylist:pl-a")});
    // From anywhere else it stays the generic two-pane Playlists destination.
    QCOMPARE(routed(QStringLiteral("browse"), QStringLiteral("playlist"), QStringLiteral("pl-a")),
             QStringList{QStringLiteral("playlist:pl-a")});
    // The contexts that are neither, spelled out: an overlay or the player on
    // top must not silently reroute a music surface's playlist.
    QCOMPARE(routed(QStringLiteral("overlay"), QStringLiteral("playlist"), QStringLiteral("pl-a")),
             QStringList{QStringLiteral("playlist:pl-a")});
    QCOMPARE(routed(QStringLiteral("player"), QStringLiteral("playlist"), QStringLiteral("pl-a")),
             QStringList{QStringLiteral("playlist:pl-a")});

    // The surface must not leak into the other kinds: album and artist route
    // the same way from music as from anywhere else.
    QCOMPARE(routed(QStringLiteral("music"), QStringLiteral("album"), QStringLiteral("al-1")),
             QStringList{QStringLiteral("album")});
    QCOMPARE(routed(QStringLiteral("browse"), QStringLiteral("album"), QStringLiteral("al-1")),
             QStringList{QStringLiteral("album")});
    QCOMPARE(routed(QStringLiteral("music"), QStringLiteral("artist"), QStringLiteral("ar-1")),
             QStringList{QStringLiteral("artist")});
    QCOMPARE(routed(QStringLiteral("browse"), QStringLiteral("artist"), QStringLiteral("ar-1")),
             QStringList{QStringLiteral("artist")});
}

// The other route back to an audio playlist: its page graph evicted, so the
// page is rebuilt from the compact route through reconstructedProperties.
// The retained-page path (audioPlaylistRouteSelectsTheMusicPlaylistPage) can
// never reach this — it keeps the instance and its original push properties.
void NavigationHistoryTest::evictedAudioPlaylistIsRebuiltFromItsRoute()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QQuickView view;
    const auto [root, history] = createHistoryProbe(dir, view);
    QVERIFY(root);
    QVERIFY(history);

    QVERIFY(invoke(root, "resetRoute", QStringLiteral("base")));
    QVERIFY(invoke(root, "pushRoute", 1));
    QVERIFY(invoke(root, "pushAudioPlaylist", QStringLiteral("pl-a")));
    QTRY_COMPARE(currentItem(history)->objectName(), QStringLiteral("musicPlaylistPage"));
    QCOMPARE(currentItem(history)->property("playlistId").toString(), QStringLiteral("pl-a"));

    // Push the playlist out of the live window (2 here) so its page graph is
    // evicted. depth settling at the window's width with two newer routes on
    // top is the evidence the graph is gone, not merely covered.
    QVERIFY(invoke(root, "pushRoute", 3));
    QVERIFY(invoke(root, "pushRoute", 4));
    QTRY_COMPARE(history->property("depth").toInt(), 2);

    QVERIFY(invoke(root, "goBack"));
    QVERIFY(invoke(root, "goBack"));
    QTRY_COMPARE(currentItem(history)->objectName(), QStringLiteral("musicPlaylistPage"));

    // Nothing survived of the original instance, so these can only have come
    // from reconstructedProperties rebuilding them off the compact route.
    QCOMPARE(currentItem(history)->property("playlistId").toString(), QStringLiteral("pl-a"));
    QCOMPARE(currentItem(history)->property("playlistName").toString(),
             QStringLiteral("Playlist pl-a"));
    const QVariantMap entry = history->property("currentEntry").toMap();
    QCOMPARE(entry.value(QStringLiteral("mode")).toString(), QStringLiteral("audio"));
    QCOMPARE(entry.value(QStringLiteral("key")).toString(), QStringLiteral("musicPlaylist:pl-a"));

    // A video playlist reconstructed the same way must NOT pick up playlist
    // properties, or the generic page would be handed a record's contract.
    QVERIFY(invoke(root, "pushVideoPlaylist", QStringLiteral("pl-v")));
    QTRY_COMPARE(currentItem(history)->objectName(), QStringLiteral("playlistPage"));
    QVERIFY(invoke(root, "pushRoute", 6));
    QVERIFY(invoke(root, "pushRoute", 7));
    QTRY_COMPARE(history->property("depth").toInt(), 2);
    QVERIFY(invoke(root, "goBack"));
    QVERIFY(invoke(root, "goBack"));
    QTRY_COMPARE(currentItem(history)->objectName(), QStringLiteral("playlistPage"));
}

// An artist is scoped to the music library they were opened in. That library
// has to ride the entry: reading the shell's current one on the way back
// reopened the artist against whichever library had been visited since, and
// after eviction the page came back with no library at all.
void NavigationHistoryTest::artistEntryRetainsTheLibraryItWasOpenedUnder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QQuickView view;
    const auto [root, history] = createHistoryProbe(dir, view);
    QVERIFY(root);
    QVERIFY(history);

    QVERIFY(invoke(root, "resetRoute", QStringLiteral("base")));
    QVERIFY(invoke(root, "pushRoute", 1));
    QVERIFY(invoke2(root, "pushArtistInLibrary", QStringLiteral("ar-1"),
                    QStringLiteral("lib-x")));
    QTRY_COMPARE(currentItem(history)->objectName(), QStringLiteral("artist-ar-1"));
    QCOMPARE(currentItem(history)->property("libraryId").toString(), QStringLiteral("lib-x"));

    // The library must be part of the retained DESCRIPTOR, not merely the live
    // page's properties, or it dies with the page graph.
    QVERIFY(invoke(root, "pushRoute", 3));
    const QVariantMap retained = listProperty(history, "navTrail").at(2).toMap();
    QCOMPARE(retained.value(QStringLiteral("id")).toString(), QStringLiteral("ar-1"));
    QCOMPARE(retained.value(QStringLiteral("libraryId")).toString(), QStringLiteral("lib-x"));

    // Push the artist out of the live window, then walk back: the library can
    // only have come from the route through reconstructedProperties.
    QVERIFY(invoke(root, "pushRoute", 4));
    QTRY_COMPARE(history->property("depth").toInt(), 2);
    QVERIFY(invoke(root, "goBack"));
    QVERIFY(invoke(root, "goBack"));
    QTRY_COMPARE(currentItem(history)->objectName(), QStringLiteral("artist-ar-1"));
    QCOMPARE(currentItem(history)->property("libraryId").toString(), QStringLiteral("lib-x"));
}

QTEST_MAIN(NavigationHistoryTest)
#include "tst_navigation_history.moc"
