#pragma once
#include <vector>

struct ResonancePeakResult { float freqHz = 0.f, q = 1.f, gainDb = 0.f; };

// Pure function: avgMagLinear (linear magnitude spectrum, halfN bins) -> up
// to kMaxResults peaks, sorted by prominence descending. Exposed for
// golden-vector testing; production code and tests call the exact same
// function.
std::vector<ResonancePeakResult> pickTrueSightResonancePeaks (
    const float* avgMagLinear, int halfN, double sampleRate, int fftSize);
