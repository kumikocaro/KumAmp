#pragma once

#include "WsolaPitchShifter.h"
#include "../../Core/Smoothing.h"
#include "../../Graph/DSPBlock.h"

#include <vector>

namespace kumamp
{
/**
    Grid block wrapping WsolaPitchShifter: -12..+12 semitones plus fine tune,
    dry/wet mix (harmoniser use), grain size (latency vs. smoothness) and glide
    (whammy-style sweeps when driven by a macro / expression pedal), and the
    instrument: Guitarra (lowest latency) or Bajo (clean splices on low notes).

    At 0 semitones / 0 cents the wet path is crossfaded out, so the block is
    truly zero-latency when not transposing.
*/
class PitchShifterBlock final : public DSPBlock
{
public:
    enum Param { Semitones, Cents, Mix, GrainMs, GlideMs, Instrument, NumParams };

    explicit PitchShifterBlock (StoreId id);

    /** Instantaneous shifter delay, for UI display. Audio thread value, read racily. */
    float getCurrentDelaySamples() const noexcept { return shifter.getCurrentDelaySamples(); }

protected:
    void prepareBlock (double sampleRate, int maxBlockSize) override;
    void resetBlock() noexcept override;
    void processBlock (float* left, float* right, int numSamples) noexcept override;

private:
    WsolaPitchShifter shifter;
    std::vector<float> wetLeft, wetRight;
    LinearSmoother wetAmount;
};
} // namespace kumamp
