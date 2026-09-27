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
    height: 1100
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
    property int pillActivated: 0
    property int pillCleared: 0
    property string letterChosenValue: ""
    property var genresChosenIds: []
    property int genreDismissed: 0
    property int digitShortcutFired: 0

    Keys.onUpPressed: root.upEscaped = true

    // The number keys' shape in Main.qml (library.open1): a bare-digit window
    // Shortcut, which a focused CrateDividers must still win against.
    Shortcut {
        sequence: "1"
        onActivated: root.digitShortcutFired++
    }
    readonly property color hiResTone: Theme.crateBadgeHiRes
    readonly property color accentTone: Theme.accentColor
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

    // Placed below CrateShelf's footprint (y: 660, up to ~848 once populated)
    // so mouse events aimed at these two never land on the shelf instead.
    FilterPill {
        id: pill
        objectName: "pill"
        x: 20; y: 900
        text: "Decade: 70s"
        active: true
        onActivated: root.pillActivated++
        onCleared: root.pillCleared++
    }

    CrateDividers {
        id: dividers
        objectName: "dividers"
        x: 960; y: 900
        height: 180
        letters: ["#", "A", "B", "C"]
        currentLetter: "A"
        onLetterChosen: letter => root.letterChosenValue = letter
    }

    GenrePicker {
        id: genrePicker
        objectName: "genrePicker"
        options: [
            { id: "1", name: "Alpha", subtitle: "10 records", selected: false },
            { id: "2", name: "Beta", subtitle: "5 records", selected: true }
        ]
        onGenresChosen: ids => root.genresChosenIds = ids
        onDismissed: root.genreDismissed++
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
    QStringLiteral("StrmRail"),       QStringLiteral("StrmSkeleton"), QStringLiteral("StrmSearchField"),
    QStringLiteral("FocusClip"),
};

