#include "EditorSections.h"
#include "../Core/PitchDetector.h"

namespace kumamp
{
namespace
{
    void drawSectionTitle (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title)
    {
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (title, area, juce::Justification::centredLeft);
    }

    void styleKnob (juce::Slider& s, int textBoxWidth = 70, int textBoxHeight = 22)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, textBoxWidth, textBoxHeight);
        s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    }
} // namespace

//==============================================================================
void HeaderBar::Tab::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    g.drawText (getName(), area, juce::Justification::centred);

    // Underlined like a link on the website: red when active, hairline on hover.
    if (getToggleState() || highlighted)
    {
        const float w = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), getName());
        g.setColour (getToggleState() ? theme::accent : theme::border);
        g.fillRect (juce::Rectangle<float> (w, 3.0f).withCentre ({ area.getCentreX(), area.getCentreY() + 15.0f }));
    }
}

HeaderBar::HeaderBar()
{
    const char* names[] = { "GRID", "PRESETS", "SNAPSHOTS", "IRs", "SETTINGS" };
    for (int i = 0; i < 5; ++i)
    {
        tabs[(size_t) i] = std::make_unique<Tab> (names[i]);
        tabs[(size_t) i]->onClick = [this, i] { setActiveTab (i); if (onTabChanged) onTabChanged (i); };
        addAndMakeVisible (*tabs[(size_t) i]);
    }
    setActiveTab (0);

    fieldButton.setTooltip ("Un instrumento aparte para descubrir sonidos, sin tocar el Grid");
    fieldButton.onClick = [this] { if (onFieldClicked) onFieldClicked(); };
    simpButton.setTooltip (juce::String (juce::CharPointer_UTF8 ("La misma cadena de la forma m\xc3\xa1s simple, explicando qu\xc3\xa9 hace cada cosa")));
    simpButton.onClick = [this] { if (onSimpClicked) onSimpClicked(); };
    aboutButton.onClick = [this] { if (onAboutClicked) onAboutClicked(); };
    for (auto* button : { &fieldButton, &simpButton })
    {
        button->setColour (juce::TextButton::buttonColourId, theme::panel);
        addAndMakeVisible (*button);
    }
    addAndMakeVisible (aboutButton);
}

void HeaderBar::setViewButtons (bool fieldOpen, bool simpOpen)
{
    fieldButton.setToggleState (fieldOpen, juce::dontSendNotification);
    simpButton.setToggleState (simpOpen, juce::dontSendNotification);
}

void HeaderBar::setActiveTab (int tab)
{
    for (int i = 0; i < 5; ++i)
        tabs[(size_t) i]->setToggleState (i == tab, juce::dontSendNotification);
}

void HeaderBar::setStats (double cpuProportion, double sampleRate, int blockSize, bool running, double bpm)
{
    bpmText = juce::String (bpm, std::abs (bpm - std::round (bpm)) < 0.05 ? 0 : 1) + " BPM";
    cpuText = "CPU " + juce::String (juce::roundToInt (cpuProportion * 100.0)) + "%";
    rateText = sampleRate > 0.0 ? juce::String (sampleRate / 1000.0, sampleRate == 44100.0 ? 1 : 0) + " kHz" : "-- kHz";
    blockText = blockSize > 0 ? juce::String (blockSize) + " smp" : "-- smp";
    audioRunning = running;
    repaint (statsArea);
}

