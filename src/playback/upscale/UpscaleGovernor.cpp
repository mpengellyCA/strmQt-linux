#include "playback/upscale/UpscaleGovernor.h"

#include "core/Log.h"
#include "core/Settings.h"
#include "playback/upscale/UpscalePolicy.h"

#include <QElapsedTimer>

#include <algorithm>
#include <cmath>

namespace strmqt {

namespace {
// Half the frame interval: the rest is Qt's scene graph and the OSD (spec §4.2).
constexpr qreal kBudgetShare = 0.5;
constexpr qint64 kOverBudgetForMs = 3000;
constexpr qint64 kGraceMs = 2000;
// Summaries come twice a second while MpvVideoItem renders. Three missed in a
// row means the render thread is not presenting (a minimised or hidden window).
constexpr qint64 kNotRenderingAfterMs = 1500;
// A stream that reports no rate. 30 fps is stricter than a film's 24 and
// looser than a broadcast's 60, so a wrong guess errs towards smoothness.
constexpr qreal kFallbackFps = 30.0;

qint64 monotonicMs()
{
    static QElapsedTimer clock = [] {
        QElapsedTimer timer;
        timer.start();
        return timer;
    }();
    return clock.elapsed();
}
} // namespace

UpscaleGovernor::UpscaleGovernor(Settings *settings, QObject *parent)
    : QObject(parent), m_settings(settings)
{
}

qint64 UpscaleGovernor::now() const
{
    return m_clock ? m_clock() : monotonicMs();
}

int UpscaleGovernor::savedCeiling() const
{
    const int saved = m_settings ? m_settings->upscaleCeiling(m_renderer) : -1;
    return saved < 0 ? kTopUpscaleRung : saved;
}

void UpscaleGovernor::setRenderer(const QString &glRenderer)
{
    m_renderer = glRenderer;
    const int ceiling = savedCeiling();
    if (ceiling == m_ceiling)
        return;
    m_ceiling = ceiling;
    emit ceilingChanged();
}

void UpscaleGovernor::beginVideo()
{
    m_lastDropped = -1;
    m_lastDelayed = -1;
    m_lastSummaryAt = -1;
    m_overBudgetSince = -1;
    noteTransient();
    const int ceiling = savedCeiling();
    if (ceiling != m_ceiling) {
        m_ceiling = ceiling;
        emit ceilingChanged();
    }
}

void UpscaleGovernor::setFrameRate(qreal fps)
{
    m_fps = fps;
}

void UpscaleGovernor::noteTransient()
{
    m_graceUntil = now() + kGraceMs;
    m_overBudgetSince = -1;
}

qreal UpscaleGovernor::budgetMs() const
{
    const qreal fps = (std::isfinite(m_fps) && m_fps > 0.0) ? m_fps : kFallbackFps;
    const qreal scale = m_settings ? m_settings->upscaleBudgetScale() : 1.0;
    return kBudgetShare * (1000.0 / fps) * scale;
}

void UpscaleGovernor::setHeld(bool held)
{
    if (held == m_held)
        return;
    m_held = held;
    if (held)
        m_overBudgetSince = -1;
    else
        noteTransient();
}

bool UpscaleGovernor::mayStep() const
{
    return !m_manual && !m_held && m_activeRung > 0 && now() >= m_graceUntil;
}

bool UpscaleGovernor::rendering() const
{
    // Before the first summary this is the normal start of a video, which the
    // load's grace period already covers.
    return m_lastSummaryAt < 0 || now() - m_lastSummaryAt <= kNotRenderingAfterMs;
}

void UpscaleGovernor::addTimingSummary(qreal p95Ms, qreal meanMs, int samples)
{
    Q_UNUSED(meanMs);
    if (samples != 0) {
        // Frames are being rendered. After a gap (a window restored), the
        // first of them are a transient, like a seek's.
        const bool resumed = !rendering();
        m_lastSummaryAt = now();
        if (resumed)
            noteTransient();
    }
    if (samples < 0) {
        m_timingAvailable = false; // dropped frames alone decide from now on
        emit timingChanged();
        return;
    }
    if (samples == 0)
        return;
    m_timingAvailable = true;
    m_lastP95Ms = p95Ms;
    emit timingChanged();

    if (!mayStep() || p95Ms <= budgetMs()) {
        m_overBudgetSince = -1;
        return;
    }
    const qint64 t = now();
    if (m_overBudgetSince < 0) {
        m_overBudgetSince = t;
        return;
    }
    if (t - m_overBudgetSince >= kOverBudgetForMs)
        stepDown("over budget");
}

void UpscaleGovernor::addDropCounts(qint64 dropped, qint64 delayed)
{
    // A stream reload restarts mpv's counters at 0: that is a new baseline.
    const bool restarted = dropped < m_lastDropped || delayed < m_lastDelayed;
    const bool rose = !restarted && m_lastDropped >= 0
                      && (dropped > m_lastDropped || delayed > m_lastDelayed);
    m_lastDropped = dropped;
    m_lastDelayed = delayed;
    if (rose && rendering() && mayStep())
        stepDown("frames dropped");
}

void UpscaleGovernor::stepDown(const char *why)
{
    const int next = std::max(0, std::min(m_ceiling, m_activeRung) - 1);
    m_overBudgetSince = -1;
    if (next == m_ceiling)
        return;
    m_ceiling = next;
    if (m_settings)
        m_settings->setUpscaleCeiling(m_renderer, m_ceiling);
    qCInfo(logPlayback) << "upscaling stepped down to rung" << m_ceiling << "(" << why
                        << ", budget" << budgetMs() << "ms ) on" << m_renderer;
    // The new chain's own first frames are a transient too.
    noteTransient();
    emit ceilingChanged();
}

void UpscaleGovernor::resetCeiling()
{
    if (m_settings) {
        // No video has rendered yet, so the GPU is unknown: forget every GPU.
        if (m_renderer.isEmpty())
            m_settings->clearAllUpscaleCeilings();
        else
            m_settings->clearUpscaleCeiling(m_renderer);
    }
    m_overBudgetSince = -1;
    if (m_ceiling == kTopUpscaleRung)
        return;
    m_ceiling = kTopUpscaleRung;
    emit ceilingChanged();
}

} // namespace strmqt
