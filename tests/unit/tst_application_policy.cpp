#include <QtTest>

#include "app/ApplicationPolicy.h"

using namespace strmqt;

class ApplicationPolicyTest : public QObject
{
    Q_OBJECT

private slots:
    void liveSuspensionComposesEveryCause();
    void displayInhibitRequiresReadyVideo();
};

void ApplicationPolicyTest::liveSuspensionComposesEveryCause()
{
    QVERIFY(shouldSuspendLiveUpdates(false, false, false));
    QVERIFY(shouldSuspendLiveUpdates(false, true, true));
    QVERIFY(shouldSuspendLiveUpdates(true, true, false));
    QVERIFY(!shouldSuspendLiveUpdates(true, true, true));
    QVERIFY(!shouldSuspendLiveUpdates(true, false, false));
}

void ApplicationPolicyTest::displayInhibitRequiresReadyVideo()
{
    QVERIFY(shouldInhibitDisplay(true, true, false, false));
    QVERIFY(!shouldInhibitDisplay(true, false, false, false)); // still loading
    QVERIFY(!shouldInhibitDisplay(true, true, true, false));  // ready but paused
    QVERIFY(!shouldInhibitDisplay(true, true, false, true));  // audio
    QVERIFY(!shouldInhibitDisplay(false, true, false, false));
}

QTEST_GUILESS_MAIN(ApplicationPolicyTest)
#include "tst_application_policy.moc"
