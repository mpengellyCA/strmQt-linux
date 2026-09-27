#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QImage>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest>

// The first row's focus ring is drawn whole.
//
// FocusRing draws outside the item it frames, and a focused card is raised by
// Theme.focusScale on top of that, so a view that clips at its own bounds used
// to slice the ring off whatever sat flush against its edge: the top row of a
// library grid, the first sleeve of a gutterless shelf, the first row of a
// list. The check here is geometric and independent of which control drew the
// ring: every visible ring stroke must lie inside the scene rectangle of every
// clipping ancestor it has.

namespace {

const char *kProbe = R"QML(
import QtQuick
import StrmQt

Item {
    id: root
    width: 1000
    height: 1800

    // TV density raises every size and the ring with it; the room left for
    // the ring has to follow.
    function setDensity(mode: string) { Theme.densityMode = mode }
    function density(): string { return Theme.densityMode }

    ListModel {
        id: rows
        ListElement { itemId: "a"; name: "A"; label: "A" }
        ListElement { itemId: "b"; name: "B"; label: "B" }
        ListElement { itemId: "c"; name: "C"; label: "C" }
        ListElement { itemId: "d"; name: "D"; label: "D" }
        ListElement { itemId: "e"; name: "E"; label: "E" }
        ListElement { itemId: "f"; name: "F"; label: "F" }
        ListElement { itemId: "g"; name: "G"; label: "G" }
        ListElement { itemId: "h"; name: "H"; label: "H" }
    }

    // A card the way the Crate draws one: its ring sits outside the art, and
    // the art grows on focus.
    component Tile: Item {
        property var model
        property int index: -1
        property bool current: false
        property bool hovered: false
        signal activated()
        width: 150
        height: 150
        Item {
            width: 150
            height: 150
            scale: parent.current ? Theme.focusScale : 1.0
            FocusRing {
                objectName: "tileRing"
                active: parent.parent.current
                inset: -Theme.scale(3)
            }
        }
    }

    // Library grid: stock cards, first row flush with the top of the view.
    StrmGrid {
        id: grid
        objectName: "grid"
        y: 12
        width: 1000
        height: 420
        gridModel: rows
    }

    // A shelf whose gutter is zero, as the album page's More-by shelf is.
    Item {
        y: 440
        x: 60
        width: 700
        height: 240
        StrmRail {
            id: gutterless
            objectName: "gutterless"
            width: parent.width
            showHeading: false
            gutter: 0
            railModel: rows
            customCardWidth: 150
            customCardHeight: 150
            cardComponent: Component { Tile {} }
        }
    }

    component ListRow: Item {
        required property int index
        width: ListView.view.width
        height: 40
        FocusRing {
            objectName: "rowRing"
            active: parent.ListView.isCurrentItem && parent.ListView.view.activeFocus
            inset: -Theme.scale(1)
        }
    }

    // A list with the clip moved onto a FocusClip.
    FocusClip {
        id: listClip
        y: 720
        x: 60
        width: 400
        height: 200
    }
    ListView {
        id: list
        objectName: "list"
        parent: listClip.contentItem
        anchors.fill: parent
        model: rows
        delegate: ListRow {}
    }

    // The same list clipping at its own bounds: the audit must see that one.
    ListView {
        id: clipped
        objectName: "clipped"
        y: 960
        x: 520
        width: 400
        height: 200
        clip: true
        model: rows
        delegate: ListRow {}
    }

    // The genre grid as MusicBrowsePage builds it: record bins, whose tile is
    // a short stack with two caption lines under it, sized by a metrics tile.
    component Bin: GenreBinTile {
        property var model: null
        property int index: -1
        size: Theme.crateSleeveSize
        name: model && model.name !== undefined ? String(model.name) : ""
        subtitle: "12 records"
    }
    StrmGrid {
        id: bins
        objectName: "bins"
        y: 1180
        width: 480
        height: 560
        gridModel: rows
        customCardWidth: Theme.crateSleeveSize
        customCardHeight: binMetrics.implicitHeight
        GenreBinTile {
            id: binMetrics
            visible: false
            enabled: false
            size: Theme.crateSleeveSize
            name: "M"
            subtitle: "M"
        }
        cardComponent: Component { Bin {} }
    }

    // The same bins declared square, as the page used to: the tile is taller
    // than its cell, so the ring overruns the room the grid left for it.
    StrmGrid {
        id: squareBins
        objectName: "squareBins"
        x: 500
        y: 1180
        width: 480
        height: 560
        gridModel: rows
        customCardWidth: Theme.crateSleeveSize
        customCardHeight: Theme.crateSleeveSize
        cardComponent: Component { Bin {} }
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

bool effectivelyVisible(const QQuickItem *item)
{
    for (const QQuickItem *p = item; p; p = p->parentItem()) {
        if (!p->isVisible() || p->opacity() <= 0.0)
            return false;
    }
    return true;
}

// Rings are recognised by shape rather than type name: FocusRing is an Item
// with `active` and `inset`, whose first child is the stroke.
void collectRings(QQuickItem *from, QList<QQuickItem *> &out)
{
    if (!from)
        return;
    const QMetaObject *meta = from->metaObject();
    if (meta->indexOfProperty("active") >= 0 && meta->indexOfProperty("inset") >= 0
        && !from->childItems().isEmpty())
        out << from;
    for (QQuickItem *child : from->childItems())
        collectRings(child, out);
}

// Every visible stroke that crosses a clipping ancestor's edge, described.
QStringList clippedRings(QQuickItem *root, QQuickItem *scope)
{
    QList<QQuickItem *> rings;
    collectRings(scope, rings);
    QStringList cut;
    for (QQuickItem *ring : std::as_const(rings)) {
        QQuickItem *stroke = ring->childItems().constFirst();
        if (!ring->property("active").toBool() || !effectivelyVisible(stroke))
            continue;
        const QRectF s = stroke->mapRectToScene(stroke->boundingRect());
        for (QQuickItem *p = ring->parentItem(); p && p != root; p = p->parentItem()) {
            if (!p->clip())
                continue;
            const QRectF c = p->mapRectToScene(p->boundingRect());
            // Half a pixel of tolerance: the ring's edge is antialiased.
            if (s.left() < c.left() - 0.5 || s.top() < c.top() - 0.5
                || s.right() > c.right() + 0.5 || s.bottom() > c.bottom() + 0.5) {
                cut << QStringLiteral("%1 cut by %2: ring %3,%4 %5x%6, clip %7,%8 %9x%10")
                           .arg(ring->parentItem()->objectName(), p->objectName())
                           .arg(s.x()).arg(s.y()).arg(s.width()).arg(s.height())
                           .arg(c.x()).arg(c.y()).arg(c.width()).arg(c.height());
            }
        }
    }
    return cut;
}

// Scene rectangles of every visible active stroke under scope, in tree order.
QList<QRectF> ringRects(QQuickItem *scope)
{
    QList<QQuickItem *> rings;
    collectRings(scope, rings);
    QList<QRectF> out;
    for (QQuickItem *ring : std::as_const(rings)) {
        QQuickItem *stroke = ring->childItems().constFirst();
        if (ring->property("active").toBool() && effectivelyVisible(stroke))
            out << stroke->mapRectToScene(stroke->boundingRect());
    }
    return out;
}

int activeRings(QQuickItem *scope)
{
    QList<QQuickItem *> rings;
    collectRings(scope, rings);
    int n = 0;
    for (QQuickItem *ring : std::as_const(rings)) {
        if (ring->property("active").toBool()
            && effectivelyVisible(ring->childItems().constFirst()))
            ++n;
    }
    return n;
}

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
        QStringLiteral("StrmGrid.qml"),
    };
    const QStringList musicFiles = {
        QStringLiteral("GenreBinTile.qml"), QStringLiteral("CrateHeading.qml"),
        QStringLiteral("CrateKicker.qml"),
    };
    for (const QString &name : moduleFiles + musicFiles) {
        const QString sourceRoot = name == QStringLiteral("Theme.qml")
                                       ? QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/")
                                   : musicFiles.contains(name)
                                       ? QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/music/")
                                       : QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/controls/");
        if (!QFile::copy(sourceRoot + name, modulePath + QLatin1Char('/') + name))
            return nullptr;
    }
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
                 "StrmGrid 1.0 StrmGrid.qml\n"
                 "GenreBinTile 1.0 GenreBinTile.qml\n"
                 "CrateHeading 1.0 CrateHeading.qml\n"
                 "CrateKicker 1.0 CrateKicker.qml\n");
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
    view.resize(1000, 1800);
    view.show();
    if (!QTest::qWaitForWindowExposed(&view))
        return nullptr;
    return view.rootObject();
}

} // namespace

class FocusClipTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void gridTopRowRingIsWhole();
    void gridTopRowRingIsWholeAfterScrollingBack();
    void gutterlessRailFirstRingIsWhole();
    void listFirstRowRingIsWhole();
    void auditSeesARingCutByItsOwnView();
    void clipKeepsTheViewGeometry();
    void genreBinTopRowRingIsWhole();
    void auditSeesASquareGenreCellCut();
    void nothingAnchorsToAViewAFocusClipTookOver();
    void tvDensityRingsAreWhole();

private:
    QTemporaryDir m_dir;
    QQuickView m_view;
    QQuickItem *m_root = nullptr;
    void focusAndSettle(QQuickItem *item);
    void settle();
    void saveShot(const QString &name);
};

void FocusClipTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_root = createProbe(m_dir, m_view);
    QVERIFY(m_root);
}

// Focus moves the ring in at Theme.animFastMs and raises the card over the
// same time, and a grid may scroll its highlight into range: the audit has to
// see where they end up, not where they start.
void FocusClipTest::focusAndSettle(QQuickItem *item)
{
    item->forceActiveFocus();
    QTRY_VERIFY(activeRings(m_root) > 0);
    settle();
}

// Settled means three samples in a row, a QTRY step (50 ms) apart, see every
// ring in the same place: longer than any of those animations stands still.
void FocusClipTest::settle()
{
    QList<QRectF> last;
    int same = 0;
    QTRY_VERIFY_WITH_TIMEOUT(([&] {
                                 const QList<QRectF> now = ringRects(m_root);
                                 same = (!now.isEmpty() && now == last) ? same + 1 : 0;
                                 last = now;
                                 return same >= 2;
                             }()),
                             5000);
}

