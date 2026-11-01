#pragma once

#include "../../Core/RealtimePitchTracker.h"

#include <array>
#include <cmath>

namespace kumamp
{
/**
    Turns the pitch tracker's estimates into MIDI note / pitch-bend events
    (monophonic: one note at a time, like any guitar-to-MIDI with a normal pickup).

    - Note on after the pitch is stable for two hops (~8 ms) above the threshold.
    - Velocity from the attack level.
    - Gradual pitch changes (bends, vibrato, slides) become pitch bend within the
      configured range; a jump of more than ~0.4 semitone in one hop (new fret,
      hammer-on) or leaving the bend range starts a new note (legato).
    - A re-pick of the same note (level jump) re-triggers it.
    - Note off when the level drops 6 dB below the threshold.
    Allocation-free: events go into a fixed array per audio block.
*/
class GuitarToMidi
{
public:
    struct Event
    {
        enum class Type { NoteOn, NoteOff, PitchBend };
        Type type = Type::NoteOn;
        int sampleOffset = 0;
        int note = 0;          // NoteOn / NoteOff
        int velocity = 0;      // NoteOn
        int bend = 8192;       // PitchBend, 0 .. 16383
    };

    static constexpr int kMaxEvents = 64;

    void setThresholdDb (float db) noexcept   { thresholdDb = db; }
    void setBendRange (int semitones) noexcept { bendRange = std::max (0, semitones); }
    void setTranspose (int semitones) noexcept { transpose = semitones; }

    void beginBlock() noexcept { numEvents = 0; }
    int getNumEvents() const noexcept { return numEvents; }
    const Event& getEvent (int i) const noexcept { return events[(size_t) i]; }

    int getActiveNote() const noexcept { return activeNote; }
    float getBendSemitones() const noexcept { return lastBend; }

    /** Releases the sounding note (bypass, disable, transport stop). */
    void allNotesOff (int sampleOffset) noexcept
    {
        if (activeNote >= 0)
        {
            push ({ Event::Type::NoteOff, sampleOffset, activeNote, 0, 8192 });
            if (bendRange > 0 && std::abs (lastBend) > 0.0f)
                push ({ Event::Type::PitchBend, sampleOffset, 0, 0, 8192 });
        }
        activeNote = candidateNote = -1;
        candidateCount = 0;
        lastBend = 0.0f;
    }

    void onEstimate (int sampleOffset, const RealtimePitchTracker::Estimate& e) noexcept
    {
        // Note off: level fell clearly below the threshold.
        if (activeNote >= 0 && e.fastDb < thresholdDb - 6.0f)
            allNotesOff (sampleOffset);

        if (! e.valid || e.fastDb < thresholdDb)
        {
            candidateCount = 0;
            lastMidi = -1.0f;
            return;
        }

        const float midi = 69.0f + 12.0f * std::log2 (e.frequency / 440.0f) + (float) transpose;
        const int note = (int) std::lround (midi);
        const bool jump = lastMidi >= 0.0f && std::abs (midi - lastMidi) > 0.4f;
        const bool onset = e.fastDb - e.slowDb > 6.0f;
        lastMidi = midi;

        if (activeNote < 0)
        {
            if (confirm (note))
                start (sampleOffset, note, e.fastDb);
            return;
        }

        const float deviation = midi - (float) activeNote;
        const bool outOfBend = bendRange == 0 ? std::abs (deviation) > 0.5f : std::abs (deviation) > (float) bendRange;

        if ((jump || outOfBend) && note != activeNote)
        {
            if (confirm (note))
            {
                const int old = activeNote;
                push ({ Event::Type::NoteOff, sampleOffset, old, 0, 8192 });
                activeNote = -1;
                start (sampleOffset, note, e.fastDb);
            }
            return;
        }

        ++hopsSinceStart;
        if (onset && note == activeNote && hopsSinceStart > 8 && e.fastDb > lastOnsetDb + 6.0f)
        {
            // Re-pick of the same note.
            push ({ Event::Type::NoteOff, sampleOffset, activeNote, 0, 8192 });
            activeNote = -1;
            start (sampleOffset, note, e.fastDb);
            return;
        }
        lastOnsetDb = std::min (lastOnsetDb, e.fastDb);

        if (bendRange > 0 && std::abs (deviation - lastBend) > 0.02f)   // ~2 cents
        {
            lastBend = std::clamp (deviation, -(float) bendRange, (float) bendRange);
            push ({ Event::Type::PitchBend, sampleOffset, 0, 0, bendValue (lastBend) });
        }
    }

private:
    bool confirm (int note) noexcept
    {
        if (note == candidateNote) ++candidateCount;
        else { candidateNote = note; candidateCount = 1; }
        return candidateCount >= 2;
    }

    void start (int sampleOffset, int note, float levelDb) noexcept
    {
        if (note < 0 || note > 127)
            return;
        if (bendRange > 0 && std::abs (lastBend) > 0.0f)
            push ({ Event::Type::PitchBend, sampleOffset, 0, 0, 8192 });   // centre before the new note
        lastBend = 0.0f;

        // Velocity: threshold .. -6 dBFS -> 20 .. 127
        const float t = std::clamp ((levelDb - thresholdDb) / std::max (1.0f, -6.0f - thresholdDb), 0.0f, 1.0f);
        push ({ Event::Type::NoteOn, sampleOffset, note, (int) std::lround (20.0f + 107.0f * t), 8192 });
        activeNote = note;
        candidateCount = 0;
        hopsSinceStart = 0;   // ignore "onsets" during the attack itself (~32 ms)
        lastOnsetDb = levelDb;
    }

    int bendValue (float semitones) const noexcept
    {
        return std::clamp ((int) std::lround (8192.0f + semitones / (float) bendRange * 8191.0f), 0, 16383);
    }

    void push (const Event& e) noexcept
    {
        if (numEvents < kMaxEvents)
            events[(size_t) numEvents++] = e;
    }

    std::array<Event, kMaxEvents> events {};
    int numEvents = 0;
    int activeNote = -1, candidateNote = -1, candidateCount = 0, hopsSinceStart = 0;
    float lastMidi = -1.0f, lastBend = 0.0f, lastOnsetDb = 0.0f;
    float thresholdDb = -45.0f;
    int bendRange = 2, transpose = 0;
};
} // namespace kumamp
