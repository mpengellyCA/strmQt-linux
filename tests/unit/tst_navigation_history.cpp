#include <QFile>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>
#include <QTest>

namespace {

const char *kProbe = R"QML(
import QtQuick
import QtQuick.Controls.Basic
import "."

Item {
    id: root
    width: 480
    height: 320
    focus: true

    property int createdCount: 0
    property int destroyedCount: 0
    property var destroyedIds: []
    property var preparedIds: []

    function pushRoute(id): void {
        history.pushRoute({
            "kind": "details",
            "id": String(id),
            "name": "Item " + id,
            "itemType": "Movie",
            "key": "details:" + id,
            "title": "Title " + id,
            // Neither field is part of the retained descriptor whitelist.
            "arbitraryMap": { "large": "not history" },
            "prepare": () => root.preparedIds.push("closure")
        }, { "item": { "itemId": String(id), "name": "Item " + id, "type": "Movie" } });
    }

    function goBack(): void { history.goBack(); }
    function goForward(): void { history.goForward(); }
    function goHome(): void { history.goHome(); }
    function resetRoute(id): void {
        history.resetToRoute({
            "kind": "details", "id": String(id), "name": "Session " + id,
            "itemType": "Movie", "key": "details:" + id, "title": "Session " + id
        });
    }

    Component {
        id: pageComponent

        FocusScope {
            property var item: ({ "itemId": "0", "name": "Item 0", "type": "Movie" })
            readonly property string routeId: String(item.itemId)
            objectName: "page-" + routeId
            focus: true

            Component.onCompleted: root.createdCount += 1
            Component.onDestruction: {
                root.destroyedCount += 1;
                root.destroyedIds = root.destroyedIds.concat([routeId]);
            }

            TextInput {
                objectName: "focus-" + parent.routeId
                text: parent.item.name
                focus: true
            }
        }
    }

    BoundedNavigationStack {
        id: history
        objectName: "history"
        anchors.fill: parent
        historyLimit: 4
        focusItem: root.Window.window ? root.Window.window.activeFocusItem : null
        initialRoute: ({
            "kind": "details", "id": "0", "name": "Item 0", "itemType": "Movie",
            "key": "details:0", "title": "Title 0"
        })
        initialItem: pageComponent
        detailsPageComponent: pageComponent
        onPrepareRequested: route => {
            root.preparedIds = root.preparedIds.concat([route.id]);
        }
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

} // namespace

class NavigationHistoryTest : public QObject
{
    Q_OBJECT

private slots:
    void capsGraphsAndDescriptorsThenReconstructs();
};

void NavigationHistoryTest::capsGraphsAndDescriptorsThenReconstructs()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QQuickView view;
    QObject *root = createProbe(dir, view);
    QVERIFY2(root, qPrintable(view.errors().isEmpty() ? QStringLiteral("failed to create probe")
                                                     : view.errors().first().toString()));
    QObject *history = root->findChild<QObject *>(QStringLiteral("history"));
    QVERIFY(history);
    QTRY_COMPARE(root->property("createdCount").toInt(), 1);

    // A cached page retains exact focus, and Back restores it as one
    // navigation transaction before any eviction has occurred.
    auto *initialFocus = root->findChild<QQuickItem *>(QStringLiteral("focus-0"));
    QVERIFY(initialFocus);
    initialFocus->forceActiveFocus(Qt::OtherFocusReason);
    QTRY_COMPARE(view.activeFocusItem(), initialFocus);
    QVERIFY(invoke(root, "pushRoute", 1));
    QTRY_COMPARE(history->property("focusMemoryCount").toInt(), 1);
    QVERIFY(invoke(root, "goBack"));
    QTRY_VERIFY(view.activeFocusItem());
    QTRY_COMPARE(view.activeFocusItem()->objectName(), QStringLiteral("focus-0"));

    QVERIFY(invoke(root, "resetRoute", QStringLiteral("0")));
    QTRY_COMPARE(history->property("retainedRouteCount").toInt(), 1);
    QTRY_COMPARE(history->property("pageGraphCount").toInt(), 1);

    const int createdBeforeOverflow = root->property("createdCount").toInt();
    const int destroyedBeforeOverflow = root->property("destroyedCount").toInt();
    for (int id = 1; id <= 7; ++id)
        QVERIFY(invoke(root, "pushRoute", id));

    QTRY_COMPARE(history->property("retainedRouteCount").toInt(), 4);
    QCOMPARE(listProperty(history, "navTrail").size(), 4);
    QCOMPARE(listProperty(history, "navForward").size(), 0);
    QCOMPARE(history->property("pageGraphCount").toInt(), 1);
    QCOMPARE(history->property("depth").toInt(), 1);
    QCOMPARE(history->property("focusMemoryCount").toInt(), 0);

    QObject *current = history->property("currentItem").value<QObject *>();
    QVERIFY(current);
    QCOMPARE(current->property("routeId").toString(), QStringLiteral("7"));

    const QVariantMap retained = listProperty(history, "navTrail").constLast().toMap();
    QCOMPARE(retained.value(QStringLiteral("id")).toString(), QStringLiteral("7"));
    QCOMPARE(retained.value(QStringLiteral("key")).toString(), QStringLiteral("details:7"));
    QCOMPARE(retained.value(QStringLiteral("title")).toString(), QStringLiteral("Title 7"));
    QVERIFY(!retained.contains(QStringLiteral("arbitraryMap")));
    QVERIFY(!retained.contains(QStringLiteral("prepare")));

    // Crossing the cap destroyed every cached graph; only the current graph is
    // live, and retained predecessors are scalar routes.
    QTRY_VERIFY(root->property("destroyedCount").toInt() > destroyedBeforeOverflow);
    QCOMPARE(root->property("createdCount").toInt()
                 - root->property("destroyedCount").toInt(),
             1);
    QVERIFY(root->property("createdCount").toInt() > createdBeforeOverflow);

    const int createdBeforeBack = root->property("createdCount").toInt();
    const int destroyedBeforeBack = root->property("destroyedCount").toInt();
    QVERIFY(invoke(root, "goBack"));
    QTRY_COMPARE(root->property("createdCount").toInt(), createdBeforeBack + 1);
    QTRY_COMPARE(root->property("destroyedCount").toInt(), destroyedBeforeBack + 1);
    current = history->property("currentItem").value<QObject *>();
    QVERIFY(current);
    QCOMPARE(current->property("routeId").toString(), QStringLiteral("6"));
    QCOMPARE(history->property("retainedRouteCount").toInt(), 4);
    QCOMPARE(listProperty(history, "navForward").size(), 1);
    QVERIFY(listProperty(root, "preparedIds").contains(QStringLiteral("6")));

    QVERIFY(invoke(root, "goForward"));
    QTRY_COMPARE(history->property("pageGraphCount").toInt(), 2);
    current = history->property("currentItem").value<QObject *>();
    QVERIFY(current);
    QCOMPARE(current->property("routeId").toString(), QStringLiteral("7"));
    QCOMPARE(history->property("currentEntry").toMap().value(QStringLiteral("key")).toString(),
             QStringLiteral("details:7"));
    QCOMPARE(history->property("currentEntry").toMap().value(QStringLiteral("title")).toString(),
             QStringLiteral("Title 7"));
    QVERIFY(listProperty(root, "preparedIds").contains(QStringLiteral("7")));

    // The base destination survives intermediate-route eviction. Home destroys
    // the cached suffix, restores its own key/title, and keeps the just-left
    // destination at the front of Forward.
    QVERIFY(invoke(root, "goHome"));
    QTRY_COMPARE(history->property("pageGraphCount").toInt(), 1);
    current = history->property("currentItem").value<QObject *>();
    QVERIFY(current);
    QCOMPARE(current->property("routeId").toString(), QStringLiteral("0"));
    QCOMPARE(history->property("currentEntry").toMap().value(QStringLiteral("key")).toString(),
             QStringLiteral("details:0"));
    QCOMPARE(history->property("currentEntry").toMap().value(QStringLiteral("title")).toString(),
             QStringLiteral("Session 0"));
    QCOMPARE(listProperty(history, "navForward").size(), 3);
    QCOMPARE(history->property("retainedRouteCount").toInt(), 4);

    // An identity boundary destroys all cached pages and clears both branches
    // and every focus reference.
    QVERIFY(invoke(root, "resetRoute", QStringLiteral("session-b")));
    QTRY_COMPARE(history->property("depth").toInt(), 1);
    QCOMPARE(history->property("retainedRouteCount").toInt(), 1);
    QCOMPARE(listProperty(history, "navTrail").size(), 1);
    QCOMPARE(listProperty(history, "navForward").size(), 0);
    QCOMPARE(history->property("focusMemoryCount").toInt(), 0);
    QCOMPARE(history->property("pageGraphCount").toInt(), 1);
    current = history->property("currentItem").value<QObject *>();
    QVERIFY(current);
    QCOMPARE(current->property("routeId").toString(), QStringLiteral("session-b"));
}

QTEST_MAIN(NavigationHistoryTest)
#include "tst_navigation_history.moc"
