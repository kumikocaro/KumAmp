#include "PreFxBlocks.h"

#include <cstring>
#include <iterator>

namespace kumamp
{
namespace
{
    constexpr ParamInfo kWahParams[] = {
        { "position",  "Posicion",    0.0f,  100.0f,   50.0f, "%",  false },
        { "resonance", "Resonancia",  1.0f,   15.0f,    5.0f, "",   false },
        { "low",       "Grave",     200.0f,  800.0f,  400.0f, "Hz", false },
        { "high",      "Agudo",    1000.0f, 3500.0f, 2200.0f, "Hz", false },
        { "level",     "Nivel",     -12.0f,   12.0f,    0.0f, "dB", false },
    };
    static_assert (std::size (kWahParams) == WahBlock::NumParams);

    constexpr ParamInfo kAutoWahParams[] = {
        { "sensitivity", "Sensibilidad",  0.0f,  100.0f,   60.0f, "%",  false },
        { "attack",      "Attack",        1.0f,  100.0f,   10.0f, "ms", false },
        { "release",     "Release",      10.0f, 1000.0f,  150.0f, "ms", false },
        { "low",         "Grave",       150.0f,  800.0f,  300.0f, "Hz", false },
        { "high",        "Agudo",      1000.0f, 4000.0f, 2500.0f, "Hz", false },
        { "resonance",   "Resonancia",    1.0f,   15.0f,    6.0f, "",   false },
        { "direction",   "Direccion",     0.0f,    1.0f,    0.0f, "Arriba|Abajo", true },
        { "mix",         "Mezcla",        0.0f,  100.0f,  100.0f, "%",  false },
    };
    static_assert (std::size (kAutoWahParams) == AutoWahBlock::NumParams);

    constexpr ParamInfo kOctParams[] = {
        { "dry",  "Directo",   -60.0f,    6.0f,    0.0f, "dB", false },
        { "sub",  "-1 Octava", -60.0f,    6.0f,   -3.0f, "dB", false },
        { "up",   "+1 Octava", -60.0f,    6.0f,  -60.0f, "dB", false },
        { "tone", "Tono",      300.0f, 12000.0f, 4000.0f, "Hz", false },
    };
    static_assert (std::size (kOctParams) == OctaverBlock::NumParams);

