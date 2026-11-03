#include "InstrumentSynth.h"
#include "SynthOscillators.h"

#include <algorithm>
#include <cmath>

namespace kumamp
{
namespace
{
    using Patch = InstrumentSynth::Patch;
    using Engine = InstrumentSynth::Engine;
    using VoiceMode = InstrumentSynth::VoiceMode;

    constexpr float kTwoPi = 6.28318530718f;
    constexpr float kGateDb = -55.0f;
    constexpr double kRateBeats[] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
    constexpr const char* kSoundNames[] = {
        "Piano", "E. Piano", "Organo Jazz", "Organo Rock", "Organo Iglesia", "Cuerdas", "Cuerdas Solina",
        "Pad Calido", "Pad Brillante", "Coro", "Brass", "Campanas", "Mallets", "Pluck", "Lead", "Bajo Synth"
    };
    static_assert (std::size (kSoundNames) == InstrumentSynth::kNumSounds);

    /** Tonewheel drawbar footages as harmonic ratios: 16' 5 1/3' 8' 4' 2 2/3' 2' 1 3/5' 1 1/3' 1'. */
    constexpr float kDrawbarRatio[] = { 0.5f, 1.5f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 8.0f };

    void setDrawbars (Patch& p, const char* registration)
    {
        p.engine = Engine::Additive;
        p.numPartials = 0;
        for (int i = 0; i < 9 && registration[i] != 0; ++i)
        {
            const int d = registration[i] - '0';
            if (d > 0)   // each drawbar step is 3 dB
                p.partials[(size_t) p.numPartials++] = { kDrawbarRatio[i], std::pow (10.0f, -3.0f * (float) (8 - d) / 20.0f), 0.0f };
        }
    }

