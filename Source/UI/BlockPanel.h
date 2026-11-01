#pragma once

#include "EqView.h"
#include "Widgets.h"
#include "../Graph/ParamStore.h"

#include <functional>
#include <memory>
#include <vector>

namespace kumamp
{
/** Bottom panel: editor for the selected block (auto-generated from its
    ParamInfo table) plus the global output meter.

    Left card: power (bypass), name, file selectors (files of the same folder
    for quick switching, "..." for any file), info chips, remove.
    Centre: big knobs with value pills, or choice boxes for "A|B" params. */
class BlockPanel final : public juce::Component
{
public:
    BlockPanel();

    void setStore (std::shared_ptr<ParamStore> newStore);
    StoreId getStoreId() const noexcept { return store != nullptr ? store->getId() : kInvalidStore; }

    /** Pull current values (scenes, macros, automation, file loads) into the controls. */
    void refresh();

    LevelMeter& getOutputMeter() noexcept { return outputMeter; }

    /** "Quitar" pressed: remove the block from the grid. */
    std::function<void()> onRemove;
    /** Expand button of the graphic EQ: open it large. */
    std::function<void (std::shared_ptr<ParamStore>)> onExpandEq;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Pill final : public juce::Component
    {
        juce::String text;
        void paint (juce::Graphics&) override;
    };

    struct ParamControl
    {
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::ComboBox> combo;
        std::unique_ptr<juce::TextButton> toggle;   // "sync"
        std::unique_ptr<juce::Label> name;
        std::unique_ptr<Pill> value;
    };

    struct FileSlot
    {
        std::unique_ptr<juce::ComboBox> files;
        std::unique_ptr<IconButton> browse;
        juce::Array<juce::File> choices;
        std::string shownPath;
    };

    void chooseFile (int slot);
    void populateFileSlot (int slot);

    std::shared_ptr<ParamStore> store;
    PowerButton power;
    IconButton info { theme::Icon::Info }, remove { theme::Icon::Trash, "Quitar bloque (Supr)" };
    std::vector<ParamControl> controls;
    std::vector<FileSlot> fileSlots;
    std::vector<std::string> chips;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<EqView> eqView;
    int syncParam = -1, divisionParam = -1;
    LevelMeter outputMeter { 2, true };

    juce::Rectangle<int> leftCard, knobArea, meterCard, titleArea, chipsArea;
};
} // namespace kumamp