void HeaderBar::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
    g.setColour (theme::border);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    // Logo + name.
    auto area = getLocalBounds().reduced (24, 10);
    const juce::Font title (theme::display (36.0f));
    g.setFont (title);
    g.setColour (theme::text);
    g.drawText ("KUMAMP", area, juce::Justification::centredLeft);
    area.removeFromLeft (juce::GlyphArrangement::getStringWidthInt (title, "KUMAMP") + 10);
    g.setFont (theme::mono (17.0f, true));
    g.setColour (theme::accent);
    g.drawText ("//", area.removeFromLeft (28), juce::Justification::centredLeft);
    g.setFont (theme::mono (11.0f));
    g.setColour (theme::textDim);
    auto subtitle = area.removeFromLeft (180);
    g.drawText ("NAM 0.5-0.7", subtitle.removeFromTop (subtitle.getHeight() / 2), juce::Justification::bottomLeft);
    g.drawText ("TONE3000 / VST3 / AU", subtitle, juce::Justification::topLeft);

    // Stats.
    auto stats = statsArea;
    g.setFont (theme::mono (13.0f));
    auto dot = stats.removeFromRight (20).toFloat().withSizeKeepingCentre (9.0f, 9.0f);
    g.setColour (audioRunning ? theme::accent : theme::textDim);
    g.fillRect (dot);
    stats.removeFromRight (10);
    for (const auto* textItem : { &blockText, &rateText, &bpmText, &cpuText })
    {
        const int w = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), *textItem) + 8;
        g.setColour (theme::text);
        g.drawText (*textItem, stats.removeFromRight (w), juce::Justification::centredRight);
        if (textItem != &cpuText)
        {
            auto sep = stats.removeFromRight (22);
            g.setColour (theme::border);
            g.drawVerticalLine (sep.getCentreX(), (float) sep.getY() + 12.0f, (float) sep.getBottom() - 12.0f);
        }
    }
}

void HeaderBar::resized()
{
    auto area = getLocalBounds();
    const int tabsWidth = std::min (640, getWidth() - 900);
    tabArea = juce::Rectangle<int> (tabsWidth, 48).withCentre ({ getWidth() / 2 + 40, getHeight() / 2 });
    auto t = tabArea.reduced (4);
    const int w = t.getWidth() / 5;
    for (auto& tab : tabs)
        tab->setBounds (t.removeFromLeft (w));
    statsArea = area.removeFromRight (420).reduced (20, 10);
    fieldButton.setBounds (tabArea.getX() - 184, getHeight() / 2 - 19, 86, 38);
    simpButton.setBounds (tabArea.getX() - 92, getHeight() / 2 - 19, 76, 38);
    aboutButton.setBounds (tabArea.getRight() + 4, getHeight() / 2 - 17, 34, 34);
}

