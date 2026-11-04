#include "GridView.h"
#include "../DSP/Cab/IrLoaderBlock.h"
#include "../Graph/BlockFactory.h"

namespace kumamp
{
namespace
{
    constexpr float kMarkerRadius = 10.0f;
    constexpr int kSerialBase = 1000, kParallelBase = 2000;

    juce::PopupMenu typesMenu (const std::vector<BlockType>& types, int idBase)
    {
        juce::PopupMenu menu;
        for (auto type : types)
            if (isImplemented (type))
                menu.addItem (idBase + (int) type, getBlockTypeInfo (type).displayName);
        return menu;
    }

    juce::PopupMenu groupsMenu (int group, int idBase)
    {
        const auto& groups = theme::paletteGroups();
        if (group >= 0)
            return typesMenu (groups[(size_t) group].types, idBase);

        juce::PopupMenu menu;
        for (const auto& g : groups)
            menu.addSubMenu (g.name, typesMenu (g.types, idBase));
        return menu;
    }

    bool isJunction (const GridNode& n) { return n.kind == NodeKind::Split || n.kind == NodeKind::Mix; }
} // namespace

GridView::GridView (GridEngine& engineToShow) : engine (engineToShow)
{
    setWantsKeyboardFocus (true);
    setTooltip ("Clic: editar  |  Clic derecho: bypass  |  Arrastrar: mover  |  +: insertar  |  Supr: quitar");
}

GridView::Layout GridView::computeLayout() const
{
    Layout layout;
    for (const auto& node : engine.getModel().getNodes())
    {
        layout.columns = std::max (layout.columns, node.column + 1);
        layout.rows    = std::max (layout.rows, node.row + 1);
    }
    const auto area = getLocalBounds().toFloat().reduced (24.0f, 20.0f);
    layout.cellWidth  = std::min (150.0f, area.getWidth() / (float) layout.columns);
    layout.cellHeight = std::min (140.0f, area.getHeight() / (float) layout.rows);
    // Centre the graph in the view.
    layout.origin = { area.getCentreX() - layout.cellWidth * (float) layout.columns * 0.5f,
                      area.getCentreY() - layout.cellHeight * (float) layout.rows * 0.5f };
    return layout;
}

juce::Rectangle<float> GridView::nodeBounds (const GridNode& node, const Layout& layout) const
{
    const auto cell = juce::Rectangle<float> (layout.origin.x + (float) node.column * layout.cellWidth,
                                              layout.origin.y + (float) node.row * layout.cellHeight,
                                              layout.cellWidth, layout.cellHeight);
    if (isJunction (node))
        return cell.withSizeKeepingCentre (24.0f, 24.0f);
    if (node.kind == NodeKind::Input || node.kind == NodeKind::Output)
        return cell.withSizeKeepingCentre (std::min (86.0f, cell.getWidth() - 12.0f), 44.0f);
    const float w = std::min (cell.getWidth() - 22.0f, 112.0f);
    const float h = std::min (cell.getHeight() - 30.0f, 96.0f);
    return cell.withSizeKeepingCentre (w, h);
}

juce::String GridView::nodeLabel (const GridNode& node) const
{
    if (node.kind == NodeKind::Input)  return "IN";
    if (node.kind == NodeKind::Output) return "OUT";
    const auto store = engine.getStore (node.store);
    return store != nullptr ? juce::String (store->getName()).toUpperCase() : juce::String ("?");
}

void GridView::selectNode (NodeId id)
{
    selectedNode = id;
    if (onNodeSelected)
    {
        const auto* node = engine.getModel().findNode (id);
        onNodeSelected (node != nullptr ? node->store : kInvalidStore);
    }
    repaint();
}

bool GridView::removeSelected()
{
    const auto* node = engine.getModel().findNode (selectedNode);
    if (node == nullptr || node->kind != NodeKind::Block)
        return false;

    juce::String error;
    if (! engine.removeBlock (selectedNode, &error))
    {
        if (onError) onError (error);
        return false;
    }
    selectNode (kInvalidNode);
    return true;
}

bool GridView::replaceSelected (BlockType type)
{
    juce::String error;
    if (! engine.replaceBlock (selectedNode, type, &error))
    {
        if (onError) onError (error);
        return false;
    }
    selectNode (selectedNode);   // the panel shows the new block
    return true;
}

GridView::InsertTarget GridView::defaultInsertTarget() const
{
    const auto& model = engine.getModel();
    // After the selected block (its first consumer)...
    if (const auto* selected = model.findNode (selectedNode); selected != nullptr && selected->kind == NodeKind::Block)
        for (const auto& n : model.getNodes())
            for (const auto& in : n.inputs)
                if (in.node == selectedNode)
                    return { in, n.id };

    // ... otherwise right before OUT.
    if (const auto* out = model.findNode (model.getOutputNode()); out != nullptr && ! out->inputs.empty())
        return { out->inputs[0], out->id };
    return {};
}

void GridView::insert (BlockType type, InsertTarget target, bool parallel)
{
    juce::String error;
    const NodeId added = engine.insertBlock (target.from, target.to, type, parallel, &error);
    if (added == kInvalidNode)
    {
        if (onError) onError (error);
        return;
    }
    selectNode (added);
}

void GridView::showBlockMenu (int group, InsertTarget target, juce::Rectangle<int> screenArea, bool offerParallel)
{
    if (target.to == kInvalidNode)
        return;

    juce::PopupMenu menu;
    if (offerParallel)
    {
        menu.addSectionHeader ("Insertar bloque");
        menu.addSubMenu ("En serie", groupsMenu (group, kSerialBase));
        menu.addSubMenu ("En paralelo (rama A/B)", groupsMenu (group, kParallelBase));
    }
    else
    {
        menu = groupsMenu (group, kSerialBase);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (screenArea),
                        [safe = juce::Component::SafePointer<GridView> (this), target] (int result)
                        {
                            if (safe == nullptr || result < kSerialBase)
                                return;
                            const bool parallel = result >= kParallelBase;
                            safe->insert ((BlockType) (result - (parallel ? kParallelBase : kSerialBase)), target, parallel);
                        });
}

//==============================================================================
void GridView::paintTile (juce::Graphics& g, const GridNode& node, juce::Rectangle<float> r, bool selected)
{
    const auto store = engine.getStore (node.store);
    const auto type = store != nullptr ? store->getType() : BlockType::Unknown;
    const auto* ir = dynamic_cast<const IrLoaderBlock*> (store.get());
    const bool decoupled = ir != nullptr && ir->isAutoBypassActive();
    const bool bypassed = store != nullptr && (store->isBypassed() || decoupled);
    const bool beingDragged = node.id == draggingNode;
    const auto colour = theme::categoryColour (type);
    const float alpha = beingDragged ? 0.25f : (bypassed ? 0.45f : 1.0f);

    if (selected && ! beingDragged)
        for (int i = 3; i >= 1; --i)   // soft glow
        {
            g.setColour (colour.withAlpha (0.07f * (float) (4 - i)));
            g.fillRoundedRectangle (r.expanded ((float) i * 3.0f), 12.0f + (float) i * 2.0f);
        }

    g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.22f * alpha), r.getX(), r.getY(),
                                             colour.withAlpha (0.08f * alpha), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (colour.withAlpha ((selected ? 1.0f : 0.75f) * alpha));
    g.drawRoundedRectangle (r.reduced (0.75f), 10.0f, selected ? 2.0f : 1.5f);

