#pragma once

#include "PluginProcessor.h"
#include "UI/BlockPanel.h"
#include "UI/GridView.h"

#include <juce_audio_processors/juce_audio_processors.h>

class KumAmpAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                         private juce::Timer
{
public:
    explicit KumAmpAudioProcessorEditor (KumAmpAudioProcessor&);
    ~KumAmpAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateSceneButtons (int activeScene);

    KumAmpAudioProcessor& kumProcessor;
    kumamp::GridEngine& engine;

    juce::Label title, status;
    std::array<juce::TextButton, kumamp::kNumScenes> sceneButtons;
    juce::TextButton storeButton { "Guardar escena" };
    juce::TextButton panicButton { "Reset DSP" };

    juce::Slider inputGain { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider outputGain { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label inputLabel { {}, "In" }, outputLabel { {}, "Out" };
    juce::AudioProcessorValueTreeState::SliderAttachment inputAttachment, outputAttachment;

    kumamp::GridView grid;
    kumamp::BlockPanel panel;
    int shownModelVersion = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KumAmpAudioProcessorEditor)
};