//==============================================================================
Sidebar::Sidebar (juce::AudioProcessorValueTreeState& parameters)
{
    for (auto* s : { &inputKnob, &gateThreshold, &gateRelease, &pitchKnob })
    {
        styleKnob (*s);
        addAndMakeVisible (*s);
    }
    for (auto* s : { &gateThreshold, &gateRelease })
    {
        s->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s->onValueChange = [this] { repaint (gateCard); };
    }
    addAndMakeVisible (inputMeter);
    addAndMakeVisible (gatePower);
    addAndMakeVisible (pitchPower);

    inputAttachment     = std::make_unique<SliderAttachment> (parameters, "input_gain", inputKnob);
    thresholdAttachment = std::make_unique<SliderAttachment> (parameters, "gate_threshold", gateThreshold);
    releaseAttachment   = std::make_unique<SliderAttachment> (parameters, "gate_release", gateRelease);
    pitchAttachment     = std::make_unique<SliderAttachment> (parameters, "pitch_semitones", pitchKnob);
    gateAttachment      = std::make_unique<ButtonAttachment> (parameters, "gate_on", gatePower);
    pitchOnAttachment   = std::make_unique<ButtonAttachment> (parameters, "pitch_on", pitchPower);

    inputKnob.textFromValueFunction     = [] (double v) { return juce::String (v, 1) + " dB"; };
    gateThreshold.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)) + " dB"; };
    gateRelease.textFromValueFunction   = [] (double v) { return juce::String (juce::roundToInt (v)) + " ms"; };
    pitchKnob.textFromValueFunction     = [] (double v) { return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v)) + " st"; };
    for (auto* s : { &inputKnob, &gateThreshold, &gateRelease, &pitchKnob })
        s->updateText();

    for (auto* b : { &guitarButton, &bassButton })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1);
        addAndMakeVisible (*b);
    }
    guitarButton.setConnectedEdges (juce::Button::ConnectedOnRight);
    bassButton.setConnectedEdges (juce::Button::ConnectedOnLeft);
    if (auto* instrument = parameters.getParameter ("pitch_instrument"))
    {
        instrumentAttachment = std::make_unique<juce::ParameterAttachment> (*instrument, [this] (float index)
        {
            const bool bass = index >= 0.5f;
            guitarButton.setToggleState (! bass, juce::dontSendNotification);
            bassButton.setToggleState (bass, juce::dontSendNotification);
        });
        guitarButton.onClick = [this] { if (guitarButton.getToggleState()) instrumentAttachment->setValueAsCompleteGesture (0.0f); };
        bassButton.onClick   = [this] { if (bassButton.getToggleState())   instrumentAttachment->setValueAsCompleteGesture (1.0f); };
        instrumentAttachment->sendInitialUpdate();
    }
    for (auto* b : { &classicGateButton, &advancedGateButton })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (2);
        addAndMakeVisible (*b);
    }
    classicGateButton.setConnectedEdges (juce::Button::ConnectedOnRight);
    advancedGateButton.setConnectedEdges (juce::Button::ConnectedOnLeft);
    if (auto* mode = parameters.getParameter ("gate_mode"))
    {
        gateModeAttachment = std::make_unique<juce::ParameterAttachment> (*mode, [this] (float index)
        {
            const bool advanced = index >= 0.5f;
            classicGateButton.setToggleState (! advanced, juce::dontSendNotification);
            advancedGateButton.setToggleState (advanced, juce::dontSendNotification);
        });
        classicGateButton.onClick  = [this] { if (classicGateButton.getToggleState())  gateModeAttachment->setValueAsCompleteGesture (0.0f); };
        advancedGateButton.onClick = [this] { if (advancedGateButton.getToggleState()) gateModeAttachment->setValueAsCompleteGesture (1.0f); };
        gateModeAttachment->sendInitialUpdate();
    }
    classicGateButton.setTooltip (juce::CharPointer_UTF8 ("Gate cl\xc3\xa1sico: una sola banda"));
    advancedGateButton.setTooltip (juce::CharPointer_UTF8 ("Gate para guitarra: ignora el zumbido y cierra los agudos (el soplido) "
                                                          "antes que el cuerpo de la nota, as\xc3\xad no se escapa el hiss en las colas"));

    guitarButton.setTooltip ("Pitch para guitarra: la menor latencia");
    bassButton.setTooltip ("Pitch para bajo: notas graves estables (algo mas de latencia)");

    gatePower.setTooltip ("Noise gate de entrada");
    pitchPower.setTooltip ("Transposicion global (polifonica)");
    setTooltip ("Clic en el afinador para abrirlo en grande");
}

Sidebar::~Sidebar() = default;

void Sidebar::setTunerReading (const Tuner::Reading& r)
{
    reading = r;
    if (r.valid)
    {
        lastValid = r;
        lastValidTime = juce::Time::getMillisecondCounter();
    }
    repaint (tunerCard);
}

void Sidebar::mouseUp (const juce::MouseEvent& e)
{
    if (tunerCard.contains (e.getPosition()) && onTunerClicked)
        onTunerClicked();
}

