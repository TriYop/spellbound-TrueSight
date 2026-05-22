#include "PluginProcessor.h"
#include "PluginEditor.h"

MixAdviceAudioProcessor::MixAdviceAudioProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{}

MixAdviceAudioProcessor::~MixAdviceAudioProcessor() = default;

const juce::String MixAdviceAudioProcessor::getName() const { return JucePlugin_Name; }
bool MixAdviceAudioProcessor::acceptsMidi()  const { return false; }
bool MixAdviceAudioProcessor::producesMidi() const { return false; }
bool MixAdviceAudioProcessor::isMidiEffect() const { return false; }
double MixAdviceAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int MixAdviceAudioProcessor::getNumPrograms()    { return presetManager_.getNumPresets(); }
int MixAdviceAudioProcessor::getCurrentProgram() { return currentPresetIndex; }

void MixAdviceAudioProcessor::setCurrentProgram (int index)
{
    currentPresetIndex = juce::jlimit (0, presetManager_.getNumPresets() - 1, index);
}

const juce::String MixAdviceAudioProcessor::getProgramName (int index)
{
    if (index >= 0 && index < presetManager_.getNumPresets())
        return presetManager_.getPreset (index).name;
    return {};
}

void MixAdviceAudioProcessor::changeProgramName (int, const juce::String&) {}

void MixAdviceAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = static_cast<uint32_t> (samplesPerBlock);
    spec.numChannels      = 2;
    analyser_.prepare (spec);
    analyser_.resetPeaks();
    wasPlaying_ = false;
}

void MixAdviceAudioProcessor::releaseResources() {}

bool MixAdviceAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    if (layouts.getMainInputChannelSet()  != juce::AudioChannelSet::stereo()) return false;
    return true;
}

void MixAdviceAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                             juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    bool shouldProcess = false;

    if (auto* ph = getPlayHead())
    {
        // DAW context: follow the transport state exactly.
        if (const auto pos = ph->getPosition())
        {
            const bool nowPlaying = pos->getIsPlaying();
            isPlaying_.store (nowPlaying, std::memory_order_relaxed);
            if (nowPlaying && !wasPlaying_)
                analyser_.resetPeaks();
            wasPlaying_ = nowPlaying;
            shouldProcess = nowPlaying;
        }
        else
        {
            // Playhead exists but position unavailable — err on the side of measuring.
            shouldProcess = true;
        }
    }
    else
    {
        // Standalone (no playhead): gate on signal level so measurements freeze
        // when audio stops rather than decaying toward silence.
        const float rms = buffer.getRMSLevel (0, 0, buffer.getNumSamples());
        shouldProcess = rms > 1e-4f;   // –80 dBFS gate
    }

    if (shouldProcess)
        analyser_.process (buffer);
    // Pure analyser: audio always passes through unmodified.
}

bool MixAdviceAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* MixAdviceAudioProcessor::createEditor()
{
    return new MixAdviceAudioProcessorEditor (*this);
}

void MixAdviceAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement state ("MixAdviceState");
    state.setAttribute ("presetName", presetManager_.getPreset (currentPresetIndex).name);
    copyXmlToBinary (state, destData);
}

void MixAdviceAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto state = getXmlFromBinary (data, sizeInBytes))
    {
        const auto name  = state->getStringAttribute ("presetName");
        const int  index = presetManager_.findByName (name);
        currentPresetIndex = (index >= 0) ? index : 0;
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MixAdviceAudioProcessor();
}
