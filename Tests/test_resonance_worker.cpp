#include "Analysis/ResonanceWorker.h"
#include "test_runner.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

int main()
{
    AnalysisResult result;
    ResonanceWorker worker (result);
    worker.prepare (48000.0);

    // Feed a sustained 2 kHz tone in 512-sample chunks -- enough hops for
    // several peak-pick cycles (kPeakPickEveryNHops == 5, kHopSize == 2048).
    std::vector<float> chunk (512);
    for (int block = 0; block < 400; ++block)
    {
        for (int i = 0; i < 512; ++i)
        {
            const double t = static_cast<double> (block * 512 + i) / 48000.0;
            chunk[static_cast<size_t> (i)] = 0.3f * static_cast<float> (std::sin (2.0 * 3.14159265 * 2000.0 * t));
        }
        worker.pushSamples (chunk.data(), 512);
    }

    // Give the background worker time to drain the queue.
    std::this_thread::sleep_for (std::chrono::milliseconds (200));

    // AnalysisResult is only ever written from the audio thread (inside
    // pushSamples()), matching the "written by the audio thread, read by
    // the UI thread" contract documented on AnalysisResult -- so, exactly
    // as real continuous audio would, one more call is needed to actually
    // copy the by-now-converged background result into AnalysisResult.
    const float silence = 0.f;
    worker.pushSamples (&silence, 1);

    const auto snap = result.read();
    CHECK_MSG (snap.resonanceCount > 0, "expected at least one resonance peak published");

    bool foundNear2k = false;
    for (int i = 0; i < snap.resonanceCount; ++i)
        if (std::abs (snap.resonanceFreqHz[static_cast<size_t> (i)] - 2000.f) < 200.f)
            foundNear2k = true;
    CHECK_MSG (foundNear2k, "expected a detected peak near 2000 Hz");

    worker.suspend();
    TEST_SUMMARY();
}
