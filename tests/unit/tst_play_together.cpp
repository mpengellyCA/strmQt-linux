// How far behind the host StrmLights play together (PlayTogether): the slowest
// one sets it, it only goes up while the host plays, and a TV that stops is
// remembered for a while.

#include "remote/PlayTogether.h"

#include <QJsonObject>
#include <QTest>

using strmqt::PlayTogether;

namespace {

QJsonObject latency(double ms)
{
    return QJsonObject{{QStringLiteral("latencyMs"), ms}};
}

int delayOf(const QJsonObject &answer)
{
    return answer.value(QStringLiteral("delayMs")).toInt(-1);
}

} // namespace

class PlayTogetherTest : public QObject
{
    Q_OBJECT
private slots:
    void oneTvIsHeldByItsOwnLatency();
    void aTvNotMeasuredYetPlaysAsSoonAsItCan();
    void theSlowestTvSetsTheDelay();
    void theDelayComesDownOnlyWhileNothingPlays();
    void aTvThatStopsIsRememberedForAWhile();
    void aReportThatIsNotBelievedIsIgnored();
    void theMenusAreNotHeldBack();
};

void PlayTogetherTest::oneTvIsHeldByItsOwnLatency()
{
    PlayTogether together;
    together.setPlaying(true);
    const QJsonObject answer = together.report(QStringLiteral("a"), latency(42), 0);
    QCOMPARE(delayOf(answer), 42 + PlayTogether::kMarginMs);
    QCOMPARE(answer.value(QStringLiteral("tvs")).toInt(), 1);
    // A faster TV joins without moving it.
    QCOMPARE(delayOf(together.report(QStringLiteral("b"), latency(30), 100)), 42 + PlayTogether::kMarginMs);
}

void PlayTogetherTest::aTvNotMeasuredYetPlaysAsSoonAsItCan()
{
    PlayTogether together;
    together.setPlaying(true);
    const QJsonObject answer = together.report(QStringLiteral("a"), QJsonObject(), 0);
    QCOMPARE(delayOf(answer), 0);
    QCOMPARE(answer.value(QStringLiteral("tvs")).toInt(), 1);
}

void PlayTogetherTest::theSlowestTvSetsTheDelay()
{
    PlayTogether together;
    together.setPlaying(true);
    together.report(QStringLiteral("a"), latency(42), 0);
    const QJsonObject answer = together.report(QStringLiteral("b"), latency(61.2), 100);
    QCOMPARE(delayOf(answer), 62 + PlayTogether::kMarginMs);
    QCOMPARE(answer.value(QStringLiteral("tvs")).toInt(), 2);
    // Both are told the same.
    QCOMPARE(delayOf(together.report(QStringLiteral("a"), latency(42), 200)), 62 + PlayTogether::kMarginMs);
    // A little slower is still well inside it.
    QCOMPARE(delayOf(together.report(QStringLiteral("b"), latency(70), 300)), 62 + PlayTogether::kMarginMs);
    // Close to it, it moves up at once.
    QCOMPARE(delayOf(together.report(QStringLiteral("a"), latency(80), 400)), 80 + PlayTogether::kMarginMs);
}

void PlayTogetherTest::theDelayComesDownOnlyWhileNothingPlays()
{
    PlayTogether together;
    together.setPlaying(true);
    together.report(QStringLiteral("a"), latency(40), 0);
    QCOMPARE(delayOf(together.report(QStringLiteral("b"), latency(70), 0)), 85);
    // Lower, for however long: not worth a change everyone hears.
    for (qint64 at = 1000; at < 60000; at += 1000) {
        together.report(QStringLiteral("a"), latency(40), at);
        QCOMPARE(delayOf(together.report(QStringLiteral("b"), latency(45), at)), 85);
    }
    // Between two things played it comes down.
    together.setPlaying(false);
    together.report(QStringLiteral("b"), latency(45), 60000);
    together.setPlaying(true);
    QCOMPARE(delayOf(together.report(QStringLiteral("a"), latency(40), 61000)), 60);
}

void PlayTogetherTest::aTvThatStopsIsRememberedForAWhile()
{
    PlayTogether together;
    together.setPlaying(true);
    together.report(QStringLiteral("a"), latency(40), 0);
    together.report(QStringLiteral("b"), latency(70), 0);
    QCOMPARE(together.tvs(PlayTogether::kForgetMs - 1), 2);
    QCOMPARE(together.tvs(PlayTogether::kForgetMs), 0);
    // Only a keeps reporting: b no longer streams, but still counts.
    QJsonObject answer = together.report(QStringLiteral("a"), latency(40), PlayTogether::kForgetMs + 100);
    QCOMPARE(delayOf(answer), 85);
    QCOMPARE(answer.value(QStringLiteral("tvs")).toInt(), 1);
    // b comes back, not measured yet: it is held already.
    together.report(QStringLiteral("a"), latency(40), 59000);
    answer = together.report(QStringLiteral("b"), QJsonObject(), 60000);
    QCOMPARE(delayOf(answer), 85);
    QCOMPARE(answer.value(QStringLiteral("tvs")).toInt(), 2);
    // Long gone, it no longer counts, once nothing plays.
    together.setPlaying(false);
    const qint64 later = 60000 + PlayTogether::kRememberMs;
    together.report(QStringLiteral("a"), latency(40), later);
    together.setPlaying(true);
    QCOMPARE(delayOf(together.report(QStringLiteral("a"), latency(40), later + 1000)), 55);
}

void PlayTogetherTest::aReportThatIsNotBelievedIsIgnored()
{
    PlayTogether together;
    together.setPlaying(true);
    together.report(QStringLiteral("a"), latency(40), 0);
    together.report(QStringLiteral("b"), latency(30), 0);
    for (const QJsonObject &report : {latency(-1), latency(5000), QJsonObject{{QStringLiteral("latencyMs"), QStringLiteral("90")}},
                                      QJsonObject()}) {
        // b counts still, at what it last gave.
        const QJsonObject answer = together.report(QStringLiteral("b"), report, 100);
        QCOMPARE(delayOf(answer), 40 + PlayTogether::kMarginMs);
        QCOMPARE(answer.value(QStringLiteral("tvs")).toInt(), 2);
    }
}

void PlayTogetherTest::theMenusAreNotHeldBack()
{
    PlayTogether together;
    together.report(QStringLiteral("a"), latency(40), 0);
    QCOMPARE(delayOf(together.report(QStringLiteral("b"), latency(70), 0)), 0);
    // Playing, held at once, by what was measured meanwhile.
    together.setPlaying(true);
    QCOMPARE(delayOf(together.report(QStringLiteral("a"), latency(40), 1000)), 85);
    together.setPlaying(false);
    QCOMPARE(delayOf(together.report(QStringLiteral("a"), latency(40), 2000)), 0);
}

QTEST_GUILESS_MAIN(PlayTogetherTest)
#include "tst_play_together.moc"
