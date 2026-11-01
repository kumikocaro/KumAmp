#include "DynamicsBlocks.h"

#include <iterator>

namespace kumamp
{
namespace
{
    inline float toDb (float linear) noexcept { return 20.0f * std::log10 (std::max (linear, 1.0e-9f)); }
    /** Exact conversion for thresholds (dbToGain() floors at -60 dB for faders). */
    inline float toLinear (float db) noexcept { return std::pow (10.0f, db * 0.05f); }

    /** Per-block one-pole smoothing of a parameter value (for coefficient-based blocks). */
    inline bool approach (float& state, float target, bool snap) noexcept
    {
        if (snap || std::abs (target - state) < 1.0e-4f * std::max (1.0f, std::abs (target)))
        {
            const bool changed = ! exactlyEqual (state, target);
            state = target;
            return changed;
        }
        state += 0.35f * (target - state);
        return true;
    }

    // ---- Noise gate -----------------------------------------------------------
    constexpr ParamInfo kGateParams[] = {
        { "threshold",  "Threshold",  -90.0f,    0.0f, -65.0f, "dB", false },
        { "hysteresis", "Histeresis",   0.0f,   20.0f,   6.0f, "dB", false },
        { "attack",     "Attack",       0.1f,   20.0f,   0.5f, "ms", false },
        { "hold",       "Hold",         0.0f,  500.0f,  40.0f, "ms", false },
        { "release",    "Release",      5.0f, 2000.0f, 150.0f, "ms", false },
        { "range",      "Rango",      -90.0f,    0.0f, -90.0f, "dB", false },
    };
    static_assert (std::size (kGateParams) == NoiseGateBlock::NumParams);

    // ---- Compressor -------------------------------------------------------------
    constexpr ParamInfo kCompParams[] = {
        { "threshold", "Threshold", -60.0f,    0.0f, -20.0f, "dB", false },
        { "ratio",     "Ratio",       1.0f,   20.0f,   4.0f, ":1", false },
        { "attack",    "Attack",      0.1f,  100.0f,  10.0f, "ms", false },
        { "release",   "Release",    10.0f, 1000.0f, 120.0f, "ms", false },
        { "knee",      "Knee",        0.0f,   12.0f,   6.0f, "dB", false },
        { "makeup",    "Makeup",      0.0f,   24.0f,   0.0f, "dB", false },
        { "mix",       "Mezcla",      0.0f,  100.0f, 100.0f, "%",  false },
    };
    static_assert (std::size (kCompParams) == CompressorBlock::NumParams);

