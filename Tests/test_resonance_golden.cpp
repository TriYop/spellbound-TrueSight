// TrueSight/Tests/test_resonance_golden.cpp
#include "../Source/Analysis/ResonancePeakMath.h"
#include "test_runner.h"
#include <cmath>
#include <vector>

int main() {
    constexpr int kFftSize = 4096, kHalfN = kFftSize / 2;
    const double sr = 48000.0;

    std::vector<float> avgMag(kHalfN, 0.01f);
    auto binOf = [&](float hz) { return int(hz * kFftSize / sr); };
    avgMag[binOf(1000.f)] = 1.0f;
    avgMag[binOf(4000.f)] = 0.5f;

    auto peaks = pickTrueSightResonancePeaks(avgMag.data(), kHalfN, sr, kFftSize);

    CHECK_MSG(peaks.size() >= 2, "expected at least 2 peaks (1kHz and 4kHz)");
    // Captured from Tests/capture_resonance_reference.cpp run against the
    // pre-migration (just-extracted, still hand-rolled) pickTrueSightResonancePeaks:
    // `freqHz=996.093750 q=42.500000 gainDb=-12.000000`
    // `freqHz=3996.093750 q=170.500000 gainDb=-12.000000`
    CHECK_MSG(std::abs(peaks[0].freqHz - 996.09375f) < 20.f, "peak 0 freq");
    CHECK_MSG(std::abs(peaks[0].gainDb - (-12.f)) < 0.5f, "peak 0 gain");
    CHECK_MSG(std::abs(peaks[1].freqHz - 3996.09375f) < 20.f, "peak 1 freq");
    CHECK_MSG(std::abs(peaks[1].gainDb - (-12.f)) < 0.5f, "peak 1 gain");

    TEST_SUMMARY();
}
