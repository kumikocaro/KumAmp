#include "VolumeBlock.h"
#include "../../Core/DspMath.h"

#include <iterator>

namespace kumamp
{
namespace
{
    constexpr ParamInfo kParams[] = {
        { "level", "Nivel", -60.0f, 24.0f, 0.0f, "dB", false },
        { "pan",   "Pan",    -1.0f,  1.0f, 0.0f, "",   false },
    };
    static_assert (std::size (kParams) == VolumeBlock::NumParams);
} // namespace

VolumeBlock::VolumeBlock (StoreId id)
    : DSPBlock (id, BlockType::Volume, kParams, NumParams)
{
}

void VolumeBlock::computeTargets (float& left, float& right) const noexcept
{
    const float gain = dbToGain (getParam (LevelDb));
    balanceGains (getParam (Pan), left, right);
    left  *= gain;
    right *= gain;
}

void VolumeBlock::prepareBlock (double sampleRate, int)
{
    gainLeft.prepare (sampleRate, 0.020);
    gainRight.prepare (sampleRate, 0.020);
    resetBlock();
}

void VolumeBlock::resetBlock() noexcept
{
    float l, r;
    computeTargets (l, r);
    gainLeft.setCurrentAndTarget (l);
    gainRight.setCurrentAndTarget (r);
}

void VolumeBlock::processBlock (float* left, float* right, int numSamples) noexcept
{
    float l, r;
    computeTargets (l, r);
    gainLeft.setTarget (l);
    gainRight.setTarget (r);

    for (int i = 0; i < numSamples; ++i)
    {
        left[i]  *= gainLeft.next();
        right[i] *= gainRight.next();
    }
}
} // namespace kumamp
