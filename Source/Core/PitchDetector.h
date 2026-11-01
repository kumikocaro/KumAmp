#pragma once

#include <vector>

namespace kumamp
{
/**
    Monophonic pitch detector, McLeod Pitch Method (normalised square
    difference function + key-maximum picking + parabolic refinement).

    Robust against the strong 2nd harmonic of guitar/bass strings (no octave
    errors) and accurate to a fraction of a cent. Not RT-bound: runs on the
    tuner's analysis thread. All buffers are allocated in prepare().
*/
class PitchDetector
{
public:
    struct Result
    {
        bool valid = false;
        float frequency = 0.0f;   // Hz
        float clarity = 0.0f;     // 0..1, NSDF peak height
        float levelDb = -120.0f;  // RMS of the analysis window
    };

    /** windowSize must be >= 2 * rate / minHz for the lowest note to be seen. */
    void prepare (double analysisRate, int windowSize, float minHz = 30.0f, float maxHz = 1500.0f);

    /** `window` holds getWindowSize() samples, oldest first. */
    Result analyse (const float* window) noexcept;

    int getWindowSize() const noexcept { return windowSize; }

private:
    double rate = 24000.0;
    int windowSize = 2048, minLag = 16, maxLag = 800;
    std::vector<float> centred, nsdf;

    static constexpr float kKeyMaximumRatio = 0.90f;   // MPM "k"
    static constexpr float kMinClarity      = 0.80f;
    static constexpr float kSilenceDb       = -60.0f;
};

/** Note helpers (equal temperament). */
struct NoteInfo
{
    int midiNote = -1;      // nearest note
    float cents = 0.0f;     // -50..+50 from that note
};

NoteInfo frequencyToNote (float frequency, float referenceA4) noexcept;
const char* noteName (int midiNote) noexcept;          // "C", "C#", ... "B"
int noteOctave (int midiNote) noexcept;                // scientific pitch: A4 = 69
} // namespace kumamp
