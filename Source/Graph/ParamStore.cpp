#include "ParamStore.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace kumamp
{
ParamStore::ParamStore (StoreId storeId, BlockType blockType, const ParamInfo* paramTable, int count) noexcept
    : id (storeId), type (blockType), table (paramTable), numParams (std::clamp (count, 0, kMaxParams))
{
    assert (count <= kMaxParams);
    resetToDefaults();
}

int ParamStore::findParam (std::string_view paramId) const noexcept
{
    for (int i = 0; i < numParams; ++i)
        if (paramId == table[i].id)
            return i;
    return -1;
}

void ParamStore::setParam (int index, float value) noexcept
{
    if (index < 0 || index >= numParams || ! std::isfinite (value))
        return;

    const auto& info = table[index];
    values[(size_t) index].store (std::clamp (value, info.minValue, info.maxValue), std::memory_order_relaxed);
}

float ParamStore::getParam (int index) const noexcept
{
    if (index < 0 || index >= numParams)
        return 0.0f;
    return values[(size_t) index].load (std::memory_order_relaxed);
}

void ParamStore::resetToDefaults() noexcept
{
    for (int i = 0; i < numParams; ++i)
        values[(size_t) i].store (table[i].defaultValue, std::memory_order_relaxed);
}
} // namespace kumamp
