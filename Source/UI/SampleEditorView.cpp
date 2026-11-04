#include "SampleEditorView.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace kumamp
{
namespace
{
    constexpr int kHandleReach = 9;
    constexpr double kMinView = 0.0005;
    constexpr double kMinTrim = 0.0002;

    juce::String utf8 (const char* text) { return juce::String (juce::CharPointer_UTF8 (text)); }

    juce::String timeText (double seconds)
    {
        return juce::String (seconds, seconds < 10.0 ? 3 : 2).replaceCharacter ('.', ',') + " s";
    }
} // namespace

SampleEditorView::SampleEditorView (std::shared_ptr<ParamStore> samplerStore)
    : store (std::move (samplerStore)), sampler (dynamic_cast<SamplerBlock*> (store.get()))
{
    jassert (sampler != nullptr);
    sound = block().getSound();

    loadButton.setButtonText (utf8 ("Cargar sonido\xe2\x80\xa6"));
    loadButton.setColour (juce::TextButton::buttonColourId, theme::accent.withAlpha (0.35f));
    loadButton.onClick = [this] { chooseFile(); };
    closeButton.onClick = [this] { if (onClose) onClose(); };

    listenButton.setClickingTogglesState (true);
    listenButton.setColour (juce::TextButton::buttonOnColourId, theme::accent);
    listenButton.setTooltip ("Escucha el recorte en su tono original");
    listenButton.onClick = [this] { block().setPreviewing (listenButton.getToggleState()); };

    silenceButton.setTooltip ("Lleva el recorte a donde empieza y termina el sonido");
    silenceButton.onClick = [this] { block().trimSilence(); repaint(); };
    zoomButton.onClick = [this]
    {
        const double start = store->getParam (SamplerBlock::Start) * 0.01, end = store->getParam (SamplerBlock::End) * 0.01;
        const double margin = (end - start) * 0.05;
        setView (start - margin, end + margin);
    };
    allButton.onClick = [this] { setView (0.0, 1.0); };

    const auto addChoices = [this] (juce::ComboBox& box, int param)
    {
        const auto choices = juce::StringArray::fromTokens (store->getParamInfo (param).unit, "|", {});
        for (int c = 0; c < choices.size(); ++c)
            box.addItem (choices[c], c + 1);
        box.onChange = [this, &box, param]
        {
            if (box.getSelectedId() > 0)
                store->setParam (param, (float) (box.getSelectedId() - 1));
            syncControls();
            repaint();
        };
        addAndMakeVisible (box);
    };
    addChoices (rootBox, SamplerBlock::RootNote);
    addChoices (octaveBox, SamplerBlock::RootOctave);
    rootBox.setTooltip ("La nota del sonido. Auto la detecta del recorte");
    octaveBox.setTooltip ("Octava de la nota base");

    rootLabel.setText ("Nota base", juce::dontSendNotification);
    tuneLabel.setText ("Afinar", juce::dontSendNotification);
    for (auto* label : { &rootLabel, &tuneLabel })
    {
        label->setFont (juce::FontOptions (14.0f));
        label->setColour (juce::Label::textColourId, theme::textDim);
        label->setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (*label);
    }

    tuneSlider.setRange (-100.0, 100.0, 1.0);
    tuneSlider.setDoubleClickReturnValue (true, 0.0);
    tuneSlider.setTextValueSuffix (" c");
    tuneSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 26);
    tuneSlider.setColour (juce::Slider::trackColourId, theme::accent);
    tuneSlider.onValueChange = [this] { store->setParam (SamplerBlock::Tune, (float) tuneSlider.getValue()); };
    addAndMakeVisible (tuneSlider);

    for (juce::Component* c : { (juce::Component*) &loadButton, (juce::Component*) &closeButton, (juce::Component*) &listenButton,
                                (juce::Component*) &silenceButton, (juce::Component*) &zoomButton, (juce::Component*) &allButton })
        addAndMakeVisible (c);

    setTooltip (utf8 ("Arrastr\xc3\xa1 los bordes para recortar  |  Rueda: zoom  |  Shift + rueda: moverse  |  Tambi\xc3\xa9n pod\xc3\xa9s soltar un archivo ac\xc3\xa1"));
    syncControls();
    startTimerHz (30);
}

SampleEditorView::~SampleEditorView()
{
    block().setPreviewing (false);
}

