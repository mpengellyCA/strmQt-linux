// How far behind the host StrmLights play together (PlayTogether): the slowest
// one sets it, it steps down only by enough, and a TV that stops is forgotten.

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
    void oneTvPlaysAsSoonAsItCan();
    void theSlowestTvSetsTheDelay();
    void theDelayComesDownOnlyByEnough();
    void aTvThatStopsIsForgotten();
    void aReportThatIsNotBelievedIsIgnored();
    void theMenusAreNotHeldBack();
};

void PlayTogetherTest::oneTvPlaysAsSoonAsItCan()
{
    PlayTogether together;
    together.setPlaying(true);
    const QJsonObject answer = together.report(QStringLiteral("a"), latency(42), 0);
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

void PlayTogetherTest::theDelayComesDownOnlyByEnough()
{
    PlayTogether together;
    together.setPlaying(true);
    together.report(QStringLiteral("a"), latency(40), 0);
    QCOMPARE(delayOf(together.report(QStringLiteral("b"), latency(70), 0)), 85);
    // Somewhat lower: not worth a change everyone hears.
    QCOMPARE(delayOf(together.report(QStringLiteral("b"), latency(55), 1000)), 85);
    // Well lower, but only for a moment.
    QCOMPARE(delayOf(together.report(QStringLiteral("b"), latency(45), 2000)), 85);
    QCOMPARE(delayOf(together.report(QStringLiteral("b"), latency(70), 3000)), 85);
    // Well lower for long enough.
    QCOMPARE(delayOf(together.report(QStringLiteral("b"), latency(45), 4000)), 85);
    // Both keep reporting meanwhile.
    for (qint64 at = 5000; at < 4000 + PlayTogether::kStepDownAfterMs; at += 1000) {
        together.report(QStringLiteral("a"), latency(40), at);
        QCOMPARE(delayOf(together.report(QStringLiteral("b"), latency(45), at)), 85);
    }
    together.report(QStringLiteral("a"), latency(40), 4000 + PlayTogether::kStepDownAfterMs);
    QCOMPARE(delayOf(together.report(QStringLiteral("b"), latency(45), 4000 + PlayTogether::kStepDownAfterMs)), 60);
}

void PlayTogetherTest::aTvThatStopsIsForgotten()
{
    PlayTogether together;
    together.setPlaying(true);
    together.report(QStringLiteral("a"), latency(40), 0);
    together.report(QStringLiteral("b"), latency(70), 0);
    QCOMPARE(together.tvs(PlayTogether::kForgetMs - 1), 2);
    QCOMPARE(together.tvs(PlayTogether::kForgetMs), 0);
    // Only a keeps reporting: alone again, it plays as soon as it can.
    const QJsonObject answer = together.report(QStringLiteral("a"), latency(40), PlayTogether::kForgetMs + 100);
    QCOMPARE(delayOf(answer), 0);
    QCOMPARE(answer.value(QStringLiteral("tvs")).toInt(), 1);
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
