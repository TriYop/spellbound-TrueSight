#pragma once
#include <array>
#include <vector>
#include "audioplugins/common/dsp/SevenBandSplitter.h"
#include "BandConfig.h"
#include "AnalysisResult.h"
#include "QuantileHistogram.h"
#include "LoudnessAnalyser.h"
#include "ResonanceWorker.h"

// Splits the stereo input into 7 frequency bands using
// common::dsp::SevenBandSplitter (Linkwitz-Riley crossovers), then computes
// per-band RMS and L/R Pearson correlation. All measurements are smoothed
// with a first-order IIR before being written to `result` (which is safe to
// read from the UI thread at any time).
class AnalyserEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void process (const float* const* channelData, int numChannels, int numSamples);
    void reset();

    AnalysisResult result;

private:
    static float blockRmsLinear   (const float* data, int n) noexcept;
    static float blockPeak        (const float* data, int n) noexcept;
    static float blockCorrelation (const float* L, const float* R, int n) noexcept;

    audioplugins::common::dsp::SevenBandSplitter splitter_;
    // Pre-allocated once in prepare(); process() only fills these, never resizes,
    // so the audio thread never allocates.
    std::vector<std::vector<float>> splitterInput_;                    // [channel][frame]
    std::vector<std::vector<std::vector<float>>> splitterBands_;       // [band][channel][frame]

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

    // Per-band P10/P50/P95 of per-block RMS (dB), integrated since last resetPeaks()
    std::array<QuantileHistogram, BandConfig::numBands> bandRmsHistograms_;

    // EBU R128 Loudness Range, measured on the same raw broadband signal used
    // for overall RMS/correlation.
    LoudnessAnalyser loudness_;

    double   sampleRate_        { 44100.0 };
    uint64_t samplesSinceReset_ { 0 };

    // Pre-allocated mono downmix scratch buffer (was juce::AudioBuffer<float>).
    std::vector<float> monoScratch_;

    // Background-thread spectral resonance detector. Declared after `result` (above) so
    // it is destroyed *before* `result` (C++ destroys members in reverse declaration
    // order) — it holds a reference to `result` and must stop before that reference
    // becomes dangling.
    ResonanceWorker resonance_ { result };

public:
    void resetPeaks();
    void suspend();   // stop the resonance-detector worker thread (PluginProcessor::releaseResources())
};
