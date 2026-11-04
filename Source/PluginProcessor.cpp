#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    const juce::String kInputGainId     { "input_gain" };
    const juce::String kOutputGainId    { "output_gain" };
    const juce::String kSceneId         { "scene" };
    const juce::String kTunerRefId      { "tuner_reference" };
    const juce::String kTunerMuteId     { "tuner_mute" };
    const juce::String kTunerModeId     { "tuner_mode" };
    const juce::String kGateOnId        { "gate_on" };
    const juce::String kGateThresholdId { "gate_threshold" };
    const juce::String kGateReleaseId   { "gate_release" };
    const juce::String kGateModeId      { "gate_mode" };
    const juce::String kPitchOnId       { "pitch_on" };
    const juce::String kPitchSemisId    { "pitch_semitones" };
    const juce::String kPitchInstrId    { "pitch_instrument" };
    const juce::String kMidiOutOnId     { "midi_out_on" };
    const juce::String kMidiChannelId   { "midi_out_channel" };
    const juce::String kMidiThresholdId { "midi_out_threshold" };
    const juce::String kMidiBendId      { "midi_out_bend" };
    const juce::String kMidiTransposeId { "midi_out_transpose" };
    const juce::String kMidiPcId        { "midi_pc_scenes" };
    const juce::String kMidiCcId        { "midi_cc_macros" };
    constexpr int kBendRanges[] = { 0, 2, 12, 24 };
    constexpr int kSceneCcFirst = 20;   // CC 20-23 -> scenes A-D
    const juce::Identifier kStateTag    { "KumAmpState" };

    juce::String macroId (int index) { return "macro_" + juce::String (index + 1); }

    void applySmoothedGain (juce::AudioBuffer<float>& buffer, int numChannels, juce::SmoothedValue<float>& gain)
    {
        const int numSamples = buffer.getNumSamples();
        if (! gain.isSmoothing())
        {
            const float g = gain.getTargetValue();
            if (! juce::exactlyEqual (g, 1.0f))
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

    /** Runs a stereo DSPBlock over a host block of any size (blocks accept <= maxBlock). */
    void processChunked (kumamp::DSPBlock& block, float* left, float* right, int numSamples, int maxBlock) noexcept
    {
        for (int offset = 0; offset < numSamples; offset += maxBlock)
            block.process (left + offset, right + offset, std::min (maxBlock, numSamples - offset));
    }
} // namespace

KumAmpAudioProcessor::KumAmpAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMS", createParameterLayout())
{
    inputGainDb    = parameters.getRawParameterValue (kInputGainId);
    outputGainDb   = parameters.getRawParameterValue (kOutputGainId);
    sceneIndex     = parameters.getRawParameterValue (kSceneId);
    for (int i = 0; i < kumamp::kNumMacros; ++i)
        macroValues[(size_t) i] = parameters.getRawParameterValue (macroId (i));
    tunerReference = parameters.getRawParameterValue (kTunerRefId);
    tunerMute      = parameters.getRawParameterValue (kTunerMuteId);
    gateOn         = parameters.getRawParameterValue (kGateOnId);
    gateThreshold  = parameters.getRawParameterValue (kGateThresholdId);
    gateRelease    = parameters.getRawParameterValue (kGateReleaseId);
    gateMode       = parameters.getRawParameterValue (kGateModeId);
    pitchOn        = parameters.getRawParameterValue (kPitchOnId);
    pitchSemitones = parameters.getRawParameterValue (kPitchSemisId);
    pitchInstrument = parameters.getRawParameterValue (kPitchInstrId);
    midiOutOn        = parameters.getRawParameterValue (kMidiOutOnId);
    midiOutChannel   = parameters.getRawParameterValue (kMidiChannelId);
    midiOutThreshold = parameters.getRawParameterValue (kMidiThresholdId);
    midiOutBendRange = parameters.getRawParameterValue (kMidiBendId);
    midiOutTranspose = parameters.getRawParameterValue (kMidiTransposeId);
    midiPcScenes     = parameters.getRawParameterValue (kMidiPcId);
    midiCcMacros     = parameters.getRawParameterValue (kMidiCcId);

    engine.loadDefaultPreset();
    startTimerHz (20);
}

KumAmpAudioProcessor::~KumAmpAudioProcessor()
{
    stopTimer();
}

juce::AudioProcessorValueTreeState::ParameterLayout KumAmpAudioProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    const NormalisableRange<float> gainRange { -24.0f, 24.0f, 0.1f };
    const auto dB = AudioParameterFloatAttributes().withLabel ("dB");

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { kInputGainId, 1 }, "Input Gain", gainRange, 0.0f, dB));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { kOutputGainId, 1 }, "Output Gain", gainRange, 0.0f, dB));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { kSceneId, 1 }, "Scene", StringArray { "A", "B", "C", "D" }, 0));

    for (int i = 0; i < kumamp::kNumMacros; ++i)
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { macroId (i), 1 }, "Macro " + String (i + 1),
                                                           NormalisableRange<float> { 0.0f, 1.0f }, 0.0f));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { kTunerRefId, 1 }, "Tuner A4",
                                                       NormalisableRange<float> { 430.0f, 450.0f, 0.1f }, 440.0f,
                                                       AudioParameterFloatAttributes().withLabel ("Hz")));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { kTunerMuteId, 1 }, "Tuner Mute", true));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { kTunerModeId, 1 }, "Tuner Mode",
                                                        StringArray { "Tradicional", "Trazo" }, 0));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { kGateOnId, 1 }, "Gate", true));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { kGateThresholdId, 1 }, "Gate Threshold",
                                                       NormalisableRange<float> { -90.0f, 0.0f, 0.5f }, -60.0f, dB));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { kGateReleaseId, 1 }, "Gate Release",
                                                       NormalisableRange<float> { 5.0f, 1000.0f, 1.0f, 0.4f }, 120.0f,
                                                       AudioParameterFloatAttributes().withLabel ("ms")));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { kGateModeId, 1 }, "Gate Mode",
                                                        StringArray { juce::CharPointer_UTF8 ("Cl\xc3\xa1sico"), "Avanzado" }, 0));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { kPitchOnId, 1 }, "Pitch", false));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { kPitchSemisId, 1 }, "Pitch Semitones",
                                                       NormalisableRange<float> { -12.0f, 12.0f, 1.0f }, 0.0f,
                                                       AudioParameterFloatAttributes().withLabel ("st")));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { kPitchInstrId, 1 }, "Pitch Instrument",
                                                        StringArray { "Guitarra", "Bajo" }, 0));

    // MIDI
    StringArray channels;
    for (int c = 1; c <= 16; ++c) channels.add (String (c));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { kMidiOutOnId, 1 }, "Guitar MIDI Out", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { kMidiChannelId, 1 }, "MIDI Out Channel", channels, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { kMidiThresholdId, 1 }, "MIDI Out Threshold",
                                                       NormalisableRange<float> { -70.0f, -15.0f, 0.5f }, -45.0f, dB));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { kMidiBendId, 1 }, "MIDI Out Bend Range",
                                                        StringArray { String ("Off"), String (CharPointer_UTF8 ("\xc2\xb1" "2")),
                                                                      String (CharPointer_UTF8 ("\xc2\xb1" "12")), String (CharPointer_UTF8 ("\xc2\xb1" "24")) }, 1));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { kMidiTransposeId, 1 }, "MIDI Out Transpose", -24, 24, 0));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { kMidiPcId, 1 }, "MIDI PC Scenes", true));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { kMidiCcId, 1 }, "MIDI CC Macros", true));
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
    maxBlock = std::max (1, samplesPerBlock);
    engine.prepare (sampleRate, maxBlock);
    tuner.prepare (sampleRate);
    inputGate.prepare (sampleRate, maxBlock);
    globalPitch.prepare (sampleRate, maxBlock);
    loadMeasurer.reset (sampleRate, maxBlock);
    midiTracker.prepare (sampleRate);
    midiMono.assign ((size_t) maxBlock, 0.0f);
    midiWasOn = false;

    inputGain.reset (sampleRate, 0.02);
    outputGain.reset (sampleRate, 0.02);
    inputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (inputGainDb->load()));
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputGainDb->load()));
    tunerMuteGain.reset (sampleRate, 0.03);
    tunerMuteGain.setCurrentAndTargetValue (1.0f);

    setLatencySamples (engine.getLatencySamples());
}

