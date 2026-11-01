#pragma once

#include <array>
#include <cmath>

namespace kumamp
{
/** Musical note divisions for tempo-synced times and rates.
    The choice string doubles as the parameter "unit" (see ParamInfo). */
namespace tempo
{
    inline constexpr const char* kDivisionChoices =
        "4/1|2/1|1/1|1/2|1/2.|1/2T|1/4|1/4.|1/4T|1/8|1/8.|1/8T|1/16|1/16.|1/16T|1/32";
    inline constexpr int kNumDivisions = 16;
    inline constexpr int kQuarter = 6;   // index of "1/4"
    inline constexpr int kEighth  = 9;   // index of "1/8"

    /** Length of each division in quarter-note beats. */
    inline constexpr std::array<double, kNumDivisions> kBeats {
        16.0, 8.0, 4.0, 2.0, 3.0, 4.0 / 3.0, 1.0, 1.5, 2.0 / 3.0,
        0.5, 0.75, 1.0 / 3.0, 0.25, 0.375, 1.0 / 6.0, 0.125
    };

    inline constexpr std::array<const char*, kNumDivisions> kNames {
        "4/1", "2/1", "1/1", "1/2", "1/2.", "1/2T", "1/4", "1/4.", "1/4T",
        "1/8", "1/8.", "1/8T", "1/16", "1/16.", "1/16T", "1/32"
    };

    inline int clampDivision (float value) noexcept
    {
        const int i = (int) std::lround (value);
        return i < 0 ? 0 : (i >= kNumDivisions ? kNumDivisions - 1 : i);
    }

    inline double divisionMs (float division, double bpm) noexcept
    {
        return 60000.0 / (bpm > 1.0 ? bpm : 120.0) * kBeats[(size_t) clampDivision (division)];
    }

    /** One LFO cycle per division. */
    inline double divisionHz (float division, double bpm) noexcept
    {
        return 1000.0 / divisionMs (division, bpm);
    }
} // namespace tempo
} // namespace kumamp
