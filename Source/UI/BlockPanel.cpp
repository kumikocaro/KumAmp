#include "BlockPanel.h"
#include "../Graph/DSPBlock.h"

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

    status.setColour (juce::Label::textColourId, juce::Colour (0xffaab3c5));
    status.setFont (juce::FontOptions (13.0f));
    addAndMakeVisible (status);
}

void BlockPanel::chooseFile (int slot)
{
    auto* block = dynamic_cast<DSPBlock*> (store.get());
    if (block == nullptr)
        return;

    auto start = juce::File (juce::String (block->getFilePath (slot)));
    if (! start.existsAsFile())
        start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

    chooser = std::make_unique<juce::FileChooser> ("Cargar " + juce::String (block->getFileSlotName (slot)),
                                                   start, juce::String (block->getFileSlotPattern (slot)));

    std::weak_ptr<ParamStore> target = store;
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [target, slot] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (auto locked = target.lock(); locked != nullptr && file.existsAsFile())
                                  if (auto* b = dynamic_cast<DSPBlock*> (locked.get()))
                                      b->loadFile (slot, file.getFullPathName().toStdString());
                          });
}

void BlockPanel::setStore (std::shared_ptr<ParamStore> newStore)
{
    sliders.clear();
    labels.clear();
    fileButtons.clear();
    status.setText ({}, juce::dontSendNotification);
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

    if (auto* block = dynamic_cast<DSPBlock*> (store.get()))
    {
        for (int slot = 0; slot < block->getNumFileSlots(); ++slot)
        {
            auto* button = fileButtons.add (new juce::TextButton ("Cargar " + juce::String (block->getFileSlotName (slot)) + "..."));
            button->onClick = [this, slot] { chooseFile (slot); };
            addAndMakeVisible (button);
        }
    }

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

    if (auto* block = dynamic_cast<DSPBlock*> (store.get()))
        status.setText (juce::String (block->getStatusText()), juce::dontSendNotification);
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
    for (int i = fileButtons.size(); --i >= 0;)
        fileButtons[i]->setBounds (header.removeFromRight (150).reduced (3, 0));
    title.setBounds (header);
    status.setBounds (area.removeFromTop (20));
    area.removeFromTop (4);

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
