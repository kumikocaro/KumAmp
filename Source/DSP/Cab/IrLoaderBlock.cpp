#include "IrLoaderBlock.h"
#include "../../Core/DspMath.h"

#include <iterator>

namespace kumamp
{
namespace
{
    constexpr ParamInfo kParams[] = {
        { "level_a",  "Nivel A",    -60.0f,    12.0f,     0.0f, "dB", false },
        { "pan_a",    "Pan A",       -1.0f,     1.0f,     0.0f, "",   false },
        { "level_b",  "Nivel B",    -60.0f,    12.0f,     0.0f, "dB", false },
        { "pan_b",    "Pan B",       -1.0f,     1.0f,     0.0f, "",   false },
        { "enable_b", "IR B activa",  0.0f,     1.0f,     0.0f, "",   true  },
        { "low_cut",  "Low Cut",     20.0f,   500.0f,    20.0f, "Hz", false },   // 20 Hz = off
        { "high_cut", "High Cut",  2000.0f, 20000.0f, 20000.0f, "Hz", false },  // 20 kHz = off
        { "mix",      "Mezcla",       0.0f,   100.0f,   100.0f, "%",  false },
        { "auto_e2e", "Auto E2E",     0.0f,     1.0f,     1.0f, "",   true  },
    };
    static_assert (std::size (kParams) == IrLoaderBlock::NumParams);
} // namespace

IrLoaderBlock::IrLoaderBlock (StoreId id)
    : DSPBlock (id, BlockType::IrLoader, kParams, NumParams)
{
    statuses = { "Sin IR", "Sin IR" };
}

bool IrLoaderBlock::loadFile (int slot, const std::string& path)
{
    if (slot < 0 || slot > 1 || path.empty())
        return false;

    {
        const std::lock_guard<std::mutex> guard (pathLock);
        paths[(size_t) slot] = path;
    }

    if (isPrepared())
        loadImpulse (slot);
    return true;
}

std::string IrLoaderBlock::getFilePath (int slot) const
{
    const std::lock_guard<std::mutex> guard (pathLock);
    return slot >= 0 && slot < 2 ? paths[(size_t) slot] : std::string {};
}

bool IrLoaderBlock::isAutoBypassActive() const noexcept
{
    return autoBypassed.load (std::memory_order_relaxed) && getParam (AutoE2E) > 0.5f;
}

std::string IrLoaderBlock::getStatusText() const
{
    const std::lock_guard<std::mutex> guard (pathLock);
    std::string text = "A: " + statuses[0] + "   B: " + statuses[1];
    if (isAutoBypassActive())
        text += "   |   Desacoplado: perfil NAM End-to-End";
    return text;
}

void IrLoaderBlock::loadImpulse (int slot)
{
    const std::lock_guard<std::mutex> guard (pathLock);
    const juce::File file { juce::String (paths[(size_t) slot]) };

    if (! file.existsAsFile())
    {
        statuses[(size_t) slot] = "No encontrado";
        return;
    }

    // No trimming: keeps the mic phase relationship between IR A and B.
    // Thread-safe: JUCE parses, resamples and swaps the IR on its own queue thread.
    convolutions[(size_t) slot].loadImpulseResponse (file, juce::dsp::Convolution::Stereo::yes,
                                                     juce::dsp::Convolution::Trim::no, 0,
                                                     juce::dsp::Convolution::Normalise::yes);
    loaded[(size_t) slot].store (true);
    statuses[(size_t) slot] = file.getFileName().toStdString();
}

void IrLoaderBlock::prepareBlock (double sampleRate, int maxBlockSize)
{
    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 2 };
    for (auto& c : convolutions)
        c.prepare (spec);

    for (auto* v : { &dryL, &dryR, &aL, &aR, &bL, &bR })
        v->assign ((size_t) maxBlockSize, 0.0f);

    for (auto& g : gains)
        g.prepare (sampleRate, 0.02);
    mix.prepare (sampleRate, 0.02);
    autoWet.prepare (sampleRate, 0.030);

    for (int slot = 0; slot < 2; ++slot)
        if (! getFilePath (slot).empty())
            loadImpulse (slot);

    lastLowCut = lastHighCut = -1.0f;
    resetBlock();
}