    std::array<Patch, InstrumentSynth::kNumSounds> buildBank()
    {
        std::array<Patch, InstrumentSynth::kNumSounds> bank {};

        auto& piano = bank[0];   // partials and decays are generated per note
        piano.engine = Engine::Piano;
        piano.attack = 0.001f; piano.release = 0.35f; piano.velocitySens = 0.7f;
        piano.noiseLevel = 0.12f; piano.noiseMs = 4.0f; piano.noiseHz = 2400.0f;
        piano.restrike = true; piano.gain = 0.375f;

        auto& epiano = bank[1];  // DX-style tine: 1:1 body + 1:14 bark
        epiano.engine = Engine::Fm;
        epiano.fm[0] = { 1.0f, 1.0f, 1.6f, 0.25f, 0.9f, 1.0f };
        epiano.fm[1] = { 1.0f, 14.0f, 0.9f, 0.0f, 0.06f, 0.22f };
        epiano.numPairs = 2;
        epiano.attack = 0.001f; epiano.decay = 3.5f; epiano.sustain = 0.0f; epiano.release = 0.3f;
        epiano.velocitySens = 0.7f; epiano.restrike = true; epiano.autopan = 0.45f; epiano.gain = 0.86f;

        auto& jazz = bank[2];
        setDrawbars (jazz, "888000000");
        jazz.partials[(size_t) jazz.numPartials++] = { 3.0f, 0.55f, 0.35f };   // 3rd harmonic percussion
        jazz.attack = 0.004f; jazz.release = 0.05f; jazz.velocitySens = 0.1f;
        jazz.noiseLevel = 0.1f; jazz.noiseMs = 3.0f; jazz.noiseHz = 2500.0f;   // key click
        jazz.rotary = 1; jazz.gain = 0.073f;

        auto& rock = bank[3];
        setDrawbars (rock, "888800000");
        rock.attack = 0.003f; rock.release = 0.05f; rock.velocitySens = 0.1f;
        rock.noiseLevel = 0.16f; rock.noiseMs = 3.0f; rock.noiseHz = 2200.0f;
        rock.drive = 0.6f; rock.rotary = 2; rock.gain = 0.42f; rock.outputTrim = 0.37f;

        auto& church = bank[4];  // flue pipes 16' 8' 4' 2 2/3' 2' 1', with chiff and a celeste
        church.engine = Engine::Additive;
        church.partials = { { { 0.5f, 0.45f, 0 }, { 1.0f, 1.0f, 0 }, { 2.0f, 0.7f, 0 }, { 3.0f, 0.3f, 0 },
                              { 4.0f, 0.45f, 0 }, { 8.0f, 0.2f, 0 } } };
        church.numPartials = 6;
        church.attack = 0.07f; church.release = 0.45f; church.velocitySens = 0.0f;
        church.noiseLevel = 0.05f; church.noiseMs = 30.0f; church.noiseHz = 1800.0f;
        church.ensemble = 0.25f; church.gain = 0.093f;

        auto& strings = bank[5];
        strings.saws = 3; strings.detuneCents = 7.0f;
        strings.cutoffHz = 3500.0f;
        strings.attack = 0.35f; strings.release = 0.9f; strings.velocitySens = 0.2f;
        strings.ensemble = 0.6f; strings.gain = 0.447f;

        auto& solina = bank[6];
        solina.saws = 2; solina.detuneCents = 4.0f; solina.octaveLevel = 0.6f;
        solina.cutoffHz = 6000.0f;
        solina.attack = 0.12f; solina.release = 0.7f; solina.velocitySens = 0.1f;
        solina.ensemble = 1.0f; solina.gain = 0.278f;

        auto& warm = bank[7];
        warm.saws = 4; warm.detuneCents = 10.0f; warm.subLevel = 0.35f;
        warm.cutoffHz = 700.0f; warm.resonance = 1.3f; warm.filterEnvOct = 1.6f;
        warm.filterAttack = 1.2f; warm.filterDecay = 2.0f; warm.filterSustain = 0.5f;
        warm.attack = 0.8f; warm.release = 1.8f; warm.velocitySens = 0.1f;
        warm.ensemble = 0.4f; warm.gain = 0.42f;

        auto& bright = bank[8];
        bright.saws = 5; bright.detuneCents = 18.0f; bright.octaveLevel = 0.35f;
        bright.cutoffHz = 5000.0f; bright.resonance = 0.8f;
        bright.attack = 0.45f; bright.release = 1.5f; bright.velocitySens = 0.1f;
        bright.ensemble = 0.5f; bright.gain = 0.4f;

        auto& choir = bank[9];
        choir.saws = 3; choir.detuneCents = 6.0f; choir.formants = true;
        choir.cutoffHz = 5000.0f;
        choir.attack = 0.35f; choir.release = 0.8f; choir.velocitySens = 0.2f;
        choir.ensemble = 0.7f; choir.gain = 2.39f;

        auto& brass = bank[10];
        brass.saws = 2; brass.detuneCents = 5.0f;
        brass.cutoffHz = 350.0f; brass.resonance = 1.1f; brass.filterEnvOct = 3.2f;
        brass.filterAttack = 0.04f; brass.filterDecay = 0.45f; brass.filterSustain = 0.45f;
        brass.attack = 0.035f; brass.decay = 0.4f; brass.sustain = 0.85f; brass.release = 0.22f;
        brass.velocitySens = 0.5f; brass.gain = 0.507f;

        auto& bells = bank[11];
        bells.engine = Engine::Fm;
        bells.fm[0] = { 1.0f, 3.5f, 2.4f, 0.4f, 1.2f, 1.0f };
        bells.fm[1] = { 2.76f, 1.0f, 0.8f, 0.0f, 0.5f, 0.35f };
        bells.numPairs = 2;
        bells.attack = 0.001f; bells.decay = 5.0f; bells.sustain = 0.0f; bells.release = 2.0f;
        bells.velocitySens = 0.6f; bells.restrike = true; bells.gain = 0.574f;

        auto& mallets = bank[12];   // marimba bar modes
        mallets.engine = Engine::Additive;
        mallets.partials = { { { 1.0f, 1.0f, 0.9f }, { 3.93f, 0.3f, 0.18f }, { 9.2f, 0.1f, 0.06f } } };
        mallets.numPartials = 3;
        mallets.attack = 0.001f; mallets.release = 0.25f; mallets.velocitySens = 0.7f;
        mallets.noiseLevel = 0.15f; mallets.noiseMs = 3.0f; mallets.noiseHz = 3000.0f;
        mallets.restrike = true; mallets.gain = 0.53f;

        auto& pluck = bank[13];
        pluck.saws = 2; pluck.detuneCents = 6.0f; pluck.subLevel = 0.25f;
        pluck.cutoffHz = 250.0f; pluck.resonance = 2.2f; pluck.filterEnvOct = 4.5f;
        pluck.filterAttack = 0.001f; pluck.filterDecay = 0.28f; pluck.filterSustain = 0.0f;
        pluck.attack = 0.001f; pluck.decay = 0.6f; pluck.sustain = 0.0f; pluck.release = 0.35f;
        pluck.velocitySens = 0.6f; pluck.restrike = true; pluck.ensemble = 0.3f; pluck.gain = 1.14f;

        auto& lead = bank[14];
        lead.saws = 2; lead.detuneCents = 4.0f; lead.subLevel = 0.45f;
        lead.cutoffHz = 1800.0f; lead.resonance = 3.0f; lead.filterEnvOct = 1.2f;
        lead.filterAttack = 0.005f; lead.filterDecay = 0.35f; lead.filterSustain = 0.3f;
        lead.attack = 0.006f; lead.release = 0.18f; lead.velocitySens = 0.3f;
        lead.voiceMode = VoiceMode::Highest; lead.gain = 0.52f;

        auto& bass = bank[15];
        bass.saws = 1; bass.subLevel = 0.6f;
        bass.cutoffHz = 400.0f; bass.resonance = 2.5f; bass.filterEnvOct = 2.5f;
        bass.filterAttack = 0.001f; bass.filterDecay = 0.22f; bass.filterSustain = 0.1f;
        bass.attack = 0.002f; bass.decay = 0.5f; bass.sustain = 0.6f; bass.release = 0.1f;
        bass.velocitySens = 0.4f; bass.voiceMode = VoiceMode::Lowest; bass.transpose = -12;
        bass.restrike = true; bass.gain = 0.7f;

        return bank;
    }

