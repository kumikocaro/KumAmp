#pragma once

#include "../Graph/BlockTypes.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace kumamp::theme
{
inline const juce::Colour background  { 0xff0b0d14 };
inline const juce::Colour panel       { 0xff12152a };
inline const juce::Colour panelRaised { 0xff1a1e33 };
inline const juce::Colour border      { 0xff262b45 };
inline const juce::Colour text        { 0xffe7e9f3 };
inline const juce::Colour textDim     { 0xff8b90a8 };
inline const juce::Colour accent      { 0xff8b5cf6 };
inline const juce::Colour accentLight { 0xffa78bfa };
inline const juce::Colour good        { 0xff4ade80 };
inline const juce::Colour warn        { 0xfff59e0b };
inline const juce::Colour bad         { 0xffef4444 };
inline const juce::Colour cable       { 0xff3a4062 };

/** Accent colour of a block family (tile border, icon, glow). */
inline juce::Colour categoryColour (BlockType type)
{
    switch (type)
    {
        case BlockType::ParametricEq:
        case BlockType::GraphicEq:      return juce::Colour (0xff2dd4bf);   // EQ: teal
        default: break;
    }

    switch (getBlockTypeInfo (type).category)
    {
        case BlockCategory::Input:      return juce::Colour (0xff22d3ee);
        case BlockCategory::PitchPre:   return juce::Colour (0xffa855f7);
        case BlockCategory::Drive:      return juce::Colour (0xff84cc16);   // Screamer green
        case BlockCategory::Amp:        return juce::Colour (0xfff87171);
        case BlockCategory::Cab:        return juce::Colour (0xfff59e0b);
        case BlockCategory::Dynamics:   return juce::Colour (0xff34d399);
        case BlockCategory::Modulation: return juce::Colour (0xff38bdf8);
        case BlockCategory::Time:       return juce::Colour (0xff818cf8);
        case BlockCategory::Utility:
        case BlockCategory::Routing:    break;
    }
    return juce::Colour (0xff94a3b8);
}

enum class Icon
{
    Pitch, Wave, Octave, Dynamics, Eq, Amp, Speaker, Sine, Delay, Reverb, Utility,
    Plus, Save, Undo, Redo, Menu, ChevronLeft, ChevronRight, Power, File, More, Info, Trash, Logo, Expand, Synth, Drive
};

Icon iconFor (BlockType type) noexcept;

/** Draws a stroked line icon fitted into `area`. */
void drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour, float stroke = 1.7f);

/** The palette groups shown under the grid (Pitch, Dynamics, EQ, NAM...). */
struct PaletteGroup
{
    const char* name;
    Icon icon;
    std::vector<BlockType> types;
};
const std::vector<PaletteGroup>& paletteGroups();

/** Card background used by every section. */
inline void drawCard (juce::Graphics& g, juce::Rectangle<float> area, float radius = 12.0f)
{
    g.setColour (panel);
    g.fillRoundedRectangle (area, radius);
    g.setColour (border);
    g.drawRoundedRectangle (area.reduced (0.5f), radius, 1.0f);
}

inline void drawChip (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& label, juce::Colour colour)
{
    g.setColour (colour.withAlpha (0.16f));
    g.fillRoundedRectangle (area, area.getHeight() * 0.5f);
    g.setColour (colour.withAlpha (0.9f));
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (label, area, juce::Justification::centred);
}
} // namespace kumamp::theme