void KumAmpAudioProcessor::storePeak (std::atomic<float>& slot, const float* data, int numSamples) noexcept
{
    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        peak = std::max (peak, std::abs (data[i]));

    float previous = slot.load (std::memory_order_relaxed);
    while (peak > previous && ! slot.compare_exchange_weak (previous, peak, std::memory_order_relaxed)) {}
}

bool KumAmpAudioProcessor::isAudioRunning() const noexcept
{
    return juce::Time::getMillisecondCounter() - lastProcessTime.load (std::memory_order_relaxed) < 500;
}

void KumAmpAudioProcessor::handleMidiInput (const juce::MidiBuffer& midi, int& sceneFromMidi) noexcept
{
    const bool pcScenes = midiPcScenes->load (std::memory_order_relaxed) > 0.5f;
    const bool ccMacros = midiCcMacros->load (std::memory_order_relaxed) > 0.5f;

    for (const auto metadata : midi)
    {
        const auto m = metadata.getMessage();
        if (m.getRawDataSize() <= 3 && ! m.isMidiClock() && ! m.isActiveSense())
        {
            const auto* d = m.getRawData();
            const int size = m.getRawDataSize();
            lastMidiIn.store ((juce::uint32) d[0] << 16 | (juce::uint32) (size > 1 ? d[1] : 0) << 8 | (juce::uint32) (size > 2 ? d[2] : 0),
                              std::memory_order_relaxed);
            midiInCount.fetch_add (1, std::memory_order_relaxed);
        }

        // Program Change 1-4 (0-3 on the wire) selects scene A-D.
        // CC 20-23 (value > 0) does too, for controllers or hosts that only pass CCs.
        if (pcScenes && m.isProgramChange() && m.getProgramChangeNumber() < kumamp::kNumScenes)
            sceneFromMidi = m.getProgramChangeNumber();

        // VST3 has no Program Change events: the host sends them as the kCtrlProgramChange
        // controller, which JUCE's wrapper hands over as CC 127 with the program as its value.
        if (pcScenes && wrapperType == wrapperType_VST3 && m.isController() && m.getControllerNumber() == 127
            && m.getControllerValue() < kumamp::kNumScenes)
            sceneFromMidi = m.getControllerValue();
        if (pcScenes && m.isController() && m.getControllerNumber() >= kSceneCcFirst
            && m.getControllerNumber() < kSceneCcFirst + kumamp::kNumScenes && m.getControllerValue() > 0)
            sceneFromMidi = m.getControllerNumber() - kSceneCcFirst;

        // Expression (CC 11) and foot controller (CC 4) -> Macro 1; mod wheel CC 1 -> Macro 2; CC 2 -> Macro 3.
        if (ccMacros && m.isController())
        {
            const float v = (float) m.getControllerValue() / 127.0f;
            switch (m.getControllerNumber())
            {
                case 11: case 4: ccMacro[0] = v; break;
                case 1:          ccMacro[1] = v; break;
                case 2:          ccMacro[2] = v; break;
                default: break;
            }
        }
    }
}