    const std::array<Patch, InstrumentSynth::kNumSounds>& bank()
    {
        static const auto patches = buildBank();
        return patches;
    }

    /** Per-sample multiplier that reaches -40 dB after `seconds`. */
    float decayPerSample (float sampleRate, float seconds) noexcept
    {
        return seconds <= 0.0f ? 1.0f : std::exp (std::log (0.01f) / (seconds * sampleRate));
    }

    float midiToHz (int note) noexcept { return 440.0f * std::exp2 ((float) (note - 69) / 12.0f); }
} // namespace

//==============================================================================
void InstrumentSynth::Adsr::set (float rate, float attack, float decay, float sustainLevel, float release) noexcept
{
    attackStep = 1.0f / std::max (1.0f, attack * rate);
    sustain = decay <= 0.0f ? 1.0f : sustainLevel;
    decayCoefficient = decayPerSample (rate, decay);
    releaseCoefficient = decayPerSample (rate, std::max (0.005f, release));
}

float InstrumentSynth::Adsr::next() noexcept
{
    switch (stage)
    {
        case Stage::Attack:
            value += attackStep;
            if (value >= 1.0f) { value = 1.0f; stage = Stage::Decay; }
            break;
        case Stage::Decay:
            value = sustain + (value - sustain) * decayCoefficient;
            break;
        case Stage::Release:
            value *= releaseCoefficient;
            if (value < 1.0e-4f) { value = 0.0f; stage = Stage::Idle; }
            break;
        case Stage::Idle:
            break;
    }
    return value;
}

//==============================================================================
const char* InstrumentSynth::soundName (int sound) noexcept
{
    return kSoundNames[std::clamp (sound, 0, kNumSounds - 1)];
}

std::string InstrumentSynth::getHeldNotesText() const
{
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const auto packed = heldForUi.load (std::memory_order_relaxed);
    std::string text;
    for (int i = 0; i < (int) (packed & 0xff); ++i)
    {
        const int note = (int) ((packed >> (8 * (i + 1))) & 0xff);
        if (! text.empty())
            text += ' ';
        text += names[note % 12] + std::to_string (note / 12 - 1);
    }
    return text;
}

void InstrumentSynth::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;
    sr = (float) newSampleRate;
    detector.prepare (newSampleRate);
    tracker.prepare (newSampleRate);
    fastEnvelope.setTimes (newSampleRate, 0.3f, 25.0f);
    slowEnvelope.setTimes (newSampleRate, 80.0f, 300.0f);
    busLeft.assign ((size_t) maxBlockSize, 0.0f);
    busRight.assign ((size_t) maxBlockSize, 0.0f);
    hornLeft.prepare ((int) (0.005 * newSampleRate));
    hornRight.prepare ((int) (0.005 * newSampleRate));
    rotaryCrossover.setCutoff (newSampleRate, 800.0f);
    for (auto& line : ensembleLines)
        line.prepare ((int) (0.02 * newSampleRate));
    reset();
}

void InstrumentSynth::reset() noexcept
{
    for (auto& v : voices)
    {
        v = Voice {};
    }
    detector.reset();
    tracker.reset();
    fastEnvelope.reset();
    slowEnvelope.reset();
    pickPeak = 0.0f;
    sinceOnset = 1 << 20;
    monoCandidate = -1;
    monoCount = monoMisses = 0;
    heldCount = soundingCount = 0;
    restrikePending = false;
    restrikeWait = 0;
    seqRunning = false;
    stepPosition = 0.0;
    stepIndex = 0;
    hornLeft.reset();
    hornRight.reset();
    rotaryCrossover.reset();
    for (auto& line : ensembleLines)
        line.reset();
    heldForUi.store (0, std::memory_order_relaxed);
}

float InstrumentSynth::nextRandom() noexcept
{
    randomState = randomState * 1664525u + 1013904223u;
    return (float) (randomState >> 8) * (1.0f / 16777216.0f);
}

//==============================================================================
InstrumentSynth::Voice* InstrumentSynth::findVoice (int note) noexcept
{
    for (auto& v : voices)
        if (v.note == note && v.held)
            return &v;
    return nullptr;
}

void InstrumentSynth::releaseNote (int note) noexcept
{
    for (auto& v : voices)
        if (v.note == note && v.held && v.gateSamples < 0)
        {
            v.held = false;
            v.amp.stop();
            v.filterEnv.stop();
        }
}

void InstrumentSynth::releaseAll (bool fast) noexcept
{
    for (auto& v : voices)
    {
        if (v.note < 0)
            continue;
        v.held = false;
        if (fast)
            v.amp.releaseCoefficient = decayPerSample (sr, 0.01f);
        v.amp.stop();
        v.filterEnv.stop();
    }
}

