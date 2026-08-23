#pragma once

#include <QObject>

namespace strmqt {

// Probes whether any enabled display currently runs in HDR mode (Plasma 6
// Wayland color management, read via `kscreen-doctor --json`). Informational
// for v1: the embedded GL video path always tone-maps HDR content through
// libplacebo (PLAN §3.10 — passthrough needs an out-of-band presentation path
// and is deferred). Degrades silently when the tool is unavailable.
class HdrSupport : public QObject
{
    Q_OBJECT

public:
    explicit HdrSupport(QObject *parent = nullptr);

    // Runs the probe asynchronously; emits probed() when done.
    void probe();

    bool probeCompleted() const { return m_probed; }
    bool hdrDisplayActive() const { return m_hdrActive; }

signals:
    void probed();

private:
    bool m_probed = false;
    bool m_hdrActive = false;
};

} // namespace strmqt