    // ---- Parametric EQ ------------------------------------------------------------
    #define KUMAMP_EQ_BAND(n, type, freq, q) \
        { "b" #n "_on",    "Banda " #n,     0.0f,     1.0f,  0.0f, "Off|On", true }, \
        { "b" #n "_type",  "Tipo " #n,      0.0f,     6.0f,  type, "Bell|Low Shelf|High Shelf|Low Cut|High Cut|Notch|Band Pass", true }, \
        { "b" #n "_freq",  "Freq " #n,     20.0f, 20000.0f,  freq, "Hz", false }, \
        { "b" #n "_gain",  "Gain " #n,    -24.0f,    24.0f,  0.0f, "dB", false }, \
        { "b" #n "_q",     "Q " #n,         0.1f,    18.0f,  q,    "",   false }, \
        { "b" #n "_slope", "Pendiente " #n, 0.0f,     3.0f,  1.0f, "12 dB/oct|24 dB/oct|36 dB/oct|48 dB/oct", true },

    constexpr ParamInfo kPeqParams[] = {
        KUMAMP_EQ_BAND (1, 3.0f,    80.0f, 0.7071f)
        KUMAMP_EQ_BAND (2, 1.0f,   150.0f, 0.7071f)
        KUMAMP_EQ_BAND (3, 0.0f,   400.0f, 1.0f)
        KUMAMP_EQ_BAND (4, 0.0f,  1000.0f, 1.0f)
        KUMAMP_EQ_BAND (5, 0.0f,  2500.0f, 1.0f)
        KUMAMP_EQ_BAND (6, 0.0f,  5000.0f, 1.0f)
        KUMAMP_EQ_BAND (7, 2.0f,  8000.0f, 0.7071f)
        KUMAMP_EQ_BAND (8, 4.0f, 12000.0f, 0.7071f)
        { "output", "Output", -24.0f, 24.0f, 0.0f, "dB", false },
    };
    #undef KUMAMP_EQ_BAND
    static_assert (std::size (kPeqParams) == ParametricEqBlock::NumParams);

    // ---- Graphic EQ ---------------------------------------------------------------
    constexpr float kGeqFrequencies[GraphicEqBlock::kNumBands] = { 31.25f, 62.5f, 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f };
    constexpr ParamInfo kGeqParams[] = {
        { "b31",   "31",   -12.0f, 12.0f, 0.0f, "dB", false },
        { "b62",   "62",   -12.0f, 12.0f, 0.0f, "dB", false },
        { "b125",  "125",  -12.0f, 12.0f, 0.0f, "dB", false },
        { "b250",  "250",  -12.0f, 12.0f, 0.0f, "dB", false },
        { "b500",  "500",  -12.0f, 12.0f, 0.0f, "dB", false },
        { "b1k",   "1k",   -12.0f, 12.0f, 0.0f, "dB", false },
        { "b2k",   "2k",   -12.0f, 12.0f, 0.0f, "dB", false },
        { "b4k",   "4k",   -12.0f, 12.0f, 0.0f, "dB", false },
        { "b8k",   "8k",   -12.0f, 12.0f, 0.0f, "dB", false },
        { "b16k",  "16k",  -12.0f, 12.0f, 0.0f, "dB", false },
        { "level", "Nivel", -18.0f, 18.0f, 0.0f, "dB", false },
    };
    static_assert (std::size (kGeqParams) == GraphicEqBlock::NumParams);

    // ---- Transient shaper ---------------------------------------------------------
    constexpr ParamInfo kTsParams[] = {
        { "attack",  "Attack",  -100.0f, 100.0f, 0.0f, "%",  false },
        { "sustain", "Sustain", -100.0f, 100.0f, 0.0f, "%",  false },
        { "output",  "Output",   -18.0f,  18.0f, 0.0f, "dB", false },
    };
    static_assert (std::size (kTsParams) == TransientShaperBlock::NumParams);
} // namespace

//==============================================================================
NoiseGateBlock::NoiseGateBlock (StoreId id) : DSPBlock (id, BlockType::NoiseGate, kGateParams, NumParams) {}

void NoiseGateBlock::prepareBlock (double sampleRate, int)
{
    detector.setTimes (sampleRate, 0.1f, 20.0f);
    resetBlock();
}

void NoiseGateBlock::resetBlock() noexcept
{
    detector.reset();
    gain = dbToGain (getParam (RangeDb));
    open = false;
    holdCounter = 0;
}

void NoiseGateBlock::processBlock (float* left, float* right, int numSamples) noexcept
{
    const double sr = getSampleRate();
    const float openLevel  = toLinear (getParam (ThresholdDb));
    const float closeLevel = toLinear (getParam (ThresholdDb) - getParam (HysteresisDb));
    const float floorGain  = dbToGain (getParam (RangeDb));
    const int holdSamples  = (int) (getParam (HoldMs) * 0.001f * (float) sr);
    const float attackC    = EnvelopeFollower::coefficient (sr, getParam (AttackMs));
    const float releaseC   = EnvelopeFollower::coefficient (sr, getParam (ReleaseMs));

    for (int i = 0; i < numSamples; ++i)
    {
        const float level = detector.process (std::max (std::abs (left[i]), std::abs (right[i])));

        if (level >= openLevel)
        {
            open = true;
            holdCounter = holdSamples;
        }
        else if (open && level < closeLevel)
        {
            if (holdCounter > 0) --holdCounter;
            else open = false;
        }

        const float target = open ? 1.0f : floorGain;
        const float c = target > gain ? attackC : releaseC;
        gain = target + c * (gain - target);

        left[i]  *= gain;
        right[i] *= gain;
    }
    meterGainDb.store (toDb (gain), std::memory_order_relaxed);
}

//==============================================================================
CompressorBlock::CompressorBlock (StoreId id) : DSPBlock (id, BlockType::Compressor, kCompParams, NumParams) {}

void CompressorBlock::prepareBlock (double sampleRate, int)
{
    makeup.prepare (sampleRate, 0.02);
    mix.prepare (sampleRate, 0.02);
    resetBlock();
}

void CompressorBlock::resetBlock() noexcept
{
    reductionDb = heldDb = 0.0f;
    makeup.setCurrentAndTarget (dbToGain (getParam (MakeupDb)));
    mix.setCurrentAndTarget (getParam (Mix) * 0.01f);
}

void CompressorBlock::processBlock (float* left, float* right, int numSamples) noexcept
{
    const double sr = getSampleRate();
    const float threshold = getParam (ThresholdDb);
    const float slope = 1.0f - 1.0f / getParam (Ratio);
    const float knee = getParam (KneeDb);
    const float attackC = EnvelopeFollower::coefficient (sr, getParam (AttackMs));
    const float releaseC = EnvelopeFollower::coefficient (sr, getParam (ReleaseMs));
    makeup.setTarget (dbToGain (getParam (MakeupDb)));
    mix.setTarget (getParam (Mix) * 0.01f);

    float maxReduction = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        const float levelDb = toDb (std::max (std::abs (left[i]), std::abs (right[i])));
        const float over = levelDb - threshold;

        float targetDb = 0.0f;   // gain reduction (positive dB)
        if (knee > 0.0f && std::abs (over) <= 0.5f * knee)
            targetDb = slope * (over + 0.5f * knee) * (over + 0.5f * knee) / (2.0f * knee);
        else if (over > 0.0f)
            targetDb = slope * over;

        // Decoupled smooth peak detector: release-held peak, then attack smoothing.
        heldDb = std::max (targetDb, releaseC * heldDb + (1.0f - releaseC) * targetDb);
        reductionDb = attackC * reductionDb + (1.0f - attackC) * heldDb;
        maxReduction = std::max (maxReduction, reductionDb);

        const float g = dbToGain (-reductionDb) * makeup.next();
        const float m = mix.next();
        left[i]  = left[i]  * (1.0f - m) + left[i]  * g * m;
        right[i] = right[i] * (1.0f - m) + right[i] * g * m;
    }
    meterReductionDb.store (maxReduction, std::memory_order_relaxed);
}

//==============================================================================
ParametricEqBlock::ParametricEqBlock (StoreId id) : DSPBlock (id, BlockType::ParametricEq, kPeqParams, NumParams) {}

EqBand ParametricEqBlock::getBand (int b) const noexcept
{
    EqBand band;
    band.on        = getParam (bandParam (b, BandOn)) > 0.5f;
    band.type      = (EqBandType) std::clamp ((int) std::lround (getParam (bandParam (b, BandType))), 0, (int) EqBandType::NumTypes - 1);
    band.frequency = getParam (bandParam (b, BandFreq));
    band.gainDb    = getParam (bandParam (b, BandGain));
    band.q         = getParam (bandParam (b, BandQ));
    band.slope     = (int) std::lround (getParam (bandParam (b, BandSlope)));
    return band;
}

void ParametricEqBlock::prepareBlock (double sampleRate, int maxBlockSize)
{
    monoScratch.assign ((size_t) maxBlockSize, 0.0f);
    output.prepare (sampleRate, 0.02);
    resetBlock();
}

void ParametricEqBlock::resetBlock() noexcept
{
    for (auto& band : bands)
        for (auto& ch : band.stages)
            for (auto& f : ch)
                f.reset();
    updateBands (true);
    output.setCurrentAndTarget (dbToGain (getParam (OutputDb)));
}

void ParametricEqBlock::updateBands (bool snap) noexcept
{
    const double sr = getSampleRate();
    for (int b = 0; b < kNumBands; ++b)
    {
        auto& state = bands[(size_t) b];
        const EqBand target = getBand (b);
        auto& s = state.smoothed;

        // Discrete changes apply at once; a band switched on ramps its gain up from 0 dB.
        bool changed = snap || target.on != s.on || target.type != s.type || target.slope != s.slope;
        if (target.on && ! s.on && ! snap)
            s.gainDb = 0.0f;
        s.on = target.on;
        s.type = target.type;
        s.slope = target.slope;
        changed |= approach (s.frequency, target.frequency, snap);
        changed |= approach (s.gainDb, target.gainDb, snap);
        changed |= approach (s.q, target.q, snap);

        if (! changed)
            continue;

        // Designing only rewrites coefficients, so filter states survive (no clicks).
        state.numStages = designEqBand (s, sr, state.stages[0]);
        for (int k = 0; k < kMaxEqStages; ++k)
            state.stages[1][(size_t) k].copyCoefficientsFrom (state.stages[0][(size_t) k]);
    }
}

void ParametricEqBlock::processBlock (float* left, float* right, int numSamples) noexcept
{
    const bool analyse = analyserEnabled.load (std::memory_order_relaxed);
    if (analyse)
    {
        for (int i = 0; i < numSamples; ++i)
            monoScratch[(size_t) i] = 0.5f * (left[i] + right[i]);
        preTap.push (monoScratch.data(), numSamples);
    }

    updateBands (false);
    output.setTarget (dbToGain (getParam (OutputDb)));

    float* channels[] { left, right };
    for (auto& band : bands)
        for (int s = 0; s < band.numStages; ++s)
            for (int c = 0; c < 2; ++c)
            {
                auto& f = band.stages[(size_t) c][(size_t) s];
                float* x = channels[c];
                for (int i = 0; i < numSamples; ++i)
                    x[i] = f.process (x[i]);
            }

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = output.next();
        left[i] *= g;
        right[i] *= g;
    }

    if (analyse)
    {
        for (int i = 0; i < numSamples; ++i)
            monoScratch[(size_t) i] = 0.5f * (left[i] + right[i]);
        postTap.push (monoScratch.data(), numSamples);
    }
}

//==============================================================================
GraphicEqBlock::GraphicEqBlock (StoreId id) : DSPBlock (id, BlockType::GraphicEq, kGeqParams, NumParams) {}

void GraphicEqBlock::prepareBlock (double sampleRate, int)
{
    level.prepare (sampleRate, 0.02);
    resetBlock();
}

void GraphicEqBlock::resetBlock() noexcept
{
    for (auto& ch : bands)
        for (auto& f : ch)
            f.reset();
    updateCoefficients (true);
    level.setCurrentAndTarget (dbToGain (getParam (LevelDb)));
}

void GraphicEqBlock::updateCoefficients (bool snap) noexcept
{
    const double sr = getSampleRate();
    for (int b = 0; b < kNumBands; ++b)
        if (approach (smoothed[(size_t) b], getParam (Band0 + b), snap) || snap)
            for (auto& ch : bands)
                ch[(size_t) b].setPeak (sr, std::min (kGeqFrequencies[b], 0.45f * (float) sr), 1.41f, smoothed[(size_t) b]);
}

void GraphicEqBlock::processBlock (float* left, float* right, int numSamples) noexcept
{
    updateCoefficients (false);
    level.setTarget (dbToGain (getParam (LevelDb)));

    float* channels[] { left, right };
    for (int c = 0; c < 2; ++c)
    {
        auto& ch = bands[(size_t) c];
        float* x = channels[c];
        for (int b = 0; b < kNumBands; ++b)
        {
            if (std::abs (smoothed[(size_t) b]) < 0.01f)
                continue;   // flat band: skip
            auto& f = ch[(size_t) b];
            for (int i = 0; i < numSamples; ++i)
                x[i] = f.process (x[i]);
        }
    }

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = level.next();
        left[i] *= g;
        right[i] *= g;
    }
}

//==============================================================================
TransientShaperBlock::TransientShaperBlock (StoreId id) : DSPBlock (id, BlockType::TransientShaper, kTsParams, NumParams) {}

void TransientShaperBlock::prepareBlock (double sampleRate, int)
{
    fast.setTimes (sampleRate, 0.5f, 25.0f);
    slow.setTimes (sampleRate, 25.0f, 250.0f);
    gainCoefficient = EnvelopeFollower::coefficient (sampleRate, 2.0f);
    output.prepare (sampleRate, 0.02);
    resetBlock();
}

void TransientShaperBlock::resetBlock() noexcept
{
    fast.reset();
    slow.reset();
    gainDb = 0.0f;
    output.setCurrentAndTarget (dbToGain (getParam (OutputDb)));
}

void TransientShaperBlock::processBlock (float* left, float* right, int numSamples) noexcept
{
    const float attack = getParam (Attack) * 0.01f;
    const float sustain = getParam (Sustain) * 0.01f;
    output.setTarget (dbToGain (getParam (OutputDb)));

    for (int i = 0; i < numSamples; ++i)
    {
        const float x = std::max (std::abs (left[i]), std::abs (right[i]));
        const float f = toDb (fast.process (x) + 1.0e-6f);
        const float s = toDb (slow.process (x) + 1.0e-6f);

        // fast > slow while a note starts (transient), slow > fast while it decays (sustain).
        const float targetDb = std::clamp (attack * 1.0f * std::max (0.0f, f - s)
                                         + sustain * 1.0f * std::max (0.0f, s - f), -24.0f, 24.0f);
        gainDb = targetDb + gainCoefficient * (gainDb - targetDb);

        const float g = dbToGain (gainDb) * output.next();
        left[i]  *= g;
        right[i] *= g;
    }
}
} // namespace kumamp
