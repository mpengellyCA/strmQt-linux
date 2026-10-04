#include "playback/upscale/UpscalePolicy.h"

#include <QCoreApplication>

#include <algorithm>

namespace strmqt {

namespace {

// Below this the picture already fills the window: 1080 lines in a 1090-line
// item is not worth a shader.
constexpr double kNoUpscaleAbove = 1.01;
// A 2× doubler at 1080p → 1200p costs GPU time for no visible gain (spec §3).
constexpr double kDoublerFrom = 1.5;
constexpr int kTopNonDoublerRung = 2;

QString tr(const char *text)
{
    return QCoreApplication::translate("strmqt::UpscalePolicy", text);
}

UpscaleDecision offDecision(const QString &reason)
{
    UpscaleDecision decision;
    decision.activeName = tr("Off");
    decision.reason = reason;
    return decision;
}

UpscaleDecision chosen(const UpscaleEntry &entry, ContentType content, QSize source,
                       double factor, const QStringList &notes)
{
    UpscaleDecision decision;
    decision.chain = entry.chain();
    decision.activeId = entry.id;
    decision.activeName = entry.name;
    decision.rung = entry.manualOnly() ? kTopNonDoublerRung : entry.rung;
    QStringList parts{entry.name};
    if (content == ContentType::Animation)
        parts << tr("animation");
    if (factor > 0.0)
        parts << QStringLiteral("%1p → %2p").arg(source.height()).arg(qRound(source.height() * factor));
    parts << notes;
    decision.reason = parts.join(QStringLiteral(" · "));
    return decision;
}

} // namespace

double upscaleFactor(QSize source, QSize output)
{
    if (source.isEmpty() || output.isEmpty())
        return 0.0;
    return std::min(double(output.width()) / source.width(),
                    double(output.height()) / source.height());
}

bool isSoftwareGlRenderer(const QString &glRenderer)
{
    return glRenderer.contains(QLatin1String("llvmpipe"), Qt::CaseInsensitive)
        || glRenderer.contains(QLatin1String("softpipe"), Qt::CaseInsensitive)
        || glRenderer.startsWith(QLatin1String("SWR"));
}

UpscaleDecision decideUpscale(const UpscaleCatalog &catalog, const UpscaleInput &in)
{
    if (in.softwareRenderer)
        return offDecision(tr("Needs a GPU"));
    if (in.override == QLatin1String("off"))
        return offDecision(tr("Off"));

    const double factor = upscaleFactor(in.source, in.output);
    // Unknown plays the live-action ladder, and says nothing about it.
    const ContentType content =
        in.content == ContentType::Animation ? ContentType::Animation : ContentType::LiveAction;
    QString failed;

    // The user asked for this chain: honoured whatever the scale or ceiling,
    // unless it cannot run at all.
    if (!in.override.isEmpty()) {
        if (const UpscaleEntry *entry = catalog.find(in.override)) {
            if (in.broken.contains(entry->id))
                failed = entry->name;
            else if (!(entry->sdrOnly && in.hdr))
                return chosen(*entry, in.content, in.source, factor, {tr("manual")});
        }
    }

    if (factor <= kNoUpscaleAbove) {
        if (in.source.isEmpty())
            return offDecision(tr("Off"));
        return offDecision(tr("Off · %1p fills the window").arg(in.source.height()));
    }

    const int natural = factor >= kDoublerFrom ? kTopUpscaleRung : kTopNonDoublerRung;
    const int top = std::clamp(std::min(in.ceiling, natural), 0, kTopUpscaleRung);
    QStringList notes;
    if (in.ceiling < natural)
        notes << tr("stepped down for smooth playback");

    for (int rung = top; rung >= 1; --rung) {
        const UpscaleEntry *entry = catalog.atRung(content, rung);
        if (!entry || (entry->sdrOnly && in.hdr))
            continue;
        if (in.broken.contains(entry->id)) {
            if (failed.isEmpty())
                failed = entry->name;
            continue;
        }
        UpscaleDecision decision = chosen(*entry, content, in.source, factor, notes);
        if (!failed.isEmpty())
            decision.reason = tr("%1 failed to load — using %2").arg(failed, decision.reason);
        return decision;
    }
    return offDecision(notes.isEmpty() ? tr("Off")
                                       : tr("Off · %1").arg(notes.join(QStringLiteral(" · "))));
}

} // namespace strmqt