void SampleEditorView::chooseFile()
{
    auto start = juce::File (juce::String (block().getFilePath (0)));
    if (! juce::File::isAbsolutePath (start.getFullPathName()) || ! start.existsAsFile())
        start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

    chooser = std::make_unique<juce::FileChooser> ("Cargar sonido", start, juce::String (block().getFileSlotPattern (0)));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<SampleEditorView> (this)] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (safe != nullptr && file.existsAsFile())
                                  safe->block().loadFile (0, file.getFullPathName().toStdString());
                          });
}

bool SampleEditorView::isInterestedInFileDrag (const juce::StringArray& files)
{
    const auto patterns = juce::StringArray::fromTokens (block().getFileSlotPattern (0), ";", {});
    return files.size() == 1
        && std::any_of (patterns.begin(), patterns.end(), [&] (const juce::String& p) { return juce::File (files[0]).getFileName().matchesWildcard (p, true); });
}

void SampleEditorView::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    block().loadFile (0, files[0].toStdString());
    repaint();
}

void SampleEditorView::setView (double newStart, double newEnd)
{
    const double length = std::clamp (newEnd - newStart, kMinView, 1.0);
    viewStart = std::clamp (newStart, 0.0, 1.0 - length);
    viewEnd = viewStart + length;
    repaint();
}

void SampleEditorView::syncControls()
{
    if (! rootBox.isPopupActive())
        rootBox.setSelectedId ((int) std::lround (store->getParam (SamplerBlock::RootNote)) + 1, juce::dontSendNotification);
    if (! octaveBox.isPopupActive())
        octaveBox.setSelectedId ((int) std::lround (store->getParam (SamplerBlock::RootOctave)) + 1, juce::dontSendNotification);
    octaveBox.setVisible (std::lround (store->getParam (SamplerBlock::RootNote)) != 0);
    if (! tuneSlider.isMouseButtonDown())
        tuneSlider.setValue (store->getParam (SamplerBlock::Tune), juce::dontSendNotification);
}

void SampleEditorView::timerCallback()
{
    if (auto latest = block().getSound(); latest != sound)
    {
        sound = std::move (latest);
        viewStart = 0.0;
        viewEnd = 1.0;
        repaint();
    }

    const bool canListen = sound != nullptr && ! store->isBypassed();
    listenButton.setEnabled (canListen);
    listenButton.setTooltip (canListen ? juce::String ("Escucha el recorte en su tono original") : utf8 ("Encend\xc3\xa9 el bloque para escuchar"));
    if (! listenButton.isMouseButtonDown())
        listenButton.setToggleState (block().isPreviewing(), juce::dontSendNotification);
    syncControls();

    const auto root = juce::String (block().getRootText());
    const float playhead = block().getPlayheadSeconds();
    if (root != lastRoot || ! juce::exactlyEqual (playhead, lastPlayhead))
    {
        lastRoot = root;
        lastPlayhead = playhead;
        repaint();
    }
}

float SampleEditorView::xOf (double fraction) const noexcept
{
    return (float) waveArea.getX() + (float) ((fraction - viewStart) / (viewEnd - viewStart)) * (float) waveArea.getWidth();
}

double SampleEditorView::fractionAt (float x) const noexcept
{
    return viewStart + (double) ((x - (float) waveArea.getX()) / (float) waveArea.getWidth()) * (viewEnd - viewStart);
}

SampleEditorView::Drag SampleEditorView::dragAt (float x) const noexcept
{
    const float start = xOf (store->getParam (SamplerBlock::Start) * 0.01);
    const float end = xOf (store->getParam (SamplerBlock::End) * 0.01);
    const float toStart = std::abs (x - start), toEnd = std::abs (x - end);
    if (std::min (toStart, toEnd) <= (float) kHandleReach)
        return toStart <= toEnd ? Drag::Start : Drag::End;
    return x > start && x < end ? Drag::Both : Drag::None;
}

void SampleEditorView::mouseMove (const juce::MouseEvent& e)
{
    const auto hover = sound != nullptr && waveArea.contains (e.getPosition()) ? dragAt ((float) e.x) : Drag::None;
    setMouseCursor (hover == Drag::Start || hover == Drag::End ? juce::MouseCursor::LeftRightResizeCursor
                    : hover == Drag::Both                     ? juce::MouseCursor::DraggingHandCursor
                                                              : juce::MouseCursor::NormalCursor);
}

