#include "SimpView.h"

#include "../Presets/FactoryPresets.h"

#include <algorithm>

namespace kumamp
{
namespace
{
    juce::String utf8 (const char* text) { return juce::String (juce::CharPointer_UTF8 (text)); }
    constexpr int kCardWidth = 210, kCardHeight = 206, kCardGap = 10;
    constexpr int kColumnWidth = 150;

    void styleKnob (juce::Slider& s)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    }
} // namespace

SimpInfo simpInfoFor (BlockType type)
{
    switch (type)
    {
        case BlockType::NoiseGate:       return { "Calla el ruido cuando no est\xc3\xa1" "s tocando.", "threshold", "Umbral" };
        case BlockType::PitchShifter:    return { "Sube o baja el tono de lo que toc\xc3\xa1" "s.", "semitones", "Tono" };
        case BlockType::Wah:             return { "El filtro que dice \xc2\xab" "u\xc3\xa1\xc2\xbb" ". Se mueve con pedal o a mano.", "position", "Posici\xc3\xb3" "n" };
        case BlockType::AutoWah:         return { "Un wah que se mueve solo, seg\xc3\xba" "n qu\xc3\xa9" " tan fuerte toc\xc3\xa1" "s.", "sensitivity", "Sensibilidad" };
        case BlockType::Octaver:         return { "Suma una copia de tu guitarra una octava m\xc3\xa1" "s grave.", "sub", "Octava baja" };
        case BlockType::GuitarSynth:     return { "Convierte tu guitarra en un sintetizador.", "mix", "Cantidad" };
        case BlockType::Sitar:           return { "Hace sonar tu guitarra como un sitar.", "decay", "Duraci\xc3\xb3" "n", "tanpura", "Tampura" };
        case BlockType::Charango:        return { "Hace sonar tu guitarra como un charango.", "mix", "Cantidad" };
        case BlockType::Sampler:         return { "Toca un sonido cargado por vos, en el tono de tu guitarra.", "mix", "Cantidad" };
        case BlockType::Overdrive:       return { "Empuja y ensucia el sonido antes del amplificador.", "drive", "Suciedad" };
        case BlockType::NamAmp:          return { "El amplificador: le da su car\xc3\xa1" "cter a todo el sonido.", "input", "Ganancia" };
        case BlockType::IrLoader:        return { "El parlante: sin \xc3\xa9" "l, un amplificador suena \xc3\xa1" "spero.", "mix", "Cantidad" };
        case BlockType::Compressor:      return { "Empareja el volumen entre lo suave y lo fuerte.", "threshold", "Umbral" };
        case BlockType::ParametricEq:    return { "Sube o baja graves, medios y agudos. Se ajusta en Grid.", nullptr, nullptr };
        case BlockType::GraphicEq:       return { "Sube o baja graves, medios y agudos.", "level", "Volumen" };
        case BlockType::TransientShaper: return { "Hace el ataque de cada nota m\xc3\xa1" "s golpeado o m\xc3\xa1" "s suave.", "attack", "Ataque" };
        case BlockType::Chorus:          return { "Duplica la guitarra, como si tocaran dos a la vez.", "mix", "Cantidad" };
        case BlockType::Flanger:         return { "Un barrido met\xc3\xa1" "lico, como un avi\xc3\xb3" "n que pasa.", "mix", "Cantidad" };
        case BlockType::Phaser:          return { "Un remolino suave que da vueltas en el sonido.", "mix", "Cantidad" };
        case BlockType::Tremolo:         return { "Sube y baja el volumen en forma pareja.", "depth", "Profundidad" };
        case BlockType::Rotary:          return { "Imita un parlante que gira.", "speed", "Velocidad" };
        case BlockType::Univibe:         return { "Una ondulaci\xc3\xb3" "n c\xc3\xa1" "lida, de los a\xc3\xb1" "os 60.", "intensity", "Intensidad" };
        case BlockType::Bitcrusher:      return { "Rompe el sonido a prop\xc3\xb3" "sito, como un videojuego viejo.", "mix", "Cantidad" };
        case BlockType::TapeDelay:       return { "Repite lo que toc\xc3\xa1" "s, como una cinta vieja.", "mix", "Cantidad" };
        case BlockType::AnalogDelay:     return { "Repite lo que toc\xc3\xa1" "s, y cada repetici\xc3\xb3" "n es m\xc3\xa1" "s oscura.", "mix", "Cantidad" };
        case BlockType::DigitalDelay:    return { "Repite lo que toc\xc3\xa1" "s, limpio y exacto.", "mix", "Cantidad" };
        case BlockType::ReverseDelay:    return { "Repite lo que toc\xc3\xa1" "s, pero al rev\xc3\xa9" "s.", "mix", "Cantidad" };
        case BlockType::RepeatPercussion:return { "Corta el sonido en golpes parejos. Con Cortina, sigue sonando solo.", "feedback", "Cortina" };
        case BlockType::PlateReverb:     return { "El sonido de un lugar grande, brillante y parejo.", "mix", "Cantidad" };
        case BlockType::SpringReverb:    return { "El eco el\xc3\xa1" "stico de los amplificadores cl\xc3\xa1" "sicos.", "mix", "Cantidad" };
        case BlockType::HallReverb:      return { "Suena como si tocaras en una sala grande.", "mix", "Cantidad" };
        case BlockType::ShimmerReverb:   return { "Una reverb que brilla y sube hacia los agudos.", "mix", "Cantidad" };
        case BlockType::CloudReverb:     return { "Convierte lo que toc\xc3\xa1" "s en una nube larga.", "mix", "Cantidad" };
        case BlockType::Volume:          return { "Sube o baja el volumen en este punto.", "level", "Volumen" };
        default: break;
    }
    return { "", nullptr, nullptr };
}

