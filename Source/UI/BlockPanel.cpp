#include "BlockPanel.h"
#include "../Graph/DSPBlock.h"
#include <utility>

namespace kumamp
{
namespace
{
    constexpr int kOpenLibraryId = 9998;    // file combo: open Documents/KumAmp/TONE3000
    constexpr int kLibraryBaseId = 20000;   // file combo: downloaded TONE3000 models
} // namespace

void BlockPanel::Pill::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (theme::panelRaised);
    g.fillRoundedRectangle (area, area.getHeight() * 0.5f);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (std::min (text.length() > 9 ? 14.0f : 17.0f, area.getHeight() * 0.62f)));
    g.drawText (text, area, juce::Justification::centred);
}

BlockPanel::BlockPanel()
{
    power.setTooltip ("Activar / bypass");
    power.onClick = [this] { if (store != nullptr) store->setBypassed (! power.getToggleState()); };
    addChildComponent (power);

    remove.onClick = [this] { if (onRemove) onRemove(); };
    addChildComponent (remove);
    addChildComponent (info);
    addAndMakeVisible (outputMeter);
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

void BlockPanel::populateFileSlot (int slot)
{
    auto* block = dynamic_cast<DSPBlock*> (store.get());
    auto& fs = fileSlots[(size_t) slot];
    if (block == nullptr)
        return;

    const auto path = block->getFilePath (slot);
    fs.shownPath = path;
    fs.choices.clear();
    fs.files->clear (juce::dontSendNotification);

    const juce::File current { juce::String (path) };
    if (current.existsAsFile())
    {
        // Every model / IR in the same folder, for quick A/B switching.
        for (const auto& entry : juce::RangedDirectoryIterator (current.getParentDirectory(), false,
                                                                juce::String (block->getFileSlotPattern (slot))))
            fs.choices.add (entry.getFile());
        fs.choices.sort();
        for (int i = 0; i < fs.choices.size(); ++i)
            fs.files->addItem (fs.choices[i].getFileName(), i + 1);
        fs.files->setSelectedId (fs.choices.indexOf (current) + 1, juce::dontSendNotification);
    }
    else
    {
        fs.files->setTextWhenNothingSelected (path.empty() ? "Seleccionar " + juce::String (block->getFileSlotName (slot)) + "..."
                                                           : juce::String ("No encontrado: ") + juce::File (juce::String (path)).getFileName());
    }
    // NAM amp: the TONE3000 library (Documents/KumAmp/TONE3000), one submenu per tone.
    fs.library.clear();
    if (slot == 0 && store->getType() == BlockType::NamAmp)
    {
        std::vector<std::pair<juce::String, juce::Array<juce::File>>> tones;
        for (const auto& entry : juce::RangedDirectoryIterator (t3k::Session::libraryFolder(), false, "*",
                                                                juce::File::findDirectories))
        {
            juce::Array<juce::File> models;
            for (const auto& model : juce::RangedDirectoryIterator (entry.getFile(), false, "*.nam"))
                models.add (model.getFile());
            if (models.isEmpty())
                continue;
            models.sort();
            t3k::Tone tone;
            juce::Array<t3k::Model> unused;
            const auto title = t3k::readToneFolder (entry.getFile(), tone, unused)
                                   ? tone.title + (tone.creator.isNotEmpty() ? juce::String (juce::CharPointer_UTF8 (" \xe2\x80\x94 ")) + tone.creator : juce::String())
                                   : entry.getFile().getFileName();
            tones.emplace_back (title, models);
        }
        std::sort (tones.begin(), tones.end(), [] (const auto& a, const auto& b) { return a.first.compareNatural (b.first, false) < 0; });

        fs.files->addSeparator();
        fs.files->addSectionHeading ("Mis tonos (TONE3000)");
        for (auto& [title, models] : tones)
        {
            juce::PopupMenu sub;
            for (const auto& model : models)
            {
                fs.library.add (model);
                sub.addItem (kLibraryBaseId + fs.library.size() - 1, model.getFileNameWithoutExtension(), true, model == current);
            }
            fs.files->getRootMenu()->addSubMenu (title + "  (" + juce::String (models.size()) + ")", sub, true, nullptr,
                                                 current.isAChildOf (models.getFirst().getParentDirectory()));
        }
        if (tones.empty())
            fs.files->addItem (juce::CharPointer_UTF8 ("Todav\xc3\xad" "a no descargaste tonos: us\xc3\xa1 el bot\xc3\xb3n TONE3000"), -1);
        fs.files->setItemEnabled (-1, false);
        fs.files->addItem ("Abrir carpeta de tonos", kOpenLibraryId);
    }

    fs.files->addSeparator();
    fs.files->addItem (juce::CharPointer_UTF8 ("Buscar archivo\xe2\x80\xa6"), 9999);

    if (slot == 0 && variants != nullptr)
        populateVariants (current);
}

void BlockPanel::populateVariants (const juce::File& currentModel)
{
    variantModels.clear();
    variants->clear (juce::dontSendNotification);
    variantFolder = currentModel.getParentDirectory();
    const bool isTone = currentModel.existsAsFile() && t3k::readToneFolder (variantFolder, variantTone, variantModels)
                        && variantModels.size() > 1;
    variants->setVisible (isTone);
    if (isTone)
    {
        int downloaded = 0;
        for (int i = 0; i < variantModels.size(); ++i)
        {
            const auto& m = variantModels.getReference (i);
            const bool here = variantFolder.getChildFile (t3k::modelFileName (m)).existsAsFile();
            downloaded += here ? 1 : 0;
            // Check mark = on disk (instant); arrow = downloads from TONE3000 when picked.
            variants->addItem (juce::String (juce::CharPointer_UTF8 (here ? "\xe2\x9c\x93  " : "\xe2\x86\x93  ")) + m.name, i + 1);
            if (t3k::modelFileName (m) == currentModel.getFileName())
                variants->setSelectedId (i + 1, juce::dontSendNotification);
        }
        variants->setTextWhenNothingSelected ("Variantes (" + juce::String (variantModels.size()) + ")");
        variants->setTooltip (juce::String (variantModels.size()) + " variantes de \"" + variantTone.title + "\", "
                              + juce::String (downloaded) + " descargadas. " + juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x93")) + " = en el disco, "
                              + juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x93")) + " = se descarga al elegirla");
    }
    resized();
}

void BlockPanel::setStore (std::shared_ptr<ParamStore> newStore)
{
    controls.clear();
    fileSlots.clear();
    chips.clear();
    eqView.reset();
    tone3000Button.reset();
    tone3000Credit.reset();
    variants.reset();
    variantModels.clear();
    syncParam = divisionParam = -1;
    store = std::move (newStore);

    const bool hasStore = store != nullptr;
    const bool isRouting = hasStore && (store->getType() == BlockType::Splitter || store->getType() == BlockType::Mixer);
    power.setVisible (hasStore && ! isRouting);
    remove.setVisible (hasStore && ! isRouting);
    info.setVisible (false);

    if (! hasStore)
    {
        resized();
        repaint();
        return;
    }

    if (auto* block = dynamic_cast<DSPBlock*> (store.get()))
    {
        for (int slot = 0; slot < block->getNumFileSlots(); ++slot)
        {
            FileSlot fs;
            fs.files = std::make_unique<juce::ComboBox>();
            fs.browse = std::make_unique<IconButton> (theme::Icon::More, "Buscar archivo...");
            auto* combo = fs.files.get();
            combo->onChange = [this, slot, combo]
            {
                auto* b = dynamic_cast<DSPBlock*> (store.get());
                const int id = combo->getSelectedId();
                if (b == nullptr || id <= 0)
                    return;
                if (id == 9999)
                {
                    populateFileSlot (slot);   // restore the shown entry, then browse
                    chooseFile (slot);
                    return;
                }
                if (id == kOpenLibraryId)
                {
                    populateFileSlot (slot);
                    const auto folder = t3k::Session::libraryFolder();
                    folder.createDirectory();
                    folder.startAsProcess();
                    return;
                }
                if (id >= kLibraryBaseId)
                {
                    const auto& library = fileSlots[(size_t) slot].library;
                    if (id - kLibraryBaseId < library.size())
                        b->loadFile (slot, library[id - kLibraryBaseId].getFullPathName().toStdString());
                    return;
                }
                const auto& choices = fileSlots[(size_t) slot].choices;
                if (id - 1 < choices.size())
                    b->loadFile (slot, choices[id - 1].getFullPathName().toStdString());
            };
            fs.browse->onClick = [this, slot] { chooseFile (slot); };
            addAndMakeVisible (*fs.files);
            addAndMakeVisible (*fs.browse);
            fileSlots.push_back (std::move (fs));
            populateFileSlot (slot);
        }

        if (store->getType() == BlockType::NamAmp)
        {
            tone3000Button = std::make_unique<juce::TextButton> ("TONE3000");
            tone3000Button->setTooltip ("Elegir un amp en tone3000.com y cargarlo directo");
            tone3000Button->setColour (juce::TextButton::buttonColourId, theme::accent.withAlpha (0.35f));
            tone3000Button->onClick = [this]
            {
                if (onBrowseTone3000 && store != nullptr && tone3000Button != nullptr)
                    onBrowseTone3000 (store, *tone3000Button);
            };
            addAndMakeVisible (*tone3000Button);
            // Attribution required by the TONE3000 API terms.
            tone3000Credit = std::make_unique<juce::HyperlinkButton> ("Powered by TONE3000", juce::URL ("https://www.tone3000.com"));
            tone3000Credit->setFont (juce::FontOptions (12.0f), false, juce::Justification::centredLeft);
            tone3000Credit->setColour (juce::HyperlinkButton::textColourId, theme::textDim);
            addAndMakeVisible (*tone3000Credit);

            variants = std::make_unique<juce::ComboBox>();
            variants->setTooltip ("Otras variantes de este tono de TONE3000");
            auto* combo = variants.get();
            combo->onChange = [this, combo]
            {
                const int index = combo->getSelectedId() - 1;
                if (index < 0 || index >= variantModels.size() || store == nullptr)
                    return;
                const auto& model = variantModels.getReference (index);
                const auto file = variantFolder.getChildFile (t3k::modelFileName (model));
                if (file.existsAsFile())
                {
                    if (auto* b = dynamic_cast<DSPBlock*> (store.get()))
                        b->loadFile (0, file.getFullPathName().toStdString());
                }
                else if (onTone3000Variant)
                {
                    onTone3000Variant (store, variantTone, model);
                }
            };
            addChildComponent (*combo);
            if (auto* b = dynamic_cast<DSPBlock*> (store.get()))
                populateVariants (juce::File (juce::String (b->getFilePath (0))));
        }
    }

    // The 8-band EQ gets the graphic editor instead of 49 knobs.
    if (store->getType() == BlockType::ParametricEq)
    {
        eqView = std::make_unique<EqView> (true);
        eqView->setBlock (store);
        eqView->onExpand = [this] { if (onExpandEq) onExpandEq (store); };
        addAndMakeVisible (*eqView);
        refresh();
        resized();
        repaint();
        return;
    }

    for (int i = 0; i < store->getNumParams(); ++i)
    {
        const auto& paramInfo = store->getParamInfo (i);
        const juce::String unit (paramInfo.unit);
        const juce::String paramId (paramInfo.id);
        ParamControl control;

        if (paramId == "sync")
        {
            // Tempo sync: a toggle button; the knob it drives shows the synced value.
            syncParam = i;
            control.toggle = std::make_unique<juce::TextButton> ("SYNC");
            control.toggle->setClickingTogglesState (true);
            control.toggle->setTooltip ("Sincronizar con el tempo del proyecto");
            auto* button = control.toggle.get();
            button->onClick = [this, i, button] { if (store != nullptr) store->setParam (i, button->getToggleState() ? 1.0f : 0.0f); };
            addAndMakeVisible (button);
        }
        else if (paramInfo.isDiscrete && unit.containsChar ('|'))
        {
            control.combo = std::make_unique<juce::ComboBox>();
            const auto choices = juce::StringArray::fromTokens (unit, "|", {});
            for (int c = 0; c < choices.size(); ++c)
                control.combo->addItem (choices[c], c + 1);
            auto* combo = control.combo.get();
            combo->onChange = [this, i, combo]
            {
                if (store != nullptr && combo->getSelectedId() > 0)
                    store->setParam (i, (float) (combo->getSelectedId() - 1));
            };
            if (paramId == "division")
                divisionParam = i;
            addAndMakeVisible (combo);
        }
        else
        {
            control.slider = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
            auto* slider = control.slider.get();
            slider->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
            slider->setRange (paramInfo.minValue, paramInfo.maxValue, paramInfo.isDiscrete ? 1.0 : 0.0);
            slider->setDoubleClickReturnValue (true, paramInfo.defaultValue);
            slider->setColour (juce::Slider::trackColourId, theme::accent);
            const int decimals = paramInfo.isDiscrete ? 0 : (paramInfo.maxValue - paramInfo.minValue >= 100.0f ? 0 : 1);
            const juce::String suffix = unit.isNotEmpty() ? " " + unit : juce::String();
            slider->textFromValueFunction = [decimals, suffix] (double v) { return juce::String (v, decimals) + suffix; };

            control.value = std::make_unique<Pill>();
            auto* pill = control.value.get();
            slider->onValueChange = [this, i, slider, pill]
            {
                if (store != nullptr)
                    store->setParam (i, (float) slider->getValue());
                pill->text = slider->getTextFromValue (slider->getValue());
                pill->repaint();
            };
            addAndMakeVisible (slider);
            addAndMakeVisible (pill);
        }

        control.name = std::make_unique<juce::Label> (juce::String(), paramInfo.name);
        control.name->setJustificationType (juce::Justification::centred);
        control.name->setFont (juce::FontOptions (17.0f));
        addAndMakeVisible (*control.name);
        controls.push_back (std::move (control));
    }

    refresh();
    resized();
    repaint();
}

void BlockPanel::refresh()
{
    if (store == nullptr)
        return;

    auto* dspBlock = dynamic_cast<DSPBlock*> (store.get());
    const bool synced = syncParam >= 0 && store->getParam (syncParam) > 0.5f;

    for (int i = 0; i < (int) controls.size(); ++i)
    {
        auto& c = controls[(size_t) i];
        if (c.slider != nullptr && ! c.slider->isMouseButtonDown())
        {
            c.slider->setValue (store->getParam (i), juce::dontSendNotification);
            // A tempo-synced time/rate shows the resulting value and is locked.
            const auto override = dspBlock != nullptr ? juce::String (dspBlock->getDisplayOverride (i)) : juce::String();
            c.value->text = override.isNotEmpty() ? override : c.slider->getTextFromValue (c.slider->getValue());
            c.slider->setEnabled (override.isEmpty());
            c.slider->setAlpha (override.isEmpty() ? 1.0f : 0.45f);
            c.value->repaint();
        }
        if (c.combo != nullptr && ! c.combo->isPopupActive())
        {
            c.combo->setSelectedId ((int) std::lround (store->getParam (i)) + 1, juce::dontSendNotification);
            if (i == divisionParam)
                c.combo->setEnabled (synced);
        }
        if (c.toggle != nullptr)
            c.toggle->setToggleState (synced, juce::dontSendNotification);
    }

    power.setToggleState (! store->isBypassed(), juce::dontSendNotification);

    if (auto* block = dynamic_cast<DSPBlock*> (store.get()))
    {
        for (int slot = 0; slot < (int) fileSlots.size(); ++slot)
            if (fileSlots[(size_t) slot].shownPath != block->getFilePath (slot) && ! fileSlots[(size_t) slot].files->isPopupActive())
                populateFileSlot (slot);

        const auto newChips = block->getInfoChips();
        const auto status = juce::String (block->getStatusText());
        info.setVisible (status.isNotEmpty());
        info.setTooltip (status);
        if (newChips != chips)
        {
            chips = newChips;
            repaint (chipsArea);
        }
    }
}

void BlockPanel::paint (juce::Graphics& g)
{
    theme::drawCard (g, getLocalBounds().toFloat());

    // Left card.
    g.setColour (theme::panelRaised.withAlpha (0.45f));
    g.fillRoundedRectangle (leftCard.toFloat(), 12.0f);
    g.setColour (theme::border);
    g.drawRoundedRectangle (leftCard.toFloat().reduced (0.5f), 12.0f, 1.0f);

    // Output meter card.
    g.setColour (theme::panelRaised.withAlpha (0.45f));
    g.fillRoundedRectangle (meterCard.toFloat(), 12.0f);
    g.setColour (theme::border);
    g.drawRoundedRectangle (meterCard.toFloat().reduced (0.5f), 12.0f, 1.0f);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    g.drawText ("OUT", meterCard.withHeight (40), juce::Justification::centred);

    if (store == nullptr)
    {
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (17.0f));
        g.drawText ("Selecciona un bloque en el grid", leftCard.reduced (20), juce::Justification::centredLeft);
        return;
    }

    // Title: "Name - Type".
    const auto type = store->getType();
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    const juce::String title = juce::String (store->getName());
    g.drawText (title, titleArea, juce::Justification::centredLeft);
    const int tw = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), title + "  ");
    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (16.0f));
    if (getBlockTypeInfo (type).displayName != store->getName())
        g.drawText (juce::String ("(") + getBlockTypeInfo (type).displayName + ")", titleArea.withTrimmedLeft (tw),
                    juce::Justification::centredLeft);

    // Chips.
    auto chipRow = chipsArea;
    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (16.0f));
    g.drawText ("Tipo:", chipRow.removeFromLeft (52), juce::Justification::centredLeft);
    const auto colour = theme::categoryColour (type);
    for (size_t i = 0; i < chips.size(); ++i)
    {
        const juce::String text (chips[i]);
        const int w = juce::GlyphArrangement::getStringWidthInt (juce::Font (juce::FontOptions (13.0f)), text) + 26;
        if (w > chipRow.getWidth())
            break;
        const auto chipColour = i == 0 ? (text == "End-to-End" || text.startsWith ("Desacoplado") ? theme::warn : theme::good)
                                       : theme::accentLight;
        theme::drawChip (g, chipRow.removeFromLeft (w).toFloat().withSizeKeepingCentre ((float) w, 28.0f), text, chipColour);
        chipRow.removeFromLeft (8);
    }
    if (chips.empty())
        theme::drawChip (g, chipRow.removeFromLeft (120).toFloat().withSizeKeepingCentre (120.0f, 28.0f),
                         getBlockTypeInfo (type).displayName, colour);
}

