#include "ResonanceDetector.h"
#include <algorithm>
#include <cmath>

ResonanceDetector::ResonanceDetector (AnalysisResult& result)
    : juce::Thread ("MixAdvice Resonance"), result_ (result)
{
}

ResonanceDetector::~ResonanceDetector()
{
    stopThread (1000);
}

void ResonanceDetector::prepare (double sampleRate)
{
    stopThread (1000);

    sampleRate_ = sampleRate;

    for (int i = 0; i < kFftSize; ++i)
        hannWindow_[static_cast<size_t> (i)] = 0.5f * (1.f - std::cos (
            2.f * juce::MathConstants<float>::pi * static_cast<float> (i) / static_cast<float> (kFftSize)));

    fifo_.reset();
    resetWorkerState();

    startThread (juce::Thread::Priority::low);
}

void ResonanceDetector::suspend()
{
    stopThread (1000);
}

void ResonanceDetector::requestReset() noexcept
{
    resetRequested_.store (true, std::memory_order_release);
}

void ResonanceDetector::pushSamples (const float* mono, int numSamples) noexcept
{
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo_.prepareToWrite (numSamples, start1, size1, start2, size2);

    if (size1 > 0) std::copy (mono, mono + size1, fifoBuffer_.begin() + start1);
    if (size2 > 0) std::copy (mono + size1, mono + size1 + size2, fifoBuffer_.begin() + start2);

    fifo_.finishedWrite (size1 + size2);
}

void ResonanceDetector::resetWorkerState() noexcept
{
    avgMag_.fill (0.f);
    historyBuffer_.fill (0.f);
    windowsSinceReset_ = 0;
    hopsSincePeakPick_ = 0;
    result_.resonanceCount.store (0, std::memory_order_relaxed);
}

void ResonanceDetector::run()
{
    while (! threadShouldExit())
    {
        wait (15);

        if (resetRequested_.exchange (false, std::memory_order_acq_rel))
            resetWorkerState();

        while (fifo_.getNumReady() >= kHopSize && ! threadShouldExit())
        {
            int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
            fifo_.prepareToRead (kHopSize, start1, size1, start2, size2);

            if (size1 > 0) std::copy (fifoBuffer_.begin() + start1, fifoBuffer_.begin() + start1 + size1,
                                       hopScratch_.begin());
            if (size2 > 0) std::copy (fifoBuffer_.begin() + start2, fifoBuffer_.begin() + start2 + size2,
                                       hopScratch_.begin() + size1);

            fifo_.finishedRead (size1 + size2);

            // Slide the 4096-sample analysis window by one hop (50% overlap).
            std::copy (historyBuffer_.begin() + kHopSize, historyBuffer_.end(), historyBuffer_.begin());
            std::copy (hopScratch_.begin(), hopScratch_.end(), historyBuffer_.end() - kHopSize);

            for (int i = 0; i < kFftSize; ++i)
                fftScratch_[static_cast<size_t> (i)] =
                    historyBuffer_[static_cast<size_t> (i)] * hannWindow_[static_cast<size_t> (i)];
            std::fill (fftScratch_.begin() + kFftSize, fftScratch_.end(), 0.f);

            fft_.performFrequencyOnlyForwardTransform (fftScratch_.data(), true);

            // Cumulative (Welford) running-mean magnitude spectrum, integrated since last
            // reset — matches the "integrated" family used elsewhere (avgRmsDb, etc), not a
            // decaying smoother: resonance detection is about stable, sustained peaks.
            ++windowsSinceReset_;
            for (int k = 0; k < kHalfN; ++k)
            {
                auto& avg = avgMag_[static_cast<size_t> (k)];
                avg += (fftScratch_[static_cast<size_t> (k)] - avg) / static_cast<float> (windowsSinceReset_);
            }

            if (++hopsSincePeakPick_ >= kPeakPickEveryNHops)
            {
                hopsSincePeakPick_ = 0;
                peakPickAndPublish();
            }
        }
    }
}

