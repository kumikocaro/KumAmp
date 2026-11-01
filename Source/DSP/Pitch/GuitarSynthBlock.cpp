#include "GuitarSynthBlock.h"

#include <iterator>

namespace kumamp
{
namespace
{
    constexpr ParamInfo kParams[] = {
        { "mode",      "Modo",        0.0f,     1.0f,    0.0f, "Mono|Poly", true },
        { "wave",      "Onda",        0.0f,     2.0f,    0.0f, "Saw|Square|Pulse", true },
        { "octave",    "Octava",      0.0f,     3.0f,    2.0f, "-2|-1|0|+1", true },
        { "sub",       "Sub",         0.0f,   100.0f,   30.0f, "%",  false },
        { "cutoff",    "Cutoff",    100.0f, 12000.0f, 1200.0f, "Hz", false },
        { "resonance", "Resonancia",  0.5f,    15.0f,    4.0f, "",   false },
        { "env",       "Env",         0.0f,   100.0f,   60.0f, "%",  false },
        { "attack",    "Attack",      1.0f,   500.0f,    5.0f, "ms", false },
        { "release",   "Release",    10.0f,  2000.0f,  250.0f, "ms", false },
        { "glide",     "Glide",       0.0f,   500.0f,   20.0f, "ms", false },
        { "mix",       "Mezcla",      0.0f,   100.0f,  100.0f, "%",  false },
        { "level",     "Nivel",     -24.0f,    12.0f,    0.0f, "dB", false },
    };
    static_assert (std::size (kParams) == GuitarSynthBlock::NumParams);

    constexpr float kOctaveRatio[] = { 0.25f, 0.5f, 1.0f, 2.0f };
    constexpr float kGateDb = -55.0f;

