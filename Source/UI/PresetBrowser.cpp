#include "PresetBrowser.h"
#include "../Presets/FactoryPresets.h"
#include "UserSettings.h"

#include <map>

namespace kumamp
{
namespace
{
    juce::String utf8 (const char* text) { return juce::String (juce::CharPointer_UTF8 (text)); }

    void styleField (juce::TextEditor& field, const juce::String& placeholder)
    {
        field.setTextToShowWhenEmpty (placeholder, theme::textDim);
        field.setFont (juce::FontOptions (15.0f));
        field.setIndents (10, 7);
    }

    void drawTitle (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title, const juce::String& subtitle)
    {
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        g.drawText (title, area.removeFromTop (28), juce::Justification::centredLeft);
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (subtitle, area.removeFromTop (20), juce::Justification::centredLeft, true);
    }
} // namespace

//==============================================================================
PresetBar::PresetBar (PresetManager& m) : manager (m)
{
    previous.onClick = [this] { step (-1); };
    next.onClick     = [this] { step (+1); };
    current.onClick  = [this] { showPresetMenu(); };
    save.onClick     = [this] { if (onSave) onSave(); };
    undo.onClick     = [this] { if (onUndo) onUndo(); };
    redo.onClick     = [this] { if (onRedo) onRedo(); };
    menu.onClick     = [this] { if (onMenu) onMenu (menu); };

    current.setColour (juce::TextButton::buttonColourId, theme::panelRaised);
    current.setTooltip ("Elegir preset");
    for (juce::Component* c : { (juce::Component*) &previous, (juce::Component*) &current, (juce::Component*) &next,
                                (juce::Component*) &save, (juce::Component*) &undo, (juce::Component*) &redo, (juce::Component*) &menu })
        addAndMakeVisible (c);
    refresh();
}

void PresetBar::setUndoState (bool canUndo, bool canRedo)
{
    undo.setEnabled (canUndo);
    redo.setEnabled (canRedo);
}

void PresetBar::refresh()
{
    if (manager.getCurrentFile() == shownFile && manager.getCurrentName() == shownName)
        return;
    shownFile = manager.getCurrentFile();
    shownName = manager.getCurrentName();
    shownAuthor = shownFile.existsAsFile() ? PresetManager::readInfo (shownFile).author : juce::String();
    current.setButtonText (shownName.isNotEmpty() ? shownName : utf8 ("Sin preset \xe2\x80\x94 eleg\xc3\xad uno o guard\xc3\xa1 el tuyo"));
    repaint();
}

void PresetBar::step (int delta)
{
    const auto presets = manager.scan();
    if (presets.empty())
    {
        if (onShowAll) onShowAll();
        return;
    }
    int index = -1;
    for (int i = 0; i < (int) presets.size(); ++i)
        if (presets[(size_t) i].file == manager.getCurrentFile())
            index = i;
    const int count = (int) presets.size();
    index = index < 0 ? (delta > 0 ? 0 : count - 1) : (index + delta + count) % count;
    if (onLoad) onLoad (presets[(size_t) index].file);
}

void PresetBar::showPresetMenu()
{
    const auto presets = manager.scan();
    juce::PopupMenu menuOfPresets;
    std::map<juce::String, juce::PopupMenu> categories;
    for (int i = 0; i < (int) presets.size(); ++i)
    {
        const auto& p = presets[(size_t) i];
        const auto label = p.name + (p.author.isNotEmpty() ? utf8 ("   \xc2\xb7 ") + p.author : juce::String());
        const bool ticked = p.file == manager.getCurrentFile();
        if (p.category.isEmpty())
            menuOfPresets.addItem (i + 1, label, true, ticked);
        else
            categories[p.category].addItem (i + 1, label, true, ticked);
    }
    for (auto& [category, sub] : categories)
        menuOfPresets.addSubMenu (category, sub);
    if (presets.empty())
        menuOfPresets.addItem (-1, utf8 ("Todav\xc3\xad" "a no hay presets"), false);
    menuOfPresets.addSeparator();
    menuOfPresets.addItem (100000, utf8 ("Ver todos / guardar como\xe2\x80\xa6"));

    menuOfPresets.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&current).withMinimumWidth (current.getWidth()),
                                 [safe = juce::Component::SafePointer<PresetBar> (this), presets] (int chosen)
    {
        if (safe == nullptr || chosen == 0)
            return;
        if (chosen == 100000)
        {
            if (safe->onShowAll) safe->onShowAll();
        }
        else if (chosen > 0 && chosen <= (int) presets.size() && safe->onLoad)
            safe->onLoad (presets[(size_t) chosen - 1].file);
    });
}

