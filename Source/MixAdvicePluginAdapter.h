#pragma once
#include "DistrhoPlugin.hpp"
#include "Analysis/AnalyserEngine.h"
#include "Presets/PresetManager.h"
#include <atomic>

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

    void activate() override;
    void deactivate() override;
    void run(const float** inputs, float** outputs, uint32_t frames) override;

public:
    const AnalysisResult& getAnalysisResult() const noexcept { return analyser_.result; }
    const PresetManager& getPresetManager() const noexcept { return presetManager_; }
    bool isCurrentlyPlaying() const noexcept { return isPlaying_.load(std::memory_order_relaxed); }

    // Overrides Plugin's protected getParameterValue()/setParameterValue(),
    // re-declared public here (legal -- access control is per declaring
    // class, and a derived class may broaden it): MixAdviceUI needs to read/
    // write the preset-index parameter directly through the
    // DISTRHO_PLUGIN_WANT_DIRECT_ACCESS pointer (see Common's PresetSelector
    // onIndexSelected handler in MixAdviceUI.cpp), the same direct-access
    // idiom HexPluginAdapter's public getInputLevel()/getOutputLevel() use
    // for meters. Task 6's brief only documented the three accessors above as
    // its public surface; broadening these two is this task's fix for that
    // gap once MixAdviceUI.cpp's actual call sites needed it.
    float getParameterValue(uint32_t index) const override;
    void setParameterValue(uint32_t index, float value) override;

private:
    PresetManager presetManager_;
    int presetIndex_ = 0;
    bool wasPlaying_ = false;
    std::atomic<bool> isPlaying_ { false };
    AnalyserEngine analyser_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixAdvicePluginAdapter)
};

END_NAMESPACE_DISTRHO
