#pragma once

#include <QObject>
#include <QTimer>

class QProcess;

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
    ~HdrSupport() override;

    // Runs the probe asynchronously; emits probed() when done.
    void probe();

    bool probeCompleted() const { return m_probed; }
    bool hdrDisplayActive() const { return m_hdrActive; }

signals:
    void probed();

private:
    void finishProbe(QProcess *process, int exitCode, bool normalExit);

    bool m_probed = false;
    bool m_hdrActive = false;
    bool m_probeTimedOut = false;
    QProcess *m_process = nullptr;
    QTimer m_timeout;
};

} // namespace strmqt
