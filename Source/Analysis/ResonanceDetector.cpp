#include "ResonanceDetector.h"
#include "ResonancePeakMath.h"
#include "audioplugins/common/dsp/Fft.h"
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

    fftRe_.assign (kFftSize, 0.f);
    fftIm_.assign (kFftSize, 0.f);

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

            std::copy (fftScratch_.begin(), fftScratch_.begin() + kFftSize, fftRe_.begin());
            std::fill (fftIm_.begin(), fftIm_.end(), 0.f);
            audioplugins::common::dsp::inplaceFft (fftRe_, fftIm_);
            for (int k = 0; k < kHalfN; ++k)
                fftScratch_[static_cast<size_t> (k)] =
                    std::sqrt (fftRe_[static_cast<size_t> (k)] * fftRe_[static_cast<size_t> (k)]
                             + fftIm_[static_cast<size_t> (k)] * fftIm_[static_cast<size_t> (k)]);

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
    const auto peaks = pickTrueSightResonancePeaks (avgMag_.data(), kHalfN, sampleRate_, kFftSize);
    for (size_t i = 0; i < peaks.size() && i < static_cast<size_t> (kMaxResults); ++i)
    {
        result_.resonanceFreqHz[i].store (peaks[i].freqHz, std::memory_order_relaxed);
        result_.resonanceQ[i].store (peaks[i].q, std::memory_order_relaxed);
        result_.resonanceGainDb[i].store (peaks[i].gainDb, std::memory_order_relaxed);
    }
    // Publish count last: readers use it to bound how many slots are valid, matching the
    // "slight tearing acceptable for a display" convention documented in AnalysisResult.h.
    result_.resonanceCount.store (static_cast<int> (peaks.size()), std::memory_order_relaxed);
}
