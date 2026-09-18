#pragma once
#include "NanoVG.hpp"
#include "audioplugins/common/hui/Theme.h"
#include "audioplugins/common/analysis/AdviceSet.h"
#include "audioplugins/common/analysis/ResonancePeakPicker.h"
#include <vector>

// NanoVG port of Source/_juce_reference/PluginEditor.cpp:551's
// drawAdvicePanel() (per the Task 8 plan's Deviation 4) -- a simplified,
// non-pixel-perfect numeric data table (per-band EQ gain/Q, mixbus comp,
// loudness/limiter target, resonance-cut list). Visual polish is deferred to
// Task 11's manual pass.
class MasteringAdvicePanel : public DGL_NAMESPACE::NanoSubWidget
{
public:
    explicit MasteringAdvicePanel(DGL_NAMESPACE::NanoTopLevelWidget* parent);

    void update(const audioplugins::common::analysis::AdviceSet& advice,
                const std::vector<audioplugins::common::analysis::ResonancePeak>& resonances,
                float lraLu);

protected:
    void onNanoDisplay() override;

private:
    audioplugins::common::analysis::AdviceSet advice_;
    std::vector<audioplugins::common::analysis::ResonancePeak> resonances_;
    float lraLu_ = 0.f;

    DISTRHO_LEAK_DETECTOR(MasteringAdvicePanel)
};
