#include "RenderPlan.h"
#include "DSPBlock.h"
#include "../DSP/Routing/RoutingParams.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace kumamp
{
namespace
{
    int valueKey (int nodeIndex, int port) noexcept { return nodeIndex * kMaxBranches + port; }

    /** Linear per-block gain ramp from `state` to `target`, multiply-accumulate or assign. */
    template <bool Accumulate>
    void applyRamp (const float* src, float* dst, int numSamples, float& state, float target) noexcept
    {
        float g = state;
        const float step = (target - g) / (float) numSamples;
        for (int i = 0; i < numSamples; ++i)
        {
            g += step;
            if constexpr (Accumulate) dst[i] += src[i] * g;
            else                      dst[i]  = src[i] * g;
        }
        state = target;
    }
} // namespace

RenderPlan::CompileResult RenderPlan::compile (const GridModel& model, const StoreResolver& resolve, int maxBlockSize)
{
    CompileResult result;

    if (maxBlockSize <= 0)
    {
        result.error = "Tamano de bloque invalido";
        return result;
    }

    if (! model.validate (&result.error))
        return result;

    const auto& nodes = model.getNodes();
    const int numNodes = (int) nodes.size();

    std::unordered_map<NodeId, int> indexOf;
    for (int i = 0; i < numNodes; ++i)
        indexOf[nodes[(size_t) i].id] = i;

    auto sourceIndex = [&] (const PortRef& p) { return indexOf.at (p.node); };

    std::vector<std::vector<int>> consumers ((size_t) numNodes);
    for (int j = 0; j < numNodes; ++j)
        for (const auto& in : nodes[(size_t) j].inputs)
            consumers[(size_t) sourceIndex (in)].push_back (j);

    const int inputIndex  = indexOf.at (model.getInputNode());
    const int outputIndex = indexOf.at (model.getOutputNode());

    // --- Liveness: reachable from Input AND reaching Output ------------------
    std::vector<char> forward ((size_t) numNodes, 0), backward ((size_t) numNodes, 0);
    std::vector<int> stack { inputIndex };
    forward[(size_t) inputIndex] = 1;
    while (! stack.empty())
    {
        const int n = stack.back();
        stack.pop_back();
        for (int c : consumers[(size_t) n])
            if (! forward[(size_t) c]) { forward[(size_t) c] = 1; stack.push_back (c); }
    }

    stack = { outputIndex };
    backward[(size_t) outputIndex] = 1;
    while (! stack.empty())
    {
        const int n = stack.back();
        stack.pop_back();
        for (const auto& in : nodes[(size_t) n].inputs)
        {
            const int s = sourceIndex (in);
            if (! backward[(size_t) s]) { backward[(size_t) s] = 1; stack.push_back (s); }
        }
    }

    std::vector<char> live ((size_t) numNodes, 0);
    for (int i = 0; i < numNodes; ++i)
        live[(size_t) i] = forward[(size_t) i] && backward[(size_t) i];

    if (! live[(size_t) outputIndex])
    {
        result.error = "La salida no esta conectada a la entrada";
        return result;
    }

    auto liveInput = [&] (const PortRef& p) { return live[(size_t) sourceIndex (p)] != 0; };

    // --- Topological order over the live sub-graph -----------------------------
    std::vector<int> indegree ((size_t) numNodes, 0), order, ready;
    for (int j = 0; j < numNodes; ++j)
        if (live[(size_t) j])
            for (const auto& in : nodes[(size_t) j].inputs)
                if (liveInput (in))
                    ++indegree[(size_t) j];

    for (int j = 0; j < numNodes; ++j)
        if (live[(size_t) j] && indegree[(size_t) j] == 0)
            ready.push_back (j);

    while (! ready.empty())
    {
        const int n = ready.back();
        ready.pop_back();
        order.push_back (n);
        for (int c : consumers[(size_t) n])
            if (live[(size_t) c] && --indegree[(size_t) c] == 0)
                ready.push_back (c);
    }

    // --- Reader counts per output port ------------------------------------------
    const auto numValues = (size_t) numNodes * kMaxBranches;
    std::vector<int> remaining (numValues, 0), valueBuffer (numValues, -1), valueLatency (numValues, 0);
    for (int j : order)
        for (const auto& in : nodes[(size_t) j].inputs)
            if (liveInput (in))
                ++remaining[(size_t) valueKey (sourceIndex (in), in.port)];

    std::unique_ptr<RenderPlan> plan (new RenderPlan());
    plan->maxBlock = maxBlockSize;

    std::vector<int> freeList;
    auto allocate = [&]
    {
        if (! freeList.empty()) { const int b = freeList.back(); freeList.pop_back(); return b; }
        return plan->numBuffers++;
    };
    auto release = [&] (int key)
    {
        if (--remaining[(size_t) key] == 0)
            freeList.push_back (valueBuffer[(size_t) key]);
    };
    auto newGainSlot = [&] (float initial)
    {
        plan->gains.push_back (initial);
        return (int) plan->gains.size() - 1;
    };
    auto own = [&] (const std::shared_ptr<ParamStore>& store)
    {
        plan->owned.push_back (store);
        plan->lookup.emplace_back (store->getId(), store.get());
    };
    auto resolveTyped = [&] (const GridNode& node, BlockType expected) -> std::shared_ptr<ParamStore>
    {
        auto store = resolve (node.store);
        if (store == nullptr || store->getType() != expected)
            return nullptr;
        return store;
    };

    // --- Emit ops -----------------------------------------------------------
    for (int j : order)
    {
        const auto& node = nodes[(size_t) j];

        switch (node.kind)
        {
            case NodeKind::Input:
            {
                const int dst = allocate();
                plan->ops.push_back ({ OpType::LoadInput, dst });
                valueBuffer[(size_t) valueKey (j, 0)] = dst;
                break;
            }

            case NodeKind::Block:
            {
                auto store = resolve (node.store);
                auto* block = dynamic_cast<DSPBlock*> (store.get());
                if (block == nullptr || ! block->isPrepared())
                {
                    result.error = "Bloque inexistente o no preparado en el grid";
                    return result;
                }
                own (store);

                const int srcKey = valueKey (sourceIndex (node.inputs[0]), node.inputs[0].port);
                int buffer = valueBuffer[(size_t) srcKey];

                if (remaining[(size_t) srcKey] == 1)
                {
                    remaining[(size_t) srcKey] = 0;   // sole reader: take the buffer, process in place
                }
                else
                {
                    const int dst = allocate();
                    plan->ops.push_back ({ OpType::Copy, dst, buffer });
                    release (srcKey);
                    buffer = dst;
                }

                Op op { OpType::ProcessBlock, buffer };
                op.block = block;
                plan->ops.push_back (op);

                const int key = valueKey (j, 0);
                valueBuffer[(size_t) key]  = buffer;
                valueLatency[(size_t) key] = valueLatency[(size_t) srcKey] + block->getLatencySamples();
                break;
            }

            case NodeKind::Split:
            {
                auto store = resolveTyped (node, BlockType::Splitter);
                if (store == nullptr)
                {
                    result.error = "Splitter sin parametros validos";
                    return result;
                }
                own (store);

                const int srcKey = valueKey (sourceIndex (node.inputs[0]), node.inputs[0].port);
                for (int p = 0; p < node.numOutputs; ++p)
                {
                    const int key = valueKey (j, p);
                    if (remaining[(size_t) key] == 0)
                        continue;   // unconnected branch

                    Op op { OpType::SplitScale, allocate(), valueBuffer[(size_t) srcKey] };
                    op.params = store.get();
                    op.paramIndex = SplitterParam::branchLevel (p);
                    op.gainSlot = newGainSlot (store->getParam (op.paramIndex));
                    plan->ops.push_back (op);

                    valueBuffer[(size_t) key]  = op.dst;
                    valueLatency[(size_t) key] = valueLatency[(size_t) srcKey];
                }
                release (srcKey);
                break;
            }

            case NodeKind::Mix:
            {
                auto store = resolveTyped (node, BlockType::Mixer);
                if (store == nullptr)
                {
                    result.error = "Mixer sin parametros validos";
                    return result;
                }
                own (store);

                int maxLatency = 0;
                for (const auto& in : node.inputs)
                    if (liveInput (in))
                        maxLatency = std::max (maxLatency, valueLatency[(size_t) valueKey (sourceIndex (in), in.port)]);

                Op op { OpType::Mix, allocate() };
                op.params = store.get();
                op.firstMixInput = (int) plan->mixInputs.size();

                for (int k = 0; k < (int) node.inputs.size(); ++k)
                {
                    const auto& in = node.inputs[(size_t) k];
                    if (! liveInput (in))
                        continue;

                    const int srcKey = valueKey (sourceIndex (in), in.port);
                    MixInput mi;
                    mi.src = valueBuffer[(size_t) srcKey];
                    mi.inputIndex = k;

                    float gl, gr;
                    computeMixerGains (*store, k, gl, gr);
                    mi.gainSlot = newGainSlot (gl);
                    newGainSlot (gr);

                    if (const int compensation = maxLatency - valueLatency[(size_t) srcKey]; compensation > 0)
                    {
                        plan->delays.emplace_back();
                        plan->delays.back().prepare (compensation);
                        mi.delayIndex = (int) plan->delays.size() - 1;
                    }

                    plan->mixInputs.push_back (mi);
                    ++op.numMixInputs;
                }

                plan->ops.push_back (op);

                for (const auto& in : node.inputs)
                    if (liveInput (in))
                        release (valueKey (sourceIndex (in), in.port));

                const int key = valueKey (j, 0);
                valueBuffer[(size_t) key]  = op.dst;
                valueLatency[(size_t) key] = maxLatency;
                break;
            }

            case NodeKind::Output:
            {
                const int srcKey = valueKey (sourceIndex (node.inputs[0]), node.inputs[0].port);
                plan->ops.push_back ({ OpType::StoreOutput, -1, valueBuffer[(size_t) srcKey] });
                plan->latencySamples = valueLatency[(size_t) srcKey];
                release (srcKey);
                break;
            }
        }
    }

    plan->storage.assign ((size_t) plan->numBuffers * kGraphChannels * (size_t) maxBlockSize, 0.0f);
    plan->scratch.assign ((size_t) kGraphChannels * (size_t) maxBlockSize, 0.0f);

    std::sort (plan->lookup.begin(), plan->lookup.end());
    plan->lookup.erase (std::unique (plan->lookup.begin(), plan->lookup.end()), plan->lookup.end());

    result.plan = std::move (plan);
    return result;
}

ParamStore* RenderPlan::findStore (StoreId id) const noexcept
{
    const auto it = std::lower_bound (lookup.begin(), lookup.end(), id,
                                      [] (const auto& entry, StoreId value) { return entry.first < value; });
    return it != lookup.end() && it->first == id ? it->second : nullptr;
}

void RenderPlan::resetState() noexcept
{
    for (auto& d : delays)
        d.reset();

    for (const auto& op : ops)
        if (op.type == OpType::ProcessBlock)
            op.block->reset();
}

void RenderPlan::runSplit (const Op& op, int numSamples) noexcept
{
    float& state = gains[(size_t) op.gainSlot];
    const float target = op.params->getParam (op.paramIndex);
    const float start = state;

    for (int ch = 0; ch < kGraphChannels; ++ch)
    {
        state = start;
        applyRamp<false> (channel (op.src, ch), channel (op.dst, ch), numSamples, state, target);
    }
}

void RenderPlan::runMix (const Op& op, int numSamples) noexcept
{
    float* dstL = channel (op.dst, 0);
    float* dstR = channel (op.dst, 1);
    const auto bytes = sizeof (float) * (size_t) numSamples;
    std::memset (dstL, 0, bytes);
    std::memset (dstR, 0, bytes);

    for (int k = 0; k < op.numMixInputs; ++k)
    {
        const auto& mi = mixInputs[(size_t) (op.firstMixInput + k)];
        const float* srcL = channel (mi.src, 0);
        const float* srcR = channel (mi.src, 1);

        if (mi.delayIndex >= 0)
        {
            // The source buffer may still be read by another node: delay a copy.
            float* tmpL = scratch.data();
            float* tmpR = scratch.data() + maxBlock;
            std::memcpy (tmpL, srcL, bytes);
            std::memcpy (tmpR, srcR, bytes);
            delays[(size_t) mi.delayIndex].process (tmpL, tmpR, numSamples);
            srcL = tmpL;
            srcR = tmpR;
        }

        float targetL, targetR;
        computeMixerGains (*op.params, mi.inputIndex, targetL, targetR);
        applyRamp<true> (srcL, dstL, numSamples, gains[(size_t) mi.gainSlot], targetL);
        applyRamp<true> (srcR, dstR, numSamples, gains[(size_t) mi.gainSlot + 1], targetR);
    }
}

void RenderPlan::process (const float* const* inputs, int numInputs,
                          float* const* outputs, int numOutputs, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;

    numSamples = std::min (numSamples, maxBlock);
    const auto bytes = sizeof (float) * (size_t) numSamples;

    for (const auto& op : ops)
    {
        switch (op.type)
        {
            case OpType::LoadInput:
            {
                float* l = channel (op.dst, 0);
                float* r = channel (op.dst, 1);
                if (numInputs <= 0)
                {
                    std::memset (l, 0, bytes);
                    std::memset (r, 0, bytes);
                }
                else
                {
                    // Mono input feeds both sides of the stereo grid.
                    std::memcpy (l, inputs[0], bytes);
                    std::memcpy (r, inputs[numInputs > 1 ? 1 : 0], bytes);
                }
                break;
            }

            case OpType::Copy:
                std::memcpy (channel (op.dst, 0), channel (op.src, 0), bytes);
                std::memcpy (channel (op.dst, 1), channel (op.src, 1), bytes);
                break;

            case OpType::ProcessBlock:
                op.block->process (channel (op.dst, 0), channel (op.dst, 1), numSamples);
                break;

            case OpType::SplitScale:
                runSplit (op, numSamples);
                break;

            case OpType::Mix:
                runMix (op, numSamples);
                break;

            case OpType::StoreOutput:
            {
                const float* l = channel (op.src, 0);
                const float* r = channel (op.src, 1);
                if (numOutputs == 1)
                {
                    for (int i = 0; i < numSamples; ++i)
                        outputs[0][i] = 0.5f * (l[i] + r[i]);
                }
                else if (numOutputs >= 2)
                {
                    std::memcpy (outputs[0], l, bytes);
                    std::memcpy (outputs[1], r, bytes);
                    for (int ch = 2; ch < numOutputs; ++ch)
                        std::memset (outputs[ch], 0, bytes);
                }
                break;
            }
        }
    }
}
} // namespace kumamp
