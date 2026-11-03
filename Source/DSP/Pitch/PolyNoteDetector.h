#pragma once

#include "../../Core/Biquad.h"

#include <array>
#include <cstdint>

namespace kumamp
{
/**
    Polyphonic note detection for the guitar synth's instruments (chords).

    The input is decimated to ~8 kHz and split by a semitone filter bank
    (E2 .. E7, two cascaded band passes per semitone). Every 4 ms each
    candidate fundamental (E2 .. C6) gets a harmonic salience (the weighted
    sum of its first harmonics' bands); the strongest is taken as a note, its
    harmonics are removed, and the search repeats (up to kMaxNotes). Notes
    need two hops to turn on and ~40 ms of absence to turn off.

    It reads the strings ringing, not MIDI: the low strings take ~40-60 ms to
    settle (a semitone band is narrow) and a doubled octave in a chord is heard
    as a harmonic of the lower note. For sustained sounds this is inaudible.

    All storage is fixed size; process() is RT-safe.
*/
class PolyNoteDetector
{
public:
    static constexpr int kLowestNote  = 40;   // E2
    static constexpr int kHighestNote = 84;   // C6 (highest fundamental)
    static constexpr int kTopBand     = 100;  // E7 (harmonics)
    static constexpr int kNumBands    = kTopBand - kLowestNote + 1;
    static constexpr int kMaxNotes    = 6;

    struct NoteSet
    {
        std::array<std::uint8_t, kMaxNotes> notes {};   // MIDI notes, ascending
        std::array<float, kMaxNotes> levels {};          // band amplitude (linear)
        int count = 0;
    };

    void prepare (double sampleRate);
    void reset() noexcept;

    /** Mono input; calls onUpdate (sampleOffset, const NoteSet&) every hop. */
    template <typename Callback>
    void process (const float* mono, int numSamples, Callback&& onUpdate) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float filtered = antiAlias[2].process (antiAlias[1].process (antiAlias[0].process (mono[i])));
            if (++phase < decimation)
                continue;
            phase = 0;
            analyseSample (filtered);
            if (++sinceHop < kHop)
                continue;
            sinceHop = 0;
            updateNotes();
            onUpdate (i, current);
        }
    }

    const NoteSet& getNotes() const noexcept { return current; }
    /** True while some band is still building up (a pick): the note set may still change. */
    bool isSettling() const noexcept { return settlingNow; }

private:
    void analyseSample (float x) noexcept;
    void updateNotes() noexcept;

    static constexpr int kHop = 32;   // 4 ms at 8 kHz

    struct Band
    {
        Biquad a, b;
        float power = 0.0f, lastPower = 0.0f, smoothed = 0.0f, smoothing = 0.0f;
        bool active = false;
    };
    std::array<Band, kNumBands> bands;
    std::array<Biquad, 3> antiAlias;
    std::array<std::uint8_t, kNumBands> seen {}, missing {};
    int decimation = 6, phase = 0, sinceHop = 0;
    NoteSet current;
    bool settlingNow = false;
};
} // namespace kumamp