void InstrumentSynth::startVoice (int note, float velocity, int gateSamples, const Settings& s) noexcept
{
    // Free voice, else the quietest released one, else the oldest.
    Voice* voice = nullptr;
    for (auto& v : voices)
        if (v.note < 0) { voice = &v; break; }
    if (voice == nullptr)
        for (auto& v : voices)
            if (! v.held && (voice == nullptr || v.amp.value < voice->amp.value))
                voice = &v;
    if (voice == nullptr)
        for (auto& v : voices)
            if (voice == nullptr || v.age < voice->age)
                voice = &v;

    const auto& p = bank()[(size_t) std::clamp (currentSound, 0, kNumSounds - 1)];
    auto& v = *voice;
    const float tailScale = std::exp2 (1.5f * s.tail), releaseScale = std::exp2 (2.0f * s.tail);

    v.note = note;
    v.held = true;
    v.gateSamples = gateSamples;
    v.age = ++voiceCounter;
    v.frequency = midiToHz (note);
    v.gain = p.gain * (1.0f - p.velocitySens * (1.0f - velocity));
    // Spread the notes a little across the stereo field (equal power).
    const float pan = 0.3f * (float) (((note * 7) % 12) - 5.5f) / 5.5f;
    v.panLeft = std::cos ((pan + 1.0f) * 0.25f * 3.14159265f);
    v.panRight = std::sin ((pan + 1.0f) * 0.25f * 3.14159265f);

    v.amp.set (sr, p.attack, p.decay * tailScale, p.sustain, p.release * releaseScale);
    v.amp.value = 0.0f;
    v.amp.start();
    v.filterEnv.set (sr, p.filterAttack, p.filterDecay * tailScale, p.filterSustain, p.release * releaseScale);
    v.filterEnv.value = 0.0f;
    v.filterEnv.start();

    v.noise = p.noiseLevel * (0.5f + 0.5f * velocity);
    v.noiseDecay = std::exp (-1.0f / (0.001f * p.noiseMs * sr));
    v.noiseFilter.setBandPass (sampleRate, p.noiseHz, 1.2f);
    v.noiseFilter.reset();

    auto addOscillator = [&] (float hz, float level, float decaySeconds)
    {
        if (hz >= 0.45f * sr || v.numOscillators >= kMaxOscillators || level <= 0.0f)
            return;
        const auto i = (size_t) v.numOscillators++;
        const float w = kTwoPi * hz / sr, d = decayPerSample (sr, decaySeconds);
        v.rotRe[i] = d * std::cos (w);
        v.rotIm[i] = d * std::sin (w);
        v.re[i] = level;    // the output is the imaginary part: starts at 0, no click
        v.im[i] = 0.0f;
        v.level[i] = decaySeconds <= 0.0f ? level : 0.0f;   // sustained: renormalised
    };
    v.numOscillators = 0;

    switch (p.engine)
    {
        case Engine::Additive:
            for (int j = 0; j < p.numPartials; ++j)
            {
                const auto& partial = p.partials[(size_t) j];
                const float tilt = std::pow (partial.ratio, 0.7f * s.tone);
                addOscillator (v.frequency * partial.ratio, partial.level * tilt,
                               partial.decay > 0.0f ? partial.decay * tailScale : 0.0f);
            }
            break;

        case Engine::Piano:
        {
            // Stiff strings: stretched partials, softer and shorter going up; louder
            // playing brightens the spectrum. Two strings a cent apart beat slowly.
            const float b = 0.00025f * std::exp2 ((float) (note - 48) / 24.0f);
            const float sustainSeconds = std::clamp (5.0f * std::exp2 (-(float) (note - 40) / 20.0f), 0.6f, 6.0f) * tailScale;
            const float exponent = std::clamp (1.6f - 0.9f * velocity - 0.6f * s.tone, 0.5f, 2.5f);
            for (int string = 0; string < 2; ++string)
            {
                const float detune = string == 0 ? 1.0f : 1.00052f;
                for (int n = 1; n <= 10; ++n)
                {
                    const float ratio = (float) n * std::sqrt (1.0f + b * (float) (n * n));
                    addOscillator (v.frequency * ratio * detune, 0.5f / std::pow ((float) n, exponent),
                                   sustainSeconds / (1.0f + 0.45f * (float) (n - 1)));
                }
            }
            break;
        }

        case Engine::Fm:
            for (int k = 0; k < p.numPairs; ++k)
            {
                const auto& pair = p.fm[(size_t) k];
                v.phase[(size_t) (2 * k)] = v.phase[(size_t) (2 * k + 1)] = 0.0;
                v.increment[(size_t) (2 * k)] = v.frequency * pair.carrier / sr;
                v.increment[(size_t) (2 * k + 1)] = v.frequency * pair.modulator / sr;
                const float index = pair.index * (0.4f + 0.6f * velocity) * std::exp2 (s.tone);
                v.fmIndex[(size_t) k] = index;
                v.fmFloor[(size_t) k] = index * pair.indexFloor;
                v.fmDecay[(size_t) k] = decayPerSample (sr, pair.indexDecay * tailScale);
            }
            break;

        case Engine::Analog:
            for (int k = 0; k < p.saws; ++k)
            {
                const float spread = p.saws > 1 ? 2.0f * (float) k / (float) (p.saws - 1) - 1.0f : 0.0f;
                v.increment[(size_t) k] = v.frequency * std::exp2 (spread * p.detuneCents / 1200.0f) / sr;
                v.phase[(size_t) k] = nextRandom();   // free-running feel: no phasey unison
            }
            v.increment[6] = v.frequency * 0.5f / sr;   // sub (square, -1 oct)
            v.increment[7] = v.frequency * 2.0f / sr;   // octave (saw, +1 oct)
            v.phase[6] = v.phase[7] = 0.0;
            v.baseCutoff = p.cutoffHz;
            v.filter.reset();
            if (p.formants)
            {
                // "Aah": F1 700, F2 1150, F3 2600 Hz.
                v.formant[0].setBandPass (sampleRate, 700.0f, 6.0f);
                v.formant[1].setBandPass (sampleRate, 1150.0f, 7.0f);
                v.formant[2].setBandPass (sampleRate, 2600.0f, 9.0f);
                for (auto& f : v.formant) f.reset();
            }
            break;
    }
}

