#include "AnalyserEngine.h"
#include <cmath>
#include <algorithm>
#include <cstdint>

void AnalyserEngine::prepare (const juce::dsp::ProcessSpec& spec)
{
    const auto sr       = spec.sampleRate;
    const auto maxBlock = static_cast<int> (spec.maximumBlockSize);
    const auto nch      = static_cast<int> (spec.numChannels);

    splitter_.prepare (static_cast<float> (sr), nch);
    splitterInput_.assign (static_cast<size_t> (nch), std::vector<float> (static_cast<size_t> (maxBlock)));
    splitterBands_.assign (static_cast<size_t> (BandConfig::numBands),
        std::vector<std::vector<float>> (static_cast<size_t> (nch), std::vector<float> (static_cast<size_t> (maxBlock))));

    monoScratch_.setSize  (1, maxBlock);

    const float blocksPerSec = static_cast<float> (sr) / static_cast<float> (maxBlock);
    rmsAlpha_   = std::exp (-1.f / (0.10f * blocksPerSec));   // 100 ms
    corrAlpha_  = std::exp (-1.f / (0.30f * blocksPerSec));   // 300 ms
    crestAlpha_ = std::exp (-1.f / (0.50f * blocksPerSec));   // 500 ms

    sampleRate_ = sr;
    loudness_.prepare (sr);
    resonance_.prepare (sr);

    reset();
}

void AnalyserEngine::reset()
{
    splitter_.reset();

    smoothRmsL_.fill   (0.f);
    smoothRmsR_.fill   (0.f);
    smoothCorr_.fill   (1.f);
    smoothCrestL_.fill (1.f);
    smoothCrestR_.fill (1.f);
    intBandSumL2_.fill (0.0);
    intBandSumR2_.fill (0.0);
    intBandBlockCount_ = 0;

    for (auto& h : bandRmsHistograms_) h.reset();
    loudness_.reset();
    samplesSinceReset_ = 0;
}

void AnalyserEngine::resetPeaks()
{
    peakRmsLinL_.fill (0.f);
    peakRmsLinR_.fill (0.f);
    peakOverallLinL_ = 0.f;
    peakOverallLinR_ = 0.f;

    intSumLR_ = intSumL2_ = intSumR2_ = 0.0;

    intBandSumL2_.fill (0.0);
    intBandSumR2_.fill (0.0);
    intBandBlockCount_ = 0;

    for (auto& h : bandRmsHistograms_) h.reset();
    loudness_.reset();
    resonance_.requestReset();
    samplesSinceReset_ = 0;

    for (auto& a : result.peakRmsDbL) a.store (-100.f, std::memory_order_relaxed);
    for (auto& a : result.peakRmsDbR) a.store (-100.f, std::memory_order_relaxed);
    result.peakOverallDbL       .store (-100.f, std::memory_order_relaxed);
    result.peakOverallDbR       .store (-100.f, std::memory_order_relaxed);
    result.integratedCorrelation.store (1.f,    std::memory_order_relaxed);
    result.secondsSinceReset    .store (0.f,    std::memory_order_relaxed);
    result.lraLu                .store (0.f,    std::memory_order_relaxed);

    for (auto& a : result.p10RmsDb) a.store (-100.f, std::memory_order_relaxed);
    for (auto& a : result.p50RmsDb) a.store (-100.f, std::memory_order_relaxed);
    for (auto& a : result.p95RmsDb) a.store (-100.f, std::memory_order_relaxed);
}

void AnalyserEngine::suspend()
{
    resonance_.suspend();
}

