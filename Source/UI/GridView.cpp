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
    const float alpha = beingDragged ? 0.25f : (bypassed && ! selected ? 0.55f : 1.0f);

    // Selected: black box. On: solid family colour. Off: only the outline, on the bone field.
    if (selected)
        g.setColour (theme::ink.withAlpha (alpha));
    else
        g.setColour (bypassed ? juce::Colours::transparentBlack : colour.withAlpha (alpha));
    g.fillRect (r);
    g.setColour (theme::ink.withAlpha (alpha));
    g.drawRect (r, 1.5f);

    const auto foreground = selected ? theme::bone : theme::ink.withAlpha (alpha);
    auto content = r.reduced (6.0f);
    const auto labelArea = content.removeFromBottom (std::min (34.0f, content.getHeight() * 0.42f));
    theme::drawIcon (g, theme::iconFor (type), content.reduced (4.0f), selected && ! bypassed ? colour : foreground, 1.8f);

    g.setColour (foreground);
    // Narrow tiles (long chains) shrink the name instead of breaking it in two.
    g.setFont (juce::FontOptions (juce::jlimit (9.0f, 15.0f, r.getWidth() * 0.21f), juce::Font::bold));
    juce::String label = nodeLabel (node);
    if (decoupled)      label << "\n(E2E)";
    else if (bypassed)  label << "\nOFF";
    g.drawFittedText (label, labelArea.withX (r.getX() + 1.0f).withWidth (r.getWidth() - 2.0f).toNearestInt(), juce::Justification::centred, 2, 0.7f);
}

void GridView::paint (juce::Graphics& g)
{
    // Bone field with engraved wavy lines and a ruler along the bottom.
    const auto bounds = getLocalBounds().toFloat();
    g.fillAll (theme::bone);
    constexpr int textureScale = 2;
    if (fieldTexture.getWidth() != getWidth() * textureScale || fieldTexture.getHeight() != getHeight() * textureScale)
    {
        fieldTexture = juce::Image (juce::Image::ARGB, std::max (1, getWidth() * textureScale), std::max (1, getHeight() * textureScale), true);
        juce::Graphics tg (fieldTexture);
        tg.addTransform (juce::AffineTransform::scale ((float) textureScale));
        tg.setColour (theme::ink.withAlpha (0.07f));
        for (float y = -8.0f; y < bounds.getHeight() + 8.0f; y += 7.0f)
        {
            juce::Path line;
            line.startNewSubPath (0.0f, y);
            for (float x = 12.0f; x <= bounds.getWidth() + 12.0f; x += 12.0f)
                line.lineTo (x, y + 2.2f * std::sin (x * 0.035f + y * 0.05f) + 1.6f * std::sin (x * 0.011f - y * 0.02f));
            tg.strokePath (line, juce::PathStrokeType (0.8f));
        }
    }
    g.drawImage (fieldTexture, bounds);

    g.setColour (theme::ink);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (juce::CharPointer_UTF8 ("CADENA DE SE\xc3\x91" "AL"), getLocalBounds().reduced (14, 10).removeFromTop (18), juce::Justification::centredLeft);
    for (int i = 0; i * 14 < getWidth() - 28; ++i)
        g.fillRect (14 + i * 14, getHeight() - (i % 5 == 0 ? 12 : 8), 1, i % 5 == 0 ? 8 : 4);

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
                // Orthogonal, like a circuit diagram.
                const float midX = isJunction (*source) ? a.x + 10.0f : b.x - 10.0f;
                cable.lineTo (midX, a.y);
                cable.lineTo (midX, b.y);
                cable.lineTo (b);
            }
            g.setColour (theme::cable);
            g.strokePath (cable, juce::PathStrokeType (1.5f));
            for (auto p : { a, b })
                g.fillRect (p.x - 2.5f, p.y - 2.5f, 5.0f, 5.0f);

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
            g.setColour (hot ? theme::accent : theme::ink);
            g.fillRect (c.x - radius, c.y - radius, 2 * radius, 2 * radius);
            if (hot || ! dragHover)
                theme::drawIcon (g, theme::Icon::Plus, juce::Rectangle<float> (2 * radius, 2 * radius).withCentre (c).reduced (radius * 0.35f),
                                 hot ? theme::ink : theme::bone, 2.0f);
        }

    // Nodes.
    for (const auto& node : model.getNodes())
    {
        const auto r = nodeBounds (node, layout);
        const bool selected = node.id == selectedNode;

        if (isJunction (node))
        {
            g.setColour (selected ? theme::ink : theme::bone);
            g.fillEllipse (r);
            g.setColour (theme::ink);
            g.drawEllipse (r.reduced (1.0f), 1.5f);
            theme::drawIcon (g, theme::Icon::Plus, r.reduced (5.0f), selected ? theme::bone : theme::ink, 1.8f);
        }
        else if (node.kind == NodeKind::Input || node.kind == NodeKind::Output)
        {
            g.setColour (theme::ink);
            g.fillRect (r);
            g.setColour (theme::bone);
            g.setFont (theme::mono (15.0f, true));
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
