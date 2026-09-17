#include <QPromise>
#include <QtTest>

#include <memory>

#include "app/music/Fanout.h"
#include "app/music/TtlCache.h"

using namespace strmqt;
using namespace strmqt::music;
using namespace std::chrono_literals;

// A QObject subclass that flags its own destruction from its own destructor
// body (before ~QObject runs), so a test can tell whether a Fanout callback
// fired synchronously from inside the context's teardown.
class DyingContext : public QObject
{
public:
    explicit DyingContext(bool *destroyedFlag) : m_destroyedFlag(destroyedFlag) {}
    ~DyingContext() override { *m_destroyedFlag = true; }

private:
    bool *m_destroyedFlag;
};

class MusicCacheTest : public QObject
{
    Q_OBJECT

private slots:
    void cacheExpiresAndGoesStale();
    void cacheRemovesByPredicate();
    void fanoutWaitsForAllAndSeal();
    void fanoutEmptyFiresOnSeal();
    void fanoutReportsCancellation();
    void fanoutDropsWhenContextDies();
};

void MusicCacheTest::cacheExpiresAndGoesStale()
{
    const QDateTime t0 = QDateTime::fromSecsSinceEpoch(1'000'000);
    TtlCache<int> cache(5min);
    cache.put(QStringLiteral("home:recent"), 1, t0);
    cache.put(QStringLiteral("sleeve:a"), 2, t0);
    QCOMPARE(cache.get(QStringLiteral("home:recent"), t0.addSecs(299)), std::optional(1));
    QVERIFY(!cache.get(QStringLiteral("home:recent"), t0.addSecs(301)).has_value());

    cache.markStale(QStringLiteral("home:"));
    QVERIFY(!cache.get(QStringLiteral("home:recent"), t0).has_value());
    QCOMPARE(cache.get(QStringLiteral("sleeve:a"), t0), std::optional(2));
    cache.put(QStringLiteral("home:recent"), 3, t0); // a fresh put clears staleness
    QCOMPARE(cache.get(QStringLiteral("home:recent"), t0), std::optional(3));

    cache.markStale();
    QVERIFY(!cache.get(QStringLiteral("sleeve:a"), t0).has_value());

    TtlCache<int> session(-1ms);
    session.put(QStringLiteral("k"), 9, t0);
    QCOMPARE(session.get(QStringLiteral("k"), t0.addYears(1)), std::optional(9));
    session.clear();
    QCOMPARE(session.size(), 0);
}

void MusicCacheTest::cacheRemovesByPredicate()
{
    const QDateTime t0 = QDateTime::currentDateTimeUtc();
    TtlCache<QStringList> cache(10min);
    cache.put(QStringLiteral("a"), {QStringLiteral("t1"), QStringLiteral("t2")}, t0);
    cache.put(QStringLiteral("b"), {QStringLiteral("t3")}, t0);
    cache.removeIf([](const QString &, const QStringList &ids) {
        return ids.contains(QStringLiteral("t2"));
    });
    QCOMPARE(cache.size(), 1);
    QVERIFY(cache.get(QStringLiteral("b"), t0).has_value());
    cache.remove(QStringLiteral("b"));
    QCOMPARE(cache.size(), 0);
}

void MusicCacheTest::fanoutWaitsForAllAndSeal()
{
    QObject context;
    QPromise<Result<int>> first;
    QPromise<Result<QString>> second;
    first.start();
    second.start();
    int total = 0;
    QString text;
    int doneCount = 0;

    auto fan = Fanout::create(&context, [&] { ++doneCount; });
    fan->add<int>(first.future(), [&](Result<int> r) { total = r.value; });
    fan->add<QString>(second.future(), [&](Result<QString> r) { text = r.error; });
    fan->seal();

    first.addResult(Result<int>::success(4));
    first.finish();
    QTest::qWait(20);
    QCOMPARE(doneCount, 0);
    second.addResult(Result<QString>::failure(QStringLiteral("boom")));
    second.finish();
    QTRY_COMPARE(doneCount, 1);
    QCOMPARE(total, 4);
    QCOMPARE(text, QStringLiteral("boom"));
    QTest::qWait(20);
    QCOMPARE(doneCount, 1);
}

void MusicCacheTest::fanoutEmptyFiresOnSeal()
{
    QObject context;
    int doneCount = 0;
    auto fan = Fanout::create(&context, [&] { ++doneCount; });
    QCOMPARE(doneCount, 0);
    fan->seal();
    QCOMPARE(doneCount, 0); // always queued, never re-entrant
    QTRY_COMPARE(doneCount, 1);
}

void MusicCacheTest::fanoutReportsCancellation()
{
    QObject context;
    QString error;
    bool done = false;
    auto fan = Fanout::create(&context, [&] { done = true; });
    {
        QPromise<Result<int>> promise;
        promise.start();
        fan->add<int>(promise.future(), [&](Result<int> r) { error = r.error; });
        promise.future().cancel();
        promise.finish();
    }
    fan->seal();
    QTRY_VERIFY(done);
    QCOMPARE(error, QStringLiteral("cancelled"));
}

void MusicCacheTest::fanoutDropsWhenContextDies()
{
    bool done = false;
    int sinkCalls = 0;
    QPromise<Result<int>> promise;
    promise.start();
    {
        QObject context;
        auto fan = Fanout::create(&context, [&] { done = true; });
        fan->add<int>(promise.future(), [&](Result<int>) { ++sinkCalls; });
        fan->seal();
    }
    promise.addResult(Result<int>::success(1));
    promise.finish();
    QTest::qWait(50);
    QVERIFY(!done);
    QCOMPARE(sinkCalls, 0); // the sink itself must never run once the context is gone

    // Stronger probe: a context whose own destructor flags itself as gone.
    // Qt cancels a future's chain when its context is destroyed (QFuture::then
    // docs), and does so from inside the context's own teardown — so a sink
    // guarded only at add() time, and not inside the delivered callback, can
    // still fire on a half-destroyed object. Fanout's contract ("if context is
    // destroyed first, nothing runs") must hold even then.
    bool contextDestroyed = false;
    int sinkCallsAfterDestruction = 0;
    {
        QPromise<Result<int>> promise2;
        promise2.start();
        auto context = std::make_unique<DyingContext>(&contextDestroyed);
        auto fan2 = Fanout::create(context.get(), [] {});
        fan2->add<int>(promise2.future(), [&](Result<int>) {
            if (contextDestroyed)
                ++sinkCallsAfterDestruction;
        });
        fan2->seal();
        context.reset(); // must not synchronously invoke the sink
        promise2.finish();
    }
    QCOMPARE(sinkCallsAfterDestruction, 0);
}

QTEST_GUILESS_MAIN(MusicCacheTest)
#include "tst_music_cache.moc"
