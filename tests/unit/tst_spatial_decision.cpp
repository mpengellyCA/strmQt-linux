// Which rectangle a direction lands on (src/input/SpatialDecision.h).
//
// The layouts below are the ones the per-page wiring kept getting wrong: a
// row of buttons over a full-width shelf, a grid beside a side column, a
// header control far off to one side.

#include "input/SpatialDecision.h"

#include <QTest>

using strmqt::NavDirection;
using strmqt::pickNearest;
using strmqt::pickSpatialTarget;

class SpatialDecisionTest : public QObject
{
    Q_OBJECT

private slots:
    void nothingThatWay();
    void walksARow();
    void prefersTheRowItIsIn();
    void fullWidthShelfIsStraightBelow();
    void nearerRowWinsGoingDown();
    void gridReachesASideColumn();
    void overlappingCandidateBehindIsIgnored();
    void tiesKeepTheEarlierCandidate();
    void nearestByCentre();
};

void SpatialDecisionTest::nothingThatWay()
{
    const QRectF source(100, 100, 50, 50);
    QCOMPARE(pickSpatialTarget(source, {}, NavDirection::Right), -1);
    QCOMPARE(pickSpatialTarget(source, {QRectF(0, 100, 50, 50)}, NavDirection::Right), -1);
    QCOMPARE(pickSpatialTarget(source, {QRectF(0, 100, 50, 50)}, NavDirection::Left), 0);
}

void SpatialDecisionTest::walksARow()
{
    const QList<QRectF> row{QRectF(0, 0, 80, 40), QRectF(100, 0, 80, 40), QRectF(200, 0, 80, 40)};
    QCOMPARE(pickSpatialTarget(row.at(0), row, NavDirection::Right), 1);
    QCOMPARE(pickSpatialTarget(row.at(1), row, NavDirection::Right), 2);
    QCOMPARE(pickSpatialTarget(row.at(2), row, NavDirection::Right), -1);
    QCOMPARE(pickSpatialTarget(row.at(2), row, NavDirection::Left), 1);
    QCOMPARE(pickSpatialTarget(row.at(1), row, NavDirection::Up), -1);
}

void SpatialDecisionTest::prefersTheRowItIsIn()
{
    // Right from a button: the far control in the same row beats a near one
    // that sits a row lower.
    const QRectF source(0, 0, 80, 40);
    const QList<QRectF> candidates{QRectF(90, 60, 80, 40), QRectF(600, 0, 80, 40)};
    QCOMPARE(pickSpatialTarget(source, candidates, NavDirection::Right), 1);
}

void SpatialDecisionTest::fullWidthShelfIsStraightBelow()
{
    // Down from the right-hand button of a hero row. The shelf spans the page;
    // a small control lower down and off to the left must not beat it.
    const QRectF source(900, 100, 120, 40);
    const QList<QRectF> candidates{QRectF(0, 400, 80, 40), QRectF(0, 200, 1200, 180)};
    QCOMPARE(pickSpatialTarget(source, candidates, NavDirection::Down), 1);
}

void SpatialDecisionTest::nearerRowWinsGoingDown()
{
    const QRectF source(100, 0, 100, 40);
    const QList<QRectF> candidates{QRectF(100, 300, 100, 40), QRectF(100, 100, 100, 40)};
    QCOMPARE(pickSpatialTarget(source, candidates, NavDirection::Down), 1);
    QCOMPARE(pickSpatialTarget(candidates.at(0), {source, candidates.at(1)}, NavDirection::Up), 1);
}

void SpatialDecisionTest::gridReachesASideColumn()
{
    // The last cell of a grid row, with a list to the right of the grid.
    const QRectF cell(600, 300, 150, 200);
    const QList<QRectF> candidates{QRectF(800, 100, 300, 600), QRectF(0, 0, 1100, 60)};
    QCOMPARE(pickSpatialTarget(cell, candidates, NavDirection::Right), 0);
    QCOMPARE(pickSpatialTarget(cell, candidates, NavDirection::Up), 1);
}

void SpatialDecisionTest::overlappingCandidateBehindIsIgnored()
{
    // A control that starts left of the source and ends inside it is not "to
    // the right" merely because part of it is.
    const QRectF source(100, 0, 100, 40);
    QCOMPARE(pickSpatialTarget(source, {QRectF(50, 0, 100, 40)}, NavDirection::Right), -1);
    QCOMPARE(pickSpatialTarget(source, {QRectF(150, 0, 100, 40)}, NavDirection::Right), 0);
}

void SpatialDecisionTest::tiesKeepTheEarlierCandidate()
{
    const QRectF source(100, 100, 50, 50);
    const QList<QRectF> candidates{QRectF(100, 200, 50, 50), QRectF(100, 200, 50, 50)};
    QCOMPARE(pickSpatialTarget(source, candidates, NavDirection::Down), 0);
}

void SpatialDecisionTest::nearestByCentre()
{
    QCOMPARE(pickNearest(QRectF(0, 0, 10, 10), {}), -1);
    const QList<QRectF> candidates{QRectF(500, 500, 10, 10), QRectF(20, 0, 10, 10)};
    QCOMPARE(pickNearest(QRectF(0, 0, 10, 10), candidates), 1);
}

QTEST_APPLESS_MAIN(SpatialDecisionTest)
#include "tst_spatial_decision.moc"
