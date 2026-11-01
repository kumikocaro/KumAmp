#pragma once

#include "Theme.h"

namespace kumamp
{
/** Dark "pedalboard" look: purple-arc knobs, rounded controls, pill value boxes. */
class KumAmpLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    KumAmpLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float position,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawLabel (juce::Graphics&, juce::Label&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float position, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
};
} // namespace kumamp
