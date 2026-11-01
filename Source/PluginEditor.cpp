#include "PluginEditor.h"

void KumAmpAudioProcessorEditor::Placeholder::paint (juce::Graphics& g)
{
    g.fillAll (kumamp::theme::background);
    kumamp::theme::drawCard (g, getLocalBounds().toFloat());
    g.setColour (kumamp::theme::text);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText (title, getLocalBounds().withTrimmedBottom (40), juce::Justification::centred);
    g.setColour (kumamp::theme::textDim);
    g.setFont (juce::FontOptions (16.0f));
    g.drawText (juce::CharPointer_UTF8 ("Pr\xc3\xb3ximamente. Por ahora todo se edita desde GRID."),
                getLocalBounds().withTrimmedTop (40), juce::Justification::centred);
}

KumAmpAudioProcessorEditor::KumAmpAudioProcessorEditor (KumAmpAudioProcessor& p)
    : AudioProcessorEditor (&p),
      kumProcessor (p),
      engine (p.getEngine()),
      sidebar (p.getParameters()),
      grid (p.getEngine()),
      settings (p.getParameters())
{
    setLookAndFeel (&lookAndFeel);

    header.onTabChanged = [this] (int tab) { showTab (tab); };
    addAndMakeVisible (header);

    sidebar.onTunerClicked = [this] { showTuner (! tunerView.isVisible()); };
    addAndMakeVisible (sidebar);

    sceneBar.onSceneSelected = [this] (int scene) { kumProcessor.selectScene (scene); };
    sceneBar.onNameEdited    = [this] (int scene, const juce::String& name) { engine.setSceneName (scene, name); };
    sceneBar.onStore         = [this]
    {
        engine.storeScene (engine.getActiveScene());
        showToast ("Escena " + juce::String::charToString ((juce::juce_wchar) ('A' + engine.getActiveScene())) + " guardada");
    };
    sceneBar.onUndo = [this] { engine.undo(); };
    sceneBar.onRedo = [this] { engine.redo(); };
    sceneBar.onMenu = [this] (juce::Component& source) { showMainMenu (source); };
    addAndMakeVisible (sceneBar);

    grid.onNodeSelected = [this] (kumamp::StoreId id) { panel.setStore (engine.getStore (id)); };
    grid.onError = [this] (const juce::String& message) { showToast ("Error: " + message); };
    addAndMakeVisible (grid);

    palette.onGroupClicked = [this] (int group, juce::Component& source)
    {
        const auto& types = kumamp::theme::paletteGroups()[(size_t) group].types;
        if (types.size() == 1)
            grid.insert (types.front(), grid.defaultInsertTarget(), false);
        else
            grid.showBlockMenu (group, grid.defaultInsertTarget(), source.getScreenBounds(), false);
    };
    palette.onAddClicked = [this] (juce::Component& source)
    {
        grid.showBlockMenu (-1, grid.defaultInsertTarget(), source.getScreenBounds(), true);
    };
    addAndMakeVisible (palette);

    panel.onRemove = [this] { grid.removeSelected(); };
    panel.onExpandEq = [this] (std::shared_ptr<kumamp::ParamStore> eqStore) { showEqOverlay (std::move (eqStore)); };
    addAndMakeVisible (panel);

    tunerView.readingSource      = [this] { return kumProcessor.getTuner().getLatest(); };
    tunerView.onModeChanged      = [this] (int mode) { setParameter ("tuner_mode", (float) mode); };
    tunerView.onMuteChanged      = [this] (bool mute) { setParameter ("tuner_mute", mute ? 1.0f : 0.0f); };
    tunerView.onReferenceChanged = [this] (float hz) { setParameter ("tuner_reference", hz); };
    tunerView.onClose            = [this] { showTuner (false); };
    addChildComponent (tunerView);
    addChildComponent (placeholder);
    settings.noteSource     = [this] { return kumProcessor.getMidiOutNote(); };
    settings.velocitySource = [this] { return kumProcessor.getMidiOutVelocity(); };
    settings.bendSource     = [this] { return kumProcessor.getMidiOutBend(); };
    addChildComponent (settings);

    toast.setJustificationType (juce::Justification::centred);
    toast.setColour (juce::Label::backgroundColourId, kumamp::theme::panelRaised);
    toast.setColour (juce::Label::outlineColourId, kumamp::theme::border);
    toast.setFont (juce::FontOptions (15.0f));
    addChildComponent (toast);

    // The compact tuner in the sidebar is always live while the editor is open.
    kumProcessor.getTuner().setListening (true);

    setResizable (true, true);
    setResizeLimits (1240, 760, 2600, 1600);
    setSize (1500, 880);
    startTimerHz (30);
}