void BlockPanel::resized()
{
    auto area = getLocalBounds().reduced (14);
    meterCard = area.removeFromRight (190);
    area.removeFromRight (14);
    const bool hasFiles = ! fileSlots.empty();
    leftCard = area.removeFromLeft (hasFiles ? 500 : (eqView != nullptr ? 300 : 380));
    area.removeFromLeft (18);
    knobArea = area;

    {
        auto meter = meterCard.reduced (18, 10);
        meter.removeFromTop (34);
        outputMeter.setBounds (meter);
    }

    auto left = leftCard.reduced (16, 14);
    auto top = left.removeFromTop (44);
    power.setBounds (top.removeFromLeft (44));
    top.removeFromLeft (12);
    remove.setBounds (top.removeFromRight (36).withSizeKeepingCentre (36, 36));
    titleArea = top;
    left.removeFromTop (10);

    for (auto& fs : fileSlots)
    {
        auto row = left.removeFromTop (44);
        if (tone3000Button != nullptr && &fs == &fileSlots.front())
        {
            tone3000Button->setBounds (row.removeFromRight (104).reduced (0, 4));
            row.removeFromRight (6);
        }
        fs.browse->setBounds (row.removeFromRight (48).reduced (2));
        row.removeFromRight (8);
        fs.files->setBounds (row);
        left.removeFromTop (8);
    }
    if (variants != nullptr && variants->isVisible())
    {
        auto row = left.removeFromTop (32);
        tone3000Credit->setBounds (row.removeFromRight (140));
        row.removeFromRight (8);
        variants->setBounds (row);
    }
    else if (tone3000Credit != nullptr)
        tone3000Credit->setBounds (left.removeFromTop (18).removeFromRight (150));

    auto chipRow = left.removeFromBottom (36);
    info.setBounds (chipRow.removeFromRight (36));
    chipsArea = chipRow;

    // Choice params (combo boxes) stack in a column on the right; knobs get the rest.
    if (eqView != nullptr)
    {
        eqView->setBounds (knobArea);
        return;
    }

    std::vector<ParamControl*> knobs, choices;
    for (auto& c : controls)
        (c.slider != nullptr ? knobs : choices).push_back (&c);

    if (! choices.empty())
    {
        auto column = knobArea.removeFromRight (170);
        knobArea.removeFromRight (12);
        const int rowHeight = std::min (64, column.getHeight() / (int) choices.size());
        auto stack = column.withSizeKeepingCentre (column.getWidth(), rowHeight * (int) choices.size());
        for (auto* c : choices)
        {
            auto row = stack.removeFromTop (rowHeight);
            c->name->setBounds (row.removeFromTop (22));
            c->name->setFont (juce::FontOptions (14.0f));
            if (c->combo != nullptr)  c->combo->setBounds (row.removeFromTop (34).reduced (4, 0));
            if (c->toggle != nullptr) c->toggle->setBounds (row.removeFromTop (34).reduced (4, 0));
        }
    }

    // Knobs: one row if possible, otherwise wrap; cells shrink to fit.
    const int count = (int) knobs.size();
    if (count == 0)
        return;
    const int cellWidth = std::clamp (knobArea.getWidth() / count, 84, 150);
    const int perRow = std::max (1, knobArea.getWidth() / cellWidth);
    const int rows = (count + perRow - 1) / perRow;
    const int cellHeight = knobArea.getHeight() / rows;
    const int usedWidth = std::min (count, perRow) * cellWidth;
    const int x0 = knobArea.getX() + (knobArea.getWidth() - usedWidth) / 2;

    for (int i = 0; i < count; ++i)
    {
        auto& c = *knobs[(size_t) i];
        auto cell = juce::Rectangle<int> (x0 + (i % perRow) * cellWidth, knobArea.getY() + (i / perRow) * cellHeight, cellWidth, cellHeight).reduced (6, 2);
        const int pillHeight = rows > 1 ? 22 : 30;
        const int nameHeight = rows > 1 ? 20 : 26;
        auto pillArea = cell.removeFromBottom (pillHeight);
        auto nameArea = cell.removeFromBottom (nameHeight);
        c.name->setBounds (nameArea);
        c.name->setFont (juce::FontOptions (rows > 1 ? 14.0f : 17.0f));
        const int knob = std::min (cell.getWidth(), cell.getHeight());
        c.slider->setBounds (cell.withSizeKeepingCentre (knob, knob));
        c.value->setBounds (pillArea.withSizeKeepingCentre (std::min (pillArea.getWidth() + 8, 128), pillHeight));
    }
}
} // namespace kumamp
