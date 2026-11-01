#include "TunerView.h"

namespace kumamp
{
namespace
{
    const juce::Colour kBackground  { 0xff12141a };
    const juce::Colour kPanel       { 0xff1a1d24 };
    const juce::Colour kGrid        { 0xff2c313c };
    const juce::Colour kText        { 0xffe6e9ef };
    const juce::Colour kDim         { 0xff6b7280 };
    const juce::Colour kInTune      { 0xff4cd964 };
    const juce::Colour kClose       { 0xffffcc00 };
    const juce::Colour kOff         { 0xffff5a4f };

    constexpr float kInTuneCents = 3.0f;
    constexpr double kHoldSeconds = 1.5;   // keep showing the last note after the string dies
}

TunerView::TunerView()
{
    for (auto* b : { &traditionalButton, &traceButton })
    {
        b->setClickingTogglesState (false);
        b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3b82f6));
        addAndMakeVisible (b);
    }
    traditionalButton.onClick = [this] { mode = Traditional; updateModeButtons(); if (onModeChanged) onModeChanged (mode); repaint(); };
    traceButton.onClick       = [this] { mode = Trace;       updateModeButtons(); if (onModeChanged) onModeChanged (mode); repaint(); };

    muteToggle.onClick = [this] { if (onMuteChanged) onMuteChanged (muteToggle.getToggleState()); };
    addAndMakeVisible (muteToggle);

    reference.setRange (430.0, 450.0, 0.1);
    reference.setValue (440.0, juce::dontSendNotification);
    reference.setTextValueSuffix (" Hz");
    reference.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 20);
    reference.setDoubleClickReturnValue (true, 440.0);
    reference.onValueChange = [this] { if (onReferenceChanged) onReferenceChanged ((float) reference.getValue()); };
    addAndMakeVisible (reference);
    addAndMakeVisible (referenceLabel);

    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeButton);

    updateModeButtons();
}

TunerView::~TunerView()
{
    stopTimer();
}

void TunerView::setSettings (int newMode, bool mute, float referenceHz)
{
    if (newMode != mode)
    {
        mode = newMode;
        updateModeButtons();
        repaint();
    }
    muteToggle.setToggleState (mute, juce::dontSendNotification);
    if (! reference.isMouseButtonDown())
        reference.setValue (referenceHz, juce::dontSendNotification);
}

void TunerView::updateModeButtons()
{
    traditionalButton.setToggleState (mode == Traditional, juce::dontSendNotification);
    traceButton.setToggleState (mode == Trace, juce::dontSendNotification);
}

void TunerView::visibilityChanged()
{
    if (isVisible())
        startTimerHz (60);
    else
        stopTimer();
}

void TunerView::timerCallback()
{
    if (readingSource)
        pushReading (readingSource(), juce::Time::getMillisecondCounterHiRes() * 0.001);
}

void TunerView::pushReading (const Tuner::Reading& reading, double nowSeconds)
{
    const double dt = lastPushTime < 0.0 ? 0.0 : juce::jlimit (0.0, 0.1, nowSeconds - lastPushTime);
    lastPushTime = nowSeconds;
    now = nowSeconds;
    current = reading;

    if (reading.valid)
    {
        lastValid = reading;
        lastValidTime = now;
        needleCents += (reading.cents - needleCents) * 0.35f;

        // Strobe: drift speed proportional to the error, each band twice as fast (harmonics).
        for (size_t k = 0; k < strobePhase.size(); ++k)
            strobePhase[k] += (double) reading.cents * 6.0 * (double) (k + 1) * dt;
    }

    traceHead = (traceHead + 1) % kTraceLength;
    trace[(size_t) traceHead] = { reading.cents, reading.midiNote, reading.valid };
    traceCount = std::min (traceCount + 1, kTraceLength);

    repaint();
}

juce::Colour TunerView::colourFor (float cents, bool valid)
{
    if (! valid)
        return kDim;
    const float c = std::abs (cents);
    return c <= kInTuneCents ? kInTune : (c <= 10.0f ? kClose : kOff);
}

float TunerView::traceX (float cents, juce::Rectangle<float> area)
{
    // Expanded scale: +-5 ct uses ~38 % of each half, +-50 ct reaches the edge.
    const float c = juce::jlimit (-50.0f, 50.0f, cents);
    const float shaped = std::copysign (std::pow (std::abs (c) / 50.0f, 0.6f), c);
    return area.getCentreX() + shaped * area.getWidth() * 0.5f;
}

//==============================================================================
void TunerView::resized()
{
    auto area = getLocalBounds().reduced (12);
    auto bar = area.removeFromTop (32);

    traditionalButton.setBounds (bar.removeFromLeft (110).reduced (2));
    traceButton.setBounds (bar.removeFromLeft (110).reduced (2));
    bar.removeFromLeft (16);
    muteToggle.setBounds (bar.removeFromLeft (150));
    closeButton.setBounds (bar.removeFromRight (90).reduced (2));
    bar.removeFromRight (12);
    reference.setBounds (bar.removeFromRight (220));
    referenceLabel.setBounds (bar.removeFromRight (30));

    area.removeFromTop (8);
    displayArea = area;
}

