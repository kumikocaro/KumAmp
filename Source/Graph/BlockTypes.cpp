#include "BlockTypes.h"

#include <array>

namespace kumamp
{
namespace
{
    using C = BlockCategory;

    constexpr std::array<BlockTypeInfo, (size_t) BlockType::NumTypes> kTypes { {
        { BlockType::Unknown,         "unknown",          "Desconocido",      C::Utility },
        { BlockType::Tuner,           "tuner",            "Afinador",         C::Input },
        { BlockType::NoiseGate,       "noise_gate",       "Noise Gate",       C::Input },
        { BlockType::PitchShifter,    "pitch_shifter",    "Pitch Shifter",    C::PitchPre },
        { BlockType::Wah,             "wah",              "Wah",              C::PitchPre },
        { BlockType::AutoWah,         "auto_wah",         "Auto-Wah",         C::PitchPre },
        { BlockType::Octaver,         "octaver",          "Octaver",          C::PitchPre },
        { BlockType::NamAmp,          "nam_amp",          "NAM Amp",          C::Amp },
        { BlockType::IrLoader,        "ir_loader",        "IR Loader",        C::Cab },
        { BlockType::Compressor,      "compressor",       "Compresor",        C::Dynamics },
        { BlockType::ParametricEq,    "parametric_eq",    "EQ Parametrico",   C::Dynamics },
        { BlockType::GraphicEq,       "graphic_eq",       "EQ Grafico",       C::Dynamics },
        { BlockType::TransientShaper, "transient_shaper", "Transient Shaper", C::Dynamics },
        { BlockType::Chorus,          "chorus",           "Chorus",           C::Modulation },
        { BlockType::Flanger,         "flanger",          "Flanger",          C::Modulation },
        { BlockType::Phaser,          "phaser",           "Phaser",           C::Modulation },
        { BlockType::Tremolo,         "tremolo",          "Tremolo",          C::Modulation },
        { BlockType::Rotary,          "rotary",           "Rotary",           C::Modulation },
        { BlockType::Univibe,         "univibe",          "Univibe",          C::Modulation },
        { BlockType::Bitcrusher,      "bitcrusher",       "Bitcrusher",       C::Modulation },
        { BlockType::TapeDelay,       "tape_delay",       "Tape Delay",       C::Time },
        { BlockType::AnalogDelay,     "analog_delay",     "Analog Delay",     C::Time },
        { BlockType::DigitalDelay,    "digital_delay",    "Digital Delay",    C::Time },
        { BlockType::ReverseDelay,    "reverse_delay",    "Reverse Delay",    C::Time },
        { BlockType::PlateReverb,     "plate_reverb",     "Plate Reverb",     C::Time },
        { BlockType::SpringReverb,    "spring_reverb",    "Spring Reverb",    C::Time },
        { BlockType::HallReverb,      "hall_reverb",      "Hall Reverb",      C::Time },
        { BlockType::ShimmerReverb,   "shimmer_reverb",   "Shimmer Reverb",   C::Time },
        { BlockType::CloudReverb,     "cloud_reverb",     "Cloud Reverb",     C::Time },
        { BlockType::Volume,          "volume",           "Volumen",          C::Utility },
        { BlockType::Splitter,        "splitter",         "Splitter",         C::Routing },
        { BlockType::Mixer,           "mixer",            "Mixer",            C::Routing },
    } };

    constexpr bool tableMatchesEnum()
    {
        for (size_t i = 0; i < kTypes.size(); ++i)
            if ((size_t) kTypes[i].type != i)
                return false;
        return true;
    }
    static_assert (tableMatchesEnum(), "kTypes must be in BlockType order");
} // namespace

const BlockTypeInfo& getBlockTypeInfo (BlockType type) noexcept
{
    const auto index = (size_t) type;
    return index < kTypes.size() ? kTypes[index] : kTypes[0];
}

BlockType blockTypeFromId (std::string_view id) noexcept
{
    for (const auto& info : kTypes)
        if (id == info.id)
            return info.type;
    return BlockType::Unknown;
}
} // namespace kumamp
