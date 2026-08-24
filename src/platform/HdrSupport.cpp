#include "HdrSupport.h"

#include "core/Log.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

namespace strmqt {

namespace {
constexpr int kProbeTimeoutMs = 2'000;
constexpr int kTerminateGraceMs = 250;
}

HdrSupport::HdrSupport(QObject *parent) : QObject(parent)
{
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(kProbeTimeoutMs);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (!m_process || m_process->state() == QProcess::NotRunning)
            return;
        m_probeTimedOut = true;
        qCInfo(logApp) << "HDR probe timed out — assuming SDR";
        QProcess *process = m_process;
        process->terminate();
        QTimer::singleShot(kTerminateGraceMs, process, [process] {
            if (process->state() != QProcess::NotRunning)
                process->kill();
        });
    });
}

HdrSupport::~HdrSupport()
{
    m_timeout.stop();
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(kTerminateGraceMs);
    }
}

void HdrSupport::probe()
{
    // One result is enough for the process lifetime. In particular, several
    // QML surfaces asking during construction must not fan out subprocesses.
    if (m_probed || m_process)
        return;
    // The page-construction self-test checks QML availability, not the host's
    // display stack. Skipping the external utility also guarantees there is no
    // child process left behind when that deliberately short-lived app exits.
    if (qEnvironmentVariableIsSet("STRMQT_SELFTEST")) {
        m_probed = true;
        emit probed();
        return;
    }

    auto *process = new QProcess(this);
    m_process = process;
    m_probeTimedOut = false;
    connect(process, &QProcess::finished, this,
            [this, process](int exitCode, QProcess::ExitStatus status) {
                finishProbe(process, exitCode, status == QProcess::NormalExit);
            });
    connect(process, &QProcess::errorOccurred, this,
            [this, process](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart)
                    finishProbe(process, -1, false);
            });
    m_timeout.start();
    process->start(QStringLiteral("kscreen-doctor"), {QStringLiteral("--json")});
}

void HdrSupport::finishProbe(QProcess *process, int exitCode, bool normalExit)
{
    if (process != m_process)
        return;
    m_timeout.stop();
    m_process = nullptr;
    m_probed = true;

    if (!m_probeTimedOut && normalExit && exitCode == 0) {
        const QJsonObject root =
            QJsonDocument::fromJson(process->readAllStandardOutput()).object();
        const QJsonArray outputs = root.value(QLatin1String("outputs")).toArray();
        for (const QJsonValue &value : outputs) {
            const QJsonObject output = value.toObject();
            if (output.value(QLatin1String("enabled")).toBool(false) &&
                output.value(QLatin1String("hdr")).toBool(false)) {
                m_hdrActive = true;
                break;
            }
        }
        qCInfo(logApp) << "HDR display active:" << m_hdrActive;
    } else if (!m_probeTimedOut) {
        qCInfo(logApp) << "HDR probe unavailable (kscreen-doctor rc" << exitCode
                       << ") — assuming SDR";
    }

    process->deleteLater();
    emit probed();
}

} // namespace strmqt
