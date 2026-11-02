#pragma once

#include <vector>

namespace kumamp
{
/**
    Low-latency, polyphonic, time-domain pitch shifter (WSOLA splicing).

    Principle
    ---------
    Input is written into a circular buffer. A read head trails the write head
    by a variable delay d and advances at `ratio` samples per input sample, so
    d changes by (1 - ratio) every sample: the output is resampled -> pitch
    shifted. When d reaches its bound (minimum for up-shifts, maximum for
    down-shifts) a second head is started one "grain" away and the two are
    crossfaded.

    WSOLA part: the new head's position is not fixed. Within +/- searchRadius
    around the nominal jump it is chosen by maximising the normalised
    cross-correlation between the source material behind the outgoing head
    and behind the candidate. The splice therefore lands in phase, which
    removes the comb-filter "warble" of classic two-head shifters and keeps
    chords clean (no pitch detection, so it is inherently polyphonic).

    Latency is dynamic: between kMinDelay and kMinDelay + grain + searchRadius
    samples (about 0..20 ms with the default 15 ms grain at 48 kHz). The
    ratio can be glided continuously (whammy-style) without artefacts.

    Bass: a splice can only land in phase if the correlation window spans a
    full period and the search covers half of one. A low E on bass (41 Hz) has
    a 24 ms period, longer than the guitar-sized geometry above. At every
    splice the dominant period is estimated (NSDF on a decimated copy of the
    input); when it is long, grain / search / correlation grow to fit it and
    the search runs coarse-to-fine on that decimated copy so the cost stays
    bounded. Guitar material keeps the short, low-latency geometry.

    All memory is allocated in prepare(); process() is RT-safe. Stereo channels
    share the same splice decisions (taken on the mono sum) to keep the image.
*/
class WsolaPitchShifter
{
public:
    static constexpr int kBufferSize = 1 << 15;
    static constexpr int kMinDelay   = 4;     // Hermite interpolation needs 2 samples of look-ahead

    void prepare (double sampleRate);
    void reset() noexcept;

    /** Takes effect at the next splice. Clamped to 5..40 ms. */
    void setGrainMs (float milliseconds) noexcept;

    /** Pitch ratio target (0.5 .. 2.0) and one-pole glide time. */
    void setRatio (float targetRatio, float glideMilliseconds) noexcept;

    /** in/out may alias (in-place). */
    void process (const float* inLeft, const float* inRight,
                  float* outLeft, float* outRight, int numSamples) noexcept;

    float getRatio() const noexcept        { return ratio; }
    float getTargetRatio() const noexcept  { return targetRatio; }
    bool  isGliding() const noexcept;
    float getCurrentDelaySamples() const noexcept { return delays[activeHead]; }

    /** Period (samples) the geometry is currently fitted to; 0 = guitar-sized geometry. */
    float getTrackedPeriod() const noexcept { return trackedPeriod; }

private:
    static constexpr int kCoarseSize = 1 << 12;

    void updateGeometry() noexcept;
    void estimatePeriod() noexcept;
    void startSplice (float rate) noexcept;
    float findBestDelay (float outgoingDelay, float lowest, float highest) const noexcept;
    float correlationScore (int referenceEnd, int candidateEnd, int step) const noexcept;
    float coarseScore (int referenceEnd, int candidateEnd, int length) const noexcept;
    float read (const std::vector<float>& buffer, float delay) const noexcept;

    std::vector<float> bufferLeft, bufferRight, analysis;
    int writePos = 0;

    // Decimated, low-passed copy of `analysis` for period tracking and coarse search.
    std::vector<float> coarse, nsdf, trackWindow;
    int coarseWrite = 0, decimation = 8, decimationPhase = 0;
    float lpB0 = 0, lpB1 = 0, lpB2 = 0, lpA1 = 0, lpA2 = 0, lpZ1 = 0, lpZ2 = 0;
    float trackedPeriod = 0.0f;

    float delays[2] { (float) kMinDelay, (float) kMinDelay };
    int activeHead = 0;
    bool fading = false;
    int fadePos = 0;

    float ratio = 1.0f, targetRatio = 1.0f, glideCoefficient = 0.0f;

    double sampleRate = 48000.0;
    float requestedGrainMs = 15.0f;
    int grainLength = 720, searchRadius = 300, correlationLength = 240, fadeLength = 180;
};
} // namespace kumamp
