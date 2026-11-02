#pragma once

#include "../Tone3000/Tone3000.h"
#include "Widgets.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace kumamp
{
/**
    In-window chooser for the models of a TONE3000 tone (one tone often holds
    several captures: channels, gain settings, mic positions).

    An overlay rather than a PopupMenu: the tone arrives while the browser is
    in front, and a popup menu of a background window closes itself at once.
    This stays until the user picks a model or cancels.
*/
class Tone3000Picker final : public juce::Component,
                             private juce::ListBoxModel
{
public:
    Tone3000Picker();
    ~Tone3000Picker() override;

    void show (const t3k::Tone& tone, const juce::Array<t3k::Model>& models);

    std::function<void (const t3k::Tone&, const t3k::Model&)> onChosen;
    std::function<void()> onClosed;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    int getNumRows() override { return models.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { choose (row); }
    void returnKeyPressed (int row) override { choose (row); }
    void selectedRowsChanged (int row) override { loadButton.setEnabled (row >= 0); }

    void choose (int row);

    t3k::Tone tone;
    juce::Array<t3k::Model> models;
    juce::ListBox list { "models", this };
    juce::TextButton loadButton { "Cargar en el Amp" }, cancelButton { "Cancelar" };
    juce::HyperlinkButton credit { "Powered by TONE3000", juce::URL ("https://www.tone3000.com") };
    juce::Rectangle<int> card, header;
};
} // namespace kumamp
