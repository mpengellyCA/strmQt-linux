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

    int next = 0;
    if (m_tvs.size() >= 2) {
        double slowest = 0;
        for (const Tv &each : std::as_const(m_tvs))
            slowest = std::max(slowest, each.latencyMs);
        const int needed = int(std::ceil(slowest));
        next = m_delayMs;
        if (m_delayMs == 0 || needed + kCloseMs > m_delayMs) {
            next = needed + kMarginMs;
            m_highSinceMs = -1;
        } else if (m_delayMs > needed + kMarginMs + kStepDownMs) {
            if (m_highSinceMs < 0)
                m_highSinceMs = nowMs;
            if (nowMs - m_highSinceMs >= kStepDownAfterMs) {
                next = needed + kMarginMs;
                m_highSinceMs = -1;
            }
        } else {
            m_highSinceMs = -1;
        }
    }
    if (next != m_delayMs) {
        qCInfo(logApp) << "TVs playing together:" << m_tvs.size() << "delay" << next << "ms";
        m_delayMs = next;
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