void TunerView::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);

    auto area = displayArea.toFloat();
    g.setColour (kPanel);
    g.fillRoundedRectangle (area, 10.0f);

    area = area.reduced (16.0f);
    paintNote (g, area.removeFromTop (96.0f));
    area.removeFromTop (8.0f);

    if (mode == Traditional)
        paintTraditional (g, area);
    else
        paintTrace (g, area);
}

void TunerView::paintNote (juce::Graphics& g, juce::Rectangle<float> area)
{
    const bool live = current.valid;
    const bool held = ! live && (now - lastValidTime) < kHoldSeconds && lastValid.valid;
    const auto& r = live ? current : lastValid;

    if (! live && ! held)
    {
        g.setColour (kDim);
        g.setFont (juce::FontOptions (22.0f));
        g.drawText ("Toca una cuerda", area, juce::Justification::centred);
        return;
    }

    const auto colour = colourFor (r.cents, true).withAlpha (held ? 0.45f : 1.0f);

    // Note name + octave, centred.
    const juce::String name (noteName (r.midiNote));
    const juce::String octave (noteOctave (r.midiNote));
    auto centre = area.withSizeKeepingCentre (220.0f, area.getHeight());
    g.setColour (colour);
    g.setFont (juce::FontOptions (84.0f, juce::Font::bold));
    g.drawText (name, centre.removeFromLeft (170.0f), juce::Justification::centredRight);
    g.setFont (juce::FontOptions (30.0f));
    g.drawText (octave, centre.withTrimmedTop (40.0f), juce::Justification::centredLeft);

    // Frequency (left) and cents (right).
    g.setColour (kText.withAlpha (held ? 0.45f : 0.9f));
    g.setFont (juce::FontOptions (20.0f));
    g.drawText (juce::String (r.frequency, 2) + " Hz", area.removeFromLeft (220.0f), juce::Justification::centredLeft);
    const juce::String cents = (r.cents >= 0.0f ? "+" : "") + juce::String (r.cents, 1) + " ct";
    g.setColour (colour);
    g.drawText (cents, area.removeFromRight (220.0f), juce::Justification::centredRight);
}

void TunerView::paintTraditional (juce::Graphics& g, juce::Rectangle<float> area)
{
    const bool live = current.valid;
    const bool inTune = live && std::abs (current.cents) <= kInTuneCents;

    // --- Strobe bands -----------------------------------------------------------
    auto strobe = area.removeFromTop (area.getHeight() * 0.48f);
    g.setColour (kBackground);
    g.fillRoundedRectangle (strobe, 6.0f);

    const auto stripeColour = ! live ? kDim.withAlpha (0.35f) : (inTune ? kInTune : kClose);
    const float bandHeight = strobe.getHeight() / 3.0f;

    g.saveState();
    g.reduceClipRegion (strobe.toNearestInt());
    for (int k = 0; k < 3; ++k)
    {
        const float stripe = 28.0f / (float) (1 << k);          // finer stripes = higher harmonic
        const float period = stripe * 2.0f;
        const float offset = (float) std::fmod (strobePhase[(size_t) k], (double) period);
        const auto band = strobe.withY (strobe.getY() + bandHeight * (float) k).withHeight (bandHeight).reduced (0.0f, 4.0f);

        g.setColour (stripeColour.withAlpha (1.0f - 0.2f * (float) k));
        for (float x = band.getX() - period + offset; x < band.getRight(); x += period)
            g.fillRect (x, band.getY(), stripe, band.getHeight());
    }
    g.restoreState();

    if (inTune)
    {
        g.setColour (kInTune);
        g.drawRoundedRectangle (strobe, 6.0f, 2.0f);
    }

    // --- Cents needle ---------------------------------------------------------
    area.removeFromTop (18.0f);
    auto meter = area.reduced (24.0f, 0.0f);
    const float baseline = meter.getY() + meter.getHeight() * 0.62f;
    auto xFor = [&] (float c) { return meter.getCentreX() + juce::jlimit (-50.0f, 50.0f, c) / 50.0f * meter.getWidth() * 0.5f; };

    g.setColour (kInTune.withAlpha (0.18f));
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xFor (-kInTuneCents), meter.getY(), xFor (kInTuneCents), baseline));

    g.setFont (juce::FontOptions (13.0f));
    for (int c = -50; c <= 50; c += 5)
    {
        const float x = xFor ((float) c);
        const bool major = c % 25 == 0;
        g.setColour (c == 0 ? kInTune : (major ? kText : kGrid));
        g.drawLine (x, baseline - (major ? 26.0f : 14.0f), x, baseline, c == 0 ? 2.5f : 1.5f);
        if (major)
            g.drawText ((c > 0 ? "+" : "") + juce::String (c), juce::Rectangle<float> (x - 24.0f, baseline + 6.0f, 48.0f, 16.0f),
                        juce::Justification::centred);
    }
    g.setColour (kDim);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText ("b", juce::Rectangle<float> (meter.getX() - 24.0f, baseline - 30.0f, 20.0f, 20.0f), juce::Justification::centred);
    g.drawText ("#", juce::Rectangle<float> (meter.getRight() + 4.0f, baseline - 30.0f, 20.0f, 20.0f), juce::Justification::centred);

    if (live || (now - lastValidTime) < kHoldSeconds)
    {
        const float x = xFor (needleCents);
        g.setColour (colourFor (needleCents, true).withAlpha (live ? 1.0f : 0.4f));
        g.drawLine (x, meter.getY(), x, baseline, 4.0f);
        juce::Path tip;
        tip.addTriangle (x - 8.0f, baseline + 2.0f, x + 8.0f, baseline + 2.0f, x, baseline - 10.0f);
        g.fillPath (tip);
    }
}