//==============================================================================
class SimpView::Card final : public juce::Component
{
public:
    Card (std::shared_ptr<ParamStore> blockStore, int number) : store (std::move (blockStore)), index (number)
    {
        const auto info = simpInfoFor (store->getType());
        description = utf8 (info.description);
        addAndMakeVisible (power);
        power.setTooltip ("Prender / apagar");
        power.onClick = [this] { store->setBypassed (! power.getToggleState()); repaint(); };

        param = info.paramId != nullptr ? store->findParam (info.paramId) : -1;
        if (param >= 0)
        {
            const auto& p = store->getParamInfo (param);
            label = utf8 (info.paramLabel);
            styleKnob (knob);
            knob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            knob.setRange (p.minValue, p.maxValue, p.isDiscrete ? 1.0 : 0.0);
            knob.setDoubleClickReturnValue (true, p.defaultValue);
            knob.setColour (juce::Slider::trackColourId, theme::accent);
            knob.onValueChange = [this] { store->setParam (param, (float) knob.getValue()); repaint(); };
            addAndMakeVisible (knob);
        }

        option = info.optionId != nullptr ? store->findParam (info.optionId) : -1;
        if (option >= 0)
        {
            optionButton.setButtonText (utf8 (info.optionLabel));
            optionButton.setClickingTogglesState (true);
            optionButton.setColour (juce::TextButton::buttonColourId, theme::panel);
            optionButton.setTooltip ("Prende o apaga el drone de fondo");
            optionButton.onClick = [this] { store->setParam (option, optionButton.getToggleState() ? 1.0f : 0.0f); };
            addAndMakeVisible (optionButton);
        }
        sync();
    }

    void sync()
    {
        if (option >= 0)
            optionButton.setToggleState (store->getParam (option) > 0.5f, juce::dontSendNotification);

        const bool on = ! store->isBypassed();
        if (power.getToggleState() != on)
        {
            power.setToggleState (on, juce::dontSendNotification);
            repaint();
        }
        if (param >= 0 && ! knob.isMouseButtonDown() && ! juce::approximatelyEqual ((float) knob.getValue(), store->getParam (param)))
        {
            knob.setValue (store->getParam (param), juce::dontSendNotification);
            repaint();
        }
        knob.setAlpha (on ? 1.0f : 0.4f);
    }

