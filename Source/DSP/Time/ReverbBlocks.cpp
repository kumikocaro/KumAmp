#include "ReverbBlocks.h"

#include <iterator>

namespace kumamp
{
namespace
{
    constexpr ParamInfo kHallParams[] = {
        { "decay",    "Decay",      0.3f,  20.0f,  2.5f, "s",  false },
        { "size",     "Tamano",     0.0f, 100.0f, 60.0f, "%",  false },
        { "predelay", "Pre-delay",  0.0f, 250.0f, 20.0f, "ms", false },
        { "damping",  "Damping",    0.0f, 100.0f, 40.0f, "%",  false },
        { "low_cut",  "Low Cut",   20.0f, 500.0f, 80.0f, "Hz", false },
        { "mod",      "Modulacion", 0.0f, 100.0f, 30.0f, "%",  false },
        { "mix",      "Mezcla",     0.0f, 100.0f, 25.0f, "%",  false },
    };
    constexpr ParamInfo kShimmerParams[] = {
        { "decay",    "Decay",      1.0f,  30.0f,  6.0f, "s",  false },
        { "shimmer",  "Shimmer",    0.0f, 100.0f, 50.0f, "%",  false },
        { "pitch",    "Intervalo",  0.0f,   3.0f,  0.0f, "+12|+7|+5|-12", true },
        { "damping",  "Damping",    0.0f, 100.0f, 35.0f, "%",  false },
        { "predelay", "Pre-delay",  0.0f, 250.0f, 30.0f, "ms", false },
        { "low_cut",  "Low Cut",   20.0f, 500.0f, 150.0f, "Hz", false },
        { "mix",      "Mezcla",     0.0f, 100.0f, 35.0f, "%",  false },
    };
    constexpr ParamInfo kCloudParams[] = {
        { "decay",     "Decay",      1.0f,  30.0f,  8.0f, "s",  false },
        { "diffusion", "Difusion",   0.0f, 100.0f, 80.0f, "%",  false },
        { "mod",       "Modulacion", 0.0f, 100.0f, 60.0f, "%",  false },
        { "size",      "Tamano",     0.0f, 100.0f, 80.0f, "%",  false },
        { "damping",   "Damping",    0.0f, 100.0f, 50.0f, "%",  false },
        { "low_cut",   "Low Cut",   20.0f, 500.0f, 120.0f, "Hz", false },
        { "mix",       "Mezcla",     0.0f, 100.0f, 40.0f, "%",  false },
    };
    constexpr ParamInfo kPlateParams[] = {
        { "decay",     "Decay",      0.0f, 100.0f, 50.0f, "%",  false },
        { "predelay",  "Pre-delay",  0.0f, 200.0f, 10.0f, "ms", false },
        { "bandwidth", "Brillo",     0.0f, 100.0f, 80.0f, "%",  false },
        { "damping",   "Damping",    0.0f, 100.0f, 30.0f, "%",  false },
        { "mod",       "Modulacion", 0.0f, 100.0f, 30.0f, "%",  false },
        { "low_cut",   "Low Cut",   20.0f, 500.0f, 60.0f, "Hz", false },
        { "mix",       "Mezcla",     0.0f, 100.0f, 25.0f, "%",  false },
    };
    constexpr ParamInfo kSpringParams[] = {
        { "decay",   "Decay",      0.0f,  100.0f,   55.0f, "%",  false },
        { "tone",    "Tono",    1000.0f, 8000.0f, 4500.0f, "Hz", false },
        { "drip",    "Drip",       0.0f,  100.0f,   60.0f, "%",  false },
        { "mix",     "Mezcla",     0.0f,  100.0f,   30.0f, "%",  false },
        { "low_cut", "Low Cut",   20.0f,  500.0f,  120.0f, "Hz", false },
    };
    static_assert (std::size (kHallParams)    == HallReverbBlock::NumParams);
    static_assert (std::size (kShimmerParams) == ShimmerReverbBlock::NumParams);
    static_assert (std::size (kCloudParams)   == CloudReverbBlock::NumParams);
    static_assert (std::size (kPlateParams)   == PlateReverbBlock::NumParams);
    static_assert (std::size (kSpringParams)  == SpringReverbBlock::NumParams);

