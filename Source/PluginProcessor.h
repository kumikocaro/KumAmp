#pragma once

#include "Engine/GridEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

/**
    KumAmp master processor.

    Host-facing parameters (APVTS): input/output gain, active scene (A-D) and
    four macros (expression pedals). Everything inside the pedalboard lives in
    the GridEngine and is saved as part of the plugin state.
*/
class KumAmpAudioProcessor final : public juce::AudioProcessor,
                                   private juce::Timer
{
public:
    KumAmpAudioProcessor();
    ~KumAmpAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    kumamp::GridEngine& getEngine() noexcept { return engine; }
    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters; }

    void selectScene (int sceneIndex);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void timerCallback() override;

    kumamp::GridEngine engine;
    juce::AudioProcessorValueTreeState parameters;

    std::atomic<float>* inputGainDb = nullptr;
    std::atomic<float>* outputGainDb = nullptr;
    std::atomic<float>* sceneIndex = nullptr;
    std::array<std::atomic<float>*, kumamp::kNumMacros> macroValues {};

    juce::SmoothedValue<float> inputGain, outputGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KumAmpAudioProcessor)
};