    /** PolyBLEP residual: removes the aliasing of naive saw / square edges. */
    inline float polyBlep (float t, float dt) noexcept
    {
        if (t < dt)        { t /= dt; return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }

    inline float oscillator (int wave, float t, float dt) noexcept
    {
        switch (wave)
        {
            case 0:  return 2.0f * t - 1.0f - polyBlep (t, dt);                                    // saw
            case 1:
            {
                float t2 = t + 0.5f; if (t2 >= 1.0f) t2 -= 1.0f;
                return (t < 0.5f ? 1.0f : -1.0f) + polyBlep (t, dt) - polyBlep (t2, dt);          // square
            }
            default:
            {
                float t2 = t + 0.75f; if (t2 >= 1.0f) t2 -= 1.0f;
                return (t < 0.25f ? 1.0f : -1.0f) + polyBlep (t, dt) - polyBlep (t2, dt) - 0.5f;  // 25 % pulse
            }
        }
    }
} // namespace

GuitarSynthBlock::GuitarSynthBlock (StoreId id) : DSPBlock (id, BlockType::GuitarSynth, kParams, NumParams) {}

void GuitarSynthBlock::prepareBlock (double sampleRate, int maxBlockSize)
{
    tracker.prepare (sampleRate);
    subShifter.prepare (sampleRate);
    subShifter.setGrainMs (25.0f);
    subShifter.setRatio (0.5f, 0.0f);
    inputHighPass.setHighPass (sampleRate, 70.0f);
    for (auto* v : { &mono, &synth, &subBuffer })
        v->assign ((size_t) maxBlockSize, 0.0f);
    mix.prepare (sampleRate, 0.02);
    level.prepare (sampleRate, 0.02);
    resetBlock();
}

void GuitarSynthBlock::resetBlock() noexcept
{
    tracker.reset();
    subShifter.reset();
    filter.reset();
    inputHighPass.reset();
    envelope.reset();
    phase = subPhase = 0.0;
    hasPitch = false;
    mix.setCurrentAndTarget (getParam (Mix) * 0.01f);
    level.setCurrentAndTarget (dbToGain (getParam (LevelDb)));
}

void GuitarSynthBlock::renderMono (const float* input, float* output, int numSamples) noexcept
{
    // 1. Track pitch over the block; remember where each estimate lands.
    int numUpdates = 0;
    tracker.process (input, numSamples, [&] (int offset, const RealtimePitchTracker::Estimate& e)
    {
        if (numUpdates < (int) updates.size())
            updates[(size_t) numUpdates++] = { offset, e.valid && e.fastDb > kGateDb, e.frequency };
    });

    const auto sr = (float) getSampleRate();
    const float ratio = kOctaveRatio[std::clamp ((int) std::lround (getParam (Octave)), 0, 3)];
    const int wave = (int) std::lround (getParam (Wave));
    const float subAmount = getParam (Sub) * 0.01f;
    const float glideCoefficient = EnvelopeFollower::coefficient (sr, std::max (2.0f, getParam (GlideMs)));

    // 2. Synthesize, applying pitch updates at their sample positions.
    int next = 0;
    for (int i = 0; i < numSamples; ++i)
    {
        while (next < numUpdates && updates[(size_t) next].offset <= i)
        {
            const auto& u = updates[(size_t) next++];
            if (u.valid)
            {
                targetFrequency = u.frequency * ratio;
                if (! hasPitch)
                    frequency = targetFrequency;   // new note: no glide from the previous one
                hasPitch = true;
            }
        }

        const float env = envelope.process (input[i]);
        if (env < dbToGain (kGateDb) * 0.5f)
            hasPitch = false;

        frequency = targetFrequency + glideCoefficient * (frequency - targetFrequency);
        const float dt = std::min (0.45f, frequency / sr);
        phase += dt;    if (phase >= 1.0)    phase -= 1.0;
        subPhase += dt * 0.5f; if (subPhase >= 1.0) subPhase -= 1.0;

        const float main = oscillator (wave, (float) phase, dt);
        const float sub = oscillator (1, (float) subPhase, dt * 0.5f);
        // The guitar's envelope is the VCA (x3: a picked string peaks well below full scale).
        output[i] = (main * (1.0f - 0.5f * subAmount) + sub * subAmount) * std::min (1.0f, env * 3.0f);
    }
    playingFrequency.store (hasPitch ? frequency : 0.0f, std::memory_order_relaxed);
}

void GuitarSynthBlock::renderPoly (const float* input, float* output, int numSamples) noexcept
{
    const float subAmount = getParam (Sub) * 0.01f;
    const bool bright = (int) std::lround (getParam (Wave)) == 0;   // "Saw": add octave-up brightness

    if (subAmount > 0.0f)
        subShifter.process (input, input, subBuffer.data(), subBuffer.data(), numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        const float x = inputHighPass.process (input[i]);
        const float env = envelope.process (x);
        // Square the waveform (synth-like, works on chords), then restore the dynamics.
        float y = std::tanh (x * 60.0f);
        if (bright)
            y = 0.7f * y + 0.3f * (std::tanh (std::abs (x) * 60.0f) * 2.0f - 1.0f);
        if (subAmount > 0.0f)
            y = y * (1.0f - 0.5f * subAmount) + std::tanh (subBuffer[(size_t) i] * 60.0f) * subAmount;
        output[i] = y * std::min (1.0f, env * 3.0f);
    }
    playingFrequency.store (0.0f, std::memory_order_relaxed);
}

void GuitarSynthBlock::processBlock (float* left, float* right, int numSamples) noexcept
{
    const double sr = getSampleRate();
    envelope.setTimes (sr, getParam (AttackMs), getParam (ReleaseMs));
    mix.setTarget (getParam (Mix) * 0.01f);
    level.setTarget (dbToGain (getParam (LevelDb)));

    for (int i = 0; i < numSamples; ++i)
        mono[(size_t) i] = 0.5f * (left[i] + right[i]);

    if (getParam (Mode) > 0.5f) renderPoly (mono.data(), synth.data(), numSamples);
    else                        renderMono (mono.data(), synth.data(), numSamples);

    // Envelope-controlled resonant low-pass (the "filter sweep" of the synth).
    const float cutoff = getParam (CutoffHz), resonance = getParam (Resonance);
    const float envAmount = getParam (EnvAmount) * 0.01f;
    for (int i = 0; i < numSamples; ++i)
    {
        if ((i & 15) == 0)
        {
            const float open = std::min (1.0f, envelope.getValue() * 4.0f);
            filter.set (sr, std::min (18000.0f, cutoff * std::exp2 (5.0f * envAmount * open)), resonance);
        }
        const float y = filter.process (synth[(size_t) i]).low * 0.5f;
        const float m = mix.next(), g = level.next();
        left[i]  = (left[i]  + (y - left[i])  * m) * g;
        right[i] = (right[i] + (y - right[i]) * m) * g;
    }
}
} // namespace kumamp
