#pragma once

#include <algorithm>
#include <cmath>

namespace kumamp
{
inline constexpr float kPi = 3.14159265358979323846f;

/** -60 dB or lower is treated as silence so faders can fully close. */
inline float dbToGain (float db) noexcept
{
    return db <= -59.9f ? 0.0f : std::pow (10.0f, db * 0.05f);
}

/** Stereo balance law: centre = unity on both sides, never boosts. */
inline void balanceGains (float pan, float& left, float& right) noexcept
{
    pan   = std::clamp (pan, -1.0f, 1.0f);
    left  = std::min (1.0f, 1.0f - pan);
    right = std::min (1.0f, 1.0f + pan);
}

inline float semitonesToRatio (float semitones) noexcept
{
    return std::exp2 (semitones / 12.0f);
}
} // namespace kumamp
