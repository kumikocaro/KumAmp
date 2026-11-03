#pragma once

#include "../../Core/DspPrimitives.h"
#include "../../Core/Smoothing.h"
#include "../../Graph/DSPBlock.h"

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <memory>

namespace kumamp
{
/**
    Tube Screamer style overdrive (TS808 / TS9 circuit model), meant to sit
    right before the amp.

    Clipping stage: non-inverting op-amp whose gain leg is 4.7k + 47nF, so only
    the content above ~720 Hz is amplified (the "mid hump"), with two 1N914
    diodes and 51 pF across the 51k + Drive feedback. The feedback node is
    solved per sample (implicit Euler + Newton on the Shockley diode law) at
    4x oversampling; the op-amp output is the input plus that clipped voltage,
    which is why a Screamer keeps so much of the clean note.

    Tone stage: the passive 1k / 220nF low-pass (723 Hz) blended back towards
    flat by the tone pot, then the output level.

    Modelo: TS808 (original), TS9 (brighter, tighter output) and Mod (100nF
    gain cap: more lows into the clipper, asymmetric 1+2 diodes, more gain).
*/
class OverdriveBlock final : public DSPBlock
{
public:
    enum Param { Drive, Tone, LevelDb, Model, NumParams };
    enum class Variant { TS808, TS9, Mod };

    explicit OverdriveBlock (StoreId id);

    int getLatencySamples() const noexcept override { return latency; }
    std::vector<std::string> getInfoChips() const override;

protected:
    void prepareBlock (double sampleRate, int maxBlockSize) override;
    void resetBlock() noexcept override;
    void processBlock (float* left, float* right, int numSamples) noexcept override;

private:
    struct Clipper
    {
        OnePole gainLeg;   // 4.7k + C: high-pass feeding the diode network
        float v = 0.0f;    // feedback node voltage (state of the 51 pF cap)
        void reset() noexcept { gainLeg.reset(); v = 0.0f; }
    };

    float clip (Clipper& c, float x, float feedbackConductance, float capConductance) noexcept;
    void updateVariant() noexcept;

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    std::array<Clipper, 2> clippers;
    std::array<OnePole, 2> passiveLowPass, dcBlock;
    std::array<OnePole, 2> brightLowPass;   // TS9: extra presence
    LinearSmoother feedbackResistance, toneAmount, level;
    double overSampleRate = 192000.0;
    int latency = 0;
    int currentVariant = -1;
    float forwardScale = 0.0453f, reverseScale = 0.0453f;   // n * Vt per diode string
};
} // namespace kumamp
