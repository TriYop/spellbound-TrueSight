#pragma once
#include "DistrhoUI.hpp"
#include "MixAdvicePluginAdapter.h"
#include "Analysis/BandConfig.h"
#include "audioplugins/common/hui/dgl/SpectrumMeter.h"
#include "audioplugins/common/hui/dgl/CorrelationGauge.h"
#include "audioplugins/common/hui/dgl/PresetSelector.h"
#include "audioplugins/common/hui/dgl/AdviceLabel.h"
#include <array>
#include <memory>

START_NAMESPACE_DISTRHO

class MixAdviceUI : public UI
{
public:
    MixAdviceUI();

protected:
    void parameterChanged(uint32_t index, float value) override;
    void uiIdle() override;
    void onNanoDisplay() override;

private:
    void refreshPresetSelector();

    MixAdvicePluginAdapter* const fPluginPtr;

    std::unique_ptr<audioplugins::common::hui::dgl::SpectrumMeter> fSpectrumMeter;
    std::array<std::unique_ptr<audioplugins::common::hui::dgl::CorrelationGauge>, BandConfig::numBands> fCorrelationGauges;
    std::unique_ptr<audioplugins::common::hui::dgl::PresetSelector> fPresetSelector;
    std::unique_ptr<audioplugins::common::hui::dgl::AdviceLabel> fWarmupLabel;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixAdviceUI)
};

END_NAMESPACE_DISTRHO
