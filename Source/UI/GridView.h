#pragma once

#include "Theme.h"
#include "../Engine/GridEngine.h"

#include <functional>
#include <vector>

namespace kumamp
{
/** Visualises and edits the routing grid.

    - Click a block: select it (its controls open in the panel).
    - Right-click a block: toggle bypass.
    - Drag a block onto another cable: move it there (same instance and state).
    - Drag a palette family onto a cable: pick a block and insert it there.
    - "+" on a cable (shown on hover): insert a block in series or in parallel.
    - Delete / Backspace: remove the selected block.
    Layout is automatic (GridModel::autoLayout). */
class GridView final : public juce::Component,
                       public juce::DragAndDropTarget,
                       public juce::SettableTooltipClient
{
public:
    struct InsertTarget { PortRef from; NodeId to = kInvalidNode; };

    explicit GridView (GridEngine& engineToShow);

    std::function<void (StoreId)> onNodeSelected;
    std::function<void (const juce::String&)> onError;

    NodeId getSelectedNode() const noexcept { return selectedNode; }
    void selectNode (NodeId id);
    bool removeSelected();
    /** Swaps the selected block for another type in the same place (its variants). */
    bool replaceSelected (BlockType type);

    /** Where a block picked from the palette goes: after the selected block, or before OUT. */
    InsertTarget defaultInsertTarget() const;
    /** Inserts a new block on target and selects it (errors go to onError). */
    void insert (BlockType type, InsertTarget target, bool parallel);
    /** Shows the block menu for a palette group (or all groups if group < 0) and inserts on target. */
    void showBlockMenu (int group, InsertTarget target, juce::Rectangle<int> screenArea, bool offerParallel);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

private:
    struct Layout { int columns = 1, rows = 1; float cellWidth = 0, cellHeight = 0; juce::Point<float> origin; };
    struct InsertMarker { juce::Point<float> centre; InsertTarget target; };

    Layout computeLayout() const;
    juce::Rectangle<float> nodeBounds (const GridNode& node, const Layout& layout) const;
    juce::String nodeLabel (const GridNode& node) const;
    int markerNear (juce::Point<float> position, float radius) const;
    void paintTile (juce::Graphics&, const GridNode&, juce::Rectangle<float>, bool selected);

    GridEngine& engine;
    NodeId selectedNode = kInvalidNode, pressedNode = kInvalidNode, draggingNode = kInvalidNode;
    std::vector<InsertMarker> markers;   // rebuilt on every paint
    int hoveredMarker = -1;
    bool mouseInside = false, dragHover = false;
};
} // namespace kumamp