void IrLoaderBlock::resetBlock() noexcept
{
    for (auto& c : convolutions)
        c.reset();
    for (auto& f : highPass) f.reset();
    for (auto& f : lowPass)  f.reset();

    float l, r;
    balanceGains (getParam (PanA), l, r);
    const float a = dbToGain (getParam (LevelA));
    gains[0].setCurrentAndTarget (l * a);
    gains[1].setCurrentAndTarget (r * a);

    balanceGains (getParam (PanB), l, r);
    const float b = getParam (EnableB) > 0.5f ? dbToGain (getParam (LevelB)) : 0.0f;
    gains[2].setCurrentAndTarget (l * b);
    gains[3].setCurrentAndTarget (r * b);

    mix.setCurrentAndTarget (getParam (Mix) * 0.01f);
    autoWet.setCurrentAndTarget (isAutoBypassActive() ? 0.0f : 1.0f);
    updateFilters();
}

void IrLoaderBlock::updateFilters() noexcept
{
    const float low = getParam (LowCut), high = getParam (HighCut);
    if (! exactlyEqual (low, lastLowCut))
    {
        for (auto& f : highPass) f.setHighPass (getSampleRate(), low);
        lastLowCut = low;
    }
    if (! exactlyEqual (high, lastHighCut))
    {
        for (auto& f : lowPass) f.setLowPass (getSampleRate(), high);
        lastHighCut = high;
    }
}

void IrLoaderBlock::processBlock (float* left, float* right, int numSamples) noexcept
{
    autoWet.setTarget (isAutoBypassActive() ? 0.0f : 1.0f);
    if (! autoWet.isSmoothing() && exactlyEqual (autoWet.getCurrent(), 0.0f))
        return;   // decoupled by an End-to-End profile: dry, no convolution cost

    const auto bytes = sizeof (float) * (size_t) numSamples;
    std::memcpy (dryL.data(), left, bytes);
    std::memcpy (dryR.data(), right, bytes);

    auto convolve = [&] (int slot, float* l, float* r)
    {
        std::memcpy (l, left, bytes);
        std::memcpy (r, right, bytes);
        if (! loaded[(size_t) slot].load (std::memory_order_relaxed))
            return;   // no IR: transparent

        float* channels[] { l, r };
        juce::dsp::AudioBlock<float> block (channels, 2, (size_t) numSamples);
        convolutions[(size_t) slot].process (juce::dsp::ProcessContextReplacing<float> (block));
    };

    const bool useB = getParam (EnableB) > 0.5f;
    convolve (0, aL.data(), aR.data());
    if (useB)
        convolve (1, bL.data(), bR.data());

    float l, r;
    balanceGains (getParam (PanA), l, r);
    const float a = dbToGain (getParam (LevelA));
    gains[0].setTarget (l * a);
    gains[1].setTarget (r * a);
    balanceGains (getParam (PanB), l, r);
    const float b = useB ? dbToGain (getParam (LevelB)) : 0.0f;
    gains[2].setTarget (l * b);
    gains[3].setTarget (r * b);
    mix.setTarget (getParam (Mix) * 0.01f);
    updateFilters();

    // At their end stops the cuts are off, so an empty loader is fully transparent.
    const bool useLowCut  = getParam (LowCut)  > kParams[LowCut].minValue + 0.5f;
    const bool useHighCut = getParam (HighCut) < kParams[HighCut].maxValue - 0.5f;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto n = (size_t) i;
        float yl = aL[n] * gains[0].next();
        float yr = aR[n] * gains[1].next();
        const float gbl = gains[2].next(), gbr = gains[3].next();
        if (useB || gbl != 0.0f || gbr != 0.0f)
        {
            yl += (useB ? bL[n] : 0.0f) * gbl;
            yr += (useB ? bR[n] : 0.0f) * gbr;
        }

        if (useLowCut)
        {
            yl = highPass[0].process (yl);
            yr = highPass[1].process (yr);
        }
        if (useHighCut)
        {
            yl = lowPass[0].process (yl);
            yr = lowPass[1].process (yr);
        }

        const float m = mix.next() * autoWet.next();
        left[i]  = dryL[n] + (yl - dryL[n]) * m;
        right[i] = dryR[n] + (yr - dryR[n]) * m;
    }
}
} // namespace kumamp
