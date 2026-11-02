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

    constexpr double kCoarseRate    = 6000.0;
    constexpr int    kCheckInterval = 32;      // samples between splice searches while waiting
    constexpr float  kThreshold     = 0.98f;   // normalised correlation of a clean splice
    constexpr int    kFineRadius    = 2;       // full-rate refinement: +/- this many coarse steps
    constexpr float  kFadeFraction  = 0.5f;    // crossfade = half the jump, within [fadeMin, fadeMax]
} // namespace

void WsolaPitchShifter::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;

    bufferLeft.assign (kBufferSize, 0.0f);
    bufferRight.assign (kBufferSize, 0.0f);
    analysis.assign (kBufferSize, 0.0f);

    decimation = std::max (1, (int) std::lround (sampleRate / kCoarseRate));
    coarse.assign (kCoarseSize, 0.0f);

    // RBJ low-pass at ~35 % of the decimated Nyquist (anti-alias for the coarse copy).
    {
        const double fc = 0.35 * 0.5 * sampleRate / decimation;
        const double w0 = 2.0 * 3.141592653589793 * fc / sampleRate;
        const double alpha = std::sin (w0) / (2.0 * 0.7071067811865476);
        const double cw = std::cos (w0), a0 = 1.0 + alpha;
        lpB0 = (float) ((1.0 - cw) * 0.5 / a0);
        lpB1 = (float) ((1.0 - cw) / a0);
        lpB2 = lpB0;
        lpA1 = (float) (-2.0 * cw / a0);
        lpA2 = (float) ((1.0 - alpha) / a0);
    }

    updateGeometry();
    reset();
}

void WsolaPitchShifter::reset() noexcept
{
    std::fill (bufferLeft.begin(), bufferLeft.end(), 0.0f);
    std::fill (bufferRight.begin(), bufferRight.end(), 0.0f);
    std::fill (analysis.begin(), analysis.end(), 0.0f);
    std::fill (coarse.begin(), coarse.end(), 0.0f);

    coarseWrite = decimationPhase = sinceCheck = 0;
    lpZ1 = lpZ2 = 0.0f;
    writePos   = 0;
    delays[0]  = delays[1] = (float) kMinDelay;
    activeHead = 0;
    fading     = false;
    fadePos    = 0;
    ratio      = targetRatio;
}

void WsolaPitchShifter::setGrainMs (float milliseconds) noexcept
{
    requestedMaxJumpMs = std::clamp (milliseconds, 10.0f, 80.0f);
    updateGeometry();
}

