#pragma once

#include "../../Core/Biquad.h"
#include "../../Core/DspPrimitives.h"
#include "../../Core/SampleRing.h"
#include "../../Core/Smoothing.h"
#include "EqDesign.h"
#include "../../Graph/DSPBlock.h"

#include <array>
#include <atomic>
#include <vector>

namespace kumamp
{
/** Discrete parameters whose unit contains '|' are shown as a choice list
    ("Off|On", "Lento|Rapido"...); the value is the 0-based index. */

//==============================================================================
/** Noise gate with hysteresis and hold, stereo-linked, smooth gain ramps. */
class NoiseGateBlock final : public DSPBlock
{
public:
    enum Param { ThresholdDb, HysteresisDb, AttackMs, HoldMs, ReleaseMs, RangeDb, NumParams };
    explicit NoiseGateBlock (StoreId id);
    float getGainDb() const noexcept { return meterGainDb.load (std::memory_order_relaxed); }

protected:
    void prepareBlock (double, int) override;
    void resetBlock() noexcept override;
    void processBlock (float*, float*, int) noexcept override;

private:
    EnvelopeFollower detector;
    float gain = 1.0f;
    int holdCounter = 0;
    bool open = false;
    std::atomic<float> meterGainDb { 0.0f };
};

//==============================================================================
/** Feed-forward compressor: soft knee, log-domain attack/release, parallel mix. */
class CompressorBlock final : public DSPBlock
{
public:
    enum Param { ThresholdDb, Ratio, AttackMs, ReleaseMs, KneeDb, MakeupDb, Mix, NumParams };
    explicit CompressorBlock (StoreId id);
    float getGainReductionDb() const noexcept { return meterReductionDb.load (std::memory_order_relaxed); }

protected:
    void prepareBlock (double, int) override;
    void resetBlock() noexcept override;
    void processBlock (float*, float*, int) noexcept override;

private:
    float reductionDb = 0.0f, heldDb = 0.0f;
    LinearSmoother makeup, mix;
    std::atomic<float> meterReductionDb { 0.0f };
};

//==============================================================================
/** 8-band parametric EQ (Bell, shelves, 12-48 dB/oct cuts, notch, band pass)
    with pre/post analyser taps for the graphic EQ display. */
class ParametricEqBlock final : public DSPBlock
{
public:
    static constexpr int kNumBands = 8;
    static constexpr int kParamsPerBand = 6;
    enum BandParam { BandOn, BandType, BandFreq, BandGain, BandQ, BandSlope };
    static constexpr int bandParam (int band, BandParam p) noexcept { return band * kParamsPerBand + (int) p; }
    static constexpr int OutputDb = kNumBands * kParamsPerBand;
    static constexpr int NumParams = OutputDb + 1;

    explicit ParametricEqBlock (StoreId id);

    /** Current band settings straight from the parameters (for the display). */
    EqBand getBand (int band) const noexcept;

    /** The display turns the analyser taps on only while it is visible. */
    void setAnalyserEnabled (bool enabled) noexcept { analyserEnabled.store (enabled, std::memory_order_relaxed); }
    const SampleRing<8192>& getPreAnalyser() const noexcept  { return preTap; }
    const SampleRing<8192>& getPostAnalyser() const noexcept { return postTap; }

protected:
    void prepareBlock (double, int) override;
    void resetBlock() noexcept override;
    void processBlock (float*, float*, int) noexcept override;

private:
    void updateBands (bool snap) noexcept;

    struct BandState
    {
        EqBand target, smoothed;
        std::array<std::array<Biquad, kMaxEqStages>, 2> stages;   // per channel
        int numStages = 0;
    };
    std::array<BandState, kNumBands> bands;
    std::vector<float> monoScratch;
    LinearSmoother output;
    std::atomic<bool> analyserEnabled { false };
    SampleRing<8192> preTap, postTap;
};

//==============================================================================
/** 10-band octave graphic EQ (31 Hz .. 16 kHz), +-12 dB. */
class GraphicEqBlock final : public DSPBlock
{
public:
    static constexpr int kNumBands = 10;
    enum Param { Band0 = 0, LevelDb = kNumBands, NumParams };
    explicit GraphicEqBlock (StoreId id);

protected:
    void prepareBlock (double, int) override;
    void resetBlock() noexcept override;
    void processBlock (float*, float*, int) noexcept override;

private:
    void updateCoefficients (bool snap) noexcept;

    std::array<std::array<Biquad, kNumBands>, 2> bands;
    std::array<float, kNumBands> smoothed {};
    LinearSmoother level;
};

//==============================================================================
/** Level-independent transient designer (attack / sustain), SPL-style. */
class TransientShaperBlock final : public DSPBlock
{
public:
    enum Param { Attack, Sustain, OutputDb, NumParams };
    explicit TransientShaperBlock (StoreId id);

protected:
    void prepareBlock (double, int) override;
    void resetBlock() noexcept override;
    void processBlock (float*, float*, int) noexcept override;

private:
    EnvelopeFollower fast, slow;
    float gainDb = 0.0f, gainCoefficient = 0.0f;
    LinearSmoother output;
};
} // namespace kumamp
