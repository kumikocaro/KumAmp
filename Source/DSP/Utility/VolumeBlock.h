#pragma once

#include "../../Core/Smoothing.h"
#include "../../Graph/DSPBlock.h"

namespace kumamp
{
/** Level + balance utility block. */
class VolumeBlock final : public DSPBlock
{
public:
    enum Param { LevelDb, Pan, NumParams };

    explicit VolumeBlock (StoreId id);

protected:
    void prepareBlock (double sampleRate, int maxBlockSize) override;
    void resetBlock() noexcept override;
    void processBlock (float* left, float* right, int numSamples) noexcept override;

private:
    void computeTargets (float& left, float& right) const noexcept;

    LinearSmoother gainLeft, gainRight;
};
} // namespace kumamp