void TunerView::paintTrace (juce::Graphics& g, juce::Rectangle<float> area)
{
    auto labels = area.removeFromTop (20.0f);
    auto plot = area;

    g.setColour (kBackground);
    g.fillRoundedRectangle (plot, 6.0f);

    // In-tune corridor and grid.
    g.setColour (kInTune.withAlpha (0.12f));
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (traceX (-kInTuneCents, plot), plot.getY(),
                                                          traceX (kInTuneCents, plot), plot.getBottom()));

    g.setFont (juce::FontOptions (12.0f));
    for (int c : { -50, -25, -10, -5, 0, 5, 10, 25, 50 })
    {
        const float x = traceX ((float) c, plot);
        g.setColour (c == 0 ? kInTune.withAlpha (0.8f) : kGrid);
        g.drawLine (x, plot.getY(), x, plot.getBottom(), c == 0 ? 2.0f : 1.0f);
        g.setColour (c == 0 ? kInTune : kDim);
        g.drawText ((c > 0 ? "+" : "") + juce::String (c), juce::Rectangle<float> (x - 20.0f, labels.getY(), 40.0f, labels.getHeight()),
                    juce::Justification::centred);
    }

    // Time ticks every second on the left edge (newest at the top).
    for (int s = 1; s * 60 < kTraceLength; ++s)
    {
        const float y = plot.getY() + plot.getHeight() * (float) (s * 60) / (float) (kTraceLength - 1);
        g.setColour (kGrid.withAlpha (0.6f));
        g.drawLine (plot.getX(), y, plot.getX() + 8.0f, y, 1.0f);
        g.setColour (kDim);
        g.drawText (juce::String (s) + "s", juce::Rectangle<float> (plot.getX() + 10.0f, y - 7.0f, 30.0f, 14.0f),
                    juce::Justification::centredLeft);
    }

    // The trace: newest sample at the top, flowing down.
    const float step = plot.getHeight() / (float) (kTraceLength - 1);
    g.saveState();
    g.reduceClipRegion (plot.toNearestInt());

    const TracePoint* previous = nullptr;
    float previousX = 0.0f, previousY = 0.0f;
    int lastLabelledNote = -2;

    for (int i = 0; i < traceCount; ++i)
    {
        const auto& p = trace[(size_t) ((traceHead - i + kTraceLength) % kTraceLength)];
        const float y = plot.getY() + step * (float) i;
        const float x = traceX (p.cents, plot);

        if (p.valid)
        {
            if (previous != nullptr && previous->valid && previous->note == p.note)
            {
                g.setColour (colourFor (0.5f * (p.cents + previous->cents), true));
                g.drawLine (previousX, previousY, x, y, 3.0f);
            }

            // Label each run of a note at its newest (top) end.
            if (p.note != lastLabelledNote)
            {
                g.setColour (kText.withAlpha (0.85f));
                g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
                g.drawText (juce::String (noteName (p.note)) + juce::String (noteOctave (p.note)),
                            juce::Rectangle<float> (plot.getRight() - 54.0f, juce::jlimit (plot.getY(), plot.getBottom() - 16.0f, y - 8.0f), 46.0f, 16.0f),
                            juce::Justification::centredRight);
                lastLabelledNote = p.note;
            }
        }
        else
        {
            lastLabelledNote = -2;
        }

        previous = &p;
        previousX = x;
        previousY = y;
    }
    g.restoreState();

    // The "pen" at the top.
    if (traceCount > 0 && trace[(size_t) traceHead].valid)
    {
        const auto& head = trace[(size_t) traceHead];
        const float x = traceX (head.cents, plot);
        g.setColour (colourFor (head.cents, true));
        g.fillEllipse (x - 7.0f, plot.getY() - 7.0f + 4.0f, 14.0f, 14.0f);
    }
}
} // namespace kumamp
