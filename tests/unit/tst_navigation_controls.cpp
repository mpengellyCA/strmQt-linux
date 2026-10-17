// The real shelf, grid and tab bar under the real navigator: their edges are
// edges, and what lies past an edge is reached without the control having to
// know what it is (src/input/FocusNavigator.h).

#include "input/FocusNavigator.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>
#include <QtTest>

#include "QmlShimStaging.h"

namespace {

const char *kProbe = R"QML(
import QtQuick
import StrmQt

Item {
    id: root
    width: 1200
    height: 900

    ListModel {
        id: rows
        ListElement { itemId: "a"; name: "A"; label: "A" }
        ListElement { itemId: "b"; name: "B"; label: "B" }
        ListElement { itemId: "c"; name: "C"; label: "C" }
        ListElement { itemId: "d"; name: "D"; label: "D" }
        ListElement { itemId: "e"; name: "E"; label: "E" }
        ListElement { itemId: "f"; name: "F"; label: "F" }
        ListElement { itemId: "g"; name: "G"; label: "G" }
    }

    component Tile: Item {
        property var model
        property int index: -1
        property bool current: false
        property bool hovered: false
        signal activated()
        width: 150
        height: 150
    }

    StrmTabBar {
        id: tabs
        objectName: "tabs"
        x: 200; y: 0
        width: 500
        tabs: [{ label: "One" }, { label: "Two" }, { label: "Three" }]
    }

    Item { objectName: "leftPane"; x: 0; y: 100; width: 100; height: 400; activeFocusOnTab: true }

    // Three across, seven cards: a full row, a full row, and one card.
    StrmGrid {
        id: grid
        objectName: "grid"
        x: 200; y: 100
        width: 480
        height: 500
        gridModel: rows
        cellsAcross: 3
        customCardWidth: 150
        customCardHeight: 150
        cardComponent: Component { Tile {} }
    }

    Item { objectName: "sideColumn"; x: 800; y: 100; width: 300; height: 400; activeFocusOnTab: true }

    StrmRail {
        id: shelf
        objectName: "shelf"
        x: 200; y: 620
        width: 900
        title: "Shelf"
        showMore: true
        railModel: rows
        customCardWidth: 150
        customCardHeight: 150
        cardComponent: Component { Tile {} }
    }

    // The stock card, which a shelf builds when it is given no card of its
    // own. Hidden until its test: an invisible shelf is nowhere an arrow can
    // go, so the geometry the tests above walk is unchanged by it.
    property int stockActivated: -1
    StrmRail {
        id: stock
        objectName: "stock"
        x: 100; y: 100
        z: 10
        width: 1000
        visible: false
        title: "Stock"
        railModel: rows
        onItemActivated: index => root.stockActivated = index
    }

    function showStock(on) { stock.visible = on }
    function focusStock() { stock.forceActiveFocus() }
    function stockIndex() { return stock.currentIndex }

    function gridIndex() { return grid.currentIndex }
    function gridColumns() { return grid.columns }
    function setGridIndex(i) { grid.restoreNavigationCursor(i) }
    function shelfIndex() { return shelf.currentIndex }
    function tabIndex() { return tabs.currentIndex }
    function focusGrid() { grid.forceActiveFocus() }
    function focusTabs() { tabs.forceActiveFocus() }
    function focusShelf() { shelf.forceActiveFocus() }
    function where() {
        const item = Window.activeFocusItem
        for (let p = item; p; p = p.parent) {
            if (p.objectName && p.objectName.length > 0)
                return p.objectName
            if (p.text !== undefined && p.text === "See all")
                return "seeAll"
        }
        return ""
    }
}
)QML";

