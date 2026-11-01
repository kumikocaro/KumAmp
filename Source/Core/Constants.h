#pragma once

#include <cstdint>

namespace kumamp
{
using StoreId = std::uint32_t;
using NodeId  = std::uint32_t;

inline constexpr StoreId kInvalidStore = 0;
inline constexpr NodeId  kInvalidNode  = 0;

inline constexpr int kMaxParams      = 64; // parameters per block / routing node (8-band EQ uses 49)
inline constexpr int kMaxBranches    = 4;  // max outputs of a Splitter / inputs of a Mixer
inline constexpr int kNumScenes      = 4;  // A, B, C, D
inline constexpr int kMaxStores      = 64; // blocks + routing nodes captured per scene
inline constexpr int kNumMacros      = 4;
inline constexpr int kGraphChannels  = 2;  // the grid always runs in stereo
} // namespace kumamp
