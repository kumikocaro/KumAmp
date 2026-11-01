#pragma once

#include "WsolaPitchShifter.h"
#include "../../Core/Biquad.h"
#include "../../Core/DspPrimitives.h"
#include "../../Core/RealtimePitchTracker.h"
#include "../../Core/Smoothing.h"
#include "../../Graph/DSPBlock.h"

#include <array>
#include <vector>

namespace kumamp
{
/**
    Guitar synthesizer.

    Mono: the real-time pitch tracker drives band-limited oscillators (saw /
          square / pulse) plus a sub-octave square, with glide. The guitar's
          own dynamics shape the amplitude (attack / release) and open a
          resonant low-pass (envelope amount). Single notes only.
    Poly: chords work: the guitar is squared into a synth-like waveform
          (fuzz), a polyphonic -1 octave (WSOLA) adds the sub, and the same
          envelope filter shapes it. It is the classic "synth pedal" approach.
*/
class GuitarSynthBlock final : public DSPBlock
{
public:
    enum Param { Mode, Wave, Octave, Sub, CutoffHz, Resonance, EnvAmount, AttackMs, ReleaseMs, GlideMs, Mix, LevelDb, NumParams };

    explicit GuitarSynthBlock (StoreId id);

    /** UI: frequency the mono synth is currently playing (0 = silent). */
    float getPlayingFrequency() const noexcept { return playingFrequency.load (std::memory_order_relaxed); }

protected:
    void prepareBlock (double, int) override;
    void resetBlock() noexcept override;
    void processBlock (float*, float*, int) noexcept override;

private:
    void renderMono (const float* input, float* output, int numSamples) noexcept;
    void renderPoly (const float* input, float* output, int numSamples) noexcept;

    RealtimePitchTracker tracker;
    WsolaPitchShifter subShifter;
    StateVariableFilter filter;
    Biquad inputHighPass;
    EnvelopeFollower envelope;
    std::vector<float> mono, synth, subBuffer;

    struct PitchUpdate { int offset; bool valid; float frequency; };
    std::array<PitchUpdate, 64> updates {};

    double phase = 0.0, subPhase = 0.0;
    float frequency = 110.0f, targetFrequency = 110.0f;
    bool hasPitch = false;
    LinearSmoother mix, level;
    std::atomic<float> playingFrequency { 0.0f };
};
} // namespace kumamp