    void paint (juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat();
        const bool on = ! store->isBypassed();
        const auto colour = theme::categoryColour (store->getType());
        g.setColour (theme::panel);
        g.fillRect (area);
        g.setColour (theme::border);
        g.drawRect (area, 1.0f);
        g.setColour (on ? colour : theme::border);
        g.fillRect (area.withHeight (5.0f));

        const float alpha = on ? 1.0f : 0.55f;
        auto content = getLocalBounds().reduced (12).withTrimmedTop (4);
        auto top = content.removeFromTop (30);
        g.setColour (theme::textDim.withAlpha (alpha));
        g.setFont (theme::mono (12.0f, true));
        g.drawText (juce::String (index), top.removeFromLeft (24), juce::Justification::centredLeft);
        g.setColour (theme::text.withAlpha (alpha));
        g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        g.drawFittedText (juce::String (store->getName()).toUpperCase(), top.withTrimmedRight (36), juce::Justification::centredLeft, 1, 0.75f);

        content.removeFromTop (6);
        g.setColour (theme::text.withAlpha (0.82f * alpha));
        g.setFont (juce::FontOptions (15.5f));
        g.drawFittedText (description, content.removeFromTop (64), juce::Justification::topLeft, 3, 1.0f);

        if (param >= 0)
        {
            auto text = getLocalBounds().reduced (12).removeFromBottom (72).withTrimmedLeft (78);
            g.setColour (theme::textDim.withAlpha (alpha));
            g.setFont (theme::mono (11.0f));
            g.drawText (label.toUpperCase(), text.removeFromTop (36), juce::Justification::bottomLeft);
            const auto& info = store->getParamInfo (param);
            const float value = store->getParam (param);
            g.setColour (theme::text.withAlpha (alpha));
            g.setFont (theme::mono (15.0f, true));
            g.drawText (juce::String (value, std::abs (value) < 10.0f && ! info.isDiscrete ? 1 : 0) + " " + info.unit, text, juce::Justification::topLeft);
        }
    }

    void resized() override
    {
        power.setBounds (getWidth() - 44, 12, 32, 32);
        knob.setBounds (getLocalBounds().reduced (12).removeFromBottom (72).removeFromLeft (72));
        optionButton.setBounds (12, 84, 112, 30);
    }

    ParamStore* getStore() const noexcept { return store.get(); }
    void setValue (float value) { knob.setValue (value, juce::sendNotificationSync); }
    float getKnobValue() const  { return (float) knob.getValue(); }
    void toggleOption()         { optionButton.setToggleState (! optionButton.getToggleState(), juce::sendNotificationSync); }
    void toggle()               { power.setToggleState (! power.getToggleState(), juce::sendNotificationSync); store->setBypassed (! power.getToggleState()); }

private:
    std::shared_ptr<ParamStore> store;
    int index, param = -1, option = -1;
    juce::String description, label;
    PowerButton power;
    juce::Slider knob;
    juce::TextButton optionButton;
};

//==============================================================================
class SimpView::PresetIcon final : public juce::Button
{
public:
    PresetIcon (theme::Icon iconToUse, const juce::String& name) : juce::Button (name), icon (iconToUse)
    {
        setTooltip ("Cargar el preset " + name);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        auto area = getLocalBounds().toFloat();
        const bool on = getToggleState();
        g.setColour (on ? theme::accent : (down ? theme::panelRaised.brighter (0.1f) : theme::panel));
        g.fillRect (area);
        if (! on)
        {
            g.setColour (highlighted ? theme::text : theme::border);
            g.drawRect (area, 1.0f);
        }
        const auto colour = on ? theme::ink : theme::text;
        theme::drawIcon (g, icon, area.removeFromTop (58.0f).withTrimmedTop (12.0f), colour, 1.9f);
        g.setColour (colour);
        g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
        g.drawText (getName().toUpperCase(), area.withTrimmedBottom (6.0f), juce::Justification::centred);
    }

private:
    theme::Icon icon;
};

//==============================================================================
SimpView::SimpView (GridEngine& e, juce::AudioProcessorValueTreeState& parameters) : engine (e)
{
    for (auto* knob : { &inputKnob, &outputKnob })
    {
        styleKnob (*knob);
        knob->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 24);
        knob->setColour (juce::Slider::trackColourId, theme::accent);
        knob->setTextValueSuffix (" dB");
        addAndMakeVisible (*knob);
    }
    inputAttachment = std::make_unique<SliderAttachment> (parameters, "input_gain", inputKnob);
    outputAttachment = std::make_unique<SliderAttachment> (parameters, "output_gain", outputKnob);

    const theme::Icon presetIcons[] { theme::Icon::Sine, theme::Icon::Bolt, theme::Icon::Drive, theme::Icon::Synth, theme::Icon::Note, theme::Icon::Strings };
    for (int i = 0; i < factorypresets::getNumSimpPresets(); ++i)
    {
        icons.push_back (std::make_unique<PresetIcon> (presetIcons[i], factorypresets::getSimpPresetName (i)));
        icons.back()->onClick = [this, i] { choosePreset (i); };
        addAndMakeVisible (*icons.back());
    }

    viewport.setViewedComponent (&cardArea, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);
    rebuild();
}

SimpView::~SimpView()
{
    stopTimer();
}

void SimpView::visibilityChanged()
{
    if (isVisible())
    {
        refresh();
        startTimerHz (15);
    }
    else
    {
        stopTimer();
    }
}

void SimpView::timerCallback()
{
    refresh();
}

