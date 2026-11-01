#include "EqView.h"
#include <utility>

namespace kumamp
{
namespace
{
    constexpr float kMinHz = 10.0f, kMaxHz = 30000.0f;
    constexpr float kNodeRadius = 7.0f;
    constexpr float kAnalyserFloorDb = -84.0f;
    using EQ = ParametricEqBlock;

    juce::String formatHz (float hz)
    {
        return hz >= 1000.0f ? juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz";
    }

    void styleSmallKnob (juce::Slider& s)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 18);
        s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    }
} // namespace

juce::Colour EqView::bandColour (int band)
{
    static const juce::Colour colours[] = {
        juce::Colour (0xff22d3ee), juce::Colour (0xff4ade80), juce::Colour (0xfffacc15), juce::Colour (0xfffb923c),
        juce::Colour (0xfff43f5e), juce::Colour (0xffe879f9), juce::Colour (0xffa78bfa), juce::Colour (0xff60a5fa),
    };
    return colours[(size_t) juce::jlimit (0, 7, band)];
}

bool EqView::hasGain (EqBandType type) noexcept
{
    return type == EqBandType::Bell || type == EqBandType::LowShelf || type == EqBandType::HighShelf;
}

EqView::EqView (bool compact) : compactMode (compact)
{
    typeBox.addItemList ({ "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass" }, 1);
    slopeBox.addItemList ({ "12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct" }, 1);
    typeBox.onChange  = [this] { if (! updatingStrip && selectedBand >= 0) setBandParam (selectedBand, EQ::BandType, (float) (typeBox.getSelectedId() - 1)); };
    slopeBox.onChange = [this] { if (! updatingStrip && selectedBand >= 0) setBandParam (selectedBand, EQ::BandSlope, (float) (slopeBox.getSelectedId() - 1)); };

    for (auto* k : { &freqKnob, &gainKnob, &qKnob, &outputKnob })
        styleSmallKnob (*k);
    freqKnob.setRange (20.0, 20000.0);
    freqKnob.setSkewFactorFromMidPoint (1000.0);
    freqKnob.textFromValueFunction = [] (double v) { return formatHz ((float) v); };
    gainKnob.setRange (-24.0, 24.0);
    gainKnob.textFromValueFunction = [] (double v) { return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; };
    qKnob.setRange (0.1, 18.0);
    qKnob.setSkewFactorFromMidPoint (1.0);
    qKnob.textFromValueFunction = [] (double v) { return juce::String (v, 2); };
    outputKnob.setRange (-24.0, 24.0);
    outputKnob.setDoubleClickReturnValue (true, 0.0);
    outputKnob.textFromValueFunction = gainKnob.textFromValueFunction;
    for (auto* k : { &freqKnob, &gainKnob, &qKnob, &outputKnob })
        k->updateText();

    freqKnob.onValueChange = [this] { if (! updatingStrip && selectedBand >= 0) setBandParam (selectedBand, EQ::BandFreq, (float) freqKnob.getValue()); };
    gainKnob.onValueChange = [this] { if (! updatingStrip && selectedBand >= 0) setBandParam (selectedBand, EQ::BandGain, (float) gainKnob.getValue()); };
    qKnob.onValueChange    = [this] { if (! updatingStrip && selectedBand >= 0) setBandParam (selectedBand, EQ::BandQ, (float) qKnob.getValue()); };
    outputKnob.onValueChange = [this] { if (! updatingStrip && store != nullptr) store->setParam (EQ::OutputDb, (float) outputKnob.getValue()); };

    for (auto [label, text] : { std::pair<juce::Label*, const char*> { &freqLabel, "Freq" }, { &gainLabel, "Gain" }, { &qLabel, "Q" }, { &outputLabel, "Output" } })
    {
        label->setText (text, juce::dontSendNotification);
        label->setJustificationType (juce::Justification::centred);
        label->setFont (juce::FontOptions (13.0f));
        label->setColour (juce::Label::textColourId, theme::textDim);
    }

    removeBand.onClick = [this] { if (selectedBand >= 0) { setBandParam (selectedBand, EQ::BandOn, 0.0f); selectBand (-1); } };
    expandButton.onClick = [this] { if (onExpand) onExpand(); };
    closeButton.onClick = [this] { if (onClose) onClose(); };

    for (juce::Component* c : { (juce::Component*) &typeBox, (juce::Component*) &slopeBox, (juce::Component*) &freqKnob, (juce::Component*) &gainKnob,
                                (juce::Component*) &qKnob, (juce::Component*) &freqLabel, (juce::Component*) &gainLabel, (juce::Component*) &qLabel,
                                (juce::Component*) &removeBand })
        addChildComponent (c);
    addAndMakeVisible (outputKnob);
    addAndMakeVisible (outputLabel);
    addChildComponent (expandButton);
    addChildComponent (closeButton);
    expandButton.setVisible (compactMode);
    closeButton.setVisible (! compactMode);
    expandButton.setTooltip ("Abrir EQ grande");
    setTooltip ("Doble clic: nueva banda  |  Arrastrar: freq / gain  |  Rueda: Q  |  Shift: fino  |  Clic derecho: tipo");
}

