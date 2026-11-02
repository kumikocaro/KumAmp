#pragma once

#include "../Presets/PresetManager.h"
#include "Widgets.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace kumamp
{
/**
    Top of the centre card: the current preset, previous / next through the
    preset folder, a menu of every preset by category, save, undo / redo and
    the main menu.
*/
class PresetBar final : public juce::Component
{
public:
    explicit PresetBar (PresetManager& manager);

    std::function<void (const juce::File&)> onLoad;
    std::function<void()> onSave, onShowAll, onUndo, onRedo;
    std::function<void (juce::Component&)> onMenu;

    void setUndoState (bool canUndo, bool canRedo);
    /** Re-reads the current preset name (cheap; call from the editor timer). */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void step (int delta);
    void showPresetMenu();

    PresetManager& manager;
    IconButton previous { theme::Icon::ChevronLeft, "Preset anterior" }, next { theme::Icon::ChevronRight, "Preset siguiente" };
    juce::TextButton current;
    IconButton save { theme::Icon::Save, "Guardar preset" }, undo { theme::Icon::Undo, "Deshacer" },
               redo { theme::Icon::Redo, "Rehacer" }, menu { theme::Icon::Menu, "Menu" };
    juce::String shownName, shownAuthor;
    juce::File shownFile;
    juce::Rectangle<int> captionArea;
};

/**
    PRESETS tab: the preset folder as a searchable list (sub-folders are
    categories), load / delete, and a "save as" form with name, author,
    category and notes. The author and a custom folder are remembered per user.
*/
class PresetBrowser final : public juce::Component,
                            private juce::ListBoxModel
{
public:
    explicit PresetBrowser (PresetManager& manager);
    ~PresetBrowser() override;

    /** After a successful load (the editor resets scene / selection). */
    std::function<void (const juce::String& name)> onLoaded;
    std::function<void (const juce::String& message)> onMessage;

    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void deleteKeyPressed (int lastRowSelected) override;
    void returnKeyPressed (int lastRowSelected) override;

    void applyFilter();
    const PresetManager::Info* selectedPreset() const;
    void loadSelected();
    void deleteSelected();
    void saveCurrent();
    void chooseFolder();
    void rememberAuthor();

    PresetManager& manager;
    std::vector<PresetManager::Info> all;
    std::vector<int> visible;   // indices into `all`

    juce::TextEditor search;
    juce::ListBox list { "presets", this };
    juce::TextButton loadButton { "Cargar" }, deleteButton { "Borrar" };
    juce::TextButton openFolderButton { "Abrir carpeta" }, changeFolderButton { "Cambiar carpeta..." };

    juce::TextEditor nameField, authorField, commentField;
    juce::ComboBox categoryField;
    juce::TextButton saveButton { "Guardar preset" };

    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> listCard, saveCard, detailsArea, fieldsArea;
};
} // namespace kumamp
