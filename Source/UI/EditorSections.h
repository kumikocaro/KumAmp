#pragma once

#include "Widgets.h"
#include "../Tuner/Tuner.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <memory>

namespace kumamp
{
//==============================================================================
/** Logo, main tabs and live engine stats (CPU, rate, buffer, audio status). */
class HeaderBar final : public juce::Component
{
public:
    HeaderBar();

    std::function<void (int)> onTabChanged;
    std::function<void()> onFieldClicked, onSimpClicked, onAboutClicked;
    void setActiveTab (int tab);
    /** The buttons that go to the CAMPO and SIMP views and back. */
    void setViewButtons (bool fieldOpen, bool simpOpen);
    void setStats (double cpuProportion, double sampleRate, int blockSize, bool running, double bpm);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Tab final : public juce::Button
    {
        explicit Tab (const juce::String& name) : juce::Button (name) {}
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    };

    std::array<std::unique_ptr<Tab>, 5> tabs;
    juce::TextButton fieldButton { "Campo" }, simpButton { "Simp" };
    IconButton aboutButton { theme::Icon::Info, "Acerca de KumAmp" };
    juce::Rectangle<int> tabArea, statsArea;
    juce::String cpuText, rateText, blockText, bpmText;
    bool audioRunning = false;
};

//==============================================================================
/** Left column: compact tuner, input gain + meter, global gate and pitch. */
class Sidebar final : public juce::Component,
                      public juce::SettableTooltipClient
{
public:
    explicit Sidebar (juce::AudioProcessorValueTreeState& parameters);
    ~Sidebar() override;

    std::function<void()> onTunerClicked;
    void setTunerReading (const Tuner::Reading& reading);
    void pushInputLevel (float left, float right) { inputMeter.push (left, right); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    juce::Slider inputKnob, gateThreshold, gateRelease, pitchKnob;
    PowerButton gatePower, pitchPower;
    LevelMeter inputMeter { 2 };
    std::unique_ptr<SliderAttachment> inputAttachment, thresholdAttachment, releaseAttachment, pitchAttachment;
    std::unique_ptr<ButtonAttachment> gateAttachment, pitchOnAttachment;

    // Guitarra / Bajo segmented switch for the global pitch.
    juce::TextButton guitarButton { "GUITARRA" }, bassButton { "BAJO" };
    std::unique_ptr<juce::ParameterAttachment> instrumentAttachment;

    // Clasico / Avanzado segmented switch for the input gate.
    juce::TextButton classicGateButton { juce::CharPointer_UTF8 ("CL\xc3\x81SICO") }, advancedGateButton { "AVANZADO" };
    std::unique_ptr<juce::ParameterAttachment> gateModeAttachment;

    Tuner::Reading reading;
    juce::uint32 lastValidTime = 0;
    Tuner::Reading lastValid;
    juce::Rectangle<int> tunerCard, inputCard, gateCard, pitchCard;
};

//==============================================================================
/** Scene strip under the grid: A-D, previous / next, scene name and store. */
class SceneBar final : public juce::Component
{
public:
    SceneBar();

    std::function<void (int)> onSceneSelected;
    std::function<void (int, const juce::String&)> onNameEdited;
    std::function<void()> onStore;

    void setActiveScene (int scene);
    void setSceneName (const juce::String& name);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    std::array<juce::TextButton, 4> sceneButtons;
    IconButton previous { theme::Icon::ChevronLeft, "Escena anterior" }, next { theme::Icon::ChevronRight, "Escena siguiente" };
    IconButton store { theme::Icon::Save, "Guardar escena" };
    juce::Label prefix, name;
    int activeScene = 0;
};

//==============================================================================
/** Block families; click to pick a block, or drag the family onto a cable. */
class BlockPalette final : public juce::Component
{
public:
    BlockPalette();

    std::function<void (int group, juce::Component& source)> onGroupClicked;
    std::function<void (juce::Component& source)> onAddClicked;
    void setHighlightedGroup (int group);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Item final : public juce::Component,
                        public juce::SettableTooltipClient
    {
        Item (BlockPalette& owner, int groupIndex) : palette (owner), group (groupIndex) {}
        void paint (juce::Graphics&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { repaint(); }

        BlockPalette& palette;
        int group;
        bool dragging = false;
    };

    IconButton add { theme::Icon::Plus, "Insertar bloque" };
    std::vector<std::unique_ptr<Item>> items;
    int highlighted = -1;
};
} // namespace kumamp
