#pragma once

#include "../../Core/Biquad.h"
#include "../../Core/DspPrimitives.h"
#include "../../Core/Smoothing.h"
#include "../../Graph/DSPBlock.h"
#include "../Time/ReverbBlocks.h"

#include <array>
#include <vector>

namespace kumamp
{
/** Stereo chorus: modulated delay, quadrature LFOs for width. */
class ChorusBlock final : public DSPBlock
{
public:
    enum Param { RateHz, Depth, DelayMs, Mix, Spread, Sync, Division, NumParams };
    explicit ChorusBlock (StoreId id);
    std::string getDisplayOverride (int param) const override;
    /** LFO rate in Hz: the knob, or one cycle per note division at the host tempo. */
    float effectiveRateHz() const noexcept;

protected:
    void prepareBlock (double, int) override;
    void resetBlock() noexcept override;
    void processBlock (float*, float*, int) noexcept override;

private:
    std::array<DelayLine, 2> lines;
    Lfo lfo;
    LinearSmoother mix;
};

/** Through-zero-style flanger (short modulated delay with +/- feedback). */
class FlangerBlock final : public DSPBlock
{
public:
    enum Param { RateHz, Depth, ManualMs, Feedback, Mix, Spread, Sync, Division, NumParams };
    explicit FlangerBlock (StoreId id);
    std::string getDisplayOverride (int param) const override;
    /** LFO rate in Hz: the knob, or one cycle per note division at the host tempo. */
    float effectiveRateHz() const noexcept;

protected:
    void prepareBlock (double, int) override;
    void resetBlock() noexcept override;
    void processBlock (float*, float*, int) noexcept override;

private:
    std::array<DelayLine, 2> lines;
    Lfo lfo;
    LinearSmoother mix;
};

/** Phaser: 2..12 first-order allpass stages swept by an LFO, with feedback. */
class PhaserBlock final : public DSPBlock
{
public:
    enum Param { RateHz, Depth, Stages, Feedback, CenterHz, Mix, Spread, Sync, Division, NumParams };
    static constexpr int kMaxStages = 12;
    explicit PhaserBlock (StoreId id);
    std::string getDisplayOverride (int param) const override;
    /** LFO rate in Hz: the knob, or one cycle per note division at the host tempo. */
    float effectiveRateHz() const noexcept;

protected:
    void prepareBlock (double, int) override;
    void resetBlock() noexcept override;
    void processBlock (float*, float*, int) noexcept override;

private:
    struct Channel { std::array<float, kMaxStages> x1 {}, y1 {}; float last = 0.0f; };
    std::array<Channel, 2> channels;
    Lfo lfo;
    LinearSmoother mix;
};

/** Tremolo with sine -> square morph and stereo phase (up to auto-pan). */
class TremoloBlock final : public DSPBlock
{
public:
    enum Param { RateHz, Depth, Shape, Stereo, Sync, Division, NumParams };
    explicit TremoloBlock (StoreId id);
    std::string getDisplayOverride (int param) const override;
    /** LFO rate in Hz: the knob, or one cycle per note division at the host tempo. */
    float effectiveRateHz() const noexcept;

protected:
    void prepareBlock (double, int) override;
    void resetBlock() noexcept override;
    void processBlock (float*, float*, int) noexcept override;

private:
    Lfo lfo;
    LinearSmoother depth;
};

/** Repeat percussion: the organ and solid-state amp effect of the 60s: a reverse sawtooth
    chops the volume, so each cycle hits at full level and decays. The original is not an
    echo; Cortina holds the sound in a diffuse tail, chopped too, so it keeps going after you
    stop playing (at 100 % it takes minutes to fade). */
class RepeatPercussionBlock final : public DSPBlock
{
public:
    enum Param { RateHz, Depth, Decay, Sync, Division, Curtain, NumParams };
    explicit RepeatPercussionBlock (StoreId id);
    std::string getDisplayOverride (int param) const override;
    float effectiveRateHz() const noexcept;

protected:
    void prepareBlock (double, int) override;
    void resetBlock() noexcept override;
    void processBlock (float*, float*, int) noexcept override;

private:
    Lfo lfo;
    LinearSmoother depth, curtain;
    float envelope = 1.0f, envelopeCoef = 1.0f;

    // Curtain: a diffuse network, not a delay of one cycle: that is a comb that makes
    // some notes ring and cancels others.
    FdnReverbCore network;
    ReverbInput diffusion;
    float wetPeak = 0.0f, wetRelease = 1.0f;
};

/** Rotary speaker: horn/drum crossover, each with Doppler + AM, inertia when
    switching slow/fast, two virtual mics for stereo. */
class RotaryBlock final : public DSPBlock
{
public:
    enum Param { Speed, Inertia, Balance, Depth, MicSpread, Mix, NumParams };
    explicit RotaryBlock (StoreId id);

protected:
    void prepareBlock (double, int) override;
    void resetBlock() noexcept override;
    void processBlock (float*, float*, int) noexcept override;

private:
    Biquad crossLow, crossHigh;
    std::array<DelayLine, 2> hornLines, drumLines;
    double hornPhase = 0.0, drumPhase = 0.0;
    float hornSpeed = 0.8f, drumSpeed = 0.7f;
    LinearSmoother mix;
};

/** Uni-Vibe: four allpass stages with staggered corners driven by a skewed
    "lamp" LFO; chorus (mixed) or vibrato (wet only). */
class UnivibeBlock final : public DSPBlock
{
public:
    enum Param { SpeedHz, Intensity, Mode, LevelDb, Sync, Division, NumParams };
    explicit UnivibeBlock (StoreId id);
    std::string getDisplayOverride (int param) const override;
    /** LFO rate in Hz: the knob, or one cycle per note division at the host tempo. */
    float effectiveRateHz() const noexcept;

protected:
    void prepareBlock (double, int) override;
    void resetBlock() noexcept override;
    void processBlock (float*, float*, int) noexcept override;

private:
    struct Channel { std::array<float, 4> x1 {}, y1 {}; };
    std::array<Channel, 2> channels;
    Lfo lfo;
    float lamp = 0.0f, lampCoefficient = 0.0f;
    LinearSmoother wet, level;
};

/** Bit depth + sample-rate reduction. */
class BitcrusherBlock final : public DSPBlock
{
public:
    enum Param { Bits, Downsample, Mix, LevelDb, NumParams };
    explicit BitcrusherBlock (StoreId id);

protected:
    void prepareBlock (double, int) override;
    void resetBlock() noexcept override;
    void processBlock (float*, float*, int) noexcept override;

private:
    float heldL = 0.0f, heldR = 0.0f, holdPhase = 0.0f;
    LinearSmoother mix, level;
};
} // namespace kumamp
