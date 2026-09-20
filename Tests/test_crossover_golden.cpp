// TrueSight/Tests/test_crossover_golden.cpp
#include "audioplugins/common/dsp/SevenBandSplitter.h"
#include "test_runner.h"
#include <cmath>
#include <vector>

using audioplugins::common::dsp::SevenBandSplitter;

static float blockRmsDb(const std::vector<float>& v) {
    double sum = 0.0;
    for (float x : v) sum += double(x) * x;
    float rms = float(std::sqrt(sum / v.size()));
    return rms > 1e-7f ? 20.f * std::log10(rms) : -100.f;
}

int main() {
    SevenBandSplitter splitter;
    const int sr = 48000, blockSize = 512;
    splitter.prepare((float)sr, 2);

    const float centers[7] = {50.f, 160.f, 375.f, 1000.f, 3500.f, 10000.f, 16000.f};
    std::array<double, 7> phase{};

    std::vector<std::vector<float>> input(2, std::vector<float>(blockSize));
    std::vector<std::vector<std::vector<float>>> bands(
        7, std::vector<std::vector<float>>(2, std::vector<float>(blockSize)));

    // Accumulate per-band sum-of-squares across the whole 2s run for a
    // stable long-term RMS comparable to the captured reference.
    std::array<double, 7> sumSq{};
    long totalSamples = 0;

    for (int block = 0; block < (2 * sr) / blockSize; ++block) {
        for (int i = 0; i < blockSize; ++i) {
            float sample = 0.f;
            for (int b = 0; b < 7; ++b) {
                sample += 0.1f * std::sin((float)phase[b]);
                phase[b] += 2.0 * M_PI * centers[b] / sr;
            }
            input[0][i] = sample;
            input[1][i] = sample;
        }
        splitter.process(input, bands, blockSize);
        for (int b = 0; b < 7; ++b)
            for (int i = 0; i < blockSize; ++i)
                sumSq[b] += double(bands[b][0][i]) * bands[b][0][i];
        totalSamples += blockSize;
    }

    // Captured from the pre-migration juce::dsp::LinkwitzRileyFilter-based
    // AnalyserEngine via Tests/capture_crossover_reference.cpp (deleted after
    // this test was written), run with:
    //   cmake --build build --target capture_crossover_reference
    //   ./build/Tests/capture_crossover_reference
    // Raw output (band N: rmsDbL=...):
    //   band 0: rmsDbL=-24.180374
    //   band 1: rmsDbL=-24.630735
    //   band 2: rmsDbL=-26.745998
    //   band 3: rmsDbL=-23.824854
    //   band 4: rmsDbL=-24.627335
    //   band 5: rmsDbL=-22.833149
    //   band 6: rmsDbL=-29.039557
    // Tolerance is loose (2 dB) since this compares two independent LR4
    // implementations, not bit-identical code paths -- see this task's header note.
    const float expectedDb[7] = { -24.180374f, -24.630735f,
                                   -26.745998f, -23.824854f,
                                   -24.627335f, -22.833149f,
                                   -29.039557f };

    for (int b = 0; b < 7; ++b) {
        float rms = float(std::sqrt(sumSq[b] / totalSamples));
        float db = rms > 1e-7f ? 20.f * std::log10(rms) : -100.f;
        CHECK_MSG(std::abs(db - expectedDb[b]) < 2.f, "band RMS should match pre-migration reference within 2 dB");
    }

    TEST_SUMMARY();
}