EqView::~EqView()
{
    stopTimer();
    if (auto* block = eq())
        block->setAnalyserEnabled (false);
}

void EqView::setBlock (std::shared_ptr<ParamStore> eqStore)
{
    if (auto* old = eq())
        old->setAnalyserEnabled (false);
    store = std::move (eqStore);
    selectBand (-1);
    preSpectrum.fill (kAnalyserFloorDb);
    postSpectrum.fill (kAnalyserFloorDb);
    syncStripFromParams();
    repaint();
}

void EqView::visibilityChanged()
{
    if (isVisible()) startTimerHz (30);
    else
    {
        stopTimer();
        if (auto* block = eq()) block->setAnalyserEnabled (false);
    }
}

//==============================================================================
juce::Rectangle<float> EqView::plotArea() const
{
    auto area = getLocalBounds().toFloat().reduced (1.0f);
    area.removeFromBottom (compactMode ? 0.0f : 96.0f);   // large view: strip below the plot
    return area;
}

float EqView::freqToX (float hz) const
{
    const auto a = plotArea();
    return a.getX() + a.getWidth() * std::log (hz / kMinHz) / std::log (kMaxHz / kMinHz);
}

float EqView::xToFreq (float x) const
{
    const auto a = plotArea();
    return kMinHz * std::pow (kMaxHz / kMinHz, juce::jlimit (0.0f, 1.0f, (x - a.getX()) / a.getWidth()));
}

float EqView::gainToY (float db) const
{
    const auto a = plotArea().reduced (0.0f, 14.0f);
    return a.getCentreY() - db / dbRange() * a.getHeight() * 0.5f;
}

float EqView::yToGain (float y) const
{
    const auto a = plotArea().reduced (0.0f, 14.0f);
    return (a.getCentreY() - y) / (a.getHeight() * 0.5f) * dbRange();
}

juce::Point<float> EqView::nodePosition (int b) const
{
    const auto band = eq()->getBand (b);
    return { freqToX (band.frequency), gainToY (hasGain (band.type) ? band.gainDb : 0.0f) };
}

int EqView::bandAt (juce::Point<float> position) const
{
    if (eq() == nullptr)
        return -1;
    int best = -1;
    float bestDistance = kNodeRadius + 7.0f;
    for (int b = 0; b < EQ::kNumBands; ++b)
        if (eq()->getBand (b).on)
            if (const float d = nodePosition (b).getDistanceFrom (position); d < bestDistance)
            {
                bestDistance = d;
                best = b;
            }
    return best;
}

void EqView::setBandParam (int band, EQ::BandParam param, float value)
{
    if (store != nullptr)
        store->setParam (EQ::bandParam (band, param), value);
    repaint();
}

