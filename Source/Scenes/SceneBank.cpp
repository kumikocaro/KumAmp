#include "SceneBank.h"

#include <algorithm>

namespace kumamp
{
Scene SceneBank::capture (const std::vector<const ParamStore*>& stores) noexcept
{
    Scene scene;
    scene.valid = true;

    for (const auto* store : stores)
    {
        if (store == nullptr || scene.numStores >= kMaxStores)
            continue;

        auto& snap = scene.stores[(size_t) scene.numStores++];
        snap.id = store->getId();
        snap.bypassed = store->isBypassed();
        snap.numParams = std::min (store->getNumParams(), kMaxParams);
        for (int p = 0; p < snap.numParams; ++p)
            snap.values[(size_t) p] = store->getParam (p);
    }

    return scene;
}
} // namespace kumamp
