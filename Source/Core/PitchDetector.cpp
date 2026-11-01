#include "PitchDetector.h"

#include <algorithm>
#include <cmath>

namespace kumamp
{
void PitchDetector::prepare (double analysisRate, int newWindowSize, float minHz, float maxHz)
{
    rate = analysisRate;
    windowSize = std::max (64, newWindowSize);
    maxLag = std::min (windowSize / 2, (int) std::ceil (rate / (double) minHz));
    minLag = std::max (2, (int) std::floor (rate / (double) maxHz));

    centred.assign ((size_t) windowSize, 0.0f);
    nsdf.assign ((size_t) maxLag + 2, 0.0f);
}

PitchDetector::Result PitchDetector::analyse (const float* window) noexcept
{
    Result result;
    const int n = windowSize;

    double mean = 0.0;
    for (int i = 0; i < n; ++i)
        mean += window[i];
    mean /= n;

    double energy = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const float v = window[i] - (float) mean;
        centred[(size_t) i] = v;
        energy += (double) v * v;
    }

    result.levelDb = energy > 0.0 ? 10.0f * (float) std::log10 (energy / n) : -120.0f;
    if (result.levelDb < kSilenceDb)
        return result;

    // NSDF: n'(tau) = 2 r(tau) / m(tau), with m updated incrementally.
    const float* x = centred.data();
    double m = 2.0 * energy;
    for (int tau = 0; tau <= maxLag + 1 && tau < n; ++tau)
    {
        if (tau > 0)
            m -= (double) x[tau - 1] * x[tau - 1] + (double) x[n - tau] * x[n - tau];

        double r = 0.0;
        for (int j = 0; j < n - tau; ++j)
            r += (double) x[j] * x[j + tau];

        nsdf[(size_t) tau] = m > 1.0e-12 ? (float) (2.0 * r / m) : 0.0f;
    }

    // Key maxima: highest point of each positive lobe after the first negative zero crossing.
    int bestTau = -1;
    float overallMax = 0.0f;
    struct Peak { int tau; float value; };
    Peak peaks[64];
    int numPeaks = 0;

    int tau = 1;
    while (tau <= maxLag && nsdf[(size_t) tau] > 0.0f)   // skip the lobe around tau = 0
        ++tau;

    while (tau <= maxLag && numPeaks < 64)
    {
        while (tau <= maxLag && nsdf[(size_t) tau] <= 0.0f)
            ++tau;
        if (tau > maxLag)
            break;

        Peak peak { tau, nsdf[(size_t) tau] };
        while (tau <= maxLag && nsdf[(size_t) tau] > 0.0f)
        {
            if (nsdf[(size_t) tau] > peak.value)
                peak = { tau, nsdf[(size_t) tau] };
            ++tau;
        }

        if (peak.tau >= minLag && peak.tau < maxLag)
        {
            peaks[numPeaks++] = peak;
            overallMax = std::max (overallMax, peak.value);
        }
    }

    for (int i = 0; i < numPeaks; ++i)
        if (peaks[i].value >= kKeyMaximumRatio * overallMax)
        {
            bestTau = peaks[i].tau;
            break;
        }

    if (bestTau < 1)
        return result;

    // Parabolic refinement of the period and of the peak height.
    const float a = nsdf[(size_t) bestTau - 1], b = nsdf[(size_t) bestTau], c = nsdf[(size_t) bestTau + 1];
    const float denominator = a - 2.0f * b + c;
    const float delta = std::abs (denominator) > 1.0e-9f ? std::clamp (0.5f * (a - c) / denominator, -0.5f, 0.5f) : 0.0f;
    const float period = (float) bestTau + delta;

    result.clarity   = b - 0.25f * (a - c) * delta;
    result.frequency = (float) rate / period;
    result.valid     = result.clarity >= kMinClarity;
    return result;
}

NoteInfo frequencyToNote (float frequency, float referenceA4) noexcept
{
    NoteInfo info;
    if (frequency <= 0.0f || referenceA4 <= 0.0f)
        return info;

    const float midi = 69.0f + 12.0f * std::log2 (frequency / referenceA4);
    info.midiNote = (int) std::lround (midi);
    info.cents = (midi - (float) info.midiNote) * 100.0f;
    return info;
}

const char* noteName (int midiNote) noexcept
{
    static constexpr const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return midiNote < 0 ? "-" : names[midiNote % 12];
}

int noteOctave (int midiNote) noexcept
{
    return midiNote < 0 ? 0 : midiNote / 12 - 1;
}
} // namespace kumamp