// Set STRMQT_FOCUS_SHOTS to a directory to keep what the audit looked at.
void FocusClipTest::saveShot(const QString &name)
{
    const QString dir = qEnvironmentVariable("STRMQT_FOCUS_SHOTS");
    if (dir.isEmpty())
        return;
    QDir().mkpath(dir);
    m_view.grabWindow().save(dir + QLatin1Char('/') + name + QStringLiteral(".png"));
}

void FocusClipTest::gridTopRowRingIsWhole()
{
    QQuickItem *grid = findItem(m_root, QStringLiteral("grid"));
    QVERIFY(grid);
    QTRY_VERIFY(grid->property("count").toInt() > 0);
    focusAndSettle(grid);
    QCOMPARE(grid->property("currentIndex").toInt(), 0);
    saveShot(QStringLiteral("grid"));
    const QStringList cut = clippedRings(m_root, grid);
    QVERIFY2(cut.isEmpty(), qPrintable(cut.join(QLatin1Char('\n'))));
}

// Qt's positionViewAtIndex() and the highlight range bring the first row back
// flush against the view's edge; the ring has to survive that too.
void FocusClipTest::gridTopRowRingIsWholeAfterScrollingBack()
{
    QQuickItem *grid = findItem(m_root, QStringLiteral("grid"));
    QVERIFY(grid);
    focusAndSettle(grid);
    for (int i = 0; i < 3; ++i)
        QTest::keyClick(&m_view, Qt::Key_Down);
    QTRY_VERIFY(grid->property("currentIndex").toInt() > 0);
    QTest::keyClick(&m_view, Qt::Key_Home);
    QTRY_COMPARE(grid->property("currentIndex").toInt(), 0);
    settle();
    const QStringList cut = clippedRings(m_root, grid);
    QVERIFY2(cut.isEmpty(), qPrintable(cut.join(QLatin1Char('\n'))));
}