KumAmpAudioProcessorEditor::~KumAmpAudioProcessorEditor()
{
    stopTimer();
    kumProcessor.getTuner().setListening (false);
    kumProcessor.setTunerViewOpen (false);   // never leave the output muted
    setLookAndFeel (nullptr);
}

void KumAmpAudioProcessorEditor::showTuner (bool shouldShow)
{
    if (shouldShow)
        showTab (0);
    tunerView.setVisible (shouldShow);
    kumProcessor.setTunerViewOpen (shouldShow);
    if (shouldShow)
        tunerView.toFront (false);
}

void KumAmpAudioProcessorEditor::showEqOverlay (std::shared_ptr<kumamp::ParamStore> eqStore)
{
    eqOverlay = std::make_unique<kumamp::EqView> (false);
    eqOverlay->setBlock (std::move (eqStore));
    eqOverlay->onClose = [this]
    {
        // Deferred: don't delete the view from inside its own button callback.
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<KumAmpAudioProcessorEditor> (this)]
                                         { if (safe != nullptr) safe->eqOverlay.reset(); });
    };
    eqOverlay->setBounds (centreColumn);
    addAndMakeVisible (*eqOverlay);
    eqOverlay->toFront (false);
}

void KumAmpAudioProcessorEditor::showTab (int tab)
{
    header.setActiveTab (tab);
    const char* titles[] = { "", "PRESETS", "SNAPSHOTS", "IRs", "SETTINGS" };
    placeholder.title = titles[juce::jlimit (0, 4, tab)];
    placeholder.setVisible (tab != 0 && tab != 4);
    settings.setVisible (tab == 4);
    if (tab == 4)
        settings.toFront (false);
    if (tab != 0)
    {
        tunerView.setVisible (false);
        kumProcessor.setTunerViewOpen (false);
        if (tab != 4)
            placeholder.toFront (false);
        placeholder.repaint();
    }
}

void KumAmpAudioProcessorEditor::showToast (const juce::String& message)
{
    toast.setText (message, juce::dontSendNotification);
    toast.setVisible (true);
    toast.toFront (false);
    toastUntil = juce::Time::getMillisecondCounter() + 3000;
}

void KumAmpAudioProcessorEditor::showMainMenu (juce::Component& source)
{
    juce::PopupMenu menu;
    menu.addItem (1, "Afinador grande", true, tunerView.isVisible());
    menu.addItem (2, "Deshacer", engine.canUndo());
    menu.addItem (3, "Rehacer", engine.canRedo());
    menu.addSeparator();
    menu.addItem (4, "Reset DSP (limpiar colas y estados)");
    menu.addItem (5, "Cargar preset por defecto");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&source),
                        [safe = juce::Component::SafePointer<KumAmpAudioProcessorEditor> (this)] (int result)
                        {
                            if (safe == nullptr) return;
                            switch (result)
                            {
                                case 1: safe->showTuner (! safe->tunerView.isVisible()); break;
                                case 2: safe->engine.undo(); break;
                                case 3: safe->engine.redo(); break;
                                case 4: safe->engine.pushCommand ({ kumamp::EngineCommand::Type::ResetAllBlocks, 0 }); break;
                                case 5: safe->engine.loadDefaultPreset(); safe->grid.selectNode (kumamp::kInvalidNode); break;
                                default: break;
                            }
                        });
}

