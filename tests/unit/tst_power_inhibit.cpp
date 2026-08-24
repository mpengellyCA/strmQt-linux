#include <QtTest>

#include "platform/PowerInhibit.h"

using namespace strmqt;

class FakePowerInhibit final : public PowerInhibit
{
public:
    struct AcquireCall
    {
        Backend backend;
        QString reason;
        quint64 generation;
    };

    struct ReleaseCall
    {
        Backend backend;
        quint32 cookie;
    };

    QList<AcquireCall> acquires;
    QList<ReleaseCall> releases;

    void reply(int index, bool success, quint32 cookie = 0,
               const QString &error = QStringLiteral("service unavailable"))
    {
        const AcquireCall call = acquires.at(index);
        completeAcquire(call.backend, call.generation, success, cookie, error);
    }

protected:
    void requestAcquire(Backend backend, const QString &reason, quint64 generation) override
    {
        acquires.push_back({backend, reason, generation});
    }

    void requestRelease(Backend backend, quint32 cookie) override
    {
        releases.push_back({backend, cookie});
    }
};

class PowerInhibitTest : public QObject
{
    Q_OBJECT

private slots:
    void screenSaverBackendAcquiresAndReleases();
    void powerManagementIsIndependentFallback();
    void noServiceDegradesGracefully();
    void releaseBeforeAcquireReplyReleasesLateCookie();
    void releaseBeforeFallbackReplyReleasesLateCookie();
};

void PowerInhibitTest::screenSaverBackendAcquiresAndReleases()
{
    FakePowerInhibit inhibit;
    inhibit.acquire(QStringLiteral("Watching a film"));

    QCOMPARE(inhibit.acquires.size(), 1);
    QCOMPARE(inhibit.acquires[0].backend, PowerInhibit::Backend::ScreenSaver);
    QCOMPARE(inhibit.acquires[0].reason, QStringLiteral("Watching a film"));
    QVERIFY(!inhibit.active());

    inhibit.reply(0, true, 41);
    QVERIFY(inhibit.active());

    inhibit.release();
    QVERIFY(!inhibit.active());
    QCOMPARE(inhibit.releases.size(), 1);
    QCOMPARE(inhibit.releases[0].backend, PowerInhibit::Backend::ScreenSaver);
    QCOMPARE(inhibit.releases[0].cookie, quint32(41));
}

void PowerInhibitTest::powerManagementIsIndependentFallback()
{
    FakePowerInhibit inhibit;
    inhibit.acquire(QStringLiteral("Playing video"));
    inhibit.reply(0, false);

    QCOMPARE(inhibit.acquires.size(), 2);
    QCOMPARE(inhibit.acquires[1].backend, PowerInhibit::Backend::PowerManagement);
    QCOMPARE(inhibit.acquires[1].reason, QStringLiteral("Playing video"));
    QVERIFY(!inhibit.active());

    inhibit.reply(1, true, 73);
    QVERIFY(inhibit.active());
    inhibit.release();

    QCOMPARE(inhibit.releases.size(), 1);
    QCOMPARE(inhibit.releases[0].backend, PowerInhibit::Backend::PowerManagement);
    QCOMPARE(inhibit.releases[0].cookie, quint32(73));
}

void PowerInhibitTest::noServiceDegradesGracefully()
{
    FakePowerInhibit inhibit;
    inhibit.acquire(QStringLiteral("Playing video"));
    inhibit.reply(0, false, 0, QStringLiteral("no screensaver"));
    inhibit.reply(1, false, 0, QStringLiteral("no power manager"));

    QCOMPARE(inhibit.acquires.size(), 2);
    QVERIFY(!inhibit.active());
    QVERIFY(inhibit.releases.isEmpty());

    // A later policy notification may retry if a desktop service appeared.
    inhibit.acquire(QStringLiteral("Playing video"));
    QCOMPARE(inhibit.acquires.size(), 3);
    QCOMPARE(inhibit.acquires[2].backend, PowerInhibit::Backend::ScreenSaver);
    inhibit.release();
}

void PowerInhibitTest::releaseBeforeAcquireReplyReleasesLateCookie()
{
    FakePowerInhibit inhibit;
    inhibit.acquire(QStringLiteral("Playing video"));
    QCOMPARE(inhibit.acquires.size(), 1);

    inhibit.release();
    QVERIFY(!inhibit.active());
    QVERIFY(inhibit.releases.isEmpty());

    // The stale successful reply must not reactivate inhibition. Its cookie is
    // returned immediately through the same backend that issued it.
    inhibit.reply(0, true, 99);
    QVERIFY(!inhibit.active());
    QCOMPARE(inhibit.releases.size(), 1);
    QCOMPARE(inhibit.releases[0].backend, PowerInhibit::Backend::ScreenSaver);
    QCOMPARE(inhibit.releases[0].cookie, quint32(99));
}

void PowerInhibitTest::releaseBeforeFallbackReplyReleasesLateCookie()
{
    FakePowerInhibit inhibit;
    inhibit.acquire(QStringLiteral("Playing video"));
    inhibit.reply(0, false); // ScreenSaver unavailable, PowerManagement pending
    QCOMPARE(inhibit.acquires.size(), 2);

    inhibit.release();
    inhibit.reply(1, true, 123);

    QVERIFY(!inhibit.active());
    QCOMPARE(inhibit.releases.size(), 1);
    QCOMPARE(inhibit.releases[0].backend, PowerInhibit::Backend::PowerManagement);
    QCOMPARE(inhibit.releases[0].cookie, quint32(123));
}

QTEST_GUILESS_MAIN(PowerInhibitTest)
#include "tst_power_inhibit.moc"
