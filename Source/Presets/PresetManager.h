#pragma once

#include "../Engine/GridEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

namespace kumamp
{
/**
    Preset files (.kumamp, XML) in a user folder, for building author presets.

    Default folder: Documents/KumAmp/Presets. Sub-folders are categories (an
    author's pack, "Clean", "Metal"...). A preset holds the whole rig: grid,
    blocks, the four scenes and macros, plus the global gate, pitch and output
    level. Input gain, tuner and MIDI settings belong to the player's setup and
    are left alone.

    Model / IR files are referenced, not copied. Each path is stored absolute
    and relative to the preset file; on load a missing file is looked up by the
    relative path, then by file name inside the preset's folder tree and in
    Documents/KumAmp. So a pack of presets plus its .nam / .wav files can be
    zipped and shared.

    Message thread only.
*/
class PresetManager
{
public:
    struct Info
    {
        juce::File file;
        juce::String name, author, category, comment;
        juce::Time created;
    };

    PresetManager (GridEngine& engine, juce::AudioProcessorValueTreeState& parameters);

    static juce::File getDefaultFolder();
    static constexpr const char* kExtension = ".kumamp";

    juce::File getFolder() const { return folder; }
    void setFolder (const juce::File& newFolder) { folder = newFolder; }

    /** Every preset under the folder, sorted by category then name. */
    std::vector<Info> scan() const;

    /** Target file for a name / category (illegal characters replaced). */
    juce::File fileFor (const juce::String& name, const juce::String& category) const;

    /** Writes the current state; overwrites an existing file. */
    juce::Result save (const juce::String& name, const juce::String& author,
                       const juce::String& category, const juce::String& comment);
    juce::Result load (const juce::File& file);

    /** Moves the file to the trash (recoverable), not a permanent delete. */
    bool remove (const juce::File& file);

    static Info readInfo (const juce::File& file);

    const juce::String& getCurrentName() const noexcept { return currentName; }
    const juce::File& getCurrentFile() const noexcept { return currentFile; }

    /** Global parameters stored with a preset. */
    static const juce::StringArray& globalParameterIds();

private:
    void relocateFiles (juce::ValueTree grid, const juce::File& presetFile) const;
    juce::File findMissing (const juce::String& absolute, const juce::String& relative, const juce::File& presetFile) const;

    GridEngine& engine;
    juce::AudioProcessorValueTreeState& parameters;
    juce::File folder;
    juce::String currentName;
    juce::File currentFile;
};
} // namespace kumamp
