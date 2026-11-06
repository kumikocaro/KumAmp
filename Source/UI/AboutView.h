#pragma once

#include "Theme.h"

#include <functional>

namespace kumamp
{
/** ACERCA DE: who makes KumAmp. */
class AboutView final : public juce::Component
{
public:
    AboutView();

    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Image photo;
    juce::HyperlinkButton website { "www.kumodev.com", juce::URL ("https://www.kumodev.com") };
    juce::TextButton close { "Cerrar" };
    juce::Rectangle<int> photoArea, textArea;
};
} // namespace kumamp
