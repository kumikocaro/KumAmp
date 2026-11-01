#include "GridModel.h"

#include <algorithm>
#include <unordered_map>

namespace kumamp
{
namespace
{
    bool fail (std::string* error, const char* message)
    {
        if (error != nullptr)
            *error = message;
        return false;
    }
} // namespace

NodeId GridModel::addNode (NodeKind kind, StoreId store, int row, int column, int numOutputs)
{
    GridNode node;
    node.id = nextId++;
    node.kind = kind;
    node.store = store;
    node.row = row;
    node.column = column;
    node.numOutputs = kind == NodeKind::Split ? std::clamp (numOutputs, 2, kMaxBranches) : 1;
    nodes.push_back (std::move (node));
    return nodes.back().id;
}

bool GridModel::addNodeWithId (const GridNode& node)
{
    if (node.id == kInvalidNode || findNode (node.id) != nullptr)
        return false;

    nodes.push_back (node);
    nextId = std::max (nextId, node.id + 1);
    return true;
}

bool GridModel::removeNode (NodeId id)
{
    const auto it = std::find_if (nodes.begin(), nodes.end(), [id] (const GridNode& n) { return n.id == id; });
    if (it == nodes.end())
        return false;

    nodes.erase (it);
    for (auto& n : nodes)
        n.inputs.erase (std::remove_if (n.inputs.begin(), n.inputs.end(),
                                        [id] (const PortRef& p) { return p.node == id; }),
                        n.inputs.end());
    return true;
}

bool GridModel::connect (PortRef from, NodeId to)
{
    const auto* source = findNode (from.node);
    auto* destination = findNode (to);
    if (source == nullptr || destination == nullptr || from.port < 0 || from.port >= source->numOutputs)
        return false;

    destination->inputs.push_back (from);
    return true;
}

bool GridModel::disconnect (PortRef from, NodeId to)
{
    auto* destination = findNode (to);
    if (destination == nullptr)
        return false;

    auto& in = destination->inputs;
    const auto it = std::find (in.begin(), in.end(), from);
    if (it == in.end())
        return false;

    in.erase (it);
    return true;
}

void GridModel::clear()
{
    nodes.clear();
    nextId = 1;
}

GridNode* GridModel::findNode (NodeId id) noexcept
{
    for (auto& n : nodes)
        if (n.id == id)
            return &n;
    return nullptr;
}

const GridNode* GridModel::findNode (NodeId id) const noexcept
{
    return const_cast<GridModel*> (this)->findNode (id);
}

NodeId GridModel::getInputNode() const noexcept
{
    for (const auto& n : nodes)
        if (n.kind == NodeKind::Input)
            return n.id;
    return kInvalidNode;
}

NodeId GridModel::getOutputNode() const noexcept
{
    for (const auto& n : nodes)
        if (n.kind == NodeKind::Output)
            return n.id;
    return kInvalidNode;
}

bool GridModel::validate (std::string* error) const
{
    int numInputs = 0, numOutputs = 0;
    std::unordered_map<NodeId, size_t> indexOf;

    for (size_t i = 0; i < nodes.size(); ++i)
    {
        const auto& n = nodes[i];
        if (! indexOf.emplace (n.id, i).second)
            return fail (error, "ID de nodo duplicado");

        const auto inputs = (int) n.inputs.size();
        switch (n.kind)
        {
            case NodeKind::Input:
                ++numInputs;
                if (inputs != 0) return fail (error, "El nodo Input no admite entradas");
                break;
            case NodeKind::Output:
                ++numOutputs;
                if (inputs != 1) return fail (error, "El nodo Output necesita exactamente 1 entrada");
                break;
            case NodeKind::Block:
                if (inputs != 1) return fail (error, "Cada bloque necesita exactamente 1 entrada");
                break;
            case NodeKind::Split:
                if (inputs != 1) return fail (error, "El Splitter necesita exactamente 1 entrada");
                if (n.numOutputs < 2 || n.numOutputs > kMaxBranches) return fail (error, "Numero de ramas invalido");
                break;
            case NodeKind::Mix:
                if (inputs < 1 || inputs > kMaxBranches) return fail (error, "El Mixer admite de 1 a 4 entradas");
                break;
        }

        if (n.kind != NodeKind::Split && n.numOutputs != 1)
            return fail (error, "Solo el Splitter tiene varias salidas");

        const bool needsStore = n.kind == NodeKind::Block || n.kind == NodeKind::Split || n.kind == NodeKind::Mix;
        if (needsStore && n.store == kInvalidStore)
            return fail (error, "Nodo sin parametros asociados");
    }

    if (numInputs != 1 || numOutputs != 1)
        return fail (error, "El grid necesita exactamente un Input y un Output");

    // References + Kahn's algorithm for cycle detection.
    std::vector<int> indegree (nodes.size(), 0);
    std::vector<std::vector<size_t>> consumers (nodes.size());

    for (size_t i = 0; i < nodes.size(); ++i)
    {
        for (const auto& in : nodes[i].inputs)
        {
            const auto it = indexOf.find (in.node);
            if (it == indexOf.end())
                return fail (error, "Conexion a un nodo inexistente");

            const auto& source = nodes[it->second];
            if (source.kind == NodeKind::Output)
                return fail (error, "El Output no puede alimentar otros nodos");
            if (in.port < 0 || in.port >= source.numOutputs)
                return fail (error, "Puerto de salida invalido");

            consumers[it->second].push_back (i);
            ++indegree[i];
        }
    }

    std::vector<size_t> ready;
    for (size_t i = 0; i < nodes.size(); ++i)
        if (indegree[i] == 0)
            ready.push_back (i);

    size_t visited = 0;
    while (! ready.empty())
    {
        const auto i = ready.back();
        ready.pop_back();
        ++visited;

        for (auto c : consumers[i])
            if (--indegree[c] == 0)
                ready.push_back (c);
    }

    if (visited != nodes.size())
        return fail (error, "El grid contiene un ciclo (realimentacion no permitida)");

    return true;
}
} // namespace kumamp
