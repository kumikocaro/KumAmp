#pragma once

#include "GridModel.h"
#include "ParamStore.h"
#include "../Core/DelayLine.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace kumamp
{
class DSPBlock;
class FusionBus;

/**
    Immutable, pre-allocated execution schedule compiled from a GridModel.

    compile() runs on the message thread and does all the expensive work:
      - prunes nodes not on an Input -> Output path,
      - topologically sorts the graph,
      - allocates a minimal pool of stereo buffers via liveness analysis
        (blocks process in place whenever their input has no other reader),
      - computes per-path latency and inserts compensation delays at every
        Mixer input so parallel branches stay sample-aligned,
      - holds shared ownership of every block it references, so a block removed
        from the grid is only destroyed after the audio thread has retired the
        last plan using it (and that deletion happens on the message thread).

    process() is a flat loop over the op list: no allocation, no locks, no
    virtual dispatch other than DSPBlock::process.
*/
class RenderPlan
{
public:
    using StoreResolver = std::function<std::shared_ptr<ParamStore> (StoreId)>;

    struct CompileResult
    {
        std::unique_ptr<RenderPlan> plan;
        std::string error;
    };

    static CompileResult compile (const GridModel& model, const StoreResolver& resolve, int maxBlockSize);

    /** Audio thread. numSamples must be <= getMaxBlockSize(). inputs/outputs may alias. */
    void process (const float* const* inputs, int numInputs,
                  float* const* outputs, int numOutputs, int numSamples) noexcept;

    /** Audio thread (RT-safe): clears compensation delays and every block's state. */
    void resetState() noexcept;

    /** Before publishing. Blocks then read and feed the bus around each process call. */
    void setFusionBus (FusionBus* bus) noexcept { fusionBus = bus; }

    int getLatencySamples() const noexcept { return latencySamples; }
    int getMaxBlockSize() const noexcept   { return maxBlock; }
    int getNumBuffers() const noexcept     { return numBuffers; }

    /** RT-safe lookup (binary search) used by scenes and macros. */
    ParamStore* findStore (StoreId id) const noexcept;

private:
    RenderPlan() = default;

    enum class OpType : std::uint8_t { LoadInput, Copy, ProcessBlock, SplitScale, Mix, StoreOutput };

    struct Op
    {
        OpType type;
        int dst = -1, src = -1;
        DSPBlock* block = nullptr;
        ParamStore* params = nullptr;
        int paramIndex = 0;
        int gainSlot = 0;
        int firstMixInput = 0, numMixInputs = 0;
    };

    struct MixInput
    {
        int src = -1;
        int inputIndex = 0;   // position in the Mixer's input list (A, B, C, D)
        int delayIndex = -1;  // latency compensation line, -1 if aligned
        int gainSlot = 0;     // two consecutive slots: left, right
    };

    float* channel (int buffer, int ch) noexcept
    {
        return storage.data() + ((size_t) buffer * kGraphChannels + (size_t) ch) * (size_t) maxBlock;
    }

    void runSplit (const Op& op, int numSamples) noexcept;
    void runMix (const Op& op, int numSamples) noexcept;

    std::vector<Op> ops;
    std::vector<MixInput> mixInputs;
    std::vector<FixedDelay> delays;
    std::vector<float> gains;     // smoothed gain state per slot
    std::vector<float> storage;   // numBuffers * 2 * maxBlock
    std::vector<float> scratch;   // 2 * maxBlock, for delayed mixer inputs

    std::vector<std::shared_ptr<ParamStore>> owned;
    std::vector<std::pair<StoreId, ParamStore*>> lookup; // sorted by id

    int maxBlock = 0, numBuffers = 0, latencySamples = 0;
    FusionBus* fusionBus = nullptr;
};
} // namespace kumamp
