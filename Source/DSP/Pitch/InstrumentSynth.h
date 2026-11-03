#pragma once

#include "PolyNoteDetector.h"
#include "../../Core/Biquad.h"
#include "../../Core/DspPrimitives.h"
#include "../../Core/RealtimePitchTracker.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace kumamp
{
/**
    The guitar synth's "Instrumento" mode: KumAmp's own bank of synthesised
    instruments (piano, electric piano, organs, strings, pads, choir, brass,
    bells, mallets, pluck, lead, bass), played by the guitar.

    Notes come from the polyphonic detector (chords) or the monophonic pitch
    tracker (single notes, the tightest timing), quantised to semitones. An
    optional sequencer turns the notes held on the guitar into rhythm, synced
    to the host tempo and started by the pick: arpeggios (up / down / up-down
    / random), chord stabs and rhythmic patterns.

    Three voice engines, all synthesis (no samples, so the bank ships under
    the project licence and costs nothing to load):
      - additive: drawbar organ (tonewheel ratios, key click, percussion),
        pipe organ, piano (stretched inharmonic partials, two detuned strings
        per note, per-partial decay, hammer noise), mallets;
      - FM: electric piano (tine), bells;
      - analog: detuned saws + sub through a resonant low-pass with its own
        envelope (strings, pads, brass, pluck, lead, bass), formant filters
        for the choir.
    Per-patch effects: drive and rotary speaker for the organs, ensemble
    chorus for strings and pads, auto-pan for the electric piano.

    8 voices; everything is allocated in prepare(); process() is RT-safe.
*/
class InstrumentSynth
{
public:
    static constexpr int kNumVoices = 8;
    static constexpr int kNumSounds = 16;
    static constexpr const char* kSoundChoices =
        "Piano|E. Piano|Organo Jazz|Organo Rock|Organo Iglesia|Cuerdas|Cuerdas Solina|Pad Calido|"
        "Pad Brillante|Coro|Brass|Campanas|Mallets|Pluck|Lead|Bajo Synth";
    static constexpr const char* kSeqChoices =
        "Off|Arp Sube|Arp Baja|Arp Sube-Baja|Arp Azar|Acordes|Tresillo|Galope|Octavas";
    static constexpr const char* kRateChoices = "1/4|1/8|1/8T|1/16|1/16T|1/32";

    enum class Seq { Off, Up, Down, UpDown, Random, Chords, Tresillo, Gallop, Octaves };

    struct Settings
    {
        int sound = 0;
        bool polyNotes = true;   // chords (polyphonic detector) or single notes (tracker)
        int transpose = 0;       // semitones
        float tone = 0.0f;       // -1 .. 1: darker / brighter
        float tail = 0.0f;       // -1 .. 1: shorter / longer decays and releases
        float width = 0.6f;      // 0 .. 1 (0.6 = natural)
        Seq seq = Seq::Off;
        int rate = 3;            // index in kRateChoices
        float gate = 0.5f;       // note length, fraction of a step
        int range = 1;           // arpeggio octaves 1 .. 3
        float swing = 0.0f;      // 0 .. 1
        double bpm = 120.0;
    };

    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;

    /** Mono guitar in, stereo instrument out (replaces left / right). */
    void process (const float* mono, float* left, float* right, int numSamples, const Settings& settings) noexcept;

    static const char* soundName (int sound) noexcept;
    /** UI: notes currently held by the guitar, "C3 E3 G3" (empty = none). Any thread. */
    std::string getHeldNotesText() const;

    // Patch description (public for the bank table in the .cpp).
    enum class Engine : std::uint8_t { Additive, Piano, Fm, Analog };
    enum class VoiceMode : std::uint8_t { Poly, Lowest, Highest };
    struct Partial { float ratio, level, decay; };           // decay: seconds to -40 dB, 0 = sustain
    struct FmPair  { float carrier, modulator, index, indexFloor, indexDecay, level; };
    struct Patch
    {
        Engine engine = Engine::Analog;
        std::array<Partial, 10> partials {};
        int numPartials = 0;
        std::array<FmPair, 2> fm {};
        int numPairs = 0;
        int saws = 0;
        float detuneCents = 0.0f, subLevel = 0.0f, octaveLevel = 0.0f;
        bool formants = false;
        float cutoffHz = 20000.0f, resonance = 0.7071f, filterEnvOct = 0.0f;
        float filterAttack = 0.001f, filterDecay = 0.3f, filterSustain = 0.0f;
        float attack = 0.002f, decay = 0.0f, sustain = 1.0f, release = 0.2f;   // decay 0 = hold at sustain 1
        float velocitySens = 0.5f;
        float noiseLevel = 0.0f, noiseMs = 5.0f, noiseHz = 3000.0f;
        bool restrike = false;   // a new pick re-strikes held notes
        VoiceMode voiceMode = VoiceMode::Poly;
        int transpose = 0;
        float drive = 0.0f, ensemble = 0.0f, autopan = 0.0f;
        int rotary = 0;          // 0 off, 1 slow, 2 fast
        float gain = 1.0f;         // voice level (before the effects: sets the drive)
        float outputTrim = 1.0f;   // after the effects
    };

private:
    struct Adsr
    {
        enum class Stage : std::uint8_t { Idle, Attack, Decay, Release };
        Stage stage = Stage::Idle;
        float value = 0.0f, attackStep = 1.0f, decayCoefficient = 0.0f, sustain = 1.0f, releaseCoefficient = 0.0f;
        void set (float sampleRate, float attack, float decay, float sustainLevel, float release) noexcept;
        void start() noexcept { stage = Stage::Attack; }
        void stop() noexcept  { if (stage != Stage::Idle) stage = Stage::Release; }
        float next() noexcept;
    };

    static constexpr int kMaxOscillators = 20;
    struct Voice
    {
        int note = -1;            // -1 = free
        bool held = false;        // key down (released voices still ring)
        int gateSamples = -1;     // sequencer: samples until note-off (-1 = held by the guitar)
        float gain = 0.0f, frequency = 0.0f, panLeft = 0.7071f, panRight = 0.7071f;
        std::uint32_t age = 0;
        Adsr amp, filterEnv;
        // Additive: complex rotators (the decay is folded into the rotation).
        std::array<float, kMaxOscillators> re {}, im {}, rotRe {}, rotIm {}, level {};
        int numOscillators = 0;
        // FM / analog phases.
        std::array<double, 8> phase {};
        std::array<float, 8> increment {};
        std::array<float, 2> fmIndex {}, fmFloor {}, fmDecay {};
        float noise = 0.0f, noiseDecay = 0.0f;
        Biquad noiseFilter;
        std::array<Biquad, 3> formant;
        StateVariableFilter filter;
        float baseCutoff = 20000.0f;
    };

    void startVoice (int note, float velocity, int gateSamples, const Settings& s) noexcept;
    void releaseNote (int note) noexcept;
    void releaseAll (bool fast) noexcept;
    Voice* findVoice (int note) noexcept;
    float renderVoice (Voice& v, const Settings& s) noexcept;

    void updateHeld (const std::uint8_t* notes, int count, const Settings& s) noexcept;
    void applyHeld (const Settings& s) noexcept;
    void stepSequencer (const Settings& s) noexcept;
    float nextRandom() noexcept;

    double sampleRate = 48000.0;
    float sr = 48000.0f;
    std::array<Voice, kNumVoices> voices;
    std::uint32_t voiceCounter = 0, sampleCounter = 0;
    int currentSound = -1;

    // Note input.
    PolyNoteDetector detector;
    RealtimePitchTracker tracker;
    EnvelopeFollower fastEnvelope, slowEnvelope;
    float pickPeak = 0.0f, pickVelocity = 0.7f;
    int sinceOnset = 1 << 20, monoCandidate = -1, monoCount = 0, monoMisses = 0;
    std::array<std::uint8_t, PolyNoteDetector::kMaxNotes> held {};   // after transpose / voice mode
    int heldCount = 0;
    std::array<std::uint8_t, PolyNoteDetector::kMaxNotes> sounding {};
    int soundingCount = 0;
    bool restrikePending = false;
    int restrikeWait = 0;
    std::atomic<std::uint64_t> heldForUi { 0 };

    // Sequencer.
    double stepPosition = 0.0;   // samples into the current pair of steps
    int stepIndex = 0;
    bool seqRunning = false;
    std::uint32_t randomState = 0x9e3779b9u;

    // Effects.
    std::vector<float> busLeft, busRight;
    DelayLine hornLeft, hornRight;
    OnePole rotaryCrossover;
    double hornPhase = 0.0, drumPhase = 0.0;
    float hornHz = 0.8f, drumHz = 0.7f;
    std::array<DelayLine, 2> ensembleLines;
    double ensembleSlow = 0.0, ensembleFast = 0.0, panPhase = 0.0;
};
} // namespace kumamp
