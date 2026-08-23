#include "HdrSupport.h"

#include "core/Log.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

namespace strmqt {

HdrSupport::HdrSupport(QObject *parent) : QObject(parent) {}

void HdrSupport::probe()
{
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this,
            [this, process](int exitCode, QProcess::ExitStatus status) {
                process->deleteLater();
                m_probed = true;
                if (status != QProcess::NormalExit || exitCode != 0) {
                    qCInfo(logApp) << "HDR probe unavailable (kscreen-doctor rc" << exitCode
                                   << ") — assuming SDR";
                    emit probed();
                    return;
                }

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
                emit probed();
            });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError) {
        process->deleteLater();
        if (!m_probed) {
            m_probed = true;
            qCInfo(logApp) << "HDR probe unavailable (kscreen-doctor missing) — assuming SDR";
            emit probed();
        }
    });
    process->start(QStringLiteral("kscreen-doctor"), {QStringLiteral("--json")});
}

} // namespace strmqt
