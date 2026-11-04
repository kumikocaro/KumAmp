#pragma once

#include "Widgets.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <memory>

namespace kumamp
{
/** SETTINGS tab: guitar -> MIDI output and MIDI control input. */
class SettingsView final : public juce::Component, private juce::Timer
{
public:
    explicit SettingsView (juce::AudioProcessorValueTreeState& parameters);
    ~SettingsView() override;

    /** Live activity source: sounding note (-1 none), velocity, bend in semitones. */
    std::function<int()> noteSource, velocitySource;
    std::function<float()> bendSource;
    /** MIDI control input: last message (status << 16 | data1 << 8 | data2, 0 = none) and count. */
    std::function<juce::uint32()> midiInSource, midiInCountSource;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

private:
    void timerCallback() override;

    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    juce::ToggleButton midiOut { "Enviar MIDI desde la guitarra" };
    juce::ComboBox channel, bendRange;
    juce::Slider threshold { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider transpose { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::ToggleButton pcScenes { "Program Change 1-4 (0-3) o CC 20-23  ->  Escenas A-D" };
    juce::ToggleButton ccMacros { "CC 11 / CC 4 (expresion) -> Macro 1 (Wah)    CC 1 -> Macro 2    CC 2 -> Macro 3" };

    std::unique_ptr<ButtonAttachment> midiOutAttachment, pcAttachment, ccAttachment;
    std::unique_ptr<ComboAttachment> channelAttachment, bendAttachment;
    std::unique_ptr<SliderAttachment> thresholdAttachment, transposeAttachment;

    juce::Rectangle<int> outCard, controlCard, activityArea, helpArea, midiInArea;
    juce::uint32 lastMidiIn = 0, midiInCount = 0;
    double lastMidiInTime = 0.0;
    int note = -1, velocity = 0;
    float bend = 0.0f;
};
} // namespace kumamp
