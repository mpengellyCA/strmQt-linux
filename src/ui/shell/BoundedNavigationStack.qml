import QtQuick
import QtQuick.Controls.Basic

// Browser-style navigation with a bounded page cache. History records are
// deliberately scalar route descriptors: covered pages may retain rich input
// while instantiated, but a popped or evicted page is reconstructed from a
// strict display snapshot rather than an arbitrary model row or a captured
// closure. Focus is retained as a bounded scalar locator, never a QObject
// reference.
StackView {
    id: navigation

    property int historyLimit: 40
    property var initialRoute: ({})
    property Item focusItem: null
    // SearchController is process-wide while search routes are per-entry. Main
    // binds this to the current controller query so a route captures edits made
    // while its page is active before any navigation transition hides it.
    property string currentSearchQuery: ""

    property Component loginPageComponent: null
    property Component homePageComponent: null
    property Component libraryPageComponent: null
    property Component personPageComponent: null
    property Component playlistPageComponent: null
    property Component artistPageComponent: null
    property Component albumPageComponent: null
    property Component musicPageComponent: null
    property Component detailsPageComponent: null
    property Component seriesPageComponent: null
    property Component searchPageComponent: null
    property Component settingsPageComponent: null

    property var navTrail: []
    property var navForward: []
    property var focusMemory: ({})
    property var instantiatedTokens: []
    property int nextRouteToken: 0
    property int _focusRetryToken: -1
    property string _focusRetryLocator: ""
    property int _focusRetryAttempts: 0

    readonly property int semanticControlLimit: 128
    readonly property int semanticTraversalLimit: 4096
    readonly property int semanticIndexLimit: 2147483646
    readonly property int focusRetryLimit: 40

    readonly property int retainedRouteCount: navTrail.length + navForward.length
    readonly property int focusMemoryCount: Object.keys(focusMemory).length
    readonly property int pageGraphCount: instantiatedTokens.length
    readonly property var currentEntry: navTrail.length > 0 ? navTrail[navTrail.length - 1] : null
    readonly property bool canGoBack: navTrail.length > 1
    readonly property bool canGoForward: navForward.length > 0

    signal prepareRequested(var route)

    onFocusItemChanged: {
        if (navigation._focusRetryToken < 0)
            return;
        // focusCurrentPage() runs before the retry is armed. Afterwards, a
        // change away from the page is a user override and must retire the old
        // locator. The one exception is the requested Loader finally creating
        // and focusing its semantic owner; restore it immediately.
        if (navigation.focusItem === navigation.currentItem)
            return;
        let parts = null;
        try {
            parts = JSON.parse(navigation._focusRetryLocator);
        } catch (error) {
            navigation.cancelFocusRetry();
            return;
        }
        const owner = navigation.semanticOwner(navigation.focusItem);
        if (Array.isArray(parts) && parts.length === 4 && parts[0] === "semantic"
                && owner && navigation.semanticKey(owner) === String(parts[1])
                && navigation.restoreFocusLocator(navigation._focusRetryLocator)) {
            navigation.cancelFocusRetry();
            return;
        }
        navigation.cancelFocusRetry();
    }

    function scalar(value): string {
        return value === undefined || value === null ? "" : String(value)
    }

    function boundedText(value, limit): string {
        const text = navigation.scalar(value);
        if (text.length <= limit)
            return text;
        return text.slice(0, Math.max(0, limit - 1)) + "\u2026";
    }

    function boundedUrl(value): string {
        const text = navigation.scalar(value);
        // A truncated provider/HTTP URL is never useful and can identify the
        // wrong resource. Drop pathological values instead of retaining one.
        return text.length <= 8192 ? text : "";
    }

    function finiteNumber(value, fallback): double {
        const number = Number(value);
        return isFinite(number) ? number : fallback;
    }

    function boundedStrings(values): var {
        const result = [];
        if (values === undefined || values === null)
            return result;
        const count = Math.min(Number(values.length) || 0, 32);
        for (let i = 0; i < count; ++i)
            result.push(navigation.boundedText(values[i], 1024));
        return result;
    }

    function routeOrItem(route, item, key, fallback): var {
        if (route[key] !== undefined && route[key] !== null)
            return route[key];
        if (item && item[key] !== undefined && item[key] !== null)
            return item[key];
        return fallback;
    }

    function retainedItem(initialProperties): var {
        if (initialProperties === undefined || initialProperties === null)
            return null;
        if (initialProperties.item !== undefined)
            return initialProperties.item;
        if (initialProperties.albumItem !== undefined)
            return initialProperties.albumItem;
        if (initialProperties.artistItem !== undefined)
            return initialProperties.artistItem;
        return null;
    }

    // Whitelist the complete retained shape. Unknown fields, model rows,
    // Components and functions never enter either history array.
    function descriptor(route, item): var {
        return {
            "token": ++navigation.nextRouteToken,
            "kind": navigation.boundedText(route.kind, 32),
            "mode": navigation.boundedText(route.mode, 32),
            "id": navigation.boundedText(
                      navigation.routeOrItem(route, item, "id",
                                             navigation.routeOrItem(route, item, "itemId", "")),
                      1024),
            "name": navigation.boundedText(
                        navigation.routeOrItem(route, item, "name", ""), 1024),
            "itemType": navigation.boundedText(
                            navigation.routeOrItem(route, item, "itemType",
                                                   navigation.routeOrItem(route, item, "type", "")),
                            64),
            "collectionType": navigation.boundedText(route.collectionType, 64),
            "key": navigation.boundedText(route.key, 2048),
            "title": navigation.boundedText(route.title, 1024),
            "query": navigation.boundedText(route.query, 1024),

            // Strict, bounded page-header DTO. These are the complete scalar
            // fields Details/Album/Artist consume from their original model
            // row; controller-owned lists remain controller-owned.
            "posterUrl": navigation.boundedUrl(
                             navigation.routeOrItem(route, item, "posterUrl", "")),
            "backdropUrl": navigation.boundedUrl(
                               navigation.routeOrItem(route, item, "backdropUrl", "")),
            "overview": navigation.boundedText(
                            navigation.routeOrItem(route, item, "overview", ""), 32768),
            "year": navigation.finiteNumber(
                        navigation.routeOrItem(route, item, "year", 0), 0),
            "officialRating": navigation.boundedText(
                                  navigation.routeOrItem(route, item, "officialRating", ""), 128),
            "communityRating": navigation.finiteNumber(
                                   navigation.routeOrItem(route, item, "communityRating", 0), 0),
            "resumable": navigation.routeOrItem(route, item, "resumable", false) === true,
            "positionMs": navigation.finiteNumber(
                              navigation.routeOrItem(route, item, "positionMs", 0), 0),
            "runtimeMs": navigation.finiteNumber(
                             navigation.routeOrItem(route, item, "runtimeMs", 0), 0),
            "seriesName": navigation.boundedText(
                              navigation.routeOrItem(route, item, "seriesName", ""), 1024),
            "parentIndexNumber": navigation.finiteNumber(
                                     navigation.routeOrItem(route, item,
                                                            "parentIndexNumber", -1), -1),
            "indexNumber": navigation.finiteNumber(
                               navigation.routeOrItem(route, item, "indexNumber", -1), -1),
            "albumArtist": navigation.boundedText(
                               navigation.routeOrItem(route, item, "albumArtist", ""), 1024),
            "artistIds": navigation.boundedStrings(
                             navigation.routeOrItem(route, item, "artistIds", [])),
            "childCount": navigation.finiteNumber(
                              navigation.routeOrItem(route, item, "childCount", 0), 0),
            "favorite": navigation.routeOrItem(route, item, "favorite", false) === true
        };
    }

    function componentFor(route): Component {
        switch (route.kind) {
        case "login": return navigation.loginPageComponent;
        case "home": return navigation.homePageComponent;
        case "library": return navigation.libraryPageComponent;
        case "person": return navigation.personPageComponent;
        case "playlist": return navigation.playlistPageComponent;
        case "artist": return navigation.artistPageComponent;
        case "album": return navigation.albumPageComponent;
        case "music": return navigation.musicPageComponent;
        case "details": return navigation.detailsPageComponent;
        case "series": return navigation.seriesPageComponent;
        case "search": return navigation.searchPageComponent;
        case "settings": return navigation.settingsPageComponent;
        default: return null;
        }
    }

    // Properties used after a page graph has been evicted. They are derived
    // entirely from the compact route; an initial push may still give its live
    // page a richer display snapshot, which remains bounded with that graph.
    function reconstructedProperties(route): var {
        const item = {
            "itemId": route.id,
            "name": route.name,
            "type": route.itemType,
            "posterUrl": route.posterUrl,
            "backdropUrl": route.backdropUrl,
            "overview": route.overview,
            "year": route.year,
            "officialRating": route.officialRating,
            "communityRating": route.communityRating,
            "resumable": route.resumable,
            "positionMs": route.positionMs,
            "runtimeMs": route.runtimeMs,
            "seriesName": route.seriesName,
            "parentIndexNumber": route.parentIndexNumber,
            "indexNumber": route.indexNumber,
            "albumArtist": route.albumArtist,
            "artistIds": route.artistIds,
            "childCount": route.childCount,
            "favorite": route.favorite
        };
        switch (route.kind) {
        case "details": return { "item": item };
        case "artist": return { "artistItem": item };
        case "album": return { "albumItem": item };
        case "person": return { "personId": route.id, "personName": route.name };
        default: return ({});
        }
    }

    function clearFocusMemory(): void {
        navigation.focusMemory = ({});
    }

    function updateFavorite(itemId, favorite): void {
        const id = navigation.boundedText(itemId, 1024);
        if (id.length === 0)
            return;
        const rewrite = routes => {
            const next = [];
            for (let i = 0; i < routes.length; ++i) {
                const route = routes[i];
                if (route.id === id && route.favorite !== (favorite === true))
                    next.push(Object.assign({}, route, { "favorite": favorite === true }));
                else
                    next.push(route);
            }
            return next;
        };
        navigation.navTrail = rewrite(navigation.navTrail);
        navigation.navForward = rewrite(navigation.navForward);
    }

    function pruneFocusMemory(): void {
        const retained = ({});
        const routes = navigation.navTrail.concat(navigation.navForward);
        for (let i = 0; i < routes.length; ++i) {
            const key = String(routes[i].token);
            if (navigation.focusMemory[key] !== undefined)
                retained[key] = navigation.focusMemory[key];
        }
        navigation.focusMemory = retained;
    }

    function retainCurrentRouteState(): void {
        const route = navigation.currentEntry;
        if (!route || route.kind !== "search")
            return;
        route.query = navigation.boundedText(navigation.currentSearchQuery, 1024);
    }

    function semanticKey(item): string {
        if (!item)
            return "";
        const value = item["navigationFocusKey"];
        return value === undefined || value === null
                ? "" : navigation.boundedText(value, 128);
    }

    function semanticControls(key): var {
        const result = [];
        if (!navigation.currentItem)
            return result;
        const queue = [navigation.currentItem];
        let cursor = 0;
        let visited = 0;
        while (cursor < queue.length && visited < navigation.semanticTraversalLimit
                && result.length < navigation.semanticControlLimit) {
            const item = queue[cursor++];
            ++visited;
            const itemKey = navigation.semanticKey(item);
            if (itemKey.length > 0 && (key.length === 0 || itemKey === key)
                    && typeof item["navigationFocusSnapshot"] === "function"
                    && typeof item["restoreNavigationFocus"] === "function")
                result.push(item);
            const children = item.children;
            if (children === undefined || children === null)
                continue;
            for (let i = 0; i < children.length
                    && queue.length < navigation.semanticTraversalLimit; ++i)
                queue.push(children[i]);
        }
        return result;
    }

    function semanticOwner(item): Item {
        // Virtual delegates expose hover-only/transient action buttons. Their
        // durable keyboard location is the owning view cursor: restoring that
        // item keeps the same card/track selected, while an overlay verb that
        // is no longer visible is deliberately not treated as eligible focus.
        let cursor = item;
        while (cursor && cursor !== navigation.currentItem) {
            if (navigation.semanticKey(cursor).length > 0
                    && typeof cursor["navigationFocusSnapshot"] === "function")
                return cursor;
            cursor = cursor.parent;
        }
        return null;
    }

    function semanticLocator(owner): string {
        if (!owner)
            return "";
        const key = navigation.semanticKey(owner);
        if (!/^[A-Za-z0-9][A-Za-z0-9_.:\/-]{0,127}$/.test(key))
            return "";
        const controls = navigation.semanticControls(key);
        // A duplicate key is not stable. Refuse to remember one rather than
        // silently depending on transient object-tree order.
        if (controls.length !== 1 || controls[0] !== owner)
            return "";
        const snapshot = owner["navigationFocusSnapshot"]();
        const index = snapshot ? Number(snapshot.index) : -1;
        if (!snapshot || snapshot.valid !== true || !Number.isInteger(index)
                || index < 0 || index > navigation.semanticIndexLimit)
            return "";
        const identity = navigation.boundedText(snapshot.identity, 1024);
        return JSON.stringify(["semantic", key, identity, index]);
    }

    function focusLocator(item): string {
        if (!navigation.currentItem)
            return "";

        const owner = navigation.semanticOwner(item);
        if (owner && owner["navigationFocusRestorePending"] === true)
            return "";
        const owned = navigation.semanticLocator(owner);
        if (owned.length > 0)
            return owned;

        if (!item)
            return "";
        if (item === navigation.currentItem)
            return JSON.stringify(["path", "@"]);

        const path = [];
        let cursor = item;
        while (cursor && cursor !== navigation.currentItem) {
            if (path.length >= 256)
                return "";
            const visualParent = cursor.parent;
            if (!visualParent || visualParent.children === undefined)
                return "";
            let childIndex = -1;
            for (let i = 0; i < visualParent.children.length; ++i) {
                if (visualParent.children[i] === cursor) {
                    childIndex = i;
                    break;
                }
            }
            if (childIndex < 0)
                return "";
            path.unshift(childIndex);
            cursor = visualParent;
        }
        return cursor === navigation.currentItem
                ? JSON.stringify(["path", path.join("/")]) : "";
    }

    function itemForFocusPath(pathText): Item {
        if (!navigation.currentItem || pathText.length === 0)
            return null;
        if (pathText === "@")
            return navigation.currentItem;

        let item = navigation.currentItem;
        const path = pathText.split("/");
        if (path.length > 256)
            return null;
        for (let i = 0; i < path.length; ++i) {
            const index = Number(path[i]);
            if (!Number.isInteger(index) || index < 0 || index >= item.children.length)
                return null;
            item = item.children[index];
        }
        return item;
    }

    function restoreFocusLocator(locator): bool {
        if (!navigation.currentItem || locator.length === 0 || locator.length > 8192)
            return false;
        let parts = null;
        try {
            parts = JSON.parse(locator);
        } catch (error) {
            return false;
        }
        if (!Array.isArray(parts) || parts.length !== 2 && parts.length !== 4)
            return false;

        if (parts[0] === "path" && parts.length === 2) {
            const pathText = String(parts[1]);
            if (pathText.length > 4096)
                return false;
            const remembered = navigation.itemForFocusPath(pathText);
            if (remembered && remembered.visible && remembered.enabled) {
                remembered.forceActiveFocus(Qt.OtherFocusReason);
                return true;
            }
            return false;
        }

        if (parts[0] !== "semantic" || parts.length !== 4)
            return false;
        const controlKey = String(parts[1]);
        const identity = String(parts[2]);
        const index = Number(parts[3]);
        if (!/^[A-Za-z0-9][A-Za-z0-9_.:\/-]{0,127}$/.test(controlKey)
                || identity.length > 1024 || !Number.isInteger(index)
                || index < 0 || index > navigation.semanticIndexLimit)
            return false;
        const controls = navigation.semanticControls(controlKey);
        if (controls.length !== 1)
            return false;
        const control = controls[0];
        if (!control.visible || !control.enabled)
            return false;
        return control["restoreNavigationFocus"](identity, index) === true;
    }

    function cancelFocusRetry(): void {
        focusRetry.stop();
        navigation._focusRetryToken = -1;
        navigation._focusRetryLocator = "";
        navigation._focusRetryAttempts = 0;
    }

    function cancelLiveFocusRestores(): void {
        const controls = navigation.semanticControls("");
        for (let i = 0; i < controls.length; ++i) {
            if (typeof controls[i]["cancelNavigationFocusRestore"] === "function")
                controls[i]["cancelNavigationFocusRestore"]();
        }
    }

    function armFocusRetry(token, locator): void {
        navigation._focusRetryToken = Number(token);
        navigation._focusRetryLocator = locator;
        navigation._focusRetryAttempts = 0;
        focusRetry.restart();
    }

    function rememberFocus(): void {
        navigation.retainCurrentRouteState();
        const route = navigation.currentEntry;
        const item = navigation.focusItem;
        if (!route) {
            navigation.cancelFocusRetry();
            navigation.cancelLiveFocusRestores();
            return;
        }
        const locator = navigation.focusLocator(item);
        if (locator.length > 0) {
            const next = Object.assign({}, navigation.focusMemory);
            next[String(route.token)] = locator;
            navigation.focusMemory = next;
        }
        navigation.cancelFocusRetry();
        navigation.cancelLiveFocusRestores();
    }

    function focusCurrentPage(): void {
        if (navigation.currentItem)
            navigation.currentItem.forceActiveFocus(Qt.OtherFocusReason);
    }

    function restoreFocusToCurrentPage(): void {
        const route = navigation.currentEntry;
        const key = route ? String(route.token) : "";
        const locator = key.length > 0 && navigation.focusMemory[key] !== undefined
                      ? String(navigation.focusMemory[key]) : "";
        navigation.cancelFocusRetry();
        if (navigation.restoreFocusLocator(locator))
            return;
        navigation.focusCurrentPage();
        if (route && locator.length > 0) {
            // Dynamic Home rails may appear only after their model arrives.
            // Retry for two seconds; virtual controls take over their own
            // constant-space refill wait as soon as one is found.
            navigation.armFocusRetry(route.token, locator);
        }
    }

    function pushResolved(route, initialProperties, operation): bool {
        const component = navigation.componentFor(route);
        if (!component) {
            console.warn("No page component for route " + route.kind);
            return false;
        }
        const properties = initialProperties !== undefined && initialProperties !== null
                         ? initialProperties : navigation.reconstructedProperties(route);
        navigation.push(component, properties, operation);
        navigation.instantiatedTokens = navigation.instantiatedTokens.concat([route.token]);
        return true;
    }

    function adoptInitialRoute(route): void {
        if (navigation.navTrail.length > 0)
            return;
        const entry = navigation.descriptor(route, null);
        navigation.navTrail = [entry];
        navigation.navForward = [];
        navigation.instantiatedTokens = [entry.token];
        navigation.clearFocusMemory();
    }

    function resetToRoute(route): void {
        navigation.cancelFocusRetry();
        navigation.cancelLiveFocusRestores();
        const entry = navigation.descriptor(route, null);
        navigation.clear(StackView.Immediate);
        navigation.navTrail = [entry];
        navigation.navForward = [];
        navigation.instantiatedTokens = [];
        navigation.clearFocusMemory();
        navigation.pushResolved(entry, null, StackView.Immediate);
        Qt.callLater(navigation.focusCurrentPage);
    }

    function pushRoute(route, initialProperties): void {
        navigation.rememberFocus();
        navigation.navForward = [];

        const entry = navigation.descriptor(route, navigation.retainedItem(initialProperties));
        navigation.navTrail = navigation.navTrail.concat([entry]);

        const limit = Math.max(2, navigation.historyLimit);
        if (navigation.navTrail.length > limit) {
            // Keep the session's base destination (Home or Login) reachable;
            // evict the oldest intermediate route and retain the newest tail.
            navigation.navTrail = [navigation.navTrail[0]].concat(
                        navigation.navTrail.slice(navigation.navTrail.length - (limit - 1)));
            // StackView cannot remove its bottom page without also removing the
            // pages above it. Rebuild only the current destination: all old
            // graphs are destroyed, while retained routes stay reconstructable.
            navigation.clear(StackView.Immediate);
            navigation.instantiatedTokens = [];
        }

        navigation.pruneFocusMemory();

        navigation.pushResolved(entry, initialProperties, StackView.Immediate);
        Qt.callLater(navigation.focusCurrentPage);
    }

    function goBack(): void {
        if (!navigation.canGoBack)
            return;

        navigation.rememberFocus();
        const leaving = navigation.navTrail[navigation.navTrail.length - 1];
        navigation.navTrail = navigation.navTrail.slice(0, -1);
        navigation.navForward = [leaving].concat(navigation.navForward);

        const target = navigation.currentEntry;
        if (navigation.instantiatedTokens.length > 1) {
            navigation.pop(StackView.Immediate);
            navigation.instantiatedTokens = navigation.instantiatedTokens.slice(0, -1);
            navigation.prepareRequested(target);
        } else {
            navigation.clear(StackView.Immediate);
            navigation.instantiatedTokens = [];
            navigation.prepareRequested(target);
            navigation.pushResolved(target, null, StackView.Immediate);
        }
        navigation.pruneFocusMemory();
        Qt.callLater(navigation.restoreFocusToCurrentPage);
    }

    function goForward(): void {
        if (!navigation.canGoForward)
            return;
        navigation.rememberFocus();
        const entry = navigation.navForward[0];
        navigation.navForward = navigation.navForward.slice(1);
        navigation.navTrail = navigation.navTrail.concat([entry]);
        navigation.prepareRequested(entry);
        navigation.pushResolved(entry, null, StackView.Immediate);
        navigation.pruneFocusMemory();
        Qt.callLater(navigation.restoreFocusToCurrentPage);
    }

    function goHome(): void {
        if (navigation.navTrail.length < 2) {
            Qt.callLater(navigation.restoreFocusToCurrentPage);
            return;
        }

        navigation.rememberFocus();
        const home = navigation.navTrail[0];
        navigation.navForward = navigation.navTrail.slice(1).reverse()
                                .concat(navigation.navForward);
        navigation.navTrail = [home];

        if (navigation.instantiatedTokens.length > 0
                && navigation.instantiatedTokens[0] === home.token) {
            while (navigation.instantiatedTokens.length > 1) {
                navigation.pop(StackView.Immediate);
                navigation.instantiatedTokens = navigation.instantiatedTokens.slice(0, -1);
            }
        } else {
            navigation.clear(StackView.Immediate);
            navigation.instantiatedTokens = [];
            navigation.pushResolved(home, null, StackView.Immediate);
        }
        navigation.pruneFocusMemory();
        Qt.callLater(navigation.restoreFocusToCurrentPage);
    }

    Component.onCompleted: navigation.adoptInitialRoute(navigation.initialRoute)

    Timer {
        id: focusRetry
        interval: 50
        repeat: true
        onTriggered: {
            const route = navigation.currentEntry;
            if (!route || Number(route.token) !== navigation._focusRetryToken) {
                navigation.cancelFocusRetry();
                return;
            }
            if (navigation.restoreFocusLocator(navigation._focusRetryLocator)) {
                navigation.cancelFocusRetry();
                return;
            }
            ++navigation._focusRetryAttempts;
            if (navigation._focusRetryAttempts >= navigation.focusRetryLimit)
                navigation.cancelFocusRetry();
        }
    }
}
