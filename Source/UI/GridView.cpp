#include "GridView.h"
#include "../DSP/Cab/IrLoaderBlock.h"

namespace kumamp
{
GridView::Layout GridView::computeLayout() const
{
    Layout layout;
    for (const auto& node : engine.getModel().getNodes())
    {
        layout.columns = std::max (layout.columns, node.column + 1);
        layout.rows    = std::max (layout.rows, node.row + 1);
    }
    layout.cellWidth  = (float) getWidth() / (float) layout.columns;
    layout.cellHeight = std::min (120.0f, (float) getHeight() / (float) layout.rows);
    return layout;
}

juce::Rectangle<float> GridView::nodeBounds (const GridNode& node, const Layout& layout) const
{
    return juce::Rectangle<float> ((float) node.column * layout.cellWidth, (float) node.row * layout.cellHeight,
                                   layout.cellWidth, layout.cellHeight)
        .reduced (layout.cellWidth * 0.12f, layout.cellHeight * 0.18f);
}

juce::Colour GridView::nodeColour (const GridNode& node) const
{
    switch (node.kind)
    {
        case NodeKind::Input:
        case NodeKind::Output: return juce::Colour (0xff3a3f4b);
        case NodeKind::Split:
        case NodeKind::Mix:    return juce::Colour (0xff5a5f6e);
        case NodeKind::Block:  break;
    }

    const auto store = engine.getStore (node.store);
    if (store == nullptr)
        return juce::Colours::darkred;

    switch (getBlockTypeInfo (store->getType()).category)
    {
        case BlockCategory::Input:      return juce::Colour (0xff4f8a8b);
        case BlockCategory::PitchPre:   return juce::Colour (0xff8e5bd6);
        case BlockCategory::Amp:        return juce::Colour (0xffd65b5b);
        case BlockCategory::Cab:        return juce::Colour (0xffb07a4a);
        case BlockCategory::Dynamics:   return juce::Colour (0xff4a90d6);
        case BlockCategory::Modulation: return juce::Colour (0xff4ab07a);
        case BlockCategory::Time:       return juce::Colour (0xffd6a64a);
        case BlockCategory::Utility:
        case BlockCategory::Routing:    break;
    }
    return juce::Colour (0xff6e7385);
}

juce::String GridView::nodeLabel (const GridNode& node) const
{
    if (node.kind == NodeKind::Input)  return "IN";
    if (node.kind == NodeKind::Output) return "OUT";

    const auto store = engine.getStore (node.store);
    return store != nullptr ? juce::String (store->getName()) : juce::String ("?");
}

void GridView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff14161b));

    const auto layout = computeLayout();
    const auto& model = engine.getModel();

    // Cables first, so nodes are drawn on top.
    g.setColour (juce::Colour (0xff9aa3b5));
    for (const auto& node : model.getNodes())
    {
        const auto to = nodeBounds (node, layout);
        for (const auto& in : node.inputs)
        {
            const auto* source = model.findNode (in.node);
            if (source == nullptr)
                continue;

            const auto from = nodeBounds (*source, layout);
            const juce::Point<float> a { from.getRight(), from.getCentreY() };
            const juce::Point<float> b { to.getX(), to.getCentreY() };
            const float bend = (b.x - a.x) * 0.5f;

            juce::Path cable;
            cable.startNewSubPath (a);
            cable.cubicTo (a.translated (bend, 0.0f), b.translated (-bend, 0.0f), b);
            g.strokePath (cable, juce::PathStrokeType (2.0f));
        }
    }

    for (const auto& node : model.getNodes())
    {
        const auto r = nodeBounds (node, layout);
        const auto store = engine.getStore (node.store);
        const auto* ir = dynamic_cast<const IrLoaderBlock*> (store.get());
        const bool decoupled = ir != nullptr && ir->isAutoBypassActive();
        const bool bypassed = store != nullptr && node.kind == NodeKind::Block && (store->isBypassed() || decoupled);

        g.setColour (nodeColour (node).withMultipliedAlpha (bypassed ? 0.35f : 1.0f));
        g.fillRoundedRectangle (r, 6.0f);

        if (node.id == selectedNode)
        {
            g.setColour (juce::Colours::white);
            g.drawRoundedRectangle (r, 6.0f, 2.0f);
        }

        g.setColour (juce::Colours::white.withAlpha (bypassed ? 0.5f : 0.95f));
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText (nodeLabel (node) + (decoupled ? "\n(E2E)" : ""), r.toNearestInt().reduced (4),
                          juce::Justification::centred, 2);
    }
}

void GridView::mouseDown (const juce::MouseEvent& e)
{
    const auto layout = computeLayout();
    for (const auto& node : engine.getModel().getNodes())
    {
        if (nodeBounds (node, layout).contains (e.position))
        {
            selectedNode = node.id;
            if (onNodeSelected)
                onNodeSelected (node.store);
            repaint();
            return;
        }
    }
}
} // namespace kumamp