float InstrumentSynth::renderVoice (Voice& v, const Settings& s) noexcept
{
    const float env = v.amp.next();
    if (v.amp.stage == Adsr::Stage::Idle)
    {
        v.note = -1;
        v.held = false;
        return 0.0f;
    }
    if (v.gateSamples > 0 && --v.gateSamples == 0)
    {
        v.held = false;
        v.amp.stop();
        v.filterEnv.stop();
    }

    const auto& p = bank()[(size_t) std::clamp (currentSound, 0, kNumSounds - 1)];
    float out = 0.0f;

    switch (p.engine)
    {
        case Engine::Additive:
        case Engine::Piano:
        {
            const bool renormalise = (sampleCounter & 255) == 0;
            for (int i = 0; i < v.numOscillators; ++i)
            {
                const auto k = (size_t) i;
                const float re = v.re[k] * v.rotRe[k] - v.im[k] * v.rotIm[k];
                const float im = v.re[k] * v.rotIm[k] + v.im[k] * v.rotRe[k];
                v.re[k] = re;
                v.im[k] = im;
                out += im;
                if (renormalise && v.level[k] > 0.0f)
                {
                    const float magnitude = std::sqrt (re * re + im * im);
                    if (magnitude > 0.0f)
                    {
                        v.re[k] *= v.level[k] / magnitude;
                        v.im[k] *= v.level[k] / magnitude;
                    }
                }
            }
            break;
        }

        case Engine::Fm:
            for (int k = 0; k < p.numPairs; ++k)
            {
                auto& carrier = v.phase[(size_t) (2 * k)];
                auto& modulator = v.phase[(size_t) (2 * k + 1)];
                const float m = std::sin (kTwoPi * (float) modulator);
                out += p.fm[(size_t) k].level * std::sin (kTwoPi * (float) carrier + v.fmIndex[(size_t) k] * m);
                v.fmIndex[(size_t) k] = v.fmFloor[(size_t) k] + (v.fmIndex[(size_t) k] - v.fmFloor[(size_t) k]) * v.fmDecay[(size_t) k];
                carrier += v.increment[(size_t) (2 * k)];       if (carrier >= 1.0) carrier -= 1.0;
                modulator += v.increment[(size_t) (2 * k + 1)]; if (modulator >= 1.0) modulator -= 1.0;
            }
            out *= 0.5f;
            break;

        case Engine::Analog:
        {
            for (int k = 0; k < p.saws; ++k)
            {
                auto& phase = v.phase[(size_t) k];
                out += synth::oscillator (0, (float) phase, v.increment[(size_t) k]);
                phase += v.increment[(size_t) k]; if (phase >= 1.0) phase -= 1.0;
            }
            out /= std::sqrt ((float) std::max (1, p.saws));
            if (p.subLevel > 0.0f)
            {
                out += p.subLevel * synth::oscillator (1, (float) v.phase[6], v.increment[6]);
                v.phase[6] += v.increment[6]; if (v.phase[6] >= 1.0) v.phase[6] -= 1.0;
            }
            if (p.octaveLevel > 0.0f)
            {
                out += p.octaveLevel * synth::oscillator (0, (float) v.phase[7], v.increment[7]);
                v.phase[7] += v.increment[7]; if (v.phase[7] >= 1.0) v.phase[7] -= 1.0;
            }
            if (p.formants)
                out = v.formant[0].process (out) + 0.55f * v.formant[1].process (out) + 0.3f * v.formant[2].process (out);

            const float fenv = v.filterEnv.next();
            if ((sampleCounter & 15) == 0)
                v.filter.set (sampleRate, std::min (18000.0f, v.baseCutoff * std::exp2 (2.0f * s.tone + p.filterEnvOct * fenv)), p.resonance);
            out = v.filter.process (out).low * 0.5f;
            break;
        }
    }

    if (v.noise > 1.0e-5f)
    {
        out += v.noiseFilter.process ((nextRandom() * 2.0f - 1.0f) * v.noise);
        v.noise *= v.noiseDecay;
    }
    return out * env * v.gain;
}

