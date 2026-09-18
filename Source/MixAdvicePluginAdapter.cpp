#include "MixAdvicePluginAdapter.h"
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
    parameter.ranges.max = 0.f;   // Task 6 sets this to (numPresets - 1) once PresetManager is wired in
}

float MixAdvicePluginAdapter::getParameterValue(const uint32_t index) const
{
    return index == kParameterPresetIndex ? presetIndex : 0.f;
}

void MixAdvicePluginAdapter::setParameterValue(const uint32_t index, const float value)
{
    if (index == kParameterPresetIndex) presetIndex = value;
}

void MixAdvicePluginAdapter::run(const float** inputs, float** outputs, uint32_t frames)
{
    if (outputs[0] != inputs[0]) std::memcpy(outputs[0], inputs[0], sizeof(float) * frames);
    if (outputs[1] != inputs[1]) std::memcpy(outputs[1], inputs[1], sizeof(float) * frames);
}

int MixAdvicePluginAdapter::getNumPresets() const noexcept { return 0; }
const char* MixAdvicePluginAdapter::getPresetName(int) const noexcept { return ""; }

Plugin* createPlugin() { return new MixAdvicePluginAdapter(); }

END_NAMESPACE_DISTRHO
