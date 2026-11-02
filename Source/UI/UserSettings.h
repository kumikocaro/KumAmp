#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <memory>

namespace kumamp
{
/** Per-user preferences shared by every KumAmp instance (UI scale, preset author / folder). */
inline std::unique_ptr<juce::PropertiesFile> openUserSettings()
{
    juce::PropertiesFile::Options options;
    options.applicationName     = "KumAmp";
    options.filenameSuffix      = ".settings";
    options.folderName          = "KumAmp";
    options.osxLibrarySubFolder = "Application Support";
    return std::make_unique<juce::PropertiesFile> (options);
}
} // namespace kumamp
