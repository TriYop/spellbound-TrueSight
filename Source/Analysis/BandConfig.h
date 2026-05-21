#pragma once
#include <array>

namespace BandConfig
{
    static constexpr int numBands = 7;

    // Crossover frequencies separating the 7 bands
    static constexpr std::array<float, numBands - 1> crossoverHz = {
        80.f, 250.f, 500.f, 2000.f, 6000.f, 16000.f
    };

    static constexpr std::array<const char*, numBands> bandNames = {
        "Sub", "Lows", "Lo-Mid", "Mids", "Hi-Mid", "Highs", "Air"
    };

    // Characteristic EQ frequency per band (Hz) — used for mastering EQ advice.
    // Sub and Air are better treated as shelves; all others as bell filters.
    static constexpr std::array<float, numBands> bandCenterHz = {
        50.f, 160.f, 375.f, 1000.f, 3500.f, 10000.f, 16000.f
    };

    // true = suggest low/high shelf rather than a bell filter
    static constexpr std::array<bool, numBands> bandIsShelf = {
        true, false, false, false, false, false, true
    };

    // dBFS display range
    static constexpr float displayFloorDb = -60.f;
    static constexpr float displayCeilDb  =   0.f;
}