void EqView::selectBand (int band)
{
    selectedBand = band;
    const bool show = band >= 0;
    for (juce::Component* c : { (juce::Component*) &typeBox, (juce::Component*) &freqKnob, (juce::Component*) &gainKnob,
                                (juce::Component*) &qKnob, (juce::Component*) &freqLabel, (juce::Component*) &gainLabel,
                                (juce::Component*) &qLabel, (juce::Component*) &removeBand })
        c->setVisible (show);
    syncStripFromParams();
    resized();
    repaint();
}

void EqView::syncStripFromParams()
{
    auto* block = eq();
    if (block == nullptr)
        return;

    const juce::ScopedValueSetter<bool> guard (updatingStrip, true);
    if (! outputKnob.isMouseButtonDown())
        outputKnob.setValue (block->getParam (EQ::OutputDb), juce::dontSendNotification);

    if (selectedBand < 0)
    {
        slopeBox.setVisible (false);
        return;
    }

    const auto band = block->getBand (selectedBand);
    if (! typeBox.isPopupActive())  typeBox.setSelectedId ((int) band.type + 1, juce::dontSendNotification);
    if (! slopeBox.isPopupActive()) slopeBox.setSelectedId (band.slope + 1, juce::dontSendNotification);
    if (! freqKnob.isMouseButtonDown()) freqKnob.setValue (band.frequency, juce::dontSendNotification);
    if (! gainKnob.isMouseButtonDown()) gainKnob.setValue (band.gainDb, juce::dontSendNotification);
    if (! qKnob.isMouseButtonDown())    qKnob.setValue (band.q, juce::dontSendNotification);

    const bool isCut = band.type == EqBandType::LowCut || band.type == EqBandType::HighCut;
    slopeBox.setVisible (isCut);
    gainKnob.setEnabled (hasGain (band.type));
    gainKnob.setAlpha (hasGain (band.type) ? 1.0f : 0.4f);
    for (auto* k : { &freqKnob, &gainKnob, &qKnob })
        k->setColour (juce::Slider::trackColourId, bandColour (selectedBand));
}

//==============================================================================
void EqView::updateSpectrum (const SampleRing<8192>& ring, std::array<float, kFftSize / 2 + 1>& smoothed)
{
    std::fill (fftBuffer.begin(), fftBuffer.end(), 0.0f);
    ring.readLatest (fftBuffer.data(), kFftSize);
    window.multiplyWithWindowingTable (fftBuffer.data(), (size_t) kFftSize);
    fft.performFrequencyOnlyForwardTransform (fftBuffer.data(), true);

    const float norm = 4.0f / (float) kFftSize;   // Hann coherent gain + one-sided spectrum
    for (int bin = 0; bin <= kFftSize / 2; ++bin)
    {
        const float db = juce::Decibels::gainToDecibels (fftBuffer[(size_t) bin] * norm, -120.0f);
        auto& s = smoothed[(size_t) bin];
        s = db > s ? db : s + 0.25f * (db - s);   // fast attack, smooth release
    }
}

juce::Path EqView::spectrumPath (const std::array<float, kFftSize / 2 + 1>& spectrum, bool closed) const
{
    const auto area = plotArea();
    const double sr = eq() != nullptr ? eq()->getSampleRate() : 48000.0;
    const float binHz = (float) sr / (float) kFftSize;
    juce::Path p;

    for (float x = area.getX(); x <= area.getRight(); x += 2.0f)
    {
        const float hz = xToFreq (x);
        const float position = hz / binHz;
        const int bin = juce::jlimit (1, kFftSize / 2 - 1, (int) position);
        const float t = juce::jlimit (0.0f, 1.0f, position - (float) bin);
        float db = spectrum[(size_t) bin] + t * (spectrum[(size_t) bin + 1] - spectrum[(size_t) bin]);
        db += 4.5f * std::log2 (hz / 1000.0f);   // +4.5 dB/oct display tilt: guitar spectra look flat
        const float y = area.getBottom() - area.getHeight() * juce::jlimit (0.0f, 1.0f, (db - kAnalyserFloorDb) / -kAnalyserFloorDb);
        if (x <= area.getX()) p.startNewSubPath (x, y);
        else                  p.lineTo (x, y);
    }

    if (closed)
    {
        p.lineTo (area.getRight(), area.getBottom());
        p.lineTo (area.getX(), area.getBottom());
        p.closeSubPath();
    }
    return p;
}

