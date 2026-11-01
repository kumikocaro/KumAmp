#pragma once

#include <cstdint>
#include <string_view>

namespace kumamp
{
/** Every block the pedalboard will offer. The numeric value is never
    serialised (the string id is), so the enum can be reordered freely. */
enum class BlockType : std::uint16_t
{
    Unknown = 0,

    // Input
    Tuner, NoiseGate,
    // Pitch & Pre-FX
    PitchShifter, Wah, AutoWah, Octaver,
    // Core
    NamAmp,
    // Cab
    IrLoader,
    // Post-amp dynamics / EQ
    Compressor, ParametricEq, GraphicEq, TransientShaper,
    // Modulation
    Chorus, Flanger, Phaser, Tremolo, Rotary, Univibe, Bitcrusher,
    // Time
    TapeDelay, AnalogDelay, DigitalDelay, ReverseDelay,
    PlateReverb, SpringReverb, HallReverb, ShimmerReverb, CloudReverb,
    // Utility / routing
    Volume, Splitter, Mixer,

    NumTypes
};

enum class BlockCategory : std::uint8_t
{
    Input, PitchPre, Amp, Cab, Dynamics, Modulation, Time, Utility, Routing
};

struct BlockTypeInfo
{
    BlockType type;
    const char* id;          // stable, used for presets
    const char* displayName;
    BlockCategory category;
};

const BlockTypeInfo& getBlockTypeInfo (BlockType type) noexcept;
BlockType blockTypeFromId (std::string_view id) noexcept;
} // namespace kumamp