    // Mutually prime-ish line lengths (ms) for the FDN.
    constexpr float kFdnLengthsMs[FdnReverbCore::kLines] = { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.3f, 67.9f, 73.1f };
    constexpr float kDiffuserMs[8] = { 4.7f, 3.6f, 12.7f, 9.3f, 17.1f, 23.3f, 31.9f, 41.3f };

    inline float dampingHz (float percent) noexcept
    {
        return 16000.0f * std::pow (1500.0f / 16000.0f, percent * 0.01f);
    }
    inline float sizeScale (float percent) noexcept { return 0.5f + 1.5f * percent * 0.01f; }
} // namespace

//==============================================================================
void FdnReverbCore::prepare (double newSampleRate, float maxSizeScale)
{
    sampleRate = newSampleRate;
    for (int i = 0; i < kLines; ++i)
        lines[(size_t) i].prepare ((int) (kFdnLengthsMs[i] * 0.001 * sampleRate * maxSizeScale) + 256);
    for (int i = 0; i < kLines; ++i)
        phases[(size_t) i] = (double) i / kLines;
    configure (1.0f, 2.0f, 8000.0f, 0.0f, 0.5f);
}

void FdnReverbCore::configure (float scale, float decaySeconds, float dampHz, float modulationDepth, float modulationHz) noexcept
{
    for (int i = 0; i < kLines; ++i)
    {
        const float length = std::min (kFdnLengthsMs[i] * 0.001f * (float) sampleRate * scale,
                                       (float) lines[(size_t) i].getCapacity() - 200.0f);
        lengths[(size_t) i] = length;
        // Gain per pass for a -60 dB decay in decaySeconds.
        gains[(size_t) i] = std::pow (10.0f, -3.0f * length / ((float) sampleRate * std::max (0.05f, decaySeconds)));
        damping[(size_t) i].setCutoff (sampleRate, dampHz);
    }
    modulationSamples = modulationDepth * 0.0012f * (float) sampleRate;   // up to ~1.2 ms
    phaseIncrement = (double) modulationHz / sampleRate;
}

void FdnReverbCore::reset() noexcept
{
    for (auto& l : lines) l.reset();
    for (auto& d : damping) d.reset();
}

void FdnReverbCore::process (float inLeft, float inRight, float& outLeft, float& outRight) noexcept
{
    std::array<float, kLines> v {};
    float sum = 0.0f;
    for (int i = 0; i < kLines; ++i)
    {
        auto& phase = phases[(size_t) i];
        phase += phaseIncrement * (1.0 + 0.13 * i);   // decorrelated modulation rates
        if (phase >= 1.0) phase -= 1.0;
        const float mod = modulationSamples * Lfo::sine (phase);
        v[(size_t) i] = damping[(size_t) i].lowPass (lines[(size_t) i].read (lengths[(size_t) i] + mod)) * gains[(size_t) i];
        sum += v[(size_t) i];
    }

    // Householder reflection: lossless, dense mixing at O(N).
    const float reflection = sum * (2.0f / kLines);
    outLeft = outRight = 0.0f;
    for (int i = 0; i < kLines; ++i)
    {
        const float in = (i & 1) ? inRight : inLeft;
        const float sign = (i & 2) ? -1.0f : 1.0f;
        lines[(size_t) i].push (v[(size_t) i] - reflection + in * sign * 0.5f);
        outLeft  += (i < 4 ? 1.0f : -1.0f) * ((i & 1) ? 0.5f : 1.0f) * v[(size_t) i];
        outRight += (i & 1 ? 1.0f : -0.6f) * v[(size_t) i];
    }
    outLeft *= 0.35f;
    outRight *= 0.35f;
}

//==============================================================================
void ReverbInput::prepare (double sampleRate, int count, float diffuserScale)
{
    numDiffusers = std::clamp (count, 0, 8);
    for (auto& d : predelay) d.prepare ((int) (sampleRate * 0.26));
    for (int c = 0; c < 2; ++c)
        for (int k = 0; k < 8; ++k)
            diffusers[(size_t) c][(size_t) k].prepare ((int) (kDiffuserMs[k] * (c == 0 ? 1.0f : 1.07f) * diffuserScale * 0.001 * sampleRate));
}

void ReverbInput::reset() noexcept
{
    for (auto& f : lowCut) f.reset();
    for (auto& d : predelay) d.reset();
    for (auto& ch : diffusers)
        for (auto& d : ch) d.reset();
}

void ReverbInput::process (float& left, float& right, float predelaySamples, float diffusion) noexcept
{
    float* io[] { &left, &right };
    for (int c = 0; c < 2; ++c)
    {
        float x = lowCut[(size_t) c].process (*io[c]);
        predelay[(size_t) c].push (x);
        x = predelaySamples >= 1.0f ? predelay[(size_t) c].read (predelaySamples) : x;
        for (int k = 0; k < numDiffusers; ++k)
            x = diffusers[(size_t) c][(size_t) k].process (x, diffusion);
        *io[c] = x;
    }
}

//==============================================================================
HallReverbBlock::HallReverbBlock (StoreId id) : TrailsBlock (id, BlockType::HallReverb, kHallParams, NumParams) {}

void HallReverbBlock::prepareBlock (double sampleRate, int)
{
    input.prepare (sampleRate, 4, 1.0f);
    fdn.prepare (sampleRate, 2.0f);
    mix.prepare (sampleRate, 0.02);
    prepareTrails (sampleRate);
    resetBlock();
}

void HallReverbBlock::resetTail() noexcept { input.reset(); fdn.reset(); }

void HallReverbBlock::resetBlock() noexcept
{
    resetTail();
    mix.setCurrentAndTarget (getParam (Mix) * 0.01f);
}

void HallReverbBlock::processBlock (float* left, float* right, int numSamples) noexcept
{
    if (! beginTrailsBlock (numSamples))
        return;

    const double sr = getSampleRate();
    fdn.configure (sizeScale (getParam (Size)), getParam (DecaySeconds), dampingHz (getParam (Damping)),
                   getParam (Modulation) * 0.01f, 0.6f);
    for (auto& f : input.lowCut) f.setHighPass (sr, getParam (LowCutHz));
    const float predelay = getParam (PredelayMs) * 0.001f * (float) sr;
    mix.setTarget (getParam (Mix) * 0.01f);

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = nextFeed();
        float l = left[i] * g, r = right[i] * g;
        input.process (l, r, predelay, 0.7f);
        float wl, wr;
        fdn.process (l, r, wl, wr);
        trackTail (std::max (std::abs (wl), std::abs (wr)));
        const float m = mix.next();
        left[i]  += wl * m;
        right[i] += wr * m;
    }
    endTrailsBlock (numSamples);
}

