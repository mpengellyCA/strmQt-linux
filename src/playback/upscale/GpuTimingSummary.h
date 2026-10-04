#pragma once

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace strmqt {

// What MpvVideoItem reports twice a second (spec 2026-10-03 §4.4). Lives here,
// QtGui-free, so the arithmetic is tested without a GL context.
struct GpuTimingSummary
{
    double p95Ms = -1.0;
    double meanMs = -1.0;
    int samples = 0;
};

// Nearest-rank 95th percentile and mean of per-frame GPU milliseconds.
inline GpuTimingSummary summarizeGpuTimes(std::vector<double> samplesMs)
{
    GpuTimingSummary summary;
    if (samplesMs.empty())
        return summary;
    std::sort(samplesMs.begin(), samplesMs.end());
    const auto count = samplesMs.size();
    const auto rank = static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(count)));
    summary.p95Ms = samplesMs[std::max<std::size_t>(rank, 1) - 1];
    summary.meanMs = std::accumulate(samplesMs.begin(), samplesMs.end(), 0.0) / count;
    summary.samples = static_cast<int>(count);
    return summary;
}

} // namespace strmqt
