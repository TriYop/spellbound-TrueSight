#pragma once
#include <array>
#include <juce_core/juce_core.h>
#include "Analysis/BandConfig.h"

// Per-genre reference targets displayed as amber guidelines on the meters.
// bandRmsDb      : expected average per-band RMS (dBFS) for a well-balanced mix in this genre
// bandMinCorr    : minimum acceptable L/R correlation per band (below this = visible warning)
// bandTransientDb: expected per-band crest factor (peak/RMS) in dB — proxy for transient energy
//                  ~3 dB = sustained/sine, ~10 dB = moderate, ~18 dB = very punchy
// overallRmsDb   : target integrated loudness proxy (dBFS RMS)
// overallMinCorr : minimum acceptable broadband mono compatibility
// description    : one-line genre characterisation shown in the UI
struct PresetData
{
    juce::String name;
    juce::String description;
    std::array<float, BandConfig::numBands> bandRmsDb;
    std::array<float, BandConfig::numBands> bandMinCorr;
    std::array<float, BandConfig::numBands> bandTransientDb;
    float overallRmsDb;
    float overallMinCorr;
};