    auto content = r.reduced (6.0f);
    const auto labelArea = content.removeFromBottom (std::min (34.0f, content.getHeight() * 0.42f));
    theme::drawIcon (g, theme::iconFor (type), content.reduced (4.0f), colour.withAlpha (alpha), 1.8f);

    g.setColour (theme::text.withAlpha (alpha));
    g.setFont (juce::FontOptions (r.getWidth() < 90.0f ? 13.0f : 15.0f));
    juce::String label = nodeLabel (node);
    if (decoupled)      label << "\n(E2E)";
    else if (bypassed)  label << "\nOFF";
    g.drawFittedText (label, labelArea.toNearestInt(), juce::Justification::centred, 2, 0.85f);
}

void GridView::paint (juce::Graphics& g)
{
    // Subtle dot grid (the editor paints the surrounding card).
    const auto bounds = getLocalBounds().toFloat();
    g.saveState();
    g.reduceClipRegion (getLocalBounds().reduced (2));
    g.setColour (juce::Colours::white.withAlpha (0.035f));
    for (float y = 14.0f; y < bounds.getHeight(); y += 18.0f)
        for (float x = 14.0f; x < bounds.getWidth(); x += 18.0f)
            g.fillRect (x, y, 1.5f, 1.5f);
    g.restoreState();

    const auto layout = computeLayout();
    const auto& model = engine.getModel();
    markers.clear();

    // Cables.
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
            juce::Path cable;
            cable.startNewSubPath (a);
            if (std::abs (a.y - b.y) < 1.0f)
                cable.lineTo (b);
            else
            {
                // Orthogonal with rounded corners, like a patch-bay.
                const float midX = isJunction (*source) ? a.x + 10.0f : b.x - 10.0f;
                const float r = std::min (12.0f, std::abs (b.y - a.y) * 0.5f);
                const float dir = b.y > a.y ? 1.0f : -1.0f;
                cable.lineTo (midX - r, a.y);
                cable.quadraticTo (midX, a.y, midX, a.y + r * dir);
                cable.lineTo (midX, b.y - r * dir);
                cable.quadraticTo (midX, b.y, midX + r, b.y);
                cable.lineTo (b);
            }
            g.setColour (theme::cable.brighter (0.25f));
            g.strokePath (cable, juce::PathStrokeType (2.0f));
            for (auto p : { a, b })
            {
                g.setColour (theme::textDim);
                g.fillEllipse (p.x - 3.0f, p.y - 3.0f, 6.0f, 6.0f);
            }

            markers.push_back ({ cable.getPointAlongPath (cable.getLength() * 0.5f), { in, node.id } });
        }
    }

    // Insertion markers: visible on hover, highlighted under the pointer / drop position.
    if (mouseInside || dragHover)
        for (int i = 0; i < (int) markers.size(); ++i)
        {
            const auto c = markers[(size_t) i].centre;
            const bool hot = i == hoveredMarker;
            const float radius = hot ? kMarkerRadius + 2.0f : kMarkerRadius - 3.0f;
            g.setColour (hot ? theme::accent : theme::panelRaised.brighter (0.2f));
            g.fillEllipse (c.x - radius, c.y - radius, 2 * radius, 2 * radius);
            if (hot || ! dragHover)
                theme::drawIcon (g, theme::Icon::Plus, juce::Rectangle<float> (2 * radius, 2 * radius).withCentre (c).reduced (radius * 0.35f),
                                 hot ? juce::Colours::white : theme::textDim, 2.0f);
        }

    // Nodes.
    for (const auto& node : model.getNodes())
    {
        const auto r = nodeBounds (node, layout);
        const bool selected = node.id == selectedNode;

        if (isJunction (node))
        {
            g.setColour (theme::panel);
            g.fillEllipse (r);
            g.setColour (selected ? theme::accentLight : theme::textDim);
            g.drawEllipse (r.reduced (1.0f), 1.8f);
            theme::drawIcon (g, theme::Icon::Plus, r.reduced (5.0f), selected ? theme::accentLight : theme::text, 1.8f);
        }
        else if (node.kind == NodeKind::Input || node.kind == NodeKind::Output)
        {
            g.setColour (theme::panelRaised);
            g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
            g.setColour (theme::border.brighter (0.4f));
            g.drawRoundedRectangle (r.reduced (0.75f), r.getHeight() * 0.5f, 1.5f);
            g.setColour (theme::text);
            g.setFont (juce::FontOptions (17.0f));
            g.drawText (nodeLabel (node), r, juce::Justification::centred);
        }
        else
        {
            paintTile (g, node, r, selected);
        }
    }
}

