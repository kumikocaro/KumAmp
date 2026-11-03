#pragma once

#include "InstrumentSynth.h"
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
    Textura: a synthetic, guitar-like layer to blend under the real guitar
          (chords too). Each pick is detected and fires a plucked envelope
          (decay) that drives both the level and the filter, like a synth
          pluck; the voice is the squared guitar plus an octave up (shimmer)
          and the sub, widened in stereo by two slow modulated delays.
    Instrumento: KumAmp's synthesised instrument bank (piano, organs, strings,
          pads...) played by the guitar's notes or chords, with an optional
          tempo-synced sequencer / arpeggiator (see InstrumentSynth).
*/
class GuitarSynthBlock final : public DSPBlock
{
public:
    enum Param { Mode, Wave, Octave, Sub, CutoffHz, Resonance, EnvAmount, AttackMs, ReleaseMs, GlideMs, Mix, LevelDb,
                 DecayMs, Shimmer, Width,
                 Sound, Notes, Tone, Tail, Sequence, SeqRate, SeqGate, SeqRange, Swing, NumParams };
    enum class Voice { Mono, Poly, Texture, Instrument };

    explicit GuitarSynthBlock (StoreId id);

    /** UI: frequency the mono synth is currently playing (0 = silent). */
    float getPlayingFrequency() const noexcept { return playingFrequency.load (std::memory_order_relaxed); }

    bool isParamRelevant (int param) const noexcept override;
    std::vector<std::string> getInfoChips() const override;

protected:
    void prepareBlock (double, int) override;
    void resetBlock() noexcept override;
    void processBlock (float*, float*, int) noexcept override;

private:
    void renderMono (const float* input, float* output, int numSamples) noexcept;
    void renderPoly (const float* input, float* output, int numSamples) noexcept;
    void renderTexture (const float* input, float* output, int numSamples) noexcept;

    InstrumentSynth instrument;
    RealtimePitchTracker tracker;
    WsolaPitchShifter subShifter, upShifter;
    StateVariableFilter filter;
    Biquad inputHighPass;
    EnvelopeFollower envelope;
    std::vector<float> mono, synth, subBuffer, upBuffer, filterOpen;

    // Texture: pick detection -> plucked envelope; stereo widener.
    EnvelopeFollower fastEnvelope, slowEnvelope;
    float pluck = 0.0f, pluckTarget = 0.0f, pickPeak = 0.0f;
    int sinceOnset = 0;
    DelayLine widenLeft, widenRight;
    Lfo widenLfo;

    struct PitchUpdate { int offset; bool valid; float frequency; };
    std::array<PitchUpdate, 64> updates {};

    double phase = 0.0, subPhase = 0.0;
    float frequency = 110.0f, targetFrequency = 110.0f;
    bool hasPitch = false;
    LinearSmoother mix, level;
    std::atomic<float> playingFrequency { 0.0f };
};
} // namespace kumamp
