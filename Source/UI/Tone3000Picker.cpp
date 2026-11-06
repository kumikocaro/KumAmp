#include "Tone3000Picker.h"

namespace kumamp
{
namespace
{
    juce::String utf8 (const char* text) { return juce::String (juce::CharPointer_UTF8 (text)); }
} // namespace

Tone3000Picker::Tone3000Picker()
{
    list.setRowHeight (40);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    loadButton.setColour (juce::TextButton::buttonColourId, theme::accent);
    loadButton.onClick   = [this] { choose (list.getSelectedRow()); };
    cancelButton.onClick = [this] { setVisible (false); if (onClosed) onClosed(); };
    credit.setFont (juce::FontOptions (12.0f), false, juce::Justification::centredLeft);
    credit.setColour (juce::HyperlinkButton::textColourId, theme::textDim);
    for (juce::Component* c : { (juce::Component*) &list, (juce::Component*) &loadButton,
                                (juce::Component*) &cancelButton, (juce::Component*) &credit })
        addAndMakeVisible (c);
}

Tone3000Picker::~Tone3000Picker()
{
    list.setModel (nullptr);
}

void Tone3000Picker::show (const t3k::Tone& newTone, const juce::Array<t3k::Model>& newModels)
{
    tone = newTone;
    models = newModels;
    list.updateContent();
    list.selectRow (0);
    setVisible (true);
    toFront (true);
    repaint();
}

void Tone3000Picker::choose (int row)
{
    if (row < 0 || row >= models.size())
        return;
    setVisible (false);
    if (onChosen)
        onChosen (tone, models[row]);
}

void Tone3000Picker::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= models.size())
        return;
    auto area = juce::Rectangle<int> (width, height).reduced (4, 2);
    if (selected)
    {
        g.setColour (theme::accent.withAlpha (0.28f));
        g.fillRect (area.toFloat());
    }
    area.reduce (14, 0);
    const auto& m = models.getReference (row);
    if (m.size.isNotEmpty())
    {
        theme::drawChip (g, area.removeFromRight (84).withSizeKeepingCentre (84, 24).toFloat(), m.size, theme::accentLight);
        area.removeFromRight (8);
    }
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (15.0f));
    g.drawText (m.name, area, juce::Justification::centredLeft, true);
}

void Tone3000Picker::paint (juce::Graphics& g)
{
    g.fillAll (theme::background.withAlpha (0.82f));
    theme::drawCard (g, card.toFloat());

    auto h = header;
    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (utf8 ("TONE3000 \xc2\xb7 eleg\xc3\xad el modelo"), h.removeFromTop (20), juce::Justification::centredLeft);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (21.0f, juce::Font::bold));
    g.drawText (tone.title, h.removeFromTop (30), juce::Justification::centredLeft, true);

    juce::String details;
    if (tone.creator.isNotEmpty()) details << "por " << tone.creator;
    if (tone.license.isNotEmpty()) details << (details.isEmpty() ? "" : "   ") << "Licencia: " << tone.license.toUpperCase();
    details << (details.isEmpty() ? "" : "   ") << juce::String (models.size()) << " modelos";
    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (details, h.removeFromTop (20), juce::Justification::centredLeft, true);
}

void Tone3000Picker::resized()
{
    card = getLocalBounds().withSizeKeepingCentre (std::min (getWidth() - 40, 720), std::min (getHeight() - 30, 460));
    auto a = card.reduced (22, 16);
    header = a.removeFromTop (74);
    a.removeFromTop (6);
    auto footer = a.removeFromBottom (38);
    loadButton.setBounds (footer.removeFromRight (170));
    footer.removeFromRight (8);
    cancelButton.setBounds (footer.removeFromRight (110));
    credit.setBounds (footer.removeFromLeft (160));
    a.removeFromBottom (10);
    list.setBounds (a);
}
} // namespace kumamp
