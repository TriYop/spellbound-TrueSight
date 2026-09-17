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
    // submits one ResonanceJob per completed hop. Never blocks or allocates
    // (the leftover-sample staging buffer is fixed-size, pre-allocated in
    // prepare()).
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
};
