// TrueSight/Tests/test_loudness_golden.cpp
#include "audioplugins/common/analysis/LoudnessAnalyser.h"
#include "test_runner.h"
#include <cmath>
#include <vector>

using audioplugins::common::analysis::LoudnessAnalyser;

int main() {
    LoudnessAnalyser la;
    la.prepare(48000.0, 2);

    const int sr = 48000;
    std::vector<float> buf(512);
    double phase = 0.0;
    const double freq = 1000.0;

    auto runSeconds = [&](double seconds, float amplitude) {
        int totalSamples = int(seconds * sr);
        for (int done = 0; done < totalSamples; done += (int)buf.size()) {
            int n = std::min((int)buf.size(), totalSamples - done);
            for (int i = 0; i < n; ++i) {
                buf[i] = amplitude * std::sin(phase);
                phase += 2.0 * M_PI * freq / sr;
            }
            la.processBlock(buf.data(), buf.data(), n);
        }
    };

    runSeconds(4.0, 0.1f);
    // Captured from Tests/capture_loudness_reference.cpp run against the
    // pre-migration ::LoudnessAnalyser (Source/Analysis/LoudnessAnalyser.cpp):
    // `after quiet: lraLu=0.000000`
    CHECK_MSG(std::abs(la.getLraLu() - 0.f) < 0.1f,
              "LRA after quiet segment should match the pre-migration reference");

    runSeconds(4.0, 0.5f);
    // Captured: `after loud: lraLu=14.000000`
    CHECK_MSG(std::abs(la.getLraLu() - 14.f) < 0.1f,
              "LRA after loud segment should match the pre-migration reference");

    TEST_SUMMARY();
}