//==============================================================================
ShimmerReverbBlock::ShimmerReverbBlock (StoreId id) : TrailsBlock (id, BlockType::ShimmerReverb, kShimmerParams, NumParams) {}

void ShimmerReverbBlock::prepareBlock (double sampleRate, int)
{
    input.prepare (sampleRate, 4, 1.2f);
    fdn.prepare (sampleRate, 2.0f);
    shifter.prepare (sampleRate);
    shifter.setGrainMs (40.0f);   // smooth, chorus-free octave in a wash
    mix.prepare (sampleRate, 0.02);
    prepareTrails (sampleRate);
    resetBlock();
}

void ShimmerReverbBlock::resetTail() noexcept
{
    input.reset();
    fdn.reset();
    shifter.reset();
    shimmerTone.reset();
    shimmerSample = 0.0f;
}

void ShimmerReverbBlock::resetBlock() noexcept
{
    resetTail();
    mix.setCurrentAndTarget (getParam (Mix) * 0.01f);
}

void ShimmerReverbBlock::processBlock (float* left, float* right, int numSamples) noexcept
{
    if (! beginTrailsBlock (numSamples))
        return;

    static constexpr float kRatios[] = { 2.0f, 1.4983071f, 1.3348399f, 0.5f };
    const double sr = getSampleRate();
    fdn.configure (1.6f, getParam (DecaySeconds), dampingHz (getParam (Damping)), 0.4f, 0.4f);
    for (auto& f : input.lowCut) f.setHighPass (sr, getParam (LowCutHz));
    shifter.setRatio (kRatios[std::clamp ((int) std::lround (getParam (Pitch)), 0, 3)], 0.0f);
    shimmerTone.setLowPass (sr, 7000.0f);
    const float amount = getParam (Shimmer) * 0.01f * 0.45f;   // < 0.5 keeps the loop stable
    const float predelay = getParam (PredelayMs) * 0.001f * (float) sr;
    mix.setTarget (getParam (Mix) * 0.01f);

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = nextFeed();
        float l = left[i] * g, r = right[i] * g;
        input.process (l, r, predelay, 0.7f);

        float wl, wr;
        fdn.process (l + shimmerSample, r + shimmerSample, wl, wr);

        // Pitch-shift the tail and feed it back in: every pass climbs an interval.
        float mono = 0.5f * (wl + wr), shifted = 0.0f;
        shifter.process (&mono, &mono, &shifted, &shifted, 1);
        shimmerSample = shimmerTone.process (shifted) * amount;

        trackTail (std::max (std::abs (wl), std::abs (wr)));
        const float m = mix.next();
        left[i]  += wl * m;
        right[i] += wr * m;
    }
    endTrailsBlock (numSamples);
}

