#include "BlockFactory.h"

#include "../DSP/Pitch/PitchShifterBlock.h"
#include "../DSP/Routing/RoutingParams.h"
#include "../DSP/Utility/VolumeBlock.h"

namespace kumamp
{
std::shared_ptr<ParamStore> createStore (BlockType type, StoreId id)
{
    switch (type)
    {
        case BlockType::PitchShifter: return std::make_shared<PitchShifterBlock> (id);
        case BlockType::Volume:       return std::make_shared<VolumeBlock> (id);
        case BlockType::Splitter:
        case BlockType::Mixer:        return std::make_shared<RoutingParams> (id, type);
        default:                      return nullptr; // see README roadmap
    }
}

bool isImplemented (BlockType type) noexcept
{
    switch (type)
    {
        case BlockType::PitchShifter:
        case BlockType::Volume:
        case BlockType::Splitter:
        case BlockType::Mixer:
            return true;
        default:
            return false;
    }
}
} // namespace kumamp
