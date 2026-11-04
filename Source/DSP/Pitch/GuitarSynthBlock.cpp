#include "GuitarSynthBlock.h"
#include "SynthOscillators.h"

#include <iterator>

namespace kumamp
{
namespace
{
    constexpr ParamInfo kParams[] = {
        { "mode",      "Modo",        0.0f,     3.0f,    0.0f, "Mono|Poly|Textura|Instrumento", true },
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
        { "decay",     "Decay",      40.0f,  3000.0f,  450.0f, "ms", false },   // Textura: pluck length
        { "shimmer",   "+1 Oct",      0.0f,   100.0f,   35.0f, "%",  false },   // Textura: octave-up layer
        { "width",     "Ancho",       0.0f,   100.0f,   60.0f, "%",  false },   // Textura / Instrumento: stereo width
        // Instrumento
        { "sound",     "Sonido",      0.0f, (float) (InstrumentSynth::kNumSounds - 1), 0.0f, InstrumentSynth::kSoundChoices, true },
        { "notes",     "Notas",       0.0f,     1.0f,    0.0f, "Acordes|Nota simple", true },
        { "tone",      "Tono",     -100.0f,   100.0f,    0.0f, "%",  false },
        { "tail",      "Cola",     -100.0f,   100.0f,    0.0f, "%",  false },
        { "seq",       "Secuencia",   0.0f,     8.0f,    0.0f, InstrumentSynth::kSeqChoices, true },
        { "seq_rate",  "Ritmo",       0.0f,     5.0f,    3.0f, InstrumentSynth::kRateChoices, true },
        { "seq_gate",  "Duracion",    5.0f,   100.0f,   50.0f, "%",  false },
        { "seq_range", "Octavas",     1.0f,     3.0f,    1.0f, "",   true },
        { "swing",     "Swing",       0.0f,   100.0f,    0.0f, "%",  false },
    };
    static_assert (std::size (kParams) == GuitarSynthBlock::NumParams);

    constexpr float kOctaveRatio[] = { 0.25f, 0.5f, 1.0f, 2.0f };
    constexpr float kGateDb = -55.0f;
} // namespace

GuitarSynthBlock::GuitarSynthBlock (StoreId id) : DSPBlock (id, BlockType::GuitarSynth, kParams, NumParams) {}

bool GuitarSynthBlock::isParamRelevant (int param) const noexcept
{
    const auto voice = (Voice) std::clamp ((int) std::lround (getParam (Mode)), 0, 3);
    const bool playsInstrument = voice == Voice::Instrument;
    switch (param)
    {
        case Mode: case Octave: case Mix: case LevelDb:   return true;
        case Wave: case Sub: case CutoffHz: case Resonance: case EnvAmount:
        case AttackMs: case ReleaseMs:                     return ! playsInstrument;
        case GlideMs:                                      return voice == Voice::Mono;
        case DecayMs: case Shimmer:                        return voice == Voice::Texture;
        case Width:                                        return voice == Voice::Texture || playsInstrument;
        case Sound: case Notes: case Tone: case Tail: case Sequence: return playsInstrument;
        case SeqRate: case SeqGate: case SeqRange: case Swing:
            return playsInstrument && std::lround (getParam (Sequence)) != 0;
        default:                                           return true;
    }
}

std::vector<std::string> GuitarSynthBlock::getInfoChips() const
{
    if (std::lround (getParam (Mode)) != (long) Voice::Instrument)
        return {};
    std::vector<std::string> chips { InstrumentSynth::soundName ((int) std::lround (getParam (Sound))) };
    const auto notes = instrument.getHeldNotesText();
    if (! notes.empty())
        chips.push_back (notes);
    return chips;
}

void GuitarSynthBlock::prepareBlock (double sampleRate, int maxBlockSize)
{
    instrument.prepare (sampleRate, maxBlockSize);
    tracker.prepare (sampleRate);
    subShifter.prepare (sampleRate);
    subShifter.setGrainMs (25.0f);
    subShifter.setRatio (0.5f, 0.0f);
    upShifter.prepare (sampleRate);
    upShifter.setRatio (2.0f, 0.0f);
    fastEnvelope.setTimes (sampleRate, 0.3f, 25.0f);
    slowEnvelope.setTimes (sampleRate, 80.0f, 300.0f);
    widenLeft.prepare ((int) (0.03 * sampleRate));
    widenRight.prepare ((int) (0.03 * sampleRate));
    widenLfo.prepare (sampleRate);
    widenLfo.setFrequency (0.27f);
    inputHighPass.setHighPass (sampleRate, 70.0f);
    for (auto* v : { &mono, &synth, &subBuffer, &upBuffer, &filterOpen })
        v->assign ((size_t) maxBlockSize, 0.0f);
    mix.prepare (sampleRate, 0.02);
    level.prepare (sampleRate, 0.02);
    resetBlock();
}

void GuitarSynthBlock::resetBlock() noexcept
{
    instrument.reset();
    tracker.reset();
    subShifter.reset();
    upShifter.reset();
    fastEnvelope.reset();
    slowEnvelope.reset();
    widenLeft.reset();
    widenRight.reset();
    widenLfo.reset();
    pluck = pluckTarget = pickPeak = 0.0f;
    sinceOnset = 1 << 20;
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

        const float main = synth::oscillator (wave, (float) phase, dt);
        const float sub = synth::oscillator (1, (float) subPhase, dt * 0.5f);
        // The guitar's envelope is the VCA (x3: a picked string peaks well below full scale).
        output[i] = (main * (1.0f - 0.5f * subAmount) + sub * subAmount) * std::min (1.0f, env * 3.0f);
        filterOpen[(size_t) i] = std::min (1.0f, env * 4.0f);
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
        filterOpen[(size_t) i] = std::min (1.0f, env * 4.0f);
    }
    playingFrequency.store (0.0f, std::memory_order_relaxed);
}