void EqView::timerCallback()
{
    if (auto* block = eq())
    {
        block->setAnalyserEnabled (true);
        updateSpectrum (block->getPreAnalyser(), preSpectrum);
        updateSpectrum (block->getPostAnalyser(), postSpectrum);
        syncStripFromParams();
    }
    repaint();
}

//==============================================================================
void EqView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto area = plotArea();

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff171c28), 0.0f, area.getY(),
                                             juce::Colour (0xff0b0e14), 0.0f, area.getBottom(), false));
    g.fillRoundedRectangle (bounds, 10.0f);

    auto* block = eq();
    if (block == nullptr)
        return;

    g.saveState();
    g.reduceClipRegion (area.toNearestInt());

    // Spectrum: pre (outline) and post (filled).
    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.strokePath (spectrumPath (preSpectrum, false), juce::PathStrokeType (1.0f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff60a5fa).withAlpha (0.30f), 0.0f, area.getY() + area.getHeight() * 0.3f,
                                             juce::Colour (0xff60a5fa).withAlpha (0.03f), 0.0f, area.getBottom(), false));
    g.fillPath (spectrumPath (postSpectrum, true));

    // Grid.
    const float labelSize = compactMode ? 11.0f : 13.0f;
    g.setFont (juce::FontOptions (labelSize));
    for (int ihz : { 20, 30, 40, 50, 60, 70, 80, 90, 100, 200, 300, 400, 500, 600, 700, 800, 900,
                     1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000, 20000 })
    {
        const float hz = (float) ihz;
        const bool major = ihz == 20 || ihz == 50 || ihz == 100 || ihz == 200 || ihz == 500 || ihz == 1000
                        || ihz == 2000 || ihz == 5000 || ihz == 10000 || ihz == 20000;
        const float x = freqToX (hz);
        g.setColour (juce::Colours::white.withAlpha (major ? 0.09f : 0.035f));
        g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());
        if (major)
        {
            g.setColour (theme::textDim);
            const juce::String label = hz >= 1000.0f ? juce::String (juce::roundToInt (hz / 1000.0f)) + "k" : juce::String (juce::roundToInt (hz));
            g.drawText (label, juce::Rectangle<float> (x - 20.0f, area.getBottom() - labelSize - 4.0f, 40.0f, labelSize + 2.0f), juce::Justification::centred);
        }
    }
    const int range = juce::roundToInt (dbRange());
    for (int db = -range; db <= range; db += 6)
    {
        const float y = gainToY ((float) db);
        g.setColour (juce::Colours::white.withAlpha (db == 0 ? 0.16f : 0.06f));
        g.drawHorizontalLine (juce::roundToInt (y), area.getX(), area.getRight());
        if (std::abs (db) != range)
        {
            g.setColour (db == 0 ? juce::Colour (0xfffde68a) : theme::textDim);
            g.drawText ((db > 0 ? "+" : "") + juce::String (db), juce::Rectangle<float> (area.getRight() - 34.0f, y - 8.0f, 30.0f, 16.0f),
                        juce::Justification::centredRight);
        }
    }

    // Response curves.
    const double sr = block->getSampleRate();
    std::array<EqBand, EQ::kNumBands> bands;
    bool anyOn = false;
    for (int b = 0; b < EQ::kNumBands; ++b)
    {
        bands[(size_t) b] = block->getBand (b);
        anyOn |= bands[(size_t) b].on;
    }
    const float outputDb = block->getParam (EQ::OutputDb);
    const float zeroY = gainToY (0.0f);

    juce::Path total, totalFill, selected, selectedCurve;
    bool first = true;
    for (float x = area.getX(); x <= area.getRight() + 1.0f; x += 2.0f)
    {
        const float hz = xToFreq (x);
        float sum = outputDb;
        for (int b = 0; b < EQ::kNumBands; ++b)
            if (bands[(size_t) b].on)
            {
                const float db = eqBandMagnitudeDb (bands[(size_t) b], sr, hz);
                sum += db;
                if (b == selectedBand)
                {
                    // (Path::isEmpty() stays true until a line exists, so track "first" explicitly.)
                    const float by = gainToY (std::max (db, -dbRange() * 1.5f));
                    if (first) { selected.startNewSubPath (x, zeroY); selectedCurve.startNewSubPath (x, by); }
                    else       selectedCurve.lineTo (x, by);
                    selected.lineTo (x, by);
                }
            }
        const float y = gainToY (juce::jlimit (-dbRange() * 1.5f, dbRange() * 1.5f, sum));
        if (first) { total.startNewSubPath (x, y); totalFill.startNewSubPath (x, zeroY); }
        else       total.lineTo (x, y);
        totalFill.lineTo (x, y);
        first = false;
    }
    totalFill.lineTo (area.getRight() + 1.0f, zeroY);
    totalFill.closeSubPath();

    if (! selected.isEmpty())
    {
        selected.lineTo (area.getRight() + 1.0f, zeroY);
        selected.closeSubPath();
        g.setColour (bandColour (selectedBand).withAlpha (0.22f));
        g.fillPath (selected);
        g.setColour (bandColour (selectedBand).withAlpha (0.8f));
        g.strokePath (selectedCurve, juce::PathStrokeType (1.2f));
    }

    g.setColour (juce::Colour (0xfffde68a).withAlpha (0.07f));
    g.fillPath (totalFill);
    g.setColour (juce::Colour (0xfffff3c4));
    g.strokePath (total, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.restoreState();

    // Nodes.
    for (int b = 0; b < EQ::kNumBands; ++b)
    {
        if (! bands[(size_t) b].on)
            continue;
        const auto p = nodePosition (b);
        const auto colour = bandColour (b);
        const bool active = b == selectedBand || b == hoverBand;
        if (active)
        {
            g.setColour (colour.withAlpha (0.25f));
            g.fillEllipse (p.x - 15.0f, p.y - 15.0f, 30.0f, 30.0f);
        }
        g.setColour (colour);
        g.fillEllipse (p.x - kNodeRadius, p.y - kNodeRadius, 2 * kNodeRadius, 2 * kNodeRadius);
        g.setColour (b == selectedBand ? juce::Colours::white : juce::Colours::black.withAlpha (0.5f));
        g.drawEllipse (p.x - kNodeRadius, p.y - kNodeRadius, 2 * kNodeRadius, 2 * kNodeRadius, b == selectedBand ? 2.0f : 1.0f);
        g.setColour (juce::Colours::black.withAlpha (0.75f));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (juce::String (b + 1), juce::Rectangle<float> (p.x - kNodeRadius, p.y - kNodeRadius, 2 * kNodeRadius, 2 * kNodeRadius),
                    juce::Justification::centred);
    }

    // Readout for the hovered / dragged band.
    const int readoutBand = dragBand >= 0 ? dragBand : hoverBand;
    if (readoutBand >= 0 && bands[(size_t) readoutBand].on)
    {
        const auto& band = bands[(size_t) readoutBand];
        juce::String text = formatHz (band.frequency);
        if (hasGain (band.type)) text << "   " << (band.gainDb > 0 ? "+" : "") << juce::String (band.gainDb, 1) << " dB";
        text << "   Q " << juce::String (band.q, 2);
        const auto p = nodePosition (readoutBand);
        auto box = juce::Rectangle<float> (170.0f, 24.0f).withCentre ({ p.x, p.y - 28.0f });
        box = box.constrainedWithin (area.reduced (4.0f));
        g.setColour (juce::Colour (0xee11141c));
        g.fillRoundedRectangle (box, 6.0f);
        g.setColour (bandColour (readoutBand));
        g.drawRoundedRectangle (box, 6.0f, 1.0f);
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (12.5f));
        g.drawText (text, box, juce::Justification::centred);
    }

    if (! anyOn)
    {
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (compactMode ? 15.0f : 18.0f));
        g.drawText ("Doble clic para crear una banda", area, juce::Justification::centred);
    }

    // Strip background.
    if (selectedBand >= 0 || ! compactMode)
    {
        g.setColour (juce::Colour (0xe8141823));
        g.fillRoundedRectangle (stripArea.toFloat(), 10.0f);
        if (selectedBand < 0)
        {
            g.setColour (theme::textDim);
            g.setFont (juce::FontOptions (15.0f));
            g.drawText ("Selecciona una banda para editarla  -  doble clic en la curva para crear una nueva",
                        stripArea.withTrimmedRight (120), juce::Justification::centred);
        }
        if (selectedBand >= 0)
        {
            g.setColour (bandColour (selectedBand).withAlpha (0.7f));
            g.drawRoundedRectangle (stripArea.toFloat().reduced (0.5f), 10.0f, 1.2f);
            g.setColour (bandColour (selectedBand));
            g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
            g.drawText ("Banda " + juce::String (selectedBand + 1), stripArea.withWidth (80).reduced (10, 0), juce::Justification::centredLeft);
        }
    }
}

