#include "DSPBlock.h"

#include <algorithm>
#include <cstring>
#include <cmath>

namespace kumamp
{
void DSPBlock::prepare (double sampleRate, int maxBlockSize)
{
    currentSampleRate = sampleRate;
    maxBlock = std::max (1, maxBlockSize);

    prepareBlock (sampleRate, maxBlock);

    dryLeft.assign ((size_t) maxBlock, 0.0f);
    dryRight.assign ((size_t) maxBlock, 0.0f);
    dryDelay.prepare (getLatencySamples());

    bypassStep    = 1.0f / std::max (1.0f, (float) (sampleRate * 0.010));
    bypassMix     = isBypassed() ? 1.0f : 0.0f;
    fullyBypassed = isBypassed();
    prepared      = true;
}

void DSPBlock::reset() noexcept
{
    dryDelay.reset();
    resetBlock();
}

void DSPBlock::process (float* left, float* right, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;

    if (handlesBypassInternally())
    {
        processBlock (left, right, numSamples);
        return;
    }

    const float target = isBypassed() ? 1.0f : 0.0f;
    const bool hasLatency = dryDelay.getDelay() > 0;
    const auto bytes = sizeof (float) * (size_t) numSamples;

    if (bypassMix == target)
    {
        if (target == 1.0f)
        {
            // Fully bypassed: skip DSP, keep the dry path time-aligned.
            fullyBypassed = true;
            dryDelay.process (left, right, numSamples);
            return;
        }

        if (fullyBypassed)
        {
            resetBlock();
            fullyBypassed = false;
        }

        if (hasLatency)
        {
            // Keep the compensation line warm so a later bypass ramp is seamless.
            std::memcpy (dryLeft.data(), left, bytes);
            std::memcpy (dryRight.data(), right, bytes);
            dryDelay.process (dryLeft.data(), dryRight.data(), numSamples);
        }

        processBlock (left, right, numSamples);
        return;
    }

    // Bypass transition: equal-gain crossfade between wet and (aligned) dry.
    if (fullyBypassed)
    {
        resetBlock();
        fullyBypassed = false;
    }

    std::memcpy (dryLeft.data(), left, bytes);
    std::memcpy (dryRight.data(), right, bytes);
    dryDelay.process (dryLeft.data(), dryRight.data(), numSamples);

    processBlock (left, right, numSamples);

    const float delta = target > bypassMix ? bypassStep : -bypassStep;
    for (int i = 0; i < numSamples; ++i)
    {
        bypassMix = std::clamp (bypassMix + delta, 0.0f, 1.0f);
        left[i]  += (dryLeft[(size_t) i]  - left[i])  * bypassMix;
        right[i] += (dryRight[(size_t) i] - right[i]) * bypassMix;
    }

    if (std::abs (bypassMix - target) < 1.0e-6f)
        bypassMix = target;
}
} // namespace kumamp