QQuickItem *createProbe(QTemporaryDir &dir, QQuickView &view)
{
    const QString modulePath = dir.filePath(QStringLiteral("StrmQt"));
    if (!QDir().mkpath(modulePath))
        return nullptr;
    const QStringList moduleFiles = {
        QStringLiteral("Theme.qml"),          QStringLiteral("FocusRing.qml"),
        QStringLiteral("FocusClip.qml"),      QStringLiteral("StrmIcon.qml"),
        QStringLiteral("StrmTooltip.qml"),    QStringLiteral("StrmIconButton.qml"),
        QStringLiteral("StrmButton.qml"),     QStringLiteral("StrmCard.qml"),
        QStringLiteral("StrmImage.qml"),      QStringLiteral("StrmScrollBar.qml"),
        QStringLiteral("NavigationFocusRestorer.qml"),
        QStringLiteral("NavigationColumn.qml"), QStringLiteral("StrmRail.qml"),
        QStringLiteral("StrmGrid.qml"),       QStringLiteral("StrmTabBar.qml"),
    };
    QByteArray lines = "module StrmQt\n"
                       "singleton Theme 1.0 Theme.qml\n"
                       "singleton NavigationColumn 1.0 NavigationColumn.qml\n";
    for (const QString &name : moduleFiles) {
        const QString sourceRoot = name == QStringLiteral("Theme.qml")
                                       ? QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/")
                                       : QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/controls/");
        if (!QFile::copy(sourceRoot + name, modulePath + QLatin1Char('/') + name))
            return nullptr;
        if (name != QStringLiteral("Theme.qml") && name != QStringLiteral("NavigationColumn.qml"))
            lines += name.chopped(4).toUtf8() + " 1.0 " + name.toUtf8() + '\n';
    }
    const QByteArray shimLines = strmqt::test::stageShims(modulePath);
    if (shimLines.isEmpty())
        return nullptr;
    QFile qmldir(modulePath + QStringLiteral("/qmldir"));
    if (!qmldir.open(QIODevice::WriteOnly))
        return nullptr;
    qmldir.write(lines);
    qmldir.write(shimLines);
    qmldir.close();

    QFile probe(dir.filePath(QStringLiteral("Probe.qml")));
    if (!probe.open(QIODevice::WriteOnly))
        return nullptr;
    probe.write(kProbe);
    probe.close();

    view.engine()->addImportPath(dir.path());
    view.setSource(QUrl::fromLocalFile(probe.fileName()));
    if (view.status() != QQuickView::Ready)
        return nullptr;
    view.resize(1200, 900);
    view.show();
    if (!QTest::qWaitForWindowExposed(&view))
        return nullptr;
    return view.rootObject();
}

} // namespace

class NavigationControlsTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void gridWalksInCellsAndDeclinesAtItsEdges();
    void gridShortLastRowIsReachable();
    void tabBarDeclinesAtItsEnds();
    void shelfEndLeadsToSeeAll();
    void shelfBuildsOnlyTheCardItDraws();

private:
    int call(const char *name)
    {
        QVariant result;
        QMetaObject::invokeMethod(m_root, name, Q_RETURN_ARG(QVariant, result));
        return result.toInt();
    }
    QString where()
    {
        QVariant result;
        QMetaObject::invokeMethod(m_root, "where", Q_RETURN_ARG(QVariant, result));
        return result.toString();
    }
    void run(const char *name) { QMetaObject::invokeMethod(m_root, name); }
    void setGridIndex(int index)
    {
        QMetaObject::invokeMethod(m_root, "setGridIndex", Q_ARG(QVariant, index));
    }
    void press(Qt::Key key) { QTest::keyClick(&m_view, key); }

    QTemporaryDir m_dir;
    QQuickView m_view;
    QQuickItem *m_root = nullptr;
};

void NavigationControlsTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_root = createProbe(m_dir, m_view);
    QVERIFY(m_root);
    new strmqt::FocusNavigator(&m_view, &m_view);
    QTRY_COMPARE(call("gridColumns"), 3);
}

void NavigationControlsTest::gridWalksInCellsAndDeclinesAtItsEdges()
{
    run("focusGrid");
    setGridIndex(0);
    press(Qt::Key_Right);
    QCOMPARE(call("gridIndex"), 1);
    press(Qt::Key_Down);
    QCOMPARE(call("gridIndex"), 4);
    press(Qt::Key_Left);
    QCOMPARE(call("gridIndex"), 3);
    QCOMPARE(where(), QStringLiteral("grid"));

    // The first column does not wrap onto the row above: Left is the grid's
    // edge, and what is beside the grid takes it.
    press(Qt::Key_Left);
    QCOMPARE(call("gridIndex"), 3);
    QCOMPARE(where(), QStringLiteral("leftPane"));

    // Nor does the last column wrap onto the row below. Right off it used to
    // be swallowed, which made anything beside a grid unreachable.
    run("focusGrid");
    setGridIndex(2);
    press(Qt::Key_Right);
    QCOMPARE(call("gridIndex"), 2);
    QCOMPARE(where(), QStringLiteral("sideColumn"));

    run("focusGrid");
    setGridIndex(1);
    press(Qt::Key_Up);
    QCOMPARE(call("gridIndex"), 1);
    QCOMPARE(where(), QStringLiteral("tabs"));
}

void NavigationControlsTest::gridShortLastRowIsReachable()
{
    // Card 5 is the end of the second row; the third row holds only card 6,
    // which is not under it. Down has to land there, not do nothing.
    run("focusGrid");
    setGridIndex(5);
    press(Qt::Key_Down);
    QCOMPARE(call("gridIndex"), 6);
    QCOMPARE(where(), QStringLiteral("grid"));
    // And from the last row Down leaves for what is below the grid.
    press(Qt::Key_Down);
    QCOMPARE(call("gridIndex"), 6);
    QCOMPARE(where(), QStringLiteral("shelf"));
}

