#pragma once

#include "Widgets.h"
#include "../DSP/Pitch/SamplerBlock.h"

#include <functional>
#include <memory>

namespace kumamp
{
/**
    Large editor of the Sampler's sound, over the centre column.

    - Waveform with the trim: drag a handle to move that end, drag between them
      to move the whole trim. Mouse wheel = zoom at the cursor, horizontal wheel
      (or Shift + wheel) = move along the sound.
    - Escuchar plays the trim as it is, at its original pitch.
    - Quitar silencio finds where the sound starts and ends.
    - Base note: detected from the trim (Auto) or set by hand, with fine tuning.
    - Load with the button, or drag an audio file onto the view.
*/
class SampleEditorView final : public juce::Component,
                               public juce::FileDragAndDropTarget,
                               public juce::SettableTooltipClient,
                               private juce::Timer
{
public:
    explicit SampleEditorView (std::shared_ptr<ParamStore> samplerStore);
    ~SampleEditorView() override;

    std::function<void()> onClose;

    StoreId getStoreId() const noexcept { return store->getId(); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }

private:
    enum class Drag { None, Start, End, Both };

    void timerCallback() override;
    SamplerBlock& block() const noexcept { return *sampler; }
    void chooseFile();
    void setView (double newStart, double newEnd);
    void syncControls();
    void drawWaveform (juce::Graphics&, juce::Rectangle<float> area, const SamplerSound&) const;
    Drag dragAt (float x) const noexcept;
    float xOf (double fraction) const noexcept;
    double fractionAt (float x) const noexcept;

    std::shared_ptr<ParamStore> store;
    SamplerBlock* sampler = nullptr;
    std::shared_ptr<const SamplerSound> sound;

    juce::TextButton loadButton, closeButton { "Cerrar" }, listenButton { "Escuchar" }, silenceButton { "Quitar silencio" },
                     zoomButton { "Zoom al recorte" }, allButton { "Ver todo" };
    juce::ComboBox rootBox, octaveBox;
    juce::Slider tuneSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label rootLabel, tuneLabel;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> headerArea, waveArea;
    double viewStart = 0.0, viewEnd = 1.0;   // fractions of the sound
    float lastPlayhead = -1.0f;
    juce::String lastRoot;
    Drag drag = Drag::None;
    double grabOffset = 0.0, grabLength = 0.0;
    bool dragOver = false;
};
} // namespace kumamp