void GuitarSynthBlock::renderTexture (const float* input, float* output, int numSamples) noexcept
{
    const auto sr = (float) getSampleRate();
    const float subAmount = getParam (Sub) * 0.01f;
    const float shimmer = getParam (Shimmer) * 0.01f;
    const int wave = (int) std::lround (getParam (Wave));
    const float decay = std::exp (-1.0f / (0.001f * getParam (DecayMs) * sr));
    const float pluckAttack = EnvelopeFollower::coefficient (sr, 2.0f);
    const int refractory = (int) (0.06f * sr);   // one pick, one pluck (a strum's strings blend in)
    const float gate = dbToGain (kGateDb);
    const float pickPeakDecay = EnvelopeFollower::coefficient (sr, 300.0f);

    if (subAmount > 0.0f)
        subShifter.process (input, input, subBuffer.data(), subBuffer.data(), numSamples);
    if (shimmer > 0.0f)
        upShifter.process (input, input, upBuffer.data(), upBuffer.data(), numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        const float x = inputHighPass.process (input[i]);
        const float env = envelope.process (x);
        // Peak envelope vs. its own slow version (same crest factor, so a chord's
        // ripple never looks like a pick).
        const float fast = fastEnvelope.process (x), slow = slowEnvelope.process (fast);

        // A pick: the fast envelope jumps well above both the slow one and the
        // (decaying) peak of the last pick, so one pick fires once.
        pickPeak = sinceOnset < refractory ? std::max (pickPeak, fast) : pickPeak * pickPeakDecay;
        if (++sinceOnset > refractory && fast > gate && fast > 1.5f * std::max (slow, pickPeak) + 1.0e-4f)
        {
            pluckTarget = 1.0f;
            sinceOnset = 0;
            pickPeak = fast;
        }
        pluckTarget *= decay;
        pluck = pluckTarget + pluckAttack * (pluck - pluckTarget);

        // Voice: squared guitar (soft) with an octave-up character set by "Onda".
        const float square = std::tanh (x * 25.0f);
        const float octave = std::tanh (std::abs (x) * 25.0f) * 2.0f - 1.0f;
        float y = wave == 0 ? 0.55f * square + 0.45f * octave        // Saw: bright
                : wave == 1 ? square                                  // Square: hollow, woody
                            : 0.3f * square + 0.7f * octave;          // Pulse: glassy
        if (shimmer > 0.0f)
            y = y * (1.0f - 0.4f * shimmer) + std::tanh (upBuffer[(size_t) i] * 25.0f) * shimmer * 0.7f;
        if (subAmount > 0.0f)
            y = y * (1.0f - 0.4f * subAmount) + std::tanh (subBuffer[(size_t) i] * 25.0f) * subAmount * 0.8f;

        // The pluck shapes level and filter; the guitar's envelope still ends the note.
        output[i] = y * pluck * std::min (1.0f, env * 3.0f);
        filterOpen[(size_t) i] = pluck;
    }
    playingFrequency.store (0.0f, std::memory_order_relaxed);
}

