#pragma once

#include "ParamStore.h"
#include "../Core/DelayLine.h"

#include <vector>

namespace kumamp
{
/**
    Base class of every audio block in the grid. Blocks are always stereo and
    process in place.

    The base class owns the bypass logic so every block gets the same
    click-free behaviour:
      - bypass toggles crossfade wet/dry over ~10 ms,
      - a fully bypassed block is not processed (zero CPU) but its dry signal
        is still delayed by getLatencySamples() so parallel paths stay aligned,
      - when re-enabled after a full bypass, resetBlock() clears stale state.

    Contract for subclasses:
      - prepareBlock()  : may allocate. Called off the audio thread.
      - resetBlock()    : RT-safe (no allocation, no locks).
      - processBlock()  : RT-safe; numSamples <= maxBlockSize.
      - getLatencySamples() must be constant between prepare() calls.
*/
class DSPBlock : public ParamStore
{
public:
    using ParamStore::ParamStore;

    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void process (float* left, float* right, int numSamples) noexcept;

    virtual int getLatencySamples() const noexcept { return 0; }

    bool   isPrepared() const noexcept    { return prepared; }
    double getSampleRate() const noexcept { return currentSampleRate; }
    int    getMaxBlockSize() const noexcept { return maxBlock; }

protected:
    virtual void prepareBlock (double sampleRate, int maxBlockSize) = 0;
    virtual void resetBlock() noexcept = 0;
    virtual void processBlock (float* left, float* right, int numSamples) noexcept = 0;

private:
    std::vector<float> dryLeft, dryRight;
    FixedDelay dryDelay;
    double currentSampleRate = 48000.0;
    int maxBlock = 0;
    float bypassMix = 0.0f;   // 0 = active, 1 = bypassed
    float bypassStep = 0.0f;
    bool fullyBypassed = false;
    bool prepared = false;
};
} // namespace kumamp