void EqView::resized()
{
    const auto area = plotArea().toNearestInt();
    expandButton.setBounds (area.getRight() - 40, area.getY() + 6, 32, 32);
    closeButton.setBounds (getWidth() - 100, 8, 90, 30);

    // Output knob: bottom-right corner (compact) or end of the strip (large).
    const int knob = compactMode ? 48 : 64;
    if (compactMode)
    {
        outputKnob.setBounds (area.getRight() - knob - 40, area.getBottom() - knob - 34, knob, knob + 18);
        outputLabel.setBounds (outputKnob.getX() - 10, outputKnob.getY() - 16, knob + 20, 16);
    }

    // Edit strip: floating at the bottom of the plot (compact) or below it (large).
    const int stripHeight = compactMode ? 74 : 88;
    stripArea = compactMode ? juce::Rectangle<int> (area.getX() + 60, area.getBottom() - stripHeight - 22, std::min (area.getWidth() - 220, 560), stripHeight)
                            : juce::Rectangle<int> (area.getX() + 40, area.getBottom() + 6, area.getWidth() - 80, stripHeight);
    auto s = stripArea.reduced (10, 6);
    s.removeFromLeft (80);   // "Banda n"
    auto column = [&s] (int width) { return s.removeFromLeft (width); };
    auto typeColumn = column (130);
    typeBox.setBounds (typeColumn.removeFromTop (s.getHeight() / 2).reduced (0, 4));
    slopeBox.setBounds (typeColumn.reduced (0, 4));
    s.removeFromLeft (10);
    for (auto [k, l] : { std::pair<juce::Slider*, juce::Label*> { &freqKnob, &freqLabel }, { &gainKnob, &gainLabel }, { &qKnob, &qLabel } })
    {
        auto c = column (compactMode ? 86 : 100);
        l->setBounds (c.removeFromTop (14));
        k->setBounds (c);
    }
    removeBand.setBounds (s.removeFromLeft (40).withSizeKeepingCentre (32, 32));
    if (! compactMode)
    {
        auto c = stripArea.withLeft (stripArea.getRight() - 110).reduced (10, 6);
        outputLabel.setBounds (c.removeFromTop (14));
        outputKnob.setBounds (c);
    }
}

