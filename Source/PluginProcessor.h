#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Analysis/AnalyserEngine.h"

class MixAdviceAudioProcessorEditor;

class MixAdviceAudioProcessor final : public juce::AudioProcessor
{
public:
    MixAdviceAudioProcessor();
    ~MixAdviceAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    const AnalysisResult& getAnalysisResult() const noexcept { return analyser_.result; }
    bool isCurrentlyPlaying() const noexcept
    {
        return isPlaying_.load (std::memory_order_relaxed);
    }

private:
    int  currentPresetIndex { 0 };
    bool wasPlaying_        { false };
    std::atomic<bool> isPlaying_ { false };
    AnalyserEngine analyser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixAdviceAudioProcessor)
};
