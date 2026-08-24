#include <QFile>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickView>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "app/controllers/HomeController.h"
#include "server/emby/EmbyClient.h"

using namespace strmqt;

namespace {

const char *kSource = R"QML(
import QtQuick

Item {
    id: root

    width: 640
    height: 480
    property int createdCount: 0
    property int destroyedCount: 0
    property int modelRoleCount: 0

    ListView {
        anchors.fill: parent
        model: HomeCtl.rails
        interactive: false

        delegate: Item {
            required property string railKey
            required property string title
            required property var railModel
            required property bool library
            required property bool wide
            required property string genreId
            width: ListView.view.width
            height: 80
            objectName: "rail-" + railKey

            Component.onCompleted: {
                root.createdCount = root.createdCount + 1
                if (railModel)
                    root.modelRoleCount = root.modelRoleCount + 1
            }
            Component.onDestruction: root.destroyedCount = root.destroyedCount + 1
        }
    }
}
)QML";

MediaItem item(const QString &id)
{
    MediaItem result;
    result.id = id;
    result.name = id;
    result.type = QStringLiteral("Movie");
    return result;
}

} // namespace

class HomeRailsTest : public QObject
{
    Q_OBJECT

private slots:
    void childContentUpdatesKeepRailDelegates();
};

void HomeRailsTest::childContentUpdatesKeepRailDelegates()
{
    emby::EmbyClient client;
    HomeController home(&client);
    home.resume()->setItems({item(QStringLiteral("resume-a"))});
    home.nextUp()->setItems({item(QStringLiteral("next-a"))});
    QCOMPARE(home.rails()->rowCount(), 2);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("HomeRailsProbe.qml"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(kSource);
    file.close();

    QQuickView view;
    view.rootContext()->setContextProperty(QStringLiteral("HomeCtl"), &home);
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.setSource(QUrl::fromLocalFile(path));
    QVERIFY2(view.status() == QQuickView::Ready,
             qPrintable(view.errors().isEmpty() ? QStringLiteral("no root object")
                                                : view.errors().first().toString()));
    QQuickItem *root = view.rootObject();
    QVERIFY(root);
    view.resize(640, 480);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTRY_COMPARE(root->property("createdCount").toInt(), 2);
    QCOMPARE(root->property("modelRoleCount").toInt(), 2);
    QCOMPARE(root->property("destroyedCount").toInt(), 0);

    QSignalSpy resetSpy(home.rails(), &QAbstractItemModel::modelReset);
    QSignalSpy insertedSpy(home.rails(), &QAbstractItemModel::rowsInserted);
    QSignalSpy removedSpy(home.rails(), &QAbstractItemModel::rowsRemoved);
    QSignalSpy movedSpy(home.rails(), &QAbstractItemModel::rowsMoved);
    QSignalSpy changedSpy(home.rails(), &QAbstractItemModel::dataChanged);

    // The child model replaces every card at the same cardinality. Its own
    // modelReset updates that rail, but the vertical descriptor model is quiet.
    home.resume()->setItems({item(QStringLiteral("resume-b"))});
    QCoreApplication::processEvents();
    QCOMPARE(root->property("createdCount").toInt(), 2);
    QCOMPARE(root->property("destroyedCount").toInt(), 0);

    // Even a real 1 -> 2 child count change leaves the set of rails unchanged.
    // HomeController does run its descriptor sync, which must remain a no-op.
    home.resume()->setItems(
        {item(QStringLiteral("resume-c")), item(QStringLiteral("resume-d"))});
    QCoreApplication::processEvents();
    QCOMPARE(home.rails()->rowCount(), 2);
    QCOMPARE(root->property("createdCount").toInt(), 2);
    QCOMPARE(root->property("destroyedCount").toInt(), 0);
    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(insertedSpy.count(), 0);
    QCOMPARE(removedSpy.count(), 0);
    QCOMPARE(movedSpy.count(), 0);
    QCOMPARE(changedSpy.count(), 0);

    // A genuine new rail is one row insertion; both existing delegates survive.
    home.favorites()->setItems({item(QStringLiteral("favorite-a"))});
    QTRY_COMPARE(home.rails()->rowCount(), 3);
    QTRY_COMPARE(root->property("createdCount").toInt(), 3);
    QCOMPARE(root->property("destroyedCount").toInt(), 0);
    QCOMPARE(insertedSpy.count(), 1);
    QCOMPARE(resetSpy.count(), 0);
}

QTEST_MAIN(HomeRailsTest)
#include "tst_home_rails.moc"
