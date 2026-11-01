#include "BlockFactory.h"

#include "../DSP/Pitch/PitchShifterBlock.h"
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
        case BlockType::PitchShifter: return std::make_shared<PitchShifterBlock> (id);
        case BlockType::Volume:       return std::make_shared<VolumeBlock> (id);
        case BlockType::Splitter:
        case BlockType::Mixer:        return std::make_shared<RoutingParams> (id, type);
        default:                      return nullptr; // see README roadmap
    }
}

bool isImplemented (BlockType type) noexcept
{
    if ((size_t) type < registry().size() && registry()[(size_t) type] != nullptr)
        return true;

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
