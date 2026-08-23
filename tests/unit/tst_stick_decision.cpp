#include <QtTest>

#include "input/StickDecision.h"

using namespace strmqt;

// The user's report: "when trying to scroll sideways it is too easy to go up or
// down; it should be more deliberate". The cause was treating X and Y as two
// independent axes with one threshold, so a push toward 2 o'clock crossed both
// and fired right AND up.
class StickDecisionTest : public QObject
{
    Q_OBJECT

private slots:
    void deadzoneRejectsRest();
    void cardinalPushesPickTheirAxis();
    void diagonalsResolveToOneAxis();
    void verticalNeedsMoreCommitmentThanHorizontal();
    void ownedAxisSurvivesAWanderingSweep();
    void deliberateChangeOfDirectionStillWins();
};

void StickDecisionTest::deadzoneRejectsRest()
{
    // A worn pad rests off centre; that must not move anything.
    QCOMPARE(decideStickAxis(0, 0, StickAxis::None), StickAxis::None);
    QCOMPARE(decideStickAxis(6000, 4000, StickAxis::None), StickAxis::None);
    QCOMPARE(decideStickAxis(-5000, 5000, StickAxis::None), StickAxis::None);
}

void StickDecisionTest::cardinalPushesPickTheirAxis()
{
    QCOMPARE(decideStickAxis(30000, 0, StickAxis::None), StickAxis::Horizontal);
    QCOMPARE(decideStickAxis(-30000, 0, StickAxis::None), StickAxis::Horizontal);
    QCOMPARE(decideStickAxis(0, 30000, StickAxis::None), StickAxis::Vertical);
    QCOMPARE(decideStickAxis(0, -30000, StickAxis::None), StickAxis::Vertical);
}

void StickDecisionTest::diagonalsResolveToOneAxis()
{
    // The reported case: a sideways push that drifts upward. Horizontal leads,
    // so nothing may move vertically.
    QCOMPARE(decideStickAxis(28000, -14000, StickAxis::None), StickAxis::Horizontal);
    QCOMPARE(decideStickAxis(28000, 14000, StickAxis::None), StickAxis::Horizontal);

    // A true 45° push says neither: the user has not chosen, so nothing moves.
    // This is the wedge, and it is deliberate — guessing here is what produced
    // the drift.
    QCOMPARE(decideStickAxis(24000, 24000, StickAxis::None), StickAxis::None);
    QCOMPARE(decideStickAxis(-24000, 24000, StickAxis::None), StickAxis::None);
}

void StickDecisionTest::verticalNeedsMoreCommitmentThanHorizontal()
{
    // A hand rolls up-down far more easily than left-right, so up/down is what
    // gets hit by accident and has to clear a higher bar.
    const int between = 14000; // above the horizontal threshold, below vertical
    QCOMPARE(decideStickAxis(between, 0, StickAxis::None), StickAxis::Horizontal);
    QCOMPARE(decideStickAxis(0, between, StickAxis::None), StickAxis::None);

    // Push vertically in earnest and it works.
    QCOMPARE(decideStickAxis(0, 20000, StickAxis::None), StickAxis::Vertical);
}

void StickDecisionTest::ownedAxisSurvivesAWanderingSweep()
{
    // Holding right, the hand drifts upward mid-sweep. Without ownership this
    // flips to vertical halfway through a scroll.
    StickAxis owned = StickAxis::Horizontal;
    QCOMPARE(decideStickAxis(26000, 17000, owned), StickAxis::Horizontal);
    QCOMPARE(decideStickAxis(20000, 19000, owned), StickAxis::Horizontal);
    // Even once vertical is nominally larger, it has not earned the takeover.
    QCOMPARE(decideStickAxis(15000, 22000, owned), StickAxis::Horizontal);

    // Releasing toward centre gives the stick up.
    QCOMPARE(decideStickAxis(3000, 3000, owned), StickAxis::None);
}

void StickDecisionTest::deliberateChangeOfDirectionStillWins()
{
    // Ownership must not become a trap: a real change of direction has to work
    // without returning to centre first.
    const StickAxis owned = StickAxis::Horizontal;
    QCOMPARE(decideStickAxis(6000, 30000, owned), StickAxis::Vertical);

    // And the same in reverse.
    QCOMPARE(decideStickAxis(30000, 6000, StickAxis::Vertical), StickAxis::Horizontal);
}

QTEST_MAIN(StickDecisionTest)
#include "tst_stick_decision.moc"
