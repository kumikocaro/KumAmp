#pragma once

#include "PluginProcessor.h"
#include "UI/BlockPanel.h"
#include "UI/EditorSections.h"
#include "UI/GridView.h"
#include "UI/KumAmpLookAndFeel.h"
#include "UI/PresetBrowser.h"
#include "UI/Tone3000Picker.h"
#include "UI/SettingsView.h"
#include "UI/TunerView.h"

#include <juce_audio_processors/juce_audio_processors.h>

/**
    Main window:
      header (logo, tabs, stats)
      sidebar (tuner, input, gate, pitch) | scene bar + grid + block palette
      block panel (selected block + output meter)
    The big tuner and the not-yet-available tabs overlay the centre column.

    UI scale: every child lives in `content`, laid out at the logical size
    (editor size / scale) and drawn through an AffineTransform, so the whole
    interface can shrink for small screens (Windows laptops at 125-150 %
    display scaling) without a separate layout. The scale is chosen from the
    main menu or Ctrl/Cmd +/-/0, is remembered per user, and is reduced
    automatically on open when the window would not fit the screen.
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
    bool keyPressed (const juce::KeyPress&) override;

    float getUiScale() const noexcept { return uiScale; }
    /** remember = store as this user's preference (not for tests / automatic fitting). */
    void setUiScale (float newScale, bool remember = true);

    static constexpr int kDefaultWidth = 1500, kDefaultHeight = 880;   // logical size at 100 %
    static constexpr int kMinWidth = 1240, kMinHeight = 760;
    static constexpr int kMaxWidth = 2600, kMaxHeight = 1600;

private:
    struct Content final : public juce::Component
    {
        explicit Content (KumAmpAudioProcessorEditor& e) : owner (e) {}
        void paint (juce::Graphics& g) override { owner.paintContent (g); }
        void resized() override                 { owner.layoutContent(); }
        KumAmpAudioProcessorEditor& owner;
    };

    struct Placeholder final : public juce::Component
    {
        juce::String title;
        void paint (juce::Graphics&) override;
    };

    void timerCallback() override;
    void paintContent (juce::Graphics&);
    void layoutContent();
    void applyScaleLimits();
    static float loadSavedScale();
    static void saveScale (float scale);
    static float fittingScale (float wanted);
    void showTuner (bool shouldShow);
    void showEqOverlay (std::shared_ptr<kumamp::ParamStore> eqStore);
    void showTab (int tab);
    void showToast (const juce::String& message);
    void showMainMenu (juce::Component& source);
    void browseTone3000 (std::shared_ptr<kumamp::ParamStore> ampStore, juce::Component& source);
    void downloadTone3000Model (const kumamp::t3k::Tone& tone, const kumamp::t3k::Model& model);
    void downloadTone3000Variant (std::shared_ptr<kumamp::ParamStore> ampStore, const kumamp::t3k::Tone& tone,
                                  const kumamp::t3k::Model& variant);
    void loadPreset (const juce::File& file);
    void saveCurrentPreset();
    void setParameter (const juce::String& id, float value);
    int groupOfSelectedBlock() const;

    kumamp::KumAmpLookAndFeel lookAndFeel;   // must outlive every child
    KumAmpAudioProcessor& kumProcessor;
    kumamp::GridEngine& engine;

    Content content { *this };
    float uiScale = 1.0f;

    kumamp::HeaderBar header;
    kumamp::Sidebar sidebar;
    kumamp::PresetBar presetBar;
    kumamp::SceneBar sceneBar;
    kumamp::GridView grid;
    kumamp::BlockPalette palette;
    kumamp::BlockPanel panel;
    kumamp::TunerView tunerView;
    std::unique_ptr<kumamp::EqView> eqOverlay;   // large graphic EQ over the centre column
    Placeholder placeholder;
    kumamp::SettingsView settings;
    kumamp::PresetBrowser presetBrowser;
    kumamp::Tone3000Picker tone3000Picker;
    std::weak_ptr<kumamp::ParamStore> tone3000Target;   // amp block the picked model goes to
    juce::Array<kumamp::t3k::Model> tone3000Variants;   // all models of the tone being downloaded
    juce::Label toast;
    juce::TooltipWindow tooltips { this, 700 };

    juce::Rectangle<int> centreCard, centreColumn;
    int shownModelVersion = -1;
    juce::uint32 toastUntil = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KumAmpAudioProcessorEditor)
};
