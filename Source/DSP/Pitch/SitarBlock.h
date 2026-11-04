#pragma once

#include "TanpuraDrone.h"
#include "WsolaPitchShifter.h"
#include "../../Core/Biquad.h"
#include "../../Core/DspPrimitives.h"
#include "../../Core/RealtimePitchTracker.h"
#include "../../Core/Smoothing.h"
#include "../../Graph/DSPBlock.h"

#include <array>
#include <cstdint>
#include <vector>

namespace kumamp
{
/**
    Sitar: turns the guitar into a sitar-like lead with a ring of sympathetic
    strings. Everything works on the real guitar signal (no note detection),
    so it adds no latency and plays chords.

    Modo Modelado (default): the lead is a sitar string of its own. The
    pitch tracker reads the note and every pick plucks a steel waveguide
    string with the jawari INSIDE the loop: the bridge contact shortens the
    string while it swings over it, so the buzz builds up and then sinks
    through the harmonics as the note decays, exactly like the real bridge.
    The string follows the guitar continuously, so slides and bends become
    meend; it rings while the guitar note does. Through a sitar body (gourd,
    nasal mids, steel top). Single notes only; ~10-15 ms to read the pitch.

    Modo Procesado - lead (jawari): a sitar string grazes its wide, curved bridge, so its
    effective length changes with the displacement. That is modelled as a
    one-sided, amplitude-dependent delay (the string "wraps" over the bridge
    only on one half of the swing) plus a one-sided contact clip, both on the
    signal normalised by its own attack peak: the buzz is strongest on the
    pick and fades as the note decays, like the real thing. A resonant
    band-pass that tracks the note (pitch tracker) sweeps from a high harmonic
    down to a low one after every pick: the characteristic "yowww".
    Bend raises the lead's pitch (WSOLA) up to an octave (the meend), e.g.
    from an expression pedal or automation.

    Sympathetic strings (taraf): 12 waveguide strings tuned to a key and a
    raga-like scale, excited by the guitar. They only ring when what you play
    shares their pitches, as on a sitar. Congelar holds the drone (the input
    stops exciting them and the loss goes to almost nothing).

    Tampura: an optional sustained drone behind it all, to set the mood: four
    strings (the first on Pa, Ma or Ni, two on Sa and the low Sa) ringing
    continuously, with the tampura's bridge shimmer moving slowly through
    their harmonics (see TanpuraDrone). Its tone follows the sitar's key or is
    picked on its own. By default it goes straight to the output (Directo),
    around every block after this one, so the amp and effects of the chain
    do not touch it; "Con la cadena" sends it down the chain with the guitar.

    Levels: dry guitar, lead and sympathetic strings are mixed separately.
    All memory is allocated in prepare(); processBlock() is RT-safe.
*/
class SitarBlock final : public DSPBlock
{
public:
    enum Param { Mode, Dry, Lead, Buzz, LeadTone, Sympathetic, SympTone, Decay, Key, Scale, Bend, Freeze,
                 Tanpura, TanpuraLevel, TanpuraKey, TanpuraString, TanpuraMovement, TanpuraTexture, TanpuraOutput,
                 TanpuraDetune, TanpuraBreath, TanpuraDrift, NumParams };
    static constexpr int kNumStrings = 12;
    static constexpr const char* kKeyChoices = "C|C#|D|D#|E|F|F#|G|G#|A|A#|B";
    static constexpr const char* kTanpuraKeyChoices = "= Tonalidad|C|C#|D|D#|E|F|F#|G|G#|A|A#|B";
    static constexpr const char* kScaleChoices =
        "Bilawal (mayor)|Khamaj (mixolidio)|Kafi (dorico)|Asavari (menor)|Bhairav|Yaman (lidio)|Todi|"
        "Bhupali (penta)|Sa-Pa (drone)";

    explicit SitarBlock (StoreId id);

    std::vector<std::string> getInfoChips() const override;
    bool isParamRelevant (int param) const noexcept override;

    /** MIDI notes of the four tampura strings (first string, Sa, Sa, low Sa). */
    static std::array<int, 4> tanpuraNotes (int key, int firstString) noexcept;

    /** MIDI note (and detune in cents) of every sympathetic string for a key / scale. */
    struct Tuning { std::array<int, kNumStrings> notes {}; std::array<float, kNumStrings> cents {}; };
    static Tuning tuningFor (int key, int scale) noexcept;

protected:
    void prepareBlock (double sampleRate, int maxBlockSize) override;
    void resetBlock() noexcept override;
    void processBlock (float* left, float* right, int numSamples) noexcept override;
    float* directBuffer (int channel) noexcept override;

private:
    struct String
    {
        DelayLine line;
        float length = 100.0f, feedback = 0.0f, damp = 0.0f, state = 0.0f;
        float panLeft = 0.7071f, panRight = 0.7071f;
    };

    /** One-sided bridge contact (the jawari) on one signal. */
    struct Jawari
    {
        DelayLine line;
        EnvelopeFollower peak;
        OnePole dc;
        void reset() noexcept { line.reset(); peak.reset(); dc.reset(); }
        float process (float x, float depth, float maxDelay) noexcept;
    };

    /** The modelled lead string (steel, jawari in the loop). */
    struct LeadString
    {
        DelayLine line;
        OnePole dc;
        float hz = 220.0f, targetHz = 220.0f, damp = 0.15f, state = 0.0f, last = 0.0f, shortenMean = 0.0f;
        float exciteLevel = 0.0f, exciteState = 0.0f;
        int exciteRemaining = 0, exciteLength = 1;
        void reset() noexcept { line.reset(); dc.reset(); state = last = shortenMean = exciteState = 0.0f; exciteRemaining = 0; }
    };

    /** Sitar body (gourd, neck, nasal mids), per channel of the lead. */
    struct Body
    {
        Biquad highPass, gourd, nasal, presence, lowPass;
        void reset() noexcept { highPass.reset(); gourd.reset(); nasal.reset(); presence.reset(); lowPass.reset(); }
        float process (float x) noexcept { return lowPass.process (presence.process (nasal.process (gourd.process (highPass.process (x))))); }
    };

    void retune() noexcept;
    float renderString (bool released, float depth) noexcept;
    void pluckString (float velocity) noexcept;
    static float velocityOf (float peak) noexcept;
    float nextNoise() noexcept;

    std::vector<float> mono, leadLeft, leadRight;
    WsolaPitchShifter bender;
    RealtimePitchTracker tracker;
    Jawari jawari;
    StateVariableFilter sweep;
    Biquad leadShelf;
    Biquad exciteHighPass;
    Body body;
    LeadString string;
    Biquad attackFilter;
    float trackedHz = 0.0f, attack = 0.0f;
    bool hasPitch = false, pluckPending = false, freshEstimate = false;
    int pendingAge = 0, silentFor = 0;
    std::uint32_t noiseState = 0x9e3779b9u;
    std::array<String, kNumStrings> strings;
    EnvelopeFollower fastEnvelope, slowEnvelope;
    LinearSmoother dryGain, leadGain, sympGain, buzzDepth, exciteGain;

    float noteHz = 220.0f, harmonic = 3.0f, pickPeak = 0.0f;
    int sinceOnset = 1 << 20;
    int currentKey = -1, currentScale = -1;
    float currentDecay = -1.0f, currentTone = -1.0f, currentLeadTone = -1.0f;
    bool frozen = false;

    TanpuraDrone drone;
    std::vector<float> droneLeft, droneRight;
    LinearSmoother tanpuraGain;
    bool droneDirect = false;   // this block's drone went to droneLeft / droneRight
};
} // namespace kumamp
