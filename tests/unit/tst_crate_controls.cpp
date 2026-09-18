#include <QDir>
#include <QFile>
#include <QFont>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>
#include <QtTest>

namespace {

// Deviation from the brief: its ShelfError probe text reads "Couldn.t load"
// (an apostrophe). An apostrophe inside this raw string desyncs CMake and
// Qt automoc file extraction (/usr/lib/qt6/cmake_automoc_parser, measured on
// cmake 4.4.3, Qt 6.11.2) badly enough that it stops seeing the Q_OBJECT
// class below and emits an empty .moc, failing the link with an undefined
// vtable for CrateControlsTest. No assertion below reads this text, so the
// fixture wording was changed to avoid the apostrophe entirely.
const char *kProbe = R"QML(
import QtQuick
import StrmQt

Item {
    id: root
    width: 1000
    height: 900
    focus: true

    property int sleeveActivated: 0
    property int sleevePlayed: 0
    property int sleeveMenus: 0
    property int portraitActivated: 0
    property int stationActivated: 0
    property int genreActivated: 0
    property int retried: 0
    property string lastSection: ""
    property bool leftEscaped: false
    property int shelfActivated: -1
    property int shelfActions: 0
    property bool upEscaped: false

    Keys.onUpPressed: root.upEscaped = true
    readonly property color hiResTone: Theme.crateBadgeHiRes
    readonly property string displayFamily: Theme.fontDisplay
    readonly property string monoFamily: Theme.fontMono

    Keys.onLeftPressed: root.leftEscaped = true

    CrateHeading { id: heading; objectName: "heading"; text: "New in the crate" }
    CrateKicker { id: kicker; objectName: "kicker"; y: 40; text: "3 added this week" }
    CrateBadge { objectName: "emptyBadge"; y: 60; text: "" }
    CrateBadge { objectName: "plainBadge"; x: 100; y: 60; text: "FLAC" }
    CrateBadge { objectName: "hiResBadge"; x: 200; y: 60; text: "24/96"; hiRes: true }

    CoverCollage { objectName: "oneCover"; y: 90; size: 80; covers: [""] }
    CoverCollage { objectName: "fourCovers"; x: 100; y: 90; size: 80; covers: ["", "", "", ""] }

    CrateSleeve {
        objectName: "sleeve"
        x: 20; y: 200
        size: 160
        title: "Moon Safari"
        subtitle: "Air · 1998"
        badge: "FLAC"
        onActivated: root.sleeveActivated++
        onPlayRequested: root.sleevePlayed++
        onMenuRequested: (mx, my) => root.sleeveMenus++
    }

    CratePortrait {
        objectName: "portrait"
        x: 220; y: 200
        size: 120
        name: "Air"
        subtitle: "12 records"
        onActivated: root.portraitActivated++
    }

    StationTile {
        objectName: "station"
        x: 380; y: 200
        size: 140
        label: "Heavy rotation"
        covers: ["", "", "", ""]
        onActivated: root.stationActivated++
    }

    GenreBinTile {
        objectName: "genre"
        x: 560; y: 200
        size: 160
        name: "Electronic"
        subtitle: "84 records"
        covers: ["", "", ""]
        onActivated: root.genreActivated++
    }

    GenreBinTile {
        objectName: "allGenres"
        x: 760; y: 200
        size: 160
        name: "All 42 genres"
        isAllBin: true
    }

    SectionStrip {
        id: strip
        objectName: "strip"
        y: 520
        currentKey: "home"
        onSectionChosen: key => root.lastSection = key
    }

    ShelfError {
        objectName: "shelfError"
        y: 600
        message: "Could not load"
        onRetry: root.retried++
    }

    ListModel { id: shelfRows }

    QtObject {
        id: fakeLane
        objectName: "fakeLane"

        property var model: shelfRows
        property bool loading: false
        property string error: ""
        readonly property bool empty: !fakeLane.loading && fakeLane.error.length === 0 && shelfRows.count === 0
        property int retries: 0

        function retry() { fakeLane.retries++ }
    }

    CrateShelf {
        id: shelf
        objectName: "shelf"
        y: 660
        width: 1000
        title: "Pull one out"
        lane: fakeLane
        actionText: "Reshuffle"
        actionIcon: "refresh"
        navigationFocusKey: "probe-shelf"
        cardWidth: 100
        cardHeight: 140
        skeletonShape: "round"
        delegate: Component {
            CrateSleeve {
                property var model
                property int index: -1
                objectName: "shelfSleeve-" + (model ? model.itemId : "")
                size: 100
                title: model ? model.title : ""
            }
        }
        onItemActivated: index => root.shelfActivated = index
        onActionTriggered: root.shelfActions++
    }

    function setLane(loading, error) {
        fakeLane.loading = loading
        fakeLane.error = error
    }

    function fillShelf() {
        shelfRows.clear()
        shelfRows.append({ itemId: "a", title: "A" })
        shelfRows.append({ itemId: "b", title: "B" })
        shelfRows.append({ itemId: "c", title: "C" })
    }
}
)QML";

