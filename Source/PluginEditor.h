#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "Analysis/AnalysisResult.h"
#include "Presets/PresetData.h"   // for PresetData& in draw signatures

class MixAdviceAudioProcessorEditor final
    : public juce::AudioProcessorEditor,
      private juce::Timer
{
public:
    explicit MixAdviceAudioProcessorEditor (MixAdviceAudioProcessor&);
    ~MixAdviceAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    void drawBandBars    (juce::Graphics&, juce::Rectangle<int> area,
                          const AnalysisResult::Snapshot&,
                          const PresetData&) const;
    void drawCorrStrip      (juce::Graphics&, juce::Rectangle<int> area,
                             const AnalysisResult::Snapshot&,
                             const PresetData&) const;
    void drawTransientStrip (juce::Graphics&, juce::Rectangle<int> area,
                             const AnalysisResult::Snapshot&,
                             const PresetData&) const;
    void drawAdvicePanel    (juce::Graphics&, juce::Rectangle<int> area,
                             const AnalysisResult::Snapshot&,
                             const PresetData&) const;
    void drawResonancePanel (juce::Graphics&, juce::Rectangle<int> area,
                             const AnalysisResult::Snapshot&) const;

    void exportAdvice();
    static juce::String generateMarkdown (const AnalysisResult::Snapshot&,
                                          const PresetData&);
    void drawDbScale     (juce::Graphics&, juce::Rectangle<int> area) const;
    void drawOverallMeter (juce::Graphics&, juce::Rectangle<int> area,
                           const AnalysisResult::Snapshot&,
                           const PresetData&) const;

    static juce::Colour corrColour (float corr) noexcept;

    MixAdviceAudioProcessor& processorRef;

    juce::ComboBox  presetSelector_;
    int             lastPresetIndex_ { -1 };

    juce::TextButton                 exportButton_ { "Export" };
    std::unique_ptr<juce::FileChooser> fileChooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixAdviceAudioProcessorEditor)
};
