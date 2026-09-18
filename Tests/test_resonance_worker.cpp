#include "Analysis/ResonanceWorker.h"
#include "test_runner.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

namespace
{
// Must match ResonanceWorker's private kHopSize (kFftSize/2, kFftSize = 1<<12).
constexpr int kHopSize = 2048;

void fill2kHzTone (std::vector<float>& chunk, long& sampleIndex, double sampleRate)
{
    for (size_t i = 0; i < chunk.size(); ++i)
    {
        const double t = static_cast<double> (sampleIndex++) / sampleRate;
        chunk[i] = 0.3f * static_cast<float> (std::sin (2.0 * 3.14159265 * 2000.0 * t));
    }
}
}

static void testBasicPeakDetection()
{
    AnalysisResult result;
    ResonanceWorker worker (result);
    worker.prepare (48000.0);

    // Feed a sustained 2 kHz tone in 512-sample chunks -- enough hops for
    // several peak-pick cycles (kPeakPickEveryNHops == 5, kHopSize == 2048).
    std::vector<float> chunk (512);
    long sampleIndex = 0;
    for (int block = 0; block < 400; ++block)
    {
        fill2kHzTone (chunk, sampleIndex, 48000.0);
        worker.pushSamples (chunk.data(), 512);
    }

    // Give the background worker time to drain the queue.
    std::this_thread::sleep_for (std::chrono::milliseconds (200));

    // AnalysisResult is only ever written from the audio thread (inside
    // pushSamples()), and publishing now only happens when a hop actually
    // completes (see ResonanceWorker::pushSamples()) -- so, exactly as real
    // continuous audio would, one more full hop is needed to copy the
    // by-now-converged background result into AnalysisResult. 400*512 is an
    // exact multiple of kHopSize, so the staging buffer is empty here and
    // this pushes exactly one more hop.
    std::vector<float> extraHop (static_cast<size_t> (kHopSize));
    fill2kHzTone (extraHop, sampleIndex, 48000.0);
    worker.pushSamples (extraHop.data(), kHopSize);

    const auto snap = result.read();
    CHECK_MSG (snap.resonanceCount > 0, "expected at least one resonance peak published");

    bool foundNear2k = false;
    for (int i = 0; i < snap.resonanceCount; ++i)
        if (std::abs (snap.resonanceFreqHz[static_cast<size_t> (i)] - 2000.f) < 200.f)
            foundNear2k = true;
    CHECK_MSG (foundNear2k, "expected a detected peak near 2000 Hz");

    worker.suspend();
}

// Regression test for the reviewer's Finding 1: requestReset()'s zeroing of
// the display must not be immediately undone by the next pushSamples() call
// republishing a stale pre-reset peak set. Deterministic by construction
// (no reliance on the background thread being "slow enough"): the fix makes
// pushSamples() either (a) see that the background hasn't processed the
// reset yet and skip publishing (leaving the zero requestReset() already
// wrote), or (b) see that it has, in which case the newly-processed result
// itself already reports count == 0. Either way the assertion below holds
// immediately, not just "eventually".
static void testResetTakesEffectPromptly()
{
    AnalysisResult result;
    ResonanceWorker worker (result);
    worker.prepare (48000.0);

    std::vector<float> hop (static_cast<size_t> (kHopSize));
    long sampleIndex = 0;

    // Bounded poll (deadline, not a blind sleep) until at least one peak has
    // been published, feeding one full hop of 2 kHz tone at a time.
    bool sawPeak = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (2);
    while (std::chrono::steady_clock::now() < deadline)
    {
        fill2kHzTone (hop, sampleIndex, 48000.0);
        worker.pushSamples (hop.data(), kHopSize);

        if (result.read().resonanceCount > 0)
        {
            sawPeak = true;
            break;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }
    CHECK_MSG (sawPeak, "expected at least one resonance peak before testing reset");

    worker.requestReset();

    // Immediately push exactly one more hop. Per the fix, this must not
    // republish the stale pre-reset peak set.
    std::fill (hop.begin(), hop.end(), 0.f);
    worker.pushSamples (hop.data(), kHopSize);

    CHECK_MSG (result.read().resonanceCount == 0,
               "expected reset to take effect on the very next hop, not stale pre-reset peaks");

    worker.suspend();
}

int main()
{
    testBasicPeakDetection();
    testResetTakesEffectPromptly();
    TEST_SUMMARY();
}
