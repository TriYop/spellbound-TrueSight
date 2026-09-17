#include "AdviceAdapter.h"
#include "BandConfig.h"
#include <algorithm>

audioplugins::common::analysis::AnalysisSnapshot buildAnalysisSnapshot (
    const AnalysisResult::Snapshot& snap, float warmupSec)
{
    using audioplugins::common::analysis::AnalysisSnapshot;
    AnalysisSnapshot out;

    const bool pctReady = snap.secondsSinceReset >= warmupSec;

    for (int i = 0; i < BandConfig::numBands; ++i)
    {
        const float avgDb = (snap.avgRmsDbL[i]  + snap.avgRmsDbR[i])  * 0.5f;
        const float maxDb = (snap.peakRmsDbL[i] + snap.peakRmsDbR[i]) * 0.5f;

        auto& band = out.bands[static_cast<size_t> (i)];
        band.avgRmsDb  = avgDb;
        band.peakRmsDb = maxDb;
        // During warm-up, feed avg/peak into the p50/p95 slots so Common's
        // fixed (p50+p95)/2 formula reproduces this UI's pre-migration
        // warm-up blend exactly -- see this task's header note.
        band.p50RmsDb    = pctReady ? snap.p50RmsDb[i] : avgDb;
        band.p95RmsDb    = pctReady ? snap.p95RmsDb[i] : maxDb;
        band.p10RmsDb    = snap.p10RmsDb[i];
        band.correlation = snap.correlation[i];
        band.crestDb     = (snap.transientDbL[i] + snap.transientDbR[i]) * 0.5f;
    }

    out.overallAvgDb  = (snap.overallRmsDbL  + snap.overallRmsDbR)  * 0.5f;
    out.overallPeakDb = (snap.peakOverallDbL + snap.peakOverallDbR) * 0.5f;
    out.overallCorr   = snap.overallCorrelation;
    // Neutral point (12 LU) zeroes deriveAdvice's LRA offset term when not
    // ready, matching the old `lraReady` gate exactly -- see header note.
    out.lraLu = snap.lraLu > 0.f ? snap.lraLu : 12.f;

    return out;
}

std::vector<audioplugins::common::analysis::ResonancePeak> buildResonancePeaks (
    const AnalysisResult::Snapshot& snap)
{
    using audioplugins::common::analysis::ResonancePeak;
    std::vector<ResonancePeak> out;

    // Same clamp the old (removed) generateMarkdown() used before iterating
    // snap.resonance* -- resonanceCount is written by the background
    // ResonanceDetector thread and isn't otherwise bounds-checked.
    const int count = std::clamp (snap.resonanceCount, 0, AnalysisResult::maxResonances);
    out.reserve (static_cast<size_t> (count));

    for (int i = 0; i < count; ++i)
    {
        const auto bi = static_cast<size_t> (i);
        out.push_back ({ snap.resonanceFreqHz[bi], snap.resonanceQ[bi], snap.resonanceGainDb[bi],
                          /*enabled=*/true, /*prominenceDb=*/0.f });
    }

    return out;
}