void PresetBar::paint (juce::Graphics& g)
{
    g.setColour (theme::border);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("PRESET", captionArea, juce::Justification::centredLeft);
    if (shownAuthor.isNotEmpty())
    {
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("por " + shownAuthor, captionArea.translated (0, 16), juce::Justification::centredLeft, true);
    }
}

void PresetBar::resized()
{
    auto area = getLocalBounds().reduced (24, 10);
    captionArea = area.removeFromLeft (88).withHeight (area.getHeight() / 2).translated (0, 2);
    menu.setBounds (area.removeFromRight (44));
    area.removeFromRight (24);
    redo.setBounds (area.removeFromRight (40));
    undo.setBounds (area.removeFromRight (40));
    area.removeFromRight (24);
    save.setBounds (area.removeFromRight (44));
    area.removeFromRight (16);

    previous.setBounds (area.removeFromLeft (36));
    const int width = std::min (area.getWidth() - 44, 520);
    current.setBounds (area.removeFromLeft (width).reduced (4, 2));
    next.setBounds (area.removeFromLeft (36));
}

PresetBrowser::PresetBrowser (PresetManager& m) : manager (m)
{
    {
        auto settings = openUserSettings();
        authorField.setText (settings->getValue ("presetAuthor"), false);
        const juce::File custom (settings->getValue ("presetFolder"));
        if (custom.isDirectory())
            manager.setFolder (custom);
    }

    styleField (search, utf8 ("Buscar por nombre, autor o categor\xc3\xad" "a"));
    styleField (nameField, "Nombre del preset");
    styleField (authorField, "Autor");
    styleField (commentField, "Notas (opcional): guitarra, pastillas, estilo...");
    search.onTextChange = [this] { applyFilter(); };
    nameField.onReturnKey = [this] { saveCurrent(); };

    categoryField.setEditableText (true);
    categoryField.setTextWhenNothingSelected (utf8 ("Categor\xc3\xad" "a / carpeta (opcional)"));

    list.setRowHeight (44);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);

    loadButton.onClick         = [this] { loadSelected(); };
    deleteButton.onClick       = [this] { deleteSelected(); };
    saveButton.onClick         = [this] { saveCurrent(); };
    openFolderButton.onClick   = [this] { manager.getFolder().createDirectory(); manager.getFolder().startAsProcess(); };
    changeFolderButton.onClick = [this] { chooseFolder(); };
    saveButton.setColour (juce::TextButton::buttonColourId, theme::accent);
    deleteButton.setTooltip ("Mueve el preset a la papelera");
    openFolderButton.setTooltip ("Abre la carpeta de presets en el explorador");

    for (juce::Component* c : { (juce::Component*) &search, (juce::Component*) &list, (juce::Component*) &loadButton,
                                (juce::Component*) &deleteButton, (juce::Component*) &openFolderButton,
                                (juce::Component*) &changeFolderButton, (juce::Component*) &nameField,
                                (juce::Component*) &authorField, (juce::Component*) &categoryField,
                                (juce::Component*) &commentField, (juce::Component*) &saveButton })
        addAndMakeVisible (c);

    refresh();
}

PresetBrowser::~PresetBrowser()
{
    list.setModel (nullptr);
}

void PresetBrowser::visibilityChanged()
{
    if (isVisible())
        refresh();
}

void PresetBrowser::refresh()
{
    const auto previous = selectedPreset() != nullptr ? selectedPreset()->file : manager.getCurrentFile();
    if (manager.getFolder() == PresetManager::getDefaultFolder())
        factorypresets::install (manager.getFolder());
    all = manager.scan();

    const auto typed = categoryField.getText();
    categoryField.clear (juce::dontSendNotification);
    juce::StringArray categories;
    for (const auto& p : all)
        if (p.category.isNotEmpty())
            categories.addIfNotAlreadyThere (p.category);
    categories.sortNatural();
    categoryField.addItemList (categories, 1);
    categoryField.setText (typed, juce::dontSendNotification);

    applyFilter();
    for (int row = 0; row < (int) visible.size(); ++row)
        if (all[(size_t) visible[(size_t) row]].file == previous)
            list.selectRow (row);
}

void PresetBrowser::applyFilter()
{
    const auto query = search.getText().trim();
    visible.clear();
    for (int i = 0; i < (int) all.size(); ++i)
    {
        const auto& p = all[(size_t) i];
        if (query.isEmpty() || p.name.containsIgnoreCase (query) || p.author.containsIgnoreCase (query)
            || p.category.containsIgnoreCase (query))
            visible.push_back (i);
    }
    list.updateContent();
    list.repaint();
    selectedRowsChanged (list.getSelectedRow());
}

