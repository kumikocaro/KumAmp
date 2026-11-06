#include "AboutView.h"

#include <KumAmpFontData.h>

namespace kumamp
{
AboutView::AboutView()
{
    photo = juce::ImageCache::getFromMemory (KumAmpFontData::kumo_jpg, KumAmpFontData::kumo_jpgSize);

    website.setFont (juce::Font (juce::FontOptions (24.0f, juce::Font::bold)), false, juce::Justification::centredLeft);
    website.setColour (juce::HyperlinkButton::textColourId, theme::accent);
    website.setTooltip ("Abre la p\xc3\xa1" "gina en tu navegador");
    addAndMakeVisible (website);

    close.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (close);
}

void AboutView::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
    theme::drawCard (g, getLocalBounds().reduced (16).toFloat());
    g.drawImage (photo, photoArea.toFloat(), juce::RectanglePlacement::centred);

    auto area = textArea;
    g.setColour (theme::textDim);
    g.setFont (theme::mono (13.0f));
    g.drawText ("ACERCA DE KUMAMP", area.removeFromTop (24), juce::Justification::centredLeft);

    g.setColour (theme::text);
    g.setFont (theme::display (64.0f));
    g.drawText ("KUMO CARO", area.removeFromTop (78), juce::Justification::centredLeft);

    area.removeFromTop (48);
    g.setColour (theme::border);
    g.fillRect (area.removeFromTop (1));
    area.removeFromTop (14);
    g.setColour (theme::textDim);
    g.setFont (theme::mono (13.0f));
    g.drawText ("KUMAMP " KUMAMP_VERSION " // SOFTWARE LIBRE, GPLv3", area.removeFromTop (22), juce::Justification::centredLeft);
}

void AboutView::resized()
{
    auto area = getLocalBounds().reduced (40);
    close.setBounds (area.getRight() - 120, area.getY(), 120, 38);

    const int textWidth = 420;
    textArea = area.removeFromRight (textWidth).withSizeKeepingCentre (textWidth, 190);
    area.removeFromRight (30);
    photoArea = area;
    website.setBounds (textArea.getX(), textArea.getY() + 106, textWidth, 42);
}
} // namespace kumamp
