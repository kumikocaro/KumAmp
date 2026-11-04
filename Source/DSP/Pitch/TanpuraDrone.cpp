#include "TanpuraDrone.h"
#include "../../Core/DspMath.h"

#include <algorithm>
#include <cmath>

namespace kumamp
{
namespace
{
    constexpr float kVoiceLevel[] = { 0.8f, 1.0f, 0.9f, 0.9f };
    constexpr float kVoicePan[]   = { -0.45f, 0.5f, -0.2f, 0.15f };   // -1 left .. 1 right
    constexpr float kVoiceDetune[] = { 0.0f, -0.5f, 0.5f, 0.0f };     // the two Sa strings beat gently
    constexpr float kPartialSpread = 1.0f / 6.0f;                     // of the detune: slow phasing
    constexpr double kTwoPi = 6.283185307179586;
} // namespace

float TanpuraDrone::nextRandom() noexcept
{
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    return (float) randomState * (1.0f / 4294967296.0f);
}

void TanpuraDrone::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate;
    reset();
}

void TanpuraDrone::reset() noexcept
{
    randomState = 0x6d2b79f5u;
    for (size_t i = 0; i < voices.size(); ++i)
    {
        auto& v = voices[i];
        v.amp.fill (0.0f);
        v.step.fill (0.0f);
        for (int h = 0; h < kPartials; ++h)
        {
            const float phase = (float) kTwoPi * nextRandom();
            v.re[(size_t) h] = std::cos (phase);
            v.im[(size_t) h] = std::sin (phase);
            v.detune[(size_t) h] = nextRandom() - 0.5f;
        }
        v.sweepPhase = nextRandom();
        v.breathPhase = nextRandom();
        v.driftPhase = nextRandom();
        v.level = kVoiceLevel[i];
        const float angle = 0.25f * kPi * (1.0f + kVoicePan[i]);
        v.panLeft = std::cos (angle);
        v.panRight = std::sin (angle);
    }
    const auto notes = currentNotes;
    currentNotes = { -1, -1, -1, -1 };
    if (notes[0] >= 0)
        setNotes (notes);
    countdown = 0;
}

void TanpuraDrone::tune (Voice& v, int note, float cents) noexcept
{
    v.hz = 440.0f * std::exp2 ((float) (note - 69) / 12.0f + cents / 1200.0f);
    const float top = std::min (0.45f * (float) sampleRate, 10000.0f);
    v.count = std::clamp ((int) (top / v.hz), 1, kPartials);

    // A bright, open spectrum (1 / h^0.75), normalised to unit power.
    float power = 0.0f;
    for (int h = 0; h < v.count; ++h)
    {
        v.base[(size_t) h] = 1.0f / std::pow ((float) (h + 1), 0.75f);
        power += v.base[(size_t) h] * v.base[(size_t) h];
    }
    const float norm = 1.0f / std::sqrt (power);
    for (int h = 0; h < kPartials; ++h)
    {
        if (h >= v.count)
        {
            v.base[(size_t) h] = v.amp[(size_t) h] = v.step[(size_t) h] = 0.0f;
            continue;
        }
        v.base[(size_t) h] *= norm;
        const double w = kTwoPi * v.hz * (h + 1) * std::exp2 (v.detune[(size_t) h] * currentDetune * kPartialSpread / 1200.0f) / sampleRate;
        v.rotRe[(size_t) h] = (float) std::cos (w);
        v.rotIm[(size_t) h] = (float) std::sin (w);
    }
}

void TanpuraDrone::setNotes (const std::array<int, kStrings>& notes) noexcept
{
    if (notes == currentNotes)
        return;
    currentNotes = notes;
    for (size_t i = 0; i < voices.size(); ++i)
        tune (voices[i], notes[i], kVoiceDetune[i] * currentDetune);
}

void TanpuraDrone::control (int index, float texture, float movementSeconds, const Motion& motion) noexcept
{
    auto& v = voices[(size_t) index];
    const double dt = kControlInterval / sampleRate;
    // Each string moves at its own pace, so the four never line up.
    v.sweepPhase += dt / (movementSeconds * (1.0 + 0.17 * index));
    v.breathPhase += dt / (movementSeconds * 1.9 * (1.0 + 0.11 * index));
    v.sweepPhase -= std::floor (v.sweepPhase);
    v.breathPhase -= std::floor (v.breathPhase);
    v.driftPhase += 2.7 * dt / (movementSeconds * (1.0 + 0.17 * index));
    v.driftPhase -= std::floor (v.driftPhase);

    // The bridge resonance wanders between the 4th harmonic and the top of the string.
    const float highest = std::max (5.0f, std::min (0.75f * (float) v.count, 24.0f));
    const auto sweep = (float) (0.5 - 0.5 * std::cos (kTwoPi * v.sweepPhase));
    const auto wobble = (float) std::sin (kTwoPi * v.driftPhase);
    const float centre = 4.0f + (highest - 4.0f) * sweep + 1.5f * motion.drift * wobble;
    const float width = 1.6f + 1.4f * texture;
    const float breath = v.level * (1.0f + motion.breath * (float) std::sin (kTwoPi * v.breathPhase));
    const float floorLevel = 1.0f - 0.55f * texture, bump = 4.5f * texture;

    for (int h = 0; h < v.count; ++h)
    {
        const float d = ((float) (h + 1) - centre) / width;
        const float target = v.base[(size_t) h] * (floorLevel + bump * std::exp (-d * d)) * breath;
        v.step[(size_t) h] = (target - v.amp[(size_t) h]) / (float) kControlInterval;

        // Keep the rotators on the unit circle.
        const float m = v.re[(size_t) h] * v.re[(size_t) h] + v.im[(size_t) h] * v.im[(size_t) h];
        const float k = 0.5f * (3.0f - m);
        v.re[(size_t) h] *= k;
        v.im[(size_t) h] *= k;
    }
}

void TanpuraDrone::process (float* left, float* right, int numSamples, float texture, float movementSeconds, const Motion& motion) noexcept
{
    texture = std::clamp (texture, 0.0f, 1.0f);
    movementSeconds = std::max (0.5f, movementSeconds);

    // Retuning keeps every phase, so moving it is silent.
    if (std::abs (motion.detuneCents - currentDetune) > 1.0e-3f)
    {
        currentDetune = motion.detuneCents;
        for (size_t i = 0; i < voices.size(); ++i)
            if (currentNotes[i] >= 0)
                tune (voices[i], currentNotes[i], kVoiceDetune[i] * currentDetune);
    }

    for (int i = 0; i < numSamples; ++i)
    {
        if (--countdown <= 0)
        {
            countdown = kControlInterval;
            for (int v = 0; v < kStrings; ++v)
                control (v, texture, movementSeconds, motion);
        }

        float l = 0.0f, r = 0.0f;
        for (auto& v : voices)
        {
            float sum = 0.0f;
            for (int h = 0; h < v.count; ++h)
            {
                const auto k = (size_t) h;
                const float re = v.re[k] * v.rotRe[k] - v.im[k] * v.rotIm[k];
                const float im = v.re[k] * v.rotIm[k] + v.im[k] * v.rotRe[k];
                v.re[k] = re;
                v.im[k] = im;
                v.amp[k] += v.step[k];
                sum += v.amp[k] * im;
            }
            l += sum * v.panLeft;
            r += sum * v.panRight;
        }
        left[i] = l;
        right[i] = r;
    }
}
} // namespace kumamp
