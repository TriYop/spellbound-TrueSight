#include "Analysis/AnalyserEngine.h"
#include "test_runner.h"
#include <cmath>
#include <vector>

int main()
{
    AnalyserEngine engine;
    engine.prepare(48000.0, 512, 2);

    // 1 kHz sine at -6 dBFS on both channels, in-phase (correlation should be ~1).
    constexpr int n = 512;
    std::vector<float> left(n), right(n);
    for (int i = 0; i < n; ++i)
    {
        const float s = 0.5f * std::sin(2.0f * 3.14159265f * 1000.0f * static_cast<float>(i) / 48000.0f);
        left[i] = s;
        right[i] = s;
    }
    const float* channels[2] = { left.data(), right.data() };

    for (int block = 0; block < 200; ++block)   // warm up the smoothers
        engine.process(channels, 2, n);

    const auto snap = engine.result.read();

    std::string msg1 = "expected overall RMS near -6 dBFS, got " + std::to_string(snap.overallRmsDbL);
    CHECK_MSG(snap.overallRmsDbL > -12.f && snap.overallRmsDbL < -3.f, msg1.c_str());

    std::string msg2 = "expected near-perfect L/R correlation for in-phase input, got "
                       + std::to_string(snap.overallCorrelation);
    CHECK_MSG(snap.overallCorrelation > 0.99f, msg2.c_str());

    // Regression for the C1 finding (final whole-branch review): prepare() sizes
    // scratch buffers to maxBlockSize, but a host (concretely LV2, where
    // lv2_set_options() can raise the effective block size without a
    // deactivate/activate cycle) may later call process() with more frames than
    // that. Without the defensive clamp in AnalyserEngine::process(), this writes
    // past monoScratch_/splitterInput_/splitterBands_ (heap-buffer-overflow,
    // reproduced under ASAN). This test doesn't need ASAN itself -- it just
    // exercises the code path so a build with ASAN (CI or local) would catch a
    // regression if the clamp were ever removed.
    {
        AnalyserEngine smallEngine;
        smallEngine.prepare(48000.0, 256, 2);

        constexpr int bigN = 1024;
        std::vector<float> bigLeft(bigN), bigRight(bigN);
        for (int i = 0; i < bigN; ++i)
        {
            const float s = 0.5f * std::sin(2.0f * 3.14159265f * 1000.0f * static_cast<float>(i) / 48000.0f);
            bigLeft[i] = s;
            bigRight[i] = s;
        }
        const float* bigChannels[2] = { bigLeft.data(), bigRight.data() };

        // Must not crash/corrupt memory even though bigN > the 256-frame block
        // size prepare() sized the scratch buffers for.
        smallEngine.process(bigChannels, 2, bigN);

        const auto smallSnap = smallEngine.result.read();
        CHECK_MSG(std::isfinite(smallSnap.overallRmsDbL),
                   "expected finite overall RMS after an oversized process() call");
    }

    TEST_SUMMARY();
}
