#include "ModulationBlocks.h"

#include "../../Core/Tempo.h"

#include <cstdio>
#include <iterator>
#include <string>

namespace kumamp
{
namespace
{
    constexpr ParamInfo kChorusParams[] = {
        { "rate",   "Rate",      0.05f,  5.0f,  0.8f, "Hz", false },
        { "depth",  "Depth",     0.0f, 100.0f, 50.0f, "%",  false },
        { "delay",  "Delay",     5.0f,  30.0f, 12.0f, "ms", false },
        { "mix",    "Mezcla",    0.0f, 100.0f, 50.0f, "%",  false },
        { "spread", "Estereo",   0.0f, 100.0f, 80.0f, "%",  false },
        { "sync",     "Sync",       0.0f,   1.0f,  0.0f, "Off|On", true },
        { "division", "Division",   0.0f,  15.0f,  3.0f, tempo::kDivisionChoices, true },
    };
    constexpr ParamInfo kFlangerParams[] = {
        { "rate",     "Rate",      0.05f,  5.0f,  0.25f, "Hz", false },
        { "depth",    "Depth",     0.0f, 100.0f, 70.0f,  "%",  false },
        { "manual",   "Manual",    0.3f,   8.0f,  2.5f,  "ms", false },
        { "feedback", "Feedback", -95.0f, 95.0f, 50.0f,  "%",  false },
        { "mix",      "Mezcla",    0.0f, 100.0f, 50.0f,  "%",  false },
        { "spread",   "Estereo",   0.0f, 100.0f, 50.0f,  "%",  false },
        { "sync",     "Sync",       0.0f,   1.0f,  0.0f, "Off|On", true },
        { "division", "Division",   0.0f,  15.0f,  2.0f, tempo::kDivisionChoices, true },
    };
    constexpr ParamInfo kPhaserParams[] = {
        { "rate",     "Rate",      0.05f,   8.0f,   0.4f, "Hz", false },
        { "depth",    "Depth",     0.0f,  100.0f,  70.0f, "%",  false },
        { "stages",   "Etapas",    0.0f,    5.0f,   1.0f, "2|4|6|8|10|12", true },
        { "feedback", "Feedback",  0.0f,   90.0f,  40.0f, "%",  false },
        { "center",   "Centro",  200.0f, 2000.0f, 700.0f, "Hz", false },
        { "mix",      "Mezcla",    0.0f,  100.0f,  50.0f, "%",  false },
        { "spread",   "Estereo",   0.0f,  100.0f,  30.0f, "%",  false },
        { "sync",     "Sync",       0.0f,   1.0f,  0.0f, "Off|On", true },
        { "division", "Division",   0.0f,  15.0f,  2.0f, tempo::kDivisionChoices, true },
    };
    constexpr ParamInfo kTremoloParams[] = {
        { "rate",   "Rate",     0.5f,  15.0f,  5.0f, "Hz", false },
        { "depth",  "Depth",    0.0f, 100.0f, 60.0f, "%",  false },
        { "shape",  "Forma",    0.0f, 100.0f,  0.0f, "%",  false },   // 0 = sine, 100 = square
        { "stereo", "Estereo",  0.0f, 100.0f,  0.0f, "%",  false },   // 100 = auto-pan
        { "sync",     "Sync",       0.0f,   1.0f,  0.0f, "Off|On", true },
        { "division", "Division",   0.0f,  15.0f,  9.0f, tempo::kDivisionChoices, true },
    };
    constexpr ParamInfo kRotaryParams[] = {
        { "speed",   "Velocidad",  0.0f,   1.0f,   0.0f, "Lento|Rapido", true },
        { "inertia", "Inercia",    0.2f,   3.0f,   1.0f, "x",  false },
        { "balance", "Horn/Drum", -1.0f,   1.0f,   0.0f, "",   false },
        { "depth",   "Depth",      0.0f, 100.0f,  70.0f, "%",  false },
        { "mics",    "Micros",     0.0f, 100.0f,  80.0f, "%",  false },
        { "mix",     "Mezcla",     0.0f, 100.0f, 100.0f, "%",  false },
    };
    constexpr ParamInfo kUnivibeParams[] = {
        { "speed",     "Velocidad",  0.5f, 12.0f,  3.0f, "Hz", false },
        { "intensity", "Intensidad", 0.0f, 100.0f, 70.0f, "%", false },
        { "mode",      "Modo",       0.0f,  1.0f,  0.0f, "Chorus|Vibrato", true },
        { "level",     "Nivel",    -12.0f, 12.0f,  0.0f, "dB", false },
        { "sync",     "Sync",       0.0f,   1.0f,  0.0f, "Off|On", true },
        { "division", "Division",   0.0f,  15.0f,  6.0f, tempo::kDivisionChoices, true },
    };
    constexpr ParamInfo kCrusherParams[] = {
        { "bits",       "Bits",       1.0f,  16.0f,   8.0f, "",  true  },
        { "downsample", "Downsample", 1.0f,  64.0f,   1.0f, "x", false },
        { "mix",        "Mezcla",     0.0f, 100.0f, 100.0f, "%", false },
        { "level",      "Nivel",    -24.0f,  12.0f,   0.0f, "dB", false },
    };

