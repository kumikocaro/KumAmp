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

    // Period tracking (bass): runs on the decimated copy at ~6 kHz.
    constexpr double kCoarseRate     = 6000.0;
    constexpr float  kTrackMinHz     = 25.0f;   // below a 5-string low B (30.9 Hz) with drop tunings
    constexpr float  kTrackMaxHz     = 400.0f;
    constexpr float  kTrackClarity   = 0.6f;
    constexpr float  kGrainPeriods   = 1.0f;    // jumping one period is the shortest in-phase splice
    constexpr double kAdaptBelowHz   = 52.0;    // the guitar geometry is clean down to ~A1 (55 Hz)
    static_assert ((WsolaPitchShifter::kBufferSize & kMask) == 0, "buffer size must be a power of two");
} // namespace

void WsolaPitchShifter::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;

    bufferLeft.assign (kBufferSize, 0.0f);
    bufferRight.assign (kBufferSize, 0.0f);
    analysis.assign (kBufferSize, 0.0f);

    decimation = std::max (1, (int) std::lround (sampleRate / kCoarseRate));
    coarse.assign (kCoarseSize, 0.0f);
    nsdf.assign (kCoarseSize / 4, 0.0f);
    trackWindow.assign (kCoarseSize / 2, 0.0f);

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

    coarseWrite = decimationPhase = 0;
    lpZ1 = lpZ2 = 0.0f;
    trackedPeriod = 0.0f;

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

    if (trackedPeriod > 0.0f)
    {
        // Long period (bass): one full period of correlation, half a period of search either side.
        grainLength       = std::clamp ((int) std::lround (kGrainPeriods * trackedPeriod), grainLength, kBufferSize / 4);
        searchRadius      = std::max (searchRadius, std::min ((int) std::lround (0.55f * trackedPeriod), grainLength / 2));
        correlationLength = std::clamp ((int) std::lround (trackedPeriod), correlationLength, 4096);
    }

    // Must stay shorter than the time between splices at the extreme ratios (0.5 / 2.0).
    fadeLength        = std::max (16, grainLength / 4);
}

