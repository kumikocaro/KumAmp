#pragma once

#include "PluginProcessor.h"
#include "UI/BlockPanel.h"
#include "UI/EditorSections.h"
#include "UI/GridView.h"
#include "UI/KumAmpLookAndFeel.h"
#include "UI/SettingsView.h"
#include "UI/TunerView.h"

#include <juce_audio_processors/juce_audio_processors.h>

/**
    Main window:
      header (logo, tabs, stats)
      sidebar (tuner, input, gate, pitch) | scene bar + grid + block palette
      block panel (selected block + output meter)
    The big tuner and the not-yet-available tabs overlay the centre column.
*/
class KumAmpAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                         public juce::DragAndDropContainer,
                                         private juce::Timer
{
public:
    explicit KumAmpAudioProcessorEditor (KumAmpAudioProcessor&);
    ~KumAmpAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Placeholder final : public juce::Component
    {
        juce::String title;
        void paint (juce::Graphics&) override;
    };

    void timerCallback() override;
    void showTuner (bool shouldShow);
    void showEqOverlay (std::shared_ptr<kumamp::ParamStore> eqStore);
    void showTab (int tab);
    void showToast (const juce::String& message);
    void showMainMenu (juce::Component& source);
    void setParameter (const juce::String& id, float value);
    int groupOfSelectedBlock() const;

    kumamp::KumAmpLookAndFeel lookAndFeel;   // must outlive every child
    KumAmpAudioProcessor& kumProcessor;
    kumamp::GridEngine& engine;

    kumamp::HeaderBar header;
    kumamp::Sidebar sidebar;
    kumamp::SceneBar sceneBar;
    kumamp::GridView grid;
    kumamp::BlockPalette palette;
    kumamp::BlockPanel panel;
    kumamp::TunerView tunerView;
    std::unique_ptr<kumamp::EqView> eqOverlay;   // large graphic EQ over the centre column
    Placeholder placeholder;
    kumamp::SettingsView settings;
    juce::Label toast;
    juce::TooltipWindow tooltips { this, 700 };

    juce::Rectangle<int> centreCard, centreColumn;
    int shownModelVersion = -1;
    juce::uint32 toastUntil = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KumAmpAudioProcessorEditor)
};
