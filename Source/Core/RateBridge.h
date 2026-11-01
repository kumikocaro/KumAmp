#pragma once

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace kumamp
{
/**
    Runs a fixed-rate mono processor (e.g. a NAM model trained at 48 kHz) inside
    a host running at another rate: host -> model rate -> process -> host rate.

    Streaming 4-point Hermite resampling with ~3 samples of priming, so every
    host block always gets exactly numSamples output samples. All buffers are
    sized in prepare(); process() is RT-safe.

    Quality note: Hermite has no anti-alias filter. Fine for 44.1 <-> 48 kHz
    guitar signals; a polyphase FIR can replace it later without API changes.
*/
class RateBridge
{
public:
    void prepare (double hostRate, double modelRate, int maxHostBlock)
    {
        active = hostRate > 0.0 && modelRate > 0.0 && std::abs (hostRate - modelRate) > 0.5;
        stepIn  = hostRate / modelRate;   // host samples per model sample
        stepOut = modelRate / hostRate;   // model samples per host sample

        const int maxModel = (int) std::ceil ((double) maxHostBlock * stepOut) + 8;
        maxModelBlock = active ? maxModel : maxHostBlock;

        inBuffer.assign ((size_t) maxHostBlock + 16, 0.0f);
        midBuffer.assign ((size_t) maxModel, 0.0f);
        outBuffer.assign ((size_t) maxModel + 32, 0.0f);
        reset();
    }

    void reset() noexcept
    {
        std::fill (inBuffer.begin(), inBuffer.end(), 0.0f);
        std::fill (outBuffer.begin(), outBuffer.end(), 0.0f);
        inCount = 1;   inPhase = 1.0;    // one sample of history for Hermite
        outCount = 4;  outPhase = 1.0;   // priming absorbs per-block count jitter
    }

    bool isActive() const noexcept { return active; }

    /** Largest block the wrapped processor will ever receive. */
    int getMaxModelBlock() const noexcept { return maxModelBlock; }

    /** `model (float* data, int numSamples)` processes in place at model rate. */
    template <typename ModelFn>
    void process (const float* input, float* output, int numSamples, ModelFn&& model) noexcept
    {
        if (! active)
        {
            if (output != input)
                std::memcpy (output, input, sizeof (float) * (size_t) numSamples);
            model (output, numSamples);
            return;
        }

        // 1. host -> model rate
        std::memcpy (inBuffer.data() + inCount, input, sizeof (float) * (size_t) numSamples);
        inCount += numSamples;

        int produced = 0;
        const int maxMid = (int) midBuffer.size();
        while (inPhase + 2.0 < (double) inCount && produced < maxMid)
        {
            midBuffer[(size_t) produced++] = interpolate (inBuffer.data(), inPhase);
            inPhase += stepIn;
        }
        compact (inBuffer, inCount, inPhase);

        // 2. model
        if (produced > 0)
            model (midBuffer.data(), produced);

        // 3. model -> host rate
        produced = std::min (produced, (int) outBuffer.size() - outCount);
        std::memcpy (outBuffer.data() + outCount, midBuffer.data(), sizeof (float) * (size_t) produced);
        outCount += produced;

        for (int i = 0; i < numSamples; ++i)
        {
            if (outPhase + 2.0 < (double) outCount)
            {
                output[i] = interpolate (outBuffer.data(), outPhase);
                outPhase += stepOut;
            }
            else
            {
                output[i] = 0.0f;   // underrun: cannot happen in steady state
            }
        }
        compact (outBuffer, outCount, outPhase);
    }

private:
    static float interpolate (const float* x, double phase) noexcept
    {
        const int i = (int) phase;
        const float t = (float) (phase - (double) i);
        const float xm1 = x[i - 1], x0 = x[i], x1 = x[i + 1], x2 = x[i + 2];
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }

    /** Drop consumed samples, keeping one sample of history before the read phase. */
    static void compact (std::vector<float>& buffer, int& count, double& phase) noexcept
    {
        const int drop = std::clamp ((int) phase - 1, 0, count);
        if (drop > 0)
        {
            std::memmove (buffer.data(), buffer.data() + drop, sizeof (float) * (size_t) (count - drop));
            count -= drop;
            phase -= (double) drop;
        }
    }

    std::vector<float> inBuffer, midBuffer, outBuffer;
    int inCount = 1, outCount = 4, maxModelBlock = 0;
    double inPhase = 1.0, outPhase = 1.0, stepIn = 1.0, stepOut = 1.0;
    bool active = false;
};
} // namespace kumamp
