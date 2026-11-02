#include "PitchShifterBlock.h"
#include "../../Core/DspMath.h"

#include <cmath>
#include <iterator>

namespace kumamp
{
namespace
{
    constexpr ParamInfo kParams[] = {
        { "semitones", "Semitonos", -12.0f,   12.0f,   0.0f, "st", true  },
        { "cents",     "Cents",     -100.0f, 100.0f,   0.0f, "ct", false },
        { "mix",       "Mezcla",       0.0f, 100.0f, 100.0f, "%",  false },
        { "window",    "Ventana",     20.0f,  60.0f,  45.0f, "ms", false },   // longest splice jump
        { "glide",     "Glide",        0.0f, 1000.0f, 30.0f, "ms", false },
        { "instrument", "Instrumento",  0.0f,    1.0f,  0.0f, "Guitarra|Bajo", true },
    };
    static_assert (std::size (kParams) == PitchShifterBlock::NumParams);
} // namespace

PitchShifterBlock::PitchShifterBlock (StoreId id)
    : DSPBlock (id, BlockType::PitchShifter, kParams, NumParams)
{
}

void PitchShifterBlock::prepareBlock (double sampleRate, int maxBlockSize)
{
    shifter.prepare (sampleRate);
    wetLeft.assign ((size_t) maxBlockSize, 0.0f);
    wetRight.assign ((size_t) maxBlockSize, 0.0f);
    wetAmount.prepare (sampleRate, 0.020);
    resetBlock();
}

void PitchShifterBlock::resetBlock() noexcept
{
    const float semis = getParam (Semitones) + getParam (Cents) * 0.01f;
    shifter.setRatio (semitonesToRatio (semis), 0.0f);
    shifter.reset();
    wetAmount.setCurrentAndTarget (std::abs (semis) < 1.0e-3f ? 0.0f : getParam (Mix) * 0.01f);
}

void PitchShifterBlock::processBlock (float* left, float* right, int numSamples) noexcept
{
    const float semis = getParam (Semitones) + getParam (Cents) * 0.01f;
    shifter.setGrainMs (getParam (WindowMs));
    shifter.setInstrument (getParam (Instrument) >= 0.5f ? WsolaPitchShifter::Instrument::Bass
                                                         : WsolaPitchShifter::Instrument::Guitar);
    shifter.setRatio (semitonesToRatio (semis), getParam (GlideMs));

    // Unity and done gliding -> fade the wet path out to get zero latency.
    const bool unity = std::abs (semis) < 1.0e-3f && ! shifter.isGliding();
    wetAmount.setTarget (unity ? 0.0f : getParam (Mix) * 0.01f);

    shifter.process (left, right, wetLeft.data(), wetRight.data(), numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        const float m = wetAmount.next();
        left[i]  += (wetLeft[(size_t) i]  - left[i])  * m;
        right[i] += (wetRight[(size_t) i] - right[i]) * m;
    }
}
} // namespace kumamp
