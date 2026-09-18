#pragma once
#include "DistrhoPlugin.hpp"
#include "Analysis/AnalyserEngine.h"
#include "Presets/PresetManager.h"
#include <atomic>

START_NAMESPACE_DISTRHO

enum Parameters { kParameterPresetIndex, kParameterCount };

class TrueSightPluginAdapter : public Plugin
{
public:
    TrueSightPluginAdapter();

protected:
    const char* getLabel() const override { return "TrueSight"; }
    const char* getDescription() const override { return "Realtime mix analyzer and pre-mastering advisor"; }
    const char* getMaker() const override { return "Spellbound"; }
    const char* getLicense() const override { return "https://spellbound.audio/plugins/truesight#license"; }
    uint32_t getVersion() const override { return d_version(0, 2, 0); }

    void initParameter(uint32_t index, Parameter& parameter) override;

    void activate() override;
    void deactivate() override;
    void run(const float** inputs, float** outputs, uint32_t frames) override;

    // DPF's docs say these only fire while deactivated, but on LV2 specifically
    // lv2_set_options() calls setBufferSize(n, /*doCallback=*/true) at runtime
    // without a deactivate/activate cycle (DistrhoPluginLV2.cpp), and DPF's LV2
    // host-side prefers the host's nominalBlockLength over maxBlockLength when
    // both are offered. Without these overrides, analyser_'s scratch buffers
    // stay sized to whatever activate() saw, and a later run() with more frames
    // writes past them (heap-buffer-overflow, reproduced under ASAN). Re-prepare
    // on both callbacks so the DSP is always sized for the current buffer/rate,
    // regardless of which format or calling convention triggered the change.
    void bufferSizeChanged(uint32_t newBufferSize) override;
    void sampleRateChanged(double newSampleRate) override;

public:
    const AnalysisResult& getAnalysisResult() const noexcept { return analyser_.result; }
    const PresetManager& getPresetManager() const noexcept { return presetManager_; }
    bool isCurrentlyPlaying() const noexcept { return isPlaying_.load(std::memory_order_relaxed); }

    // Overrides Plugin's protected getParameterValue()/setParameterValue(),
    // re-declared public here (legal -- access control is per declaring
    // class, and a derived class may broaden it): TrueSightUI needs to read/
    // write the preset-index parameter directly through the
    // DISTRHO_PLUGIN_WANT_DIRECT_ACCESS pointer (see Common's PresetSelector
    // onIndexSelected handler in TrueSightUI.cpp), the same direct-access
    // idiom HexPluginAdapter's public getInputLevel()/getOutputLevel() use
    // for meters. Task 6's brief only documented the three accessors above as
    // its public surface; broadening these two is this task's fix for that
    // gap once TrueSightUI.cpp's actual call sites needed it.
    float getParameterValue(uint32_t index) const override;
    void setParameterValue(uint32_t index, float value) override;

private:
    PresetManager presetManager_;
    int presetIndex_ = 0;
    bool wasPlaying_ = false;
    std::atomic<bool> isPlaying_ { false };
    AnalyserEngine analyser_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrueSightPluginAdapter)
};

END_NAMESPACE_DISTRHO