int GridView::markerNear (juce::Point<float> position, float radius) const
{
    int best = -1;
    float bestDistance = radius;
    for (int i = 0; i < (int) markers.size(); ++i)
    {
        const auto& m = markers[(size_t) i];
        // A block cannot be dropped on its own input or output cable.
        if (draggingNode != kInvalidNode && (m.target.from.node == draggingNode || m.target.to == draggingNode))
            continue;
        const float d = m.centre.getDistanceFrom (position);
        if (d < bestDistance)
        {
            bestDistance = d;
            best = i;
        }
    }
    return best;
}

//==============================================================================
void GridView::mouseMove (const juce::MouseEvent& e)
{
    mouseInside = true;
    const int hovered = markerNear (e.position, kMarkerRadius + 4.0f);
    if (hovered != hoveredMarker)
    {
        hoveredMarker = hovered;
        setMouseCursor (hovered >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }
    repaint();
}

void GridView::mouseExit (const juce::MouseEvent&)
{
    mouseInside = false;
    hoveredMarker = -1;
    repaint();
}

void GridView::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    pressedNode = kInvalidNode;

    if (const int m = markerNear (e.position, kMarkerRadius + 4.0f); m >= 0 && ! e.mods.isPopupMenu())
    {
        const auto c = markers[(size_t) m].centre.toInt();
        showBlockMenu (-1, markers[(size_t) m].target, localAreaToGlobal (juce::Rectangle<int> (c.x, c.y, 1, 1)), true);
        return;
    }

    const auto layout = computeLayout();
    for (const auto& node : engine.getModel().getNodes())
    {
        if (! nodeBounds (node, layout).contains (e.position))
            continue;

        // Right-click (or ctrl-click): toggle bypass of audio blocks in place.
        // Like the panel toggle, it is a live edit until the scene is stored.
        if (e.mods.isPopupMenu())
        {
            if (node.kind == NodeKind::Block)
                if (auto store = engine.getStore (node.store))
                    store->setBypassed (! store->isBypassed());
            repaint();
            return;
        }

        pressedNode = node.kind == NodeKind::Block ? node.id : kInvalidNode;
        selectNode (node.id);
        return;
    }
}