const QStringList kControls = {
    QStringLiteral("FocusRing"),      QStringLiteral("StrmIcon"),    QStringLiteral("StrmTooltip"),
    QStringLiteral("StrmIconButton"), QStringLiteral("StrmButton"),  QStringLiteral("StrmImage"),
    QStringLiteral("StrmAvatar"),     QStringLiteral("StrmCard"),    QStringLiteral("StrmScrollBar"),
    QStringLiteral("NavigationFocusRestorer"), QStringLiteral("NavigationColumn"),
    QStringLiteral("StrmRail"),       QStringLiteral("StrmSkeleton"),
};

const QStringList kMusic = {
    QStringLiteral("CrateHeading"), QStringLiteral("CrateKicker"),  QStringLiteral("CrateBadge"),
    QStringLiteral("CoverCollage"), QStringLiteral("CrateSleeve"),  QStringLiteral("CratePortrait"),
    QStringLiteral("StationTile"),  QStringLiteral("GenreBinTile"), QStringLiteral("SectionStrip"),
    QStringLiteral("ShelfError"),   QStringLiteral("CrateShelf"),
};

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

bool stage(const QString &sourceDir, const QString &type, const QString &modulePath, QByteArray &qmldir)
{
    const QString file = type + QStringLiteral(".qml");
    if (!QFile::copy(sourceDir + file, modulePath + QLatin1Char('/') + file))
        return false;
    // The one singleton among the staged controls.
    const QByteArray prefix = type == QStringLiteral("NavigationColumn") ? "singleton " : "";
    qmldir += prefix + type.toUtf8() + " 1.0 " + file.toUtf8() + '\n';
    return true;
}

