#include <QDir>
#include <QFile>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

class NavigationHistoryTest : public QObject
{
    Q_OBJECT

private slots:
    void capsGraphsAndReconstructsMetadata();
    void restoresForwardFocusAndReplacesBranches();
    void restoresPerEntrySearchAndPreparesRouteKinds();
    void restoresPerEntryMusicTab();
    void restoresPerEntrySeriesSeasonAndAcceptsLaterSelection();
    void productionRetargetOrderingRetainsDepartingScopes();
    void searchTrackOwnerRestoresAcrossResultLifecycle();
    void restoresVirtualFocusAcrossDelayedRefill();
    void pendingBackRestoreHonorsUserOverride();
    void progressExtendsRefillWithoutAdmittingStaleRows();
    void stableOwnersAndPlaylistIdentitySurviveReorder();
    void terminalFallbackAndUserOverride();
    void pendingRestoreIsNotResurrected();
    void preservesFavoriteStateAcrossReconstruction();
    void preservesBaseAndKeepsTransientPagesOutOfHistory();
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
    property string musicTab: "albums"
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
    function focusSearchTrack(index): void {
        if (history.currentItem && history.currentItem.focusTrack)
            history.currentItem.focusTrack(Number(index));
    }
    function focusSearchTrackOverride(): void {
        if (history.currentItem && history.currentItem.focusOverride)
            history.currentItem.focusOverride();
    }
    function pushMusic(tab): void {
        root.musicTab = String(tab);
        history.pushRoute({ "kind": "music", "id": "music-1", "name": "Music",
                            "key": "music-1", "title": "Music", "tab": root.musicTab });
    }
    function setMusicTab(tab): void {
        root.musicTab = String(tab);
        if (history.currentItem && history.currentItem.selectedTab !== undefined)
            history.currentItem.selectedTab = root.musicTab;
    }
    // The production Main.qml transaction: capture A, retarget the shared
    // controller to Albums in B, then construct B without re-snapshotting A.
    function openMusicLibraryFromMain(libraryId): void {
        history.rememberFocus();
        root.musicTab = "albums";
        history.pushRoute({ "kind": "music", "id": String(libraryId), "name": "Music B",
                            "key": String(libraryId), "title": "Music B", "tab": "albums" },
                          undefined, true);
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
        else if (route.kind === "music")
            root.musicTab = route.tab;
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

    component ArtistProbe: FocusScope {
        property var artistItem: ({})
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
        property string queryAtCreation: ""
        readonly property string routeId: "search:" + queryAtCreation
        property alias focusA: searchFirst
        property alias focusB: searchSecond
        readonly property int focusedTrackIndex: searchTrackList.currentIndex
        readonly property bool trackRestorePending: searchTrackFocus.pending
        readonly property bool trackFocused: searchTrackList.activeFocus
        readonly property bool trackOwnerVisible: searchTracks.visible
        objectName: routeId
        focus: true

        function focusTrack(index): void { searchTracks._applyNavigationFocus(index); }
        function focusOverride(): void { searchFirst.forceActiveFocus(Qt.TabFocusReason); }

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
            }
            FocusScope {
                id: searchTracks
                property string navigationFocusKey: "search-tracks"
                property Item navigationFocusFallbackItem: searchFirst
                property bool navigationFocusRefillActive: root.searchTrackRefillActive
                readonly property bool navigationFocusRestorePending: searchTrackFocus.pending
                property bool _navigationFocusWriting: false
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

    component MusicProbe: FocusScope {
        property string libraryId: ""
        property string libraryName: ""
        property string initialTab: "albums"
        property string selectedTab: initialTab
        property string controllerTabAtCreation: ""
        objectName: "music-" + selectedTab
        focus: true
        Component.onCompleted: controllerTabAtCreation = root.musicTab
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
    Component { id: artistComponent; ArtistProbe {} }
    Component { id: twinOwnerAComponent; TwinOwnerA {} }
    Component { id: twinOwnerBComponent; TwinOwnerB {} }
    Component { id: personComponent; TwinProbe {} }
    Component { id: libraryComponent; VirtualProbe {} }
    Component { id: searchComponent; SearchProbe {} }
    Component { id: musicComponent; MusicProbe {} }
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
        focusItem: root.Window.window ? root.Window.window.activeFocusItem : null
        currentSearchQuery: root.searchQuery
        currentMusicTab: root.musicTab
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
        initialItem: detailsComponent
        detailsPageComponent: detailsComponent
        albumPageComponent: albumComponent
        artistPageComponent: artistComponent
        personPageComponent: personComponent
        libraryPageComponent: libraryComponent
        searchPageComponent: searchComponent
        musicPageComponent: musicComponent
        seriesPageComponent: seriesComponent
        loginPageComponent: loginComponent
        homePageComponent: homeComponent
        onPrepareRequested: route => root.prepareRoute(route)
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
        QStringLiteral("StrmGrid.qml"),
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
                 "StrmGrid 1.0 StrmGrid.qml\n");
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
    QCOMPARE(history->property("pageGraphCount").toInt(), 1);
    QCOMPARE(history->property("depth").toInt(), 1);
    QVERIFY(history->property("focusMemoryCount").toInt() <= 4);
    QTRY_VERIFY(root->property("destroyedCount").toInt() > destroyedBeforeOverflow);
    QCOMPARE(root->property("createdCount").toInt() - root->property("destroyedCount").toInt(), 1);

    const QVariantMap retained = listProperty(history, "navTrail").constLast().toMap();
    QCOMPARE(retained.value(QStringLiteral("id")).toString(), QStringLiteral("7"));
    QCOMPARE(retained.value(QStringLiteral("posterUrl")).toString(), QStringLiteral("poster://7"));
    QCOMPARE(retained.value(QStringLiteral("overview")).toString(), QStringLiteral("Overview 7"));
    QCOMPARE(retained.value(QStringLiteral("year")).toInt(), 2007);
    QVERIFY(!retained.contains(QStringLiteral("arbitraryMap")));
    QVERIFY(!retained.contains(QStringLiteral("prepare")));
    for (const QVariant &value : history->property("focusMemory").toMap())
        QCOMPARE(value.metaType().id(), QMetaType::QString);

    // Route 6 no longer has a page graph. Back must reconstruct the honest
    // Details header from the retained scalar DTO and re-arm its controller.
    QVERIFY(invoke(root, "goBack"));
    QTRY_COMPARE(currentItem(history)->property("routeId").toString(), QStringLiteral("6"));
    const QVariantMap details = currentItem(history)->property("item").toMap();
    QCOMPARE(details.value(QStringLiteral("posterUrl")).toString(), QStringLiteral("poster://6"));
    QCOMPARE(details.value(QStringLiteral("backdropUrl")).toString(),
             QStringLiteral("backdrop://6"));
    QCOMPARE(details.value(QStringLiteral("overview")).toString(), QStringLiteral("Overview 6"));
    QCOMPARE(details.value(QStringLiteral("year")).toInt(), 2006);
    QCOMPARE(details.value(QStringLiteral("officialRating")).toString(), QStringLiteral("PG-6"));
    QCOMPARE(details.value(QStringLiteral("communityRating")).toDouble(), 8.25);
    QVERIFY(details.value(QStringLiteral("resumable")).toBool());
    QCOMPARE(details.value(QStringLiteral("positionMs")).toLongLong(), 1234);
    QCOMPARE(root->property("preparedDetailsId").toString(), QStringLiteral("6"));

    // Put an Album inside the retained tail, force another whole-stack
    // eviction, then walk back to it. Its header DTO must survive too.
    QVERIFY(invoke(root, "resetRoute", QStringLiteral("base")));
    QVERIFY(invoke(root, "pushRoute", 1));
    QVERIFY(invoke(root, "pushAlbum", 42));
    QVERIFY(invoke(root, "pushRoute", 3));
    QVERIFY(invoke(root, "pushRoute", 4));
    QTRY_COMPARE(history->property("depth").toInt(), 1);
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
    QCOMPARE(history->property("pageGraphCount").toInt(), 3);
    QCOMPARE(history->property("depth").toInt(), 3);
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

void NavigationHistoryTest::restoresPerEntryMusicTab()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QQuickView view;
    const auto [root, history] = createHistoryProbe(dir, view);
    QVERIFY(root);
    QVERIFY(history);

    QVERIFY(invoke(root, "pushMusic", QStringLiteral("albums")));
    QVERIFY(invoke(root, "setMusicTab", QStringLiteral("songs")));
    QVERIFY(invoke(root, "pushRoute", 91));

    const QVariantMap retainedMusic = listProperty(history, "navTrail").at(1).toMap();
    QCOMPARE(retainedMusic.value(QStringLiteral("tab")).toString(), QStringLiteral("songs"));

    QVERIFY(invoke(root, "goBack"));
    QTRY_COMPARE(currentItem(history)->property("selectedTab").toString(),
                 QStringLiteral("songs"));
    QVERIFY(invoke(root, "goBack"));
    QVERIFY(invoke(root, "goForward"));
    QTRY_COMPARE(currentItem(history)->property("initialTab").toString(),
                 QStringLiteral("songs"));
    QTRY_COMPARE(currentItem(history)->property("selectedTab").toString(),
                 QStringLiteral("songs"));
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
    const auto functionBody = [&source](const QByteArray &name, const QByteArray &next) {
        const qsizetype begin = source.indexOf("function " + name);
        const qsizetype end = source.indexOf("function " + next, begin + 1);
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
    const QByteArray libraryBody = functionBody("openLibrary", "openPlaylists");
    QVERIFY(!libraryBody.isEmpty());
    const qsizetype musicCapture = libraryBody.indexOf("root.capturePageDeparture");
    const qsizetype musicPrepare = libraryBody.indexOf("MusicCtl.setLibrary");
    const qsizetype musicLoad = libraryBody.indexOf("MusicCtl.loadAlbums");
    const qsizetype musicPush = libraryBody.indexOf("root.pushCapturedPage");
    QVERIFY(musicCapture >= 0);
    QVERIFY(musicPrepare >= 0);
    QVERIFY(musicLoad >= 0);
    QVERIFY(musicPush >= 0);
    QVERIFY(musicCapture < musicPrepare);
    QVERIFY(musicLoad < musicPush);

    QFile musicPage(QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/pages/MusicPage.qml"));
    QVERIFY(musicPage.open(QIODevice::ReadOnly));
    const QByteArray musicSource = musicPage.readAll();
    QCOMPARE(musicSource.count("navigationFocusRefillActive: MusicCtl.loading"), 0);
    for (const QByteArray &lane : {QByteArrayLiteral("albums"), QByteArrayLiteral("artists"),
                                   QByteArrayLiteral("songs"),
                                   QByteArrayLiteral("playlists")}) {
        QVERIFY(musicSource.contains("navigationFocusRefillActive: MusicCtl." + lane
                                     + "Loading"));
    }

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
    QVERIFY(invoke(root, "pushMusic", QStringLiteral("albums")));
    QVERIFY(invoke(root, "setMusicTab", QStringLiteral("songs")));
    QVERIFY(invoke(root, "openMusicLibraryFromMain", QStringLiteral("music-2")));
    trail = listProperty(history, "navTrail");
    QCOMPARE(trail.size(), 3);
    QCOMPARE(trail.at(1).toMap().value(QStringLiteral("tab")).toString(),
             QStringLiteral("songs"));
    QCOMPARE(currentItem(history)->property("controllerTabAtCreation").toString(),
             QStringLiteral("albums"));
}

void NavigationHistoryTest::searchTrackOwnerRestoresAcrossResultLifecycle()
{
    // Pin the real custom track section. Unlike the other Search sections it is
    // not a StrmRail, so the semantic-owner API has to live on TrackList itself.
    QFile searchPage(QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/pages/SearchPage.qml"));
    QVERIFY(searchPage.open(QIODevice::ReadOnly));
    const QByteArray source = searchPage.readAll();
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

    // A terminal empty result has no eligible row. Start from the same
    // production ordering so the invisible owner, not the stack retry, owns
    // the terminal edge. It must retire the locator and hand focus to the
    // SearchPage field.
    QVERIFY(invoke(root, "pushRoute", 92));
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
    QVERIFY(invoke(root, "pushRoute", 93));
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

    // Merely being a semantic control does not make an ordinary hidden owner
    // eligible. Without an explicit active refill, the stack keeps the bounded
    // route retry until a user override cancels it.
    QVERIFY(invoke(root, "seedSearchTracks"));
    QVERIFY(invoke(root, "focusSearchTrack", 1));
    QTRY_VERIFY(currentItem(history)->property("trackFocused").toBool());
    QVERIFY(invoke(root, "pushRoute", 94));
    QVERIFY(invoke(root, "clearSearchTracks"));
    QVERIFY(invoke(root, "goBack"));
    QTRY_VERIFY(!currentItem(history)->property("trackOwnerVisible").toBool());
    QVERIFY(!currentItem(history)->property("trackRestorePending").toBool());
    QTRY_COMPARE(history->property("_focusRetryToken").toInt(),
                 searchRoute.value(QStringLiteral("token")).toInt());
    QVERIFY(invoke(root, "focusSearchTrackOverride"));
    QTRY_COMPARE(history->property("_focusRetryToken").toInt(), -1);
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

QTEST_MAIN(NavigationHistoryTest)
#include "tst_navigation_history.moc"
