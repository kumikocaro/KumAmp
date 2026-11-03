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
/**
    Noise gate with hysteresis and hold, stereo-linked, smooth gain ramps.

    Clásico: one full-band detector and gain.
    Avanzado (guitar): the hiss a classic gate lets through lives in the highs,
    which stay open as long as the note's fundamental rings. Here the detector
    ignores hum (100 Hz key high-pass) and the signal is split (complementary,
    so it sums back exactly) at "Corte": the lows follow the main gate, while
    the highs also need their own level above threshold + "Agudos" and close
    faster. The note keeps its body while the hiss over the decay is removed.
*/
class NoiseGateBlock final : public DSPBlock
{
public:
    enum Param { ThresholdDb, HysteresisDb, AttackMs, HoldMs, ReleaseMs, RangeDb, Mode, HighDb, SplitHz, NumParams };
    explicit NoiseGateBlock (StoreId id);
    float getGainDb() const noexcept { return meterGainDb.load (std::memory_order_relaxed); }

protected:
    void prepareBlock (double, int) override;
    void resetBlock() noexcept override;
    void processBlock (float*, float*, int) noexcept override;

private:
    void processAdvanced (float* left, float* right, int numSamples) noexcept;

    EnvelopeFollower detector, highDetector;
    float gain = 1.0f, highGain = 1.0f;
    int holdCounter = 0, highHoldCounter = 0;
    bool open = false, highOpen = false;
    std::array<Biquad, 2> keyHighPass, splitLowPass;
    std::array<std::array<Biquad, 2>, 2> highKey;   // [channel][stage]: steep high-pass for the highs detector
    float lastSplitHz = -1.0f;
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

    /** Band listen (FabFilter style): -1 = off, otherwise only the region the band
        acts on is heard (bell / notch / band pass: a band pass at its frequency and Q;
        low shelf / low cut: what lies below it; high shelf / high cut: what lies above).
        Live UI state, not a parameter: it is never saved. Any thread. */
    void setListenBand (int band) noexcept { listenBand.store (band >= 0 && band < kNumBands ? band : -1, std::memory_order_relaxed); }
    int getListenBand() const noexcept { return listenBand.load (std::memory_order_relaxed); }

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
    void updateListenFilter (int band) noexcept;

    std::vector<float> monoScratch, dryLeft, dryRight;
    std::atomic<int> listenBand { -1 };
    int listenFilterBand = -1;
    EqBandType listenType = EqBandType::Bell;
    std::array<std::array<Biquad, 2>, 2> listenFilters;   // [channel][stage]
    LinearSmoother listenMix;
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