void FocusClipTest::gutterlessRailFirstRingIsWhole()
{
    QQuickItem *rail = findItem(m_root, QStringLiteral("gutterless"));
    QVERIFY(rail);
    QTRY_VERIFY(rail->property("count").toInt() > 0);
    focusAndSettle(rail);
    QCOMPARE(rail->property("currentIndex").toInt(), 0);
    saveShot(QStringLiteral("gutterless-rail"));
    const QStringList cut = clippedRings(m_root, rail);
    QVERIFY2(cut.isEmpty(), qPrintable(cut.join(QLatin1Char('\n'))));
}

void FocusClipTest::listFirstRowRingIsWhole()
{
    QQuickItem *list = findItem(m_root, QStringLiteral("list"));
    QVERIFY(list);
    list->setProperty("currentIndex", 0);
    focusAndSettle(list);
    saveShot(QStringLiteral("list"));
    QCOMPARE(activeRings(list), 1);
    const QStringList cut = clippedRings(m_root, list);
    QVERIFY2(cut.isEmpty(), qPrintable(cut.join(QLatin1Char('\n'))));
}

// Without this the other tests could pass by measuring nothing.
void FocusClipTest::auditSeesARingCutByItsOwnView()
{
    QQuickItem *list = findItem(m_root, QStringLiteral("clipped"));
    QVERIFY(list);
    list->setProperty("currentIndex", 0);
    focusAndSettle(list);
    QCOMPARE(activeRings(list), 1);
    QVERIFY(!clippedRings(m_root, list).isEmpty());
}

// FocusClip moves the clip, never the content: the view inside has exactly
// the geometry the FocusClip was given.
void FocusClipTest::clipKeepsTheViewGeometry()
{
    QQuickItem *list = findItem(m_root, QStringLiteral("list"));
    QVERIFY(list);
    const QRectF r = list->mapRectToScene(list->boundingRect());
    QCOMPARE(r, QRectF(60, 720, 400, 200));
    QVERIFY(!list->clip());
}

// The genre grid's bins, sized from a metrics tile as MusicBrowsePage does.
void FocusClipTest::genreBinTopRowRingIsWhole()
{
    QQuickItem *grid = findItem(m_root, QStringLiteral("bins"));
    QVERIFY(grid);
    QTRY_VERIFY(grid->property("count").toInt() > 0);
    focusAndSettle(grid);
    QCOMPARE(grid->property("currentIndex").toInt(), 0);
    saveShot(QStringLiteral("genre-bins"));
    const QStringList cut = clippedRings(m_root, grid);
    QVERIFY2(cut.isEmpty(), qPrintable(cut.join(QLatin1Char('\n'))));
}

// A card taller than the height its grid was told defeats the room the grid
// leaves: this is the square declaration the genre grid used to make, and the
// reason it now measures its tile.
void FocusClipTest::auditSeesASquareGenreCellCut()
{
    QQuickItem *grid = findItem(m_root, QStringLiteral("squareBins"));
    QVERIFY(grid);
    QTRY_VERIFY(grid->property("count").toInt() > 0);
    focusAndSettle(grid);
    QVERIFY(!clippedRings(m_root, grid).isEmpty());
}

