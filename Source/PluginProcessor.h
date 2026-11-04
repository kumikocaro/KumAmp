#pragma once

#include "DSP/Dynamics/DynamicsBlocks.h"
#include "DSP/Midi/GuitarToMidi.h"
#include "DSP/Pitch/PitchShifterBlock.h"
#include "Engine/GridEngine.h"
#include "Presets/PresetManager.h"
#include "Tone3000/Tone3000.h"
#include "Tuner/Tuner.h"

#include <juce_audio_processors/juce_audio_processors.h>

/**
    KumAmp master processor.

    Signal flow:  input gain -> [tuner tap] -> global gate -> global pitch
                  -> grid (GridEngine) -> output gain -> [tuner mute] -> out

    Host-facing parameters (APVTS): input/output gain, active scene (A-D),
    four macros (expression pedals), global gate and pitch, tuner settings.
    Everything inside the pedalboard lives in the GridEngine and is saved as
    part of the plugin state.
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
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return true; }
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
    kumamp::Tuner& getTuner() noexcept { return tuner; }
    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters; }
    kumamp::PresetManager& getPresets() noexcept { return presets; }
    kumamp::t3k::Session& getTone3000() noexcept { return tone3000; }

    void selectScene (int sceneIndex);

    //== Editor telemetry (any thread) ===========================================
    /** Peak since the previous call (the editor polls and decays). */
    float takeInputPeak (int channel) noexcept  { return inputPeaks[(size_t) (channel & 1)].exchange (0.0f); }
    float takeOutputPeak (int channel) noexcept { return outputPeaks[(size_t) (channel & 1)].exchange (0.0f); }
    double getCpuLoad() const noexcept           { return loadMeasurer.getLoadAsProportion(); }
    int getCurrentBlockSize() const noexcept     { return lastBlockSize.load (std::memory_order_relaxed); }
    bool isAudioRunning() const noexcept;

    /** Guitar -> MIDI activity for the UI: sounding note (-1 = none), velocity, bend. */
    int getMidiOutNote() const noexcept      { return midiOutNote.load (std::memory_order_relaxed); }
    int getMidiOutVelocity() const noexcept  { return midiOutVelocity.load (std::memory_order_relaxed); }
    float getMidiOutBend() const noexcept    { return midiOutBend.load (std::memory_order_relaxed); }

    /** MIDI control input for the UI: the last message received (status, data1, data2 packed,
        0 = none yet) and how many have arrived. */
    juce::uint32 getLastMidiIn() const noexcept   { return lastMidiIn.load (std::memory_order_relaxed); }
    juce::uint32 getMidiInCount() const noexcept  { return midiInCount.load (std::memory_order_relaxed); }

    /** While the big tuner view is open the output can be muted ("tuner_mute"). */
    void setTunerViewOpen (bool open) noexcept { tunerViewOpen.store (open, std::memory_order_relaxed); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void timerCallback() override;
    static void storePeak (std::atomic<float>& slot, const float* data, int numSamples) noexcept;
    void handleMidiInput (const juce::MidiBuffer& midi, int& sceneFromMidi) noexcept;
    void generateGuitarMidi (const float* left, const float* right, int numSamples, juce::MidiBuffer& out) noexcept;

    kumamp::GridEngine engine;
    kumamp::Tuner tuner;
    kumamp::NoiseGateBlock inputGate { 1 };
    kumamp::PitchShifterBlock globalPitch { 2 };
    juce::AudioProcessorValueTreeState parameters;
    kumamp::PresetManager presets { engine, parameters };
    kumamp::t3k::Session tone3000;   // outlives the editor: a browser sign-in can finish after it closes

    std::atomic<float>* inputGainDb = nullptr;
    std::atomic<float>* outputGainDb = nullptr;
    std::atomic<float>* sceneIndex = nullptr;
    std::array<std::atomic<float>*, kumamp::kNumMacros> macroValues {};
    std::atomic<float>* tunerReference = nullptr;
    std::atomic<float>* tunerMute = nullptr;
    std::atomic<float>* gateOn = nullptr;
    std::atomic<float>* gateThreshold = nullptr;
    std::atomic<float>* gateRelease = nullptr;
    std::atomic<float>* gateMode = nullptr;
    std::atomic<float>* pitchOn = nullptr;
    std::atomic<float>* pitchSemitones = nullptr;
    std::atomic<float>* pitchInstrument = nullptr;
    std::atomic<float>* midiOutOn = nullptr;
    std::atomic<float>* midiOutChannel = nullptr;
    std::atomic<float>* midiOutThreshold = nullptr;
    std::atomic<float>* midiOutBendRange = nullptr;
    std::atomic<float>* midiOutTranspose = nullptr;
    std::atomic<float>* midiPcScenes = nullptr;
    std::atomic<float>* midiCcMacros = nullptr;

    // Guitar -> MIDI (audio thread)
    kumamp::RealtimePitchTracker midiTracker;
    kumamp::GuitarToMidi guitarMidi;
    std::vector<float> midiMono;
    bool midiWasOn = false;
    int midiChannel = 1;
    std::atomic<int> midiOutNote { -1 }, midiOutVelocity { 0 };
    std::atomic<float> midiOutBend { 0.0f };
    std::atomic<juce::uint32> lastMidiIn { 0 }, midiInCount { 0 };

    // MIDI control input (audio thread) -> parameters (message thread)
    int midiSceneOverride = -1, lastParamScene = -1;
    std::atomic<int> pendingSceneFromMidi { -1 };
    std::array<float, kumamp::kNumMacros> ccMacro { -1.0f, -1.0f, -1.0f, -1.0f }, lastMacroParam {};

    juce::SmoothedValue<float> inputGain, outputGain, tunerMuteGain;
    int maxBlock = 512;

    juce::AudioProcessLoadMeasurer loadMeasurer;
    std::array<std::atomic<float>, 2> inputPeaks {}, outputPeaks {};
    std::atomic<int> lastBlockSize { 0 };
    std::atomic<juce::uint32> lastProcessTime { 0 };
    std::atomic<bool> tunerViewOpen { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KumAmpAudioProcessor)
};
