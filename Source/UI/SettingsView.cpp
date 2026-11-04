#include "SettingsView.h"
#include "../Core/PitchDetector.h"
#include <algorithm>
#include <cmath>

namespace kumamp
{
namespace
{
    void drawTitle (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title, const juce::String& subtitle)
    {
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        g.drawText (title, area.removeFromTop (28), juce::Justification::centredLeft);
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (subtitle, area.removeFromTop (20), juce::Justification::centredLeft);
    }

    void drawCaption (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text)
    {
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (text, area, juce::Justification::centredLeft);
    }
} // namespace

SettingsView::SettingsView (juce::AudioProcessorValueTreeState& parameters)
{
    for (int c = 1; c <= 16; ++c)
        channel.addItem ("Canal " + juce::String (c), c);
    // juce::String (CharPointer_UTF8): a bare const char* would be read as Latin-1 ("Â±").
    bendRange.addItemList ({ juce::String ("Sin pitch bend"), juce::String (juce::CharPointer_UTF8 ("\xc2\xb1" "2 semitonos")),
                             juce::String (juce::CharPointer_UTF8 ("\xc2\xb1" "12 semitonos")),
                             juce::String (juce::CharPointer_UTF8 ("\xc2\xb1" "24 semitonos")) }, 1);

    for (juce::Component* c : { (juce::Component*) &midiOut, (juce::Component*) &channel, (juce::Component*) &bendRange,
                                (juce::Component*) &threshold, (juce::Component*) &transpose, (juce::Component*) &pcScenes,
                                (juce::Component*) &ccMacros })
        addAndMakeVisible (c);

    midiOutAttachment   = std::make_unique<ButtonAttachment> (parameters, "midi_out_on", midiOut);
    channelAttachment   = std::make_unique<ComboAttachment> (parameters, "midi_out_channel", channel);
    bendAttachment      = std::make_unique<ComboAttachment> (parameters, "midi_out_bend", bendRange);
    thresholdAttachment = std::make_unique<SliderAttachment> (parameters, "midi_out_threshold", threshold);
    transposeAttachment = std::make_unique<SliderAttachment> (parameters, "midi_out_transpose", transpose);
    pcAttachment        = std::make_unique<ButtonAttachment> (parameters, "midi_pc_scenes", pcScenes);
    ccAttachment        = std::make_unique<ButtonAttachment> (parameters, "midi_cc_macros", ccMacros);

    threshold.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)) + " dB"; };
    transpose.textFromValueFunction = [] (double v) { return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v)) + " st"; };
    threshold.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 24);
    transpose.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 24);
    threshold.updateText();
    transpose.updateText();
}

SettingsView::~SettingsView()
{
    stopTimer();
}

void SettingsView::visibilityChanged()
{
    if (isVisible()) startTimerHz (30);
    else             stopTimer();
}

void SettingsView::timerCallback()
{
    note     = noteSource ? noteSource() : -1;
    velocity = velocitySource ? velocitySource() : 0;
    bend     = bendSource ? bendSource() : 0.0f;
    repaint (activityArea);

    const auto count = midiInCountSource ? midiInCountSource() : 0u;
    if (count != midiInCount)
        lastMidiInTime = juce::Time::getMillisecondCounterHiRes();
    midiInCount = count;
    lastMidiIn = midiInSource ? midiInSource() : 0u;
    repaint (midiInArea);
}