void Sidebar::paint (juce::Graphics& g)
{
    theme::drawCard (g, getLocalBounds().toFloat());

    // Divider lines between sections.
    g.setColour (theme::border);
    for (const auto& card : { inputCard, gateCard, pitchCard })
        g.drawHorizontalLine (card.getY() - 1, (float) getX() + 10.0f, (float) getWidth() - 10.0f);

    // --- Tuner --------------------------------------------------------------------
    {
        auto area = tunerCard.reduced (18, 12);
        auto top = area.removeFromTop (40);
        drawSectionTitle (g, top.removeFromLeft (90), "TUNER");

        const bool held = ! reading.valid && juce::Time::getMillisecondCounter() - lastValidTime < 1500 && lastValid.valid;
        const auto& r = reading.valid ? reading : lastValid;
        const bool show = reading.valid || held;
        const float alpha = reading.valid ? 1.0f : 0.45f;

        if (show)
        {
            const juce::String note (noteName (r.midiNote));
            g.setColour (theme::text.withAlpha (alpha));
            g.setFont (theme::display (38.0f));
            g.drawText (note, top.withTrimmedRight (60), juce::Justification::centred);
            g.setColour (theme::textDim.withAlpha (alpha));
            g.setFont (theme::mono (13.0f));
            g.drawText ((r.cents >= 0 ? "+" : "") + juce::String (r.cents, 1) + " ct", top.removeFromRight (70), juce::Justification::centredRight);
        }
        else
        {
            g.setColour (theme::textDim);
            g.setFont (juce::FontOptions (14.0f));
            g.drawText (juce::CharPointer_UTF8 ("\xe2\x80\x94"), top.withTrimmedRight (60), juce::Justification::centred);
        }

        // Segmented cents meter: centre segment green, the active one lit.
        auto meter = area.removeFromTop (34).reduced (4, 6);
        constexpr int segments = 31;
        const float w = (float) meter.getWidth() / segments;
        const int active = show ? juce::jlimit (0, segments - 1, juce::roundToInt ((r.cents + 50.0f) / 100.0f * (segments - 1))) : -1;
        for (int i = 0; i < segments; ++i)
        {
            const float distance = std::abs ((float) i - (segments - 1) * 0.5f) / ((segments - 1) * 0.5f);
            const float height = (float) meter.getHeight() * (0.45f + 0.55f * (1.0f - distance));
            auto seg = juce::Rectangle<float> ((float) meter.getX() + w * (float) i + 1.0f, (float) meter.getBottom() - height, w - 2.0f, height);
            juce::Colour c = theme::border;
            if (i == active)
                c = std::abs (r.cents) <= 3.0f ? theme::good : (std::abs (r.cents) <= 10.0f ? theme::warn : theme::bad);
            else if (i == segments / 2)
                c = theme::text;
            g.setColour (c.withMultipliedAlpha (i == active ? alpha : 1.0f));
            g.fillRect (seg);
        }
    }

    drawSectionTitle (g, inputCard.reduced (18, 12).removeFromTop (24), "INPUT");
    drawSectionTitle (g, gateCard.reduced (18, 8).removeFromTop (24), "GATE");
    drawSectionTitle (g, pitchCard.reduced (18, 10).removeFromTop (24), "PITCH");

    // Gate: caption and value pill under each knob.
    for (auto* s : { &gateThreshold, &gateRelease })
    {
        const auto b = s->getBounds();
        const auto caption = juce::Rectangle<int> (b.getX() - 10, b.getBottom() + 2, b.getWidth() + 20, 18);
        const auto pill = juce::Rectangle<int> (b.getCentreX() - 34, caption.getBottom() + 3, 68, 22).toFloat();
        g.setColour (theme::textDim);
        g.setFont (theme::mono (11.0f));
        g.drawText (s == &gateThreshold ? "THRESH" : "RELEASE", caption, juce::Justification::centred);
        g.setColour (theme::border);
        g.fillRect (pill.withTop (pill.getBottom() - 1.0f));
        g.setColour (theme::text);
        g.setFont (theme::mono (13.0f));
        g.drawText (s->getTextFromValue (s->getValue()), pill, juce::Justification::centred);
    }

    // Pitch range captions.
    const auto pk = pitchKnob.getBounds();
    g.setColour (theme::textDim);
    g.setFont (theme::mono (11.0f));
    g.drawText ("-12", juce::Rectangle<int> (pk.getX() - 34, pk.getBottom() - 40, 30, 16), juce::Justification::centredRight);
    g.drawText ("+12", juce::Rectangle<int> (pk.getRight() + 4, pk.getBottom() - 40, 30, 16), juce::Justification::centredLeft);
}

