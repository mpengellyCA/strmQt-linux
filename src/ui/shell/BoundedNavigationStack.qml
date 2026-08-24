import QtQuick
import QtQuick.Controls.Basic

// Browser-style navigation with a bounded page cache. History records are
// deliberately scalar route descriptors: covered pages may retain rich input
// while instantiated, but a popped or evicted page is reconstructed from its
// stable identity rather than an arbitrary model row or a captured closure.
StackView {
    id: navigation

    property int historyLimit: 40
    property var initialRoute: ({})
    property Item focusItem: null

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

    readonly property int retainedRouteCount: navTrail.length + navForward.length
    readonly property int focusMemoryCount: Object.keys(focusMemory).length
    readonly property int pageGraphCount: instantiatedTokens.length
    readonly property var currentEntry: navTrail.length > 0 ? navTrail[navTrail.length - 1] : null
    readonly property bool canGoBack: navTrail.length > 1
    readonly property bool canGoForward: navForward.length > 0

    signal prepareRequested(var route)

    function scalar(value): string {
        return value === undefined || value === null ? "" : String(value)
    }

    // Whitelist the complete retained shape. Unknown fields, model rows,
    // Components and functions never enter either history array.
    function descriptor(route): var {
        return {
            "token": ++navigation.nextRouteToken,
            "kind": navigation.scalar(route.kind),
            "mode": navigation.scalar(route.mode),
            "id": navigation.scalar(route.id),
            "name": navigation.scalar(route.name),
            "itemType": navigation.scalar(route.itemType),
            "collectionType": navigation.scalar(route.collectionType),
            "key": navigation.scalar(route.key),
            "title": navigation.scalar(route.title)
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
            "type": route.itemType
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

    function forgetFocus(token): void {
        const key = String(token);
        if (navigation.focusMemory[key] === undefined)
            return;
        const next = Object.assign({}, navigation.focusMemory);
        delete next[key];
        navigation.focusMemory = next;
    }

    function rememberFocus(): void {
        const route = navigation.currentEntry;
        const item = navigation.focusItem;
        if (!route || !item)
            return;
        const next = Object.assign({}, navigation.focusMemory);
        next[String(route.token)] = item;
        navigation.focusMemory = next;
    }

    function focusCurrentPage(): void {
        if (navigation.currentItem)
            navigation.currentItem.forceActiveFocus(Qt.OtherFocusReason);
    }

    function restoreFocusToCurrentPage(): void {
        const route = navigation.currentEntry;
        const key = route ? String(route.token) : "";
        const remembered = key.length > 0 ? navigation.focusMemory[key] : null;
        if (key.length > 0)
            navigation.forgetFocus(route.token);
        if (remembered) {
            try {
                if (remembered.visible && remembered.enabled) {
                    remembered.forceActiveFocus(Qt.OtherFocusReason);
                    return;
                }
            } catch (err) {
                // Its cached page was evicted; the reconstructed page is the
                // safe focus fallback below.
            }
        }
        navigation.focusCurrentPage();
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
        const entry = navigation.descriptor(route);
        navigation.navTrail = [entry];
        navigation.navForward = [];
        navigation.instantiatedTokens = [entry.token];
        navigation.clearFocusMemory();
    }

    function resetToRoute(route): void {
        const entry = navigation.descriptor(route);
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

        const entry = navigation.descriptor(route);
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
            navigation.clearFocusMemory();
        }

        navigation.pushResolved(entry, initialProperties, StackView.Immediate);
        Qt.callLater(navigation.focusCurrentPage);
    }

    function goBack(): void {
        if (!navigation.canGoBack)
            return;

        const leaving = navigation.navTrail[navigation.navTrail.length - 1];
        navigation.navTrail = navigation.navTrail.slice(0, -1);
        navigation.navForward = [leaving].concat(navigation.navForward);
        navigation.forgetFocus(leaving.token);

        const target = navigation.currentEntry;
        if (navigation.instantiatedTokens.length > 1) {
            navigation.pop(StackView.Immediate);
            navigation.instantiatedTokens = navigation.instantiatedTokens.slice(0, -1);
            navigation.prepareRequested(target);
        } else {
            navigation.clear(StackView.Immediate);
            navigation.instantiatedTokens = [];
            navigation.clearFocusMemory();
            navigation.prepareRequested(target);
            navigation.pushResolved(target, null, StackView.Immediate);
        }
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
        Qt.callLater(navigation.focusCurrentPage);
    }

    function goHome(): void {
        if (navigation.navTrail.length < 2) {
            Qt.callLater(navigation.restoreFocusToCurrentPage);
            return;
        }

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
            const remembered = navigation.focusMemory[String(home.token)];
            const keptFocus = ({});
            if (remembered)
                keptFocus[String(home.token)] = remembered;
            navigation.focusMemory = keptFocus;
        } else {
            navigation.clear(StackView.Immediate);
            navigation.instantiatedTokens = [];
            navigation.clearFocusMemory();
            navigation.pushResolved(home, null, StackView.Immediate);
        }
        Qt.callLater(navigation.restoreFocusToCurrentPage);
    }

    Component.onCompleted: navigation.adoptInitialRoute(navigation.initialRoute)
}
