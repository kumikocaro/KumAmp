#pragma once

#include "DspMath.h"

#include <cmath>

namespace kumamp
{
/** Transposed direct form II biquad with RBJ cookbook designs.
    Coefficient updates are plain arithmetic: safe on the audio thread. */
class Biquad
{
public:
    void setHighPass (double sampleRate, float frequency, float q = 0.7071f) noexcept { design (Type::HighPass, sampleRate, frequency, q, 0.0f); }
    void setLowPass (double sampleRate, float frequency, float q = 0.7071f) noexcept  { design (Type::LowPass, sampleRate, frequency, q, 0.0f); }
    void setBandPass (double sampleRate, float frequency, float q) noexcept           { design (Type::BandPass, sampleRate, frequency, q, 0.0f); }
    void setPeak (double sampleRate, float frequency, float q, float gainDb) noexcept { design (Type::Peak, sampleRate, frequency, q, gainDb); }
    void setLowShelf (double sampleRate, float frequency, float gainDb, float q = 0.7071f) noexcept  { design (Type::LowShelf, sampleRate, frequency, q, gainDb); }
    void setHighShelf (double sampleRate, float frequency, float gainDb, float q = 0.7071f) noexcept { design (Type::HighShelf, sampleRate, frequency, q, gainDb); }
    void setNotch (double sampleRate, float frequency, float q) noexcept              { design (Type::Notch, sampleRate, frequency, q, 0.0f); }

    void reset() noexcept { z1 = z2 = 0.0f; }

    /** Takes another filter's coefficients, keeps this filter's state (no clicks). */
    void copyCoefficientsFrom (const Biquad& other) noexcept
    {
        b0 = other.b0; b1 = other.b1; b2 = other.b2; a1 = other.a1; a2 = other.a2;
    }

    float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    /** Magnitude response (linear) at `frequency`, for tests and displays. */
    float magnitudeAt (double sampleRate, float frequency) const noexcept
    {
        const double w = 2.0 * 3.14159265358979323846 * frequency / sampleRate;
        const double c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2 * w), s2 = std::sin (2 * w);
        const double nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2);
        const double dr = 1.0 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
        return (float) std::sqrt ((nr * nr + ni * ni) / (dr * dr + di * di));
    }

private:
    enum class Type { LowPass, HighPass, BandPass, Peak, LowShelf, HighShelf, Notch };

    void design (Type type, double sampleRate, float frequency, float q, float gainDb) noexcept
    {
        const float f = std::clamp (frequency, 10.0f, 0.49f * (float) sampleRate);
        const float w = 2.0f * kPi * f / (float) sampleRate;
        const float cosw = std::cos (w), sinw = std::sin (w);
        const float alpha = sinw / (2.0f * std::max (q, 0.05f));
        const float A = std::pow (10.0f, gainDb / 40.0f);

        float nb0 = 1, nb1 = 0, nb2 = 0, na0 = 1, na1 = 0, na2 = 0;
        switch (type)
        {
            case Type::LowPass:
                nb0 = (1 - cosw) * 0.5f; nb1 = 1 - cosw; nb2 = nb0;
                na0 = 1 + alpha; na1 = -2 * cosw; na2 = 1 - alpha;
                break;
            case Type::HighPass:
                nb0 = (1 + cosw) * 0.5f; nb1 = -(1 + cosw); nb2 = nb0;
                na0 = 1 + alpha; na1 = -2 * cosw; na2 = 1 - alpha;
                break;
            case Type::BandPass:   // constant 0 dB peak gain
                nb0 = alpha; nb1 = 0; nb2 = -alpha;
                na0 = 1 + alpha; na1 = -2 * cosw; na2 = 1 - alpha;
                break;
            case Type::Notch:
                nb0 = 1; nb1 = -2 * cosw; nb2 = 1;
                na0 = 1 + alpha; na1 = -2 * cosw; na2 = 1 - alpha;
                break;
            case Type::Peak:
                nb0 = 1 + alpha * A; nb1 = -2 * cosw; nb2 = 1 - alpha * A;
                na0 = 1 + alpha / A; na1 = -2 * cosw; na2 = 1 - alpha / A;
                break;
            case Type::LowShelf:
            {
                const float s = 2 * std::sqrt (A) * alpha;
                nb0 = A * ((A + 1) - (A - 1) * cosw + s);
                nb1 = 2 * A * ((A - 1) - (A + 1) * cosw);
                nb2 = A * ((A + 1) - (A - 1) * cosw - s);
                na0 = (A + 1) + (A - 1) * cosw + s;
                na1 = -2 * ((A - 1) + (A + 1) * cosw);
                na2 = (A + 1) + (A - 1) * cosw - s;
                break;
            }
            case Type::HighShelf:
            {
                const float s = 2 * std::sqrt (A) * alpha;
                nb0 = A * ((A + 1) + (A - 1) * cosw + s);
                nb1 = -2 * A * ((A - 1) + (A + 1) * cosw);
                nb2 = A * ((A + 1) + (A - 1) * cosw - s);
                na0 = (A + 1) - (A - 1) * cosw + s;
                na1 = 2 * ((A - 1) - (A + 1) * cosw);
                na2 = (A + 1) - (A - 1) * cosw - s;
                break;
            }
        }

        b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0;
        a1 = na1 / na0; a2 = na2 / na0;
    }

    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;
};
} // namespace kumamp
