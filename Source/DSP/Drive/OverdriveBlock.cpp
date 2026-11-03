#include "OverdriveBlock.h"
#include "../../Core/DspMath.h"

#include <cmath>
#include <iterator>

namespace kumamp
{
namespace
{
    constexpr ParamInfo kParams[] = {
        { "drive", "Drive",    0.0f, 100.0f, 50.0f, "%",  false },
        { "tone",  "Tono",     0.0f, 100.0f, 50.0f, "%",  false },
        { "level", "Nivel",  -24.0f,  12.0f,  0.0f, "dB", false },
        { "model", "Modelo",   0.0f,   2.0f,  0.0f, "TS808|TS9|Mod", true },
    };
    static_assert (std::size (kParams) == OverdriveBlock::NumParams);

    // TS808 values. Diodes: 1N914 (Is 2.52 nA, n 1.752).
    constexpr double kGainLegOhms   = 4700.0;
    constexpr double kFeedbackOhms  = 51000.0;
    constexpr double kFeedbackCap   = 51e-12;
    constexpr double kDiodeIs       = 2.52e-9;
    constexpr float  kDiodeNVt      = 0.04529f;   // 1.752 * 25.85 mV
    constexpr float  kToneLowPassHz = 723.0f;     // 1k / 220 nF
    constexpr float  kOutputTrim    = 0.5f;       // ~unity at noon for a guitar-level input

    /** 500k audio-taper pot: ~10 % of the resistance at half rotation. */
    double audioTaper (float amount) noexcept
    {
        return (std::pow (10.0, 2.0 * (double) std::clamp (amount, 0.0f, 1.0f)) - 1.0) / 99.0;
    }
} // namespace

OverdriveBlock::OverdriveBlock (StoreId id)
    : DSPBlock (id, BlockType::Overdrive, kParams, NumParams) {}

std::vector<std::string> OverdriveBlock::getInfoChips() const
{
    static const char* names[] { "TS808", "TS9", "TS Mod" };
    return { names[std::clamp ((int) std::lround (getParam (Model)), 0, 2)], "Oversampling 4x" };
}

void OverdriveBlock::prepareBlock (double sampleRate, int maxBlockSize)
{
    oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
        2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    oversampling->initProcessing ((size_t) maxBlockSize);
    latency = (int) std::lround (oversampling->getLatencyInSamples());
    overSampleRate = sampleRate * 4.0;

    feedbackResistance.prepare (overSampleRate, 0.03);
    toneAmount.prepare (sampleRate, 0.03);
    level.prepare (sampleRate, 0.02);
    for (auto& f : passiveLowPass) f.setCutoff (sampleRate, kToneLowPassHz);
    for (auto& f : dcBlock)        f.setCutoff (sampleRate, 12.0f);
    for (auto& f : brightLowPass)  f.setCutoff (sampleRate, 2800.0f);
    currentVariant = -1;
    resetBlock();
}

void OverdriveBlock::updateVariant() noexcept
{
    const int variant = std::clamp ((int) std::lround (getParam (Model)), 0, 2);
    if (variant == currentVariant)
        return;
    currentVariant = variant;

    // Mod: 100 nF gain cap (339 Hz instead of 720 Hz) and a 1 + 2 diode string.
    const double gainCap = variant == (int) Variant::Mod ? 100e-9 : 47e-9;
    const auto hz = (float) (1.0 / (2.0 * juce::MathConstants<double>::pi * kGainLegOhms * gainCap));
    for (auto& c : clippers)
        c.gainLeg.setCutoff (overSampleRate, hz);
    forwardScale = kDiodeNVt;
    reverseScale = variant == (int) Variant::Mod ? 2.0f * kDiodeNVt : kDiodeNVt;
}

void OverdriveBlock::resetBlock() noexcept
{
    if (oversampling != nullptr)
        oversampling->reset();
    for (auto& c : clippers) c.reset();
    for (auto& f : passiveLowPass) f.reset();
    for (auto& f : dcBlock) f.reset();
    for (auto& f : brightLowPass) f.reset();
    updateVariant();

    const double maxDrive = currentVariant == (int) Variant::Mod ? 1.0e6 : 5.0e5;
    feedbackResistance.setCurrentAndTarget ((float) (kFeedbackOhms + maxDrive * audioTaper (getParam (Drive) * 0.01f)));
    toneAmount.setCurrentAndTarget (getParam (Tone) * 0.01f);
    level.setCurrentAndTarget (dbToGain (getParam (LevelDb)) * kOutputTrim);
}

float OverdriveBlock::clip (Clipper& c, float x, float feedbackConductance, float capConductance) noexcept
{
    // Feedback node: v/Rf + Cf dv/dt + Id(v) = i, with i the gain-leg current.
    // Implicit Euler -> G v + Is (e^(v/a) - e^(-v/b)) = rhs, solved by damped Newton
    // from the previous sample (a few iterations at 4x).
    const double i   = (double) c.gainLeg.highPass (x) / kGainLegOhms;
    const double g   = (double) feedbackConductance + (double) capConductance;
    const double rhs = i + (double) capConductance * (double) c.v;
    const double a = forwardScale, b = reverseScale;

    double v = c.v;
    for (int iteration = 0; iteration < 8; ++iteration)
    {
        const double ef = std::exp (std::clamp (v / a, -40.0, 40.0));
        const double er = std::exp (std::clamp (-v / b, -40.0, 40.0));
        const double f  = g * v + kDiodeIs * (ef - er) - rhs;
        const double df = g + kDiodeIs * (ef / a + er / b);
        const double step = std::clamp (f / df, -0.1, 0.1);
        v -= step;
        if (std::abs (step) < 1e-7)
            break;
    }
    c.v = (float) v;
    return x + c.v;   // non-inverting stage: Vout = Vin + V(feedback)
}

void OverdriveBlock::processBlock (float* left, float* right, int numSamples) noexcept
{
    updateVariant();
    const double maxDrive = currentVariant == (int) Variant::Mod ? 1.0e6 : 5.0e5;
    feedbackResistance.setTarget ((float) (kFeedbackOhms + maxDrive * audioTaper (getParam (Drive) * 0.01f)));
    toneAmount.setTarget (getParam (Tone) * 0.01f);
    level.setTarget (dbToGain (getParam (LevelDb)) * kOutputTrim);

    float* channels[] { left, right };
    juce::dsp::AudioBlock<float> block (channels, 2, (size_t) numSamples);
    auto up = oversampling->processSamplesUp (block);

    const auto capConductance = (float) (kFeedbackCap * overSampleRate);
    float* upL = up.getChannelPointer (0);
    float* upR = up.getChannelPointer (1);
    for (size_t n = 0; n < up.getNumSamples(); ++n)
    {
        const float conductance = 1.0f / feedbackResistance.next();
        upL[n] = clip (clippers[0], upL[n], conductance, capConductance);
        upR[n] = clip (clippers[1], upR[n], conductance, capConductance);
    }
    oversampling->processSamplesDown (block);

    const bool bright = currentVariant == (int) Variant::TS9;
    for (int i = 0; i < numSamples; ++i)
    {
        const float t = toneAmount.next(), gain = level.next();
        for (int ch = 0; ch < 2; ++ch)
        {
            float& s = channels[ch][i];
            const float dark = passiveLowPass[(size_t) ch].lowPass (s);
            float y = dark + t * t * (s - dark);   // tone pot: 723 Hz roll-off -> flat
            if (bright)
                y += 0.35f * (y - brightLowPass[(size_t) ch].lowPass (y));
            s = dcBlock[(size_t) ch].highPass (y) * gain;
        }
    }
}
} // namespace kumamp