//==============================================================================
void EqView::mouseDown (const juce::MouseEvent& e)
{
    const int band = bandAt (e.position);
    if (e.mods.isPopupMenu())
    {
        if (band >= 0)
        {
            selectBand (band);
            showBandMenu (band);
        }
        return;
    }

    selectBand (band);
    dragBand = band;
    if (band >= 0)
    {
        dragStart = e.position;
        dragStartBand = eq()->getBand (band);
    }
}

void EqView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragBand < 0 || eq() == nullptr)
        return;

    // Shift = fine adjustment (a quarter of the movement).
    const float scale = e.mods.isShiftDown() ? 0.25f : 1.0f;
    const auto delta = (e.position - dragStart) * scale;
    const float startX = freqToX (dragStartBand.frequency);
    setBandParam (dragBand, EQ::BandFreq, juce::jlimit (20.0f, 20000.0f, xToFreq (startX + delta.x)));

    if (hasGain (dragStartBand.type))
        setBandParam (dragBand, EQ::BandGain, juce::jlimit (-24.0f, 24.0f, yToGain (gainToY (dragStartBand.gainDb) + delta.y)));
    else
        setBandParam (dragBand, EQ::BandQ, juce::jlimit (0.1f, 18.0f, dragStartBand.q * std::exp2 (-delta.y / 40.0f)));
}

