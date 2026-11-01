#include "GridModel.h"

#include <algorithm>
#include <unordered_map>
#include <utility>

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
//==============================================================================
int GridModel::rewire (PortRef from, PortRef replacement, NodeId to)
{
    int count = 0;
    for (auto& n : nodes)
        if (to == kInvalidNode || n.id == to)
            for (auto& in : n.inputs)
                if (in == from)
                {
                    in = replacement;
                    ++count;
                }
    return count;
}

bool GridModel::removeBlockAndBridge (NodeId id)
{
    const auto* node = findNode (id);
    if (node == nullptr || node->kind != NodeKind::Block || node->inputs.size() != 1)
        return false;

    const PortRef source = node->inputs[0];
    rewire ({ id, 0 }, source);
    return removeNode (id);
}

int GridModel::collapseEmptyParallels()
{
    int collapsed = 0;
    for (bool changed = true; changed;)
    {
        changed = false;
        for (const auto& mix : nodes)
        {
            if (mix.kind != NodeKind::Mix || mix.inputs.empty())
                continue;

            const NodeId splitId = mix.inputs[0].node;
            const auto* split = findNode (splitId);
            if (split == nullptr || split->kind != NodeKind::Split || split->inputs.size() != 1)
                continue;

            const bool allFromSplit = std::all_of (mix.inputs.begin(), mix.inputs.end(),
                                                   [splitId] (const PortRef& p) { return p.node == splitId; });
            // The split must feed nothing but this mixer.
            bool splitOnlyFeedsMix = true;
            for (const auto& other : nodes)
                if (other.id != mix.id)
                    for (const auto& in : other.inputs)
                        if (in.node == splitId)
                            splitOnlyFeedsMix = false;

            if (! allFromSplit || ! splitOnlyFeedsMix)
                continue;

            const PortRef upstream = split->inputs[0];
            const NodeId mixId = mix.id;
            rewire ({ mixId, 0 }, upstream);
            removeNode (mixId);
            removeNode (splitId);
            ++collapsed;
            changed = true;
            break;   // node vector changed
        }
    }
    return collapsed;
}

void GridModel::autoLayout()
{
    const auto count = nodes.size();
    std::unordered_map<NodeId, size_t> indexOf;
    for (size_t i = 0; i < count; ++i)
        indexOf[nodes[i].id] = i;

    // Topological order (Kahn).
    std::vector<int> indegree (count, 0);
    std::vector<std::vector<size_t>> consumers (count);
    for (size_t i = 0; i < count; ++i)
        for (const auto& in : nodes[i].inputs)
            if (const auto it = indexOf.find (in.node); it != indexOf.end())
            {
                consumers[it->second].push_back (i);
                ++indegree[i];
            }

    std::vector<size_t> order, ready;
    for (size_t i = 0; i < count; ++i)
        if (indegree[i] == 0)
            ready.push_back (i);
    while (! ready.empty())
    {
        const auto i = ready.front();
        ready.erase (ready.begin());
        order.push_back (i);
        for (auto c : consumers[i])
            if (--indegree[c] == 0)
                ready.push_back (c);
    }
    if (order.size() != count)
        return;   // cyclic: leave positions alone

    // Columns: longest path from the sources.
    std::vector<int> column (count, 0);
    for (auto i : order)
        for (auto c : consumers[i])
            column[c] = std::max (column[c], column[i] + 1);

    int lastColumn = 0;
    for (auto c : column) lastColumn = std::max (lastColumn, c);

    // Rows: a lane per Splitter branch beyond the first; a Mix sits on its top input lane
    // and frees the others, so later parallel sections reuse them.
    std::vector<int> row (count, -1);
    std::unordered_map<std::uint64_t, int> portLane;
    auto key = [] (NodeId n, int port) { return ((std::uint64_t) n << 8) | (std::uint64_t) port; };
    std::vector<int> busyLanes;
    auto takeLane = [&busyLanes]
    {
        int lane = 1;
        while (std::find (busyLanes.begin(), busyLanes.end(), lane) != busyLanes.end())
            ++lane;
        busyLanes.push_back (lane);
        return lane;
    };

    for (auto i : order)
    {
        auto& n = nodes[i];
        if (n.inputs.empty())
            row[i] = 0;
        else
        {
            int lane = 1 << 20;
            for (const auto& in : n.inputs)
                if (const auto it = portLane.find (key (in.node, in.port)); it != portLane.end())
                    lane = n.kind == NodeKind::Mix ? std::min (lane, it->second) : it->second;
            row[i] = lane == (1 << 20) ? 0 : lane;

            if (n.kind == NodeKind::Mix)
                for (const auto& in : n.inputs)
                    if (const auto it = portLane.find (key (in.node, in.port)); it != portLane.end() && it->second != row[i])
                        busyLanes.erase (std::remove (busyLanes.begin(), busyLanes.end(), it->second), busyLanes.end());
        }

        for (int p = 0; p < n.numOutputs; ++p)
            portLane[key (n.id, p)] = (n.kind == NodeKind::Split && p > 0) ? takeLane() : row[i];
    }

    for (size_t i = 0; i < count; ++i)
    {
        nodes[i].column = nodes[i].kind == NodeKind::Output ? lastColumn : column[i];
        nodes[i].row = row[i];
    }
}
} // namespace kumamp
