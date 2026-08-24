#include <QtTest>

#include "app/ApplicationPolicy.h"

using namespace strmqt;

class ApplicationPolicyTest : public QObject
{
    Q_OBJECT

private slots:
    void liveSuspensionComposesEveryCause();
    void displayInhibitIsVideoOnly();
};

void ApplicationPolicyTest::liveSuspensionComposesEveryCause()
{
    QVERIFY(shouldSuspendLiveUpdates(false, false, false));
    QVERIFY(shouldSuspendLiveUpdates(false, true, true));
    QVERIFY(shouldSuspendLiveUpdates(true, true, false));
    QVERIFY(!shouldSuspendLiveUpdates(true, true, true));
    QVERIFY(!shouldSuspendLiveUpdates(true, false, false));
}

void ApplicationPolicyTest::displayInhibitIsVideoOnly()
{
    QVERIFY(shouldInhibitDisplay(true, false, false));
    QVERIFY(!shouldInhibitDisplay(true, true, false));
    QVERIFY(!shouldInhibitDisplay(true, false, true));
    QVERIFY(!shouldInhibitDisplay(false, false, false));
}

QTEST_GUILESS_MAIN(ApplicationPolicyTest)
#include "tst_application_policy.moc"