void GuitarSynthBlock::processBlock (float* left, float* right, int numSamples) noexcept
{
    const double sr = getSampleRate();
    envelope.setTimes (sr, getParam (AttackMs), getParam (ReleaseMs));
    mix.setTarget (getParam (Mix) * 0.01f);
    level.setTarget (dbToGain (getParam (LevelDb)));

    // Everything the synth does starts from this copy: the dry guitar is not touched.
    const float detection = getDetectionGain();
    for (int i = 0; i < numSamples; ++i)
        mono[(size_t) i] = 0.5f * (left[i] + right[i]) * detection;

    const auto voice = (Voice) std::clamp ((int) std::lround (getParam (Mode)), 0, 3);
    if (voice == Voice::Instrument)
    {
        InstrumentSynth::Settings settings;
        settings.sound = (int) std::lround (getParam (Sound));
        settings.polyNotes = std::lround (getParam (Notes)) == 0;
        settings.transpose = 12 * (std::clamp ((int) std::lround (getParam (Octave)), 0, 3) - 2);
        settings.tone = getParam (Tone) * 0.01f;
        settings.tail = getParam (Tail) * 0.01f;
        settings.width = getParam (Width) * 0.01f;
        settings.seq = (InstrumentSynth::Seq) std::clamp ((int) std::lround (getParam (Sequence)), 0, 8);
        settings.rate = (int) std::lround (getParam (SeqRate));
        settings.gate = getParam (SeqGate) * 0.01f;
        settings.range = (int) std::lround (getParam (SeqRange));
        settings.swing = getParam (Swing) * 0.01f;
        settings.bpm = getTempoBpm();
        instrument.process (mono.data(), synth.data(), upBuffer.data(), numSamples, settings);
        playingFrequency.store (0.0f, std::memory_order_relaxed);

        for (int i = 0; i < numSamples; ++i)
        {
            const float m = mix.next(), g = level.next();
            left[i]  = (left[i]  + (synth[(size_t) i]    - left[i])  * m) * g;
            right[i] = (right[i] + (upBuffer[(size_t) i] - right[i]) * m) * g;
        }
        return;
    }

    switch (voice)
    {
        case Voice::Mono:    renderMono (mono.data(), synth.data(), numSamples); break;
        case Voice::Poly:    renderPoly (mono.data(), synth.data(), numSamples); break;
        case Voice::Texture: renderTexture (mono.data(), synth.data(), numSamples); break;
        case Voice::Instrument: break;
    }

    // Envelope-controlled resonant low-pass (the "filter sweep" of the synth).
    const float cutoff = getParam (CutoffHz), resonance = getParam (Resonance);
    const float envAmount = getParam (EnvAmount) * 0.01f;
    const bool widen = voice == Voice::Texture;
    const float width = getParam (Width) * 0.01f;
    const auto srf = (float) sr;
    for (int i = 0; i < numSamples; ++i)
    {
        if ((i & 15) == 0)
            filter.set (sr, std::min (18000.0f, cutoff * std::exp2 (5.0f * envAmount * filterOpen[(size_t) i])), resonance);
        const float y = filter.process (synth[(size_t) i]).low * 0.5f;
        float yl = y, yr = y;
        if (widen)
        {
            // Two slow, opposite modulated delays (Haas + chorus): wide, but mono-compatible.
            const float lfo = Lfo::sine (widenLfo.advance());
            widenLeft.push (y);
            widenRight.push (y);
            const float dl = widenLeft.read ((0.011f + 0.0025f * lfo) * srf);
            const float dr = widenRight.read ((0.017f - 0.0025f * lfo) * srf);
            yl = y * (1.0f - 0.5f * width) + dl * 0.7f * width;
            yr = y * (1.0f - 0.5f * width) + dr * 0.7f * width;
        }
        const float m = mix.next(), g = level.next();
        left[i]  = (left[i]  + (yl - left[i])  * m) * g;
        right[i] = (right[i] + (yr - right[i]) * m) * g;
    }
}
} // namespace kumamp
