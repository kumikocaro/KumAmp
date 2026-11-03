#pragma once

#include <string>
#include <vector>

namespace kumamp::cabs
{
/**
    KumAmp's own cabinet IRs, synthesised (no third-party recordings, so they
    ship under the project licence).

    Each IR is built from a magnitude response (speaker resonance and cone
    break-up, cabinet loading, box modes, mic voicing) turned into a
    minimum-phase impulse with the real cepstrum, plus the early reflections
    of the cabinet (back wave for open backs, internal ones for closed). The
    4x12 modern voicing is calibrated against measured close-mic 4x12 IRs.

    Presets refer to them by a pseudo path, "kumamp:cab/<cab>/<mic>", so they
    work on any machine without files.
*/
struct MicInfo
{
    const char* id;
    const char* name;
};

struct CabInfo
{
    const char* id;
    const char* size;    // "4x12" (menu group)
    const char* name;    // "Britanico moderno (cerrado)"
    bool bass;
};

const std::vector<CabInfo>& cabinets();
const std::vector<MicInfo>& mics();

inline constexpr const char* kPathPrefix = "kumamp:cab/";

bool isFactoryPath (const std::string& path);
std::string pathFor (int cab, int mic);
/** Same, by ids ("4x12-moderno", "din-centro"); empty if unknown. */
std::string pathFor (const std::string& cabId, const std::string& micId);
/** False if the path is not a known factory IR. */
bool parsePath (const std::string& path, int& cab, int& mic);
/** "4x12 Britanico moderno - Dinamico centro" */
std::string displayName (const std::string& path);

/** Mono IR at `sampleRate` (normalised to a 0.5 peak). Empty if out of range. */
std::vector<float> render (int cab, int mic, double sampleRate = 48000.0);
} // namespace kumamp::cabs
