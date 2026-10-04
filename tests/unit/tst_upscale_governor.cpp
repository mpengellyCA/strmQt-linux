#include <QtTest>

#include "core/Settings.h"
#include "playback/upscale/GpuTimingSummary.h"
#include "playback/upscale/UpscaleGovernor.h"

#include <QSettings>
#include <QTemporaryDir>

using namespace strmqt;

class UpscaleGovernorTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void threeSecondsOverBudgetStepsOnce();
    void underThreeSecondsDoesNot();
    void dropOutsideGraceStepsAtOnce();
    void dropInsideGraceIsIgnored();
    void neverUpMidVideo();
    void ceilingPersistsPerRenderer();
    void resetCeilingClearsIt();
    void noSteppingUnderManualOverride();
    void stepsFromTheActiveRungNotTheCeiling();
    void nothingToStepWhenOff();
    void budgetFollowsFrameRateAndDebugScale();
    void unknownFrameRateUsesFallbackBudget();
    void unavailableTimingStillStepsOnDrops();
    void emptySummariesAreIgnored();
    void summaryUsesNearestRankP95();
    void resetCeilingWithoutRendererClearsEveryGpu();
    void counterResetIsANewBaseline();
    void noSteppingWhileHeld();
    void dropsWhileNotRenderingAreIgnored();
    void resumedRenderingStartsAGrace();

private:
    // Feeds an over-budget summary every stepMs over [fromMs, toMs].
    void feedOverBudget(qint64 fromMs, qint64 toMs, qint64 stepMs = 500);

    QTemporaryDir *m_dir = nullptr;
    Settings *m_settings = nullptr;
    UpscaleGovernor *m_governor = nullptr;
    qint64 m_now = 0;
};

void UpscaleGovernorTest::init()
{
    m_dir = new QTemporaryDir;
    m_settings = new Settings(m_dir->filePath(QStringLiteral("settings.ini")));
    m_governor = new UpscaleGovernor(m_settings);
    m_now = 0;
    m_governor->setClockForTests([this] { return m_now; });
    m_governor->setRenderer(QStringLiteral("Test GPU"));
    m_governor->setFrameRate(24.0);
    m_governor->setActiveRung(4);
    m_governor->beginVideo(); // grace until 2000
}

void UpscaleGovernorTest::cleanup()
{
    delete m_governor;
    delete m_settings;
    delete m_dir;
    m_governor = nullptr;
    m_settings = nullptr;
    m_dir = nullptr;
}

void UpscaleGovernorTest::feedOverBudget(qint64 fromMs, qint64 toMs, qint64 stepMs)
{
    for (m_now = fromMs; m_now <= toMs; m_now += stepMs)
        m_governor->addTimingSummary(30.0, 25.0, 12); // budget at 24 fps is 20.8
}

void UpscaleGovernorTest::threeSecondsOverBudgetStepsOnce()
{
    QSignalSpy changed(m_governor, &UpscaleGovernor::ceilingChanged);
    feedOverBudget(2500, 5500);
    QCOMPARE(m_governor->ceiling(), 3);
    // The step starts a new grace period, and a new streak after it.
    feedOverBudget(6000, 8000);
    QCOMPARE(m_governor->ceiling(), 3);
    QCOMPARE(changed.count(), 1);
}

void UpscaleGovernorTest::underThreeSecondsDoesNot()
{
    feedOverBudget(2500, 5400, 100); // 2.9 s over budget
    QCOMPARE(m_governor->ceiling(), 4);
    m_now = 5500;
    m_governor->addTimingSummary(10.0, 8.0, 12); // back under: the streak ends
    feedOverBudget(6000, 8900, 100);
    QCOMPARE(m_governor->ceiling(), 4);
}

void UpscaleGovernorTest::dropOutsideGraceStepsAtOnce()
{
    m_now = 2500;
    m_governor->addDropCounts(0, 0); // baseline
    m_now = 2600;
    m_governor->addDropCounts(1, 0);
    QCOMPARE(m_governor->ceiling(), 3);
    m_now = 5000;
    m_governor->addDropCounts(1, 1); // vo-delayed-frame-count counts too
    QCOMPARE(m_governor->ceiling(), 2);
}

void UpscaleGovernorTest::dropInsideGraceIsIgnored()
{
    m_now = 100;
    m_governor->addDropCounts(0, 0);
    m_now = 500;
    m_governor->addDropCounts(3, 0);
    QCOMPARE(m_governor->ceiling(), 4);
    m_now = 2500;
    m_governor->addDropCounts(3, 0); // no new drops
    QCOMPARE(m_governor->ceiling(), 4);
    // A seek, a load or a chain change opens a new grace period.
    m_governor->noteTransient();
    m_now = 3000;
    m_governor->addDropCounts(9, 0);
    QCOMPARE(m_governor->ceiling(), 4);
}

