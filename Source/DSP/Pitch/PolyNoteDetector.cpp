#include "PolyNoteDetector.h"
#include "../../Core/DspPrimitives.h"

#include <algorithm>
#include <cmath>

namespace kumamp
{
namespace
{
    constexpr float kBandQ = 10.0f;
    constexpr float kGate = 0.0025f;        // -52 dBFS band amplitude
    constexpr float kRelativeFloor = 0.06f; // -24 dB below the strongest band
    constexpr int kLowRegister = 12;   // bands below E3
    constexpr int kOnHops = 2, kOnHopsOnHarmonic = 6, kOffHops = 10;

    // Harmonics 1..8 in semitones above the fundamental, and their weight in the salience.
    constexpr int   kHarmonicOffset[] = { 0, 12, 19, 24, 28, 31, 34, 36 };
    constexpr float kHarmonicWeight[] = { 1.0f, 0.8f, 0.65f, 0.55f, 0.45f, 0.4f, 0.35f, 0.3f };
} // namespace

void PolyNoteDetector::prepare (double sampleRate)
{
    decimation = std::max (1, (int) std::lround (sampleRate / 8000.0));
    const double rate = sampleRate / decimation;
    // 6th-order Butterworth anti-alias at 0.4 x the decimated rate.
    constexpr float qs[] = { 0.5176f, 0.7071f, 1.9319f };
    for (size_t s = 0; s < antiAlias.size(); ++s)
        antiAlias[s].setLowPass (sampleRate, (float) (0.4 * rate), qs[s]);

    for (int b = 0; b < kNumBands; ++b)
    {
        const auto hz = (float) (440.0 * std::pow (2.0, (kLowestNote + b - 69) / 12.0));
        bands[(size_t) b].a.setBandPass (rate, hz, kBandQ);
        bands[(size_t) b].b.setBandPass (rate, hz, kBandQ);
        // Two cascaded one-poles over ~2 periods: the 2f ripple of y^2 is gone, yet the
        // high bands stay fast.
        bands[(size_t) b].smoothing = EnvelopeFollower::coefficient (rate, std::max (4.0f, 2000.0f / hz));
    }
    reset();
}

void PolyNoteDetector::reset() noexcept
{
    for (auto& f : antiAlias) f.reset();
    for (auto& b : bands)
    {
        b.a.reset();
        b.b.reset();
        b.power = b.lastPower = b.smoothed = 0.0f;
        b.active = false;
    }
    seen.fill (0);
    missing.fill (0);
    phase = sinceHop = 0;
    current = {};
    settlingNow = false;
}

void PolyNoteDetector::analyseSample (float x) noexcept
{
    for (auto& b : bands)
    {
        const float y = b.b.process (b.a.process (x));
        b.smoothed = y * y + b.smoothing * (b.smoothed - y * y);
        b.power = b.smoothed + b.smoothing * (b.power - b.smoothed);
    }
}

void PolyNoteDetector::updateNotes() noexcept
{
    std::array<float, kNumBands> amp {}, residual {};
    float strongest = 0.0f;
    for (int b = 0; b < kNumBands; ++b)
    {
        amp[(size_t) b] = std::sqrt (2.0f * bands[(size_t) b].power);   // sine amplitude
        strongest = std::max (strongest, amp[(size_t) b]);
    }
    residual = amp;

    // On a pick the fast high bands are ahead of the slow low ones: a band cannot start a
    // note while a possible fundamental below it (up to an octave and a fifth) still builds up.
    std::array<bool, kNumBands> rising {};
    settlingNow = false;
    for (int b = 0; b < kNumBands; ++b)
    {
        auto& band = bands[(size_t) b];
        rising[(size_t) b] = band.power > 1.19f * band.lastPower && amp[(size_t) b] > 0.5f * kGate;   // > 0.75 dB / hop
        band.lastPower = band.power;
        settlingNow = settlingNow || rising[(size_t) b];
    }

    std::array<bool, kNumBands> detected {};
    const float floor = std::max (kGate, strongest * kRelativeFloor);
    float firstSalience = 0.0f;
    constexpr int numCandidates = kHighestNote - kLowestNote + 1;

    for (int n = 0; n < kMaxNotes && strongest > kGate; ++n)
    {
        int best = -1;
        float bestSalience = 0.0f;
        for (int k = 0; k < numCandidates; ++k)
        {
            const float r = residual[(size_t) k];
            // A fundamental is a peak of its own band, in the spectrum and in what is left
            // of it (a neighbour's leakage never is). Below E3 the bands cannot resolve a
            // semitone during the pick, so a near-peak competes too (the harmonics decide).
            const float margin = k < kLowRegister ? 0.8f : 1.0f;
            const float leftR = k > 0 ? residual[(size_t) k - 1] : 0.0f, leftA = k > 0 ? amp[(size_t) k - 1] : 0.0f;
            if (r < floor || r < margin * leftR || amp[(size_t) k] < margin * leftA
                || r < margin * residual[(size_t) k + 1] || amp[(size_t) k] < margin * amp[(size_t) k + 1])
                continue;
            float salience = 0.0f;
            for (size_t h = 0; h < std::size (kHarmonicOffset); ++h)
                if (k + kHarmonicOffset[h] < kNumBands)
                    salience += kHarmonicWeight[h] * residual[(size_t) (k + kHarmonicOffset[h])];
            if (salience > bestSalience)
            {
                bestSalience = salience;
                best = k;
            }
        }
        if (best < 0 || bestSalience < 0.2f * firstSalience)
            break;
        if (n == 0)
            firstSalience = bestSalience;
        detected[(size_t) best] = true;

        // Remove the note's harmonics and their leakage into the neighbouring bands (all of
        // it next to a low fundamental: a semitone cluster down there is mud, not a chord).
        for (const int offset : kHarmonicOffset)
        {
            const int j = best + offset;
            if (j >= kNumBands)
                break;
            const float a = residual[(size_t) j];
            for (int d = -2; d <= 2; ++d)
                if (j + d >= 0 && j + d < kNumBands)
                {
                    const float near = j < kLowRegister ? 1.0f : 0.45f;
                    const float leak = d == 0 ? 1.0f : (std::abs (d) == 1 ? near : 0.15f);
                    residual[(size_t) (j + d)] = std::max (0.0f, residual[(size_t) (j + d)] - leak * a);
                }
        }
    }

    // A note that could be a harmonic of a lower band with real energy may be just that
    // harmonic while its fundamental is still being resolved (the pick): it needs longer
    // to be believed.
    std::array<bool, kNumBands> onHarmonic {};
    for (int k = 0; k < numCandidates; ++k)
        for (size_t h = 1; h < std::size (kHarmonicOffset) && ! onHarmonic[(size_t) k]; ++h)
        {
            const int fundamental = k - kHarmonicOffset[h];
            onHarmonic[(size_t) k] = fundamental >= 0 && amp[(size_t) fundamental] >= std::max (floor, 0.25f * amp[(size_t) k]);
        }

    // Hysteresis, then the ascending note list.
    current.count = 0;
    for (int k = 0; k < numCandidates; ++k)
    {
        auto& band = bands[(size_t) k];
        // (Down low the band's own neighbourhood must have settled too: until then it
        // cannot tell a semitone apart.)
        bool settling = false;
        const int last = k < kLowRegister ? std::min (kNumBands - 1, k + 2) : k - 1;
        for (int j = std::max (0, k - 19); j <= last && ! settling; ++j)
            settling = rising[(size_t) j];
        for (size_t h = 4; h < std::size (kHarmonicOffset) && ! settling; ++h)   // 5th..8th harmonic
            for (int d = -1; d <= 1 && ! settling; ++d)
            {
                const int j = k - kHarmonicOffset[h] + d;
                settling = j >= 0 && rising[(size_t) j];
            }
        if (detected[(size_t) k])
        {
            seen[(size_t) k] = (std::uint8_t) std::min (255, seen[(size_t) k] + 1);
            missing[(size_t) k] = 0;
            if (seen[(size_t) k] >= (onHarmonic[(size_t) k] ? kOnHopsOnHarmonic : kOnHops) && ! settling)
                band.active = true;
        }
        else
        {
            seen[(size_t) k] = 0;
            missing[(size_t) k] = (std::uint8_t) std::min (255, missing[(size_t) k] + 1);
            if (missing[(size_t) k] >= kOffHops || amp[(size_t) k] < 0.5f * kGate)
                band.active = false;
        }
        if (band.active && current.count < kMaxNotes)
        {
            current.notes[(size_t) current.count] = (std::uint8_t) (kLowestNote + k);
            current.levels[(size_t) current.count] = amp[(size_t) k];
            ++current.count;
        }
    }
}
} // namespace kumamp
