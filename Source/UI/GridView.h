#pragma once

#include "../Engine/GridEngine.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace kumamp
{
/** Visualises the routing grid (nodes at row/column, cables between ports).
    Left-click selects a node, right-click toggles a block's bypass.
    Bypassed blocks are drawn dimmed. */
class GridView final : public juce::Component,
                       public juce::SettableTooltipClient
{
public:
    explicit GridView (GridEngine& engineToShow) : engine (engineToShow)
    {
        setTooltip ("Clic: editar bloque  |  Clic derecho: bypass");
    }

    std::function<void (StoreId)> onNodeSelected;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    struct Layout { int columns = 1, rows = 1; float cellWidth = 0, cellHeight = 0; };

    Layout computeLayout() const;
    juce::Rectangle<float> nodeBounds (const GridNode& node, const Layout& layout) const;
    juce::Colour nodeColour (const GridNode& node) const;
    juce::String nodeLabel (const GridNode& node) const;

    GridEngine& engine;
    NodeId selectedNode = kInvalidNode;
};
} // namespace kumamp
