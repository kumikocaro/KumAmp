#pragma once

#include "BlockTypes.h"
#include "../Core/Constants.h"

#include <array>
#include <atomic>
#include <string>
#include <string_view>
#include <utility>

namespace kumamp
{
struct ParamInfo
{
    const char* id;    // stable, used for presets
    const char* name;  // display name
    float minValue, maxValue, defaultValue;
    const char* unit;
    bool isDiscrete;
};

/**
    Thread-safe parameter storage shared by DSP blocks and routing nodes.

    Every value lives in a std::atomic<float>, so the GUI, the scene engine,
    host macros and the audio thread can all read/write without locks. The
    DSP side reads the atomics once per block and smooths internally.

    Scenes snapshot and restore exactly this state (values + bypass), which is
    why scene changes are gapless: they only move smoothing targets.
*/
class ParamStore
{
public:
    ParamStore (StoreId id, BlockType type, const ParamInfo* table, int numParams) noexcept;
    virtual ~ParamStore() = default;

    ParamStore (const ParamStore&) = delete;
    ParamStore& operator= (const ParamStore&) = delete;

    StoreId getId() const noexcept     { return id; }
    BlockType getType() const noexcept { return type; }

    int getNumParams() const noexcept  { return numParams; }
    const ParamInfo& getParamInfo (int index) const noexcept { return table[index]; }
    int findParam (std::string_view paramId) const noexcept;

    // Any thread.
    void  setParam (int index, float value) noexcept;
    float getParam (int index) const noexcept;
    void  setBypassed (bool shouldBeBypassed) noexcept { bypassed.store (shouldBeBypassed, std::memory_order_relaxed); }
    bool  isBypassed() const noexcept                  { return bypassed.load (std::memory_order_relaxed); }
    void  resetToDefaults() noexcept;
    /** False: the parameter describes the block itself, so scenes leave it alone. Any thread. */
    virtual bool isSceneParam (int /*index*/) const noexcept { return true; }

    // Message thread only (not touched by DSP).
    void setName (std::string newName) { name = std::move (newName); }
    const std::string& getName() const noexcept { return name; }

private:
    const StoreId id;
    const BlockType type;
    const ParamInfo* const table;
    const int numParams;

    std::array<std::atomic<float>, kMaxParams> values {};
    std::atomic<bool> bypassed { false };
    std::string name;
};
} // namespace kumamp