void NavigationControlsTest::tabBarDeclinesAtItsEnds()
{
    run("focusTabs");
    QCOMPARE(call("tabIndex"), 0);
    press(Qt::Key_Right);
    press(Qt::Key_Right);
    QCOMPARE(call("tabIndex"), 2);
    QCOMPARE(where(), QStringLiteral("tabs"));
    // Past the last tab is not the bar's: the column beside it is next.
    press(Qt::Key_Right);
    QCOMPARE(call("tabIndex"), 2);
    QCOMPARE(where(), QStringLiteral("sideColumn"));

    run("focusTabs");
    press(Qt::Key_Left);
    press(Qt::Key_Left);
    QCOMPARE(call("tabIndex"), 0);
    press(Qt::Key_Left);
    QCOMPARE(call("tabIndex"), 0);
    QCOMPARE(where(), QStringLiteral("leftPane"));
}

void NavigationControlsTest::shelfEndLeadsToSeeAll()
{
    run("focusShelf");
    QCOMPARE(where(), QStringLiteral("shelf"));
    // "See all" fades in once the shelf holds the keyboard.
    QTest::qWait(300);
    for (int i = 0; i < 6; ++i)
        press(Qt::Key_Right);
    QCOMPARE(call("shelfIndex"), 6);
    press(Qt::Key_Right);
    QCOMPARE(where(), QStringLiteral("seeAll"));
    press(Qt::Key_Left);
    QCOMPARE(where(), QStringLiteral("shelf"));
    QCOMPARE(call("shelfIndex"), 6);
}

// Every StrmCard under `from`, the shelf's hidden metrics card included.
static void collectStockCards(QQuickItem *from, QList<QQuickItem *> &out)
{
    if (QByteArray(from->metaObject()->className()).startsWith("StrmCard_QMLTYPE"))
        out.append(from);
    for (QQuickItem *child : from->childItems())
        collectStockCards(child, out);
}

static QQuickItem *stockCardLabelled(QQuickItem *shelf, const QString &label)
{
    QList<QQuickItem *> cards;
    collectStockCards(shelf, cards);
    for (QQuickItem *card : cards) {
        if (card->isVisible() && card->property("label").toString() == label)
            return card;
    }
    return nullptr;
}

void NavigationControlsTest::shelfBuildsOnlyTheCardItDraws()
{
    // A shelf given a card of its own used to build a hidden StrmCard behind
    // every one of them. The only stock card left on it is the one the shelf
    // measures with.
    QQuickItem *shelf = m_root->findChild<QQuickItem *>(QStringLiteral("shelf"));
    QVERIFY(shelf);
    QTRY_COMPARE(shelf->property("count").toInt(), 7);
    QList<QQuickItem *> onCustomShelf;
    collectStockCards(shelf, onCustomShelf);
    QCOMPARE(onCustomShelf.size(), 1);
    QVERIFY(!onCustomShelf.constFirst()->isVisible());

    // The stock card is behind the same loader now, and everything the shelf
    // did with it has to survive the move: it is fed its row, it follows the
    // cursor, and a key, a click and the pointer all reach the shelf.
    QMetaObject::invokeMethod(m_root, "showStock", Q_ARG(QVariant, true));
    QQuickItem *stock = m_root->findChild<QQuickItem *>(QStringLiteral("stock"));
    QVERIFY(stock);
    QTRY_COMPARE(stock->property("count").toInt(), 7);
    QTRY_VERIFY(stockCardLabelled(stock, QStringLiteral("A")));
    QTRY_VERIFY(stockCardLabelled(stock, QStringLiteral("B")));
    QQuickItem *first = stockCardLabelled(stock, QStringLiteral("A"));
    QQuickItem *second = stockCardLabelled(stock, QStringLiteral("B"));

    run("focusStock");
    QCOMPARE(where(), QStringLiteral("stock"));
    QCOMPARE(call("stockIndex"), 0);
    QTRY_VERIFY(first->property("highlighted").toBool());
    QVERIFY(!second->property("highlighted").toBool());

    press(Qt::Key_Right);
    QCOMPARE(call("stockIndex"), 1);
    QTRY_VERIFY(second->property("highlighted").toBool());
    QVERIFY(!first->property("highlighted").toBool());
    press(Qt::Key_Return);
    QCOMPARE(m_root->property("stockActivated").toInt(), 1);

    // The pointer over a card is published by the shelf, and moves nothing.
    const QPoint onFirstLabel =
        first->mapToScene(QPointF(first->width() / 2, first->height() - 6)).toPoint();
    QTest::mouseMove(&m_view, onFirstLabel);
    QTest::mouseMove(&m_view, onFirstLabel + QPoint(1, 0));
    QTRY_COMPARE(stock->property("hoveredIndex").toInt(), 0);
    QCOMPARE(call("stockIndex"), 1);

    // A click is a commit: it activates the card and brings the cursor to it.
    QTest::mouseClick(&m_view, Qt::LeftButton, {}, onFirstLabel);
    QTRY_COMPARE(m_root->property("stockActivated").toInt(), 0);
    QCOMPARE(call("stockIndex"), 0);
    QCOMPARE(where(), QStringLiteral("stock"));

    QMetaObject::invokeMethod(m_root, "showStock", Q_ARG(QVariant, false));
}

QTEST_MAIN(NavigationControlsTest)
#include "tst_navigation_controls.moc"
