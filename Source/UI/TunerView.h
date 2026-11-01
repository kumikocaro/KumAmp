#pragma once

#include "../Tuner/Tuner.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

namespace kumamp
{
/**
    Tuner display with two modes:

    - Tradicional: big note name, strobe bands that drift left/right with the
      deviation and freeze when in tune, plus a classic cents needle.
    - Trazo: seismograph-style history. Horizontal = cents (scale expanded
      around 0), the newest reading is drawn at the top and the trace flows
      downward, so you can see how the note arrives, settles and drifts.

    Data comes from `readingSource` (polled at 60 Hz while visible); settings
    go out through the on...Changed callbacks so the view has no dependency on
    the processor (and can be driven by tests).
*/
class TunerView final : public juce::Component, private juce::Timer
{
public:
    enum Mode { Traditional = 0, Trace = 1 };
    static constexpr int kTraceLength = 480;   // 8 s at 60 Hz

    TunerView();
    ~TunerView() override;

    std::function<Tuner::Reading()> readingSource;
    std::function<void (int)> onModeChanged;
    std::function<void (bool)> onMuteChanged;
    std::function<void (float)> onReferenceChanged;
    std::function<void()> onClose;

    /** Reflect host parameter state (does not fire the callbacks). */
    void setSettings (int mode, bool mute, float referenceHz);

    /** Feed one reading; `nowSeconds` drives the strobe animation. */
    void pushReading (const Tuner::Reading& reading, double nowSeconds);

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

private:
    struct TracePoint { float cents = 0.0f; int note = -1; bool valid = false; };

    void timerCallback() override;
    void paintNote (juce::Graphics&, juce::Rectangle<float>);
    void paintTraditional (juce::Graphics&, juce::Rectangle<float>);
    void paintTrace (juce::Graphics&, juce::Rectangle<float>);
    void updateModeButtons();

    static juce::Colour colourFor (float cents, bool valid);
    static float traceX (float cents, juce::Rectangle<float> area);  // expanded around 0

    juce::TextButton traditionalButton { "Tradicional" }, traceButton { "Trazo" }, closeButton { "Cerrar" };
    juce::ToggleButton muteToggle { "Silenciar salida" };
    juce::Slider reference { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label referenceLabel { {}, "A4" };

    int mode = Traditional;
    Tuner::Reading current, lastValid;
    double now = 0.0, lastValidTime = -10.0, lastPushTime = -1.0;
    float needleCents = 0.0f;
    std::array<double, 3> strobePhase {};

    std::array<TracePoint, kTraceLength> trace {};
    int traceHead = 0, traceCount = 0;
    juce::Rectangle<int> displayArea;
};
} // namespace kumamp
