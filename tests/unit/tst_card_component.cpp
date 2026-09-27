#include <QDir>
#include <QFile>
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
    width: 900
    height: 760
    focus: true

    property int railActivated: -1
    property int railPlayed: -1
    property int railMenu: -1
    property int gridActivated: -1

    component Tile: Item {
        property var model
        property int index: -1
        property bool current: false
        property bool hovered: false
        signal activated()
        signal playRequested()
        signal menuRequested(real x, real y)
        width: 120
        height: 90
    }

    ListModel {
        id: rows
        ListElement { itemId: "a"; name: "A" }
        ListElement { itemId: "b"; name: "B" }
        ListElement { itemId: "c"; name: "C" }
        ListElement { itemId: "d"; name: "D" }
    }

    StrmRail {
        id: rail
        objectName: "rail"
        width: 900
        title: "Custom"
        showHeading: false
        railModel: rows
        customCardWidth: 120
        customCardHeight: 90
        navigationFocusKey: "probe-rail"
        cardComponent: Component {
            Tile { objectName: "rail-tile-" + (model ? model.itemId : "") }
        }
        onItemActivated: index => root.railActivated = index
        onItemPlayRequested: index => root.railPlayed = index
        onMenuRequested: (index, x, y) => root.railMenu = index
    }

    // Narrow enough that the row overflows, so the right chevron is enabled:
    // a disabled chevron is never a Tab stop anyway and would prove nothing.
    StrmRail {
        id: scrolling
        objectName: "scrolling"
        y: 380
        width: 260
        title: "Scrolling"
        showHeading: false
        railModel: rows
        customCardWidth: 120
        customCardHeight: 90
        navigationFocusKey: "probe-scrolling"
        cardComponent: Component {
            Tile { objectName: "scroll-tile-" + (model ? model.itemId : "") }
        }
    }

    StrmRail {
        id: stock
        objectName: "stock"
        y: 200
        width: 900
        title: "Stock"
        railModel: rows
    }

    StrmGrid {
        id: grid
        objectName: "grid"
        y: 520
        width: 900
        height: 240
        gridModel: rows
        customCardWidth: 120
        customCardHeight: 90
        cardComponent: Component {
            Tile { objectName: "grid-tile-" + (model ? model.itemId : "") }
        }
        onItemActivated: index => root.gridActivated = index
    }
}
)QML";

QQuickItem *findItem(QQuickItem *from, const QString &name)
{
    if (!from)
        return nullptr;
    if (from->objectName() == name)
        return from;
    for (QQuickItem *child : from->childItems()) {
        if (QQuickItem *found = findItem(child, name))
            return found;
    }
    return nullptr;
}

QQuickItem *createProbe(QTemporaryDir &dir, QQuickView &view)
{
    const QString modulePath = dir.filePath(QStringLiteral("StrmQt"));
    if (!QDir().mkpath(modulePath))
        return nullptr;
    const QStringList moduleFiles = {
        QStringLiteral("Theme.qml"),          QStringLiteral("FocusRing.qml"),
        QStringLiteral("StrmIcon.qml"),       QStringLiteral("StrmTooltip.qml"),
        QStringLiteral("StrmIconButton.qml"), QStringLiteral("StrmButton.qml"),
        QStringLiteral("StrmCard.qml"),       QStringLiteral("StrmImage.qml"),
        QStringLiteral("StrmScrollBar.qml"),  QStringLiteral("NavigationFocusRestorer.qml"),
        QStringLiteral("NavigationColumn.qml"), QStringLiteral("StrmRail.qml"),
        QStringLiteral("StrmGrid.qml"),
        QStringLiteral("FocusClip.qml"),
    };
    for (const QString &name : moduleFiles) {
        const QString sourceRoot = name == QStringLiteral("Theme.qml")
                                       ? QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/")
                                       : QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/controls/");
        if (!QFile::copy(sourceRoot + name, modulePath + QLatin1Char('/') + name))
            return nullptr;
    }
    const QByteArray shimLines = strmqt::test::stageShims(modulePath);
    if (shimLines.isEmpty())
        return nullptr;
    QFile qmldir(modulePath + QStringLiteral("/qmldir"));
    if (!qmldir.open(QIODevice::WriteOnly))
        return nullptr;
    qmldir.write("module StrmQt\n"
                 "singleton Theme 1.0 Theme.qml\n"
                 "singleton NavigationColumn 1.0 NavigationColumn.qml\n"
                 "FocusRing 1.0 FocusRing.qml\n"
                 "FocusClip 1.0 FocusClip.qml\n"
                 "StrmIcon 1.0 StrmIcon.qml\n"
                 "StrmTooltip 1.0 StrmTooltip.qml\n"
                 "StrmIconButton 1.0 StrmIconButton.qml\n"
                 "StrmButton 1.0 StrmButton.qml\n"
                 "StrmCard 1.0 StrmCard.qml\n"
                 "StrmImage 1.0 StrmImage.qml\n"
                 "StrmScrollBar 1.0 StrmScrollBar.qml\n"
                 "NavigationFocusRestorer 1.0 NavigationFocusRestorer.qml\n"
                 "StrmRail 1.0 StrmRail.qml\n"
                 "StrmGrid 1.0 StrmGrid.qml\n");
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
    view.resize(900, 760);
    view.show();
    if (!QTest::qWaitForWindowExposed(&view))
        return nullptr;
    return view.rootObject();
}

} // namespace

