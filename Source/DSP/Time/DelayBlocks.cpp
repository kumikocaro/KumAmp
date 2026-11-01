#include "DelayBlocks.h"

#include "../../Core/Tempo.h"

#include <iterator>
#include <string>

namespace kumamp
{
namespace
{
    constexpr double kMaxDelaySeconds = 2.1;

    constexpr ParamInfo kDigitalParams[] = {
        { "time",      "Tiempo",      1.0f,  2000.0f,   400.0f, "ms", false },
        { "feedback",  "Feedback",    0.0f,   100.0f,    35.0f, "%",  false },
        { "mix",       "Mezcla",      0.0f,   100.0f,    35.0f, "%",  false },
        { "low_cut",   "Low Cut",    20.0f,  1000.0f,    20.0f, "Hz", false },   // 20 Hz = off
        { "high_cut",  "High Cut", 1000.0f, 20000.0f, 20000.0f, "Hz", false },   // 20 kHz = off
        { "pingpong",  "Ping-Pong",   0.0f,     1.0f,     0.0f, "Off|On", true },
        { "sync",     "Sync",       0.0f,   1.0f,  0.0f, "Off|On", true },
        { "division", "Division",   0.0f,  15.0f,  6.0f, tempo::kDivisionChoices, true },
    };
    constexpr ParamInfo kAnalogParams[] = {
        { "time",     "Tiempo",    20.0f,  800.0f,  300.0f, "ms", false },
        { "feedback", "Feedback",   0.0f,  110.0f,   40.0f, "%",  false },
        { "mix",      "Mezcla",     0.0f,  100.0f,   35.0f, "%",  false },
        { "tone",     "Tono",     500.0f, 6000.0f, 2500.0f, "Hz", false },
        { "mod",      "Modulacion", 0.0f,  100.0f,   20.0f, "%",  false },
        { "sync",     "Sync",       0.0f,   1.0f,  0.0f, "Off|On", true },
        { "division", "Division",   0.0f,  15.0f,  6.0f, tempo::kDivisionChoices, true },
    };
    constexpr ParamInfo kTapeParams[] = {
        { "time",       "Tiempo",      40.0f, 1500.0f, 350.0f, "ms", false },
        { "feedback",   "Feedback",     0.0f,  110.0f,  45.0f, "%",  false },
        { "mix",        "Mezcla",       0.0f,  100.0f,  35.0f, "%",  false },
        { "wow",        "Wow",          0.0f,  100.0f,  30.0f, "%",  false },
        { "flutter",    "Flutter",      0.0f,  100.0f,  25.0f, "%",  false },
        { "saturation", "Saturacion",   0.0f,  100.0f,  40.0f, "%",  false },
        { "age",        "Desgaste",     0.0f,  100.0f,  30.0f, "%",  false },
        { "sync",     "Sync",       0.0f,   1.0f,  0.0f, "Off|On", true },
        { "division", "Division",   0.0f,  15.0f,  7.0f, tempo::kDivisionChoices, true },
    };
    constexpr ParamInfo kReverseParams[] = {
        { "time",     "Tiempo",   100.0f, 2000.0f, 500.0f, "ms", false },
        { "feedback", "Feedback",   0.0f,   90.0f,  30.0f, "%",  false },
        { "mix",      "Mezcla",     0.0f,  100.0f,  50.0f, "%",  false },
        { "sync",     "Sync",       0.0f,   1.0f,  0.0f, "Off|On", true },
        { "division", "Division",   0.0f,  15.0f,  3.0f, tempo::kDivisionChoices, true },
    };
    static_assert (std::size (kDigitalParams) == DigitalDelayBlock::NumParams);
    static_assert (std::size (kAnalogParams)  == AnalogDelayBlock::NumParams);
    static_assert (std::size (kTapeParams)    == TapeDelayBlock::NumParams);
    static_assert (std::size (kReverseParams) == ReverseDelayBlock::NumParams);

    std::string syncedText (float division, float ms)
    {
        return std::string (tempo::kNames[(size_t) tempo::clampDivision (division)]) + " = " + std::to_string ((int) std::lround (ms)) + " ms";
    }

