#pragma once

#include "ParamStore.h"

#include <memory>

namespace kumamp
{
/** Creates the ParamStore for a block type: a DSPBlock for audio blocks, a
    RoutingParams for Splitter / Mixer. Returns nullptr for types that are
    declared but not implemented yet. Message thread only. */
std::shared_ptr<ParamStore> createStore (BlockType type, StoreId id);

bool isImplemented (BlockType type) noexcept;
} // namespace kumamp
