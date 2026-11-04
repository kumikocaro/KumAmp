#pragma once

#include "PolyNoteDetector.h"
#include "WsolaPitchShifter.h"
#include "../../Core/Biquad.h"
#include "../../Core/DspPrimitives.h"
#include "../../Core/Smoothing.h"
#include "../../Graph/DSPBlock.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace kumamp
{
/**
    Charango (northern Argentina / Andes): 10 nylon strings in 5 double
    courses, temple natural G4 C5 E5/E4 A4 E5, re-entrant, all within about
    an octave, the middle (E) course in octaves; a small, bright body with a
    short sustain.

    Procesado (low latency, ~15 ms): the guitar an octave up (polyphonic
    WSOLA), each note doubled like a course (two slightly detuned, delayed
    copies, spread in stereo), the octavado course (the other octave mixed
    in), a shorter sustain (the decay is pulled down relative to the pick) and
    the small body (no lows, air and plate resonances, bright top).

    Modelado: the polyphonic note detector reads the chord and re-voices it
    the charango way (every note folded into E4..D#5, the lowest course in
    octaves), played by plucked nylon double courses (waveguides) through the
    same body. Each pick strums the courses, alternating down / up when the
    picks come fast. Truest on chords; adds the detector's 40-80 ms.

    All memory is allocated in prepare(); processBlock() is RT-safe.
*/
class CharangoBlock final : public DSPBlock
{
public:
    enum Param { Mode, Register, Courses, Octavado, Body, Sustain, Tone, Strum, Mix, LevelDb, NumParams };
    enum class Engine { Processed, Modelled };
    static constexpr int kMaxCourses = PolyNoteDetector::kMaxNotes;
    static constexpr int kLowestFold = 64;   // E4: notes are folded into E4 .. D#5

    explicit CharangoBlock (StoreId id);

    bool isParamRelevant (int param) const noexcept override;
    std::vector<std::string> getInfoChips() const override;
    /** UI: the charango voicing being played ("E4 G4 C5"), Modelado only. Any thread. */
    std::string getHeldNotesText() const;

    /** The detected notes re-voiced for the charango (folded, deduplicated, ascending). */
    static int foldVoicing (const std::uint8_t* notes, int count, std::uint8_t* out) noexcept;

protected:
    void prepareBlock (double sampleRate, int maxBlockSize) override;
    void resetBlock() noexcept override;
    void processBlock (float* left, float* right, int numSamples) noexcept override;

private:
    struct PluckString
    {
        DelayLine line;
        float length = 100.0f, holdFeedback = 0.0f, releaseFeedback = 0.0f, damp = 0.3f, state = 0.0f;
        float level = 1.0f, panLeft = 0.7071f, panRight = 0.7071f;
        float exciteLevel = 0.0f, exciteState = 0.0f;
        int exciteRemaining = 0;
        void reset() noexcept { line.reset(); state = exciteState = 0.0f; exciteRemaining = 0; }
    };
    struct Course
    {
        int note = -1;
        bool held = false;
        int pluckIn = -1;            // samples until a scheduled pluck (-1 = none)
        float velocity = 0.7f;
        std::array<PluckString, 2> strings;
    };

    void renderProcessed (int numSamples) noexcept;
    void renderModelled (int numSamples) noexcept;
    void onNotes (const std::uint8_t* notes, int count) noexcept;
    void strum() noexcept;
    void tuneCourse (Course& c, int note) noexcept;
    void pluck (Course& c) noexcept;
    float nextNoise() noexcept;

    std::vector<float> mono, shifted, scratch, outLeft, outRight;
    WsolaPitchShifter shifter;
    DelayLine courseLine;
    double coursePhase = 0.0;
    EnvelopeFollower sustainFast, sustainPeak;

    PolyNoteDetector detector;
    std::array<Course, kMaxCourses> courses;
    std::array<std::uint8_t, kMaxCourses> voicing {};
    int voicingCount = 0;
    EnvelopeFollower fastEnvelope, slowEnvelope;
    float pickPeak = 0.0f, pickVelocity = 0.7f;
    int sinceOnset = 1 << 20, sinceStrum = 1 << 20, strumWait = 0;
    bool strumPending = false, strumUp = false;
    std::uint32_t noiseState = 0x2545f491u;
    std::atomic<std::uint64_t> voicingForUi { 0 };

    // Body (both engines), per channel.
    struct BodyFilter
    {
        Biquad highPass, air, plate, presence, top, lowPass;
        void reset() noexcept { highPass.reset(); air.reset(); plate.reset(); presence.reset(); top.reset(); lowPass.reset(); }
        float process (float x) noexcept { return lowPass.process (top.process (presence.process (plate.process (air.process (highPass.process (x)))))); }
    };
    std::array<BodyFilter, 2> body;
    float currentBody = -1.0f, currentTone = -1.0f, currentSustain = -1.0f;
    LinearSmoother mix, level, courseAmount, octaveAmount;
    int currentEngine = -1;
};
} // namespace kumamp
