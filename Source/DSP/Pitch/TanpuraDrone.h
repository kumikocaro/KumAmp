#pragma once

#include <array>
#include <cstdint>

namespace kumamp
{
/**
    Sustained tampura drone (additive), the backing of the Sitar block.

    Four strings (first string, Sa, Sa, low Sa) sound continuously, each as a
    bank of harmonics. What makes a tampura shimmer is its bridge (jawari):
    energy keeps moving through the upper harmonics. Here every string has a
    resonance that wanders slowly up and down its harmonics (Textura sets how
    strong and wide it is, Movimiento how slow), with its own speed, plus a
    gentle breathing of the level and partials a fraction of a cent apart, so
    the drone never sits still yet stays on its note.

    Harmonics are complex rotators (4 multiplies per partial per sample);
    their levels are recomputed every 64 samples and ramped. All storage is
    fixed size; process() is RT-safe.
*/
class TanpuraDrone
{
public:
    static constexpr int kStrings = 4;
    static constexpr int kPartials = 32;

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    /** MIDI notes of the strings; a change keeps every phase (no click). */
    void setNotes (const std::array<int, kStrings>& notes) noexcept;

    /** How much it moves besides the resonance sweep. All at 0 (and texture 0): a perfectly still drone. */
    struct Motion
    {
        float detuneCents = 3.0f;   // between the two Sa strings; the partials spread with it
        float breath = 0.12f;       // level swell, 0..1
        float drift = 1.0f;         // irregularity of the sweep, 0..1
    };

    /** Writes the drone (stereo). texture 0..1, movementSeconds: one sweep of the resonance. */
    void process (float* left, float* right, int numSamples, float texture, float movementSeconds, const Motion& motion) noexcept;

private:
    static constexpr int kControlInterval = 64;

    struct Voice
    {
        float hz = 110.0f;
        int count = 0;
        std::array<float, kPartials> re {}, im {}, rotRe {}, rotIm {}, amp {}, step {}, base {}, detune {};
        double sweepPhase = 0.0, breathPhase = 0.0, driftPhase = 0.0;
        float level = 1.0f, panLeft = 0.7071f, panRight = 0.7071f;
    };

    void tune (Voice& v, int note, float cents) noexcept;
    void control (int index, float texture, float movementSeconds, const Motion& motion) noexcept;
    float nextRandom() noexcept;

    std::array<Voice, kStrings> voices;
    std::array<int, kStrings> currentNotes { -1, -1, -1, -1 };
    double sampleRate = 48000.0;
    float currentDetune = 3.0f;
    int countdown = 0;
    std::uint32_t randomState = 0x6d2b79f5u;
};
} // namespace kumamp
