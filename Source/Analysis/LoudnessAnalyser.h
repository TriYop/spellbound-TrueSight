#pragma once
#include <array>
#include <cstdint>
#include "QuantileHistogram.h"

// EBU R128 / ITU-R BS.1770-4 Loudness Range (LRA), computed causally/streaming
// for a real-time audio thread — ported from Codex/MasterTweak's offline
// LufsAnalyser (core/src/dsp/lufs_analyser.cpp), reformulated so it never
// needs the whole file in memory:
//
//  - K-weighting (high-shelf @1682Hz + high-pass @38Hz) is already causal in
//    the source implementation; filter state simply persists across blocks.
//  - The offline algorithm's relative gate depends on the mean of ALL
//    gate-1-passing 3s blocks in the whole file, computed before gating
//    starts. Here that becomes a running mean updated block-by-block — an
//    adaptive/causal gate, the same shape real-time loudness meters use.
//  - P10/P95 of the gated 3s-block LUFS values (Codex: vector + full sort)
//    becomes a QuantileHistogram (allocation-free, O(1) add).
//
// Only Codex's 3s-block/-20LU LRA path is ported; the separate 400ms/-10LU
// integrated-LUFS path is unused by the advice derivation and out of scope.
class LoudnessAnalyser
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    // L/R must be K-weighted sample-by-sample here (raw broadband input, same
    // signal AnalyserEngine already measures for overall RMS/correlation).
    void processBlock (const float* L, const float* R, int numSamples) noexcept;

    // EBU R128 Loudness Range (LU), integrated since last reset(). 0 = not enough data yet.
    float getLraLu() const noexcept { return lraLu_; }

private:
    struct BiquadCoeffs { float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f; };
    struct BiquadState  { float s0 = 0.f, s1 = 0.f; void reset() noexcept { s0 = s1 = 0.f; } };

    static float biquadProcess (const BiquadCoeffs& c, BiquadState& st, float x) noexcept;
    static BiquadCoeffs kWeightingStage1 (double sampleRate) noexcept;
    static BiquadCoeffs kWeightingStage2 (double sampleRate) noexcept;

    BiquadCoeffs stage1_ {};
    BiquadCoeffs stage2_ {};
    BiquadState  stage1StateL_, stage1StateR_, stage2StateL_, stage2StateR_;

    // 3-second LRA block reconstructed from a ring of 100ms hop-slots (30 slots,
    // aligned so sliding the ring by one slot == sliding the reference algorithm's
    // sliding window by one hop — matches Codex's 3s-block/100ms-hop 96.7% overlap).
    static constexpr int kNumSlots = 30;
    std::array<double, kNumSlots> slotSumSqL_ {};
    std::array<double, kNumSlots> slotSumSqR_ {};
    int slotWriteIdx_ { 0 };
    int filledSlots_  { 0 };

    int    hopSizeSamples_    { 0 };
    int    curHopSampleCount_ { 0 };
    double curHopSumSqL_      { 0.0 };
    double curHopSumSqR_      { 0.0 };

    // Causal relative-gate accumulator: running mean of gate-1 (absolute-gate-passing)
    // 3s-block powers, used as the -20LU relative gate threshold (Codex's offline
    // version computes this mean over the whole file before gating).
    double   gate1PowerSum_ { 0.0 };
    uint64_t gate1Count_    { 0 };

    QuantileHistogram lraHistogram_;
    float lraLu_ { 0.f };
};
