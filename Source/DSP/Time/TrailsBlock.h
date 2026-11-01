#pragma once

#include "../../Core/Smoothing.h"
#include "../../Graph/DSPBlock.h"

#include <cmath>

namespace kumamp
{
/**
    Base for delays and reverbs with "trails": bypass mutes the block's INPUT
    (20 ms ramp) instead of its output, so echoes and reverb tails ring out
    naturally. Once bypassed and the tail has decayed below -120 dBFS for
    ~0.5 s, processing stops entirely (zero CPU).

    Subclass processBlock() pattern:
        if (! beginTrailsBlock (n)) return;            // bypassed + silent
        for each sample:  g = nextFeed();  wet = dsp (dry * g);  trackTail (wet); ...
        endTrailsBlock (n);
*/
class TrailsBlock : public DSPBlock
{
public:
    using DSPBlock::DSPBlock;

protected:
    bool handlesBypassInternally() const noexcept override { return true; }

    /** silenceSeconds: how long the output must stay silent before the tail is
        considered gone. Delays must cover their longest echo gap. */
    void prepareTrails (double sampleRate, double silenceSeconds = 0.5)
    {
        feed.prepare (sampleRate, 0.020);
        feed.setCurrentAndTarget (isBypassed() ? 0.0f : 1.0f);
        silentLimit = (int) (sampleRate * silenceSeconds);
        silentSamples = isBypassed() ? silentLimit : 0;
    }

    /** False when bypassed and the tail is gone: leave the dry signal untouched. */
    bool beginTrailsBlock (int numSamples) noexcept
    {
        feed.setTarget (isBypassed() ? 0.0f : 1.0f);
        if (! isBypassed())
        {
            if (silentSamples >= silentLimit)
                resetTail();             // waking up after a full stop: start clean
            silentSamples = 0;
            return true;
        }
        blockPeak = 0.0f;
        return silentSamples < silentLimit || feed.isSmoothing() || numSamples <= 0;
    }

    float nextFeed() noexcept { return feed.next(); }
    float getFeed() const noexcept { return feed.getCurrent(); }

    void trackTail (float wetSample) noexcept { blockPeak = std::max (blockPeak, std::abs (wetSample)); }

    void endTrailsBlock (int numSamples) noexcept
    {
        if (isBypassed() && ! feed.isSmoothing())
            silentSamples = blockPeak < 1.0e-6f ? silentSamples + numSamples : 0;
        blockPeak = 0.0f;
    }

    /** RT-safe: clear delay lines / reverb state. */
    virtual void resetTail() noexcept = 0;

private:
    LinearSmoother feed;
    float blockPeak = 0.0f;
    int silentSamples = 0, silentLimit = 24000;
};
} // namespace kumamp