    static_assert (std::size (kChorusParams)  == ChorusBlock::NumParams);
    static_assert (std::size (kFlangerParams) == FlangerBlock::NumParams);
    static_assert (std::size (kPhaserParams)  == PhaserBlock::NumParams);
    static_assert (std::size (kTremoloParams) == TremoloBlock::NumParams);
    static_assert (std::size (kRotaryParams)  == RotaryBlock::NumParams);
    static_assert (std::size (kUnivibeParams) == UnivibeBlock::NumParams);
    static_assert (std::size (kCrusherParams) == BitcrusherBlock::NumParams);

    std::string syncedText (float division, float hz)
    {
        char buffer[48];
        std::snprintf (buffer, sizeof (buffer), "%s = %.2f Hz", tempo::kNames[(size_t) tempo::clampDivision (division)], (double) hz);
        return buffer;
    }

    /** First-order allpass coefficient for a corner frequency. */
    inline float allpassCoefficient (double sampleRate, float hz) noexcept
    {
        const float t = std::tan (kPi * std::clamp (hz, 20.0f, 0.45f * (float) sampleRate) / (float) sampleRate);
        return (t - 1.0f) / (t + 1.0f);
    }

    inline float allpass (float x, float a, float& x1, float& y1) noexcept
    {
        const float y = a * x + x1 - a * y1;
        x1 = x;
        y1 = y;
        return y;
    }
} // namespace

//==============================================================================
ChorusBlock::ChorusBlock (StoreId id) : DSPBlock (id, BlockType::Chorus, kChorusParams, NumParams) {}

void ChorusBlock::prepareBlock (double sampleRate, int)
{
    for (auto& l : lines) l.prepare ((int) (sampleRate * 0.050));
    lfo.prepare (sampleRate);
    mix.prepare (sampleRate, 0.02);
    resetBlock();
}

void ChorusBlock::resetBlock() noexcept
{
    for (auto& l : lines) l.reset();
    lfo.reset();
    mix.setCurrentAndTarget (getParam (Mix) * 0.01f);
}

void ChorusBlock::processBlock (float* left, float* right, int numSamples) noexcept
{
    const auto sr = (float) getSampleRate();
    lfo.setFrequency (effectiveRateHz());
    const float base = getParam (DelayMs) * 0.001f * sr;
    const float swing = getParam (Depth) * 0.01f * 0.005f * sr;   // up to +-5 ms
    const double offset = 0.25 * getParam (Spread) * 0.01;       // up to 90 degrees
    mix.setTarget (getParam (Mix) * 0.01f);

    for (int i = 0; i < numSamples; ++i)
    {
        const double p = lfo.advance();
        double pr = p + offset; if (pr >= 1.0) pr -= 1.0;
        lines[0].push (left[i]);
        lines[1].push (right[i]);
        const float wl = lines[0].read (base + swing * Lfo::sine (p));
        const float wr = lines[1].read (base + swing * Lfo::sine (pr));
        const float m = mix.next();
        left[i]  += (wl - left[i]) * m;
        right[i] += (wr - right[i]) * m;
    }
}

//==============================================================================
FlangerBlock::FlangerBlock (StoreId id) : DSPBlock (id, BlockType::Flanger, kFlangerParams, NumParams) {}

void FlangerBlock::prepareBlock (double sampleRate, int)
{
    for (auto& l : lines) l.prepare ((int) (sampleRate * 0.025));
    lfo.prepare (sampleRate);
    mix.prepare (sampleRate, 0.02);
    resetBlock();
}

void FlangerBlock::resetBlock() noexcept
{
    for (auto& l : lines) l.reset();
    lfo.reset();
    mix.setCurrentAndTarget (getParam (Mix) * 0.01f);
}

void FlangerBlock::processBlock (float* left, float* right, int numSamples) noexcept
{
    const auto sr = (float) getSampleRate();
    lfo.setFrequency (effectiveRateHz());
    const float manual = getParam (ManualMs) * 0.001f * sr;
    const float depth = getParam (Depth) * 0.01f;
    const float feedback = getParam (Feedback) * 0.01f;
    const double offset = 0.5 * getParam (Spread) * 0.01;
    mix.setTarget (getParam (Mix) * 0.01f);

    float* channels[] { left, right };
    for (int i = 0; i < numSamples; ++i)
    {
        const double p = lfo.advance();
        const float m = mix.next();
        for (int c = 0; c < 2; ++c)
        {
            double pc = p + (c == 1 ? offset : 0.0); if (pc >= 1.0) pc -= 1.0;
            // Triangle sweep from ~0.05x to 1x the manual delay (classic jet sweep).
            const float sweepAmount = 0.5f + 0.5f * Lfo::triangle (pc);
            const float d = std::max (1.0f, manual * (1.0f - depth * 0.95f * sweepAmount));
            auto& line = lines[(size_t) c];
            const float delayed = line.read (d);
            const float x = channels[c][i];
            line.push (softClip (x + feedback * delayed));
            channels[c][i] = x + (delayed - x) * m;   // 50 % = deepest comb
        }
    }
}

//==============================================================================
PhaserBlock::PhaserBlock (StoreId id) : DSPBlock (id, BlockType::Phaser, kPhaserParams, NumParams) {}

void PhaserBlock::prepareBlock (double sampleRate, int)
{
    lfo.prepare (sampleRate);
    mix.prepare (sampleRate, 0.02);
    resetBlock();
}

void PhaserBlock::resetBlock() noexcept
{
    channels = {};
    lfo.reset();
    mix.setCurrentAndTarget (getParam (Mix) * 0.01f);
}

void PhaserBlock::processBlock (float* left, float* right, int numSamples) noexcept
{
    const double sr = getSampleRate();
    lfo.setFrequency (effectiveRateHz());
    const float depth = getParam (Depth) * 0.01f;
    const int stages = 2 * (1 + (int) std::lround (getParam (Stages)));
    const float feedback = getParam (Feedback) * 0.01f;
    const float centre = getParam (CenterHz);
    const double offset = 0.5 * getParam (Spread) * 0.01;
    mix.setTarget (getParam (Mix) * 0.01f);

    float* io[] { left, right };
    float coefficient[2] {};
    for (int i = 0; i < numSamples; ++i)
    {
        const double p = lfo.advance();
        if ((i & 7) == 0)
            for (int c = 0; c < 2; ++c)
            {
                double pc = p + (c == 1 ? offset : 0.0); if (pc >= 1.0) pc -= 1.0;
                const float hz = centre * std::exp2 (2.0f * depth * Lfo::sine (pc));   // +-2 octaves
                coefficient[c] = allpassCoefficient (sr, hz);
            }

        const float m = mix.next();
        for (int c = 0; c < 2; ++c)
        {
            auto& ch = channels[(size_t) c];
            const float x = io[c][i];
            float y = x + feedback * ch.last;
            for (int s = 0; s < stages; ++s)
                y = allpass (y, coefficient[c], ch.x1[(size_t) s], ch.y1[(size_t) s]);
            ch.last = y;
            io[c][i] = x + (y - x) * m;   // mix 50 % = deepest notches
        }
    }
}

//==============================================================================
TremoloBlock::TremoloBlock (StoreId id) : DSPBlock (id, BlockType::Tremolo, kTremoloParams, NumParams) {}

void TremoloBlock::prepareBlock (double sampleRate, int)
{
    lfo.prepare (sampleRate);
    depth.prepare (sampleRate, 0.02);
    resetBlock();
}

void TremoloBlock::resetBlock() noexcept
{
    lfo.reset();
    depth.setCurrentAndTarget (getParam (Depth) * 0.01f);
}

void TremoloBlock::processBlock (float* left, float* right, int numSamples) noexcept
{
    lfo.setFrequency (effectiveRateHz());
    depth.setTarget (getParam (Depth) * 0.01f);
    const float hardness = 1.0f + getParam (Shape) * 0.12f;    // tanh drive: sine -> square
    const float norm = 1.0f / std::tanh (hardness);
    const double offset = 0.5 * getParam (Stereo) * 0.01;

    for (int i = 0; i < numSamples; ++i)
    {
        const double p = lfo.advance();
        double pr = p + offset; if (pr >= 1.0) pr -= 1.0;
        const float d = depth.next();
        const float wl = std::tanh (hardness * Lfo::sine (p)) * norm;
        const float wr = std::tanh (hardness * Lfo::sine (pr)) * norm;
        left[i]  *= 1.0f - d * (0.5f - 0.5f * wl);
        right[i] *= 1.0f - d * (0.5f - 0.5f * wr);
    }
}

//==============================================================================
RotaryBlock::RotaryBlock (StoreId id) : DSPBlock (id, BlockType::Rotary, kRotaryParams, NumParams) {}

void RotaryBlock::prepareBlock (double sampleRate, int)
{
    crossLow.setLowPass (sampleRate, 800.0f);
    crossHigh.setHighPass (sampleRate, 800.0f);
    for (auto& l : hornLines) l.prepare ((int) (sampleRate * 0.010));
    for (auto& l : drumLines) l.prepare ((int) (sampleRate * 0.010));
    mix.prepare (sampleRate, 0.02);
    resetBlock();
}

void RotaryBlock::resetBlock() noexcept
{
    crossLow.reset();
    crossHigh.reset();
    for (auto& l : hornLines) l.reset();
    for (auto& l : drumLines) l.reset();
    const bool fast = getParam (Speed) > 0.5f;
    hornSpeed = fast ? 6.8f : 0.8f;
    drumSpeed = fast ? 5.9f : 0.7f;
    mix.setCurrentAndTarget (getParam (Mix) * 0.01f);
}

void RotaryBlock::processBlock (float* left, float* right, int numSamples) noexcept
{
    const auto sr = (float) getSampleRate();
    const bool fast = getParam (Speed) > 0.5f;
    const float hornTarget = fast ? 6.8f : 0.8f, drumTarget = fast ? 5.9f : 0.7f;
    const float inertia = getParam (Inertia);
    // The light horn reaches speed in ~0.7 s, the heavy drum in ~4 s (scaled by Inertia).
    const float hornC = EnvelopeFollower::coefficient (sr, 700.0f * inertia);
    const float drumC = EnvelopeFollower::coefficient (sr, 4000.0f * inertia);
    const float balance = getParam (Balance);
    const float hornGain = std::min (1.0f, 1.0f + balance), drumGain = std::min (1.0f, 1.0f - balance);
    const float depth = getParam (Depth) * 0.01f;
    const double micOffset = 0.25 * getParam (MicSpread) * 0.01;   // up to 90 degrees apart
    const float hornSwing = 0.0012f * sr * depth, drumSwing = 0.0004f * sr * depth;
    const float base = 0.003f * sr;
    mix.setTarget (getParam (Mix) * 0.01f);

    for (int i = 0; i < numSamples; ++i)
    {
        hornSpeed = hornTarget + hornC * (hornSpeed - hornTarget);
        drumSpeed = drumTarget + drumC * (drumSpeed - drumTarget);
        hornPhase += hornSpeed / sr; if (hornPhase >= 1.0) hornPhase -= 1.0;
        drumPhase += drumSpeed / sr; if (drumPhase >= 1.0) drumPhase -= 1.0;

        const float mono = 0.5f * (left[i] + right[i]);
        const float low = crossLow.process (mono), high = crossHigh.process (mono);
        for (auto& l : hornLines) l.push (high);
        for (auto& l : drumLines) l.push (low);

        float out[2];
        for (int c = 0; c < 2; ++c)
        {
            const double off = c == 0 ? -micOffset : micOffset;
            double hp = hornPhase + off; hp -= std::floor (hp);
            double dp = drumPhase + off; dp -= std::floor (dp);
            const float hs = Lfo::sine (hp), ds = Lfo::sine (dp);
            const float hc = std::cos (2.0f * kPi * (float) hp), dc = std::cos (2.0f * kPi * (float) dp);
            // Doppler (delay follows the horn's radial velocity) + amplitude as it faces the mic.
            const float horn = hornLines[(size_t) c].read (base + hornSwing * hs) * (1.0f - 0.45f * depth * (0.5f - 0.5f * hc));
            const float drum = drumLines[(size_t) c].read (base + drumSwing * ds) * (1.0f - 0.25f * depth * (0.5f - 0.5f * dc));
            out[c] = horn * hornGain + drum * drumGain;
        }

        const float m = mix.next();
        left[i]  += (out[0] - left[i]) * m;
        right[i] += (out[1] - right[i]) * m;
    }
}

//==============================================================================
UnivibeBlock::UnivibeBlock (StoreId id) : DSPBlock (id, BlockType::Univibe, kUnivibeParams, NumParams) {}

void UnivibeBlock::prepareBlock (double sampleRate, int)
{
    lfo.prepare (sampleRate);
    lampCoefficient = EnvelopeFollower::coefficient (sampleRate, 8.0f);   // incandescent lamp lag
    wet.prepare (sampleRate, 0.03);
    level.prepare (sampleRate, 0.02);
    resetBlock();
}

void UnivibeBlock::resetBlock() noexcept
{
    channels = {};
    lfo.reset();
    lamp = 0.0f;
    wet.setCurrentAndTarget (getParam (Mode) > 0.5f ? 1.0f : 0.5f);
    level.setCurrentAndTarget (dbToGain (getParam (LevelDb)));
}

void UnivibeBlock::processBlock (float* left, float* right, int numSamples) noexcept
{
    const double sr = getSampleRate();
    lfo.setFrequency (effectiveRateHz());
    const float intensity = getParam (Intensity) * 0.01f;
    wet.setTarget (getParam (Mode) > 0.5f ? 1.0f : 0.5f);
    level.setTarget (dbToGain (getParam (LevelDb)));

    // Staggered stage corners (the original's mismatched capacitors).
    static constexpr float kStageHz[4] = { 90.0f, 330.0f, 1100.0f, 3400.0f };
    float coefficients[4] {};
    float* io[] { left, right };

    for (int i = 0; i < numSamples; ++i)
    {
        // Skewed lamp drive: bright pulses, slow decay.
        const float drive = std::pow (0.5f + 0.5f * Lfo::sine (lfo.advance()), 1.8f);
        lamp = drive + lampCoefficient * (lamp - drive);

        if ((i & 7) == 0)
            for (int s = 0; s < 4; ++s)
                coefficients[s] = allpassCoefficient (sr, kStageHz[s] * std::exp2 (2.2f * intensity * (lamp - 0.4f)));

        const float w = wet.next(), g = level.next();
        for (int c = 0; c < 2; ++c)
        {
            auto& ch = channels[(size_t) c];
            const float x = io[c][i];
            float y = x;
            for (int s = 0; s < 4; ++s)
                y = allpass (y, coefficients[s], ch.x1[(size_t) s], ch.y1[(size_t) s]);
            io[c][i] = (x + (y - x) * w) * g;
        }
    }
}

//==============================================================================
BitcrusherBlock::BitcrusherBlock (StoreId id) : DSPBlock (id, BlockType::Bitcrusher, kCrusherParams, NumParams) {}

void BitcrusherBlock::prepareBlock (double sampleRate, int)
{
    mix.prepare (sampleRate, 0.02);
    level.prepare (sampleRate, 0.02);
    resetBlock();
}

void BitcrusherBlock::resetBlock() noexcept
{
    heldL = heldR = holdPhase = 0.0f;
    mix.setCurrentAndTarget (getParam (Mix) * 0.01f);
    level.setCurrentAndTarget (dbToGain (getParam (LevelDb)));
}

void BitcrusherBlock::processBlock (float* left, float* right, int numSamples) noexcept
{
    const float steps = std::exp2 (std::round (getParam (Bits)) - 1.0f);
    const float hold = std::max (1.0f, getParam (Downsample));
    mix.setTarget (getParam (Mix) * 0.01f);
    level.setTarget (dbToGain (getParam (LevelDb)));

    for (int i = 0; i < numSamples; ++i)
    {
        holdPhase += 1.0f;
        if (holdPhase >= hold)
        {
            holdPhase -= hold;
            heldL = std::round (std::clamp (left[i], -1.0f, 1.0f) * steps) / steps;
            heldR = std::round (std::clamp (right[i], -1.0f, 1.0f) * steps) / steps;
        }
        const float m = mix.next(), g = level.next();
        left[i]  = (left[i]  + (heldL - left[i])  * m) * g;
        right[i] = (right[i] + (heldR - right[i]) * m) * g;
    }
}

float ChorusBlock::effectiveRateHz() const noexcept
{
    return getParam (Sync) > 0.5f ? (float) tempo::divisionHz (getParam (Division), getTempoBpm()) : getParam (RateHz);
}

std::string ChorusBlock::getDisplayOverride (int param) const
{
    return param == RateHz && getParam (Sync) > 0.5f ? syncedText (getParam (Division), effectiveRateHz()) : std::string {};
}

float FlangerBlock::effectiveRateHz() const noexcept
{
    return getParam (Sync) > 0.5f ? (float) tempo::divisionHz (getParam (Division), getTempoBpm()) : getParam (RateHz);
}

std::string FlangerBlock::getDisplayOverride (int param) const
{
    return param == RateHz && getParam (Sync) > 0.5f ? syncedText (getParam (Division), effectiveRateHz()) : std::string {};
}

float PhaserBlock::effectiveRateHz() const noexcept
{
    return getParam (Sync) > 0.5f ? (float) tempo::divisionHz (getParam (Division), getTempoBpm()) : getParam (RateHz);
}

std::string PhaserBlock::getDisplayOverride (int param) const
{
    return param == RateHz && getParam (Sync) > 0.5f ? syncedText (getParam (Division), effectiveRateHz()) : std::string {};
}

float TremoloBlock::effectiveRateHz() const noexcept
{
    return getParam (Sync) > 0.5f ? (float) tempo::divisionHz (getParam (Division), getTempoBpm()) : getParam (RateHz);
}

std::string TremoloBlock::getDisplayOverride (int param) const
{
    return param == RateHz && getParam (Sync) > 0.5f ? syncedText (getParam (Division), effectiveRateHz()) : std::string {};
}

float UnivibeBlock::effectiveRateHz() const noexcept
{
    return getParam (Sync) > 0.5f ? (float) tempo::divisionHz (getParam (Division), getTempoBpm()) : getParam (SpeedHz);
}

std::string UnivibeBlock::getDisplayOverride (int param) const
{
    return param == SpeedHz && getParam (Sync) > 0.5f ? syncedText (getParam (Division), effectiveRateHz()) : std::string {};
}
} // namespace kumamp
