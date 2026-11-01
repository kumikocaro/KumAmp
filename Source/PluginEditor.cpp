#include "PluginEditor.h"

KumAmpAudioProcessorEditor::KumAmpAudioProcessorEditor (KumAmpAudioProcessor& p)
    : AudioProcessorEditor (&p),
      kumProcessor (p),
      engine (p.getEngine()),
      inputAttachment (p.getParameters(), "input_gain", inputGain),
      outputAttachment (p.getParameters(), "output_gain", outputGain),
      grid (p.getEngine())
{
    title.setText ("KumAmp", juce::dontSendNotification);
    title.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    addAndMakeVisible (title);

    status.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (status);

    const char* names[] = { "A", "B", "C", "D" };
    for (int i = 0; i < kumamp::kNumScenes; ++i)
    {
        auto& b = sceneButtons[(size_t) i];
        b.setButtonText (names[i]);
        b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff8e5bd6));
        b.onClick = [this, i] { kumProcessor.selectScene (i); };
        addAndMakeVisible (b);
    }

    storeButton.onClick = [this] { engine.storeScene (engine.getActiveScene()); };
    addAndMakeVisible (storeButton);

    panicButton.onClick = [this] { engine.pushCommand ({ kumamp::EngineCommand::Type::ResetAllBlocks, 0 }); };
    addAndMakeVisible (panicButton);

    for (auto* s : { &inputGain, &outputGain })
    {
        s->setTextValueSuffix (" dB");
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
        addAndMakeVisible (s);
    }
    addAndMakeVisible (inputLabel);
    addAndMakeVisible (outputLabel);

    grid.onNodeSelected = [this] (kumamp::StoreId id) { panel.setStore (engine.getStore (id)); };
    addAndMakeVisible (grid);
    addAndMakeVisible (panel);

    setResizable (true, true);
    setResizeLimits (820, 520, 2400, 1600);
    setSize (1180, 700);

    updateSceneButtons (engine.getActiveScene());
    startTimerHz (30);
}

KumAmpAudioProcessorEditor::~KumAmpAudioProcessorEditor()
{
    stopTimer();
}

void KumAmpAudioProcessorEditor::updateSceneButtons (int activeScene)
{
    for (int i = 0; i < kumamp::kNumScenes; ++i)
        sceneButtons[(size_t) i].setToggleState (i == activeScene, juce::dontSendNotification);
}

void KumAmpAudioProcessorEditor::timerCallback()
{
    kumamp::EngineEvent event;
    while (engine.popEvent (event))
        if (event.type == kumamp::EngineEvent::Type::SceneChanged)
            updateSceneButtons (event.value);

    if (engine.getModelVersion() != shownModelVersion)
    {
        // Preset loaded or grid edited: the selected store may no longer exist.
        shownModelVersion = engine.getModelVersion();
        panel.setStore (engine.getStore (panel.getStoreId()));
    }

    const double sr = kumProcessor.getSampleRate() > 0.0 ? kumProcessor.getSampleRate() : 48000.0;
    status.setText ("Escena " + juce::String::charToString ((juce::juce_wchar) ('A' + engine.getActiveScene()))
                        + "   |   PDC " + juce::String (1000.0 * engine.getLatencySamples() / sr, 2) + " ms",
                    juce::dontSendNotification);

    panel.refresh();
    grid.repaint();
}

void KumAmpAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0f1115));
}

void KumAmpAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (12);

    auto header = area.removeFromTop (40);
    title.setBounds (header.removeFromLeft (140));
    for (auto& b : sceneButtons)
        b.setBounds (header.removeFromLeft (48).reduced (3));
    header.removeFromLeft (8);
    storeButton.setBounds (header.removeFromLeft (130).reduced (3));
    panicButton.setBounds (header.removeFromLeft (100).reduced (3));
    status.setBounds (header);

    area.removeFromTop (8);
    auto gains = area.removeFromBottom (30);
    inputLabel.setBounds (gains.removeFromLeft (30));
    inputGain.setBounds (gains.removeFromLeft (260));
    gains.removeFromLeft (20);
    outputLabel.setBounds (gains.removeFromLeft (36));
    outputGain.setBounds (gains.removeFromLeft (260));

    area.removeFromBottom (8);
    panel.setBounds (area.removeFromBottom (214));
    area.removeFromBottom (8);
    grid.setBounds (area);
}