const QStringList kMusic = {
    QStringLiteral("CrateHeading"), QStringLiteral("CrateKicker"),  QStringLiteral("CrateBadge"),
    QStringLiteral("CoverCollage"), QStringLiteral("CrateSleeve"),  QStringLiteral("CratePortrait"),
    QStringLiteral("StationTile"),  QStringLiteral("GenreBinTile"), QStringLiteral("SectionStrip"),
    QStringLiteral("ShelfError"),   QStringLiteral("CrateShelf"),
    QStringLiteral("FilterPill"),   QStringLiteral("GenrePicker"),  QStringLiteral("CrateDividers"),
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

int countSleeves(QQuickItem *from)
{
    int total = from->objectName().startsWith(QStringLiteral("shelfSleeve-")) ? 1 : 0;
    for (QQuickItem *child : from->childItems())
        total += countSleeves(child);
    return total;
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
    view.resize(1000, 1100);
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
    void shelfDropsItsRowsWhileCoveredAndPutsTheCursorBack();
    // Ruling P3-R2: two focus bugs reached the user in Phase 2 (StrmRail's
    // hover chevrons stealing a Tab stop; hover being mistaken for focus).
    // Every control added in Task 4 is checked against both.
    void filterPillActivatesAndClears();
    void filterPillClearAffordanceIsNotATabStop();
    void filterPillShowsItsFocusRing();
    void crateDividersLettersAreNotTabStops();
    void crateDividersShowsItsFocusRing();
    void crateDividersChoosesWithTheKeyboard();
    void crateDividersKeepsTypedDigitsFromTheNumberKeys();
    void genrePickerRowHoverPreviewsWithoutStealingFocus();
    void genrePickerOpenShowsFocusOnTheSearchField();
    void genrePickerHiddenButtonsAreNotTabStops();

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

// Remedy 2 of the memory fix: a shelf on a covered page drops its delegates and
// their decoded covers, and getting it back is a cursor restore, not a focus
// grab — seven shelves on one page would otherwise fight over the keyboard, and
// the navigation history's own locator would lose.
void CrateControlsTest::shelfDropsItsRowsWhileCoveredAndPutsTheCursorBack()
{
    QQuickItem *shelf = item("shelf");
    QVERIFY(QMetaObject::invokeMethod(m_root, "fillShelf"));
    QQuickItem *rail = shelf->property("rail").value<QQuickItem *>();
    QVERIFY(rail);
    QTRY_COMPARE(rail->property("count").toInt(), 3);

    // Every delegate still in the shelf's item tree, pooled ones included.
    const auto sleeveCount = [shelf] { return countSleeves(shelf); };
    const int rows = rail->property("count").toInt();
    QCOMPARE(rows, 3);
    QTRY_VERIFY(sleeveCount() >= rows);
    const int populated = sleeveCount();

    // Walk the cursor onto the last card with the keyboard. Stepping until it
    // arrives rather than pressing a fixed number of times keeps the test
    // independent of wherever an earlier test left the cursor.
    QTRY_VERIFY(findItem(shelf, QStringLiteral("shelfSleeve-c")));
    shelf->forceActiveFocus();
    QTRY_VERIFY(rail->hasActiveFocus());
    for (int guard = 0; guard < 6 && rail->property("currentIndex").toInt() < 2; ++guard)
        QTest::keyClick(&m_view, Qt::Key_Right);
    QTRY_COMPARE(rail->property("currentIndex").toInt(), 2);

    // A covered page has the keyboard somewhere else entirely.
    QQuickItem *pill = item("pill");
    pill->forceActiveFocus();
    QTRY_VERIFY(pill->hasActiveFocus());

    // Covered: the view holds no rows, and the delegates that drew them — with
    // the covers they had decoded — are released. A rail reuses delegates, so
    // the sleeves that survive are pooled shells this fixture's earlier tests
    // left behind, which is why the count is asserted as a drop of one per row
    // rather than as zero.
    shelf->setProperty("contentActive", false);
    QTRY_COMPARE(rail->property("count").toInt(), 0);
    QTRY_VERIFY(sleeveCount() <= populated - rows);
    QCOMPARE(shelf->property("lane").value<QObject *>(), m_root->findChild<QObject *>("fakeLane"));

    // Uncovered: the rows come back, the cursor is where the user left it, and
    // the keyboard has not moved.
    shelf->setProperty("contentActive", true);
    QTRY_COMPARE(rail->property("count").toInt(), rows);
    QTRY_COMPARE(rail->property("currentIndex").toInt(), 2);
    QVERIFY(pill->hasActiveFocus());
    QVERIFY(!rail->hasActiveFocus());
}

// ── Task 4 (P3-R2): FilterPill, CrateDividers, GenrePicker ─────────────────

void CrateControlsTest::filterPillActivatesAndClears()
{
    QQuickItem *pill = item("pill");
    QVERIFY(pill);
    const QPoint centre = pill->mapToScene(QPointF(pill->width() / 2, pill->height() / 2)).toPoint();
    QTest::mouseClick(&m_view, Qt::LeftButton, {}, centre);
    QTRY_COMPARE(m_root->property("pillActivated").toInt(), 1);
    QTRY_VERIFY(pill->hasActiveFocus());

    QTest::keyClick(&m_view, Qt::Key_Delete);
    QTRY_COMPARE(m_root->property("pillCleared").toInt(), 1);
}

// Ruling P3-R2, point 1: the x that clears a pill is only ever shown while
// the pill is clearable — like StrmRail's hover chevrons, it must never pick
// up a Tab stop of its own. The pill itself stays the one stop.
void CrateControlsTest::filterPillClearAffordanceIsNotATabStop()
{
    QQuickItem *pill = item("pill");
    QQuickItem *clear = item("filterPillClear");
    QVERIFY(pill);
    QVERIFY(clear);
    QVERIFY(clear->isVisible());
    QVERIFY(!clear->activeFocusOnTab());
    QVERIFY(pill->activeFocusOnTab());
}

// Ruling P3-R2, point 2: a focusable control must show that it holds focus.
void CrateControlsTest::filterPillShowsItsFocusRing()
{
    QQuickItem *pill = item("pill");
    QQuickItem *ring = item("filterPillFocusRing");
    QVERIFY(pill);
    QVERIFY(ring);

    m_root->forceActiveFocus();
    QTRY_VERIFY(!pill->hasActiveFocus());
    QTRY_VERIFY(!ring->property("active").toBool());

    pill->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(pill->hasActiveFocus());
    QTRY_VERIFY(ring->property("active").toBool());
}

// Ruling P3-R2, point 1: 27 focusable letters would flood the focus chain
// (like the plan's SectionStrip/FilterBar strips), so every divider letter is
// explicitly not a Tab stop; `dividers` alone is.
void CrateControlsTest::crateDividersLettersAreNotTabStops()
{
    QQuickItem *dividers = item("dividers");
    QVERIFY(dividers);
    QVERIFY(dividers->activeFocusOnTab());

    static const char *kLetters[] = {"crateDividersLetter-#", "crateDividersLetter-A",
                                      "crateDividersLetter-B", "crateDividersLetter-C"};
    for (const char *name : kLetters) {
        QQuickItem *tab = item(name);
        QVERIFY(tab);
        QVERIFY(!tab->activeFocusOnTab());
    }
}

void CrateControlsTest::crateDividersShowsItsFocusRing()
{
    QQuickItem *dividers = item("dividers");
    QQuickItem *ring = item("crateDividersFocusRing");
    QVERIFY(dividers);
    QVERIFY(ring);

    m_root->forceActiveFocus();
    QTRY_VERIFY(!dividers->hasActiveFocus());
    QTRY_VERIFY(!ring->property("active").toBool());

    dividers->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(dividers->hasActiveFocus());
    QTRY_VERIFY(ring->property("active").toBool());
}

void CrateControlsTest::crateDividersChoosesWithTheKeyboard()
{
    QQuickItem *dividers = item("dividers");
    QVERIFY(dividers);
    m_root->setProperty("letterChosenValue", QString());
    dividers->forceActiveFocus();
    QTRY_VERIFY(dividers->hasActiveFocus());
    // letters: ["#", "A", "B", "C"], currentLetter "A" -> cursor starts at 1.
    QCOMPARE(dividers->property("cursor").toInt(), 1);

    QTest::keyClick(&m_view, Qt::Key_Down);
    QCOMPARE(dividers->property("cursor").toInt(), 2);
    QTest::keyClick(&m_view, Qt::Key_Return);
    QCOMPARE(m_root->property("letterChosenValue").toString(), QStringLiteral("B"));
}

// A typed digit chooses "#" while the dividers hold focus, even though a bare
// digit is also a window Shortcut (the number keys open libraries).
void CrateControlsTest::crateDividersKeepsTypedDigitsFromTheNumberKeys()
{
    QQuickItem *dividers = item("dividers");
    QVERIFY(dividers);
    m_root->setProperty("letterChosenValue", QString());
    m_root->setProperty("digitShortcutFired", 0);
    dividers->forceActiveFocus();
    QTRY_VERIFY(dividers->hasActiveFocus());

    QTest::keyClick(&m_view, Qt::Key_1);
    QCOMPARE(m_root->property("letterChosenValue").toString(), QStringLiteral("#"));
    QCOMPARE(m_root->property("digitShortcutFired").toInt(), 0);

    // Anywhere else, the digit is the shortcut's again.
    m_root->forceActiveFocus();
    QTRY_VERIFY(!dividers->hasActiveFocus());
    QTest::keyClick(&m_view, Qt::Key_1);
    QTRY_COMPARE(m_root->property("digitShortcutFired").toInt(), 1);
}

// Ruling P3-R2, point 1: a row previews on hover only (a hovered row must not
// steal the caret from the search field, the same "hover is not focus" bug
// class as hoverIsNotFocus() above), and never becomes a Tab stop itself.
void CrateControlsTest::genrePickerRowHoverPreviewsWithoutStealingFocus()
{
    QQuickItem *picker = item("genrePicker");
    QVERIFY(picker);
    QVERIFY(QMetaObject::invokeMethod(picker, "open"));
    QTRY_VERIFY(picker->property("opened").toBool());

    QQuickItem *field = item("genrePickerField");
    QVERIFY(field);
    QTRY_VERIFY(field->hasActiveFocus());

    // The ListView's own height is bound to its contentHeight (Math.min(...)),
    // so the second row's delegate is not necessarily live on the same tick
    // as the first: give the layout a few event-loop turns, as
    // shelfDrawsTheDelegateAndForwardsActivation() does above.
    QQuickItem *row = nullptr;
    QTRY_VERIFY((row = item("genrePickerRow-2")) != nullptr);
    QVERIFY(!row->activeFocusOnTab());

    QTest::mouseMove(&m_view, row->mapToScene(QPointF(row->width() / 2, row->height() / 2)).toPoint());
    QTest::qWait(20);
    QVERIFY(field->hasActiveFocus());
    QVERIFY(!row->hasActiveFocus());

    QVERIFY(QMetaObject::invokeMethod(picker, "close"));
    QTRY_VERIFY(!picker->property("opened").toBool());
}

// Ruling P3-R2, point 2: open() hands real, visible keyboard focus to the
// search field, whose own border is the focus indicator (StrmSearchField).
// Ruling P3-R2 again, for the two buttons that are hidden most of the time:
// Retry (only while the load failed) and the search field's clear "x" (only
// while there is text). Both are built on controls whose activeFocusOnTab
// follows `enabled`, never `visible`, so without an explicit binding they hold
// a Tab stop nobody can see — the rail-chevron bug of 6f6b120.
void CrateControlsTest::genrePickerHiddenButtonsAreNotTabStops()
{
    QQuickItem *picker = item("genrePicker");
    QVERIFY(picker);
    QVERIFY(QMetaObject::invokeMethod(picker, "open"));
    QTRY_VERIFY(picker->property("opened").toBool());

    QQuickItem *retry = item("genrePickerRetry");
    QVERIFY(retry);
    QVERIFY(!retry->isVisible());
    QVERIFY(!retry->activeFocusOnTab());

    // Visible again when the picker reports a failure with nothing to list
    // (the hint row only shows with no rows): the guard tracks the button
    // rather than switching it off for good.
    const QVariant options = picker->property("options");
    picker->setProperty("options", QVariantList{});
    picker->setProperty("failed", true);
    QTRY_VERIFY(retry->isVisible());
    QVERIFY(retry->activeFocusOnTab());
    picker->setProperty("failed", false);
    picker->setProperty("options", options);
    QTRY_VERIFY(!retry->isVisible());

    QQuickItem *field = item("genrePickerField");
    QVERIFY(field);
    QQuickItem *clear = findItem(field, QStringLiteral("searchFieldClear"));
    QVERIFY(clear);
    QCOMPARE(field->property("text").toString(), QString());
    QVERIFY(!clear->isVisible());
    QVERIFY(!clear->activeFocusOnTab());

    field->setProperty("text", QStringLiteral("jazz"));
    QTRY_VERIFY(clear->isVisible());
    QVERIFY(clear->activeFocusOnTab());
    field->setProperty("text", QString());
    QTRY_VERIFY(!clear->isVisible());
}

void CrateControlsTest::genrePickerOpenShowsFocusOnTheSearchField()
{
    QQuickItem *picker = item("genrePicker");
    QVERIFY(picker);
    QVERIFY(QMetaObject::invokeMethod(picker, "open"));

    QQuickItem *field = item("genrePickerField");
    QVERIFY(field);
    QTRY_VERIFY(field->hasActiveFocus());

    QQuickItem *background = field->property("background").value<QQuickItem *>();
    QVERIFY(background);
    QObject *border = background->property("border").value<QObject *>();
    QVERIFY(border);
    const QColor accent = m_root->property("accentTone").value<QColor>();
    QTRY_COMPARE(border->property("color").value<QColor>(), accent);

    QVERIFY(QMetaObject::invokeMethod(picker, "close"));
}

QTEST_MAIN(CrateControlsTest)
#include "tst_crate_controls.moc"