//==============================================================================
CloudReverbBlock::CloudReverbBlock (StoreId id) : TrailsBlock (id, BlockType::CloudReverb, kCloudParams, NumParams) {}

void CloudReverbBlock::prepareBlock (double sampleRate, int)
{
    input.prepare (sampleRate, 8, 1.6f);   // long diffusion chain = soft, swelling attack
    fdn.prepare (sampleRate, 3.0f);
    mix.prepare (sampleRate, 0.02);
    prepareTrails (sampleRate);
    resetBlock();
}

void CloudReverbBlock::resetTail() noexcept { input.reset(); fdn.reset(); }

void CloudReverbBlock::resetBlock() noexcept
{
    resetTail();
    mix.setCurrentAndTarget (getParam (Mix) * 0.01f);
}

void CloudReverbBlock::processBlock (float* left, float* right, int numSamples) noexcept
{
    if (! beginTrailsBlock (numSamples))
        return;

    const double sr = getSampleRate();
    fdn.configure (1.0f + 2.0f * getParam (Size) * 0.01f, getParam (DecaySeconds), dampingHz (getParam (Damping)),
                   0.3f + 1.2f * getParam (Modulation) * 0.01f, 0.25f);
    for (auto& f : input.lowCut) f.setHighPass (sr, getParam (LowCutHz));
    const float diffusion = 0.45f + 0.35f * getParam (Diffusion) * 0.01f;
    const float predelay = 0.040f * (float) sr;
    mix.setTarget (getParam (Mix) * 0.01f);

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = nextFeed();
        float l = left[i] * g, r = right[i] * g;
        input.process (l, r, predelay, diffusion);
        float wl, wr;
        fdn.process (l, r, wl, wr);
        trackTail (std::max (std::abs (wl), std::abs (wr)));
        const float m = mix.next();
        left[i]  += wl * m;
        right[i] += wr * m;
    }
    endTrailsBlock (numSamples);
}

//==============================================================================
PlateReverbBlock::PlateReverbBlock (StoreId id) : TrailsBlock (id, BlockType::PlateReverb, kPlateParams, NumParams) {}

int PlateReverbBlock::scaled (int samplesAt29761) const noexcept
{
    return std::max (1, (int) std::lround (samplesAt29761 * getSampleRate() / 29761.0));
}

