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
    setColour (juce::TextButton::textColourOnId, ink);
    setColour (juce::ComboBox::backgroundColourId, panelRaised);
    setColour (juce::ComboBox::outlineColourId, border);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, textDim);
    setColour (juce::PopupMenu::backgroundColourId, panelRaised);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent);
    setColour (juce::PopupMenu::highlightedTextColourId, ink);
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
    setColour (juce::ScrollBar::thumbColourId, textDim);

    juce::LookAndFeel::setDefaultLookAndFeel (this);
}

KumAmpLookAndFeel::~KumAmpLookAndFeel()
{
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

juce::Typeface::Ptr KumAmpLookAndFeel::getTypefaceForFont (const juce::Font& font)
{
    if (font.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
        return font.isBold() ? typefaces->semiBold : typefaces->regular;
    return LookAndFeel_V4::getTypefaceForFont (font);
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
    const juce::PathStrokeType arcStroke (arcWidth, juce::PathStrokeType::curved, juce::PathStrokeType::butt);

    // Track + value arc (from the centre for bipolar ranges like -12..+12).
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (border);
    g.strokePath (track, arcStroke);

    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float origin = bipolar ? startAngle + (float) slider.valueToProportionOfLength (0.0) * (endAngle - startAngle) : startAngle;
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, std::min (origin, angle), std::max (origin, angle), true);
    g.setColour (slider.isEnabled() ? arcColour : textDim);
    g.strokePath (value, arcStroke);

    // Flat bone disc with an ink pointer.
    const float knobRadius = arcRadius - arcWidth * 1.4f;
    g.setColour (slider.isEnabled() ? bone : textDim);
    g.fillEllipse (juce::Rectangle<float> (knobRadius * 2.0f, knobRadius * 2.0f).withCentre (centre));

    const auto tip = centre.getPointOnCircumference (knobRadius, angle);
    const auto base = centre.getPointOnCircumference (knobRadius * 0.3f, angle);
    g.setColour (ink);
    g.drawLine ({ base, tip }, std::max (2.5f, knobRadius * 0.12f));
}

void KumAmpLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& colour,
                                              bool highlighted, bool down)
{
    using namespace theme;
    const auto area = button.getLocalBounds().toFloat();
    auto fill = button.getToggleState() ? button.findColour (juce::TextButton::buttonOnColourId) : colour;
    if (down) fill = fill.brighter (0.15f);
    else if (highlighted) fill = fill.brighter (0.07f);

    g.setColour (fill);
    g.fillRect (area);
    if (! button.getToggleState())
    {
        g.setColour (highlighted ? text : border);
        g.drawRect (area, 1.0f);
    }
}

void KumAmpLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    const auto fill = button.findColour (button.getToggleState() ? juce::TextButton::buttonOnColourId
                                                                 : juce::TextButton::buttonColourId);
    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.setColour (theme::textOn (fill).withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
    g.drawFittedText (button.getButtonText().toUpperCase(), button.getLocalBounds().reduced (4, 2),
                      juce::Justification::centred, 2, 0.8f);
}

juce::Font KumAmpLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return juce::FontOptions (std::min (15.0f, (float) buttonHeight * 0.5f), juce::Font::bold);
}

void KumAmpLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool)
{
    using namespace theme;
    const auto box = button.getLocalBounds().removeFromLeft (button.getHeight()).toFloat().withSizeKeepingCentre (16.0f, 16.0f);
    if (button.getToggleState())
    {
        g.setColour (accent);
        g.fillRect (box);
    }
    else
    {
        g.setColour (highlighted ? text : border);
        g.drawRect (box, 1.0f);
    }
    g.setColour (text.withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
    g.setFont (juce::FontOptions (15.0f));
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().withTrimmedLeft (button.getHeight() + 2),
                      juce::Justification::centredLeft, 2, 0.9f);
}

void KumAmpLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    using namespace theme;
    const auto area = juce::Rectangle<float> ((float) width, (float) height);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRect (area);
    g.setColour (box.hasKeyboardFocus (false) || box.isMouseOver (true) ? text : border);
    g.drawRect (area, 1.0f);

    juce::Path chevron;
    const float cx = (float) width - 18.0f, cy = (float) height * 0.5f;
    chevron.startNewSubPath (cx - 5.0f, cy - 2.5f);
    chevron.lineTo (cx, cy + 2.5f);
    chevron.lineTo (cx + 5.0f, cy - 2.5f);
    g.setColour (accent);
    g.strokePath (chevron, juce::PathStrokeType (1.8f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
}

void KumAmpLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (10, 1, box.getWidth() - 36, box.getHeight() - 2);
    label.setFont (theme::mono (std::min (14.0f, (float) box.getHeight() * 0.44f)));
}

void KumAmpLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    // Slider value boxes: mono figures over a hairline.
    if (dynamic_cast<juce::Slider*> (label.getParentComponent()) != nullptr && ! label.isBeingEdited())
    {
        const auto area = label.getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (theme::border);
        g.fillRect (area.withTop (area.getBottom() - 1.0f));
        g.setColour (theme::text);
        g.setFont (theme::mono (std::min (14.0f, area.getHeight() * 0.62f)));
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
    g.fillRect ((float) x, cy - 1.0f, (float) width, 2.0f);
    g.setColour (theme::accent);
    g.fillRect ((float) x, cy - 2.0f, position - (float) x, 4.0f);
    g.setColour (theme::bone);
    g.fillRect (position - 4.0f, cy - 9.0f, 8.0f, 18.0f);
}

void KumAmpLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar& bar, int x, int y, int width, int height, bool vertical,
                                       int thumbStart, int thumbSize, bool over, bool down)
{
    if (thumbSize <= 0)
        return;
    const auto thumb = vertical ? juce::Rectangle<int> (x + width - 5, thumbStart, 4, thumbSize)
                                : juce::Rectangle<int> (thumbStart, y + height - 5, thumbSize, 4);
    g.setColour (over || down ? theme::text : bar.findColour (juce::ScrollBar::thumbColourId));
    g.fillRect (thumb);
}

void KumAmpLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (theme::panelRaised);
    g.setColour (theme::border);
    g.drawRect (0, 0, width, height);
}
} // namespace kumamp