void UpscaleGovernorTest::neverUpMidVideo()
{
    feedOverBudget(2500, 5500);
    QCOMPARE(m_governor->ceiling(), 3);
    for (m_now = 6000; m_now < 60'000; m_now += 500)
        m_governor->addTimingSummary(1.0, 1.0, 12);
    QCOMPARE(m_governor->ceiling(), 3);
    m_governor->beginVideo(); // the next video starts from the saved ceiling
    QCOMPARE(m_governor->ceiling(), 3);
}

void UpscaleGovernorTest::ceilingPersistsPerRenderer()
{
    m_now = 2500;
    m_governor->addDropCounts(0, 0);
    m_governor->addDropCounts(1, 0);
    QCOMPARE(m_governor->ceiling(), 3);
    QCOMPARE(m_settings->upscaleCeiling(QStringLiteral("Test GPU")), 3);

    UpscaleGovernor again(m_settings);
    again.setRenderer(QStringLiteral("Test GPU"));
    QCOMPARE(again.ceiling(), 3);
    again.setRenderer(QStringLiteral("Another GPU"));
    QCOMPARE(again.ceiling(), 4);
}

void UpscaleGovernorTest::resetCeilingClearsIt()
{
    m_now = 2500;
    m_governor->addDropCounts(0, 0);
    m_governor->addDropCounts(1, 0);
    QCOMPARE(m_governor->ceiling(), 3);
    QSignalSpy changed(m_governor, &UpscaleGovernor::ceilingChanged);
    m_governor->resetCeiling();
    QCOMPARE(m_governor->ceiling(), 4);
    QCOMPARE(m_settings->upscaleCeiling(QStringLiteral("Test GPU")), -1);
    QCOMPARE(changed.count(), 1);
}

void UpscaleGovernorTest::noSteppingUnderManualOverride()
{
    m_governor->setManualOverride(true);
    feedOverBudget(2500, 8000);
    m_governor->addDropCounts(0, 0);
    m_governor->addDropCounts(50, 50);
    QCOMPARE(m_governor->ceiling(), 4);
    // Still recorded, for the stats overlay.
    QCOMPARE(m_governor->lastP95Ms(), 30.0);
}

void UpscaleGovernorTest::stepsFromTheActiveRungNotTheCeiling()
{
    // Below 1.5× the policy runs rung 2 under a ceiling of 4. A step must
    // change the picture, so it lands on 1, not on a no-op 3.
    m_governor->setActiveRung(2);
    m_now = 2500;
    m_governor->addDropCounts(0, 0);
    m_governor->addDropCounts(1, 0);
    QCOMPARE(m_governor->ceiling(), 1);
}

void UpscaleGovernorTest::nothingToStepWhenOff()
{
    m_governor->setActiveRung(0);
    feedOverBudget(2500, 8000);
    m_governor->addDropCounts(0, 0);
    m_governor->addDropCounts(5, 0);
    QCOMPARE(m_governor->ceiling(), 4);
}

void UpscaleGovernorTest::budgetFollowsFrameRateAndDebugScale()
{
    QCOMPARE(qRound(m_governor->budgetMs() * 10), 208); // 50% of 41.7 ms
    m_governor->setFrameRate(60.0);
    QCOMPARE(qRound(m_governor->budgetMs() * 10), 83);
    QSettings(m_dir->filePath(QStringLiteral("settings.ini")), QSettings::IniFormat)
        .setValue(QStringLiteral("upscale/budgetScale"), 0.01);
    Settings scaled(m_dir->filePath(QStringLiteral("settings.ini")));
    UpscaleGovernor governor(&scaled);
    governor.setFrameRate(24.0);
    QCOMPARE(qRound(governor.budgetMs() * 1000), 208);
}

void UpscaleGovernorTest::unknownFrameRateUsesFallbackBudget()
{
    m_governor->setFrameRate(0.0);
    QCOMPARE(qRound(m_governor->budgetMs() * 10), 167); // 30 fps fallback
    m_governor->setFrameRate(-5.0);
    QCOMPARE(qRound(m_governor->budgetMs() * 10), 167);
    m_governor->setFrameRate(qInf());
    QCOMPARE(qRound(m_governor->budgetMs() * 10), 167);
}

void UpscaleGovernorTest::unavailableTimingStillStepsOnDrops()
{
    m_now = 2500;
    m_governor->addTimingSummary(-1.0, -1.0, -1);
    QVERIFY(!m_governor->timingAvailable());
    m_governor->addDropCounts(0, 0);
    m_governor->addDropCounts(2, 0);
    QCOMPARE(m_governor->ceiling(), 3);
}

void UpscaleGovernorTest::emptySummariesAreIgnored()
{
    // Paused: frames stop, summaries carry no samples, and nothing is judged.
    for (m_now = 2500; m_now <= 9000; m_now += 500)
        m_governor->addTimingSummary(99.0, 99.0, 0);
    QCOMPARE(m_governor->ceiling(), 4);
}

void UpscaleGovernorTest::summaryUsesNearestRankP95()
{
    std::vector<double> samples;
    for (int i = 20; i >= 1; --i)
        samples.push_back(i);
    const GpuTimingSummary summary = summarizeGpuTimes(samples);
    QCOMPARE(summary.samples, 20);
    QCOMPARE(summary.p95Ms, 19.0);
    QCOMPARE(summary.meanMs, 10.5);
    QCOMPARE(summarizeGpuTimes({}).samples, 0);
    QCOMPARE(summarizeGpuTimes({4.0}).p95Ms, 4.0);
}

void UpscaleGovernorTest::resetCeilingWithoutRendererClearsEveryGpu()
{
    m_settings->setUpscaleCeiling(QStringLiteral("GPU A"), 2);
    m_settings->setUpscaleCeiling(QStringLiteral("GPU B"), 1);
    UpscaleGovernor fresh(m_settings); // no video has rendered: no renderer yet
    QVERIFY(fresh.renderer().isEmpty());
    fresh.resetCeiling();
    QCOMPARE(m_settings->upscaleCeiling(QStringLiteral("GPU A")), -1);
    QCOMPARE(m_settings->upscaleCeiling(QStringLiteral("GPU B")), -1);
    QCOMPARE(fresh.ceiling(), 4);
}

void UpscaleGovernorTest::counterResetIsANewBaseline()
{
    m_now = 2500;
    m_governor->addDropCounts(0, 0); // baseline
    m_now = 2600;
    m_governor->addDropCounts(5, 0); // a drop outside grace
    QCOMPARE(m_governor->ceiling(), 3);
    // A stream reload restarts mpv's counters. Wait out the new grace period.
    m_now = 6000;
    m_governor->addDropCounts(0, 0);
    QCOMPARE(m_governor->ceiling(), 3);
    m_now = 6100;
    m_governor->addDropCounts(1, 0);
    QCOMPARE(m_governor->ceiling(), 2);
}

void UpscaleGovernorTest::noSteppingWhileHeld()
{
    // Playback off 1× (a fast-forward hold, a user speed of 2×): drops and
    // timings are the speed's, not the GPU's.
    m_governor->setHeld(true);
    feedOverBudget(2500, 8000);
    m_governor->addDropCounts(0, 0);
    m_governor->addDropCounts(50, 50);
    QCOMPARE(m_governor->ceiling(), 4);
    QCOMPARE(m_governor->lastP95Ms(), 30.0); // still recorded

    // Back at 1×: the speed change itself is a transient... (frames keep
    // rendering, so summaries keep coming)
    m_now = 9000;
    m_governor->addTimingSummary(10.0, 8.0, 12);
    m_governor->setHeld(false);
    m_now = 10000;
    m_governor->addTimingSummary(10.0, 8.0, 12);
    m_governor->addDropCounts(51, 50);
    QCOMPARE(m_governor->ceiling(), 4);
    // ...and after it the governor judges as usual.
    m_now = 11500;
    m_governor->addTimingSummary(10.0, 8.0, 12);
    m_governor->addDropCounts(52, 50);
    QCOMPARE(m_governor->ceiling(), 3);
}

void UpscaleGovernorTest::dropsWhileNotRenderingAreIgnored()
{
    // A minimised window: Qt stops rendering, so MpvVideoItem posts no
    // summaries, and libmpv counts every frame it could not hand over as
    // dropped, about five a second. That is not the GPU.
    m_now = 2500;
    m_governor->addTimingSummary(10.0, 8.0, 12);
    m_governor->addDropCounts(0, 0);
    qint64 drops = 0;
    for (m_now = 4100; m_now <= 30'000; m_now += 200)
        m_governor->addDropCounts(++drops, 0);
    QCOMPARE(m_governor->ceiling(), 4);
    QCOMPARE(m_settings->upscaleCeiling(QStringLiteral("Test GPU")), -1);
}

void UpscaleGovernorTest::resumedRenderingStartsAGrace()
{
    m_now = 2500;
    m_governor->addTimingSummary(10.0, 8.0, 12);
    m_governor->addDropCounts(0, 0);
    m_now = 10'000; // minimised since 2500: drops climb, the baseline follows
    m_governor->addDropCounts(40, 0);
    QCOMPARE(m_governor->ceiling(), 4);

    // Restored: summaries resume. The first frames back are a transient.
    m_now = 20'000;
    m_governor->addTimingSummary(10.0, 8.0, 12);
    m_now = 20'500;
    m_governor->addTimingSummary(10.0, 8.0, 12);
    m_governor->addDropCounts(41, 0);
    QCOMPARE(m_governor->ceiling(), 4);

    // After the grace, with frames rendering (no timer queries counts too),
    // a drop is the GPU's again.
    for (m_now = 21'000; m_now <= 22'500; m_now += 500)
        m_governor->addTimingSummary(-1.0, -1.0, -1);
    m_governor->addDropCounts(42, 0);
    QCOMPARE(m_governor->ceiling(), 3);
}

QTEST_GUILESS_MAIN(UpscaleGovernorTest)
#include "tst_upscale_governor.moc"
