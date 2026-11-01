#include "RoutingParams.h"
#include "../../Core/DspMath.h"

#include <cassert>
#include <iterator>

namespace kumamp
{
namespace
{
    constexpr ParamInfo kSplitterParams[] = {
        { "branch_a", "Rama A", 0.0f, 1.0f, 1.0f, "", false },
        { "branch_b", "Rama B", 0.0f, 1.0f, 1.0f, "", false },
        { "branch_c", "Rama C", 0.0f, 1.0f, 1.0f, "", false },
        { "branch_d", "Rama D", 0.0f, 1.0f, 1.0f, "", false },
    };

    constexpr ParamInfo kMixerParams[] = {
        { "level_a", "Nivel A", -60.0f, 12.0f, 0.0f, "dB", false },
        { "level_b", "Nivel B", -60.0f, 12.0f, 0.0f, "dB", false },
        { "level_c", "Nivel C", -60.0f, 12.0f, 0.0f, "dB", false },
        { "level_d", "Nivel D", -60.0f, 12.0f, 0.0f, "dB", false },
        { "pan_a",   "Pan A",    -1.0f,  1.0f, 0.0f, "",   false },
        { "pan_b",   "Pan B",    -1.0f,  1.0f, 0.0f, "",   false },
        { "pan_c",   "Pan C",    -1.0f,  1.0f, 0.0f, "",   false },
        { "pan_d",   "Pan D",    -1.0f,  1.0f, 0.0f, "",   false },
        { "master",  "Master",  -60.0f, 12.0f, 0.0f, "dB", false },
    };

    static_assert (std::size (kSplitterParams) == SplitterParam::NumParams);
    static_assert (std::size (kMixerParams) == MixerParam::NumParams);
} // namespace

RoutingParams::RoutingParams (StoreId id, BlockType type)
    : ParamStore (id, type,
                  type == BlockType::Splitter ? kSplitterParams : kMixerParams,
                  type == BlockType::Splitter ? SplitterParam::NumParams : MixerParam::NumParams)
{
    assert (type == BlockType::Splitter || type == BlockType::Mixer);
}

void computeMixerGains (const ParamStore& mixer, int index, float& left, float& right) noexcept
{
    const float gain = dbToGain (mixer.getParam (MixerParam::inputLevelDb (index)))
                     * dbToGain (mixer.getParam (MixerParam::MasterDb));
    balanceGains (mixer.getParam (MixerParam::inputPan (index)), left, right);
    left  *= gain;
    right *= gain;
}
} // namespace kumamp
