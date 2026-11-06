#pragma once

#include "../Graph/BlockTypes.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace kumamp::theme
{
inline const juce::Colour ink         { 0xff0a0a0a };
inline const juce::Colour bone        { 0xffe7e1ce };
inline const juce::Colour background  = ink;
inline const juce::Colour panel       = ink;
inline const juce::Colour panelRaised { 0xff1b1a17 };
inline const juce::Colour border      { 0xff49473f };
inline const juce::Colour text        = bone;
inline const juce::Colour textDim     { 0xff938d7e };
inline const juce::Colour accent      { 0xffed6243 };
inline const juce::Colour accentLight = accent;
inline const juce::Colour good        { 0xffa8b45a };
inline const juce::Colour warn        { 0xffe0a93b };
inline const juce::Colour bad         = accent;
inline const juce::Colour cable       = ink;

/** The text colour that reads on a given solid fill. */
inline juce::Colour textOn (juce::Colour fill)
{
    return background.overlaidWith (fill).getPerceivedBrightness() > 0.42f ? ink : bone;
}

/** Interface typefaces, embedded so Mac and Windows look the same. Keep one alive while the UI exists. */
struct Typefaces
{
    Typefaces();
    juce::Typeface::Ptr regular, semiBold, black, mono, monoMedium;
};

/** Headlines: Jost Black. */
juce::FontOptions display (float height);
/** Technical labels and values: IBM Plex Mono. */
juce::FontOptions mono (float height, bool medium = false);

/** Accent colour of a block family (tile border, icon, glow). */
inline juce::Colour categoryColour (BlockType type)
{
    switch (type)
    {
        case BlockType::ParametricEq:
        case BlockType::GraphicEq:      return juce::Colour (0xff3f9c93);   // EQ: teal
        default: break;
    }

    switch (getBlockTypeInfo (type).category)
    {
        case BlockCategory::Input:      return juce::Colour (0xff7fb2b0);
        case BlockCategory::PitchPre:   return juce::Colour (0xffb58aa5);
        case BlockCategory::Experimental: return juce::Colour (0xffe59a8c);   // salmon
        case BlockCategory::Drive:      return juce::Colour (0xffa8b45a);   // Screamer green
        case BlockCategory::Amp:        return accent;
        case BlockCategory::Cab:        return juce::Colour (0xffe0a93b);
        case BlockCategory::Dynamics:   return juce::Colour (0xff6fa88a);
        case BlockCategory::Modulation: return juce::Colour (0xff6f9fb5);
        case BlockCategory::Time:       return juce::Colour (0xff8e95b8);
        case BlockCategory::Utility:
        case BlockCategory::Routing:    break;
    }
    return textDim;
}

enum class Icon
{
    Pitch, Wave, Octave, Dynamics, Eq, Amp, Speaker, Sine, Delay, Reverb, Utility,
    Plus, Save, Undo, Redo, Menu, ChevronLeft, ChevronRight, Power, File, More, Info, Trash, Logo, Expand, Synth, Drive, Strings, Bolt, Note
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
/** The palette group a block belongs to (its variants: blocks it can be swapped for), or nullptr. */
const PaletteGroup* paletteGroupOf (BlockType type);

/** Card background used by every section: a hairline box under a heavy bone rule. */
inline void drawCard (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (panel);
    g.fillRect (area);
    g.setColour (border);
    g.drawRect (area, 1.0f);
    g.setColour (bone);
    g.fillRect (area.withHeight (3.0f));
}

inline void drawChip (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& label, juce::Colour colour)
{
    g.setColour (colour);
    g.drawRect (area.reduced (0.5f), 1.0f);
    g.setFont (mono (11.0f));
    g.drawText (label.toUpperCase(), area, juce::Justification::centred);
}
} // namespace kumamp::theme
