#pragma once

#include "../../Core/Biquad.h"

#include <array>

namespace kumamp
{
/** Band model shared by the EQ DSP and the EQ display (so the drawn curve is
    exactly the filter that runs). */
enum class EqBandType { Bell, LowShelf, HighShelf, LowCut, HighCut, Notch, BandPass, NumTypes };

struct EqBand
{
    bool on = false;
    EqBandType type = EqBandType::Bell;
    float frequency = 1000.0f, gainDb = 0.0f, q = 1.0f;
    int slope = 1;   // cuts: 0..3 = 12 / 24 / 36 / 48 dB/oct
};

inline constexpr int kMaxEqStages = 4;

/** Designs the cascade for a band; returns the number of active stages. */
inline int designEqBand (const EqBand& band, double sampleRate, std::array<Biquad, kMaxEqStages>& stages) noexcept
{
    if (! band.on)
        return 0;

    switch (band.type)
    {
        case EqBandType::Bell:      stages[0].setPeak (sampleRate, band.frequency, band.q, band.gainDb); return 1;
        case EqBandType::LowShelf:  stages[0].setLowShelf (sampleRate, band.frequency, band.gainDb, std::min (band.q, 2.0f)); return 1;
        case EqBandType::HighShelf: stages[0].setHighShelf (sampleRate, band.frequency, band.gainDb, std::min (band.q, 2.0f)); return 1;
        case EqBandType::Notch:     stages[0].setNotch (sampleRate, band.frequency, band.q); return 1;
        case EqBandType::BandPass:  stages[0].setBandPass (sampleRate, band.frequency, band.q); return 1;
        case EqBandType::LowCut:
        case EqBandType::HighCut:
        {
            // Butterworth cascade of order 2 * n; the band's Q adds resonance to the sharpest stage.
            const int n = std::clamp (band.slope, 0, 3) + 1;
            const int order = 2 * n;
            for (int k = 0; k < n; ++k)
            {
                float q = 1.0f / (2.0f * std::sin ((2.0f * (float) k + 1.0f) * kPi / (2.0f * (float) order)));
                if (k == 0)
                    q *= band.q / 0.7071f;
                if (band.type == EqBandType::LowCut) stages[(size_t) k].setHighPass (sampleRate, band.frequency, q);
                else                                 stages[(size_t) k].setLowPass (sampleRate, band.frequency, q);
            }
            return n;
        }
        case EqBandType::NumTypes: break;
    }
    return 0;
}

/** Magnitude (dB) of one band at `frequency`, for drawing. */
inline float eqBandMagnitudeDb (const EqBand& band, double sampleRate, float frequency) noexcept
{
    std::array<Biquad, kMaxEqStages> stages;
    const int n = designEqBand (band, sampleRate, stages);
    float magnitude = 1.0f;
    for (int s = 0; s < n; ++s)
        magnitude *= stages[(size_t) s].magnitudeAt (sampleRate, frequency);
    return 20.0f * std::log10 (std::max (magnitude, 1.0e-6f));
}
} // namespace kumamp