void Sidebar::resized()
{
    auto area = getLocalBounds();
    const int h = area.getHeight();
    tunerCard = area.removeFromTop (h * 19 / 100);
    inputCard = area.removeFromTop (h * 22 / 100);
    gateCard  = area.removeFromTop (h * 30 / 100);
    pitchCard = area;

    {
        auto a = inputCard.reduced (18, 10);
        a.removeFromTop (20);
        inputMeter.setBounds (a.removeFromRight (34).reduced (4, 8));
        inputKnob.setBounds (a.withSizeKeepingCentre (std::min (a.getWidth(), 120), a.getHeight()));
    }
    {
        auto a = gateCard.reduced (14, 8);
        auto top = a.removeFromTop (28);
        gatePower.setBounds (top.removeFromRight (30));
        top.removeFromRight (10);
        const int switchWidth = std::min (172, top.getWidth() - 60);
        auto switchRow = top.removeFromRight (switchWidth).withSizeKeepingCentre (switchWidth, 26);
        classicGateButton.setBounds (switchRow.removeFromLeft (switchRow.getWidth() / 2));
        advancedGateButton.setBounds (switchRow);
        a.removeFromBottom (44);   // caption + value pill, painted in paint()
        const int w = a.getWidth() / 2;
        const int knob = std::min (a.getHeight(), w - 30);
        gateThreshold.setBounds (a.removeFromLeft (w).withSizeKeepingCentre (knob, knob));
        gateRelease.setBounds (a.withSizeKeepingCentre (knob, knob));
    }
    {
        auto a = pitchCard.reduced (14, 10);
        auto top = a.removeFromTop (28);
        pitchPower.setBounds (top.removeFromRight (30));
        // Guitarra / Bajo switch shares the title row, left of the power button.
        top.removeFromRight (10);
        const int switchWidth = std::min (172, top.getWidth() - 70);
        auto switchRow = top.removeFromRight (switchWidth).withSizeKeepingCentre (switchWidth, 26);
        guitarButton.setBounds (switchRow.removeFromLeft (switchRow.getWidth() * 11 / 20));
        bassButton.setBounds (switchRow);
        pitchKnob.setBounds (a.withSizeKeepingCentre (std::min (a.getWidth() - 110, 120), a.getHeight()).translated (-20, 0));
    }
}

//==============================================================================
SceneBar::SceneBar()
{
    const char* letters[] = { "A", "B", "C", "D" };
    for (int i = 0; i < 4; ++i)
    {
        auto& b = sceneButtons[(size_t) i];
        b.setButtonText (letters[i]);
        b.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        b.onClick = [this, i] { if (onSceneSelected) onSceneSelected (i); };
        addAndMakeVisible (b);
    }
    previous.onClick = [this] { if (onSceneSelected) onSceneSelected ((activeScene + 3) % 4); };
    next.onClick     = [this] { if (onSceneSelected) onSceneSelected ((activeScene + 1) % 4); };
    store.onClick    = [this] { if (onStore) onStore(); };
    for (auto* b : { &previous, &next, &store })
        addAndMakeVisible (*b);

    prefix.setJustificationType (juce::Justification::centredRight);
    prefix.setFont (theme::mono (13.0f));
    prefix.setColour (juce::Label::textColourId, theme::textDim);
    addAndMakeVisible (prefix);

    name.setJustificationType (juce::Justification::centredLeft);
    name.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    name.setEditable (false, true, false);
    name.setTooltip ("Doble clic para renombrar la escena");
    name.onTextChange = [this] { if (onNameEdited) onNameEdited (activeScene, name.getText()); };
    addAndMakeVisible (name);

    setActiveScene (0);
}