void SettingsView::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
    theme::drawCard (g, outCard.toFloat());
    theme::drawCard (g, controlCard.toFloat());

    drawTitle (g, outCard.reduced (22, 16), juce::CharPointer_UTF8 ("Guitarra \xe2\x86\x92 MIDI"),
               "Toca cualquier VST instrumento desde la guitarra (monofonico: una nota a la vez).");
    drawTitle (g, controlCard.reduced (22, 16), "Control MIDI",
               "Controla KumAmp desde una pedalera MIDI o un pedal de expresion.");

    drawCaption (g, channel.getBounds().withX (outCard.getX() + 22).withWidth (150), "Canal de salida");
    drawCaption (g, bendRange.getBounds().withX (outCard.getX() + 22).withWidth (150), "Pitch bend");
    drawCaption (g, threshold.getBounds().withX (outCard.getX() + 22).withWidth (150), "Sensibilidad");
    drawCaption (g, transpose.getBounds().withX (outCard.getX() + 22).withWidth (150), "Transponer");

    // Live activity: note, velocity bar, bend.
    auto a = activityArea;
    g.setColour (theme::panelRaised);
    g.fillRoundedRectangle (a.toFloat(), 10.0f);
    a.reduce (16, 10);
    const bool playing = note >= 0;
    g.setColour (playing ? theme::good : theme::textDim);
    g.setFont (juce::FontOptions (34.0f, juce::Font::bold));
    g.drawText (playing ? juce::String (noteName (note)) + juce::String (noteOctave (note)) : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")),
                a.removeFromLeft (110), juce::Justification::centredLeft);
    auto info = a.removeFromTop (a.getHeight() / 2);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (15.0f));
    g.drawText (playing ? "Nota MIDI " + juce::String (note) + "   vel " + juce::String (velocity) : juce::String ("Sin nota"),
                info, juce::Justification::centredLeft);
    auto bar = a.reduced (0, 8).withWidth (std::min (a.getWidth(), 260));
    g.setColour (theme::border);
    g.fillRoundedRectangle (bar.toFloat(), 4.0f);
    if (playing)
    {
        const float centre = (float) bar.getCentreX();
        const float x = centre + juce::jlimit (-1.0f, 1.0f, bend / 2.0f) * (float) bar.getWidth() * 0.5f;
        g.setColour (theme::accentLight);
        g.fillRoundedRectangle (juce::Rectangle<float> (std::min (centre, x), (float) bar.getY(), std::abs (x - centre) + 2.0f, (float) bar.getHeight()), 4.0f);
    }
    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("bend " + juce::String (bend, 2) + " st", bar.translated (bar.getWidth() + 10, 0).withWidth (90), juce::Justification::centredLeft);

    // MIDI control input monitor: what actually reaches KumAmp.
    {
        juce::String text (juce::CharPointer_UTF8 ("Todav\xc3\xad" "a no lleg\xc3\xb3 ning\xc3\xba" "n mensaje MIDI a KumAmp"));
        bool fresh = false;
        if (midiInCount > 0)
        {
            const int status = (int) (lastMidiIn >> 16) & 0xff, d1 = (int) (lastMidiIn >> 8) & 0xff, d2 = (int) lastMidiIn & 0xff;
            const int ch = (status & 0x0f) + 1;
            juce::String what;
            switch (status & 0xf0)
            {
                case 0xc0: what = "Program Change " + juce::String (d1); break;
                case 0xb0: what = "CC " + juce::String (d1) + " = " + juce::String (d2); break;
                case 0x90: what = "Nota " + juce::String (d1) + " vel " + juce::String (d2); break;
                case 0x80: what = "Nota off " + juce::String (d1); break;
                case 0xe0: what = "Pitch bend"; break;
                default:   what = "0x" + juce::String::toHexString (status); break;
            }
            const double ago = (juce::Time::getMillisecondCounterHiRes() - lastMidiInTime) / 1000.0;
            fresh = ago < 1.0;
            text = juce::String (juce::CharPointer_UTF8 ("\xc3\x9a" "ltimo MIDI recibido: "));
            text << what << "  (canal " << ch << ")   " << juce::String ((int) midiInCount) << (midiInCount == 1 ? " mensaje" : " mensajes");
        }
        g.setColour (fresh ? theme::good : theme::textDim);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (text, midiInArea, juce::Justification::centredLeft);
    }

    // How to route it in the DAW.
    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (14.0f));
    g.drawMultiLineText (juce::CharPointer_UTF8 (
        "En REAPER: crea una pista con tu instrumento VST, abre el boton ROUTE de la pista de KumAmp y "
        "a\xc3\xb1" "ade un env\xc3\xad" "o a esa pista con Audio: None y MIDI: All (o el canal elegido). "
        "Arma la pista del instrumento con el monitoreo activo.\n"
        "Consejo: pon el instrumento en modo mono/legato y su pitch bend en el mismo rango elegido aqu\xc3\xad" "."),
        helpArea.getX(), helpArea.getY() + 14, helpArea.getWidth());
}

void SettingsView::resized()
{
    auto area = getLocalBounds().reduced (4);
    outCard = area.removeFromTop (area.getHeight() * 64 / 100);
    area.removeFromTop (14);
    controlCard = area;

    auto o = outCard.reduced (22, 16);
    o.removeFromTop (58);
    midiOut.setBounds (o.removeFromTop (30).withWidth (360));
    o.removeFromTop (10);

    auto left = o.removeFromLeft (o.getWidth() / 2);
    auto right = o;
    auto row = [&left] { auto r = left.removeFromTop (36); left.removeFromTop (8); return r; };
    channel.setBounds (row().withTrimmedLeft (160).withWidth (200));
    bendRange.setBounds (row().withTrimmedLeft (160).withWidth (200));
    threshold.setBounds (row().withTrimmedLeft (160));
    transpose.setBounds (row().withTrimmedLeft (160));

    right.removeFromLeft (24);
    activityArea = right.removeFromTop (86);
    right.removeFromTop (6);
    helpArea = right;

    auto c = controlCard.reduced (22, 16);
    c.removeFromTop (58);
    pcScenes.setBounds (c.removeFromTop (30));
    c.removeFromTop (6);
    ccMacros.setBounds (c.removeFromTop (30));
    c.removeFromTop (6);
    midiInArea = c.removeFromTop (26);
}
} // namespace kumamp
