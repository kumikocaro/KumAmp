#include "KumAmpLookAndFeel.h"
#include <algorithm>

namespace kumamp
{
KumAmpLookAndFeel::KumAmpLookAndFeel()
{
    using namespace theme;
    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::DocumentWindow::backgroundColourId, background);
    setColour (juce::Label::textColourId, text);
    setColour (juce::TextButton::buttonColourId, panelRaised);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::ComboBox::backgroundColourId, panelRaised);
    setColour (juce::ComboBox::outlineColourId, border);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, textDim);
    setColour (juce::PopupMenu::backgroundColourId, panelRaised);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.35f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::PopupMenu::headerTextColourId, textDim);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxBackgroundColourId, panelRaised);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::thumbColourId, accentLight);
    setColour (juce::Slider::trackColourId, accent);
    setColour (juce::Slider::backgroundColourId, border);
    setColour (juce::ToggleButton::textColourId, text);
    setColour (juce::ToggleButton::tickColourId, accentLight);
    setColour (juce::ToggleButton::tickDisabledColourId, textDim);
    setColour (juce::TextEditor::backgroundColourId, panelRaised);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::outlineColourId, border);
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
    setColour (juce::TooltipWindow::backgroundColourId, panelRaised);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, border);
}

void KumAmpLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float position,
                                          float startAngle, float endAngle, juce::Slider& slider)
{
    using namespace theme;
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (4.0f);
    const float radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float arcWidth = std::max (3.0f, radius * 0.12f);
    const float arcRadius = radius - arcWidth * 0.5f;
    const float angle = startAngle + position * (endAngle - startAngle);
    const auto arcColour = slider.findColour (juce::Slider::trackColourId);

    // Track + value arc (from the centre for bipolar ranges like -12..+12).
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (border);
    g.strokePath (track, juce::PathStrokeType (arcWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float origin = bipolar ? startAngle + (float) slider.valueToProportionOfLength (0.0) * (endAngle - startAngle) : startAngle;
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, std::min (origin, angle), std::max (origin, angle), true);
    g.setColour (slider.isEnabled() ? arcColour : textDim);
    g.strokePath (value, juce::PathStrokeType (arcWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Knob body.
    const float knobRadius = arcRadius - arcWidth * 1.4f;
    const auto knob = juce::Rectangle<float> (knobRadius * 2.0f, knobRadius * 2.0f).withCentre (centre);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a2f48), knob.getX(), knob.getY(),
                                             juce::Colour (0xff0f1220), knob.getRight(), knob.getBottom(), false));
    g.fillEllipse (knob);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawEllipse (knob, 1.5f);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawEllipse (knob.reduced (1.5f), 1.0f);

    // Pointer.
    const auto tip = centre.getPointOnCircumference (knobRadius * 0.82f, angle);
    const auto base = centre.getPointOnCircumference (knobRadius * 0.35f, angle);
    g.setColour (text);
    g.drawLine ({ base, tip }, std::max (2.0f, knobRadius * 0.09f));
}

void KumAmpLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& colour,
                                              bool highlighted, bool down)
{
    using namespace theme;
    const auto area = button.getLocalBounds().toFloat().reduced (0.5f);
    const float radius = std::min (8.0f, area.getHeight() * 0.3f);
    auto fill = button.getToggleState() ? button.findColour (juce::TextButton::buttonOnColourId) : colour;
    if (down) fill = fill.brighter (0.15f);
    else if (highlighted) fill = fill.brighter (0.07f);

    g.setColour (fill);
    g.fillRoundedRectangle (area, radius);
    if (! button.getToggleState())
    {
        g.setColour (border);
        g.drawRoundedRectangle (area, radius, 1.0f);
    }
}

juce::Font KumAmpLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return juce::FontOptions (std::min (16.0f, (float) buttonHeight * 0.48f));
}

void KumAmpLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    using namespace theme;
    const auto area = juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (area, 8.0f);
    g.setColour (box.hasKeyboardFocus (false) ? accent : border);
    g.drawRoundedRectangle (area, 8.0f, 1.0f);

    juce::Path chevron;
    const float cx = (float) width - 18.0f, cy = (float) height * 0.5f;
    chevron.startNewSubPath (cx - 5.0f, cy - 2.5f);
    chevron.lineTo (cx, cy + 2.5f);
    chevron.lineTo (cx + 5.0f, cy - 2.5f);
    g.setColour (textDim);
    g.strokePath (chevron, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void KumAmpLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (10, 1, box.getWidth() - 36, box.getHeight() - 2);
    label.setFont (juce::FontOptions (std::min (16.0f, (float) box.getHeight() * 0.48f)));
}

void KumAmpLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    // Slider value boxes become pills.
    if (dynamic_cast<juce::Slider*> (label.getParentComponent()) != nullptr && ! label.isBeingEdited())
    {
        const auto area = label.getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (theme::panelRaised);
        g.fillRoundedRectangle (area, area.getHeight() * 0.5f);
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (std::min (15.0f, area.getHeight() * 0.62f)));
        g.drawText (label.getText(), area, juce::Justification::centred);
        return;
    }
    LookAndFeel_V4::drawLabel (g, label);
}

void KumAmpLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float position,
                                          float minPos, float maxPos, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, position, minPos, maxPos, style, slider);
        return;
    }
    const float cy = (float) y + (float) height * 0.5f;
    g.setColour (theme::border);
    g.fillRoundedRectangle ((float) x, cy - 2.0f, (float) width, 4.0f, 2.0f);
    g.setColour (theme::accent);
    g.fillRoundedRectangle ((float) x, cy - 2.0f, position - (float) x, 4.0f, 2.0f);
    g.setColour (theme::accentLight);
    g.fillEllipse (position - 7.0f, cy - 7.0f, 14.0f, 14.0f);
}

void KumAmpLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (theme::panelRaised);
    g.setColour (theme::border);
    g.drawRect (0, 0, width, height);
}
} // namespace kumamp