QQuickItem *createProbe(QTemporaryDir &dir, QQuickView &view)
{
    const QString modulePath = dir.filePath(QStringLiteral("StrmQt"));
    if (!QDir().mkpath(modulePath))
        return nullptr;
    QByteArray qmldir = "module StrmQt\nsingleton Theme 1.0 Theme.qml\n";
    if (!QFile::copy(QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/Theme.qml"), modulePath + QStringLiteral("/Theme.qml")))
        return nullptr;
    for (const QString &type : kControls) {
        if (!stage(QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/controls/"), type, modulePath, qmldir))
            return nullptr;
    }
    for (const QString &type : kMusic) {
        if (!stage(QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/music/"), type, modulePath, qmldir))
            return nullptr;
    }
    QFile qmldirFile(modulePath + QStringLiteral("/qmldir"));
    if (!qmldirFile.open(QIODevice::WriteOnly))
        return nullptr;
    qmldirFile.write(qmldir);
    qmldirFile.close();

    QFile probe(dir.filePath(QStringLiteral("Probe.qml")));
    if (!probe.open(QIODevice::WriteOnly))
        return nullptr;
    probe.write(kProbe);
    probe.close();

    view.engine()->addImportPath(dir.path());
    view.setSource(QUrl::fromLocalFile(probe.fileName()));
    if (view.status() != QQuickView::Ready) {
        qWarning() << view.errors();
        return nullptr;
    }
    view.resize(1000, 900);
    view.show();
    if (!QTest::qWaitForWindowExposed(&view))
        return nullptr;
    return view.rootObject();
}

QPoint centreOf(QQuickItem *item)
{
    return item->mapToScene(QPointF(item->width() / 2, item->width() / 2)).toPoint();
}

} // namespace

class CrateControlsTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void headingAndKickerUseCrateType();
    void badgeHidesWhenEmptyAndTurnsAmberForHiRes();
    void collageSwitchesToAGridAtFourCovers();
    void sleeveClicksAndMenus();
    void hoverIsNotFocus();
    void tilesActivate();
    void allGenresBinShowsTheArrow();
    void stripCyclesWithoutMovingItsKey();
    void stripKeyboardChoosesAndDeclinesLeftAtTheEdge();
    void shelfErrorRetries();
    void shelfHidesWhileTheLaneIsEmpty();
    void shelfSkeletonHasTheRealShape();
    void shelfErrorLineRetriesTheLane();
    void shelfDrawsTheDelegateAndForwardsActivation();
    void shelfErrorAndRailSwapFocusReactively();
    void shelfUpReachesTheActionThenDeclines();

private:
    QTemporaryDir m_dir;
    QQuickView m_view;
    QQuickItem *m_root = nullptr;
    QQuickItem *item(const char *name) { return findItem(m_root, QString::fromLatin1(name)); }
};

void CrateControlsTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_root = createProbe(m_dir, m_view);
    QVERIFY(m_root);
}

void CrateControlsTest::headingAndKickerUseCrateType()
{
    const QFont heading = item("heading")->property("font").value<QFont>();
    QCOMPARE(heading.family(), m_root->property("displayFamily").toString());
    QCOMPARE(heading.capitalization(), QFont::AllUppercase);
    QCOMPARE(int(heading.weight()), 820);
    QVERIFY(heading.letterSpacing() < 0);

    const QFont kicker = item("kicker")->property("font").value<QFont>();
    QCOMPARE(kicker.family(), m_root->property("monoFamily").toString());
    QCOMPARE(kicker.capitalization(), QFont::AllUppercase);
    QVERIFY(kicker.letterSpacing() > 0);
}

void CrateControlsTest::badgeHidesWhenEmptyAndTurnsAmberForHiRes()
{
    QVERIFY(!item("emptyBadge")->isVisible());
    QVERIFY(item("plainBadge")->isVisible());
    const QObject *plainBorder = item("plainBadge")->property("border").value<QObject *>();
    const QObject *hiResBorder = item("hiResBadge")->property("border").value<QObject *>();
    QVERIFY(plainBorder && hiResBorder);
    const QColor hiRes = m_root->property("hiResTone").value<QColor>();
    QCOMPARE(hiResBorder->property("color").value<QColor>(), hiRes);
    QVERIFY(plainBorder->property("color").value<QColor>() != hiRes);
}

void CrateControlsTest::collageSwitchesToAGridAtFourCovers()
{
    QVERIFY(!item("oneCover")->property("grid").toBool());
    QVERIFY(item("fourCovers")->property("grid").toBool());
    QCOMPARE(qRound(item("fourCovers")->width()), 80);
}

void CrateControlsTest::sleeveClicksAndMenus()
{
    QQuickItem *sleeve = item("sleeve");
    QTest::mouseClick(&m_view, Qt::LeftButton, {}, centreOf(sleeve));
    QTRY_COMPARE(m_root->property("sleeveActivated").toInt(), 1);
    QTest::mouseClick(&m_view, Qt::RightButton, {}, centreOf(sleeve));
    QTRY_COMPARE(m_root->property("sleeveMenus").toInt(), 1);
    // The caption sits below the square art, so the sleeve is taller than wide.
    QVERIFY(sleeve->height() > sleeve->width());
}

void CrateControlsTest::hoverIsNotFocus()
{
    QQuickItem *sleeve = item("sleeve");
    m_root->forceActiveFocus();
    QTest::mouseMove(&m_view, centreOf(sleeve));
    QTest::mouseMove(&m_view, centreOf(sleeve) + QPoint(1, 1));
    QTRY_VERIFY(sleeve->property("hovered").toBool());
    QVERIFY(!sleeve->property("current").toBool());
    QVERIFY(!sleeve->hasActiveFocus());
    QVERIFY(m_root->hasActiveFocus());
}

void CrateControlsTest::tilesActivate()
{
    QTest::mouseClick(&m_view, Qt::LeftButton, {}, centreOf(item("portrait")));
    QTRY_COMPARE(m_root->property("portraitActivated").toInt(), 1);
    QTest::mouseClick(&m_view, Qt::LeftButton, {}, centreOf(item("station")));
    QTRY_COMPARE(m_root->property("stationActivated").toInt(), 1);
    QTest::mouseClick(&m_view, Qt::LeftButton, {}, centreOf(item("genre")));
    QTRY_COMPARE(m_root->property("genreActivated").toInt(), 1);
}

void CrateControlsTest::allGenresBinShowsTheArrow()
{
    const QQuickItem *arrow = item("allGenres")->property("allArrow").value<QQuickItem *>();
    QVERIFY(arrow);
    QVERIFY(arrow->isVisible());
    const QQuickItem *plainArrow = item("genre")->property("allArrow").value<QQuickItem *>();
    QVERIFY(plainArrow);
    QVERIFY(!plainArrow->isVisible());
}

void CrateControlsTest::stripCyclesWithoutMovingItsKey()
{
    // Ruling P2-R1: `cycle(step)` returns a QML `bool`, and Qt 6.11's
    // QMetaObject::invokeMethod cannot marshal that through Q_RETURN_ARG(QVariant, ...)
    // (the QML function's declared return type is bool, not var/QVariant). Reading it
    // back with Q_RETURN_ARG(bool, ...) is what actually works.
    QQuickItem *strip = item("strip");
    bool consumed = false;
    QVERIFY(QMetaObject::invokeMethod(strip, "cycle", Q_RETURN_ARG(bool, consumed), Q_ARG(QVariant, 1)));
    QVERIFY(consumed);
    QCOMPARE(m_root->property("lastSection").toString(), QStringLiteral("albums"));
    QCOMPARE(strip->property("currentKey").toString(), QStringLiteral("home"));

    QVERIFY(QMetaObject::invokeMethod(strip, "cycle", Q_RETURN_ARG(bool, consumed), Q_ARG(QVariant, -1)));
    QCOMPARE(m_root->property("lastSection").toString(), QStringLiteral("playlists"));

    strip->setProperty("keys", QStringList{QStringLiteral("home")});
    QVERIFY(QMetaObject::invokeMethod(strip, "cycle", Q_RETURN_ARG(bool, consumed), Q_ARG(QVariant, 1)));
    QVERIFY(!consumed);
    strip->setProperty("keys", QStringList{QStringLiteral("home"), QStringLiteral("albums"), QStringLiteral("artists"),
                                           QStringLiteral("songs"), QStringLiteral("genres"),
                                           QStringLiteral("playlists")});
}

void CrateControlsTest::stripKeyboardChoosesAndDeclinesLeftAtTheEdge()
{
    QQuickItem *strip = item("strip");
    m_root->setProperty("lastSection", QString());
    m_root->setProperty("leftEscaped", false);
    strip->forceActiveFocus();
    QTRY_VERIFY(strip->hasActiveFocus());
    QCOMPARE(strip->property("cursor").toInt(), 0);

    QTest::keyClick(&m_view, Qt::Key_Left);
    QVERIFY(m_root->property("leftEscaped").toBool());

    QTest::keyClick(&m_view, Qt::Key_Right);
    QCOMPARE(strip->property("cursor").toInt(), 1);
    QVERIFY(m_root->property("lastSection").toString().isEmpty());
    QTest::keyClick(&m_view, Qt::Key_Return);
    QCOMPARE(m_root->property("lastSection").toString(), QStringLiteral("albums"));

    // Losing focus puts the cursor back on the section that is on screen.
    m_root->forceActiveFocus();
    QTRY_COMPARE(strip->property("cursor").toInt(), 0);
}

void CrateControlsTest::shelfErrorRetries()
{
    QQuickItem *error = item("shelfError");
    error->forceActiveFocus();
    QTest::keyClick(&m_view, Qt::Key_Return);
    QTRY_COMPARE(m_root->property("retried").toInt(), 1);
}

void CrateControlsTest::shelfHidesWhileTheLaneIsEmpty()
{
    QQuickItem *shelf = item("shelf");
    QVERIFY(shelf);
    QVERIFY(!shelf->isVisible());
    QVERIFY(!shelf->property("focusable").toBool());
}

void CrateControlsTest::shelfSkeletonHasTheRealShape()
{
    QQuickItem *shelf = item("shelf");
    QVERIFY(QMetaObject::invokeMethod(m_root, "setLane", Q_ARG(QVariant, true), Q_ARG(QVariant, QString())));
    QTRY_VERIFY(shelf->isVisible());
    QVERIFY(shelf->property("showSkeleton").toBool());
    QVERIFY(!shelf->property("focusable").toBool());
    QQuickItem *first = findItem(shelf, QStringLiteral("crateShelfSkeleton-0"));
    QVERIFY(first);
    QVERIFY(first->isVisible());
    QCOMPARE(qRound(first->width()), 100);
    QCOMPARE(qRound(first->property("radius").toReal()), 50); // round: a portrait's circle
    QQuickItem *rail = shelf->property("rail").value<QQuickItem *>();
    QVERIFY(rail);
    QVERIFY(!rail->isVisible());
}

void CrateControlsTest::shelfErrorLineRetriesTheLane()
{
    QQuickItem *shelf = item("shelf");
    QVERIFY(QMetaObject::invokeMethod(m_root, "setLane", Q_ARG(QVariant, false),
                                      Q_ARG(QVariant, QStringLiteral("Couldn't load"))));
    QTRY_VERIFY(shelf->property("showError").toBool());
    QVERIFY(!shelf->property("showSkeleton").toBool());
    QVERIFY(shelf->property("focusable").toBool());
    shelf->forceActiveFocus();
    QTest::keyClick(&m_view, Qt::Key_Return);
    QObject *lane = m_root->findChild<QObject *>(QStringLiteral("fakeLane"));
    QVERIFY(lane);
    QTRY_COMPARE(lane->property("retries").toInt(), 1);
    QVERIFY(QMetaObject::invokeMethod(m_root, "setLane", Q_ARG(QVariant, false), Q_ARG(QVariant, QString())));
}

void CrateControlsTest::shelfDrawsTheDelegateAndForwardsActivation()
{
    QQuickItem *shelf = item("shelf");
    QVERIFY(QMetaObject::invokeMethod(m_root, "fillShelf"));
    QTRY_VERIFY(findItem(shelf, QStringLiteral("shelfSleeve-b")));
    QVERIFY(!shelf->property("showSkeleton").toBool());
    QVERIFY(!shelf->property("showError").toBool());
    QQuickItem *rail = shelf->property("rail").value<QQuickItem *>();
    QVERIFY(rail->isVisible());

    QQuickItem *b = findItem(shelf, QStringLiteral("shelfSleeve-b"));
    QTest::mouseClick(&m_view, Qt::LeftButton, {}, b->mapToScene(QPointF(50, 50)).toPoint());
    QTRY_COMPARE(m_root->property("shelfActivated").toInt(), 1);
}

void CrateControlsTest::shelfErrorAndRailSwapFocusReactively()
{
    QQuickItem *shelf = item("shelf");
    QQuickItem *rail = shelf->property("rail").value<QQuickItem *>();
    QVERIFY(rail);
    shelf->forceActiveFocus();
    QTRY_VERIFY(rail->hasActiveFocus());

    QVERIFY(QMetaObject::invokeMethod(m_root, "setLane", Q_ARG(QVariant, false),
                                      Q_ARG(QVariant, QStringLiteral("Couldn't load"))));
    QQuickItem *error = findItem(shelf, QStringLiteral("crateShelfError"));
    QVERIFY(error);
    QTRY_VERIFY(error->hasActiveFocus());
    QVERIFY(!rail->hasActiveFocus());

    // Error clears and the lane still has the rows fillShelf() left in it: the
    // scope's remembered focus child must swap back to the rail on its own.
    QVERIFY(QMetaObject::invokeMethod(m_root, "setLane", Q_ARG(QVariant, false), Q_ARG(QVariant, QString())));
    QTRY_VERIFY(rail->hasActiveFocus());
    QVERIFY(!error->hasActiveFocus());
}

void CrateControlsTest::shelfUpReachesTheActionThenDeclines()
{
    QQuickItem *shelf = item("shelf");
    QVERIFY(QMetaObject::invokeMethod(m_root, "fillShelf"));
    m_root->setProperty("upEscaped", false);
    shelf->forceActiveFocus();
    QQuickItem *rail = shelf->property("rail").value<QQuickItem *>();
    QTRY_VERIFY(rail->hasActiveFocus());

    QTest::keyClick(&m_view, Qt::Key_Up);
    QQuickItem *action = findItem(shelf, QStringLiteral("crateShelfAction"));
    QVERIFY(action);
    QTRY_VERIFY(action->hasActiveFocus());
    QVERIFY(!m_root->property("upEscaped").toBool());

    QTest::keyClick(&m_view, Qt::Key_Return);
    QTRY_COMPARE(m_root->property("shelfActions").toInt(), 1);

    QTest::keyClick(&m_view, Qt::Key_Up);
    QVERIFY(m_root->property("upEscaped").toBool());

    QTest::keyClick(&m_view, Qt::Key_Down);
    QTRY_VERIFY(rail->hasActiveFocus());

    // Re-enter the action button, then let focus leave the shelf entirely —
    // the page moving on after a declined Up, rather than a Down that returns
    // it to the rail itself. The shelf's remembered focus child must follow
    // back to the rail, not stay parked on the action button: otherwise the
    // shelf's next forceActiveFocus() (Tab, or the page restoring a place)
    // reopens on the action button instead of the rail.
    QTest::keyClick(&m_view, Qt::Key_Up);
    QTRY_VERIFY(action->hasActiveFocus());
    m_root->forceActiveFocus();
    QVERIFY(!action->hasActiveFocus());
    shelf->forceActiveFocus();
    QTRY_VERIFY(rail->hasActiveFocus());
}

QTEST_MAIN(CrateControlsTest)
#include "tst_crate_controls.moc"
