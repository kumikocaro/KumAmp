#include "WsolaPitchShifter.h"
#include "../../Core/DspMath.h"

#include <algorithm>
#include <cmath>

namespace kumamp
{
namespace
{
    constexpr int kMask = WsolaPitchShifter::kBufferSize - 1;
    constexpr float kUnityEpsilon = 1.0e-5f;
    static_assert ((WsolaPitchShifter::kBufferSize & kMask) == 0, "buffer size must be a power of two");
} // namespace

void WsolaPitchShifter::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;

    bufferLeft.assign (kBufferSize, 0.0f);
    bufferRight.assign (kBufferSize, 0.0f);
    analysis.assign (kBufferSize, 0.0f);

    updateGeometry();
    reset();
}

void WsolaPitchShifter::reset() noexcept
{
    std::fill (bufferLeft.begin(), bufferLeft.end(), 0.0f);
    std::fill (bufferRight.begin(), bufferRight.end(), 0.0f);
    std::fill (analysis.begin(), analysis.end(), 0.0f);

    writePos   = 0;
    delays[0]  = delays[1] = (float) kMinDelay;
    activeHead = 0;
    fading     = false;
    fadePos    = 0;
    ratio      = targetRatio;
}

void WsolaPitchShifter::setGrainMs (float milliseconds) noexcept
{
    requestedGrainMs = std::clamp (milliseconds, 5.0f, 40.0f);
}

void WsolaPitchShifter::setRatio (float newTargetRatio, float glideMilliseconds) noexcept
{
    targetRatio = std::clamp (newTargetRatio, 0.5f, 2.0f);
    glideCoefficient = glideMilliseconds <= 0.0f
                         ? 0.0f
                         : std::exp (-1.0f / (glideMilliseconds * 0.001f * (float) sampleRate));
}

bool WsolaPitchShifter::isGliding() const noexcept
{
    return std::abs (ratio - targetRatio) > 1.0e-4f * targetRatio;
}

void WsolaPitchShifter::updateGeometry() noexcept
{
    const auto sr = (float) sampleRate;
    grainLength       = std::clamp ((int) std::lround (requestedGrainMs * sr * 0.001f), 64, kBufferSize / 4);
    // Half the period of a low E (82 Hz) is enough to always find an in-phase splice.
    searchRadius      = std::clamp ((int) (sr / 160.0f), 16, grainLength / 2);
    correlationLength = std::clamp ((int) (sr * 0.005f), 64, 2048);
    // Must stay shorter than the time between splices at the extreme ratios (0.5 / 2.0).
    fadeLength        = std::max (16, grainLength / 4);
}

float WsolaPitchShifter::read (const std::vector<float>& buffer, float delay) const noexcept
{
    // Position = writePos - delay. Split the delay so the fractional part keeps full precision.
    const int whole = (int) delay;
    const float t = 1.0f - (delay - (float) whole);   // fraction between x0 and x1
    const int i1 = writePos - whole;                   // x1 (never ahead of writePos since delay >= kMinDelay)

    const float xm1 = buffer[(size_t) ((i1 - 2) & kMask)];
    const float x0  = buffer[(size_t) ((i1 - 1) & kMask)];
    const float x1  = buffer[(size_t) (i1 & kMask)];
    const float x2  = buffer[(size_t) ((i1 + 1) & kMask)];

    // 4-point, 3rd-order Hermite.
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}

float WsolaPitchShifter::correlationScore (int referenceEnd, int candidateEnd, int step) const noexcept
{
    float correlation = 0.0f, energy = 1.0e-9f;
    for (int k = 0; k < correlationLength; k += step)
    {
        const float a = analysis[(size_t) ((referenceEnd - k) & kMask)];
        const float b = analysis[(size_t) ((candidateEnd - k) & kMask)];
        correlation += a * b;
        energy      += b * b;
    }
    return correlation / std::sqrt (energy);
}

