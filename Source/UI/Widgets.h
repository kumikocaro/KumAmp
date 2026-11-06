#pragma once

#include "Theme.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>

namespace kumamp
{
/** Flat button that draws a theme icon. */
class IconButton final : public juce::Button
{
public:
    explicit IconButton (theme::Icon iconToUse, const juce::String& tooltip = {})
        : juce::Button (tooltip), icon (iconToUse)
    {
        setTooltip (tooltip);
    }

    void setIcon (theme::Icon newIcon) { icon = newIcon; repaint(); }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto area = getLocalBounds().toFloat().reduced (1.0f);
        if (highlighted || down)
        {
            g.setColour (theme::panelRaised.brighter (down ? 0.15f : 0.05f));
            g.fillRect (area);
        }
        const auto colour = ! isEnabled() ? theme::textDim.withAlpha (0.35f)
                                          : (getToggleState() ? theme::accentLight : theme::text.withAlpha (0.85f));
        theme::drawIcon (g, icon, area.reduced (area.getHeight() * 0.22f), colour);
    }

private:
    theme::Icon icon;
};

/** Round on/off button with a power glyph (block / section enable). */
class PowerButton final : public juce::ToggleButton
{
public:
    PowerButton() { setClickingTogglesState (true); }

    void paintButton (juce::Graphics& g, bool highlighted, bool) override
    {
        const auto area = getLocalBounds().toFloat().reduced (2.0f);
        const auto square = area.withSizeKeepingCentre (std::min (area.getWidth(), area.getHeight()),
                                                        std::min (area.getWidth(), area.getHeight()));
        const bool on = getToggleState();
        g.setColour (on ? theme::accent : theme::panel);
        g.fillRect (square);
        if (! on)
        {
            g.setColour (highlighted ? theme::text : theme::border);
            g.drawRect (square, 1.0f);
        }
        theme::drawIcon (g, theme::Icon::Power, square.reduced (square.getWidth() * 0.24f), on ? theme::ink : theme::textDim, 2.0f);
    }
};

/** Vertical peak meter (1 or 2 channels) with hold-and-decay and dB scale. */
class LevelMeter final : public juce::Component
{
public:
    explicit LevelMeter (int channels = 2, bool showScale = false) : numChannels (channels), scale (showScale) {}

    /** Linear peaks since the last call; call at display rate. */
    void push (float left, float right)
    {
        const float in[2] { left, right };
        for (int c = 0; c < 2; ++c)
            levels[(size_t) c] = std::max (in[c], levels[(size_t) c] * 0.86f);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        if (scale)
        {
            auto labels = area.removeFromBottom (16.0f);
            g.setColour (theme::textDim);
            g.setFont (theme::mono (10.0f));
            const char* marks[] = { "-6", "-12", "-24", "-48" };
            const float w = labels.getWidth() / 4.0f;
            for (int i = 0; i < 4; ++i)
                g.drawText (marks[i], labels.withX (labels.getX() + w * (float) i).withWidth (w), juce::Justification::centred);
            area.removeFromBottom (4.0f);
        }

        const float gap = 4.0f;
        const float barWidth = std::min (10.0f, (area.getWidth() - gap * (float) (numChannels - 1)) / (float) numChannels);
        const float total = barWidth * (float) numChannels + gap * (float) (numChannels - 1);
        float x = area.getCentreX() - total * 0.5f;

        for (int c = 0; c < numChannels; ++c)
        {
            const auto bar = juce::Rectangle<float> (x, area.getY(), barWidth, area.getHeight());
            g.setColour (theme::panelRaised);
            g.fillRect (bar);

            const float db = juce::Decibels::gainToDecibels (levels[(size_t) c], -60.0f);
            const float proportion = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
            const auto lit = bar.withTop (bar.getBottom() - bar.getHeight() * proportion);
            g.setColour (theme::text);
            g.fillRect (lit);
            // The last 6 dB go red.
            const auto hot = bar.withBottom (bar.getY() + bar.getHeight() * 0.1f);
            g.setColour (theme::accent);
            g.fillRect (lit.getIntersection (hot));
            x += barWidth + gap;
        }
    }

private:
    int numChannels;
    bool scale;
    std::array<float, 2> levels {};
};
} // namespace kumamp
