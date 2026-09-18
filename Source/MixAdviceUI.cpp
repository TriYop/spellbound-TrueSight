#include "MixAdviceUI.h"
#include "audioplugins/common/presets/PresetBrowser.h"   // for PresetEntry only

#include <algorithm>

START_NAMESPACE_DISTRHO

using audioplugins::common::hui::dgl::SpectrumMeter;
using audioplugins::common::hui::dgl::CorrelationGauge;
using audioplugins::common::hui::dgl::PresetSelector;
using audioplugins::common::hui::dgl::AdviceLabel;
using audioplugins::common::hui::dgl::AdviceCategory;
using audioplugins::common::presets::PresetEntry;

static constexpr uint kWindowWidth  = 760;
static constexpr uint kWindowHeight = 520;

// Carried over from the JUCE-era PluginEditor.cpp's kPercentileWarmupSec
// (Source/_juce_reference/PluginEditor.cpp:34) -- gates buildAnalysisSnapshot()'s
// use of the percentile (p50/p95) bands until this many seconds of playback
// have accumulated since the last resetPeaks().
static constexpr float kPercentileWarmupSec = 2.0f;

MixAdviceUI::MixAdviceUI()
    : UI(kWindowWidth, kWindowHeight)
    , fPluginPtr(static_cast<MixAdvicePluginAdapter*>(getPluginInstancePointer()))
{
    // Registers DPF's built-in NANOVG_DEJAVU_SANS_TTF font with this UI's
    // NanoVG context. PresetSelector::onNanoDisplay() calls
    // fontFace(NANOVG_DEJAVU_SANS_TTF) internally (see Common's
    // src/hui/dgl/PresetSelector.cpp) -- without this call the font lookup
    // fails silently and preset names never render. Same idiom as Hex's
    // HexUI::HexUI(), which calls this first for the same reason.
    loadSharedResources();

    fSpectrumMeter = std::make_unique<SpectrumMeter>(this);
    fSpectrumMeter->setAbsolutePos(20, 60);
    fSpectrumMeter->setSize(720, 240);

    for (int i = 0; i < BandConfig::numBands; ++i)
    {
        fCorrelationGauges[static_cast<size_t>(i)] = std::make_unique<CorrelationGauge>(this);
        fCorrelationGauges[static_cast<size_t>(i)]->setAbsolutePos(20 + i * 100, 320);
        fCorrelationGauges[static_cast<size_t>(i)]->setSize(90, 70);
    }

    fPresetSelector = std::make_unique<PresetSelector>(this);
    fPresetSelector->setAbsolutePos(20, 12);
    fPresetSelector->setClosedSize(300, 28);
    fPresetSelector->onIndexSelected = [this](int index)
    {
        fPluginPtr->setParameterValue(kParameterPresetIndex, static_cast<float>(index));
        setParameterValue(kParameterPresetIndex, static_cast<float>(index));   // notify host
    };

    fWarmupLabel = std::make_unique<AdviceLabel>(this);
    fWarmupLabel->setAbsolutePos(20, 400);
    fWarmupLabel->setSize(720, 28);
    fWarmupLabel->setCategory(AdviceCategory::WARNING);
    fWarmupLabel->setText("Play audio to compute mastering recommendations");

    fAdvicePanel = std::make_unique<MasteringAdvicePanel>(this);
    fAdvicePanel->setAbsolutePos(20, 440);
    fAdvicePanel->setSize(720, 70);

    refreshPresetSelector();
}

void MixAdviceUI::refreshPresetSelector()
{
    const auto& mgr = fPluginPtr->getPresetManager();
    std::vector<PresetEntry> entries;
    entries.reserve(static_cast<size_t>(mgr.getNumPresets()));
    for (int i = 0; i < mgr.getNumPresets(); ++i)
        entries.push_back({ mgr.getPreset(i).name, /*isFactory=*/true });   // no user Save/Delete -- see plan's Deviation 3
    fPresetSelector->setEntries(std::move(entries));
    fPresetSelector->setCurrentIndex(static_cast<int>(fPluginPtr->getParameterValue(kParameterPresetIndex)));
}

void MixAdviceUI::parameterChanged(const uint32_t index, const float value)
{
    if (index == kParameterPresetIndex)
        fPresetSelector->setCurrentIndex(static_cast<int>(value));
}

void MixAdviceUI::uiIdle()
{
    const auto snap = fPluginPtr->getAnalysisResult().read();

    std::array<float, BandConfig::numBands> levelsL{}, levelsR{}, refs{};
    const auto& preset = fPluginPtr->getPresetManager().getPreset(
        static_cast<int>(fPluginPtr->getParameterValue(kParameterPresetIndex)));

    for (int i = 0; i < BandConfig::numBands; ++i)
    {
        // Map dBFS (BandConfig::displayFloorDb..displayCeilDb) to 0..1 for the meter.
        const float floor = BandConfig::displayFloorDb, ceil = BandConfig::displayCeilDb;
        levelsL[static_cast<size_t>(i)] = std::clamp((snap.rmsDbL[static_cast<size_t>(i)] - floor) / (ceil - floor), 0.f, 1.f);
        levelsR[static_cast<size_t>(i)] = std::clamp((snap.rmsDbR[static_cast<size_t>(i)] - floor) / (ceil - floor), 0.f, 1.f);
        refs[static_cast<size_t>(i)]    = std::clamp((preset.bandRmsDb[static_cast<size_t>(i)] - floor) / (ceil - floor), 0.f, 1.f);

        fCorrelationGauges[static_cast<size_t>(i)]->setValue(
            (snap.correlation[static_cast<size_t>(i)] + 1.f) * 0.5f);   // correlation is -1..1, gauge wants 0..1
    }
    fSpectrumMeter->setLevels(levelsL.data(), levelsR.data());
    fSpectrumMeter->setReferences(refs.data());

    const float overallMax = (snap.peakOverallDbL + snap.peakOverallDbR) * 0.5f;
    fWarmupLabel->setVisible(overallMax <= -99.f);

    if (overallMax > -99.f)
    {
        const auto commonSnap = buildAnalysisSnapshot(snap, kPercentileWarmupSec);
        const auto advice = audioplugins::common::analysis::deriveAdvice(commonSnap, preset);
        auto resonances = buildResonancePeaks(snap);
        fAdvicePanel->setVisible(true);
        fAdvicePanel->update(advice, resonances, snap.lraLu);
    }
    else
    {
        fAdvicePanel->setVisible(false);
    }
}

void MixAdviceUI::onNanoDisplay() {}

UI* createUI() { return new MixAdviceUI(); }

END_NAMESPACE_DISTRHO
