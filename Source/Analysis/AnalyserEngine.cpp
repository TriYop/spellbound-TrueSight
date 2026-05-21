#include "AnalyserEngine.h"
#include <cmath>
#include <algorithm>
#include <cstdint>

void AnalyserEngine::prepare (const juce::dsp::ProcessSpec& spec)
{
    const auto sr       = spec.sampleRate;
    const auto maxBlock = static_cast<int> (spec.maximumBlockSize);
    const auto nch      = static_cast<int> (spec.numChannels);

    for (size_t i = 0; i < static_cast<size_t> (numCrossovers); ++i)
    {
        lpFilters_[i].setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
        lpFilters_[i].setCutoffFrequency (BandConfig::crossoverHz[i]);
        lpFilters_[i].prepare (spec);

        hpFilters_[i].setType (juce::dsp::LinkwitzRileyFilterType::highpass);
        hpFilters_[i].setCutoffFrequency (BandConfig::crossoverHz[i]);
        hpFilters_[i].prepare (spec);
    }

    remainderBuf_.setSize (nch, maxBlock);
    bandBuf_.setSize      (nch, maxBlock);

    const float blocksPerSec = static_cast<float> (sr) / static_cast<float> (maxBlock);
    rmsAlpha_   = std::exp (-1.f / (0.10f * blocksPerSec));   // 100 ms
    corrAlpha_  = std::exp (-1.f / (0.30f * blocksPerSec));   // 300 ms
    crestAlpha_ = std::exp (-1.f / (0.50f * blocksPerSec));   // 500 ms

    reset();
}

void AnalyserEngine::reset()
{
    for (auto& f : lpFilters_) f.reset();
    for (auto& f : hpFilters_) f.reset();

    smoothRmsL_.fill   (0.f);
    smoothRmsR_.fill   (0.f);
    smoothCorr_.fill   (1.f);
    smoothCrestL_.fill (1.f);
    smoothCrestR_.fill (1.f);
    intBandSumL2_.fill (0.0);
    intBandSumR2_.fill (0.0);
    intBandBlockCount_ = 0;
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

    for (auto& a : result.peakRmsDbL) a.store (-100.f, std::memory_order_relaxed);
    for (auto& a : result.peakRmsDbR) a.store (-100.f, std::memory_order_relaxed);
    result.peakOverallDbL       .store (-100.f, std::memory_order_relaxed);
    result.peakOverallDbR       .store (-100.f, std::memory_order_relaxed);
    result.integratedCorrelation.store (1.f,    std::memory_order_relaxed);
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
    }

    // Copy input into the running remainder buffer
    for (int ch = 0; ch < nChannels; ++ch)
        remainderBuf_.copyFrom (ch, 0, buffer, ch, 0, nSamples);

    // Cascaded crossover: LP extracts the current band, HP passes the remainder forward.
    for (size_t i = 0; i < static_cast<size_t> (numCrossovers); ++i)
    {
        for (int ch = 0; ch < nChannels; ++ch)
            bandBuf_.copyFrom (ch, 0, remainderBuf_, ch, 0, nSamples);

        auto bandBlock = juce::dsp::AudioBlock<float> (
            bandBuf_.getArrayOfWritePointers(), (size_t) nChannels, (size_t) nSamples);
        lpFilters_[i].process (juce::dsp::ProcessContextReplacing<float> (bandBlock));

        auto remBlock = juce::dsp::AudioBlock<float> (
            remainderBuf_.getArrayOfWritePointers(), (size_t) nChannels, (size_t) nSamples);
        hpFilters_[i].process (juce::dsp::ProcessContextReplacing<float> (remBlock));

        storeBand (i, bandBuf_.getReadPointer (0), bandBuf_.getReadPointer (1));
    }

    // Final band: whatever remains after all HP stages
    storeBand (static_cast<size_t> (numCrossovers),
               remainderBuf_.getReadPointer (0),
               remainderBuf_.getReadPointer (1));

    ++intBandBlockCount_;
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