void SampleEditorView::mouseDown (const juce::MouseEvent& e)
{
    if (! waveArea.contains (e.getPosition()))
        return;
    if (sound == nullptr)
    {
        chooseFile();
        return;
    }
    drag = dragAt ((float) e.x);
    const double start = store->getParam (SamplerBlock::Start) * 0.01;
    grabOffset = fractionAt ((float) e.x) - start;
    grabLength = store->getParam (SamplerBlock::End) * 0.01 - start;
}

void SampleEditorView::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::None || sound == nullptr)
        return;

    const double at = std::clamp (fractionAt ((float) e.x), 0.0, 1.0);
    const double start = store->getParam (SamplerBlock::Start) * 0.01, end = store->getParam (SamplerBlock::End) * 0.01;
    if (drag == Drag::Start)
    {
        store->setParam (SamplerBlock::Start, (float) (std::min (at, end - kMinTrim) * 100.0));
    }
    else if (drag == Drag::End)
    {
        store->setParam (SamplerBlock::End, (float) (std::max (at, start + kMinTrim) * 100.0));
    }
    else
    {
        const double moved = std::clamp (fractionAt ((float) e.x) - grabOffset, 0.0, 1.0 - grabLength);
        store->setParam (SamplerBlock::Start, (float) (moved * 100.0));
        store->setParam (SamplerBlock::End, (float) ((moved + grabLength) * 100.0));
    }
    repaint();
}

void SampleEditorView::mouseUp (const juce::MouseEvent&)
{
    if (drag != Drag::None)
        block().detectRoot();   // the base note of what is left
    drag = Drag::None;
}

void SampleEditorView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (sound == nullptr || ! waveArea.contains (e.getPosition()))
        return;

    const double length = viewEnd - viewStart;
    if (e.mods.isShiftDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY))
    {
        const double delta = (e.mods.isShiftDown() && juce::exactlyEqual (wheel.deltaX, 0.0f) ? wheel.deltaY : wheel.deltaX) * length * 0.5;
        setView (viewStart - delta, viewEnd - delta);
        return;
    }

    const double at = fractionAt ((float) e.x);
    const double zoom = std::pow (0.5, (double) wheel.deltaY * 2.0);
    const double newLength = std::clamp (length * zoom, kMinView, 1.0);
    const double position = (at - viewStart) / length;
    setView (at - position * newLength, at - position * newLength + newLength);
}

void SampleEditorView::drawWaveform (juce::Graphics& g, juce::Rectangle<float> area, const SamplerSound& s) const
{
    const float centre = area.getCentreY(), scale = area.getHeight() * 0.5f / 0.55f;   // the peak is at 0.5
    const double samplesPerPixel = (viewEnd - viewStart) * s.length / area.getWidth();
    // A decimated level when zoomed out: it keeps the scan cheap (at most ~16 samples per column).
    const int level = std::clamp ((int) std::ceil (std::log2 (std::max (samplesPerPixel / 16.0, 1.0))), 0, SamplerSound::kNumLevels - 1);
    const auto& left = s.left[(size_t) level];
    const auto& right = s.right[(size_t) level];
    const double factor = 1.0 / (double) (1 << level);
    const int stride = std::max (1, (int) (samplesPerPixel * factor / 16.0));
    const int size = (int) left.size();

    g.setColour (theme::categoryColour (BlockType::Sampler).withAlpha (0.85f));
    const int width = (int) area.getWidth();
    for (int x = 0; x < width; ++x)
    {
        const double from = (viewStart + (viewEnd - viewStart) * x / width) * s.length * factor;
        const double to = from + samplesPerPixel * factor;
        const int first = std::clamp ((int) from, 0, size - 1);
        const int last = std::clamp ((int) std::ceil (to), first + 1, size);
        float low = 1.0f, high = -1.0f;
        for (int i = first; i < last; i += stride)
        {
            low = std::min (low, left[(size_t) i]);
            high = std::max (high, left[(size_t) i]);
            if (! right.empty())
            {
                low = std::min (low, right[(size_t) i]);
                high = std::max (high, right[(size_t) i]);
            }
        }
        const float top = centre - std::max (high, 0.0f) * scale, bottom = centre - std::min (low, 0.0f) * scale;
        g.fillRect (area.getX() + (float) x, top, 1.0f, std::max (1.0f, bottom - top));
    }
}

