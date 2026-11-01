#pragma once

#include "DspMath.h"

#include <cmath>

namespace kumamp
{
/** Transposed direct form II biquad with RBJ high/low-pass design.
    Coefficient updates are plain arithmetic: safe on the audio thread. */
class Biquad
{
public:
    void setHighPass (double sampleRate, float frequency, float q = 0.7071f) noexcept { design (sampleRate, frequency, q, true); }
    void setLowPass (double sampleRate, float frequency, float q = 0.7071f) noexcept  { design (sampleRate, frequency, q, false); }
    void reset() noexcept { z1 = z2 = 0.0f; }

    float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

private:
    void design (double sampleRate, float frequency, float q, bool highPass) noexcept
    {
        const float f = std::clamp (frequency, 10.0f, 0.49f * (float) sampleRate);
        const float w = 2.0f * kPi * f / (float) sampleRate;
        const float cosw = std::cos (w);
        const float alpha = std::sin (w) / (2.0f * q);
        const float a0 = 1.0f + alpha;

        const float k = highPass ? (1.0f + cosw) * 0.5f : (1.0f - cosw) * 0.5f;
        b0 = k / a0;
        b1 = (highPass ? -2.0f * k : 2.0f * k) / a0;
        b2 = k / a0;
        a1 = -2.0f * cosw / a0;
        a2 = (1.0f - alpha) / a0;
    }

    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;
};
} // namespace kumamp
