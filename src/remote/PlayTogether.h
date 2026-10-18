#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QtGlobal>

namespace strmqt {

// PlayTogether — how far behind the host every StrmLight streaming from it
// plays, so their sound lines up (StrmLight's docs/synchronized-playback.md).
//
// While it streams, each StrmLight says about once a second how long its own
// path is: from the host sending a sample to that sample leaving its speaker,
// as it measures it (its "latency"). Every one of them is told the same delay
// D, which is the slowest one's latency plus kMarginMs, and plays each sample
// D after the host sent it, on the host's clock (TimeService). With one TV,
// D is 0: it plays as soon as it can, as before.
//
// D goes up at once, since a TV that cannot keep up is heard, and comes down
// only by kStepDownMs or more, so a latency that wavers does not move every
// TV's sound back and forth. A TV that has not reported for kForgetMs has
// stopped streaming and no longer counts.
class PlayTogether
{
public:
    static constexpr qint64 kForgetMs = 5000;
    static constexpr int kMarginMs = 10;
    static constexpr int kStepDownMs = 20;
    // Beyond this a report is not believed: no TV takes a second.
    static constexpr double kMaxLatencyMs = 1000;

    // A report from `device` at `nowMs`; returns what it is told:
    // {"delayMs": D, "tvs": how many are streaming}. A report without a
    // believable "latencyMs" still counts the TV, at the latency it last gave.
    QJsonObject report(const QString &device, const QJsonObject &report, qint64 nowMs);

    int delayMs() const { return m_delayMs; }
    int tvs(qint64 nowMs) const;

private:
    struct Tv {
        double latencyMs = 0;
        qint64 seenMs = 0;
    };
    void forget(qint64 nowMs);

    QHash<QString, Tv> m_tvs;
    int m_delayMs = 0;
};

} // namespace strmqt