void WsolaPitchShifter::estimatePeriod() noexcept
{
    constexpr int cMask = kCoarseSize - 1;
    const double coarseRate = sampleRate / decimation;
    const int maxLag = std::min ((int) nsdf.size() - 1, (int) std::ceil (coarseRate / kTrackMinHz));
    const int minLag = std::max (2, (int) (coarseRate / kTrackMaxHz));
    const int window = std::min (2 * maxLag, kCoarseSize / 2);
    const int start  = coarseWrite - window;   // oldest sample of the window

    // Linear copy so the lag loops run without ring masking.
    float* w = trackWindow.data();
    for (int n = 0; n < window; ++n) w[n] = coarse[(size_t) ((start + n) & cMask)];
    auto x = [w] (int n) { return w[n]; };

    float energy = 0.0f;
    for (int n = 0; n < window; ++n) energy += x (n) * x (n);
    if (energy < 1.0e-6f * (float) window)
        return;   // silence: keep the previous geometry

    // NSDF (McLeod): 2 r(tau) / m(tau), with m updated incrementally.
    float m = 2.0f * energy;
    for (int tau = 0; tau <= maxLag; ++tau)
    {
        if (tau > 0)
            m -= x (tau - 1) * x (tau - 1) + x (window - tau) * x (window - tau);
        // Four accumulators: lets the compiler vectorise without -ffast-math.
        const float* a = w + tau;
        const int count = window - tau;
        float r0 = 0.0f, r1 = 0.0f, r2 = 0.0f, r3 = 0.0f;
        int n = 0;
        for (; n + 4 <= count; n += 4)
        {
            r0 += a[n]     * w[n];
            r1 += a[n + 1] * w[n + 1];
            r2 += a[n + 2] * w[n + 2];
            r3 += a[n + 3] * w[n + 3];
        }
        for (; n < count; ++n) r0 += a[n] * w[n];
        const float r = (r0 + r1) + (r2 + r3);
        nsdf[(size_t) tau] = m > 1.0e-9f ? 2.0f * r / m : 0.0f;
    }

    // Key maxima of the positive lobes after the first negative crossing; first one near the top wins.
    int firstKey = -1;
    float globalMax = 0.0f;
    for (int pass = 0; pass < 2 && firstKey < 0; ++pass)
    {
        int tau = 1;
        while (tau <= maxLag && nsdf[(size_t) tau] > 0.0f) ++tau;
        while (tau <= maxLag)
        {
            while (tau <= maxLag && nsdf[(size_t) tau] <= 0.0f) ++tau;
            int keyLag = -1;
            float keyVal = 0.0f;
            while (tau <= maxLag && nsdf[(size_t) tau] > 0.0f)
            {
                if (nsdf[(size_t) tau] > keyVal) { keyVal = nsdf[(size_t) tau]; keyLag = tau; }
                ++tau;
            }
            if (keyLag < minLag || keyLag >= maxLag) continue;
            if (pass == 0) globalMax = std::max (globalMax, keyVal);
            else if (keyVal >= 0.9f * globalMax) { firstKey = keyLag; break; }
        }
    }

    if (firstKey < 0 || nsdf[(size_t) firstKey] < kTrackClarity)
    {
        trackedPeriod = 0.0f;   // chord / noise: guitar-sized geometry
        return;
    }

    // Parabolic refinement, then back to full-rate samples.
    const float a = nsdf[(size_t) firstKey - 1], b = nsdf[(size_t) firstKey], c = nsdf[(size_t) firstKey + 1];
    const float den = a - 2.0f * b + c;
    const float lag = (float) firstKey + (std::abs (den) > 1.0e-9f ? 0.5f * (a - c) / den : 0.0f);
    const float period = lag * (float) decimation;

    // Periods the default geometry already handles (guitar range) need no special treatment.
    trackedPeriod = period > (float) (sampleRate / kAdaptBelowHz) ? period : 0.0f;
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

float WsolaPitchShifter::coarseScore (int referenceEnd, int candidateEnd, int length) const noexcept
{
    constexpr int cMask = kCoarseSize - 1;
    float correlation = 0.0f, energy = 1.0e-9f;
    for (int k = 0; k < length; ++k)
    {
        const float a = coarse[(size_t) ((referenceEnd - k) & cMask)];
        const float b = coarse[(size_t) ((candidateEnd - k) & cMask)];
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

    int best = lo;
    float bestScore = -1.0e30f;

    if (trackedPeriod > 0.0f && decimation > 1)
    {
        // Long windows (bass): search on the decimated copy, then refine +/- one decimation step.
        // Full-rate delay d maps to coarse delay (d - decimationPhase) / decimation.
        const int newest = coarseWrite - 1;
        const int length = std::max (8, correlationLength / decimation);
        const int coarseRef = newest - (int) std::lround ((outgoingDelay - (float) decimationPhase) / (float) decimation);
        const int cLo = (int) std::ceil  ((float) (lo - decimationPhase) / (float) decimation);
        const int cHi = (int) std::floor ((float) (hi - decimationPhase) / (float) decimation);

        for (int c = cLo; c <= cHi; ++c)
        {
            const float score = coarseScore (coarseRef, newest - c, length);
            if (score > bestScore) { bestScore = score; best = c * decimation + decimationPhase; }
        }

        const int centreLag = std::clamp (best, lo, hi);
        bestScore = -1.0e30f;
        for (int lag = std::max (lo, centreLag - decimation); lag <= std::min (hi, centreLag + decimation); ++lag)
        {
            const float score = correlationScore (referenceEnd, writePos - lag, 1);
            if (score > bestScore) { bestScore = score; best = lag; }
        }
        return std::clamp ((float) best + fraction, lowest, highest);
    }

    // Coarse pass (every 2nd lag, every 2nd sample) then refine +/-1 at full resolution.
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
    estimatePeriod();
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

        {
            const float x = analysis[(size_t) writePos];
            const float y = lpB0 * x + lpZ1;
            lpZ1 = lpB1 * x - lpA1 * y + lpZ2;
            lpZ2 = lpB2 * x - lpA2 * y;
            if (++decimationPhase >= decimation)
            {
                decimationPhase = 0;
                coarse[(size_t) coarseWrite] = y;
                coarseWrite = (coarseWrite + 1) & (kCoarseSize - 1);
            }
        }

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