class CardComponentTest : public QObject
{
    Q_OBJECT

private slots:
    void railLoadsTheCustomCardWithItsRow();
    void currentFollowsKeyboardFocus();
    void cardSignalsReachTheRail();
    void menuKeyAsksForTheCurrentCard();
    void hoverIsNotFocus();
    void gridLoadsTheCustomCard();
    void stockRailIsUnchanged();
    void railChevronsAreNotTabStops();

private:
    QTemporaryDir m_dir;
    QQuickView m_view;
    QQuickItem *m_root = nullptr;
    void ensureProbe();
};

void CardComponentTest::ensureProbe()
{
    if (!m_root) {
        QVERIFY(m_dir.isValid());
        m_root = createProbe(m_dir, m_view);
    }
    QVERIFY(m_root);
}

// The chevrons are a mouse affordance. They appear on hover, and a hovered
// rail used to hand them a Tab stop of their own, so tabbing into a shelf
// landed on "Scroll right" instead of a card.
void CardComponentTest::railChevronsAreNotTabStops()
{
    ensureProbe();
    QQuickItem *rail = findItem(m_root, QStringLiteral("scrolling"));
    QVERIFY(rail);
    QQuickItem *right = findItem(rail, QStringLiteral("railChevronRight"));
    QVERIFY(right);
    QTRY_VERIFY(right->isEnabled());
    QVERIFY(!right->activeFocusOnTab());
    QQuickItem *left = findItem(rail, QStringLiteral("railChevronLeft"));
    QVERIFY(left);
    QVERIFY(!left->activeFocusOnTab());
}

void CardComponentTest::railLoadsTheCustomCardWithItsRow()
{
    ensureProbe();
    QQuickItem *rail = findItem(m_root, QStringLiteral("rail"));
    QVERIFY(rail);
    QTRY_VERIFY(findItem(rail, QStringLiteral("rail-tile-a")));
    QQuickItem *tile = findItem(rail, QStringLiteral("rail-tile-b"));
    QVERIFY(tile);
    QCOMPARE(tile->property("index").toInt(), 1);
    QCOMPARE(rail->property("cardWidth").toInt(), 120);
    QCOMPARE(rail->property("cardHeight").toInt(), 90);
    QVERIFY(rail->property("customCards").toBool());
    // No heading: the rail is exactly its shelf.
    const int padding = rail->property("rowPadding").toInt();
    QCOMPARE(qRound(rail->height()), 90 + padding * 2);
}

void CardComponentTest::currentFollowsKeyboardFocus()
{
    ensureProbe();
    QQuickItem *rail = findItem(m_root, QStringLiteral("rail"));
    QVERIFY(rail);
    QQuickItem *a = findItem(rail, QStringLiteral("rail-tile-a"));
    QVERIFY(a);
    QVERIFY(!a->property("current").toBool());

    rail->forceActiveFocus();
    QTRY_VERIFY(a->property("current").toBool());
    QTest::keyClick(&m_view, Qt::Key_Right);
    QTRY_COMPARE(rail->property("currentIndex").toInt(), 1);
    QQuickItem *b = findItem(rail, QStringLiteral("rail-tile-b"));
    QVERIFY(b);
    QTRY_VERIFY(b->property("current").toBool());
    QVERIFY(!a->property("current").toBool());
}