void PlateReverbBlock::prepareBlock (double sampleRate, int)
{
    predelay.prepare ((int) (sampleRate * 0.21));

    static constexpr int kInput[4] = { 142, 107, 379, 277 };
    for (int k = 0; k < 4; ++k)
    {
        inputDiffusers[(size_t) k].line.prepare (scaled (kInput[k]) + 4);
        inputDiffusers[(size_t) k].length = (float) scaled (kInput[k]);
    }

    auto setup = [&] (Allpass& a, int length, int excursion)
    {
        a.line.prepare (scaled (length) + scaled (excursion) + 8);
        a.length = (float) scaled (length);
    };
    setup (leftMod, 672, 16);
    setup (rightMod, 908, 16);
    setup (leftDiffuse, 1800, 0);
    setup (rightDiffuse, 2656, 0);
    leftDelayA.prepare (scaled (4453) + 4);
    leftDelayB.prepare (scaled (3720) + 4);
    rightDelayA.prepare (scaled (4217) + 4);
    rightDelayB.prepare (scaled (3163) + 4);

    lfo.prepare (sampleRate);
    lfo.setFrequency (1.0f);
    mix.prepare (sampleRate, 0.02);
    prepareTrails (sampleRate);
    resetBlock();
}

void PlateReverbBlock::resetTail() noexcept
{
    lowCut.reset();
    predelay.reset();
    for (auto& a : inputDiffusers) a.line.reset();
    for (auto* a : { &leftMod, &rightMod, &leftDiffuse, &rightDiffuse }) a->line.reset();
    for (auto* d : { &leftDelayA, &leftDelayB, &rightDelayA, &rightDelayB }) d->reset();
    bandwidthState = leftDamp = rightDamp = leftTankOut = rightTankOut = 0.0f;
}

void PlateReverbBlock::resetBlock() noexcept
{
    resetTail();
    lfo.reset();
    mix.setCurrentAndTarget (getParam (Mix) * 0.01f);
}

void PlateReverbBlock::processBlock (float* left, float* right, int numSamples) noexcept
{
    if (! beginTrailsBlock (numSamples))
        return;

    const double sr = getSampleRate();
    lowCut.setHighPass (sr, getParam (LowCutHz));
    const float decay = 0.2f + 0.78f * getParam (Decay) * 0.01f;
    const float bandwidth = 0.1f + 0.8995f * getParam (Bandwidth) * 0.01f;
    const float damping = 0.9f * getParam (Damping) * 0.01f;
    const float excursion = (float) scaled (16) * getParam (Modulation) * 0.01f * 2.0f;
    const float predelaySamples = getParam (PredelayMs) * 0.001f * (float) sr;
    mix.setTarget (getParam (Mix) * 0.01f);

    // Output tap positions (Dattorro table 2), scaled to the current rate.
    const int t266 = scaled (266), t2974 = scaled (2974), t1913 = scaled (1913), t1996 = scaled (1996);
    const int t1990 = scaled (1990), t187 = scaled (187), t1066 = scaled (1066), t353 = scaled (353);
    const int t3627 = scaled (3627), t1228 = scaled (1228), t2673 = scaled (2673), t2111 = scaled (2111);
    const int t335 = scaled (335), t121 = scaled (121);
    const int lA = scaled (4453), lB = scaled (3720), rA = scaled (4217), rB = scaled (3163);

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = nextFeed();
        float x = lowCut.process (0.5f * (left[i] + right[i]) * g);
        predelay.push (x);
        if (predelaySamples >= 1.0f)
            x = predelay.read (predelaySamples);

        bandwidthState += bandwidth * (x - bandwidthState);
        x = bandwidthState;
        x = inputDiffusers[0].process (x, 0.75f);
        x = inputDiffusers[1].process (x, 0.75f);
        x = inputDiffusers[2].process (x, 0.625f);
        x = inputDiffusers[3].process (x, 0.625f);

        const float mod = excursion * Lfo::sine (lfo.advance());

        // Left half of the tank (fed by the right half's output, and vice versa).
        float l = leftMod.process (x + decay * rightTankOut, -0.7f, mod);
        leftDelayA.push (l);
        l = leftDelayA.readInteger (lA);
        leftDamp += (1.0f - damping) * (l - leftDamp);
        l = leftDiffuse.process (leftDamp * decay, 0.5f);
        leftDelayB.push (l);

        float r = rightMod.process (x + decay * leftTankOut, -0.7f, -mod);
        rightDelayA.push (r);
        r = rightDelayA.readInteger (rA);
        rightDamp += (1.0f - damping) * (r - rightDamp);
        r = rightDiffuse.process (rightDamp * decay, 0.5f);
        rightDelayB.push (r);

        leftTankOut = leftDelayB.readInteger (lB);
        rightTankOut = rightDelayB.readInteger (rB);

        const float wl = 0.6f * (rightDelayA.readInteger (t266) + rightDelayA.readInteger (t2974)
                               - rightDiffuse.line.readInteger (t1913) + rightDelayB.readInteger (t1996)
                               - leftDelayA.readInteger (t1990) - leftDiffuse.line.readInteger (t187)
                               - leftDelayB.readInteger (t1066));
        const float wr = 0.6f * (leftDelayA.readInteger (t353) + leftDelayA.readInteger (t3627)
                               - leftDiffuse.line.readInteger (t1228) + leftDelayB.readInteger (t2673)
                               - rightDelayA.readInteger (t2111) - rightDiffuse.line.readInteger (t335)
                               - rightDelayB.readInteger (t121));

        trackTail (std::max (std::abs (wl), std::abs (wr)));
        const float m = mix.next();
        left[i]  += wl * m;
        right[i] += wr * m;
    }
    endTrailsBlock (numSamples);
}