void SceneBar::setActiveScene (int scene)
{
    activeScene = scene;
    for (int i = 0; i < 4; ++i)
        sceneButtons[(size_t) i].setToggleState (i == scene, juce::dontSendNotification);
    prefix.setText (juce::String ("Scene ") + juce::String::charToString ((juce::juce_wchar) ('A' + scene))
                        + juce::String (juce::CharPointer_UTF8 (" \xe2\x80\x93")),
                    juce::dontSendNotification);
}

void SceneBar::setSceneName (const juce::String& newName)
{
    if (! name.isBeingEdited() && name.getText() != newName)
        name.setText (newName.isEmpty() ? juce::String ("Sin nombre") : newName, juce::dontSendNotification);
}

void SceneBar::paint (juce::Graphics& g)
{
    g.setColour (theme::border);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());
}

void SceneBar::resized()
{
    auto area = getLocalBounds().reduced (24, 10);
    for (auto& b : sceneButtons)
        b.setBounds (area.removeFromLeft (56).reduced (3, 0));
    area.removeFromLeft (24);
    previous.setBounds (area.removeFromLeft (36));
    next.setBounds (area.removeFromLeft (36));

    store.setBounds (area.removeFromRight (44));

    const int centreWidth = std::min (area.getWidth(), 460);
    auto centre = area.withSizeKeepingCentre (centreWidth, area.getHeight());
    prefix.setBounds (centre.removeFromLeft (centreWidth * 2 / 5));
    name.setBounds (centre);
}

//==============================================================================
void BlockPalette::Item::paint (juce::Graphics& g)
{
    const auto& info = theme::paletteGroups()[(size_t) group];
    const bool hot = isMouseOver() || palette.highlighted == group;
    const auto colour = theme::categoryColour (info.types.front());
    auto area = getLocalBounds().toFloat().reduced (3.0f);

    const bool active = palette.highlighted == group;
    const auto foreground = active ? theme::ink : theme::text;
    g.setColour (active ? colour : theme::panel);
    g.fillRect (area);
    g.setColour (active ? colour : (hot ? theme::text : theme::border));
    g.drawRect (area, 1.0f);

    auto iconArea = area.removeFromTop (area.getHeight() * 0.58f).reduced (0.0f, 6.0f);
    theme::drawIcon (g, info.icon, iconArea.withTrimmedTop (4.0f), foreground);
    g.setColour (foreground);
    g.setFont (theme::mono (11.0f, true));
    g.drawText (juce::String (info.name).toUpperCase(), area.withTrimmedBottom (4.0f), juce::Justification::centred);
}

void BlockPalette::Item::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 6)
        return;
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
    {
        dragging = true;
        container->startDragging ("group:" + juce::String (group), this,
                                  juce::ScaledImage (createComponentSnapshot (getLocalBounds())), true);
    }
}

void BlockPalette::Item::mouseUp (const juce::MouseEvent&)
{
    if (! dragging && palette.onGroupClicked)
        palette.onGroupClicked (group, *this);
    dragging = false;
}

BlockPalette::BlockPalette()
{
    add.onClick = [this] { if (onAddClicked) onAddClicked (add); };
    addAndMakeVisible (add);
    for (int i = 0; i < (int) theme::paletteGroups().size(); ++i)
    {
        items.push_back (std::make_unique<Item> (*this, i));
        items.back()->setTooltip ("Clic: elegir bloque  |  Arrastrar a un cable: insertar ahi");
        addAndMakeVisible (*items.back());
    }
}

void BlockPalette::setHighlightedGroup (int group)
{
    if (group != highlighted)
    {
        highlighted = group;
        for (auto& item : items) item->repaint();
    }
}

void BlockPalette::paint (juce::Graphics& g)
{
    theme::drawCard (g, getLocalBounds().toFloat());
}

void BlockPalette::resized()
{
    auto area = getLocalBounds().reduced (12, 10);
    add.setBounds (area.removeFromLeft (52).withSizeKeepingCentre (48, 48));
    area.removeFromLeft (16);
    const int w = area.getWidth() / (int) items.size();
    for (auto& item : items)
        item->setBounds (area.removeFromLeft (w));
}
} // namespace kumamp
