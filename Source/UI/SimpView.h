#pragma once

#include "Widgets.h"
#include "../Engine/GridEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <memory>
#include <vector>

namespace kumamp
{
/** What a block is for, in plain words, and its one control in the SIMP view. */
struct SimpInfo
{
    const char* description;   // UTF-8
    const char* paramId;       // nullptr: nothing that one knob can say
    const char* paramLabel;    // UTF-8
    const char* optionId = nullptr;     // an on/off of its own, nullptr: none
    const char* optionLabel = nullptr;
};
SimpInfo simpInfoFor (BlockType type);

/**
    SIMP: the same pedalboard, explained. Every block of the grid is a numbered card that
    says what it does, with an on/off and a single knob. They are the grid's own values:
    changing one here changes it there. Input and output stand at both ends of the chain.
*/
class SimpView final : public juce::Component, private juce::Timer
{
public:
    SimpView (GridEngine& engine, juce::AudioProcessorValueTreeState& parameters);
    ~SimpView() override;

    /** An icon was clicked: index into factorypresets' SIMP presets. */
    std::function<void (int)> onPresetChosen;
    std::function<juce::String()> presetNameSource;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

    int getNumCards() const noexcept { return (int) cards.size(); }
    /** The icon lit for the current preset, -1 if it is some other one. */
    int getActivePreset() const;
    void choosePreset (int preset);
    /** Refreshes the cards from the engine, as the timer does. */
    void refresh();
    ParamStore* getCardStore (int card) const;
    void setCardValue (int card, float value);
    void toggleCard (int card);
    /** The card's own on/off (the sitar's tampura). */
    void toggleCardOption (int card);
    float getCardKnobValue (int card) const;

private:
    class Card;
    class PresetIcon;

    void timerCallback() override;
    void rebuild();
    void layoutCards();

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    GridEngine& engine;
    juce::Slider inputKnob, outputKnob;
    std::unique_ptr<SliderAttachment> inputAttachment, outputAttachment;
    std::vector<std::unique_ptr<PresetIcon>> icons;
    juce::Viewport viewport;
    juce::Component cardArea;
    std::vector<std::unique_ptr<Card>> cards;
    juce::Rectangle<int> inputColumn, outputColumn;
    int shownActive = -2;
    int shownVersion = -1;
};
} // namespace kumamp
