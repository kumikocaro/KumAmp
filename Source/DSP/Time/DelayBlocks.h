#pragma once

#include "TrailsBlock.h"
#include "../../Core/Biquad.h"
#include "../../Core/DspPrimitives.h"

#include <array>

namespace kumamp
{
/** Clean stereo delay; filters in the feedback path, optional ping-pong. */
class DigitalDelayBlock final : public TrailsBlock
{
public:
    enum Param { TimeMs, Feedback, Mix, LowCutHz, HighCutHz, PingPong, Sync, Division, NumParams };
    explicit DigitalDelayBlock (StoreId id);
    std::string getDisplayOverride (int param) const override;
    /** Delay time in ms: the knob, or the note division at the host tempo. */
    float effectiveTimeMs() const noexcept;

protected:
    void prepareBlock (double, int) override;
    void resetBlock() noexcept override;
    void resetTail() noexcept override;
    void processBlock (float*, float*, int) noexcept override;

private:
    std::array<DelayLine, 2> lines;
    std::array<Biquad, 2> lowCut, highCut;
    float delaySamples = 0.0f, timeCoefficient = 0.0f;
    LinearSmoother mix;
};

/** Bucket-brigade flavour: darkening, saturating repeats, gentle modulation,
    self-oscillates above 100 % feedback. */
class AnalogDelayBlock final : public TrailsBlock
{
public:
    enum Param { TimeMs, Feedback, Mix, ToneHz, Modulation, Sync, Division, NumParams };
    explicit AnalogDelayBlock (StoreId id);
    std::string getDisplayOverride (int param) const override;
    /** Delay time in ms: the knob, or the note division at the host tempo. */
    float effectiveTimeMs() const noexcept;

protected:
    void prepareBlock (double, int) override;
    void resetBlock() noexcept override;
    void resetTail() noexcept override;
    void processBlock (float*, float*, int) noexcept override;

private:
    std::array<DelayLine, 2> lines;
    std::array<OnePole, 2> toneA, toneB, highPass;
    Lfo lfo;
    float delaySamples = 0.0f, timeCoefficient = 0.0f;
    LinearSmoother mix;
};

/** Tape echo: wow & flutter, tape saturation, ageing (bandwidth loss). */
class TapeDelayBlock final : public TrailsBlock
{
public:
    enum Param { TimeMs, Feedback, Mix, Wow, Flutter, Saturation, Age, Sync, Division, NumParams };
    explicit TapeDelayBlock (StoreId id);
    std::string getDisplayOverride (int param) const override;
    /** Delay time in ms: the knob, or the note division at the host tempo. */
    float effectiveTimeMs() const noexcept;

protected:
    void prepareBlock (double, int) override;
    void resetBlock() noexcept override;
    void resetTail() noexcept override;
    void processBlock (float*, float*, int) noexcept override;

private:
    std::array<DelayLine, 2> lines;
    std::array<Biquad, 2> ageLow, ageHigh;
    Lfo wowLfo, flutterLfo;
    float drift = 0.0f, driftTarget = 0.0f;
    unsigned int noiseState = 0x12345u;
    int driftCounter = 0;
    float delaySamples = 0.0f, timeCoefficient = 0.0f;
    LinearSmoother mix;
};

/** Reverse delay: Hann-windowed backward-reading heads, 50 % overlap. */
class ReverseDelayBlock final : public TrailsBlock
{
public:
    enum Param { TimeMs, Feedback, Mix, Sync, Division, NumParams };
    explicit ReverseDelayBlock (StoreId id);
    std::string getDisplayOverride (int param) const override;
    /** Delay time in ms: the knob, or the note division at the host tempo. */
    float effectiveTimeMs() const noexcept;

protected:
    void prepareBlock (double, int) override;
    void resetBlock() noexcept override;
    void resetTail() noexcept override;
    void processBlock (float*, float*, int) noexcept override;

private:
    struct Head { int position = 0; int length = 1; };
    std::array<DelayLine, 2> lines;
    std::array<Head, 2> heads;
    LinearSmoother mix;
};
} // namespace kumamp
