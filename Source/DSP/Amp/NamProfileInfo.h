#pragma once

#include <string>
#include <string_view>

#include "json.hpp"

namespace kumamp
{
/** What a NAM capture contains. End-to-End profiles already include the
    cabinet / mic, so a following IR loader must step aside. */
enum class ProfileKind : int
{
    Unknown  = 0,  // no usable metadata -> treated as Amp-Only
    AmpOnly  = 1,
    EndToEnd = 2
};

struct NamProfileInfo
{
    ProfileKind kind = ProfileKind::Unknown;
    std::string gearType, name, gearMake, gearModel, modeledBy;
    bool hasLoudness = false;
    double loudnessDb = 0.0;
};

/**
    Reads the NAM trainer / Tone3000 metadata block.

    `gear_type` values written by the trainer: amp, pedal, pedal_amp, amp_cab,
    amp_pedal_cab, preamp, studio. Anything containing "cab" (or "full_rig",
    used by some capture tools) is End-to-End.
*/
NamProfileInfo readNamProfileInfo (const nlohmann::json& metadata);

ProfileKind classifyGearType (std::string_view gearType);

const char* profileKindName (ProfileKind kind) noexcept;
} // namespace kumamp
