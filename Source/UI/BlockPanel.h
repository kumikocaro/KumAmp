#pragma once

#include "../Graph/ParamStore.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace kumamp
{
/** Auto-generated editor for any ParamStore: one rotary per parameter plus
    bypass. Writes go straight to the store's atomics (GUI thread). */
class BlockPanel final : public juce::Component
{
public:
    BlockPanel();

    void setStore (std::shared_ptr<ParamStore> newStore);
    StoreId getStoreId() const noexcept { return store != nullptr ? store->getId() : kInvalidStore; }

    /** Pull current values (scenes, macros, automation) into the controls. */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    std::shared_ptr<ParamStore> store;
    juce::Label title;
    juce::ToggleButton bypass { "Bypass" };
    juce::OwnedArray<juce::Slider> sliders;
    juce::OwnedArray<juce::Label> labels;
};
} // namespace kumamp
