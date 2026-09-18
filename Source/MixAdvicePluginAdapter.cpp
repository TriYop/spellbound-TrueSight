#include "MixAdvicePluginAdapter.h"
#include <algorithm>
#include <cmath>
#include <cstring>

START_NAMESPACE_DISTRHO

MixAdvicePluginAdapter::MixAdvicePluginAdapter()
    : Plugin(kParameterCount, 0, 0)
{
}

void MixAdvicePluginAdapter::initParameter(const uint32_t index, Parameter& parameter)
{
    if (index != kParameterPresetIndex) return;
    parameter.hints  = kParameterIsAutomatable | kParameterIsInteger;
    parameter.name   = "Preset";
    parameter.symbol = "preset";
    parameter.ranges.def = 0.f;
    parameter.ranges.min = 0.f;
    // Floor of 1 (not 0) here is deliberate: if presetManager_.getNumPresets()
    // is ever 0 or 1 (e.g. all built-in preset XML fails to parse), max must
    // still differ from min or DPF's ParameterRanges::getNormalizedValue()
    // computes (value - min) / (max - min) == 0/0 == NaN, surfaced to
    // VST3/CLAP hosts. In production there are ~28 embedded presets
    // (Presets/*.xml), so this floor is a defensive guard, not the expected
    // path.
    parameter.ranges.max = static_cast<float>(std::max(1, presetManager_.getNumPresets() - 1));
}

float MixAdvicePluginAdapter::getParameterValue(const uint32_t index) const
{
    return index == kParameterPresetIndex ? static_cast<float>(presetIndex_) : 0.f;
}

void MixAdvicePluginAdapter::setParameterValue(const uint32_t index, const float value)
{
    if (index != kParameterPresetIndex) return;
    const int maxIndex = std::max(0, presetManager_.getNumPresets() - 1);
    presetIndex_ = std::clamp(static_cast<int>(value + 0.5f), 0, maxIndex);
}

void MixAdvicePluginAdapter::activate()
{
    analyser_.prepare(getSampleRate(), static_cast<int>(getBufferSize()), 2);
    analyser_.resetPeaks();
    wasPlaying_ = false;
}

void MixAdvicePluginAdapter::deactivate()
{
    analyser_.suspend();
}

void MixAdvicePluginAdapter::run(const float** inputs, float** outputs, uint32_t frames)
{
    if (outputs[0] != inputs[0]) std::memcpy(outputs[0], inputs[0], sizeof(float) * frames);
    if (outputs[1] != inputs[1]) std::memcpy(outputs[1], inputs[1], sizeof(float) * frames);

    // DPF has no JUCE-style getPlayHead()/PositionInfo abstraction exposed to
    // Plugin directly for transport play-state in every format uniformly;
    // gate on signal level exactly like TrueSight's pre-migration Standalone
    // path did (see Source/_juce_reference/PluginProcessor.cpp's "Standalone
    // (no playhead)" branch) -- simpler and format-uniform, at the cost of
    // losing the DAW-transport-exact gating the JUCE version had when a host
    // playhead was available. Revisit if DPF exposes transport state later.
    double sumSq = 0.0;
    for (uint32_t i = 0; i < frames; ++i)
        sumSq += static_cast<double>(outputs[0][i]) * outputs[0][i];
    const float rms = static_cast<float>(std::sqrt(sumSq / std::max<uint32_t>(1, frames)));
    const bool shouldProcess = rms > 1e-4f;

    isPlaying_.store(shouldProcess, std::memory_order_relaxed);
    if (shouldProcess && !wasPlaying_)
        analyser_.resetPeaks();
    wasPlaying_ = shouldProcess;

    if (shouldProcess)
        analyser_.process(outputs, 2, static_cast<int>(frames));
}

Plugin* createPlugin() { return new MixAdvicePluginAdapter(); }

END_NAMESPACE_DISTRHO