const PresetManager::Info* PresetBrowser::selectedPreset() const
{
    const int row = list.getSelectedRow();
    return row >= 0 && row < (int) visible.size() ? &all[(size_t) visible[(size_t) row]] : nullptr;
}

int PresetBrowser::getNumRows()
{
    return (int) visible.size();
}

void PresetBrowser::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= (int) visible.size())
        return;
    const auto& p = all[(size_t) visible[(size_t) row]];
    auto area = juce::Rectangle<int> (width, height).reduced (4, 3);

    if (selected)
    {
        g.setColour (theme::accent.withAlpha (0.25f));
        g.fillRect (area.toFloat());
    }
    area.reduce (12, 0);

    if (p.file == manager.getCurrentFile())
    {
        g.setColour (theme::good);
        g.fillEllipse (area.removeFromLeft (8).withSizeKeepingCentre (8, 8).toFloat());
        area.removeFromLeft (8);
    }

    if (p.category.isNotEmpty())
    {
        const float chipWidth = juce::jlimit (60.0f, 180.0f, 16.0f + 7.5f * (float) p.category.length());
        theme::drawChip (g, area.removeFromRight ((int) chipWidth).withSizeKeepingCentre ((int) chipWidth, 24).toFloat(),
                         p.category, theme::accentLight);
        area.removeFromRight (8);
    }

    g.setColour (theme::text);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText (p.name, area.removeFromTop (area.getHeight() / 2 + 2), juce::Justification::bottomLeft, true);
    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (p.author.isNotEmpty() ? "por " + p.author : juce::String ("sin autor"), area, juce::Justification::topLeft, true);
}

void PresetBrowser::listBoxItemDoubleClicked (int, const juce::MouseEvent&) { loadSelected(); }
void PresetBrowser::returnKeyPressed (int)                                   { loadSelected(); }
void PresetBrowser::deleteKeyPressed (int)                                   { deleteSelected(); }

void PresetBrowser::selectedRowsChanged (int)
{
    const bool any = selectedPreset() != nullptr;
    loadButton.setEnabled (any);
    deleteButton.setEnabled (any);
    repaint (detailsArea);
}

void PresetBrowser::loadSelected()
{
    const auto* preset = selectedPreset();
    if (preset == nullptr)
        return;
    const auto file = preset->file;
    const auto result = manager.load (file);
    if (result.failed())
    {
        if (onMessage) onMessage ("Error: " + result.getErrorMessage());
        return;
    }
    // Pre-fill the save form so "save" updates this preset.
    const auto info = PresetManager::readInfo (file);
    nameField.setText (info.name, false);
    commentField.setText (info.comment, false);
    const auto parent = file.getParentDirectory();
    categoryField.setText (parent == manager.getFolder() ? juce::String() : parent.getRelativePathFrom (manager.getFolder()),
                           juce::dontSendNotification);
    list.repaint();
    if (onLoaded) onLoaded (manager.getCurrentName());
}

void PresetBrowser::deleteSelected()
{
    const auto* preset = selectedPreset();
    if (preset == nullptr)
        return;
    const auto file = preset->file;
    const auto name = preset->name;
    auto options = juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon,
                                                                 "Borrar preset",
                                                                 utf8 ("\xc2\xbf" "Mover \"") + name + "\" a la papelera?",
                                                                 "Borrar", "Cancelar", this);
    juce::AlertWindow::showAsync (options, [safe = juce::Component::SafePointer<PresetBrowser> (this), file, name] (int result)
    {
        if (safe == nullptr || result != 1)
            return;
        if (safe->manager.remove (file))
        {
            if (safe->onMessage) safe->onMessage ("\"" + name + "\" movido a la papelera");
        }
        else if (safe->onMessage)
            safe->onMessage ("Error: no se pudo borrar " + file.getFileName());
        safe->refresh();
    });
}

void PresetBrowser::saveCurrent()
{
    const auto name = nameField.getText().trim();
    if (name.isEmpty())
    {
        if (onMessage) onMessage ("Escribe un nombre para el preset");
        nameField.grabKeyboardFocus();
        return;
    }
    rememberAuthor();

    const auto category = categoryField.getText().trim();
    auto doSave = [safe = juce::Component::SafePointer<PresetBrowser> (this), name, category]
    {
        if (safe == nullptr)
            return;
        const auto result = safe->manager.save (name, safe->authorField.getText(), category, safe->commentField.getText());
        if (safe->onMessage)
            safe->onMessage (result.wasOk() ? "Preset \"" + name + "\" guardado" : "Error: " + result.getErrorMessage());
        safe->refresh();
    };

    const auto target = manager.fileFor (name, category);
    if (! target.existsAsFile() || target == manager.getCurrentFile())
    {
        doSave();
        return;
    }
    auto options = juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::WarningIcon,
                                                                 "Reemplazar preset",
                                                                 "Ya existe \"" + name + "\". " + utf8 ("\xc2\xbf") + "Reemplazarlo?",
                                                                 "Reemplazar", "Cancelar", this);
    juce::AlertWindow::showAsync (options, [doSave] (int result) { if (result == 1) doSave(); });
}

