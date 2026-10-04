#pragma once

#include "playback/upscale/UpscaleCatalog.h"

#include <QSet>
#include <QSize>
#include <QString>

namespace strmqt {

inline constexpr int kTopUpscaleRung = 4;

// Everything the decision depends on (spec 2026-10-03 §4.2). `output` is the
// video item's size in device pixels; the aspect fitting happens here.
struct UpscaleInput
{
    QSize source;
    QSize output;
    ContentType content = ContentType::Unknown;
    int ceiling = kTopUpscaleRung; // from UpscaleGovernor
    bool hdr = false;
    bool softwareRenderer = false;
    QString override;              // "" = Auto, "off", or a catalog id
    QSet<QString> broken;          // catalog ids that failed to load this session
};

struct UpscaleDecision
{
    VideoProcessing chain;
    QString activeId = QStringLiteral("off");
    QString activeName = QStringLiteral("Off");
    QString reason;  // the status line under the picker
    int rung = 0;    // what the governor steps down from; a manual NIS counts as 2
};

// Spec §3 applied: software renderer → off; an override → that chain;
// nothing to upscale → off; else the content type's ladder from the ceiling
// down, doublers only from 1.5×, SDR-only entries skipped for HDR.
UpscaleDecision decideUpscale(const UpscaleCatalog &catalog, const UpscaleInput &input);

// The factor `source` is scaled by to fit inside `output` with its aspect kept.
// 0 when either size is empty.
double upscaleFactor(QSize source, QSize output);

// llvmpipe, softpipe, SWR: Mesa's CPU renderers, where any shader stutters.
bool isSoftwareGlRenderer(const QString &glRenderer);

} // namespace strmqt
