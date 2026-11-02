#pragma once

#include <vector>

namespace kumamp
{
/**
    Low-latency, polyphonic, time-domain pitch shifter (adaptive-lag WSOLA).

    Principle
    ---------
    Input is written into a circular buffer. A read head trails the write head
    by a variable delay d and advances at `ratio` samples per input sample, so
    d changes by (1 - ratio) every sample: the output is resampled -> pitch
    shifted. From time to time a second head is started elsewhere in the
    buffer and the two are crossfaded (a splice), which keeps d bounded.

    Adaptive splice
    ---------------
    A splice is clean only if the new head continues the waveform in phase. A
    single note repeats every period, but a chord only repeats at the common
    period of its notes (24 ms for an open E, 30-40 ms for open C / G), so a
    fixed grain either adds latency to everything or smears chords. Here the
    jump is chosen per splice:

      - pitch down: d grows; every kCheckInterval samples the splice looks for
        a jump forward (to a smaller delay) whose normalised correlation with
        the outgoing head is a real peak >= kThreshold. The smallest resulting
        delay wins, so notes splice after one period and chords as soon as
        their common period fits. Only past maxJump is the best peak forced.
      - pitch up: d shrinks; near the minimum the head jumps back by the
        shortest lag that reaches the threshold (the best one otherwise).

    Candidates are scored on a low-passed copy decimated to ~6 kHz (cheap long
    windows) and refined at full rate. Only interior local maxima count:
    taking the first value above the threshold lands on the edge of the peak,
    always on the same side, which drifts the pitch.

    Instrument sets the windows: guitar (6..45 ms jumps, 10 ms correlation: an open G repeats every 41 ms)
    or bass (long periods: up to 70 ms jumps, 25 ms correlation).

    All memory is allocated in prepare(); process() is RT-safe. Stereo channels
    share the splice decisions (taken on the mono sum) to keep the image.
*/
class WsolaPitchShifter
{
public:
    static constexpr int kBufferSize = 1 << 15;
    static constexpr int kMinDelay   = 4;     // Hermite interpolation needs 2 samples of look-ahead

    enum class Instrument { Guitar, Bass };

    void prepare (double sampleRate);
    void reset() noexcept;

    /** Longest splice jump (ms): more = cleaner chords, more worst-case latency. Clamped to 10..80. */
    void setGrainMs (float milliseconds) noexcept;

    /** Guitar (default) or bass (longer windows for low notes). */
    void setInstrument (Instrument newInstrument) noexcept;

    /** Pitch ratio target (0.5 .. 2.0) and one-pole glide time. */
    void setRatio (float targetRatio, float glideMilliseconds) noexcept;

    /** in/out may alias (in-place). */
    void process (const float* inLeft, const float* inRight,
                  float* outLeft, float* outRight, int numSamples) noexcept;

    float getRatio() const noexcept        { return ratio; }
    float getTargetRatio() const noexcept  { return targetRatio; }
    bool  isGliding() const noexcept;
    float getCurrentDelaySamples() const noexcept { return delays[activeHead]; }

private:
    static constexpr int kCoarseSize = 1 << 12;

    void updateGeometry() noexcept;
    void trySplice (float rate) noexcept;
    float findSplice (float current, float lowest, float highest, bool lowestDelayFirst, bool force) noexcept;
    float coarseScore (float reference, float candidate, int length) const noexcept;
    float fineScore (float reference, float candidate, int length) const noexcept;
    void startFade (float newDelay, float jump) noexcept;
    float read (const std::vector<float>& buffer, float delay) const noexcept;

    std::vector<float> bufferLeft, bufferRight, analysis;
    int writePos = 0;

    // Decimated, low-passed copy of `analysis` for the coarse search.
    std::vector<float> coarse;
    int coarseWrite = 0, decimation = 8, decimationPhase = 0;
    float lpB0 = 0, lpB1 = 0, lpB2 = 0, lpA1 = 0, lpA2 = 0, lpZ1 = 0, lpZ2 = 0;

    float delays[2] { (float) kMinDelay, (float) kMinDelay };
    int activeHead = 0;
    bool fading = false;
    int fadePos = 0, fadeLength = 96, sinceCheck = 0;

    float ratio = 1.0f, targetRatio = 1.0f, glideCoefficient = 0.0f;

    double sampleRate = 48000.0;
    float requestedMaxJumpMs = 45.0f;
    Instrument instrument = Instrument::Guitar;
    float minJump = 288, maxJump = 1920, fadeMin = 96, fadeMax = 384;
    int correlationLength = 480, coarseCorrelationLength = 60;
};
} // namespace kumamp