//==============================================================================
void InstrumentSynth::updateHeld (const std::uint8_t* notes, int count, const Settings& s) noexcept
{
    const auto& p = bank()[(size_t) std::clamp (currentSound, 0, kNumSounds - 1)];
    const int shift = s.transpose + p.transpose;
    std::array<std::uint8_t, PolyNoteDetector::kMaxNotes> next {};
    int nextCount = 0;
    for (int i = 0; i < count; ++i)
        next[(size_t) nextCount++] = (std::uint8_t) std::clamp ((int) notes[i] + shift, 21, 108);
    if (nextCount > 1 && p.voiceMode != VoiceMode::Poly)
    {
        next[0] = p.voiceMode == VoiceMode::Lowest ? next[0] : next[(size_t) nextCount - 1];
        nextCount = 1;
    }

    const bool wasEmpty = heldCount == 0;
    held = next;
    heldCount = nextCount;

    std::uint64_t packed = (std::uint64_t) heldCount;
    for (int i = 0; i < heldCount; ++i)
        packed |= (std::uint64_t) held[(size_t) i] << (8 * (i + 1));
    heldForUi.store (packed, std::memory_order_relaxed);

    if (s.seq != Seq::Off)
    {
        // The pattern starts with the first notes after silence (the pick sets the downbeat).
        if (heldCount == 0)
            seqRunning = false;
        else if (wasEmpty || ! seqRunning)
        {
            seqRunning = true;
            stepIndex = 0;
            stepPosition = 0.0;
            stepSequencer (s);
        }
    }
}

void InstrumentSynth::applyHeld (const Settings& s) noexcept
{
    const float velocity = pickVelocity;

    if (restrikePending)
        return;   // a pick is settling: the strike happens once its notes are known

    for (int i = 0; i < heldCount; ++i)
        if (findVoice (held[(size_t) i]) == nullptr)
            startVoice (held[(size_t) i], velocity, -1, s);
    for (int i = 0; i < soundingCount; ++i)
    {
        const int note = sounding[(size_t) i];
        if (std::find (held.begin(), held.begin() + heldCount, (std::uint8_t) note) == held.begin() + heldCount)
            releaseNote (note);
    }
    sounding = held;
    soundingCount = heldCount;
}

void InstrumentSynth::stepSequencer (const Settings& s) noexcept
{
    if (heldCount == 0)
        return;

    const double stepSamples = 60.0 / s.bpm * kRateBeats[(size_t) std::clamp (s.rate, 0, 5)] * sampleRate;
    const bool odd = (stepIndex & 1) != 0;
    const double length = stepSamples * (odd ? 1.0 - 0.5 * s.swing : 1.0 + 0.5 * s.swing);
    const int gate = std::max (1, (int) (length * std::clamp (s.gate, 0.05f, 1.0f)));
    const float velocity = pickVelocity;

    // Arpeggio pool: the held notes over 1..3 octaves.
    std::array<int, PolyNoteDetector::kMaxNotes * 3> pool {};
    int poolSize = 0;
    for (int octave = 0; octave < std::clamp (s.range, 1, 3); ++octave)
        for (int i = 0; i < heldCount; ++i)
            pool[(size_t) poolSize++] = std::min (108, held[(size_t) i] + 12 * octave);

    auto chord = [&] (float accent)
    {
        for (int i = 0; i < heldCount; ++i)
            startVoice (held[(size_t) i], velocity * accent, gate, s);
    };

    const int k = stepIndex;
    switch (s.seq)
    {
        case Seq::Up:     startVoice (pool[(size_t) (k % poolSize)], velocity, gate, s); break;
        case Seq::Down:   startVoice (pool[(size_t) (poolSize - 1 - k % poolSize)], velocity, gate, s); break;
        case Seq::UpDown:
        {
            const int period = std::max (1, 2 * poolSize - 2);
            const int i = k % period;
            startVoice (pool[(size_t) (i < poolSize ? i : period - i)], velocity, gate, s);
            break;
        }
        case Seq::Random: startVoice (pool[(size_t) std::min (poolSize - 1, (int) (nextRandom() * (float) poolSize))], velocity, gate, s); break;
        case Seq::Chords: chord (k % 4 == 0 ? 1.0f : 0.8f); break;
        case Seq::Tresillo:
        {
            const int i = k % 16;   // 3 + 3 + 2, twice
            if (i == 0 || i == 3 || i == 6 || i == 8 || i == 11 || i == 14)
                chord (i == 0 || i == 8 ? 1.0f : 0.8f);
            break;
        }
        case Seq::Gallop:
            if (k % 4 != 1)
                chord (k % 4 == 0 ? 1.0f : 0.75f);
            break;
        case Seq::Octaves:
            startVoice (std::min (108, held[0] + 12 * (k % (std::clamp (s.range, 1, 3) + 1))), velocity * (k % 2 == 0 ? 1.0f : 0.8f), gate, s);
            break;
        case Seq::Off:
            break;
    }
}

