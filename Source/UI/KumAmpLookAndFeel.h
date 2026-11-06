#pragma once

#include "Theme.h"

namespace kumamp
{
/** Ink, bone and red: square controls, flat bone knobs, Jost and IBM Plex Mono.
    Shared by every open editor and installed as the default look and feel, since
    that is where a plain juce::Font gets its typeface from. */
class KumAmpLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    KumAmpLookAndFeel();
    ~KumAmpLookAndFeel() override;

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float position,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawLabel (juce::Graphics&, juce::Label&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float position, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool vertical,
                        int thumbStart, int thumbSize, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;

private:
    juce::SharedResourcePointer<theme::Typefaces> typefaces;
};
} // namespace kumamp
