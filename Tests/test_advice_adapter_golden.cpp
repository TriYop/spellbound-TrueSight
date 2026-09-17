// TrueSight/Tests/test_advice_adapter_golden.cpp
//
// Golden-vector values captured from a standalone program mirroring the
// exact pre-migration inline formulas in PluginEditor.cpp's drawAdvicePanel
// (see task-7-report.md for the capture command + raw output):
//   warmup eqGain=-1.000000
//   steady eqGain=-1.000000
//   limiterTarget notReady=-15.000000
//   limiterTarget ready=-14.100000
#include "../Source/Analysis/AdviceAdapter.h"
#include "audioplugins/common/analysis/AdviceSet.h"
#include "test_runner.h"
#include <cmath>

int main() {
    using namespace audioplugins::common::analysis;

    PresetData preset;
    preset.bandRmsDb.fill(-18.f);
    preset.bandMinCorr.fill(0.6f);
    preset.bandTransientDb.fill(8.f);
    preset.overallRmsDb = -18.f;
    preset.overallMinCorr = 0.6f;

    // Warm-up case, band 3: avgDb=-20, peakDb=-14, secondsSinceReset below threshold.
    {
        AnalysisResult::Snapshot snap{};
        snap.avgRmsDbL[3] = -20.f; snap.avgRmsDbR[3] = -20.f;
        snap.peakRmsDbL[3] = -14.f; snap.peakRmsDbR[3] = -14.f;
        snap.secondsSinceReset = 0.5f;   // below kPercentileWarmupSec
        snap.lraLu = 0.f;                // not ready

        auto commonSnap = buildAnalysisSnapshot(snap, /*warmupSec=*/8.f);
        auto advice = deriveAdvice(commonSnap, preset);
        CHECK_MSG(std::abs(advice.eq[3].gainDb - (-1.000000f)) < 1e-3f,
                  "warm-up eq gain should match pre-migration inline formula");
        CHECK_MSG(std::abs(advice.limiter.targetLufsApprox - (-15.000000f)) < 1e-3f,
                  "limiter target should be flat (offset 0) when LRA isn't ready");
    }

    // Steady-state case, band 3: p50Db=-19, p95Db=-15, lraLu=15 (ready).
    {
        AnalysisResult::Snapshot snap{};
        snap.p50RmsDb[3] = -19.f; snap.p95RmsDb[3] = -15.f;
        snap.secondsSinceReset = 100.f;  // above kPercentileWarmupSec
        snap.lraLu = 15.f;

        auto commonSnap = buildAnalysisSnapshot(snap, /*warmupSec=*/8.f);
        auto advice = deriveAdvice(commonSnap, preset);
        CHECK_MSG(std::abs(advice.eq[3].gainDb - (-1.000000f)) < 1e-3f,
                  "steady-state eq gain should match pre-migration inline formula");
        CHECK_MSG(std::abs(advice.limiter.targetLufsApprox - (-14.100000f)) < 1e-3f,
                  "limiter target should include the LRA offset once ready");
    }

    // buildResonancePeaks() carries TrueSight's own live-detected resonances
    // (deriveAdvice() itself always leaves AdviceSet::resonances empty) into
    // Common's ResonancePeak shape, so the exported markdown's "Resonance EQ"
    // table isn't always empty -- covers the gap flagged in code review.
    {
        AnalysisResult::Snapshot snap{};
        snap.resonanceCount = 2;
        snap.resonanceFreqHz[0] = 120.f;  snap.resonanceQ[0] = 4.5f;  snap.resonanceGainDb[0] = -3.f;
        snap.resonanceFreqHz[1] = 2500.f; snap.resonanceQ[1] = 6.0f;  snap.resonanceGainDb[1] = -1.5f;

        const auto peaks = buildResonancePeaks(snap);
        CHECK_MSG(peaks.size() == 2, "should carry over exactly resonanceCount peaks");
        CHECK_MSG(std::abs(peaks[0].freqHz - 120.f) < 1e-3f, "peak 0 freq mismatch");
        CHECK_MSG(std::abs(peaks[0].q - 4.5f) < 1e-3f, "peak 0 Q mismatch");
        CHECK_MSG(std::abs(peaks[0].gainDb - (-3.f)) < 1e-3f, "peak 0 gain mismatch");
        CHECK_MSG(peaks[0].enabled, "carried-over peaks should default to enabled");
        CHECK_MSG(std::abs(peaks[1].freqHz - 2500.f) < 1e-3f, "peak 1 freq mismatch");
        CHECK_MSG(std::abs(peaks[1].q - 6.0f) < 1e-3f, "peak 1 Q mismatch");
        CHECK_MSG(std::abs(peaks[1].gainDb - (-1.5f)) < 1e-3f, "peak 1 gain mismatch");
    }

    // resonanceCount is clamped to AnalysisResult::maxResonances, same bound
    // the old (removed) generateMarkdown() used.
    {
        AnalysisResult::Snapshot snap{};
        snap.resonanceCount = AnalysisResult::maxResonances + 5;
        const auto peaks = buildResonancePeaks(snap);
        CHECK_MSG(peaks.size() == static_cast<size_t>(AnalysisResult::maxResonances),
                  "resonanceCount should be clamped to maxResonances");
    }

    // No detections -> no peaks (the common "silence" case).
    {
        AnalysisResult::Snapshot snap{};
        snap.resonanceCount = 0;
        const auto peaks = buildResonancePeaks(snap);
        CHECK_MSG(peaks.empty(), "zero resonanceCount should produce no peaks");
    }

    TEST_SUMMARY();
}
