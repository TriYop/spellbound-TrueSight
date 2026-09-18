#pragma once
#include "DistrhoPlugin.hpp"

START_NAMESPACE_DISTRHO

enum Parameters { kParameterPresetIndex, kParameterCount };

class MixAdvicePluginAdapter : public Plugin
{
public:
    MixAdvicePluginAdapter();

protected:
    const char* getLabel() const override { return "MixAdvice"; }
    const char* getDescription() const override { return "Realtime mix analyzer and pre-mastering advisor"; }
    const char* getMaker() const override { return "Spellbound"; }
    const char* getLicense() const override { return "https://spellbound.audio/plugins/mixadvice#license"; }
    uint32_t getVersion() const override { return d_version(0, 2, 0); }

    void initParameter(uint32_t index, Parameter& parameter) override;
    float getParameterValue(uint32_t index) const override;
    void setParameterValue(uint32_t index, float value) override;

    void run(const float** inputs, float** outputs, uint32_t frames) override;

public:
    int getNumPresets() const noexcept;
    const char* getPresetName(int index) const noexcept;

private:
    float presetIndex = 0.f;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixAdvicePluginAdapter)
};

END_NAMESPACE_DISTRHO
