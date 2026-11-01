#include "BlockPanel.h"

namespace kumamp
{
BlockPanel::BlockPanel()
{
    title.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    title.setText ("Selecciona un bloque", juce::dontSendNotification);
    addAndMakeVisible (title);

    bypass.onClick = [this]
    {
        if (store != nullptr)
            store->setBypassed (bypass.getToggleState());
    };
    addChildComponent (bypass);
}

void BlockPanel::setStore (std::shared_ptr<ParamStore> newStore)
{
    sliders.clear();
    labels.clear();
    store = std::move (newStore);

    if (store == nullptr)
    {
        title.setText ("Selecciona un bloque", juce::dontSendNotification);
        bypass.setVisible (false);
        resized();
        return;
    }

    title.setText (juce::String (store->getName()) + "  -  " + getBlockTypeInfo (store->getType()).displayName,
                   juce::dontSendNotification);

    const bool isRouting = store->getType() == BlockType::Splitter || store->getType() == BlockType::Mixer;
    bypass.setVisible (! isRouting);

    for (int i = 0; i < store->getNumParams(); ++i)
    {
        const auto& info = store->getParamInfo (i);

        auto* slider = sliders.add (new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow));
        slider->setRange (info.minValue, info.maxValue, info.isDiscrete ? 1.0 : 0.0);
        slider->setDoubleClickReturnValue (true, info.defaultValue);
        slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
        slider->setNumDecimalPlacesToDisplay (info.isDiscrete ? 0 : 2);
        if (info.unit[0] != '\0')
            slider->setTextValueSuffix (juce::String (" ") + info.unit);
        slider->onValueChange = [this, i, slider]
        {
            if (store != nullptr)
                store->setParam (i, (float) slider->getValue());
        };
        addAndMakeVisible (slider);

        auto* label = labels.add (new juce::Label ({}, info.name));
        label->setJustificationType (juce::Justification::centred);
        addAndMakeVisible (label);
    }

    refresh();
    resized();
}

void BlockPanel::refresh()
{
    if (store == nullptr)
        return;

    for (int i = 0; i < sliders.size(); ++i)
        if (auto* s = sliders[i]; ! s->isMouseButtonDown())
            s->setValue (store->getParam (i), juce::dontSendNotification);

    bypass.setToggleState (store->isBypassed(), juce::dontSendNotification);
}

void BlockPanel::paint (juce::Graphics& g)
{
    g.setColour (juce::Colour (0xff1c1f26));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
}

void BlockPanel::resized()
{
    auto area = getLocalBounds().reduced (12);
    auto header = area.removeFromTop (28);
    bypass.setBounds (header.removeFromRight (100));
    title.setBounds (header);
    area.removeFromTop (8);

    constexpr int cellWidth = 96, cellHeight = 120;
    const int perRow = std::max (1, area.getWidth() / cellWidth);

    for (int i = 0; i < sliders.size(); ++i)
    {
        const int x = area.getX() + (i % perRow) * cellWidth;
        const int y = area.getY() + (i / perRow) * cellHeight;
        labels[i]->setBounds (x, y, cellWidth, 18);
        sliders[i]->setBounds (x, y + 18, cellWidth, cellHeight - 22);
    }
}
} // namespace kumamp
