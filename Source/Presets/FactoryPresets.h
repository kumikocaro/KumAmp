#pragma once

#include <juce_core/juce_core.h>

namespace kumamp
{
/**
    Reference presets, written once as ordinary .kumamp files under
    <presets folder>/Referencia/<category>. Deleting or editing them is safe:
    they are not written again.

    NAM models are NOT included (TONE3000's terms forbid bundling its tones):
    a preset only names the model file and its comment says where to get it.
    PresetManager finds the file by name in Documents/KumAmp; without it the
    rest of the chain loads and the amp block stays without a model.
*/
namespace factorypresets
{
    /** Writes the presets unless this folder already got them. Returns how many were written. */
    int install (const juce::File& presetsFolder);

    /** The presets behind the SIMP view's icons, written on first use to <folder>/Simp. */
    int getNumSimpPresets() noexcept;
    juce::String getSimpPresetName (int preset);
    juce::File writeSimpPreset (const juce::File& presetsFolder, int preset);
    /** The model it needs (file name, and what to look for on TONE3000); empty if none. */
    juce::String getSimpModelFile (int preset);
    juce::String getSimpModelTitle (int preset);
}
} // namespace kumamp