void EqView::mouseUp (const juce::MouseEvent&)
{
    dragBand = -1;
    repaint();
}

void EqView::mouseMove (const juce::MouseEvent& e)
{
    const int band = bandAt (e.position);
    if (band != hoverBand)
    {
        hoverBand = band;
        setMouseCursor (band >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::CrosshairCursor);
        repaint();
    }
}

void EqView::mouseExit (const juce::MouseEvent&)
{
    hoverBand = -1;
    repaint();
}

void EqView::mouseDoubleClick (const juce::MouseEvent& e)
{
    auto* block = eq();
    if (block == nullptr || ! plotArea().contains (e.position))
        return;

    if (const int band = bandAt (e.position); band >= 0)
    {
        setBandParam (band, EQ::BandOn, 0.0f);   // double-click a node: remove it
        selectBand (-1);
        return;
    }

    for (int b = 0; b < EQ::kNumBands; ++b)
    {
        if (block->getBand (b).on)
            continue;

        // New band where the user clicked: cuts at the extremes, bells elsewhere.
        const float hz = juce::jlimit (20.0f, 20000.0f, xToFreq (e.position.x));
        const auto type = hz < 35.0f ? EqBandType::LowCut : (hz > 16000.0f ? EqBandType::HighCut : EqBandType::Bell);
        setBandParam (b, EQ::BandType, (float) type);
        setBandParam (b, EQ::BandFreq, hz);
        setBandParam (b, EQ::BandGain, type == EqBandType::Bell ? juce::jlimit (-24.0f, 24.0f, yToGain (e.position.y)) : 0.0f);
        setBandParam (b, EQ::BandQ, type == EqBandType::Bell ? 1.0f : 0.7071f);
        setBandParam (b, EQ::BandOn, 1.0f);
        selectBand (b);
        return;
    }
}

void EqView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const int band = bandAt (e.position) >= 0 ? bandAt (e.position) : selectedBand;
    if (band < 0 || eq() == nullptr)
        return;
    const float q = eq()->getBand (band).q * std::exp2 (wheel.deltaY * (e.mods.isShiftDown() ? 0.5f : 2.0f));
    setBandParam (band, EQ::BandQ, juce::jlimit (0.1f, 18.0f, q));
}

void EqView::showBandMenu (int band)
{
    const auto current = eq()->getBand (band);
    juce::PopupMenu menu;
    menu.addSectionHeader ("Banda " + juce::String (band + 1));
    const char* types[] = { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass" };
    for (int t = 0; t < 7; ++t)
        menu.addItem (100 + t, types[t], true, (int) current.type == t);
    if (current.type == EqBandType::LowCut || current.type == EqBandType::HighCut)
    {
        juce::PopupMenu slopes;
        const char* names[] = { "12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct" };
        for (int sl = 0; sl < 4; ++sl)
            slopes.addItem (200 + sl, names[sl], true, current.slope == sl);
        menu.addSubMenu ("Pendiente", slopes);
    }
    menu.addSeparator();
    menu.addItem (1, "Quitar banda");

    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                        [safe = juce::Component::SafePointer<EqView> (this), band] (int result)
                        {
                            if (safe == nullptr || result == 0) return;
                            if (result == 1) { safe->setBandParam (band, EQ::BandOn, 0.0f); safe->selectBand (-1); }
                            else if (result >= 200) safe->setBandParam (band, EQ::BandSlope, (float) (result - 200));
                            else if (result >= 100) safe->setBandParam (band, EQ::BandType, (float) (result - 100));
                            safe->syncStripFromParams();
                        });
}
} // namespace kumamp