void PresetBrowser::rememberAuthor()
{
    auto settings = openUserSettings();
    settings->setValue ("presetAuthor", authorField.getText().trim());
    settings->saveIfNeeded();
}

void PresetBrowser::chooseFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Carpeta de presets", manager.getFolder());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safe = juce::Component::SafePointer<PresetBrowser> (this)] (const juce::FileChooser& fc)
    {
        if (safe == nullptr)
            return;
        const auto dir = fc.getResult();
        if (! dir.isDirectory())
            return;
        safe->manager.setFolder (dir);
        auto settings = openUserSettings();
        settings->setValue ("presetFolder", dir.getFullPathName());
        settings->saveIfNeeded();
        safe->refresh();
        safe->repaint();
    });
}

void PresetBrowser::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
    theme::drawCard (g, listCard.toFloat());
    theme::drawCard (g, saveCard.toFloat());

    drawTitle (g, listCard.reduced (20, 14).withRight (openFolderButton.getX() - 12), "PRESETS",
               juce::String ((int) all.size()) + " en " + manager.getFolder().getFullPathName());
    drawTitle (g, saveCard.reduced (20, 14), "GUARDAR",
               manager.getCurrentName().isNotEmpty() ? "Actual: " + manager.getCurrentName()
                                                     : utf8 ("Guarda la cadena, las 4 escenas, gate y pitch"));

    if (all.empty())
    {
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (15.0f));
        g.drawFittedText (utf8 ("Todav\xc3\xad" "a no hay presets. Arma tu sonido y gu\xc3\xa1rdalo con el formulario de la derecha.\n"
                                "Las subcarpetas se muestran como categor\xc3\xad" "as (por ejemplo, un pack de autor)."),
                          list.getBounds().reduced (20), juce::Justification::centred, 3);
    }

    if (const auto* p = selectedPreset(); p != nullptr && ! detailsArea.isEmpty())
    {
        juce::String details = p->author.isNotEmpty() ? "por " + p->author : juce::String ("sin autor");
        const auto dot = utf8 ("  \xc2\xb7  ");
        if (p->created != juce::Time())
            details << dot << p->created.formatted ("%d/%m/%Y");
        if (p->comment.isNotEmpty())
            details << dot << p->comment;
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (details, detailsArea, juce::Justification::centredLeft, true);
    }

    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawFittedText (utf8 ("Los modelos .nam e IRs se guardan como ruta. Para compartir un pack, copia la subcarpeta "
                            "con sus .nam / .wav dentro: KumAmp los encuentra solo."),
                      fieldsArea.withTop (saveButton.getBottom() + 10), juce::Justification::topLeft, 3);
}

void PresetBrowser::resized()
{
    auto area = getLocalBounds();
    saveCard = area.removeFromRight (juce::jlimit (300, 420, area.getWidth() * 36 / 100));
    area.removeFromRight (14);
    listCard = area;

    {
        auto a = listCard.reduced (20, 14);
        auto header = a.removeFromTop (48);
        changeFolderButton.setBounds (header.removeFromRight (150).withSizeKeepingCentre (150, 32));
        header.removeFromRight (8);
        openFolderButton.setBounds (header.removeFromRight (124).withSizeKeepingCentre (124, 32));
        a.removeFromTop (6);
        search.setBounds (a.removeFromTop (36));
        a.removeFromTop (8);

        auto footer = a.removeFromBottom (36);
        deleteButton.setBounds (footer.removeFromRight (96));
        footer.removeFromRight (8);
        loadButton.setBounds (footer.removeFromRight (110));
        footer.removeFromRight (12);
        detailsArea = footer;
        a.removeFromBottom (8);
        list.setBounds (a);
    }
    {
        auto a = saveCard.reduced (20, 14);
        a.removeFromTop (54);
        fieldsArea = a;
        for (juce::Component* field : { (juce::Component*) &nameField, (juce::Component*) &authorField,
                                        (juce::Component*) &categoryField, (juce::Component*) &commentField })
        {
            field->setBounds (a.removeFromTop (36));
            a.removeFromTop (10);
        }
        saveButton.setBounds (a.removeFromTop (38));
    }
}
} // namespace kumamp