void GridView::mouseDrag (const juce::MouseEvent& e)
{
    if (pressedNode == kInvalidNode || draggingNode != kInvalidNode || e.getDistanceFromDragStart() < 6)
        return;

    auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this);
    const auto* node = engine.getModel().findNode (pressedNode);
    if (container == nullptr || node == nullptr)
        return;

    const auto tile = nodeBounds (*node, computeLayout()).toNearestInt().expanded (2);
    const auto image = createComponentSnapshot (tile);
    draggingNode = pressedNode;
    container->startDragging ("node:" + juce::String ((int) draggingNode), this, juce::ScaledImage (image), true);
}

bool GridView::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        return removeSelected();
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'Z')
        return key.getModifiers().isShiftDown() ? engine.redo() : engine.undo();
    return false;
}

//== Drag & drop ===============================================================
bool GridView::isInterestedInDragSource (const SourceDetails& details)
{
    const auto description = details.description.toString();
    return description.startsWith ("node:") || description.startsWith ("group:");
}

void GridView::itemDragMove (const SourceDetails& details)
{
    dragHover = true;
    if (details.description.toString().startsWith ("node:"))
        draggingNode = (NodeId) details.description.toString().fromFirstOccurrenceOf (":", false, false).getIntValue();
    hoveredMarker = markerNear (details.localPosition.toFloat(), 70.0f);
    repaint();
}

void GridView::itemDragExit (const SourceDetails&)
{
    dragHover = false;
    hoveredMarker = -1;
    draggingNode = kInvalidNode;
    repaint();
}

void GridView::itemDropped (const SourceDetails& details)
{
    const auto description = details.description.toString();
    const int marker = markerNear (details.localPosition.toFloat(), 70.0f);
    const NodeId moving = draggingNode;
    dragHover = false;
    hoveredMarker = -1;
    draggingNode = kInvalidNode;

    if (marker < 0)
    {
        repaint();
        return;
    }

    const auto target = markers[(size_t) marker].target;
    const auto at = markers[(size_t) marker].centre.toInt();

    if (description.startsWith ("node:"))
    {
        juce::String error;
        if (! engine.moveBlock (moving, target.from, target.to, &error) && onError)
            onError (error);
        selectNode (moving);
    }
    else if (description.startsWith ("group:"))
    {
        const int group = description.fromFirstOccurrenceOf (":", false, false).getIntValue();
        const auto& types = theme::paletteGroups()[(size_t) group].types;
        if (types.size() == 1)
            insert (types.front(), target, false);
        else
            showBlockMenu (group, target, localAreaToGlobal (juce::Rectangle<int> (at.x, at.y, 1, 1)), false);
    }
    repaint();
}
} // namespace kumamp
