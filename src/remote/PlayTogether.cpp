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

    int wanted = 0;
    if (m_tvs.size() >= 2) {
        double slowest = 0;
        for (const Tv &each : std::as_const(m_tvs))
            slowest = std::max(slowest, each.latencyMs);
        wanted = int(std::ceil(slowest)) + kMarginMs;
    }
    if (wanted > m_delayMs || wanted <= m_delayMs - kStepDownMs || wanted == 0) {
        if (wanted != m_delayMs)
            qCInfo(logApp) << "TVs playing together:" << m_tvs.size() << "delay" << wanted << "ms";
        m_delayMs = wanted;
    }
    return QJsonObject{
        {QStringLiteral("delayMs"), m_delayMs},
        {QStringLiteral("tvs"), int(m_tvs.size())},
    };
}

int PlayTogether::tvs(qint64 nowMs) const
{
    return int(std::count_if(m_tvs.cbegin(), m_tvs.cend(),
                             [nowMs](const Tv &tv) { return nowMs - tv.seenMs < kForgetMs; }));
}

void PlayTogether::forget(qint64 nowMs)
{
    for (auto it = m_tvs.begin(); it != m_tvs.end();) {
        if (nowMs - it->seenMs >= kForgetMs)
            it = m_tvs.erase(it);
        else
            ++it;
    }
}

} // namespace strmqt
