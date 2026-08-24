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
    void restoresVirtualFocusAcrossDelayedRefill();
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
    property int refillBatch: 0
    readonly property int virtualCount: virtualRows.count

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

    function refillVirtualRows(): void {
        virtualRows.clear();
        root.refillBatch = 0;
        refillTimer.restart();
    }

    function appendVirtualBatch(): void {
        const start = root.refillBatch * 6;
        const end = Math.min(30, start + 6);
        for (let i = start; i < end; ++i)
            virtualRows.append({ "itemId": "row-" + i, "name": "Row " + i });
        ++root.refillBatch;
        if (end >= 30)
            refillTimer.stop();
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
        objectName: "virtual-page"
        focus: true

        function focusRow(index): void {
            virtualGrid.restoreNavigationFocus("row-" + index, index);
        }

        StrmGrid {
            id: virtualGrid
            width: 220
            height: 90
            gridModel: virtualRows
            cellsAcross: 1
            prefetchThreshold: 0
        }
    }

    component SearchProbe: FocusScope {
        property string queryAtCreation: ""
        readonly property string routeId: "search:" + queryAtCreation
        property alias focusA: searchFirst
        property alias focusB: searchSecond
        objectName: routeId
        focus: true

        Component.onCompleted: {
            queryAtCreation = root.searchQuery;
            root.createdCount += 1;
        }
        Component.onDestruction: {
            root.destroyedCount += 1;
            root.destroyedIds = root.destroyedIds.concat([routeId]);
        }

        Column {
            TextInput { id: searchFirst; text: parent.parent.queryAtCreation; focus: true }
            TextInput { id: searchSecond; text: "second" }
        }
    }

    Component { id: detailsComponent; DetailsProbe {} }
    Component { id: albumComponent; AlbumProbe {} }
    Component { id: artistComponent; ArtistProbe {} }
    Component { id: libraryComponent; VirtualProbe {} }
    Component { id: searchComponent; SearchProbe {} }
    Component { id: loginComponent; FocusScope { objectName: "login-base"; focus: true } }
    Component { id: homeComponent; FocusScope { objectName: "home-base"; focus: true } }
    Component { id: transientComponent; FocusScope { objectName: "playerPage"; focus: true } }

    ListModel { id: virtualRows }
    Timer {
        id: refillTimer
        interval: 15
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
        libraryPageComponent: libraryComponent
        searchPageComponent: searchComponent
        loginPageComponent: loginComponent
        homePageComponent: homeComponent
        onPrepareRequested: route => root.prepareRoute(route)
    }

    Component.onCompleted: {
        root.refillBatch = 0;
        while (virtualRows.count < 30)
            root.appendVirtualBatch();
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
    QVERIFY(virtualLocator.startsWith(QStringLiteral("[\"semantic\",\"grid\",")));
    QVERIFY(invoke(root, "goBack"));
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