void CardComponentTest::cardSignalsReachTheRail()
{
    ensureProbe();
    QQuickItem *rail = findItem(m_root, QStringLiteral("rail"));
    QQuickItem *c = findItem(rail, QStringLiteral("rail-tile-c"));
    QVERIFY(c);

    QVERIFY(QMetaObject::invokeMethod(c, "activated"));
    QCOMPARE(m_root->property("railActivated").toInt(), 2);
    QCOMPARE(rail->property("currentIndex").toInt(), 2); // a click is a commit

    QVERIFY(QMetaObject::invokeMethod(c, "playRequested"));
    QCOMPARE(m_root->property("railPlayed").toInt(), 2);

    QVERIFY(QMetaObject::invokeMethod(c, "menuRequested", Q_ARG(double, 10.0), Q_ARG(double, 20.0)));
    QCOMPARE(m_root->property("railMenu").toInt(), 2);
}

void CardComponentTest::menuKeyAsksForTheCurrentCard()
{
    ensureProbe();
    QQuickItem *rail = findItem(m_root, QStringLiteral("rail"));
    m_root->setProperty("railMenu", -1);
    rail->forceActiveFocus();
    const int current = rail->property("currentIndex").toInt();
    QTest::keyClick(&m_view, Qt::Key_Menu);
    QCOMPARE(m_root->property("railMenu").toInt(), current);
}

void CardComponentTest::hoverIsNotFocus()
{
    ensureProbe();
    QQuickItem *rail = findItem(m_root, QStringLiteral("rail"));
    QQuickItem *d = findItem(rail, QStringLiteral("rail-tile-d"));
    QVERIFY(d);
    const int before = rail->property("currentIndex").toInt();
    const QPointF centre = d->mapToScene(QPointF(d->width() / 2, d->height() / 2));
    QTest::mouseMove(&m_view, centre.toPoint());
    QTRY_VERIFY(d->property("hovered").toBool());
    QTRY_COMPARE(rail->property("hoveredIndex").toInt(), 3);
    QCOMPARE(rail->property("currentIndex").toInt(), before);
    QTest::mouseMove(&m_view, QPoint(5, 700));
}

void CardComponentTest::gridLoadsTheCustomCard()
{
    ensureProbe();
    QQuickItem *grid = findItem(m_root, QStringLiteral("grid"));
    QVERIFY(grid);
    QTRY_VERIFY(findItem(grid, QStringLiteral("grid-tile-a")));
    QCOMPARE(grid->property("cardWidth").toInt(), 120);
    QQuickItem *b = findItem(grid, QStringLiteral("grid-tile-b"));
    QVERIFY(b);
    QCOMPARE(b->property("index").toInt(), 1);

    grid->forceActiveFocus();
    QQuickItem *a = findItem(grid, QStringLiteral("grid-tile-a"));
    QTRY_VERIFY(a->property("current").toBool());
    QVERIFY(QMetaObject::invokeMethod(b, "activated"));
    QCOMPARE(m_root->property("gridActivated").toInt(), 1);
    QCOMPARE(grid->property("currentIndex").toInt(), 1);
}

void CardComponentTest::stockRailIsUnchanged()
{
    ensureProbe();
    QQuickItem *stock = findItem(m_root, QStringLiteral("stock"));
    QVERIFY(stock);
    QVERIFY(!stock->property("customCards").toBool());
    QVERIFY(stock->property("cardWidth").toInt() > 0);
    QVERIFY(stock->property("cardWidth").toInt() != 120);
    // P2-R5: findItem(stock, "rail-tile-a") can never match (the stock rail
    // never loads a card named that), so this checks the stock rail's actual
    // shape instead of a vacuous absence.
    QVERIFY(stock->property("showHeading").toBool());
    QCOMPARE(stock->property("count").toInt(), 4);
}

QTEST_MAIN(CardComponentTest)
#include "tst_card_component.moc"