void SampleEditorView::paint (juce::Graphics& g)
{
    theme::drawCard (g, getLocalBounds().toFloat());

    const auto wave = waveArea.toFloat();
    g.setColour (theme::background);
    g.fillRoundedRectangle (wave, 8.0f);

    g.setColour (theme::text);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    auto header = headerArea;
    g.drawText ("Sampler", header.removeFromLeft (100), juce::Justification::centredLeft);

    if (sound == nullptr)
    {
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (18.0f));
        const auto status = juce::String (block().getStatusText());
        g.drawText (status.startsWith ("Error") || status.startsWith ("Cargando") ? status
                                                                                  : utf8 ("Solt\xc3\xa1 un archivo de audio ac\xc3\xa1 o hac\xc3\xa9 clic para elegirlo"),
                    waveArea, juce::Justification::centred);
    }
    else
    {
        const double start = store->getParam (SamplerBlock::Start) * 0.01, end = store->getParam (SamplerBlock::End) * 0.01;
        const double seconds = sound->seconds();

        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (15.0f));
        g.drawText (juce::String (juce::CharPointer_UTF8 (sound->fileName.c_str())) + "   |   "
                        + "Inicio " + timeText (start * seconds) + "   Fin " + timeText (end * seconds)
                        + utf8 ("   Duraci\xc3\xb3n ") + timeText ((end - start) * seconds)
                        + "   |   Base " + juce::String (block().getRootText()),
                    header, juce::Justification::centredLeft);

        g.saveState();
        g.reduceClipRegion (waveArea);
        drawWaveform (g, wave, *sound);

        // What the trim leaves out.
        const float startX = xOf (start), endX = xOf (end);
        g.setColour (theme::background.withAlpha (0.72f));
        if (startX > wave.getX())
            g.fillRect (wave.withRight (startX));
        if (endX < wave.getRight())
            g.fillRect (wave.withLeft (endX));

        g.setColour (theme::border);
        g.drawHorizontalLine (waveArea.getCentreY(), wave.getX(), wave.getRight());

        // Handles.
        for (const float x : { startX, endX })
        {
            g.setColour (theme::accentLight);
            g.fillRect (x - 1.0f, wave.getY(), 2.0f, wave.getHeight());
        }
        g.fillRoundedRectangle (startX - 1.0f, wave.getY(), 10.0f, 18.0f, 3.0f);
        g.fillRoundedRectangle (endX - 9.0f, wave.getBottom() - 18.0f, 10.0f, 18.0f, 3.0f);

        if (const float playhead = block().getPlayheadSeconds(); playhead >= 0.0f)
        {
            g.setColour (theme::text);
            const float x = xOf (playhead / seconds);
            g.fillRect (x - 0.5f, wave.getY(), 1.0f, wave.getHeight());
        }
        g.restoreState();
    }

    g.setColour (dragOver ? theme::accentLight : theme::border);
    g.drawRoundedRectangle (wave.reduced (0.5f), 8.0f, dragOver ? 2.0f : 1.0f);
}

void SampleEditorView::resized()
{
    auto area = getLocalBounds().reduced (12);

    auto top = area.removeFromTop (34);
    closeButton.setBounds (top.removeFromRight (90));
    top.removeFromRight (8);
    loadButton.setBounds (top.removeFromRight (150));
    top.removeFromRight (12);
    headerArea = top;
    area.removeFromTop (8);

    auto bottom = area.removeFromBottom (36);
    area.removeFromBottom (8);
    waveArea = area;

    for (auto* button : { &listenButton, &silenceButton, &zoomButton, &allButton })
    {
        button->setBounds (bottom.removeFromLeft (button == &listenButton || button == &allButton ? 88 : 130));
        bottom.removeFromLeft (6);
    }
    bottom.removeFromLeft (6);
    rootLabel.setBounds (bottom.removeFromLeft (70));
    rootBox.setBounds (bottom.removeFromLeft (84));
    bottom.removeFromLeft (4);
    octaveBox.setBounds (bottom.removeFromLeft (60));
    tuneLabel.setBounds (bottom.removeFromLeft (52));
    tuneSlider.setBounds (bottom);
}
} // namespace kumamp
