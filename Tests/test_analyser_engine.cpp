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

    TEST_SUMMARY();
}