    float sweep (float lowHz, float highHz, float amount) noexcept
    {
        return lowHz * std::pow (highHz / lowHz, std::clamp (amount, 0.0f, 1.0f));
    }
} // namespace

//==============================================================================
WahBlock::WahBlock (StoreId id) : DSPBlock (id, BlockType::Wah, kWahParams, NumParams) {}

void WahBlock::prepareBlock (double sampleRate, int)
{
    positionCoefficient = EnvelopeFollower::coefficient (sampleRate, 15.0f);
    level.prepare (sampleRate, 0.02);
    resetBlock();
}

void WahBlock::resetBlock() noexcept
{
    for (auto& f : filters) f.reset();
    position = getParam (Position) * 0.01f;
    level.setCurrentAndTarget (dbToGain (getParam (LevelDb)));
}

void WahBlock::processBlock (float* left, float* right, int numSamples) noexcept
{
    const double sr = getSampleRate();
    const float target = getParam (Position) * 0.01f;
    const float q = getParam (Resonance);
    const float low = getParam (LowHz), high = getParam (HighHz);
    level.setTarget (dbToGain (getParam (LevelDb)));

    for (int i = 0; i < numSamples; ++i)
    {
        position = target + positionCoefficient * (position - target);
        if ((i & 15) == 0)   // coefficient update every 16 samples is inaudible
            for (auto& f : filters)
                f.set (sr, sweep (low, high, position), q);

        const float g = level.next();
        const auto l = filters[0].process (left[i]);
        const auto r = filters[1].process (right[i]);
        // Band-pass core with a little low-pass body, like the classic inductor wah.
        left[i]  = (l.band * 1.6f + l.low * 0.25f) * g;
        right[i] = (r.band * 1.6f + r.low * 0.25f) * g;
    }
}

//==============================================================================
AutoWahBlock::AutoWahBlock (StoreId id) : DSPBlock (id, BlockType::AutoWah, kAutoWahParams, NumParams) {}

void AutoWahBlock::prepareBlock (double sampleRate, int)
{
    mix.prepare (sampleRate, 0.02);
    resetBlock();
}

void AutoWahBlock::resetBlock() noexcept
{
    for (auto& f : filters) f.reset();
    envelope.reset();
    mix.setCurrentAndTarget (getParam (Mix) * 0.01f);
}

void AutoWahBlock::processBlock (float* left, float* right, int numSamples) noexcept
{
    const double sr = getSampleRate();
    envelope.setTimes (sr, getParam (AttackMs), getParam (ReleaseMs));
    const float sensitivity = getParam (Sensitivity) * 0.01f;
    const bool downward = getParam (Direction) > 0.5f;
    const float q = getParam (Resonance), low = getParam (LowHz), high = getParam (HighHz);
    mix.setTarget (getParam (Mix) * 0.01f);

    for (int i = 0; i < numSamples; ++i)
    {
        const float env = envelope.process (0.5f * (left[i] + right[i]));
        if ((i & 15) == 0)
        {
            // Map the envelope logarithmically: -50 dBFS .. (0 dB - sensitivity * 40 dB) -> 0..1
            const float db = 20.0f * std::log10 (env + 1.0e-6f);
            const float top = -40.0f * sensitivity;
            float amount = std::clamp ((db + 50.0f) / std::max (1.0f, top + 50.0f), 0.0f, 1.0f);
            if (downward) amount = 1.0f - amount;
            for (auto& f : filters)
                f.set (sr, sweep (low, high, amount), q);
        }

        const float m = mix.next();
        const auto l = filters[0].process (left[i]);
        const auto r = filters[1].process (right[i]);
        left[i]  += ((l.band * 1.6f + l.low * 0.25f) - left[i]) * m;
        right[i] += ((r.band * 1.6f + r.low * 0.25f) - right[i]) * m;
    }
}

//==============================================================================
OctaverBlock::OctaverBlock (StoreId id) : DSPBlock (id, BlockType::Octaver, kOctParams, NumParams) {}

void OctaverBlock::prepareBlock (double sampleRate, int maxBlockSize)
{
    for (auto* s : { &down, &up })
    {
        s->prepare (sampleRate);
        s->setGrainMs (25.0f);   // longer grains: better low end
    }
    down.setRatio (0.5f, 0.0f);
    up.setRatio (2.0f, 0.0f);

    for (auto* v : { &downL, &downR, &upL, &upR })
        v->assign ((size_t) maxBlockSize, 0.0f);

    for (auto* s : { &dry, &sub, &octUp })
        s->prepare (sampleRate, 0.03);
    resetBlock();
}

void OctaverBlock::resetBlock() noexcept
{
    down.reset();
    up.reset();
    for (auto& t : tone) t.reset();
    dry.setCurrentAndTarget (dbToGain (getParam (DryDb)));
    sub.setCurrentAndTarget (dbToGain (getParam (SubDb)));
    octUp.setCurrentAndTarget (dbToGain (getParam (UpDb)));
}

void OctaverBlock::processBlock (float* left, float* right, int numSamples) noexcept
{
    dry.setTarget (dbToGain (getParam (DryDb)));
    sub.setTarget (dbToGain (getParam (SubDb)));
    octUp.setTarget (dbToGain (getParam (UpDb)));
    for (auto& t : tone)
        t.setLowPass (getSampleRate(), getParam (ToneHz));

    const auto bytes = sizeof (float) * (size_t) numSamples;
    const bool useSub = sub.isSmoothing() || sub.getTarget() > 0.0f;
    const bool useUp  = octUp.isSmoothing() || octUp.getTarget() > 0.0f;

    if (useSub) down.process (left, right, downL.data(), downR.data(), numSamples);
    else { std::memset (downL.data(), 0, bytes); std::memset (downR.data(), 0, bytes); }

    if (useUp) up.process (left, right, upL.data(), upR.data(), numSamples);
    else { std::memset (upL.data(), 0, bytes); std::memset (upR.data(), 0, bytes); }

    for (int i = 0; i < numSamples; ++i)
    {
        const auto n = (size_t) i;
        const float d = dry.next(), s = sub.next(), u = octUp.next();
        const float wetL = tone[0].process (downL[n] * s + upL[n] * u);
        const float wetR = tone[1].process (downR[n] * s + upR[n] * u);
        left[i]  = left[i] * d + wetL;
        right[i] = right[i] * d + wetR;
    }
}
} // namespace kumamp
