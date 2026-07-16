#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>
#include "AnalysisResult.h"

// Spectral resonance-cut detection, ported from Codex/MasterTweak's offline
// resonance_detector.cpp — but run on a dedicated background thread rather
// than inline on the audio thread.
//
// Codex's background-level estimate (mean dB over a +/-1 octave window around
// each bin) is O(bins x window) per bin if done naively; even throttled, a
// burst of that size can blow a small host block's time budget. Rather than
// rely on "fast enough in practice", the whole FFT + peak-pick pipeline runs
// on its own low-priority juce::Thread: the audio thread only does a mono
// downmix and a juce::AbstractFifo push (never blocks, never allocates).
// The prefix-sum rewrite of the background estimate (see peakPickAndPublish)
// is still required regardless of thread placement — it's what keeps the
// worker's periodic pass cheap.
class ResonanceDetector : private juce::Thread
{
public:
    explicit ResonanceDetector (AnalysisResult& result);
    ~ResonanceDetector() override;

    // (Re)configure for a new sample rate and (re)start the worker thread.
    void prepare (double sampleRate);

    // Stop the worker thread (e.g. PluginProcessor::releaseResources()).
    void suspend();

    // Audio-thread: asks the worker to clear its accumulated spectrum on its
    // next iteration (mirrors AnalyserEngine::resetPeaks()).
    void requestReset() noexcept;

    // Audio-thread: push a mono downmix block. Never blocks or allocates;
    // silently drops samples if the worker has fallen behind.
    void pushSamples (const float* mono, int numSamples) noexcept;

private:
    void run() override;
    void resetWorkerState() noexcept;
    void peakPickAndPublish() noexcept;

    static constexpr int kFftOrder = 12;                 // 4096-point FFT
    static constexpr int kFftSize  = 1 << kFftOrder;
    static constexpr int kHopSize  = kFftSize / 2;        // 50% overlap
    static constexpr int kHalfN    = kFftSize / 2;        // bins 0..kHalfN-1
    static constexpr int kFifoCapacity = 16384;
    static constexpr int kPeakPickEveryNHops = 5;         // throttle: ~4-5 Hz at typical sample rates

    static constexpr float kMinFreqHz    = 80.f;
    static constexpr float kMaxFreqHz    = 16000.f;
    static constexpr float kMinQ         = 3.f;
    static constexpr int   kMaxResults   = AnalysisResult::maxResonances;
    static constexpr float kProminenceDb = 6.f;
    static constexpr float kMaxGainDb    = 12.f;
    static constexpr float kMinGainDb    = 3.f;

    struct Candidate { float freqHz = 0.f, q = 1.f, prominence = 0.f; };

    AnalysisResult& result_;

    // Audio-thread <-> worker-thread handoff. juce::AbstractFifo is lock-free
    // (Atomic<int>-backed) and safe for exactly this single-producer/single-
    // consumer use.
    juce::AbstractFifo fifo_ { kFifoCapacity };
    std::array<float, kFifoCapacity> fifoBuffer_ {};

    std::atomic<bool> resetRequested_ { false };
    double sampleRate_ { 44100.0 };

    // Worker-thread-only state (never touched by the audio thread).
    juce::dsp::FFT fft_ { kFftOrder };
    std::array<float, kFftSize>  hannWindow_ {};
    std::array<float, kFftSize>  historyBuffer_ {};    // sliding 4096-sample analysis window
    std::array<float, kHopSize>  hopScratch_ {};
    std::array<float, 2 * kFftSize> fftScratch_ {};
    std::array<float, kHalfN>    avgMag_ {};            // cumulative (Welford) running-mean magnitude spectrum
    std::array<double, kHalfN + 1> prefixDb_ {};        // prefix sum of magDb, for O(1) background-level lookups
    uint64_t windowsSinceReset_ { 0 };
    int      hopsSincePeakPick_ { 0 };
};