void SimpView::refresh()
{
    if (engine.getModelVersion() != shownVersion)
        rebuild();
    for (auto& card : cards)
        card->sync();

    if (const int active = getActivePreset(); active != shownActive)
    {
        shownActive = active;
        for (int i = 0; i < (int) icons.size(); ++i)
            icons[(size_t) i]->setToggleState (i == active, juce::dontSendNotification);
    }
}

int SimpView::getActivePreset() const
{
    const auto name = presetNameSource ? presetNameSource() : juce::String();
    for (int i = 0; i < (int) icons.size(); ++i)
        if (factorypresets::getSimpPresetName (i) == name)
            return i;
    return -1;
}

void SimpView::choosePreset (int preset)
{
    if (onPresetChosen)
        onPresetChosen (preset);
    refresh();
}

void SimpView::rebuild()
{
    shownVersion = engine.getModelVersion();
    cards.clear();

    std::vector<const GridNode*> nodes;
    for (const auto& node : engine.getModel().getNodes())
        if (node.kind == NodeKind::Block)
            nodes.push_back (&node);
    std::stable_sort (nodes.begin(), nodes.end(), [] (const GridNode* a, const GridNode* b)
    {
        return a->column != b->column ? a->column < b->column : a->row < b->row;
    });

    int number = 1;
    for (const auto* node : nodes)
        if (auto store = engine.getStore (node->store))
        {
            cards.push_back (std::make_unique<Card> (std::move (store), number++));
            cardArea.addAndMakeVisible (*cards.back());
        }
    layoutCards();
}

void SimpView::layoutCards()
{
    const int width = viewport.getMaximumVisibleWidth();
    const int columns = std::max (1, (width + kCardGap) / (kCardWidth + kCardGap));
    const int cardWidth = (width - (columns - 1) * kCardGap) / columns;
    for (int i = 0; i < (int) cards.size(); ++i)
        cards[(size_t) i]->setBounds ((i % columns) * (cardWidth + kCardGap), (i / columns) * (kCardHeight + kCardGap), cardWidth, kCardHeight);
    const int rows = ((int) cards.size() + columns - 1) / columns;
    cardArea.setSize (width, std::max (1, rows * (kCardHeight + kCardGap) - kCardGap));
}

void SimpView::resized()
{
    auto area = getLocalBounds().reduced (16);
    auto row = area.removeFromTop (92);
    const int iconWidth = (row.getWidth() - 10 * ((int) icons.size() - 1)) / std::max (1, (int) icons.size());
    for (auto& icon : icons)
    {
        icon->setBounds (row.removeFromLeft (iconWidth));
        row.removeFromLeft (10);
    }
    area.removeFromTop (10);

    inputColumn = area.removeFromLeft (kColumnWidth);
    area.removeFromLeft (10);
    outputColumn = area.removeFromRight (kColumnWidth);
    area.removeFromRight (10);
    viewport.setBounds (area);
    layoutCards();

    for (auto [knob, column] : { std::pair { &inputKnob, inputColumn }, std::pair { &outputKnob, outputColumn } })
        knob->setBounds (column.withSizeKeepingCentre (118, 150));
}

void SimpView::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    const auto paintEnd = [&g] (juce::Rectangle<int> column, const char* title, const char* text)
    {
        theme::drawCard (g, column.toFloat());
        auto area = column.reduced (12, 16);
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        g.drawText (title, area.removeFromTop (30), juce::Justification::centred);
        g.setColour (theme::text.withAlpha (0.82f));
        g.setFont (juce::FontOptions (15.5f));
        g.drawFittedText (utf8 (text), area.removeFromTop (100), juce::Justification::centredTop, 5, 1.0f);
    };
    paintEnd (inputColumn, "ENTRADA", "Cu\xc3\xa1" "nta se\xc3\xb1" "al de tu guitarra entra. Despu\xc3\xa9" "s pasa por cada tarjeta, en orden.");
    paintEnd (outputColumn, "SALIDA", "El volumen final, despu\xc3\xa9" "s de todo lo que hay en el medio.");
}

ParamStore* SimpView::getCardStore (int card) const  { return cards[(size_t) card]->getStore(); }
void SimpView::setCardValue (int card, float value)  { cards[(size_t) card]->setValue (value); }
void SimpView::toggleCard (int card)                 { cards[(size_t) card]->toggle(); }
void SimpView::toggleCardOption (int card)           { cards[(size_t) card]->toggleOption(); }
float SimpView::getCardKnobValue (int card) const    { return cards[(size_t) card]->getKnobValue(); }
} // namespace kumamp
