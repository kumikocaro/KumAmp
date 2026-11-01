#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    const juce::String kInputGainId  { "input_gain" };
    const juce::String kOutputGainId { "output_gain" };
    const juce::String kSceneId      { "scene" };
    const juce::Identifier kStateTag { "KumAmpState" };

    juce::String macroId (int index) { return "macro_" + juce::String (index + 1); }

    void applySmoothedGain (juce::AudioBuffer<float>& buffer, int numChannels, juce::SmoothedValue<float>& gain)
    {
        const int numSamples = buffer.getNumSamples();
        if (! gain.isSmoothing())
        {
            const float g = gain.getTargetValue();
            if (g != 1.0f)
                for (int ch = 0; ch < numChannels; ++ch)
                    buffer.applyGain (ch, 0, numSamples, g);
            return;
        }

        auto* const* data = buffer.getArrayOfWritePointers();
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = gain.getNextValue();
            for (int ch = 0; ch < numChannels; ++ch)
                data[ch][i] *= g;
        }
    }
} // namespace

KumAmpAudioProcessor::KumAmpAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMS", createParameterLayout())
{
    inputGainDb  = parameters.getRawParameterValue (kInputGainId);
    outputGainDb = parameters.getRawParameterValue (kOutputGainId);
    sceneIndex   = parameters.getRawParameterValue (kSceneId);
    for (int i = 0; i < kumamp::kNumMacros; ++i)
        macroValues[(size_t) i] = parameters.getRawParameterValue (macroId (i));

    engine.loadDefaultPreset();
    startTimerHz (20);
}

KumAmpAudioProcessor::~KumAmpAudioProcessor()
{
    stopTimer();
}

juce::AudioProcessorValueTreeState::ParameterLayout KumAmpAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const juce::NormalisableRange<float> gainRange { -24.0f, 24.0f, 0.1f };

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kInputGainId, 1 }, "Input Gain", gainRange, 0.0f,
                                                             juce::AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kOutputGainId, 1 }, "Output Gain", gainRange, 0.0f,
                                                             juce::AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { kSceneId, 1 }, "Scene",
                                                              juce::StringArray { "A", "B", "C", "D" }, 0));

    for (int i = 0; i < kumamp::kNumMacros; ++i)
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { macroId (i), 1 },
                                                                 "Macro " + juce::String (i + 1),
                                                                 juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f));
    return layout;
}

bool KumAmpAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    return out == juce::AudioChannelSet::stereo()
        && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void KumAmpAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);

    inputGain.reset (sampleRate, 0.02);
    outputGain.reset (sampleRate, 0.02);
    inputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (inputGainDb->load()));
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputGainDb->load()));

    setLatencySamples (engine.getLatencySamples());
}

void KumAmpAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);

    inputGain.setTargetValue (juce::Decibels::decibelsToGain (inputGainDb->load (std::memory_order_relaxed)));
    applySmoothedGain (buffer, numIn, inputGain);

    engine.setRequestedScene ((int) sceneIndex->load (std::memory_order_relaxed));
    for (int i = 0; i < kumamp::kNumMacros; ++i)
        engine.setMacroValue (i, macroValues[(size_t) i]->load (std::memory_order_relaxed));

    engine.process (buffer.getArrayOfReadPointers(), numIn, buffer.getArrayOfWritePointers(), numOut, numSamples);

    outputGain.setTargetValue (juce::Decibels::decibelsToGain (outputGainDb->load (std::memory_order_relaxed)));
    applySmoothedGain (buffer, numOut, outputGain);
}

void KumAmpAudioProcessor::selectScene (int index)
{
    if (auto* param = parameters.getParameter (kSceneId))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 ((float) index));
        param->endChangeGesture();
    }
}

void KumAmpAudioProcessor::timerCallback()
{
    engine.runMessageThreadTasks();

    if (const int latency = engine.getLatencySamples(); latency != getLatencySamples())
        setLatencySamples (latency);
}

juce::AudioProcessorEditor* KumAmpAudioProcessor::createEditor()
{
    return new KumAmpAudioProcessorEditor (*this);
}

void KumAmpAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state (kStateTag);
    state.appendChild (parameters.copyState(), nullptr);
    state.appendChild (engine.saveState(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void KumAmpAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    const auto state = juce::ValueTree::fromXml (*xml);
    if (! state.hasType (kStateTag))
        return;

    if (const auto params = state.getChildWithName (parameters.state.getType()); params.isValid())
        parameters.replaceState (params);

    engine.loadState (state.getChildWithName ("KumAmpGrid"));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KumAmpAudioProcessor();
}
