#pragma once

#include "../../Graph/ParamStore.h"

namespace kumamp
{
/** Parameter layout of Splitter nodes: linear gain per output branch. */
namespace SplitterParam
{
    inline constexpr int branchLevel (int branch) noexcept { return branch; }
    inline constexpr int NumParams = kMaxBranches;
}

/** Parameter layout of Mixer nodes: level + pan per input, then master. */
namespace MixerParam
{
    inline constexpr int inputLevelDb (int input) noexcept { return input; }
    inline constexpr int inputPan (int input) noexcept     { return kMaxBranches + input; }
    inline constexpr int MasterDb  = 2 * kMaxBranches;
    inline constexpr int NumParams = 2 * kMaxBranches + 1;
}

/** Splitters and Mixers carry no DSP state of their own: the RenderPlan reads
    their parameters directly. Making them ParamStores lets scenes, presets and
    the GUI treat them exactly like blocks (e.g. a scene that mutes branch B). */
class RoutingParams final : public ParamStore
{
public:
    RoutingParams (StoreId id, BlockType type); // type: Splitter or Mixer
};

/** Mixer gains for input `index` (level, pan and master folded together). */
void computeMixerGains (const ParamStore& mixer, int index, float& left, float& right) noexcept;
} // namespace kumamp