// A view handed to a FocusClip through `parent:` is no longer its old
// siblings' sibling. An anchor to it from one of them still resolves, but in
// the view's own coordinates, and Qt 6.11 does not warn: SeriesPage's empty
// state drew at the page's top-left that way. Siblings must anchor to the
// clip. Lines nested deeper than the view's own properties are its children,
// which may anchor to it.
void FocusClipTest::nothingAnchorsToAViewAFocusClipTookOver()
{
    static const QRegularExpression reparent(QStringLiteral(R"(^(\s*)parent:\s*\w+\.contentItem\b)"));
    static const QRegularExpression idLine(QStringLiteral(R"(^(\s*)id:\s*(\w+)\s*$)"));
    static const QRegularExpression firstVisible(QStringLiteral(R"(\S)"));
    QStringList offenders;
    int views = 0;
    QDirIterator it(QStringLiteral(STRMQT_SOURCE_DIR "/src/ui"), {QStringLiteral("*.qml")},
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        QFile file(path);
        QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(path));
        const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
        for (qsizetype i = 0; i < lines.size(); ++i) {
            const QRegularExpressionMatch m = reparent.match(lines.at(i));
            if (!m.hasMatch())
                continue;
            const qsizetype indent = m.capturedLength(1);
            // The view's id: above `parent:` in the same object.
            QString id;
            for (qsizetype j = i - 1; j >= 0; --j) {
                const QString &line = lines.at(j);
                if (line.trimmed().isEmpty())
                    continue;
                const QRegularExpressionMatch im = idLine.match(line);
                if (im.hasMatch() && im.capturedLength(1) == indent) {
                    id = im.captured(2);
                    break;
                }
                // Past the object's opening line: this view has no id.
                if (line.indexOf(firstVisible) < indent)
                    break;
            }
            if (id.isEmpty())
                continue;
            ++views;
            const QRegularExpression ref(
                QStringLiteral(R"(^(\s*)anchors\.\w+\s*:.*\b%1\b)").arg(id));
            for (qsizetype k = 0; k < lines.size(); ++k) {
                const QRegularExpressionMatch rm = ref.match(lines.at(k));
                if (rm.hasMatch() && rm.capturedLength(1) <= indent)
                    offenders << QStringLiteral("%1:%2: %3")
                                     .arg(QDir(QStringLiteral(STRMQT_SOURCE_DIR)).relativeFilePath(path))
                                     .arg(k + 1)
                                     .arg(lines.at(k).trimmed());
            }
        }
    }
    // Every FocusClip in src/ui that takes a view over by `parent:`.
    QVERIFY2(views >= 10, qPrintable(QString::number(views)));
    QVERIFY2(offenders.isEmpty(), qPrintable(offenders.join(QLatin1Char('\n'))));
}

// Last, because it changes Theme for everything after it.
void FocusClipTest::tvDensityRingsAreWhole()
{
    QMetaObject::invokeMethod(m_root, "setDensity", Q_ARG(QString, QStringLiteral("tv")));
    const auto restore = qScopeGuard([this] {
        QMetaObject::invokeMethod(m_root, "setDensity",
                                  Q_ARG(QString, QStringLiteral("comfortable")));
    });
    QString mode;
    QMetaObject::invokeMethod(m_root, "density", Q_RETURN_ARG(QString, mode));
    QCOMPARE(mode, QStringLiteral("tv"));

    QStringList cut;
    for (const char *name : {"grid", "gutterless", "list", "bins"}) {
        QQuickItem *item = findItem(m_root, QLatin1String(name));
        QVERIFY(item);
        item->setProperty("currentIndex", 0);
        focusAndSettle(item);
        saveShot(QStringLiteral("tv-%1").arg(QLatin1String(name)));
        for (const QString &c : clippedRings(m_root, item))
            cut << QStringLiteral("%1: %2").arg(QLatin1String(name), c);
    }
    QVERIFY2(cut.isEmpty(), qPrintable(cut.join(QLatin1Char('\n'))));
}

QTEST_MAIN(FocusClipTest)
#include "tst_focus_clip.moc"
