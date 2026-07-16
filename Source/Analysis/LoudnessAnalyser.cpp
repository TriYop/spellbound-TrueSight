#include "LoudnessAnalyser.h"
#include <cmath>
#include <algorithm>
#include <numbers>

namespace
{
    // 10^((-70 + 0.691) / 10) — absolute gate threshold (-70 LUFS) in mean-square power units.
    // Same constant as Codex/MasterTweak's LufsAnalyser.
    constexpr double kAbsGateZ = 1.1724e-7;
}

float LoudnessAnalyser::biquadProcess (const BiquadCoeffs& c, BiquadState& st, float x) noexcept
{
    // Direct Form 2 Transposed, normalised so a0 = 1 — matches Codex's mt::dsp::biquadProcess.
    const float y = c.b0 * x + st.s0;
    st.s0 = c.b1 * x - c.a1 * y + st.s1;
    st.s1 = c.b2 * x - c.a2 * y;
    return y;
}

// BS.1770 Stage 1: 2nd-order high-shelf, f0=1681.97 Hz, +4 dB gain, Q=0.7072.
// Coefficients ported verbatim from Codex's kWeightingStage1 (bilinear transform,
// K = tan(pi*f0/sr)) so results stay bit-compatible with the reference algorithm.
LoudnessAnalyser::BiquadCoeffs LoudnessAnalyser::kWeightingStage1 (double sr) noexcept
{
    const double f0 = 1681.974450955533;
    const double G  = 3.999843853;
    const double Q  = 0.7071752369554196;
    const double K  = std::tan (std::numbers::pi_v<double> * f0 / sr);
    const double K2 = K * K;
    const double Vh = std::pow (10.0, G / 20.0);
    const double Vb = std::pow (10.0, G / 40.0);
    const double n  = 1.0 / (1.0 + K / Q + K2);
    return {
        static_cast<float> ((Vh + Vb * K / Q + K2) * n),
        static_cast<float> (2.0 * (K2 - Vh) * n),
        static_cast<float> ((Vh - Vb * K / Q + K2) * n),
        static_cast<float> (2.0 * (K2 - 1.0) * n),
        static_cast<float> ((1.0 - K / Q + K2) * n)
    };
}

// BS.1770 Stage 2: 2nd-order high-pass, f0=38.135 Hz, Q=0.5003 (Butterworth).
LoudnessAnalyser::BiquadCoeffs LoudnessAnalyser::kWeightingStage2 (double sr) noexcept
{
    const double f0 = 38.13547087602444;
    const double Q  = 0.5003270373238773;
    const double K  = std::tan (std::numbers::pi_v<double> * f0 / sr);
    const double K2 = K * K;
    const double n  = 1.0 / (1.0 + K / Q + K2);
    return {
        static_cast<float> (1.0 * n),
        static_cast<float> (-2.0 * n),
        static_cast<float> (1.0 * n),
        static_cast<float> (2.0 * (K2 - 1.0) * n),
        static_cast<float> ((1.0 - K / Q + K2) * n)
    };
}

void LoudnessAnalyser::prepare (double sampleRate) noexcept
{
    stage1_ = kWeightingStage1 (sampleRate);
    stage2_ = kWeightingStage2 (sampleRate);
    hopSizeSamples_ = std::max (1, static_cast<int> (0.1 * sampleRate));   // 100 ms
    reset();
}

void LoudnessAnalyser::reset() noexcept
{
    stage1StateL_.reset(); stage1StateR_.reset();
    stage2StateL_.reset(); stage2StateR_.reset();

    slotSumSqL_.fill (0.0);
    slotSumSqR_.fill (0.0);
    slotWriteIdx_ = 0;
    filledSlots_  = 0;

    curHopSampleCount_ = 0;
    curHopSumSqL_ = curHopSumSqR_ = 0.0;

    gate1PowerSum_ = 0.0;
    gate1Count_    = 0;

    lraHistogram_.reset();
    lraLu_ = 0.f;
}

void LoudnessAnalyser::processBlock (const float* L, const float* R, int numSamples) noexcept
{
    if (hopSizeSamples_ <= 0)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        const float kL = biquadProcess (stage2_, stage2StateL_,
                                         biquadProcess (stage1_, stage1StateL_, L[i]));
        const float kR = biquadProcess (stage2_, stage2StateR_,
                                         biquadProcess (stage1_, stage1StateR_, R[i]));

        curHopSumSqL_ += static_cast<double> (kL) * kL;
        curHopSumSqR_ += static_cast<double> (kR) * kR;

        if (++curHopSampleCount_ < hopSizeSamples_)
            continue;

        // Completed one 100ms hop-slot — push into the 30-slot (3s) ring.
        slotSumSqL_[static_cast<size_t> (slotWriteIdx_)] = curHopSumSqL_;
        slotSumSqR_[static_cast<size_t> (slotWriteIdx_)] = curHopSumSqR_;
        slotWriteIdx_ = (slotWriteIdx_ + 1) % kNumSlots;
        filledSlots_  = std::min (filledSlots_ + 1, kNumSlots);

        curHopSampleCount_ = 0;
        curHopSumSqL_ = curHopSumSqR_ = 0.0;

        if (filledSlots_ < kNumSlots)
            continue;

        // Ring is full: one complete 3s window, sliding by one hop each time —
        // equivalent to Codex's blockPowers() sliding-window/hop scan.
        double sumSq = 0.0;
        for (int s = 0; s < kNumSlots; ++s)
            sumSq += slotSumSqL_[static_cast<size_t> (s)] + slotSumSqR_[static_cast<size_t> (s)];
        const double windowSamples = static_cast<double> (kNumSlots) * hopSizeSamples_;
        const double z = sumSq / windowSamples;

        if (z < kAbsGateZ)
            continue;

        // Causal relative gate: running mean of gate-1-passing blocks since last reset,
        // updated to include this block before testing it (Codex's offline version uses
        // the whole file's mean, computed before any gating decision is made).
        gate1PowerSum_ += z;
        ++gate1Count_;
        const double relGateZ = (gate1PowerSum_ / static_cast<double> (gate1Count_)) * 0.01;   // -20 LU

        if (z >= relGateZ)
        {
            const float lufs = static_cast<float> (-0.691 + 10.0 * std::log10 (z));
            lraHistogram_.addSample (lufs);
        }

        if (lraHistogram_.sampleCount() >= 2)
            lraLu_ = std::max (0.f, lraHistogram_.percentile (0.95f) - lraHistogram_.percentile (0.10f));
    }
}
