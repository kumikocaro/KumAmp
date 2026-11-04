#pragma once

#include "../Graph/ParamStore.h"

#include <array>
#include <vector>

namespace kumamp
{
struct StoreSnapshot
{
    StoreId id = kInvalidStore;
    bool bypassed = false;
    int numParams = 0;
    std::array<float, kMaxParams> values {};
};

struct Scene
{
    std::array<StoreSnapshot, kMaxStores> stores {};
    int numStores = 0;
    bool valid = false;
};

/**
    Scenes A-D: snapshots of every ParamStore (values + bypass) of the current
    grid. Topology is shared by all scenes; a scene only moves parameter and
    bypass targets, which every block / routing node smooths, so switching is
    gapless and delay/reverb tails keep ringing.

    The bank is a fixed-size POD-like value. The engine publishes immutable
    copies to the audio thread through an RTObjectPublisher.
*/
struct SceneBank
{
    std::array<Scene, kNumScenes> scenes {};

    /** Message thread. Stores beyond kMaxStores are ignored. */
    static Scene capture (const std::vector<const ParamStore*>& stores) noexcept;

    /** Any thread, RT-safe. `find` maps StoreId -> ParamStore* (may return nullptr). */
    template <typename FindStore>
    static void apply (const Scene& scene, FindStore&& find) noexcept
    {
        if (! scene.valid)
            return;

        for (int i = 0; i < scene.numStores; ++i)
        {
            const auto& snap = scene.stores[(size_t) i];
            if (ParamStore* store = find (snap.id))
            {
                for (int p = 0; p < snap.numParams && p < store->getNumParams(); ++p)
                    if (store->isSceneParam (p))
                        store->setParam (p, snap.values[(size_t) p]);
                store->setBypassed (snap.bypassed);
            }
        }
    }
};
} // namespace kumamp
