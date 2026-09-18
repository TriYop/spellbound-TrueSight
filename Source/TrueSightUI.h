#pragma once
#include "DistrhoUI.hpp"
#include "TrueSightPluginAdapter.h"
#include "Analysis/BandConfig.h"
#include "Analysis/AdviceAdapter.h"
#include "UI/MasteringAdvicePanel.h"
#include "audioplugins/common/hui/dgl/SpectrumMeter.h"
#include "audioplugins/common/hui/dgl/CorrelationGauge.h"
#include "audioplugins/common/hui/dgl/PresetSelector.h"
#include "audioplugins/common/hui/dgl/AdviceLabel.h"
#include <array>
#include <memory>

START_NAMESPACE_DISTRHO

class TrueSightUI : public UI
{
public:
    TrueSightUI();

protected:
    void parameterChanged(uint32_t index, float value) override;
    void uiIdle() override;
    void onNanoDisplay() override;

private:
    void refreshPresetSelector();

    TrueSightPluginAdapter* const fPluginPtr;

    std::unique_ptr<audioplugins::common::hui::dgl::SpectrumMeter> fSpectrumMeter;
    std::array<std::unique_ptr<audioplugins::common::hui::dgl::CorrelationGauge>, BandConfig::numBands> fCorrelationGauges;
    std::unique_ptr<audioplugins::common::hui::dgl::PresetSelector> fPresetSelector;
    std::unique_ptr<audioplugins::common::hui::dgl::AdviceLabel> fWarmupLabel;
    std::unique_ptr<MasteringAdvicePanel> fAdvicePanel;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrueSightUI)
};

END_NAMESPACE_DISTRHO