void WsolaPitchShifter::setInstrument (Instrument newInstrument) noexcept
{
    if (instrument != newInstrument)
    {
        instrument = newInstrument;
        updateGeometry();
    }
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
    const auto ms = (float) sampleRate * 0.001f;
    const bool bass = instrument == Instrument::Bass;

    // Bass: a low B (31 Hz) repeats every 32 ms, so jumps and windows scale up.
    minJump = (bass ? 10.0f : 6.0f) * ms;
    maxJump = std::max (minJump + 4.0f * ms, (bass ? std::max (70.0f, requestedMaxJumpMs) : requestedMaxJumpMs) * ms);
    maxJump = std::min (maxJump, (float) (kBufferSize / 2));
    correlationLength = std::clamp ((int) std::lround ((bass ? 25.0f : 10.0f) * ms), 64, kBufferSize / 8);
    coarseCorrelationLength = std::max (8, correlationLength / decimation);
    fadeMin = 2.0f * ms;
    fadeMax = 8.0f * ms;
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

float WsolaPitchShifter::coarseScore (float reference, float candidate, int length) const noexcept
{
    // Full-rate delay d maps to coarse delay (d - decimationPhase) / decimation.
    constexpr int cMask = kCoarseSize - 1;
    const int newest = coarseWrite - 1;
    const int r = newest - (int) std::lround ((reference - (float) decimationPhase) / (float) decimation);
    const int c = newest - (int) std::lround ((candidate - (float) decimationPhase) / (float) decimation);
    float xy = 0.0f, xx = 1.0e-9f, yy = 1.0e-9f;
    for (int k = 0; k < length; ++k)
    {
        const float a = coarse[(size_t) ((r - k) & cMask)];
        const float b = coarse[(size_t) ((c - k) & cMask)];
        xy += a * b;
        xx += a * a;
        yy += b * b;
    }
    return xy / std::sqrt (xx * yy);
}

float WsolaPitchShifter::fineScore (float reference, float candidate, int length) const noexcept
{
    const int r = writePos - (int) std::lround (reference);
    const int c = writePos - (int) std::lround (candidate);
    float xy = 0.0f, xx = 1.0e-9f, yy = 1.0e-9f;
    for (int k = 0; k < length; ++k)
    {
        const float a = analysis[(size_t) ((r - k) & kMask)];
        const float b = analysis[(size_t) ((c - k) & kMask)];
        xy += a * b;
        xx += a * a;
        yy += b * b;
    }
    return xy / std::sqrt (xx * yy);
}

float WsolaPitchShifter::findSplice (float current, float lowest, float highest, bool lowestDelayFirst, bool force) noexcept
{
    if (highest < lowest)
        return -1.0f;

    // Silence: any splice point is inaudible.
    {
        constexpr int cMask = kCoarseSize - 1;
        float energy = 0.0f;
        for (int k = 0; k < coarseCorrelationLength; ++k)
        {
            const float a = coarse[(size_t) ((coarseWrite - 1 - k) & cMask)];
            energy += a * a;
        }
        if (energy < 1.0e-7f * (float) coarseCorrelationLength)
            return lowestDelayFirst ? lowest : highest;
    }

    // Coarse grid, scanned so the lowest resulting latency comes first. Only interior
    // local maxima count (see class comment); the first one >= kThreshold wins.
    const float step = (float) decimation;
    const float start = lowestDelayFirst ? lowest : highest;
    const float dir   = lowestDelayFirst ? step : -step;
    const int count   = (int) ((highest - lowest) / step) + 1;

    float best = -1.0f, bestScore = -2.0f, good = -1.0f;
    float prev2 = -2.0f, prev1 = -2.0f, prevCandidate = -1.0f;
    for (int i = 0; i <= count; ++i)
    {
        const float candidate = start + dir * (float) i;
        const float s = i < count ? coarseScore (current, candidate, coarseCorrelationLength) : -2.0f;
        if (prev1 > prev2 && prev1 >= s && prev2 > -2.0f)
        {
            if (prev1 > bestScore) { bestScore = prev1; best = prevCandidate; }
            if (prev1 >= kThreshold) { good = prevCandidate; break; }
        }
        prev2 = prev1;
        prev1 = s;
        prevCandidate = candidate;
    }

    const float pick = good >= 0.0f ? good : (force ? best : -1.0f);
    if (pick < 0.0f)
        return force ? (lowestDelayFirst ? lowest : highest) : -1.0f;

    // Refine at full rate around the coarse pick.
    float refined = pick, refinedScore = -2.0f;
    const float lo = std::max (lowest, pick - step * kFineRadius), hi = std::min (highest, pick + step * kFineRadius);
    for (float candidate = std::ceil (lo); candidate <= hi; candidate += 1.0f)
    {
        const float s = fineScore (current, candidate, correlationLength);
        if (s > refinedScore) { refinedScore = s; refined = candidate; }
    }

    // Keep the outgoing head's sub-sample phase so the relative alignment is exact.
    return std::clamp (refined + (current - std::round (current)), lowest, highest);
}

void WsolaPitchShifter::startFade (float newDelay, float jump) noexcept
{
    delays[1 - activeHead] = newDelay;
    fadeLength = (int) std::clamp (kFadeFraction * std::abs (jump), fadeMin, fadeMax);
    fading = true;
    fadePos = 0;
}

void WsolaPitchShifter::trySplice (float rate) noexcept
{
    const float d = delays[activeHead];
    const float floorDelay = (float) kMinDelay + 1.0f;

    if (rate > -kUnityEpsilon)
    {
        // Pitch down (or unity, draining latency): jump forward to a smaller delay as soon as it fits.
        const float highest = d - minJump;
        if (highest < floorDelay)
            return;
        const bool force = rate > kUnityEpsilon && d >= (float) kMinDelay + maxJump;
        if (const float next = findSplice (d, floorDelay, highest, true, force); next >= 0.0f)
            startFade (next, d - next);
        return;
    }

    // Pitch up: the delay shrinks; close to the minimum, jump back by the shortest clean lag.
    const float needed = -rate * fadeMax + 2.0f;   // the new head must survive the crossfade
    if (d + rate * (float) kCheckInterval > (float) kMinDelay + needed)
        return;
    const float maxSafe = (float) (kBufferSize - correlationLength - 16);
    const float lowest  = std::max (d + minJump, (float) kMinDelay + needed);
    const float highest = std::min (d + maxJump, maxSafe);
    if (const float next = findSplice (d, lowest, highest, true, true); next >= 0.0f)
        startFade (next, next - d);
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
        const float mono = 0.5f * (l + r);
        analysis[(size_t) writePos] = mono;

        {
            const float y = lpB0 * mono + lpZ1;
            lpZ1 = lpB1 * mono - lpA1 * y + lpZ2;
            lpZ2 = lpB2 * mono - lpA2 * y;
            if (++decimationPhase >= decimation)
            {
                decimationPhase = 0;
                coarse[(size_t) coarseWrite] = y;
                coarseWrite = (coarseWrite + 1) & (kCoarseSize - 1);
            }
        }

        const float rate = 1.0f - ratio;   // delay change per sample

        if (! fading && ++sinceCheck >= kCheckInterval)
        {
            sinceCheck = 0;
            trySplice (rate);
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