void AnalyserEngine::process (const juce::AudioBuffer<float>& buffer)
{
    const int nSamples  = buffer.getNumSamples();
    const int nChannels = std::min (buffer.getNumChannels(), 2);

    if (nSamples == 0 || nChannels < 2)
        return;

    // Captures smooth/peak state by reference for use in the store lambda below.
    auto storeBand = [&] (size_t i, const float* L, const float* R)
    {
        const float rmsL  = blockRmsLinear   (L, nSamples);
        const float rmsR  = blockRmsLinear   (R, nSamples);
        const float peakL = blockPeak        (L, nSamples);
        const float peakR = blockPeak        (R, nSamples);
        const float corr  = blockCorrelation (L, R, nSamples);

        smoothRmsL_[i]   = rmsAlpha_   * smoothRmsL_[i]   + (1.f - rmsAlpha_)   * rmsL;
        smoothRmsR_[i]   = rmsAlpha_   * smoothRmsR_[i]   + (1.f - rmsAlpha_)   * rmsR;
        smoothCorr_[i]   = corrAlpha_  * smoothCorr_[i]   + (1.f - corrAlpha_)  * corr;

        // Crest factor: instantaneous peak/RMS ratio, smoothed over 500 ms.
        // Guard against near-silence to avoid division by zero.
        const float crestL = rmsL > 1e-7f ? peakL / rmsL : 1.f;
        const float crestR = rmsR > 1e-7f ? peakR / rmsR : 1.f;
        smoothCrestL_[i] = crestAlpha_ * smoothCrestL_[i] + (1.f - crestAlpha_) * crestL;
        smoothCrestR_[i] = crestAlpha_ * smoothCrestR_[i] + (1.f - crestAlpha_) * crestR;

        if (smoothRmsL_[i] > peakRmsLinL_[i]) peakRmsLinL_[i] = smoothRmsL_[i];
        if (smoothRmsR_[i] > peakRmsLinR_[i]) peakRmsLinR_[i] = smoothRmsR_[i];

        auto toDb      = [] (float lin)   { return lin > 1e-7f ? 20.f * std::log10 (lin) : -100.f; };
        auto toCrestDb = [] (float ratio) { return ratio > 1.f  ? 20.f * std::log10 (ratio) : 0.f; };

        result.rmsDbL[i].store      (toDb      (smoothRmsL_[i]),   std::memory_order_relaxed);
        result.rmsDbR[i].store      (toDb      (smoothRmsR_[i]),   std::memory_order_relaxed);
        result.peakRmsDbL[i].store  (toDb      (peakRmsLinL_[i]),  std::memory_order_relaxed);
        result.peakRmsDbR[i].store  (toDb      (peakRmsLinR_[i]),  std::memory_order_relaxed);
        result.correlation[i].store (smoothCorr_[i],               std::memory_order_relaxed);
        result.transientDbL[i].store (toCrestDb (smoothCrestL_[i]), std::memory_order_relaxed);
        result.transientDbR[i].store (toCrestDb (smoothCrestR_[i]), std::memory_order_relaxed);

        // Accumulate RMS² for long-term average (count is incremented outside the lambda)
        intBandSumL2_[i] += static_cast<double> (rmsL) * rmsL;
        intBandSumR2_[i] += static_cast<double> (rmsR) * rmsR;

        if (intBandBlockCount_ > 0)
        {
            const auto n = static_cast<double> (intBandBlockCount_);
            result.avgRmsDbL[i].store (toDb (static_cast<float> (std::sqrt (intBandSumL2_[i] / n))),
                                       std::memory_order_relaxed);
            result.avgRmsDbR[i].store (toDb (static_cast<float> (std::sqrt (intBandSumR2_[i] / n))),
                                       std::memory_order_relaxed);
        }

        // Distribution-aware percentile stats of per-block RMS, integrated since resetPeaks().
        // Matches Codex's blend of L/R block RMS in dB, per band.
        auto& hist = bandRmsHistograms_[i];
        hist.addSample ((toDb (rmsL) + toDb (rmsR)) * 0.5f);
        result.p10RmsDb[i].store (hist.percentile (0.10f), std::memory_order_relaxed);
        result.p50RmsDb[i].store (hist.percentile (0.50f), std::memory_order_relaxed);
        result.p95RmsDb[i].store (hist.percentile (0.95f), std::memory_order_relaxed);
    };

    // Overall broadband level and mono compatibility — measured on the raw input before the filterbank
    {
        auto toDb = [] (float lin) { return lin > 1e-7f ? 20.f * std::log10 (lin) : -100.f; };

        const float* L = buffer.getReadPointer (0);
        const float* R = buffer.getReadPointer (1);

        const float rawL    = blockRmsLinear   (L, nSamples);
        const float rawR    = blockRmsLinear   (R, nSamples);
        const float rawCorr = blockCorrelation (L, R, nSamples);

        smoothOverallL_    = rmsAlpha_  * smoothOverallL_    + (1.f - rmsAlpha_)  * rawL;
        smoothOverallR_    = rmsAlpha_  * smoothOverallR_    + (1.f - rmsAlpha_)  * rawR;
        smoothOverallCorr_ = corrAlpha_ * smoothOverallCorr_ + (1.f - corrAlpha_) * rawCorr;

        if (smoothOverallL_ > peakOverallLinL_) peakOverallLinL_ = smoothOverallL_;
        if (smoothOverallR_ > peakOverallLinR_) peakOverallLinR_ = smoothOverallR_;

        // Accumulate block sums for the integrated (long-term) Pearson correlation
        for (int i = 0; i < nSamples; ++i)
        {
            const double l = L[i], r = R[i];
            intSumLR_ += l * r;
            intSumL2_ += l * l;
            intSumR2_ += r * r;
        }
        const double intDenom = std::sqrt (intSumL2_ * intSumR2_);
        const float  intCorr  = intDenom > 1e-12
            ? static_cast<float> (std::clamp (intSumLR_ / intDenom, -1.0, 1.0)) : 1.f;

        result.overallRmsDbL        .store (toDb (smoothOverallL_),   std::memory_order_relaxed);
        result.overallRmsDbR        .store (toDb (smoothOverallR_),   std::memory_order_relaxed);
        result.peakOverallDbL       .store (toDb (peakOverallLinL_),  std::memory_order_relaxed);
        result.peakOverallDbR       .store (toDb (peakOverallLinR_),  std::memory_order_relaxed);
        result.overallCorrelation   .store (smoothOverallCorr_,       std::memory_order_relaxed);
        result.integratedCorrelation.store (intCorr,                  std::memory_order_relaxed);

        loudness_.processBlock (L, R, nSamples);
        result.lraLu.store (loudness_.getLraLu(), std::memory_order_relaxed);

        // Mono downmix, handed off to the background resonance-detector thread via a
        // lock-free FIFO push (never blocks/allocates on this, the audio, thread).
        float* mono = monoScratch_.getWritePointer (0);
        for (int i = 0; i < nSamples; ++i)
            mono[i] = 0.5f * (L[i] + R[i]);
        resonance_.pushSamples (mono, nSamples);
    }

    for (int ch = 0; ch < nChannels; ++ch)
        std::copy (buffer.getReadPointer (ch), buffer.getReadPointer (ch) + nSamples,
                   splitterInput_[static_cast<size_t> (ch)].begin());

    splitter_.process (splitterInput_, splitterBands_, nSamples);

    for (size_t i = 0; i < static_cast<size_t> (BandConfig::numBands); ++i)
        storeBand (i, splitterBands_[i][0].data(), splitterBands_[i][1].data());

    ++intBandBlockCount_;

    samplesSinceReset_ += static_cast<uint64_t> (nSamples);
    result.secondsSinceReset.store (
        static_cast<float> (static_cast<double> (samplesSinceReset_) / sampleRate_),
        std::memory_order_relaxed);
}

float AnalyserEngine::blockRmsLinear (const float* data, int n) noexcept
{
    if (n <= 0) return 0.f;
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
        sum += static_cast<double> (data[i]) * data[i];
    return static_cast<float> (std::sqrt (sum / n));
}

float AnalyserEngine::blockPeak (const float* data, int n) noexcept
{
    float peak = 0.f;
    for (int i = 0; i < n; ++i)
    {
        const float a = std::abs (data[i]);
        if (a > peak) peak = a;
    }
    return peak;
}

float AnalyserEngine::blockCorrelation (const float* L, const float* R, int n) noexcept
{
    if (n <= 0) return 1.f;
    double sumLR = 0.0, sumL2 = 0.0, sumR2 = 0.0;
    for (int i = 0; i < n; ++i)
    {
        sumLR += static_cast<double> (L[i]) * R[i];
        sumL2 += static_cast<double> (L[i]) * L[i];
        sumR2 += static_cast<double> (R[i]) * R[i];
    }
    const double denom = std::sqrt (sumL2 * sumR2);
    if (denom < 1e-12) return 1.f;
    return static_cast<float> (std::clamp (sumLR / denom, -1.0, 1.0));
}
