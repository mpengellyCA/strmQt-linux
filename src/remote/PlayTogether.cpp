#include "PlayTogether.h"

#include "core/Log.h"

#include <QJsonValue>

#include <algorithm>
#include <cmath>

namespace strmqt {

QJsonObject PlayTogether::report(const QString &device, const QJsonObject &report, qint64 nowMs)
{
    forget(nowMs);
    Tv &tv = m_tvs[device];
    tv.seenMs = nowMs;
    const QJsonValue latency = report.value(QStringLiteral("latencyMs"));
    if (latency.isDouble() && std::isfinite(latency.toDouble()) && latency.toDouble() >= 0
        && latency.toDouble() <= kMaxLatencyMs)
        tv.latencyMs = latency.toDouble();

    double slowest = 0;
    for (const Tv &each : std::as_const(m_tvs))
        slowest = std::max(slowest, each.latencyMs);
    const int needed = int(std::ceil(slowest));
    if (needed == 0)
        m_delayMs = 0;
    else if (!m_playing || needed + kCloseMs > m_delayMs)
        m_delayMs = needed + kMarginMs;
    if (delayMs() != m_toldMs) {
        m_toldMs = delayMs();
        qCInfo(logApp) << "TVs playing together:" << tvs(nowMs) << "delay" << m_toldMs << "ms";
    }
    return QJsonObject{
        {QStringLiteral("delayMs"), delayMs()},
        {QStringLiteral("tvs"), tvs(nowMs)},
    };
}

void PlayTogether::setPlaying(bool playing)
{
    m_playing = playing;
}

int PlayTogether::tvs(qint64 nowMs) const
{
    return int(std::count_if(m_tvs.cbegin(), m_tvs.cend(),
                             [nowMs](const Tv &tv) { return nowMs - tv.seenMs < kForgetMs; }));
}

void PlayTogether::forget(qint64 nowMs)
{
    for (auto it = m_tvs.begin(); it != m_tvs.end();) {
        if (nowMs - it->seenMs >= kRememberMs)
            it = m_tvs.erase(it);
        else
            ++it;
    }
}

} // namespace strmqt
