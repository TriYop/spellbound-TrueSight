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

    // Qualified explicitly: AdviceAdapter.h also pulls in TrueSight's own
    // (juce::String-based) ::PresetData, which would otherwise make the bare
    // name "PresetData" ambiguous here.
    audioplugins::common::analysis::PresetData preset;
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

    TEST_SUMMARY();
}