void ResonanceDetector::peakPickAndPublish() noexcept
{
    std::array<float, kHalfN> magDb {};
    for (int k = 0; k < kHalfN; ++k)
        magDb[static_cast<size_t> (k)] = 20.f * std::log10 (avgMag_[static_cast<size_t> (k)] + 1e-9f);

    // Prefix sum of magDb -> O(1) mean-over-range lookups for the background estimate
    // (Codex's naive per-bin +/-1-octave scan is O(bins x window); this is O(bins) total).
    prefixDb_[0] = 0.0;
    for (int k = 0; k < kHalfN; ++k)
        prefixDb_[static_cast<size_t> (k + 1)] = prefixDb_[static_cast<size_t> (k)]
                                                 + static_cast<double> (magDb[static_cast<size_t> (k)]);

    const auto binToHz = [this] (int bin) noexcept {
        return static_cast<float> (bin) * static_cast<float> (sampleRate_) / static_cast<float> (kFftSize);
    };
    const auto hzToBin = [this] (float hz) noexcept {
        return static_cast<int> (hz * static_cast<float> (kFftSize) / static_cast<float> (sampleRate_));
    };

    const int minBin = std::max (1, hzToBin (kMinFreqHz));
    const int maxBin = std::min (kHalfN - 2, hzToBin (kMaxFreqHz));

    std::array<Candidate, kMaxResults> top {};
    int topCount = 0;

    for (int k = minBin; k <= maxBin; ++k)
    {
        const float mk = magDb[static_cast<size_t> (k)];
        if (! (mk > magDb[static_cast<size_t> (k - 1)] && mk > magDb[static_cast<size_t> (k + 1)]))
            continue;   // not a strict local maximum

        const float freqHz = binToHz (k);
        const int lo = std::max (0, hzToBin (freqHz / std::sqrt (2.f)));
        const int hi = std::min (kHalfN - 1, hzToBin (freqHz * std::sqrt (2.f)));
        if (hi < lo)
            continue;

        const float bg = static_cast<float> (
            (prefixDb_[static_cast<size_t> (hi + 1)] - prefixDb_[static_cast<size_t> (lo)]) / (hi - lo + 1));

        const float prominence = mk - bg;
        if (prominence < kProminenceDb)
            continue;

        // 3dB bandwidth: scan outward from the peak until magnitude drops 3dB below it.
        int left = k;
        while (left > 0 && magDb[static_cast<size_t> (left)] >= mk - 3.f) --left;
        int right = k;
        while (right < kHalfN - 1 && magDb[static_cast<size_t> (right)] >= mk - 3.f) ++right;

        const float bw3dB = binToHz (right) - binToHz (left);
        if (bw3dB <= 0.f)
            continue;

        const float q = freqHz / bw3dB;
        if (q < kMinQ)
            continue;

        // Fixed top-N running insertion, sorted by prominence descending — no vector/sort.
        if (topCount < kMaxResults || prominence > top[static_cast<size_t> (kMaxResults - 1)].prominence)
        {
            const int insertAt = std::min (topCount, kMaxResults - 1);
            top[static_cast<size_t> (insertAt)] = { freqHz, q, prominence };
            if (topCount < kMaxResults)
                ++topCount;

            for (int j = insertAt;
                 j > 0 && top[static_cast<size_t> (j)].prominence > top[static_cast<size_t> (j - 1)].prominence;
                 --j)
                std::swap (top[static_cast<size_t> (j)], top[static_cast<size_t> (j - 1)]);
        }
    }

    for (int i = 0; i < topCount; ++i)
    {
        result_.resonanceFreqHz[static_cast<size_t> (i)].store (top[static_cast<size_t> (i)].freqHz,
                                                                  std::memory_order_relaxed);
        result_.resonanceQ[static_cast<size_t> (i)].store (top[static_cast<size_t> (i)].q,
                                                             std::memory_order_relaxed);
        result_.resonanceGainDb[static_cast<size_t> (i)].store (
            -std::clamp (top[static_cast<size_t> (i)].prominence, kMinGainDb, kMaxGainDb),
            std::memory_order_relaxed);
    }
    // Publish count last: readers use it to bound how many slots are valid, matching the
    // "slight tearing acceptable for a display" convention documented in AnalysisResult.h.
    result_.resonanceCount.store (topCount, std::memory_order_relaxed);
}
