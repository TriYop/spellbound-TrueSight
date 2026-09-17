#include "ResonancePeakMath.h"
#include "audioplugins/common/analysis/ResonancePeakPicker.h"
#include <algorithm>
#include <cmath>

std::vector<ResonancePeakResult> pickTrueSightResonancePeaks (
    const float* avgMagLinear, int halfN, double sampleRate, int fftSize)
{
    constexpr float kMinFreqHz = 80.f, kMaxFreqHz = 16000.f, kMinQ = 3.f;
    constexpr float kProminenceDb = 6.f, kMaxGainDb = 12.f, kMinGainDb = 3.f;
    constexpr int   kMaxResults = 8;

    std::vector<float> magDb (static_cast<size_t> (halfN));
    for (int k = 0; k < halfN; ++k)
        magDb[static_cast<size_t> (k)] = 20.f * std::log10 (avgMagLinear[k] + 1e-9f);

    const auto peaks = audioplugins::common::analysis::pickResonancePeaks (
        magDb.data(), halfN, static_cast<float> (sampleRate), fftSize,
        kMinFreqHz, kMaxFreqHz, kMinQ, kProminenceDb, kMaxResults);

    std::vector<ResonancePeakResult> out;
    out.reserve (peaks.size());
    for (const auto& p : peaks)
        out.push_back ({ p.freqHz, p.q, -std::clamp (p.prominenceDb, kMinGainDb, kMaxGainDb) });
    return out;
}
