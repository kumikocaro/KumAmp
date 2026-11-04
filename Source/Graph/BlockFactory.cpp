#include "BlockFactory.h"

#include "../DSP/Dynamics/DynamicsBlocks.h"
#include "../DSP/Modulation/ModulationBlocks.h"
#include "../DSP/Pitch/CharangoBlock.h"
#include "../DSP/Pitch/GuitarSynthBlock.h"
#include "../DSP/Pitch/PitchShifterBlock.h"
#include "../DSP/Pitch/PreFxBlocks.h"
#include "../DSP/Pitch/SitarBlock.h"
#include "../DSP/Time/DelayBlocks.h"
#include "../DSP/Time/ReverbBlocks.h"
#include "../DSP/Routing/RoutingParams.h"
#include "../DSP/Utility/VolumeBlock.h"

#include <array>

namespace kumamp
{
namespace
{
    std::array<StoreCreator, (size_t) BlockType::NumTypes>& registry()
    {
        static std::array<StoreCreator, (size_t) BlockType::NumTypes> creators {};
        return creators;
    }
} // namespace

void registerStoreCreator (BlockType type, StoreCreator creator)
{
    if ((size_t) type < registry().size())
        registry()[(size_t) type] = creator;
}

std::shared_ptr<ParamStore> createStore (BlockType type, StoreId id)
{
    if ((size_t) type < registry().size())
        if (auto creator = registry()[(size_t) type])
            return creator (id);

    switch (type)
    {
        case BlockType::NoiseGate:       return std::make_shared<NoiseGateBlock> (id);
        case BlockType::PitchShifter:    return std::make_shared<PitchShifterBlock> (id);
        case BlockType::Wah:             return std::make_shared<WahBlock> (id);
        case BlockType::AutoWah:         return std::make_shared<AutoWahBlock> (id);
        case BlockType::Octaver:         return std::make_shared<OctaverBlock> (id);
        case BlockType::GuitarSynth:     return std::make_shared<GuitarSynthBlock> (id);
        case BlockType::Sitar:           return std::make_shared<SitarBlock> (id);
        case BlockType::Charango:        return std::make_shared<CharangoBlock> (id);
        case BlockType::Compressor:      return std::make_shared<CompressorBlock> (id);
        case BlockType::ParametricEq:    return std::make_shared<ParametricEqBlock> (id);
        case BlockType::GraphicEq:       return std::make_shared<GraphicEqBlock> (id);
        case BlockType::TransientShaper: return std::make_shared<TransientShaperBlock> (id);
        case BlockType::Chorus:          return std::make_shared<ChorusBlock> (id);
        case BlockType::Flanger:         return std::make_shared<FlangerBlock> (id);
        case BlockType::Phaser:          return std::make_shared<PhaserBlock> (id);
        case BlockType::Tremolo:         return std::make_shared<TremoloBlock> (id);
        case BlockType::Rotary:          return std::make_shared<RotaryBlock> (id);
        case BlockType::Univibe:         return std::make_shared<UnivibeBlock> (id);
        case BlockType::Bitcrusher:      return std::make_shared<BitcrusherBlock> (id);
        case BlockType::TapeDelay:       return std::make_shared<TapeDelayBlock> (id);
        case BlockType::AnalogDelay:     return std::make_shared<AnalogDelayBlock> (id);
        case BlockType::DigitalDelay:    return std::make_shared<DigitalDelayBlock> (id);
        case BlockType::ReverseDelay:    return std::make_shared<ReverseDelayBlock> (id);
        case BlockType::PlateReverb:     return std::make_shared<PlateReverbBlock> (id);
        case BlockType::SpringReverb:    return std::make_shared<SpringReverbBlock> (id);
        case BlockType::HallReverb:      return std::make_shared<HallReverbBlock> (id);
        case BlockType::ShimmerReverb:   return std::make_shared<ShimmerReverbBlock> (id);
        case BlockType::CloudReverb:     return std::make_shared<CloudReverbBlock> (id);
        case BlockType::Volume:          return std::make_shared<VolumeBlock> (id);
        case BlockType::Splitter:
        case BlockType::Mixer:           return std::make_shared<RoutingParams> (id, type);
        default:                         return nullptr;   // NamAmp / IrLoader / Overdrive register at runtime; Tuner is global
    }
}

bool isImplemented (BlockType type) noexcept
{
    if ((size_t) type < registry().size() && registry()[(size_t) type] != nullptr)
        return true;

    switch (type)
    {
        case BlockType::Unknown:
        case BlockType::Tuner:      // the tuner is a global input feature, not a grid block
        case BlockType::NamAmp:     // registered by registerJuceBlocks()
        case BlockType::IrLoader:
        case BlockType::Overdrive:
        case BlockType::NumTypes:
            return false;
        default:
            return true;
    }
}
} // namespace kumamp