void KumAmpAudioProcessorEditor::setParameter (const juce::String& id, float value)
{
    if (auto* param = kumProcessor.getParameters().getParameter (id))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 (value));
        param->endChangeGesture();
    }
}

int KumAmpAudioProcessorEditor::groupOfSelectedBlock() const
{
    const auto store = engine.getStore (panel.getStoreId());
    if (store == nullptr)
        return -1;
    const auto& groups = kumamp::theme::paletteGroups();
    for (int g = 0; g < (int) groups.size(); ++g)
        for (auto type : groups[(size_t) g].types)
            if (type == store->getType())
                return g;
    return -1;
}

void KumAmpAudioProcessorEditor::timerCallback()
{
    kumamp::EngineEvent event;
    while (engine.popEvent (event)) {}   // drained; state is read directly below

    header.setStats (kumProcessor.getCpuLoad(), kumProcessor.getSampleRate(), kumProcessor.getCurrentBlockSize(),
                     kumProcessor.isAudioRunning(), engine.getTempo());

    sidebar.setTunerReading (kumProcessor.getTuner().getLatest());
    sidebar.pushInputLevel (kumProcessor.takeInputPeak (0), kumProcessor.takeInputPeak (1));
    panel.getOutputMeter().push (kumProcessor.takeOutputPeak (0), kumProcessor.takeOutputPeak (1));

    const int scene = engine.getActiveScene();
    sceneBar.setActiveScene (scene);
    sceneBar.setSceneName (engine.getSceneName (scene));
    sceneBar.setUndoState (engine.canUndo(), engine.canRedo());

    if (engine.getModelVersion() != shownModelVersion)
    {
        // Preset loaded, grid edited or undo: the selected store may no longer exist.
        shownModelVersion = engine.getModelVersion();
        const auto store = engine.getStore (panel.getStoreId());
        if (store == nullptr)
            grid.selectNode (kumamp::kInvalidNode);
        else
            panel.setStore (store);
    }

    // The large EQ closes if its block left the grid or another block got selected.
    if (eqOverlay != nullptr)
        if (const auto selected = engine.getStore (panel.getStoreId()); selected == nullptr || selected->getType() != kumamp::BlockType::ParametricEq)
            eqOverlay.reset();

    panel.refresh();
    grid.repaint();
    palette.setHighlightedGroup (groupOfSelectedBlock());

    auto& params = kumProcessor.getParameters();
    tunerView.setSettings ((int) params.getRawParameterValue ("tuner_mode")->load(),
                           params.getRawParameterValue ("tuner_mute")->load() > 0.5f,
                           params.getRawParameterValue ("tuner_reference")->load());

    if (toast.isVisible() && juce::Time::getMillisecondCounter() > toastUntil)
        toast.setVisible (false);
}

void KumAmpAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (kumamp::theme::background);
    kumamp::theme::drawCard (g, centreCard.toFloat());
}

void KumAmpAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    header.setBounds (area.removeFromTop (84));
    area.reduce (16, 16);

    panel.setBounds (area.removeFromBottom (232));
    area.removeFromBottom (16);

    sidebar.setBounds (area.removeFromLeft (310));
    area.removeFromLeft (16);

    const auto centre = area;
    palette.setBounds (area.removeFromBottom (96));
    area.removeFromBottom (14);
    centreCard = area;
    sceneBar.setBounds (area.removeFromTop (62));
    grid.setBounds (area.reduced (2));

    centreColumn = centre;
    tunerView.setBounds (centre);
    if (eqOverlay != nullptr)
        eqOverlay->setBounds (centre);
    placeholder.setBounds (centre);
    settings.setBounds (centre);
    toast.setBounds (centreCard.withSizeKeepingCentre (420, 40).withY (centreCard.getBottom() - 56));
}
