#pragma once

#include "../Core/Constants.h"

#include <string>
#include <vector>

namespace kumamp
{
enum class NodeKind : std::uint8_t
{
    Input,   // host input, 1 output
    Output,  // host output, 1 input
    Block,   // DSPBlock, 1 input / 1 output
    Split,   // 1 input, 2..kMaxBranches outputs (per-branch level)
    Mix      // 1..kMaxBranches inputs (level/pan per input), 1 output
};

struct PortRef
{
    NodeId node = kInvalidNode;
    int port = 0;

    bool operator== (const PortRef&) const = default;
};

struct GridNode
{
    NodeId id = kInvalidNode;
    NodeKind kind = NodeKind::Block;
    StoreId store = kInvalidStore;     // Block / Split / Mix
    std::vector<PortRef> inputs;       // order matters for Mix (input A, B, ...)
    int numOutputs = 1;
    int row = 0, column = 0;           // position on the visual grid
};

/**
    Editable description of the routing graph (message thread only).

    It is a plain value type: the GUI edits a copy and hands it to the engine,
    which compiles it into an immutable RenderPlan for the audio thread. The
    audio thread never sees a GridModel.

    Series = a chain of single-input nodes. Parallel = a Split node whose
    branches are later summed by a Mix node. Branches can be nested, and an
    output port may also feed several nodes directly (implicit "Y" split).
*/
class GridModel
{
public:
    NodeId addNode (NodeKind kind, StoreId store = kInvalidStore, int row = 0, int column = 0, int numOutputs = 1);
    /** For deserialisation: keeps the given id (must be unique and non-zero). */
    bool addNodeWithId (const GridNode& node);
    bool removeNode (NodeId id);

    bool connect (PortRef from, NodeId to);
    bool disconnect (PortRef from, NodeId to);
    void clear();

    GridNode* findNode (NodeId id) noexcept;
    const GridNode* findNode (NodeId id) const noexcept;
    const std::vector<GridNode>& getNodes() const noexcept { return nodes; }

    NodeId getInputNode() const noexcept;
    NodeId getOutputNode() const noexcept;

    /** Structural checks: single Input/Output, port arity, dangling refs, cycles. */
    bool validate (std::string* error = nullptr) const;

    //== Editing helpers (keep the graph valid) =================================
    /** Replaces every input reference `from` -> `replacement` on node `to`
        (or on all nodes if to == kInvalidNode). Returns the number replaced. */
    int rewire (PortRef from, PortRef replacement, NodeId to = kInvalidNode);

    /** Removes a Block node and connects its input straight to its consumers. */
    bool removeBlockAndBridge (NodeId id);

    /** Removes Split/Mix pairs whose branches are all empty cables
        (left behind after deleting the blocks of a parallel section). */
    int collapseEmptyParallels();

    /** Recomputes row/column: column = longest path from Input, each extra
        Splitter branch gets its own row. Requires a valid (acyclic) model. */
    void autoLayout();

private:
    std::vector<GridNode> nodes;
    NodeId nextId = 1;
};
} // namespace kumamp
