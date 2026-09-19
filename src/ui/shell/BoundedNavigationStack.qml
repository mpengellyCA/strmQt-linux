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

    // How many ROUTES the session remembers. A route is a scalar descriptor of
    // a few hundred bytes, so this is cheap and deliberately generous.
    property int historyLimit: 40
    // How many live PAGE GRAPHS may exist at once — a completely different
    // currency, and the one that was never bounded. A retained page is a whole
    // QML object tree (measured at ~50 MB for MusicBrowsePage, ~48 MB for
    // LibraryPage), so 21 un-backed navigations reached 1.2 GB while
    // historyLimit was nowhere near tripping. 4 is the owner's choice: the page
    // on screen plus three instant Back hops. Back deeper than the window
    // reconstructs its page from the route descriptor, which is exactly what
    // reconstructedProperties() and prepareRequested() already exist for.
    property int livePageLimit: 4
    property var initialRoute: ({})
    property Item focusItem: null
    // SearchController is process-wide while search routes are per-entry. Main
    // binds this to the current controller query so a route captures edits made
    // while its page is active before any navigation transition hides it.
    property string currentSearchQuery: ""
    // MusicBrowseController is process-wide while each retained musicBrowse
    // route owns its section (the tab its semantic focus key belongs to) and
    // its query, as the controller's compact routeState.
    property string currentMusicBrowseSection: "albums"
    property string currentMusicBrowseState: ""
    // SeriesController is likewise process-wide. History retains the stable
    // server season id rather than a row that can move after reconstruction.
    property string currentSeriesSeasonId: ""

    property Component loginPageComponent: null
    property Component homePageComponent: null
    property Component libraryPageComponent: null
    property Component personPageComponent: null
    property Component playlistPageComponent: null
    // Audio playlists open as the Crate page. Chosen by the route's mode, so
    // a retained or reconstructed entry keeps the page it was opened as.
    property Component musicPlaylistPageComponent: null
    property Component artistPageComponent: null
    property Component albumPageComponent: null
    property Component musicBrowsePageComponent: null
    property Component musicHomePageComponent: null
    property Component detailsPageComponent: null
    property Component seriesPageComponent: null
    property Component searchPageComponent: null
    property Component settingsPageComponent: null

    property var navTrail: []
    property var navForward: []
    property var focusMemory: ({})
    // The tokens of the routes that currently have a live page graph, oldest
    // first. Always a contiguous SUFFIX of navTrail: goBack's fast path pops
    // one page and expects the next token down to be the new current entry, so
    // any drift between this and the StackView's contents is a wrong-page bug.
    property var instantiatedTokens: []
    // The page items behind those tokens, index for index. pushResolved creates
    // them itself instead of handing a Component to push(), because StackView
    // only ever destroys what it created — and a page has to be removable from
    // UNDER the pages above it. See trimLivePages().
    property var instantiatedPages: []
    property int nextRouteToken: 0
    property int _focusRetryToken: -1
    property string _focusRetryLocator: ""
    property int _focusRetryAttempts: 0
    // Scalar ownership for a semantic locator that has found its control but is
    // still waiting for that control's coherent model snapshot. Keeping this in
    // the stack closes the interval where the view itself is not focused yet and
    // therefore cannot observe the user focusing a different page control.
    property int _pendingSemanticToken: -1
    property string _pendingSemanticKey: ""
    property bool _pendingSemanticOwnerHadFocus: false

    readonly property int semanticControlLimit: 128
    readonly property int semanticTraversalLimit: 4096
    readonly property int semanticIndexLimit: 2147483646
    readonly property int focusRetryLimit: 400

    // The window can never be wider than the tail the route history keeps
    // CONTIGUOUS. pushRoute's route trim keeps navTrail[0] plus the newest
    // historyLimit-1 entries, so a wider live window would leave a live page
    // whose route is no longer in navTrail — the exact drift described on
    // instantiatedTokens.
    readonly property int liveWindow: Math.max(
                                          1, Math.min(navigation.livePageLimit,
                                                      Math.max(2, navigation.historyLimit) - 1))

    readonly property int retainedRouteCount: navTrail.length + navForward.length
    readonly property int focusMemoryCount: Object.keys(focusMemory).length
    readonly property int pageGraphCount: instantiatedTokens.length
    readonly property var currentEntry: navTrail.length > 0 ? navTrail[navTrail.length - 1] : null
    readonly property bool canGoBack: navTrail.length > 1
    readonly property bool canGoForward: navForward.length > 0

    signal prepareRequested(var route)

    onFocusItemChanged: {
        if (navigation._pendingSemanticToken >= 0) {
            const route = navigation.currentEntry;
            if (!route || Number(route.token) !== navigation._pendingSemanticToken) {
                navigation.cancelLiveFocusRestores();
                return;
            }
            // restoreFocusToCurrentPage() deliberately plants focus on the page
            // before offering a locator. That one unchanged focus location is
            // not a user override.
            if (navigation.focusItem === navigation.currentItem)
                return;
            const pendingOwner = navigation.semanticOwner(navigation.focusItem);
            if (pendingOwner
                    && navigation.semanticKey(pendingOwner) === navigation._pendingSemanticKey) {
                if (pendingOwner["navigationFocusRestorePending"] === true
                        && navigation._pendingSemanticOwnerHadFocus)
                    return;
                if (pendingOwner["navigationFocusRestorePending"] === true) {
                    navigation.cancelLiveFocusRestores();
                    return;
                }
                // The restorer retires its pending flag before it publishes the
                // exact/fallback focus, so this is its own committed focus move.
                navigation.clearPendingSemanticRestore();
                return;
            }
            // Any other focus transition is the user's newer instruction. This
            // includes entering the still-pending owner manually.
            navigation.cancelLiveFocusRestores();
            return;
        }
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
            "tab": navigation.boundedText(route.tab, 16),
            "seasonId": navigation.boundedText(route.seasonId, 1024),
            // The music library an entry was opened UNDER, not the one the
            // shell happens to be in now. Without it, Back to an artist
            // reopened them scoped to whichever library had been visited most
            // recently, so walking A(library X) -> Browse(Y) -> Back refetched
            // A against Y.
            "libraryId": navigation.boundedText(route.libraryId, 1024),

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
        case "playlist":
            return route.mode === "audio" && navigation.musicPlaylistPageComponent !== null
                   ? navigation.musicPlaylistPageComponent
                   : navigation.playlistPageComponent;
        case "artist": return navigation.artistPageComponent;
        case "album": return navigation.albumPageComponent;
        case "musicBrowse": return navigation.musicBrowsePageComponent;
        case "musicHome": return navigation.musicHomePageComponent;
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
        case "artist": return { "artistItem": item, "libraryId": route.libraryId };
        case "album": return { "albumItem": item };
        case "person": return { "personId": route.id, "personName": route.name };
        case "musicBrowse": return { "libraryId": route.id, "libraryName": route.name,
                                     "initialSection": route.tab };
        case "musicHome": return { "libraryId": route.id, "libraryName": route.name };
        case "playlist": return route.mode === "audio"
                                ? { "playlistId": route.id, "playlistName": route.name }
                                : ({});
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
        if (!route)
            return;
        if (route.kind === "search")
            route.query = navigation.boundedText(navigation.currentSearchQuery, 1024);
        else if (route.kind === "musicBrowse") {
            route.tab = navigation.boundedText(navigation.currentMusicBrowseSection, 16);
            route.query = navigation.boundedText(navigation.currentMusicBrowseState, 1024);
        }
        else if (route.kind === "series")
            route.seasonId = navigation.boundedText(navigation.currentSeriesSeasonId, 1024);
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
        if (controls.length === 0
                && typeof navigation.currentItem["prepareNavigationFocusOwner"] === "function") {
            navigation.currentItem["prepareNavigationFocusOwner"](controlKey);
            return false;
        }
        if (controls.length !== 1)
            return false;
        const control = controls[0];
        // An active refill can legitimately make a virtual owner invisible by
        // taking its count to zero. Transfer the locator to that owner's
        // restorer now so it can observe an empty terminal result. Without
        // this handoff the route-level retry can outlive the request and later
        // resurrect a stale target. Invisible controls without an explicit
        // active refill remain ineligible, as do all disabled controls.
        if (!control.enabled
                || (!control.visible
                    && control["navigationFocusRefillActive"] !== true))
            return false;
        const accepted = control["restoreNavigationFocus"](identity, index) === true;
        if (accepted && control["navigationFocusRestorePending"] === true) {
            const route = navigation.currentEntry;
            navigation._pendingSemanticToken = route ? Number(route.token) : -1;
            navigation._pendingSemanticKey = controlKey;
            const focusedOwner = navigation.semanticOwner(navigation.focusItem);
            navigation._pendingSemanticOwnerHadFocus = focusedOwner === control;
        } else if (accepted) {
            navigation.clearPendingSemanticRestore();
        }
        return accepted;
    }

    function clearPendingSemanticRestore(): void {
        navigation._pendingSemanticToken = -1;
        navigation._pendingSemanticKey = "";
        navigation._pendingSemanticOwnerHadFocus = false;
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
        navigation.clearPendingSemanticRestore();
    }

    // A page can cancel its own bounded set of semantic owners without moving
    // focus (for example, editing a query while its fallback field is already
    // focused). Retire the stack half of that ownership explicitly so neither
    // a semantic token nor a route-level retry can outlive the user override.
    function retireFocusRestoreOwnership(): void {
        navigation.cancelFocusRetry();
        navigation.clearPendingSemanticRestore();
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
        const next = Object.assign({}, navigation.focusMemory);
        if (locator.length > 0) {
            next[String(route.token)] = locator;
        } else {
            // A pending locator belongs to the previous visit. Navigating away
            // before it settles is a cancellation, not permission to resurrect
            // the older saved target on the next visit.
            delete next[String(route.token)];
        }
        navigation.focusMemory = next;
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
        navigation.cancelLiveFocusRestores();
        navigation.focusCurrentPage();
        if (navigation.restoreFocusLocator(locator))
            return;
        if (route && locator.length > 0) {
            // Dynamic Home rails may appear only after their controller model
            // arrives. Retry within the same bounded request window; virtual
            // controls take over their own refill wait as soon as one exists.
            navigation.armFocusRetry(route.token, locator);
        }
    }

    // ── Live page ownership ────────────────────────────────────────────────
    // Every page in instantiatedPages is OURS, so every removal has to destroy
    // it here; StackView's pop()/clear() only free what StackView created. The
    // three functions below are the only places that may shorten either array,
    // and each keeps tokens, pages and the StackView's contents in one step.

    function destroyPages(pages): void {
        for (let i = 0; i < pages.length; ++i) {
            if (pages[i])
                pages[i].destroy();
        }
    }

    // True while the StackView's top is the newest page we own — i.e. no
    // transient overlay (the player) is covering it. Every structural operation
    // below is a no-op otherwise rather than removing something it does not own.
    function ownsCurrentItem(): bool {
        const pages = navigation.instantiatedPages;
        return pages.length > 0 && navigation.currentItem === pages[pages.length - 1];
    }

    function popLivePage(): void {
        const pages = navigation.instantiatedPages;
        if (pages.length === 0)
            return;
        if (!navigation.ownsCurrentItem()) {
            console.warn("BoundedNavigationStack: top of stack is not the current route's page");
            return;
        }
        const leaving = pages[pages.length - 1];
        navigation.instantiatedPages = pages.slice(0, -1);
        navigation.instantiatedTokens = navigation.instantiatedTokens.slice(0, -1);
        navigation.pop(StackView.Immediate);
        navigation.destroyPages([leaving]);
    }

    function clearLivePages(): void {
        const pages = navigation.instantiatedPages;
        navigation.instantiatedPages = [];
        navigation.instantiatedTokens = [];
        navigation.clear(StackView.Immediate);
        navigation.destroyPages(pages);
    }

    // Evict the OLDEST live pages until the window fits, keeping the newest
    // tail — and above all keeping the current item untouched.
    //
    // Qt 6.11's StackView has push/pop/replace/clear and no removeItem(), so a
    // page cannot be taken out from under the pages above it. The stack is
    // therefore rebuilt from the retained tail; because those pages are ours,
    // clear() only unparents them and re-pushing them constructs nothing. The
    // current item is briefly deactivated and reactivated within this one call,
    // which is why a page's content gate settles through the event loop instead
    // of reacting to each StackView.status edge.
    function trimLivePages(): void {
        const limit = navigation.liveWindow;
        if (navigation.instantiatedPages.length <= limit)
            return;
        if (!navigation.ownsCurrentItem()) {
            console.warn("BoundedNavigationStack: not trimming under a foreign top item");
            return;
        }
        const drop = navigation.instantiatedPages.length - limit;
        const evicted = navigation.instantiatedPages.slice(0, drop);
        const kept = navigation.instantiatedPages.slice(drop);
        navigation.clear(StackView.Immediate);
        navigation.instantiatedPages = kept;
        navigation.instantiatedTokens = navigation.instantiatedTokens.slice(drop);
        navigation.push(kept, StackView.Immediate);
        navigation.destroyPages(evicted);
    }

    function pushResolved(route, initialProperties, operation): bool {
        const component = navigation.componentFor(route);
        if (!component) {
            console.warn("No page component for route " + route.kind);
            return false;
        }
        const properties = initialProperties !== undefined && initialProperties !== null
                         ? initialProperties : navigation.reconstructedProperties(route);
        // Constructed here rather than by push(component, ...) so this page is
        // ours to evict and to destroy; see the note on instantiatedPages.
        const page = component.createObject(navigation, properties);
        if (!page) {
            console.warn("Could not construct page for route " + route.kind);
            return false;
        }
        navigation.push(page, operation);
        navigation.instantiatedTokens = navigation.instantiatedTokens.concat([route.token]);
        navigation.instantiatedPages = navigation.instantiatedPages.concat([page]);
        // After the push, never before: the page that is being covered stays
        // covered across the rebuild instead of flickering back to Active.
        navigation.trimLivePages();
        return true;
    }

    function adoptInitialRoute(route): void {
        if (navigation.navTrail.length > 0)
            return;
        const entry = navigation.descriptor(route, null);
        navigation.navTrail = [entry];
        navigation.navForward = [];
        navigation.instantiatedTokens = [];
        navigation.instantiatedPages = [];
        navigation.clearFocusMemory();
        // The base page is pushed here rather than through StackView's
        // initialItem, so that every page in the window has one owner and one
        // eviction rule. A StackView-created bottom page would have to be
        // special-cased in all three functions above.
        if (navigation.depth > 0)
            navigation.clear(StackView.Immediate);
        navigation.pushResolved(entry, null, StackView.Immediate);
    }

    function resetToRoute(route): void {
        navigation.cancelFocusRetry();
        navigation.cancelLiveFocusRestores();
        const entry = navigation.descriptor(route, null);
        navigation.clearLivePages();
        navigation.navTrail = [entry];
        navigation.navForward = [];
        navigation.clearFocusMemory();
        navigation.pushResolved(entry, null, StackView.Immediate);
        Qt.callLater(navigation.focusCurrentPage);
    }

    function pushRoute(route, initialProperties, departureCaptured): void {
        // Retargetable global controllers may destroy the departing route's
        // dynamic state before the destination page can be pushed. Main can
        // explicitly remember it first, prepare the new scope, then set this
        // flag so that preparation is not allowed to overwrite the snapshot.
        if (departureCaptured !== true)
            navigation.rememberFocus();
        navigation.navForward = [];

        const entry = navigation.descriptor(route, navigation.retainedItem(initialProperties));
        navigation.navTrail = navigation.navTrail.concat([entry]);

        const limit = Math.max(2, navigation.historyLimit);
        if (navigation.navTrail.length > limit) {
            // Keep the session's base destination (Home or Login) reachable;
            // evict the oldest intermediate route and retain the newest tail.
            // This drops ROUTE DESCRIPTORS only. Page graphs are bounded by the
            // live window instead (liveWindow, trimLivePages), which is why
            // this no longer has to clear the whole StackView to stay honest.
            navigation.navTrail = [navigation.navTrail[0]].concat(
                        navigation.navTrail.slice(navigation.navTrail.length - (limit - 1)));
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
        // Inside the live window this is a pop and the page is already built.
        // Past it — the common case now that the window is four deep — the page
        // is rebuilt from the retained descriptor.
        if (navigation.instantiatedTokens.length > 1) {
            navigation.popLivePage();
            navigation.prepareRequested(target);
        } else {
            navigation.clearLivePages();
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

        // Home usually falls out of the live window long before this runs, in
        // which case it is rebuilt from its descriptor like any other deep Back.
        if (navigation.instantiatedTokens.length > 0
                && navigation.instantiatedTokens[0] === home.token) {
            while (navigation.instantiatedTokens.length > 1) {
                const before = navigation.instantiatedTokens.length;
                navigation.popLivePage();
                // popLivePage refuses to remove a page it does not own; without
                // this the refusal would spin here forever.
                if (navigation.instantiatedTokens.length >= before)
                    break;
            }
        } else {
            navigation.clearLivePages();
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
