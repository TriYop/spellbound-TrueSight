#pragma once
#include <array>
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "BandConfig.h"
#include "AnalysisResult.h"

// Splits the stereo input into 7 frequency bands using cascaded Linkwitz-Riley
// crossovers, then computes per-band RMS and L/R Pearson correlation.
// All measurements are smoothed with a first-order IIR before being written
// to `result` (which is safe to read from the UI thread at any time).
class AnalyserEngine
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void process (const juce::AudioBuffer<float>& buffer);
    void reset();

    AnalysisResult result;

private:
    static constexpr int numCrossovers = BandConfig::numBands - 1;

    static float blockRmsLinear   (const float* data, int n) noexcept;
    static float blockPeak        (const float* data, int n) noexcept;
    static float blockCorrelation (const float* L, const float* R, int n) noexcept;

    // One LP + one HP per crossover, each stereo-capable
    std::array<juce::dsp::LinkwitzRileyFilter<float>, numCrossovers> lpFilters_;
    std::array<juce::dsp::LinkwitzRileyFilter<float>, numCrossovers> hpFilters_;

    // Pre-allocated working buffers (size = maxBlockSize at prepare-time)
    juce::AudioBuffer<float> remainderBuf_;
    juce::AudioBuffer<float> bandBuf_;

    // Smoothed linear-amplitude RMS, correlation, and crest factor per band
    std::array<float, BandConfig::numBands> smoothRmsL_   {};
    std::array<float, BandConfig::numBands> smoothRmsR_   {};
    std::array<float, BandConfig::numBands> smoothCorr_   {};
    std::array<float, BandConfig::numBands> smoothCrestL_ {};
    std::array<float, BandConfig::numBands> smoothCrestR_ {};

    float rmsAlpha_   { 0.9f };   // recomputed in prepare()
    float corrAlpha_  { 0.97f };  // recomputed in prepare()
    float crestAlpha_ { 0.97f };  // recomputed in prepare()

    // Peak-hold: max smoothed linear RMS seen since last resetPeaks()
    std::array<float, BandConfig::numBands> peakRmsLinL_ {};
    std::array<float, BandConfig::numBands> peakRmsLinR_ {};

    // Broadband (pre-filterbank) level and real-time correlation
    float smoothOverallL_    { 0.f };
    float smoothOverallR_    { 0.f };
    float peakOverallLinL_   { 0.f };
    float peakOverallLinR_   { 0.f };
    float smoothOverallCorr_ { 1.f };

    // Running sums for integrated (long-term) Pearson correlation — reset with peaks
    double intSumLR_ { 0.0 };
    double intSumL2_ { 0.0 };
    double intSumR2_ { 0.0 };

    // Running RMS² sums per band for long-term average RMS — reset with peaks
    std::array<double, BandConfig::numBands> intBandSumL2_ {};
    std::array<double, BandConfig::numBands> intBandSumR2_ {};
    uint64_t intBandBlockCount_ { 0 };

public:
    void resetPeaks();
};
