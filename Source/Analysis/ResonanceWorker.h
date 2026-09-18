#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>
#include "audioplugins/common/io/AsyncWorker.h"
#include "AnalysisResult.h"

// Spectral resonance-cut detection. Same algorithm as before Stage 4B (Hann
// window, Welford-averaged magnitude spectrum, prefix-sum peak-pick, all
// already on common::dsp/common::analysis since Stage 4A) -- only the
// threading primitive changed, from juce::Thread + juce::AbstractFifo to
// audioplugins::common::io::AsyncWorker.
class ResonanceWorker
{
public:
    explicit ResonanceWorker (AnalysisResult& result);
    ~ResonanceWorker();

    void prepare (double sampleRate);
    void suspend();
    void requestReset() noexcept;

    // Audio-thread: buffers incoming samples into kHopSize-sized chunks and
    // submits one ResonanceJob per completed hop.
    //
    // NOT fully real-time-safe: it never allocates (the leftover-sample
    // staging buffer is fixed-size, pre-allocated in prepare()) and
    // submit() itself is lock-free, but once a hop completes this also
    // calls the underlying AsyncWorker::getLatest(), which reads a
    // std::atomic<ResonanceResult>. ResonanceResult is ~100 bytes -- too
    // large for a lock-free atomic on this platform (hence the explicit
    // libatomic link) -- so that read takes a lock from libatomic's
    // address-keyed lock table, the same lock the background worker thread
    // takes on every processJob() store(). This is a known, accepted
    // tradeoff (inherited from AsyncWorker's API, not fixable here without
    // Common-library-level lock-free support for large result types): the
    // audio thread can briefly block on the non-realtime worker thread.
    // To keep this rare, the lock is only taken once per completed hop
    // (~43ms at 48kHz), not once per pushSamples() call (which may be
    // called once per host audio block, i.e. every 1-10ms).
    void pushSamples (const float* mono, int numSamples) noexcept;

private:
    static constexpr int kFftOrder = 12;
    static constexpr int kFftSize  = 1 << kFftOrder;
    static constexpr int kHopSize  = kFftSize / 2;
    static constexpr int kHalfN    = kFftSize / 2;
    static constexpr int kPeakPickEveryNHops = 5;
    static constexpr int kMaxResults = AnalysisResult::maxResonances;

    struct ResonanceJob
    {
        std::array<float, kHopSize> samples {};
    };

    struct ResonanceResult
    {
        std::array<float, kMaxResults> freqHz {};
        std::array<float, kMaxResults> q      {};
        std::array<float, kMaxResults> gainDb {};
        int count = 0;

        // Bumped by Worker::resetState() every time it runs (including the
        // very first call from the constructor). Lets ResonanceWorker
        // (audio thread) tell "background has processed a reset since I
        // last asked" apart from "background hasn't gotten to it yet",
        // without relying on timing -- see ResonanceWorker::requestReset()/
        // publishLatest().
        uint64_t generation = 0;
    };

    class Worker : public audioplugins::common::io::AsyncWorker<ResonanceJob, ResonanceResult>
    {
    public:
        explicit Worker (double sampleRate);
        void resetState() noexcept;
        std::atomic<bool> resetRequested { false };

    protected:
        ResonanceResult processJob (const ResonanceJob& job) noexcept override;

    private:
        double sampleRate_ { 44100.0 };
        std::vector<float> fftRe_, fftIm_;
        std::array<float, kFftSize> hannWindow_ {};
        std::array<float, kFftSize> historyBuffer_ {};
        std::array<float, 2 * kFftSize> fftScratch_ {};
        std::array<float, kHalfN> avgMag_ {};
        uint64_t windowsSinceReset_ { 0 };
        int hopsSincePeakPick_ { 0 };

        // Last computed peak-pick outcome, returned from processJob() on
        // every hop (not just peak-pick hops) so getLatest() always yields
        // the latest known-good result -- see processJob()'s comment.
        ResonanceResult cachedResult_ {};
    };

    AnalysisResult& result_;
    std::unique_ptr<Worker> worker_;

    // Audio-thread-only staging buffer: accumulates pushSamples() calls
    // (which may not align to kHopSize) into complete hops before submit().
    std::array<float, kHopSize> stagingBuffer_ {};
    int stagingFill_ = 0;

    // Audio-thread-only reset bookkeeping (requestReset() and pushSamples()
    // are both only ever called from the audio thread -- see
    // AnalyserEngine::resetPeaks()/process()). awaitingReset_ is set by
    // requestReset() and cleared by publishLatest() once the background
    // worker's generation has moved past resetBaselineGeneration_ (the
    // generation observed at request time), proving the reset was actually
    // processed rather than assuming it based on elapsed time.
    void publishLatest (const ResonanceResult& latest) noexcept;
    bool awaitingReset_ = false;
    uint64_t resetBaselineGeneration_ = 0;
};
