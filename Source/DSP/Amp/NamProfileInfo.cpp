#include "NamProfileInfo.h"

#include <algorithm>
#include <cctype>

namespace kumamp
{
namespace
{
    std::string stringField (const nlohmann::json& j, const char* key)
    {
        const auto it = j.find (key);
        return it != j.end() && it->is_string() ? it->get<std::string>() : std::string {};
    }
} // namespace

ProfileKind classifyGearType (std::string_view gearType)
{
    std::string g (gearType);
    std::transform (g.begin(), g.end(), g.begin(), [] (unsigned char c) { return (char) std::tolower (c); });

    if (g.empty())
        return ProfileKind::Unknown;

    if (g.find ("cab") != std::string::npos || g.find ("full_rig") != std::string::npos
        || g.find ("fullrig") != std::string::npos || g == "full rig")
        return ProfileKind::EndToEnd;

    static constexpr std::string_view ampOnly[] = { "amp", "preamp", "pedal", "pedal_amp", "amp_pedal", "studio", "outboard" };
    for (auto known : ampOnly)
        if (g == known)
            return ProfileKind::AmpOnly;

    return ProfileKind::Unknown;
}

NamProfileInfo readNamProfileInfo (const nlohmann::json& metadata)
{
    NamProfileInfo info;
    if (! metadata.is_object())
        return info;

    info.gearType  = stringField (metadata, "gear_type");
    info.name      = stringField (metadata, "name");
    info.gearMake  = stringField (metadata, "gear_make");
    info.gearModel = stringField (metadata, "gear_model");
    info.modeledBy = stringField (metadata, "modeled_by");
    info.kind      = classifyGearType (info.gearType);

    if (const auto it = metadata.find ("loudness"); it != metadata.end() && it->is_number())
    {
        info.hasLoudness = true;
        info.loudnessDb = it->get<double>();
    }
    return info;
}

const char* profileKindName (ProfileKind kind) noexcept
{
    switch (kind)
    {
        case ProfileKind::AmpOnly:  return "Amp-Only";
        case ProfileKind::EndToEnd: return "End-to-End";
        case ProfileKind::Unknown:  break;
    }
    return "Desconocido";
}
} // namespace kumamp
