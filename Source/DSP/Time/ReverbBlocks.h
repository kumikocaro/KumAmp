#pragma once

#include "TrailsBlock.h"
#include "../Pitch/WsolaPitchShifter.h"
#include "../../Core/Biquad.h"
#include "../../Core/DspPrimitives.h"

#include <array>

namespace kumamp
{
/** 8-line feedback delay network with Householder mixing, per-line damping
    and slow modulation. Shared by Hall, Shimmer and Cloud. */
class FdnReverbCore
{
public:
    static constexpr int kLines = 8;

    void prepare (double sampleRate, float maxSizeScale);
    void configure (float sizeScale, float decaySeconds, float dampingHz, float modulationDepth, float modulationHz) noexcept;
    void reset() noexcept;
    void process (float inLeft, float inRight, float& outLeft, float& outRight) noexcept;

private:
    std::array<DelayLine, kLines> lines;
    std::array<OnePole, kLines> damping;
    std::array<float, kLines> lengths {}, gains {};
    std::array<double, kLines> phases {};
    double sampleRate = 48000.0, phaseIncrement = 0.0;
    float modulationSamples = 0.0f;
};

/** Input stage shared by the FDN reverbs: low cut, pre-delay, diffusion. */
struct ReverbInput
{
    void prepare (double sampleRate, int numDiffusers, float diffuserScale);
    void reset() noexcept;
    void process (float& left, float& right, float predelaySamples, float diffusion) noexcept;

    std::array<Biquad, 2> lowCut;
    std::array<DelayLine, 2> predelay;
    std::array<std::array<AllpassDiffuser, 8>, 2> diffusers;
    int numDiffusers = 4;
};

class HallReverbBlock final : public TrailsBlock
{
public:
    enum Param { DecaySeconds, Size, PredelayMs, Damping, LowCutHz, Modulation, Mix, NumParams };
    explicit HallReverbBlock (StoreId id);

protected:
    void prepareBlock (double, int) override;
    void resetBlock() noexcept override;
    void resetTail() noexcept override;
    void processBlock (float*, float*, int) noexcept override;

private:
    ReverbInput input;
    FdnReverbCore fdn;
    LinearSmoother mix;
};

/** Hall + octave (or fifth) pitch shifter inside the feedback: the tail keeps
    climbing ("shimmer"). */
class ShimmerReverbBlock final : public TrailsBlock
{
public:
    enum Param { DecaySeconds, Shimmer, Pitch, Damping, PredelayMs, LowCutHz, Mix, NumParams };
    explicit ShimmerReverbBlock (StoreId id);

protected:
    void prepareBlock (double, int) override;
    void resetBlock() noexcept override;
    void resetTail() noexcept override;
    void processBlock (float*, float*, int) noexcept override;

private:
    ReverbInput input;
    FdnReverbCore fdn;
    WsolaPitchShifter shifter;
    Biquad shimmerTone;
    float shimmerSample = 0.0f;
    LinearSmoother mix;
};

/** Ambient wash: long modulated diffusion feeding a large, heavily modulated FDN. */
class CloudReverbBlock final : public TrailsBlock
{
public:
    enum Param { DecaySeconds, Diffusion, Modulation, Size, Damping, LowCutHz, Mix, NumParams };
    explicit CloudReverbBlock (StoreId id);

protected:
    void prepareBlock (double, int) override;
    void resetBlock() noexcept override;
    void resetTail() noexcept override;
    void processBlock (float*, float*, int) noexcept override;

private:
    ReverbInput input;
    FdnReverbCore fdn;
    LinearSmoother mix;
};

/** Dattorro plate (J. Dattorro, "Effect Design Part 1", JAES 1997). */
class PlateReverbBlock final : public TrailsBlock
{
public:
    enum Param { Decay, PredelayMs, Bandwidth, Damping, Modulation, LowCutHz, Mix, NumParams };
    explicit PlateReverbBlock (StoreId id);

protected:
    void prepareBlock (double, int) override;
    void resetBlock() noexcept override;
    void resetTail() noexcept override;
    void processBlock (float*, float*, int) noexcept override;

private:
    struct Allpass   // lattice allpass exposing its line for output taps
    {
        DelayLine line;
        float length = 1.0f;
        float process (float x, float g, float modulation = 0.0f) noexcept
        {
            const float d = line.read (std::max (1.0f, length + modulation));
            const float v = x - g * d;
            line.push (v);
            return d + g * v;
        }
    };

    int scaled (int samplesAt29761) const noexcept;

    Biquad lowCut;
    DelayLine predelay;
    float bandwidthState = 0.0f;
    std::array<Allpass, 4> inputDiffusers;
    Allpass leftMod, rightMod, leftDiffuse, rightDiffuse;
    DelayLine leftDelayA, leftDelayB, rightDelayA, rightDelayB;
    float leftDamp = 0.0f, rightDamp = 0.0f, leftTankOut = 0.0f, rightTankOut = 0.0f;
    Lfo lfo;
    LinearSmoother mix;
};

/** Spring tank: dispersive allpass chain ("drip"/chirp) inside a feedback loop. */
class SpringReverbBlock final : public TrailsBlock
{
public:
    enum Param { Decay, ToneHz, Drip, Mix, LowCutHz, NumParams };
    static constexpr int kDispersionStages = 32;
    explicit SpringReverbBlock (StoreId id);

protected:
    void prepareBlock (double, int) override;
    void resetBlock() noexcept override;
    void resetTail() noexcept override;
    void processBlock (float*, float*, int) noexcept override;

private:
    struct Spring
    {
        std::array<AllpassDiffuser, kDispersionStages> dispersion;
        DelayLine loop;
        OnePole tone;
        Biquad lowCut;
        float loopLength = 1.0f, feedbackSample = 0.0f;
    };
    std::array<Spring, 2> springs;
    LinearSmoother mix;
};
} // namespace kumamp
