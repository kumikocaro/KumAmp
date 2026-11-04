#pragma once

#include "EqView.h"
#include "Widgets.h"
#include "../Tone3000/Tone3000.h"
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
    /** "Modelo" picked: swap the block for another of its family, in the same place. */
    std::function<void (BlockType)> onChangeType;
    /** Expand button of the graphic EQ: open it large. */
    std::function<void (std::shared_ptr<ParamStore>)> onExpandEq;
    /** "TONE3000" button of the NAM amp block: pick a tone on tone3000.com for this store. */
    std::function<void (std::shared_ptr<ParamStore>, juce::Component& source)> onBrowseTone3000;
    /** A not-yet-downloaded variant of the loaded TONE3000 tone was picked. */
    std::function<void (std::shared_ptr<ParamStore>, const t3k::Tone&, const t3k::Model&)> onTone3000Variant;

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
        bool relevant = true;   // DSPBlock::isParamRelevant (hidden when false)
    };

    struct FileSlot
    {
        std::unique_ptr<juce::ComboBox> files;
        std::unique_ptr<IconButton> browse;
        juce::Array<juce::File> choices;
        juce::Array<juce::File> library;   // NAM amp: every model downloaded from TONE3000
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
    std::unique_ptr<juce::TextButton> tone3000Button;
    std::unique_ptr<juce::HyperlinkButton> tone3000Credit;
    std::unique_ptr<juce::ComboBox> modelBox;          // the block's family (Chorus, Flanger, Phaser...)
    std::vector<BlockType> modelTypes;
    std::unique_ptr<juce::ComboBox> variants;          // other captures of the loaded TONE3000 tone
    t3k::Tone variantTone;
    juce::Array<t3k::Model> variantModels;
    juce::File variantFolder;
    void populateVariants (const juce::File& currentModel);
    int syncParam = -1, divisionParam = -1;
    LevelMeter outputMeter { 2, true };

    juce::Rectangle<int> leftCard, knobArea, meterCard, titleArea, chipsArea;
};
} // namespace kumamp
