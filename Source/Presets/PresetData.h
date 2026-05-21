#pragma once
#include <array>
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
    const char* name;
    const char* description;
    std::array<float, BandConfig::numBands> bandRmsDb;
    std::array<float, BandConfig::numBands> bandMinCorr;
    std::array<float, BandConfig::numBands> bandTransientDb;
    float overallRmsDb;
    float overallMinCorr;
};

namespace Presets
{
    static constexpr int count = 12;

    // Band order: Sub(0/<80Hz)  Lows(1/80-250Hz)  Lo-Mid(2/250-500Hz)
    //            Mids(3/500-2kHz)  Hi-Mid(4/2k-6kHz)  Highs(5/6k-16kHz)  Air(6/>16kHz)
    inline const PresetData data[count] =
    {
        {
            "Classical (Karajan)",
            "Very dynamic, wide orchestral range, highly mono-compatible",
            { -28.f, -24.f, -26.f, -24.f, -28.f, -32.f, -36.f },
            { 0.85f, 0.80f, 0.75f, 0.70f, 0.60f, 0.50f, 0.40f },
            // High dynamics: sustained strings + sharp brass/perc attacks
            {  6.f,  12.f,  16.f,  18.f,  20.f,  18.f,  14.f },
            -26.f, 0.70f
        },
        {
            "Soundtrack (Zimmer)",
            "Punchy lows, thick mids, controlled highs, cinematic dynamics",
            { -22.f, -20.f, -22.f, -22.f, -26.f, -30.f, -34.f },
            { 0.80f, 0.75f, 0.70f, 0.65f, 0.55f, 0.45f, 0.35f },
            // Mix of sustained pads and punchy percussion
            {  8.f,  12.f,  14.f,  16.f,  18.f,  16.f,  12.f },
            -22.f, 0.65f
        },
        {
            "Folk (Fest Noz)",
            "Vocal-range mids forward, acoustic instruments, natural stereo",
            { -32.f, -28.f, -24.f, -22.f, -24.f, -28.f, -34.f },
            { 0.85f, 0.82f, 0.78f, 0.72f, 0.65f, 0.55f, 0.45f },
            // Acoustic plucks and bowed instruments: moderate-to-high transients
            {  6.f,  12.f,  16.f,  18.f,  18.f,  16.f,  12.f },
            -24.f, 0.72f
        },
        {
            "Electronic (Daft Punk)",
            "Heavy sub and lows, very compressed, stereo widening in highs",
            { -18.f, -16.f, -20.f, -22.f, -24.f, -26.f, -30.f },
            { 0.95f, 0.85f, 0.65f, 0.55f, 0.45f, 0.35f, 0.25f },
            // Hard limiting crushes dynamics; 808s give moderate sub transients
            {  8.f,  10.f,   8.f,  10.f,  12.f,  10.f,   8.f },
            -14.f, 0.55f
        },
        {
            "Pop (Billie Eilish)",
            "Loud and intimate, forward vocals in mids/hi-mid, punchy lows",
            { -20.f, -18.f, -20.f, -20.f, -22.f, -24.f, -28.f },
            { 0.85f, 0.80f, 0.70f, 0.65f, 0.55f, 0.45f, 0.35f },
            // Heavily compressed/limited pop master, modest crest factors
            {  6.f,   8.f,   8.f,  10.f,  12.f,  10.f,   8.f },
            -16.f, 0.62f
        },
        {
            "Singer-Songwriter (Bon Iver)",
            "Dynamic, vocal-centric, natural acoustic range, wide stereo",
            { -30.f, -26.f, -22.f, -20.f, -22.f, -26.f, -32.f },
            { 0.85f, 0.80f, 0.75f, 0.70f, 0.62f, 0.52f, 0.42f },
            // Natural dynamics, acoustic guitar picks and vocal consonants
            {  6.f,  12.f,  16.f,  18.f,  18.f,  16.f,  12.f },
            -22.f, 0.68f
        },
        {
            "Hip-Hop (Kendrick Lamar)",
            "Enormous mono sub, punchy lows, forward vocal mids, heavily compressed",
            { -16.f, -16.f, -22.f, -20.f, -24.f, -28.f, -34.f },
            { 0.95f, 0.88f, 0.65f, 0.60f, 0.50f, 0.38f, 0.28f },
            // 808 sub attacks, punchy snare; mid-range compressed for loudness
            { 10.f,  12.f,   8.f,  14.f,  16.f,  14.f,  10.f },
            -12.f, 0.55f
        },
        {
            "Rock (Foo Fighters)",
            "Guitars dominate lo-mid and hi-mid, punchy lows, limited sub",
            { -30.f, -22.f, -18.f, -18.f, -20.f, -24.f, -30.f },
            { 0.78f, 0.70f, 0.60f, 0.55f, 0.48f, 0.38f, 0.28f },
            // Punchy kick/snare in lows; guitar pick attacks in hi-mid/highs
            {  6.f,  14.f,  16.f,  18.f,  20.f,  18.f,  14.f },
            -18.f, 0.52f
        },
        {
            "Metal (Metallica)",
            "Guitars fill lo-mid through hi-mid, sub scooped, very loud and compressed",
            { -30.f, -20.f, -16.f, -16.f, -18.f, -22.f, -30.f },
            { 0.75f, 0.65f, 0.52f, 0.48f, 0.42f, 0.32f, 0.25f },
            // Double-kick punch in lows; sustained distorted guitars in mids
            {  6.f,  14.f,  14.f,  14.f,  18.f,  16.f,  12.f },
            -12.f, 0.48f
        },
        {
            "Jazz (Miles Davis)",
            "Very dynamic, warm natural balance, low average level, high mono compat",
            { -38.f, -30.f, -26.f, -24.f, -28.f, -32.f, -38.f },
            { 0.82f, 0.80f, 0.76f, 0.72f, 0.65f, 0.58f, 0.48f },
            // Very high dynamics: brushed snare, plucked bass, brass stabs
            {  8.f,  14.f,  18.f,  20.f,  22.f,  20.f,  16.f },
            -28.f, 0.72f
        },
        {
            "R&B (D'Angelo)",
            "Warm sub and lows, smooth mids, laid-back feel, moderately loud",
            { -20.f, -18.f, -22.f, -22.f, -26.f, -30.f, -36.f },
            { 0.88f, 0.82f, 0.68f, 0.62f, 0.52f, 0.42f, 0.32f },
            // Groovy but compressed; some transient punch in kick/snare
            {  8.f,  12.f,  10.f,  12.f,  14.f,  12.f,   8.f },
            -16.f, 0.60f
        },
        {
            "Country (Johnny Cash)",
            "Vocal-centric, acoustic warmth in lows/lo-mid, natural dynamics",
            { -34.f, -28.f, -24.f, -20.f, -22.f, -26.f, -34.f },
            { 0.85f, 0.82f, 0.76f, 0.70f, 0.63f, 0.53f, 0.43f },
            // Acoustic guitar strums and vocal consonants; moderate dynamics
            {  6.f,  12.f,  16.f,  18.f,  18.f,  16.f,  12.f },
            -22.f, 0.68f
        }
    };
}