//==============================================================================
void InstrumentSynth::process (const float* mono, float* left, float* right, int numSamples, const Settings& s) noexcept
{
    if (s.sound != currentSound)
    {
        releaseAll (true);
        currentSound = std::clamp (s.sound, 0, kNumSounds - 1);
        soundingCount = 0;
        heldCount = 0;
        restrikePending = false;
    }
    if (s.seq == Seq::Off && seqRunning)
    {
        seqRunning = false;
        soundingCount = 0;   // held notes start again as plain notes
    }
    if (s.seq != Seq::Off && soundingCount > 0)
    {
        for (int i = 0; i < soundingCount; ++i)   // the sequencer takes over
            releaseNote (sounding[(size_t) i]);
        soundingCount = 0;
    }

    const auto& p = bank()[(size_t) currentSound];
    const float gate = dbToGain (kGateDb);
    const int refractory = (int) (0.06f * sr);
    const float pickPeakDecay = EnvelopeFollower::coefficient (sampleRate, 300.0f);
    const double stepSamples = 60.0 / s.bpm * kRateBeats[(size_t) std::clamp (s.rate, 0, 5)] * sampleRate;
    const double oddStart = stepSamples * (1.0 + 0.5 * s.swing);

    auto onNotes = [&] (const std::uint8_t* notes, int count, bool ready)
    {
        updateHeld (notes, count, s);
        // Once the pick has settled (or after ~120 ms), strike its notes: the same chord
        // again, or a new one.
        if (restrikePending && (ready || ++restrikeWait > 30))
        {
            restrikePending = false;
            for (int i = 0; i < soundingCount; ++i)
                releaseNote (sounding[(size_t) i]);
            soundingCount = 0;
        }
        if (s.seq == Seq::Off)
            applyHeld (s);
    };

    for (int i = 0; i < numSamples; ++i)
    {
        const float x = mono[i];

        // Pick detection (as in Textura): sets the velocity and re-strikes percussive sounds.
        const float fast = fastEnvelope.process (x), slow = slowEnvelope.process (fast);
        pickPeak = sinceOnset < refractory ? std::max (pickPeak, fast) : pickPeak * pickPeakDecay;
        if (++sinceOnset > refractory && fast > gate && fast > 1.5f * std::max (slow, pickPeak) + 1.0e-4f)
        {
            sinceOnset = 0;
            pickPeak = fast;
            pickVelocity = std::clamp ((20.0f * std::log10 (fast) + 42.0f) / 36.0f, 0.25f, 1.0f);
            if (p.restrike && s.seq == Seq::Off)
            {
                restrikePending = true;
                restrikeWait = 0;
                monoCount = 0;
            }
        }

        if (s.polyNotes)
        {
            detector.process (&x, 1, [&] (int, const PolyNoteDetector::NoteSet& set)
            {
                onNotes (set.notes.data(), set.count, ! detector.isSettling() && restrikeWait >= 2);
            });
        }
        else
        {
            tracker.process (&x, 1, [&] (int, const RealtimePitchTracker::Estimate& e)
            {
                if (e.valid && e.fastDb > kGateDb)
                {
                    const int note = (int) std::lround (69.0f + 12.0f * std::log2 (e.frequency / 440.0f));
                    monoCount = note == monoCandidate ? monoCount + 1 : 1;
                    monoCandidate = note;
                    monoMisses = 0;
                }
                else if (++monoMisses > 10 || e.fastDb < kGateDb)
                {
                    monoCandidate = -1;
                    monoCount = 0;
                }
                const bool confirmed = monoCandidate >= 0 && monoCount >= 2;
                if (confirmed || monoCandidate < 0)
                {
                    const auto note = (std::uint8_t) std::max (0, monoCandidate);
                    onNotes (&note, confirmed ? 1 : 0, confirmed);
                }
            });
        }

        // Sequencer clock: pairs of steps, the second one swung.
        if (s.seq != Seq::Off && seqRunning)
        {
            const bool wasFirst = stepPosition < oddStart;
            stepPosition += 1.0;
            if (wasFirst && stepPosition >= oddStart)
            {
                ++stepIndex;
                stepSequencer (s);
            }
            else if (stepPosition >= 2.0 * stepSamples)
            {
                stepPosition -= 2.0 * stepSamples;
                ++stepIndex;
                stepSequencer (s);
            }
        }

        float l = 0.0f, r = 0.0f;
        for (auto& v : voices)
            if (v.note >= 0)
            {
                const float y = renderVoice (v, s);
                l += y * v.panLeft;
                r += y * v.panRight;
            }
        busLeft[(size_t) i] = l;
        busRight[(size_t) i] = r;
        ++sampleCounter;
    }

    // Patch effects.
    if (p.drive > 0.0f)
    {
        const float drive = 1.0f + 6.0f * p.drive, makeUp = (1.0f + p.drive) / drive;
        for (int i = 0; i < numSamples; ++i)
        {
            busLeft[(size_t) i] = std::tanh (busLeft[(size_t) i] * drive) * makeUp;
            busRight[(size_t) i] = std::tanh (busRight[(size_t) i] * drive) * makeUp;
        }
    }

    if (p.rotary > 0)
    {
        // Rotary speaker: horn (highs, Doppler + level) and drum (lows, level), with the
        // inertia of the real rotors when the speed changes.
        const float hornTarget = p.rotary == 2 ? 6.7f : 0.8f, drumTarget = p.rotary == 2 ? 5.8f : 0.67f;
        const float blockSeconds = (float) numSamples / sr;
        hornHz += (hornTarget - hornHz) * std::min (1.0f, blockSeconds / 0.6f);
        drumHz += (drumTarget - drumHz) * std::min (1.0f, blockSeconds / 2.5f);
        const float depth = 0.00035f * sr, base = 0.0012f * sr;
        for (int i = 0; i < numSamples; ++i)
        {
            const float m = 0.5f * (busLeft[(size_t) i] + busRight[(size_t) i]);
            const float low = rotaryCrossover.lowPass (m), high = m - low;
            const float hs = std::sin (kTwoPi * (float) hornPhase), hc = std::cos (kTwoPi * (float) hornPhase);
            const float ds = std::sin (kTwoPi * (float) drumPhase);
            hornLeft.push (high);
            hornRight.push (high);
            const float hl = hornLeft.read (base + depth * hs) * (0.75f + 0.25f * hc);
            const float hr = hornRight.read (base - depth * hs) * (0.75f - 0.25f * hc);
            busLeft[(size_t) i] = hl + low * (0.85f + 0.15f * ds);
            busRight[(size_t) i] = hr + low * (0.85f - 0.15f * ds);
            hornPhase += hornHz / sr; if (hornPhase >= 1.0) hornPhase -= 1.0;
            drumPhase += drumHz / sr; if (drumPhase >= 1.0) drumPhase -= 1.0;
        }
    }

    if (p.ensemble > 0.0f)
    {
        // String ensemble: three taps per side, slow + fast modulation 120 degrees apart.
        const float e = p.ensemble;
        for (int i = 0; i < numSamples; ++i)
        {
            float* sides[] { &busLeft[(size_t) i], &busRight[(size_t) i] };
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& line = ensembleLines[(size_t) ch];
                line.push (*sides[ch]);
                float wet = 0.0f;
                for (int tap = 0; tap < 3; ++tap)
                {
                    const float offset = (float) tap / 3.0f + (float) ch / 6.0f;
                    const float ms = 7.0f + 1.6f * std::sin (kTwoPi * ((float) ensembleSlow + offset))
                                          + 0.3f * std::sin (kTwoPi * ((float) ensembleFast + offset));
                    wet += line.read (ms * 0.001f * sr);
                }
                *sides[ch] = *sides[ch] * (1.0f - 0.5f * e) + wet * (e / 3.0f) * 1.2f;
            }
            ensembleSlow += 0.63 / sampleRate; if (ensembleSlow >= 1.0) ensembleSlow -= 1.0;
            ensembleFast += 5.7 / sampleRate;  if (ensembleFast >= 1.0) ensembleFast -= 1.0;
        }
    }

    if (p.autopan > 0.0f)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float lfo = std::sin (kTwoPi * (float) panPhase);
            busLeft[(size_t) i] *= 1.0f - p.autopan * 0.5f * (1.0f + lfo);
            busRight[(size_t) i] *= 1.0f - p.autopan * 0.5f * (1.0f - lfo);
            panPhase += 4.5 / sampleRate; if (panPhase >= 1.0) panPhase -= 1.0;
        }
    }

    // Width (M/S): 0.6 = as rendered.
    const float side = std::clamp (s.width, 0.0f, 1.0f) / 0.6f;
    const float trim = p.outputTrim;
    for (int i = 0; i < numSamples; ++i)
    {
        const float mid = 0.5f * trim * (busLeft[(size_t) i] + busRight[(size_t) i]);
        const float sideValue = 0.5f * trim * (busLeft[(size_t) i] - busRight[(size_t) i]) * side;
        left[i] = mid + sideValue;
        right[i] = mid - sideValue;
    }
}
} // namespace kumamp
