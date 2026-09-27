// Tap or hold on a remote's ⏭, and the fast-forward ramp
// (src/playback/FastForwardRamp.h). Orderings a real remote cannot be made to
// reproduce on demand, so they are pinned here.

#include "playback/FastForwardRamp.h"

#include <QTest>

using strmqt::FastForwardRamp;
using Release = FastForwardRamp::Release;

class TestFastForwardRamp : public QObject
{
    Q_OBJECT

private slots:
    void rateAtFollowsTheSteps_data()
    {
        QTest::addColumn<qint64>("heldMs");
        QTest::addColumn<qreal>("rate");
        QTest::newRow("tap") << qint64(0) << 0.0;
        QTest::newRow("just short") << qint64(399) << 0.0;
        QTest::newRow("threshold") << qint64(400) << 2.0;
        QTest::newRow("first second") << qint64(1399) << 2.0;
        QTest::newRow("4x") << qint64(1400) << 4.0;
        QTest::newRow("8x") << qint64(2400) << 8.0;
        QTest::newRow("16x") << qint64(3400) << 16.0;
        QTest::newRow("32x") << qint64(4400) << 32.0;
        QTest::newRow("capped") << qint64(60'000) << 32.0;
    }

    void rateAtFollowsTheSteps()
    {
        QFETCH(qint64, heldMs);
        QFETCH(qreal, rate);
        QCOMPARE(FastForwardRamp::rateAt(heldMs), rate);
    }

    void aQuickReleaseIsATap()
    {
        FastForwardRamp ramp;
        QVERIFY(ramp.press(1000));
        QVERIFY(!ramp.update(1200));
        QVERIFY(!ramp.isHolding());
        QCOMPARE(ramp.release(1300), Release::Tap);
        QVERIFY(!ramp.isDown());
    }

    void aHoldRampsAndItsReleaseIsNotATap()
    {
        FastForwardRamp ramp;
        ramp.press(0);
        QVERIFY(ramp.update(400)); // 0 → 2×: the press became a hold
        QCOMPARE(ramp.rate(), 2.0);
        QVERIFY(!ramp.update(900));
        QVERIFY(ramp.update(1400));
        QCOMPARE(ramp.rate(), 4.0);
        QVERIFY(ramp.update(5000));
        QCOMPARE(ramp.rate(), 32.0);
        QCOMPARE(ramp.release(5100), Release::HoldEnded);
        QCOMPARE(ramp.rate(), 0.0);
    }

    // The timer may not have ticked past the threshold before the release
    // arrives; the time held still decides, so a long press never skips.
    void aLongPressIsAHoldEvenWithoutATick()
    {
        FastForwardRamp ramp;
        ramp.press(0);
        QCOMPARE(ramp.release(450), Release::HoldEnded);
    }

    void aSecondPressDoesNotRestartTheRamp()
    {
        FastForwardRamp ramp;
        QVERIFY(ramp.press(0));
        QVERIFY(!ramp.press(300));
        QVERIFY(ramp.update(400)); // measured from the first press
    }

    // A remote that sends press/release pairs while held: one tap, then
    // nothing for as long as the pairs keep coming inside the window.
    void aStutteringHoldIsOneTap()
    {
        FastForwardRamp ramp;
        QVERIFY(ramp.press(0));
        QCOMPARE(ramp.release(40), Release::Tap);
        for (qint64 t = 80; t < 2000; t += 80) {
            QVERIFY(!ramp.press(t));
            QCOMPARE(ramp.release(t + 40), Release::Ignored);
        }
        // A real second tap, later, counts…
        QVERIFY(ramp.press(2500));
        QCOMPARE(ramp.release(2600), Release::Tap);
        // …and so does a brisk double-tap at human speed.
        QVERIFY(ramp.press(2800));
        QCOMPARE(ramp.release(2900), Release::Tap);
    }

    void cancelSpendsTheRelease()
    {
        FastForwardRamp ramp;
        ramp.press(0);
        ramp.update(2000);
        ramp.cancel();
        QVERIFY(!ramp.isDown());
        QCOMPARE(ramp.release(2100), Release::Ignored);
        QCOMPARE(FastForwardRamp().release(10), Release::Ignored);
    }
};

QTEST_GUILESS_MAIN(TestFastForwardRamp)
#include "tst_fast_forward_ramp.moc"