    /** Guards the feedback loop against runaway at extreme settings. */
    inline float limitLoop (float x) noexcept { return std::clamp (x, -4.0f, 4.0f); }
} // namespace

//==============================================================================
DigitalDelayBlock::DigitalDelayBlock (StoreId id) : TrailsBlock (id, BlockType::DigitalDelay, kDigitalParams, NumParams) {}

void DigitalDelayBlock::prepareBlock (double sampleRate, int)
{
    for (auto& l : lines) l.prepare ((int) (sampleRate * kMaxDelaySeconds));
    timeCoefficient = EnvelopeFollower::coefficient (sampleRate, 60.0f);
    mix.prepare (sampleRate, 0.02);
    prepareTrails (sampleRate, kMaxDelaySeconds + 0.5);
    resetBlock();
}

void DigitalDelayBlock::resetTail() noexcept
{
    for (auto& l : lines) l.reset();
    for (auto& f : lowCut) f.reset();
    for (auto& f : highCut) f.reset();
}

void DigitalDelayBlock::resetBlock() noexcept
{
    resetTail();
    delaySamples = effectiveTimeMs() * 0.001f * (float) getSampleRate();
    mix.setCurrentAndTarget (getParam (Mix) * 0.01f);
}

void DigitalDelayBlock::processBlock (float* left, float* right, int numSamples) noexcept
{
    if (! beginTrailsBlock (numSamples))
        return;

    const double sr = getSampleRate();
    const float target = effectiveTimeMs() * 0.001f * (float) sr;
    const float feedback = getParam (Feedback) * 0.01f;
    const bool pingPong = getParam (PingPong) > 0.5f;
    const bool useLowCut = getParam (LowCutHz) > 20.5f, useHighCut = getParam (HighCutHz) < 19999.5f;
    for (auto& f : lowCut) f.setHighPass (sr, getParam (LowCutHz));
    for (auto& f : highCut) f.setLowPass (sr, getParam (HighCutHz));
    mix.setTarget (getParam (Mix) * 0.01f);

    for (int i = 0; i < numSamples; ++i)
    {
        delaySamples = target + timeCoefficient * (delaySamples - target);
        const float g = nextFeed();
        const float inL = left[i] * g, inR = right[i] * g;

        // Read happens before this sample is written: tap D - 1 is a delay of exactly D.
        float yL = lines[0].read (delaySamples - 1.0f), yR = lines[1].read (delaySamples - 1.0f);
        float fL = yL, fR = yR;
        if (useLowCut)  { fL = lowCut[0].process (fL);  fR = lowCut[1].process (fR); }
        if (useHighCut) { fL = highCut[0].process (fL); fR = highCut[1].process (fR); }

        if (pingPong)
        {
            lines[0].push (limitLoop (0.5f * (inL + inR) + feedback * fR));
            lines[1].push (limitLoop (feedback * fL));
        }
        else
        {
            lines[0].push (limitLoop (inL + feedback * fL));
            lines[1].push (limitLoop (inR + feedback * fR));
        }

        trackTail (std::max (std::abs (yL), std::abs (yR)));
        const float m = mix.next();
        left[i]  += yL * m;
        right[i] += yR * m;
    }
    endTrailsBlock (numSamples);
}

//==============================================================================
AnalogDelayBlock::AnalogDelayBlock (StoreId id) : TrailsBlock (id, BlockType::AnalogDelay, kAnalogParams, NumParams) {}

void AnalogDelayBlock::prepareBlock (double sampleRate, int)
{
    for (auto& l : lines) l.prepare ((int) (sampleRate * 0.9));
    timeCoefficient = EnvelopeFollower::coefficient (sampleRate, 120.0f);
    lfo.prepare (sampleRate);
    lfo.setFrequency (0.45f);
    for (auto& f : highPass) f.setCutoff (sampleRate, 70.0f);
    mix.prepare (sampleRate, 0.02);
    prepareTrails (sampleRate, 1.4);
    resetBlock();
}

void AnalogDelayBlock::resetTail() noexcept
{
    for (auto& l : lines) l.reset();
    for (auto& f : toneA) f.reset();
    for (auto& f : toneB) f.reset();
    for (auto& f : highPass) f.reset();
}

void AnalogDelayBlock::resetBlock() noexcept
{
    resetTail();
    lfo.reset();
    delaySamples = effectiveTimeMs() * 0.001f * (float) getSampleRate();
    mix.setCurrentAndTarget (getParam (Mix) * 0.01f);
}

void AnalogDelayBlock::processBlock (float* left, float* right, int numSamples) noexcept
{
    if (! beginTrailsBlock (numSamples))
        return;

    const double sr = getSampleRate();
    const float target = effectiveTimeMs() * 0.001f * (float) sr;
    const float feedback = getParam (Feedback) * 0.01f;
    const float swing = getParam (Modulation) * 0.01f * 0.0015f * (float) sr;   // up to +-1.5 ms
    for (int c = 0; c < 2; ++c)
    {
        toneA[(size_t) c].setCutoff (sr, getParam (ToneHz));
        toneB[(size_t) c].setCutoff (sr, getParam (ToneHz) * 1.5f);
    }
    mix.setTarget (getParam (Mix) * 0.01f);

    float* io[] { left, right };
    for (int i = 0; i < numSamples; ++i)
    {
        delaySamples = target + timeCoefficient * (delaySamples - target);
        const float mod = swing * Lfo::sine (lfo.advance());
        const float g = nextFeed(), m = mix.next();
        float peak = 0.0f;

        for (int c = 0; c < 2; ++c)
        {
            auto& line = lines[(size_t) c];
            const float y = line.read (delaySamples - 1.0f + (c == 0 ? mod : -mod));
            // Each repeat gets darker and slightly driven, like a BBD line.
            const float loop = highPass[(size_t) c].highPass (toneB[(size_t) c].lowPass (toneA[(size_t) c].lowPass (y)));
            line.push (limitLoop (softClip (io[c][i] * g + feedback * loop)));
            peak = std::max (peak, std::abs (y));
            io[c][i] += y * m;
        }
        trackTail (peak);
    }
    endTrailsBlock (numSamples);
}

//==============================================================================
TapeDelayBlock::TapeDelayBlock (StoreId id) : TrailsBlock (id, BlockType::TapeDelay, kTapeParams, NumParams) {}

void TapeDelayBlock::prepareBlock (double sampleRate, int)
{
    for (auto& l : lines) l.prepare ((int) (sampleRate * 1.6));
    timeCoefficient = EnvelopeFollower::coefficient (sampleRate, 250.0f);   // motor inertia
    wowLfo.prepare (sampleRate);
    wowLfo.setFrequency (0.55f);
    flutterLfo.prepare (sampleRate);
    flutterLfo.setFrequency (6.8f);
    mix.prepare (sampleRate, 0.02);
    prepareTrails (sampleRate, 2.1);
    resetBlock();
}

void TapeDelayBlock::resetTail() noexcept
{
    for (auto& l : lines) l.reset();
    for (auto& f : ageLow) f.reset();
    for (auto& f : ageHigh) f.reset();
}

void TapeDelayBlock::resetBlock() noexcept
{
    resetTail();
    wowLfo.reset();
    flutterLfo.reset (0.3);
    drift = driftTarget = 0.0f;
    delaySamples = effectiveTimeMs() * 0.001f * (float) getSampleRate();
    mix.setCurrentAndTarget (getParam (Mix) * 0.01f);
}

void TapeDelayBlock::processBlock (float* left, float* right, int numSamples) noexcept
{
    if (! beginTrailsBlock (numSamples))
        return;

    const double sr = getSampleRate();
    const float target = effectiveTimeMs() * 0.001f * (float) sr;
    const float feedback = getParam (Feedback) * 0.01f;
    const float wow = getParam (Wow) * 0.01f * 0.004f;          // up to 0.4 % speed deviation
    const float flutter = getParam (Flutter) * 0.01f * 0.0008f;
    const float drive = 1.0f + getParam (Saturation) * 0.03f;
    const float age = getParam (Age) * 0.01f;
    for (int c = 0; c < 2; ++c)
    {
        ageLow[(size_t) c].setLowPass (sr, 14000.0f * std::pow (2500.0f / 14000.0f, age));
        ageHigh[(size_t) c].setHighPass (sr, 40.0f + 160.0f * age);
    }
    mix.setTarget (getParam (Mix) * 0.01f);

    float* io[] { left, right };
    for (int i = 0; i < numSamples; ++i)
    {
        // Slow random drift on top of the wow LFO (capstan / tape stretch).
        if (++driftCounter >= 2048)
        {
            driftCounter = 0;
            noiseState = noiseState * 1664525u + 1013904223u;
            driftTarget = ((float) (noiseState >> 8) / 16777216.0f) * 2.0f - 1.0f;
        }
        drift += 0.0005f * (driftTarget - drift);

        delaySamples = target + timeCoefficient * (delaySamples - target);
        const float speed = wow * (Lfo::sine (wowLfo.advance()) + 0.5f * drift) + flutter * Lfo::sine (flutterLfo.advance());
        const float d = delaySamples * (1.0f + speed) - 1.0f;
        const float g = nextFeed(), m = mix.next();
        float peak = 0.0f;

        for (int c = 0; c < 2; ++c)
        {
            auto& line = lines[(size_t) c];
            const float y = line.read (d);
            const float loop = ageHigh[(size_t) c].process (ageLow[(size_t) c].process (y));
            const float record = softClip ((io[c][i] * g + feedback * loop) * drive) / drive;
            line.push (limitLoop (record));
            peak = std::max (peak, std::abs (y));
            io[c][i] += y * m;
        }
        trackTail (peak);
    }
    endTrailsBlock (numSamples);
}

//==============================================================================
ReverseDelayBlock::ReverseDelayBlock (StoreId id) : TrailsBlock (id, BlockType::ReverseDelay, kReverseParams, NumParams) {}

void ReverseDelayBlock::prepareBlock (double sampleRate, int)
{
    for (auto& l : lines) l.prepare ((int) (sampleRate * 4.2) + 8);   // reads up to 2 x segment
    mix.prepare (sampleRate, 0.02);
    prepareTrails (sampleRate, 4.7);
    resetBlock();
}

void ReverseDelayBlock::resetTail() noexcept
{
    for (auto& l : lines) l.reset();
}

void ReverseDelayBlock::resetBlock() noexcept
{
    resetTail();
    const int length = std::max (2, (int) (effectiveTimeMs() * 0.001f * (float) getSampleRate()));
    heads[0] = { 0, length };
    heads[1] = { length / 2, length };   // 50 % overlap
    mix.setCurrentAndTarget (getParam (Mix) * 0.01f);
}

void ReverseDelayBlock::processBlock (float* left, float* right, int numSamples) noexcept
{
    if (! beginTrailsBlock (numSamples))
        return;

    const int length = std::max (2, (int) (effectiveTimeMs() * 0.001f * (float) getSampleRate()));
    const float feedback = getParam (Feedback) * 0.01f;
    mix.setTarget (getParam (Mix) * 0.01f);

    for (int i = 0; i < numSamples; ++i)
    {
        float yL = 0.0f, yR = 0.0f;
        for (auto& head : heads)
        {
            // Reading backwards: the delay grows 2 samples per sample across the segment.
            const float u = (float) head.position / (float) head.length;
            const float w = std::sin (kPi * u);
            const float window = w * w;   // Hann, sums to 1 with 50 % overlap
            const int delay = 2 * head.position + 1;
            yL += lines[0].readInteger (delay) * window;
            yR += lines[1].readInteger (delay) * window;

            if (++head.position >= head.length)
            {
                head.position = 0;
                head.length = length;   // new time takes effect per segment: no glitches
            }
        }

        const float g = nextFeed();
        lines[0].push (limitLoop (left[i] * g + feedback * yL));
        lines[1].push (limitLoop (right[i] * g + feedback * yR));

        trackTail (std::max (std::abs (yL), std::abs (yR)));
        const float m = mix.next();
        left[i]  += yL * m;
        right[i] += yR * m;
    }
    endTrailsBlock (numSamples);
}

float DigitalDelayBlock::effectiveTimeMs() const noexcept
{
    const float ms = getParam (Sync) > 0.5f ? (float) tempo::divisionMs (getParam (Division), getTempoBpm()) : getParam (TimeMs);
    return std::clamp (ms, kDigitalParams[TimeMs].minValue, kDigitalParams[TimeMs].maxValue);
}

std::string DigitalDelayBlock::getDisplayOverride (int param) const
{
    return param == TimeMs && getParam (Sync) > 0.5f ? syncedText (getParam (Division), effectiveTimeMs()) : std::string {};
}

float AnalogDelayBlock::effectiveTimeMs() const noexcept
{
    const float ms = getParam (Sync) > 0.5f ? (float) tempo::divisionMs (getParam (Division), getTempoBpm()) : getParam (TimeMs);
    return std::clamp (ms, kAnalogParams[TimeMs].minValue, kAnalogParams[TimeMs].maxValue);
}

std::string AnalogDelayBlock::getDisplayOverride (int param) const
{
    return param == TimeMs && getParam (Sync) > 0.5f ? syncedText (getParam (Division), effectiveTimeMs()) : std::string {};
}

float TapeDelayBlock::effectiveTimeMs() const noexcept
{
    const float ms = getParam (Sync) > 0.5f ? (float) tempo::divisionMs (getParam (Division), getTempoBpm()) : getParam (TimeMs);
    return std::clamp (ms, kTapeParams[TimeMs].minValue, kTapeParams[TimeMs].maxValue);
}

std::string TapeDelayBlock::getDisplayOverride (int param) const
{
    return param == TimeMs && getParam (Sync) > 0.5f ? syncedText (getParam (Division), effectiveTimeMs()) : std::string {};
}

float ReverseDelayBlock::effectiveTimeMs() const noexcept
{
    const float ms = getParam (Sync) > 0.5f ? (float) tempo::divisionMs (getParam (Division), getTempoBpm()) : getParam (TimeMs);
    return std::clamp (ms, kReverseParams[TimeMs].minValue, kReverseParams[TimeMs].maxValue);
}

std::string ReverseDelayBlock::getDisplayOverride (int param) const
{
    return param == TimeMs && getParam (Sync) > 0.5f ? syncedText (getParam (Division), effectiveTimeMs()) : std::string {};
}
} // namespace kumamp
