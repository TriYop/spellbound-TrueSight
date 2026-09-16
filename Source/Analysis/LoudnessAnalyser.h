#pragma once
#include "audioplugins/common/analysis/LoudnessAnalyser.h"

// Thin JUCE-facing wrapper over AudioPluginsCommon's causal LRA implementation
// (ported from this exact class — see Common/CLAUDE.md's common/analysis entry).
class LoudnessAnalyser
{
public:
    void prepare (double sampleRate) noexcept { impl_.prepare (sampleRate, 2); }
    void reset() noexcept { impl_.reset(); }
    void processBlock (const float* L, const float* R, int numSamples) noexcept
    {
        impl_.processBlock (L, R, numSamples);
    }
    float getLraLu() const noexcept { return impl_.getLraLu(); }

private:
    audioplugins::common::analysis::LoudnessAnalyser impl_;
};
