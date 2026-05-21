#pragma once
#include <array>
#include <atomic>
#include "BandConfig.h"

// Written by the audio thread, read by the UI thread.
// Uses per-value atomics — slight tearing between bands is acceptable for a display.
struct AnalysisResult
{
    static constexpr int numBands = BandConfig::numBands;

    std::array<std::atomic<float>, numBands> rmsDbL;
    std::array<std::atomic<float>, numBands> rmsDbR;

    // Peak-hold RMS (dBFS) — reset when transport starts
    std::array<std::atomic<float>, numBands> peakRmsDbL;
    std::array<std::atomic<float>, numBands> peakRmsDbR;

    // Broadband RMS across all bands (dBFS)
    std::atomic<float> overallRmsDbL  { -100.f };
    std::atomic<float> overallRmsDbR  { -100.f };
    std::atomic<float> peakOverallDbL { -100.f };
    std::atomic<float> peakOverallDbR { -100.f };

    // Broadband mono compatibility — real-time (300 ms smoothed) and integrated since last reset
    std::atomic<float> overallCorrelation    { 1.f };
    std::atomic<float> integratedCorrelation { 1.f };

    // Pearson correlation between L and R per band: 1=mono-compatible, -1=full cancellation
    std::array<std::atomic<float>, numBands> correlation;

    // Long-term (integrated since last reset) average RMS per band — used for stable advice.
    std::array<std::atomic<float>, numBands> avgRmsDbL;
    std::array<std::atomic<float>, numBands> avgRmsDbR;

    // Per-band crest factor (peak/RMS ratio) expressed in dB — proxy for transient energy.
    // 0 dB = silence/DC, ~3 dB = sine, 10-20 dB = punchy/transient.
    std::array<std::atomic<float>, numBands> transientDbL;
    std::array<std::atomic<float>, numBands> transientDbR;

    AnalysisResult()
    {
        for (auto& a : rmsDbL)       a.store (-100.f, std::memory_order_relaxed);
        for (auto& a : rmsDbR)       a.store (-100.f, std::memory_order_relaxed);
        for (auto& a : peakRmsDbL)   a.store (-100.f, std::memory_order_relaxed);
        for (auto& a : peakRmsDbR)   a.store (-100.f, std::memory_order_relaxed);
        for (auto& a : correlation)  a.store (1.f,    std::memory_order_relaxed);
        for (auto& a : transientDbL) a.store (0.f,    std::memory_order_relaxed);
        for (auto& a : transientDbR) a.store (0.f,    std::memory_order_relaxed);
        for (auto& a : avgRmsDbL)    a.store (-100.f, std::memory_order_relaxed);
        for (auto& a : avgRmsDbR)    a.store (-100.f, std::memory_order_relaxed);
    }

    AnalysisResult (const AnalysisResult&) = delete;
    AnalysisResult& operator= (const AnalysisResult&) = delete;

    struct Snapshot
    {
        std::array<float, numBands> rmsDbL      {};
        std::array<float, numBands> rmsDbR      {};
        std::array<float, numBands> peakRmsDbL  {};
        std::array<float, numBands> peakRmsDbR  {};
        std::array<float, numBands> correlation {};
        std::array<float, numBands> transientDbL {};
        std::array<float, numBands> transientDbR {};
        std::array<float, numBands> avgRmsDbL    {};
        std::array<float, numBands> avgRmsDbR    {};

        float overallRmsDbL       { -100.f };
        float overallRmsDbR       { -100.f };
        float peakOverallDbL      { -100.f };
        float peakOverallDbR      { -100.f };
        float overallCorrelation    { 1.f };
        float integratedCorrelation { 1.f };
    };

    Snapshot read() const noexcept
    {
        Snapshot s;
        for (size_t i = 0; i < static_cast<size_t> (numBands); ++i)
        {
            s.rmsDbL[i]      = rmsDbL[i].load      (std::memory_order_relaxed);
            s.rmsDbR[i]      = rmsDbR[i].load      (std::memory_order_relaxed);
            s.peakRmsDbL[i]   = peakRmsDbL[i].load   (std::memory_order_relaxed);
            s.peakRmsDbR[i]   = peakRmsDbR[i].load   (std::memory_order_relaxed);
            s.correlation[i]  = correlation[i].load  (std::memory_order_relaxed);
            s.transientDbL[i] = transientDbL[i].load (std::memory_order_relaxed);
            s.transientDbR[i] = transientDbR[i].load (std::memory_order_relaxed);
            s.avgRmsDbL[i]    = avgRmsDbL[i].load    (std::memory_order_relaxed);
            s.avgRmsDbR[i]    = avgRmsDbR[i].load    (std::memory_order_relaxed);
        }
        s.overallRmsDbL          = overallRmsDbL         .load (std::memory_order_relaxed);
        s.overallRmsDbR          = overallRmsDbR         .load (std::memory_order_relaxed);
        s.peakOverallDbL         = peakOverallDbL        .load (std::memory_order_relaxed);
        s.peakOverallDbR         = peakOverallDbR        .load (std::memory_order_relaxed);
        s.overallCorrelation     = overallCorrelation    .load (std::memory_order_relaxed);
        s.integratedCorrelation  = integratedCorrelation .load (std::memory_order_relaxed);
        return s;
    }
};