void KumAmpAudioProcessor::generateGuitarMidi (const float* left, const float* right, int numSamples, juce::MidiBuffer& out) noexcept
{
    const bool on = midiOutOn->load (std::memory_order_relaxed) > 0.5f;
    const int channel = juce::jlimit (1, 16, (int) midiOutChannel->load (std::memory_order_relaxed) + 1);

    guitarMidi.beginBlock();
    if (! on || channel != midiChannel)
        guitarMidi.allNotesOff (0);   // disabled, or the channel changed while a note was sounding

    auto emit = [&] (int ch)
    {
        for (int k = 0; k < guitarMidi.getNumEvents(); ++k)
        {
            const auto& e = guitarMidi.getEvent (k);
            using T = kumamp::GuitarToMidi::Event::Type;
            if (e.type == T::NoteOn)       out.addEvent (juce::MidiMessage::noteOn (ch, e.note, (juce::uint8) e.velocity), e.sampleOffset);
            else if (e.type == T::NoteOff) out.addEvent (juce::MidiMessage::noteOff (ch, e.note), e.sampleOffset);
            else                           out.addEvent (juce::MidiMessage::pitchWheel (ch, e.bend), e.sampleOffset);
        }
    };
    emit (midiChannel);   // note-offs go out on the channel the note started on
    midiChannel = channel;

    if (! on)
    {
        midiWasOn = false;
        midiOutNote.store (-1, std::memory_order_relaxed);
        return;
    }
    if (! midiWasOn)
        midiTracker.reset();
    midiWasOn = true;

    guitarMidi.setThresholdDb (midiOutThreshold->load (std::memory_order_relaxed));
    guitarMidi.setBendRange (kBendRanges[juce::jlimit (0, 3, (int) midiOutBendRange->load (std::memory_order_relaxed))]);
    guitarMidi.setTranspose ((int) midiOutTranspose->load (std::memory_order_relaxed));

    for (int offset = 0; offset < numSamples; offset += maxBlock)
    {
        const int n = std::min (maxBlock, numSamples - offset);
        for (int i = 0; i < n; ++i)
            midiMono[(size_t) i] = 0.5f * (left[offset + i] + right[offset + i]);
        guitarMidi.beginBlock();
        midiTracker.process (midiMono.data(), n, [&] (int i, const kumamp::RealtimePitchTracker::Estimate& e)
        {
            guitarMidi.onEstimate (offset + i, e);
        });
        emit (channel);
    }

    midiOutNote.store (guitarMidi.getActiveNote(), std::memory_order_relaxed);
    midiOutBend.store (guitarMidi.getBendSemitones(), std::memory_order_relaxed);
    for (int k = 0; k < guitarMidi.getNumEvents(); ++k)
        if (guitarMidi.getEvent (k).type == kumamp::GuitarToMidi::Event::Type::NoteOn)
            midiOutVelocity.store (guitarMidi.getEvent (k).velocity, std::memory_order_relaxed);
}

void KumAmpAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const juce::AudioProcessLoadMeasurer::ScopedTimer loadTimer (loadMeasurer, numSamples);
    lastProcessTime.store (juce::Time::getMillisecondCounter(), std::memory_order_relaxed);
    lastBlockSize.store (numSamples, std::memory_order_relaxed);

    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();

    // MIDI in: control only (scenes / macros); the output carries our own notes.
    int sceneFromMidi = -1;
    handleMidiInput (midiMessages, sceneFromMidi);
    midiMessages.clear();

    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);

    inputGain.setTargetValue (juce::Decibels::decibelsToGain (inputGainDb->load (std::memory_order_relaxed)));
    applySmoothedGain (buffer, numIn, inputGain);

    // Everything after this point is stereo: a mono input feeds both sides.
    if (numIn == 1 && numOut >= 2)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);

    float* left = buffer.getWritePointer (0);
    float* right = buffer.getWritePointer (numOut > 1 ? 1 : 0);
    storePeak (inputPeaks[0], left, numSamples);
    storePeak (inputPeaks[1], right, numSamples);

    // Guitar -> MIDI from the clean input (before gate / pitch / grid).
    generateGuitarMidi (left, right, numSamples, midiMessages);

    // Tuner taps the input before the gate, so a closed gate never blinds it.
    tuner.setReferenceHz (tunerReference->load (std::memory_order_relaxed));
    if (tuner.isListening())
        tuner.pushAudio (buffer.getArrayOfReadPointers(), std::min (numIn, 2), numSamples);

    // Global input stage: gate, then transposition (both click-free on/off).
    inputGate.setBypassed (gateOn->load (std::memory_order_relaxed) < 0.5f);
    inputGate.setParam (kumamp::NoiseGateBlock::ThresholdDb, gateThreshold->load (std::memory_order_relaxed));
    inputGate.setParam (kumamp::NoiseGateBlock::ReleaseMs, gateRelease->load (std::memory_order_relaxed));
    inputGate.setParam (kumamp::NoiseGateBlock::Mode, gateMode->load (std::memory_order_relaxed));
    processChunked (inputGate, left, right, numSamples, maxBlock);

    globalPitch.setBypassed (pitchOn->load (std::memory_order_relaxed) < 0.5f);
    globalPitch.setParam (kumamp::PitchShifterBlock::Semitones, pitchSemitones->load (std::memory_order_relaxed));
    globalPitch.setParam (kumamp::PitchShifterBlock::Instrument, pitchInstrument->load (std::memory_order_relaxed));
    processChunked (globalPitch, left, right, numSamples, maxBlock);

    if (auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
            if (const auto bpm = position->getBpm())
                engine.setTempo (*bpm);

    // Scene: a MIDI Program Change wins until the host parameter catches up (set from the timer).
    const int paramScene = (int) sceneIndex->load (std::memory_order_relaxed);
    if (paramScene != lastParamScene)
        midiSceneOverride = -1;   // the user / host picked a scene: it takes over
    lastParamScene = paramScene;
    if (sceneFromMidi >= 0)
    {
        midiSceneOverride = sceneFromMidi;
        pendingSceneFromMidi.store (sceneFromMidi, std::memory_order_relaxed);
    }
    engine.setRequestedScene (midiSceneOverride >= 0 ? midiSceneOverride : paramScene);

    // Macros: a MIDI CC wins until the host parameter moves again.
    for (size_t i = 0; i < (size_t) kumamp::kNumMacros; ++i)
    {
        const float paramValue = macroValues[i]->load (std::memory_order_relaxed);
        if (! juce::exactlyEqual (paramValue, lastMacroParam[i]))
            ccMacro[i] = -1.0f;
        lastMacroParam[i] = paramValue;
        engine.setMacroValue ((int) i, ccMacro[i] >= 0.0f ? ccMacro[i] : paramValue);
    }

    engine.process (buffer.getArrayOfReadPointers(), std::min (numOut, 2), buffer.getArrayOfWritePointers(), numOut, numSamples);

    outputGain.setTargetValue (juce::Decibels::decibelsToGain (outputGainDb->load (std::memory_order_relaxed)));
    applySmoothedGain (buffer, numOut, outputGain);

    const bool muted = tunerViewOpen.load (std::memory_order_relaxed) && tunerMute->load (std::memory_order_relaxed) > 0.5f;
    tunerMuteGain.setTargetValue (muted ? 0.0f : 1.0f);
    applySmoothedGain (buffer, numOut, tunerMuteGain);

    storePeak (outputPeaks[0], buffer.getReadPointer (0), numSamples);
    storePeak (outputPeaks[1], buffer.getReadPointer (numOut > 1 ? 1 : 0), numSamples);
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

    // Reflect a MIDI Program Change in the host-visible scene parameter.
    if (const int scene = pendingSceneFromMidi.exchange (-1); scene >= 0)
        selectScene (scene);

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