float WsolaPitchShifter::findBestDelay (float outgoingDelay, float lowest, float highest) const noexcept
{
    const float centre = 0.5f * (lowest + highest);
    const int roundedOutgoing = (int) std::lround (outgoingDelay);
    const float fraction = outgoingDelay - (float) roundedOutgoing;
    const int referenceEnd = writePos - roundedOutgoing;

    float referenceEnergy = 0.0f;
    for (int k = 0; k < correlationLength; k += 2)
    {
        const float a = analysis[(size_t) ((referenceEnd - k) & kMask)];
        referenceEnergy += a * a;
    }

    const int lo = (int) std::ceil (lowest);
    const int hi = (int) std::floor (highest);

    if (referenceEnergy < 1.0e-8f || hi <= lo)
        return std::clamp (centre, lowest, highest);  // silence: any splice point is inaudible

    // Coarse pass (every 2nd lag, every 2nd sample) then refine +/-1 at full resolution.
    int best = lo;
    float bestScore = -1.0e30f;
    for (int lag = lo; lag <= hi; lag += 2)
    {
        const float score = correlationScore (referenceEnd, writePos - lag, 2);
        if (score > bestScore) { bestScore = score; best = lag; }
    }

    const int coarseBest = best;
    bestScore = -1.0e30f;
    for (int lag = std::max (lo, coarseBest - 1); lag <= std::min (hi, coarseBest + 1); ++lag)
    {
        const float score = correlationScore (referenceEnd, writePos - lag, 1);
        if (score > bestScore) { bestScore = score; best = lag; }
    }

    // Keep the outgoing head's sub-sample phase so the relative alignment is exact.
    return std::clamp ((float) best + fraction, lowest, highest);
}

void WsolaPitchShifter::startSplice (float rate) noexcept
{
    updateGeometry();

    const float current = delays[activeHead];
    const float maxSafe = (float) (kBufferSize - correlationLength - 16);

    float target;
    if (rate < -kUnityEpsilon)      target = current + (float) grainLength;  // pitch up: jump back in time
    else if (rate > kUnityEpsilon)  target = current - (float) grainLength;  // pitch down: jump forward
    else                            target = (float) (kMinDelay + searchRadius); // unity: drain latency

    // The new head must survive the whole crossfade without hitting kMinDelay.
    const float minAllowed = (float) kMinDelay + std::max (0.0f, -rate) * (float) fadeLength + 1.0f;
    const float lowest  = std::max (target - (float) searchRadius, minAllowed);
    const float highest = std::max (lowest, std::min (target + (float) searchRadius, maxSafe));

    delays[1 - activeHead] = findBestDelay (current, lowest, highest);
    fading  = true;
    fadePos = 0;
}

void WsolaPitchShifter::process (const float* inLeft, const float* inRight,
                                 float* outLeft, float* outRight, int numSamples) noexcept
{
    const float maxSafe = (float) (kBufferSize - correlationLength - 16);

    for (int i = 0; i < numSamples; ++i)
    {
        ratio = targetRatio + (ratio - targetRatio) * glideCoefficient;

        const float l = inLeft[i], r = inRight[i];
        bufferLeft[(size_t) writePos]  = l;
        bufferRight[(size_t) writePos] = r;
        analysis[(size_t) writePos]    = 0.5f * (l + r);

        const float rate = 1.0f - ratio;   // delay change per sample

        if (! fading)
        {
            const float d = delays[activeHead];
            const float projected = d + rate * (float) fadeLength;

            if (rate < -kUnityEpsilon)
            {
                if (projected <= (float) kMinDelay)
                    startSplice (rate);
            }
            else if (rate > kUnityEpsilon)
            {
                if (projected >= (float) (kMinDelay + grainLength + searchRadius))
                    startSplice (rate);
            }
            else if (d > (float) (kMinDelay + 2 * searchRadius + 1))
            {
                startSplice (rate);
            }
        }

        const int a = activeHead;
        float yl = read (bufferLeft, delays[a]);
        float yr = read (bufferRight, delays[a]);
        delays[a] = std::clamp (delays[a] + rate, (float) kMinDelay, maxSafe);

        if (fading)
        {
            const int b = 1 - a;
            const float t = ((float) fadePos + 0.5f) / (float) fadeLength;
            const float gIn  = 0.5f - 0.5f * std::cos (kPi * t);  // equal-gain: heads are phase-aligned
            const float gOut = 1.0f - gIn;

            yl = yl * gOut + read (bufferLeft, delays[b]) * gIn;
            yr = yr * gOut + read (bufferRight, delays[b]) * gIn;
            delays[b] = std::clamp (delays[b] + rate, (float) kMinDelay, maxSafe);

            if (++fadePos >= fadeLength)
            {
                activeHead = b;
                fading = false;
            }
        }

        outLeft[i]  = yl;
        outRight[i] = yr;
        writePos = (writePos + 1) & kMask;
    }
}
} // namespace kumamp
