#include "Theme.h"
#include <algorithm>
#include <cmath>

namespace kumamp::theme
{
Icon iconFor (BlockType type) noexcept
{
    switch (type)
    {
        case BlockType::PitchShifter:    return Icon::Pitch;
        case BlockType::Wah:
        case BlockType::AutoWah:         return Icon::Wave;
        case BlockType::Octaver:         return Icon::Octave;
        case BlockType::GuitarSynth:     return Icon::Synth;
        case BlockType::NoiseGate:
        case BlockType::Compressor:
        case BlockType::TransientShaper: return Icon::Dynamics;
        case BlockType::ParametricEq:
        case BlockType::GraphicEq:       return Icon::Eq;
        case BlockType::NamAmp:          return Icon::Amp;
        case BlockType::IrLoader:        return Icon::Speaker;
        case BlockType::Chorus:
        case BlockType::Flanger:
        case BlockType::Phaser:
        case BlockType::Tremolo:
        case BlockType::Rotary:
        case BlockType::Univibe:
        case BlockType::Bitcrusher:      return Icon::Sine;
        case BlockType::TapeDelay:
        case BlockType::AnalogDelay:
        case BlockType::DigitalDelay:
        case BlockType::ReverseDelay:    return Icon::Delay;
        case BlockType::PlateReverb:
        case BlockType::SpringReverb:
        case BlockType::HallReverb:
        case BlockType::ShimmerReverb:
        case BlockType::CloudReverb:     return Icon::Reverb;
        default:                         return Icon::Utility;
    }
}

const std::vector<PaletteGroup>& paletteGroups()
{
    static const std::vector<PaletteGroup> groups {
        { "Pitch",      Icon::Pitch,    { BlockType::PitchShifter, BlockType::Wah, BlockType::AutoWah, BlockType::Octaver, BlockType::GuitarSynth } },
        { "Dynamics",   Icon::Dynamics, { BlockType::NoiseGate, BlockType::Compressor, BlockType::TransientShaper } },
        { "EQ",         Icon::Eq,       { BlockType::ParametricEq, BlockType::GraphicEq } },
        { "NAM",        Icon::Amp,      { BlockType::NamAmp } },
        { "IR",         Icon::Speaker,  { BlockType::IrLoader } },
        { "Modulation", Icon::Sine,     { BlockType::Chorus, BlockType::Flanger, BlockType::Phaser, BlockType::Tremolo,
                                          BlockType::Rotary, BlockType::Univibe, BlockType::Bitcrusher } },
        { "Delay",      Icon::Delay,    { BlockType::DigitalDelay, BlockType::AnalogDelay, BlockType::TapeDelay, BlockType::ReverseDelay } },
        { "Reverb",     Icon::Reverb,   { BlockType::PlateReverb, BlockType::HallReverb, BlockType::SpringReverb,
                                          BlockType::ShimmerReverb, BlockType::CloudReverb } },
        { "Utility",    Icon::Utility,  { BlockType::Volume } },
    };
    return groups;
}

void drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour, float stroke)
{
    // Icons are designed on a 24 x 24 grid and scaled into `area`.
    const float size = std::min (area.getWidth(), area.getHeight());
    const auto box = area.withSizeKeepingCentre (size, size);
    const float s = size / 24.0f;
    auto P = [&] (float x, float y) { return juce::Point<float> (box.getX() + x * s, box.getY() + y * s); };

    juce::Path p;
    bool fill = false;

    switch (icon)
    {
        case Icon::Pitch:
            p.startNewSubPath (P (12, 3));  p.lineTo (P (12, 21));
            p.startNewSubPath (P (8, 7));   p.lineTo (P (12, 3));  p.lineTo (P (16, 7));
            p.startNewSubPath (P (8, 17));  p.lineTo (P (12, 21)); p.lineTo (P (16, 17));
            break;
        case Icon::Wave:
            p.startNewSubPath (P (2, 12));  p.lineTo (P (6, 12));  p.lineTo (P (8, 7));  p.lineTo (P (11, 18));
            p.lineTo (P (14, 5));  p.lineTo (P (16, 13)); p.lineTo (P (18, 12)); p.lineTo (P (22, 12));
            break;
        case Icon::Octave:
            p.addEllipse (juce::Rectangle<float> (P (7.5f, 2.5f), P (16.5f, 11.5f)));
            p.addEllipse (juce::Rectangle<float> (P (2.5f, 11.5f), P (11.5f, 20.5f)));
            p.addEllipse (juce::Rectangle<float> (P (12.5f, 11.5f), P (21.5f, 20.5f)));
            break;
        case Icon::Dynamics:
            p.startNewSubPath (P (2, 12)); p.lineTo (P (5, 12)); p.lineTo (P (7, 6)); p.lineTo (P (9, 18));
            p.lineTo (P (11, 9)); p.lineTo (P (13, 15)); p.lineTo (P (15, 11)); p.lineTo (P (17, 13)); p.lineTo (P (22, 12));
            break;
        case Icon::Eq:
            for (int i = 0; i < 6; ++i)
            {
                static constexpr float heights[] = { 8, 14, 18, 12, 16, 6 };
                const float x = 3.5f + (float) i * 3.4f;
                p.startNewSubPath (P (x, 21));
                p.lineTo (P (x, 21 - heights[i]));
            }
            break;
        case Icon::Amp:
            p.addRoundedRectangle (juce::Rectangle<float> (P (2, 7), P (22, 20)), 2.0f * s);
            p.startNewSubPath (P (8, 7)); p.lineTo (P (9, 4)); p.lineTo (P (15, 4)); p.lineTo (P (16, 7));
            for (int i = 0; i < 4; ++i)
                p.addEllipse (P (5.0f + (float) i * 4.2f, 10.0f).x - 0.9f * s, P (0, 10).y - 0.9f * s, 1.8f * s, 1.8f * s);
            p.startNewSubPath (P (4, 15)); p.lineTo (P (20, 15));
            break;
        case Icon::Speaker:
            p.startNewSubPath (P (3, 9));  p.lineTo (P (7, 9));  p.lineTo (P (12, 4)); p.lineTo (P (12, 20));
            p.lineTo (P (7, 15)); p.lineTo (P (3, 15)); p.closeSubPath();
            p.addCentredArc (P (12, 12).x, P (12, 12).y, 4.5f * s, 4.5f * s, 0.0f, 0.6f, 2.55f, true);
            p.addCentredArc (P (12, 12).x, P (12, 12).y, 8.0f * s, 8.0f * s, 0.0f, 0.55f, 2.6f, true);
            break;
        case Icon::Sine:
            p.startNewSubPath (P (2, 12));
            for (int i = 1; i <= 40; ++i)
            {
                const float x = 2.0f + 20.0f * (float) i / 40.0f;
                p.lineTo (P (x, 12.0f - 6.0f * std::sin ((float) i / 40.0f * 2.0f * juce::MathConstants<float>::twoPi * 0.75f)));
            }
            break;
        case Icon::Delay:
            p.addEllipse (juce::Rectangle<float> (P (2, 7), P (12, 17)));
            p.addEllipse (juce::Rectangle<float> (P (12, 7), P (22, 17)));
            p.addEllipse (P (7, 12).x - 1.2f * s, P (7, 12).y - 1.2f * s, 2.4f * s, 2.4f * s);
            p.addEllipse (P (17, 12).x - 1.2f * s, P (17, 12).y - 1.2f * s, 2.4f * s, 2.4f * s);
            break;
        case Icon::Reverb:
            p.startNewSubPath (P (12, 2));  p.lineTo (P (21, 7));  p.lineTo (P (21, 17)); p.lineTo (P (12, 22));
            p.lineTo (P (3, 17)); p.lineTo (P (3, 7)); p.closeSubPath();
            p.startNewSubPath (P (3, 7));   p.lineTo (P (12, 12)); p.lineTo (P (21, 7));
            p.startNewSubPath (P (12, 12)); p.lineTo (P (12, 22));
            break;
        case Icon::Utility:
            p.startNewSubPath (P (6, 3));  p.lineTo (P (6, 21));
            p.startNewSubPath (P (12, 3)); p.lineTo (P (12, 21));
            p.startNewSubPath (P (18, 3)); p.lineTo (P (18, 21));
            p.addRoundedRectangle (juce::Rectangle<float> (P (4, 13), P (8, 16)), 1.0f * s);
            p.addRoundedRectangle (juce::Rectangle<float> (P (10, 6), P (14, 9)), 1.0f * s);
            p.addRoundedRectangle (juce::Rectangle<float> (P (16, 15), P (20, 18)), 1.0f * s);
            break;
        case Icon::Plus:
            p.startNewSubPath (P (12, 5)); p.lineTo (P (12, 19));
            p.startNewSubPath (P (5, 12)); p.lineTo (P (19, 12));
            break;
        case Icon::Save:
            p.startNewSubPath (P (5, 3)); p.lineTo (P (16, 3)); p.lineTo (P (20, 7)); p.lineTo (P (20, 21));
            p.lineTo (P (5, 21)); p.closeSubPath();
            p.startNewSubPath (P (8, 3)); p.lineTo (P (8, 8)); p.lineTo (P (15, 8)); p.lineTo (P (15, 3));
            break;
        case Icon::Undo:
        case Icon::Redo:
        {
            const bool redo = icon == Icon::Redo;
            auto X = [&] (float x) { return redo ? 24.0f - x : x; };
            p.startNewSubPath (P (X (9), 5)); p.lineTo (P (X (4), 10)); p.lineTo (P (X (9), 15));
            p.startNewSubPath (P (X (4), 10)); p.lineTo (P (X (14), 10));
            p.quadraticTo (P (X (20), 10), P (X (20), 15));
            p.quadraticTo (P (X (20), 20), P (X (14), 20));
            break;
        }
        case Icon::Menu:
            for (float y : { 6.0f, 12.0f, 18.0f })
            {
                p.startNewSubPath (P (4, y));
                p.lineTo (P (20, y));
            }
            break;
        case Icon::ChevronLeft:
            p.startNewSubPath (P (15, 5)); p.lineTo (P (8, 12)); p.lineTo (P (15, 19));
            break;
        case Icon::ChevronRight:
            p.startNewSubPath (P (9, 5)); p.lineTo (P (16, 12)); p.lineTo (P (9, 19));
            break;
        case Icon::Power:
            p.addCentredArc (P (12, 13).x, P (12, 13).y, 7.5f * s, 7.5f * s, 0.0f, 0.75f, juce::MathConstants<float>::twoPi - 0.75f, true);
            p.startNewSubPath (P (12, 3)); p.lineTo (P (12, 12));
            break;
        case Icon::File:
            p.startNewSubPath (P (6, 3)); p.lineTo (P (14, 3)); p.lineTo (P (19, 8)); p.lineTo (P (19, 21));
            p.lineTo (P (6, 21)); p.closeSubPath();
            p.startNewSubPath (P (14, 3)); p.lineTo (P (14, 8)); p.lineTo (P (19, 8));
            break;
        case Icon::More:
            fill = true;
            for (float x : { 6.0f, 12.0f, 18.0f })
                p.addEllipse (P (x, 12).x - 1.6f * s, P (x, 12).y - 1.6f * s, 3.2f * s, 3.2f * s);
            break;
        case Icon::Info:
            p.addEllipse (juce::Rectangle<float> (P (3, 3), P (21, 21)));
            p.startNewSubPath (P (12, 11)); p.lineTo (P (12, 17));
            p.addEllipse (P (12, 7.5f).x - 0.9f * s, P (12, 7.5f).y - 0.9f * s, 1.8f * s, 1.8f * s);
            break;
        case Icon::Trash:
            p.startNewSubPath (P (4, 6));  p.lineTo (P (20, 6));
            p.startNewSubPath (P (9, 6));  p.lineTo (P (10, 3)); p.lineTo (P (14, 3)); p.lineTo (P (15, 6));
            p.startNewSubPath (P (6, 6));  p.lineTo (P (7, 21)); p.lineTo (P (17, 21)); p.lineTo (P (18, 6));
            break;
        case Icon::Synth:   // keyboard
            p.addRoundedRectangle (juce::Rectangle<float> (P (2, 5), P (22, 19)), 1.5f * s);
            for (float x : { 7.0f, 12.0f, 17.0f })
            {
                p.startNewSubPath (P (x, 12)); p.lineTo (P (x, 19));
            }
            for (float x : { 5.5f, 9.5f, 14.5f, 18.5f })
                p.addRectangle (juce::Rectangle<float> (P (x - 1.0f, 5), P (x + 1.0f, 12)));
            break;
        case Icon::Expand:
            p.startNewSubPath (P (14, 4));  p.lineTo (P (20, 4));  p.lineTo (P (20, 10));
            p.startNewSubPath (P (20, 4));  p.lineTo (P (13, 11));
            p.startNewSubPath (P (10, 20)); p.lineTo (P (4, 20));  p.lineTo (P (4, 14));
            p.startNewSubPath (P (4, 20));  p.lineTo (P (11, 13));
            break;
        case Icon::Logo:
            for (int i = 0; i < 9; ++i)
            {
                static constexpr float heights[] = { 4, 8, 13, 18, 22, 18, 12, 7, 3 };
                const float x = 2.0f + (float) i * 2.5f;
                p.startNewSubPath (P (x, 12 - heights[i] * 0.5f));
                p.lineTo (P (x, 12 + heights[i] * 0.5f));
            }
            break;
    }

    g.setColour (colour);
    if (fill)
        g.fillPath (p);
    else
        g.strokePath (p, juce::PathStrokeType (stroke * std::max (0.75f, s), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
} // namespace kumamp::theme
