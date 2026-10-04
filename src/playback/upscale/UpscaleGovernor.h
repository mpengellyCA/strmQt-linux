#pragma once

#include <QObject>
#include <QString>

#include <functional>

namespace strmqt {

class Settings;

// Owns the upscaling ceiling: the highest ladder rung Auto may use on this GPU
// (spec 2026-10-03 §4.2). It only ever lowers it while a video plays, and it
// remembers the result per GL_RENDERER, so a weak GPU steps down once and
// later films start smooth.
class UpscaleGovernor : public QObject
{
    Q_OBJECT

public:
    using Clock = std::function<qint64()>; // monotonic milliseconds

    explicit UpscaleGovernor(Settings *settings, QObject *parent = nullptr);

    void setClockForTests(Clock clock) { m_clock = std::move(clock); }

    // Loads this GPU's saved ceiling (4 for an unseen one).
    void setRenderer(const QString &glRenderer);
    QString renderer() const { return m_renderer; }
    int ceiling() const { return m_ceiling; }

    // A new item: back to the saved ceiling, a fresh drop baseline, a grace period.
    void beginVideo();
    void setFrameRate(qreal fps);
    // A load, seek or chain change. Drops and timings in the next 2 s are the
    // transient of that, not the GPU's steady state.
    void noteTransient();
    // The rung the policy is running now; 0 = off. Steps go from here.
    void setActiveRung(int rung) { m_activeRung = rung; }
    // The user picked the chain: record timings, change nothing.
    void setManualOverride(bool manual) { m_manual = manual; }
    // Playback off 1× (a fast-forward hold, a user speed): drops and timings
    // then are the speed's, not the GPU's, so nothing steps. Leaving the hold
    // is a transient of its own.
    void setHeld(bool held);

    // From MpvVideoItem, twice a second while it renders. samples < 0 means
    // the GPU has no timer queries; 0 means no frames were rendered (paused).
    // Any summary with frames is also the sign that the picture is being
    // rendered; the first after a gap starts a grace period.
    void addTimingSummary(qreal p95Ms, qreal meanMs, int samples);
    // mpv's cumulative frame-drop-count and vo-delayed-frame-count. Ignored
    // (the baseline still follows) while no summary has come for a while: a
    // minimised window stops rendering, and libmpv counts each frame it could
    // not hand over as dropped. Before a video's first summary, drops are
    // judged as always, under the load's grace period.
    void addDropCounts(qint64 dropped, qint64 delayed);

    // Settings → Playback → "Re-test upscaling on this GPU".
    void resetCeiling();

    qreal budgetMs() const;
    qreal lastP95Ms() const { return m_lastP95Ms; }
    bool timingAvailable() const { return m_timingAvailable; }

signals:
    void ceilingChanged();
    void timingChanged();

private:
    qint64 now() const;
    int savedCeiling() const;
    bool mayStep() const;
    bool rendering() const;
    void stepDown(const char *why);

    Settings *m_settings;
    Clock m_clock;
    QString m_renderer;
    int m_ceiling = 4;
    int m_activeRung = 0;
    bool m_manual = false;
    bool m_held = false;
    qreal m_fps = 0.0;
    qint64 m_graceUntil = 0;
    qint64 m_overBudgetSince = -1;
    qint64 m_lastDropped = -1;
    qint64 m_lastDelayed = -1;
    qint64 m_lastSummaryAt = -1; // -1: none yet this video
    qreal m_lastP95Ms = -1.0;
    bool m_timingAvailable = true;
};

} // namespace strmqt
