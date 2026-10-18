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
// D after the host sent it, on the host's clock (TimeService). A TV alone is
// held by its own latency too, so one that joins it need not move it.
//
// Every change of D is heard on every TV, as each skips or waits once to the
// new target, so while the host plays, D only goes up, and only when a TV
// comes within kCloseMs of it: a TV that cannot keep up is heard too. It comes
// down while nothing plays. A TV that has not reported for kForgetMs has
// stopped streaming, but its latency still counts for kRememberMs: when it
// streams again, D already holds it and the others hear nothing as it joins.
//
// Only while the host plays something (setPlaying): the menus have no sound
// to echo, and every TV waiting D there makes them feel slow. D is kept
// meanwhile, so playing starts held by it at once.
class PlayTogether
{
public:
    static constexpr qint64 kForgetMs = 5000;
    static constexpr qint64 kRememberMs = 30 * 60 * 1000;
    static constexpr int kMarginMs = 15;
    static constexpr int kCloseMs = 5;
    // Beyond this a report is not believed: no TV takes a second.
    static constexpr double kMaxLatencyMs = 1000;

    // A report from `device` at `nowMs`; returns what it is told:
    // {"delayMs": D, "tvs": how many are streaming}. A report without a
    // believable "latencyMs" still counts the TV, at the latency it last gave.
    QJsonObject report(const QString &device, const QJsonObject &report, qint64 nowMs);

    // Whether the host is playing something; D is 0 while it is not.
    void setPlaying(bool playing);

    int delayMs() const { return m_playing ? m_delayMs : 0; }
    int tvs(qint64 nowMs) const;

private:
    struct Tv {
        double latencyMs = 0;
        qint64 seenMs = 0;
    };
    void forget(qint64 nowMs);

    QHash<QString, Tv> m_tvs;
    int m_delayMs = 0;
    bool m_playing = false;
    int m_toldMs = 0;
};

} // namespace strmqt
