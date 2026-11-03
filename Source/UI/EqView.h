#pragma once

#include "Widgets.h"
#include "../DSP/Dynamics/DynamicsBlocks.h"

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <functional>
#include <memory>

namespace kumamp
{
/**
    Graphic editor for the 8-band ParametricEqBlock.

    - Frequency response (sum of all bands + output) over a live spectrum
      analyser (pre-EQ outline, post-EQ fill), log 10 Hz .. 30 kHz.
    - Coloured band nodes: drag = frequency / gain (Q for cuts, notch and band
      pass), mouse wheel = Q, Shift = fine, double-click empty space = new band,
      double-click a node = remove, right-click = type / slope / remove.
    - Band listen (FabFilter style): Alt + click-and-hold a node to hear only
      the region that band works on (you can keep dragging it), or latch it on
      the selected band with the "Escuchar" button.
    - Selected band: edit strip with type, slope, frequency, gain and Q.
    The curve is computed with the same EqDesign code that runs the audio.
*/
class EqView final : public juce::Component,
                     public juce::SettableTooltipClient,
                     private juce::Timer
{
public:
    explicit EqView (bool compact);
    ~EqView() override;

    void setBlock (std::shared_ptr<ParamStore> eqStore);

    std::function<void()> onExpand;   // compact view: open large
    std::function<void()> onClose;    // large view: close

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void visibilityChanged() override;

    static juce::Colour bandColour (int band);

private:
    static constexpr int kFftOrder = 12, kFftSize = 1 << kFftOrder;

    void timerCallback() override;
    ParametricEqBlock* eq() const noexcept { return dynamic_cast<ParametricEqBlock*> (store.get()); }

    juce::Rectangle<float> plotArea() const;
    float freqToX (float hz) const;
    float xToFreq (float x) const;
    float gainToY (float db) const;
    float yToGain (float y) const;
    float dbRange() const noexcept { return compactMode ? 18.0f : 24.0f; }
    juce::Point<float> nodePosition (int band) const;
    int bandAt (juce::Point<float> position) const;
    static bool hasGain (EqBandType type) noexcept;

    void setBandParam (int band, ParametricEqBlock::BandParam param, float value);
    void updateListen();
    void releaseListen();
    void selectBand (int band);
    void showBandMenu (int band);
    void updateSpectrum (const SampleRing<8192>& ring, std::array<float, kFftSize / 2 + 1>& smoothed);
    void syncStripFromParams();
    juce::Path spectrumPath (const std::array<float, kFftSize / 2 + 1>& spectrum, bool closed) const;

    std::shared_ptr<ParamStore> store;
    const bool compactMode;

    juce::dsp::FFT fft { kFftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) kFftSize, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, kFftSize * 2> fftBuffer {};
    std::array<float, kFftSize / 2 + 1> preSpectrum {}, postSpectrum {};

    int selectedBand = -1, hoverBand = -1, dragBand = -1;
    int heldListenBand = -1;      // Alt + click-and-hold
    bool listenLatched = false;   // "Escuchar" button
    bool ownsListen = false;      // this view set the block's listen band
    juce::Point<float> dragStart;
    EqBand dragStartBand;

    // Edit strip for the selected band.
    juce::ComboBox typeBox, slopeBox;
    juce::Slider freqKnob, gainKnob, qKnob, outputKnob;
    juce::Label freqLabel, gainLabel, qLabel, outputLabel;
    IconButton removeBand { theme::Icon::Trash, "Quitar banda" };
    juce::TextButton listenButton { "Escuchar" };
    IconButton expandButton { theme::Icon::Expand, "Abrir EQ grande" };
    juce::TextButton closeButton { "Cerrar" };
    juce::Rectangle<int> stripArea;
    bool updatingStrip = false;
};
} // namespace kumamp