//==============================================================================
SpringReverbBlock::SpringReverbBlock (StoreId id) : TrailsBlock (id, BlockType::SpringReverb, kSpringParams, NumParams) {}

void SpringReverbBlock::prepareBlock (double sampleRate, int)
{
    static constexpr float kLoopMs[2] = { 37.0f, 43.0f };   // two springs, slightly different
    for (int c = 0; c < 2; ++c)
    {
        auto& s = springs[(size_t) c];
        for (auto& d : s.dispersion)
            d.prepare (2 + c);   // very short allpasses in cascade = chirp dispersion
        s.loopLength = kLoopMs[c] * 0.001f * (float) sampleRate;
        s.loop.prepare ((int) s.loopLength + 8);
    }
    mix.prepare (sampleRate, 0.02);
    prepareTrails (sampleRate);
    resetBlock();
}

void SpringReverbBlock::resetTail() noexcept
{
    for (auto& s : springs)
    {
        for (auto& d : s.dispersion) d.reset();
        s.loop.reset();
        s.tone.reset();
        s.lowCut.reset();
        s.feedbackSample = 0.0f;
    }
}

void SpringReverbBlock::resetBlock() noexcept
{
    resetTail();
    mix.setCurrentAndTarget (getParam (Mix) * 0.01f);
}

void SpringReverbBlock::processBlock (float* left, float* right, int numSamples) noexcept
{
    if (! beginTrailsBlock (numSamples))
        return;

    const double sr = getSampleRate();
    const float feedback = 0.55f + 0.4f * getParam (Decay) * 0.01f;
    const float chirp = 0.35f + 0.4f * getParam (Drip) * 0.01f;
    for (auto& s : springs)
    {
        s.tone.setCutoff (sr, getParam (ToneHz));
        s.lowCut.setHighPass (sr, getParam (LowCutHz));
    }
    mix.setTarget (getParam (Mix) * 0.01f);

    float* io[] { left, right };
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = nextFeed(), m = mix.next();
        const float in = 0.5f * (left[i] + right[i]) * g;
        float peak = 0.0f;

        for (int c = 0; c < 2; ++c)
        {
            auto& s = springs[(size_t) c];
            float x = s.lowCut.process (in) + feedback * s.feedbackSample;
            for (auto& d : s.dispersion)
                x = d.process (x, chirp);
            s.loop.push (softClip (x));
            s.feedbackSample = s.tone.lowPass (s.loop.read (s.loopLength));
            const float y = s.feedbackSample * 0.7f;
            peak = std::max (peak, std::abs (y));
            io[c][i] += y * m;
        }
        trackTail (peak);
    }
    endTrailsBlock (numSamples);
}
} // namespace kumamp
