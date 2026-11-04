#include "PluginEditor.h"
#include "UI/UserSettings.h"

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
      presetBar (p.getPresets()),
      grid (p.getEngine()),
      settings (p.getParameters()),
      presetBrowser (p.getPresets())
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (content);
    setWantsKeyboardFocus (true);

    header.onTabChanged = [this] (int tab) { showTab (tab); };
    content.addAndMakeVisible (header);

    sidebar.onTunerClicked = [this] { showTuner (! tunerView.isVisible()); };
    content.addAndMakeVisible (sidebar);

    sceneBar.onSceneSelected = [this] (int scene) { kumProcessor.selectScene (scene); };
    sceneBar.onNameEdited    = [this] (int scene, const juce::String& name) { engine.setSceneName (scene, name); };
    sceneBar.onStore         = [this]
    {
        engine.storeScene (engine.getActiveScene());
        showToast ("Escena " + juce::String::charToString ((juce::juce_wchar) ('A' + engine.getActiveScene())) + " guardada");
    };
    content.addAndMakeVisible (sceneBar);

    presetBar.onLoad    = [this] (const juce::File& file) { loadPreset (file); };
    presetBar.onSave    = [this] { saveCurrentPreset(); };
    presetBar.onShowAll = [this] { showTab (1); };
    presetBar.onUndo    = [this] { engine.undo(); };
    presetBar.onRedo    = [this] { engine.redo(); };
    presetBar.onMenu    = [this] (juce::Component& source) { showMainMenu (source); };
    content.addAndMakeVisible (presetBar);

    grid.onNodeSelected = [this] (kumamp::StoreId id) { panel.setStore (engine.getStore (id)); };
    grid.onError = [this] (const juce::String& message) { showToast ("Error: " + message); };
    content.addAndMakeVisible (grid);

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
    content.addAndMakeVisible (palette);

    panel.onRemove = [this] { grid.removeSelected(); };
    panel.onChangeType = [this] (kumamp::BlockType type) { grid.replaceSelected (type); };
    panel.onExpandEq = [this] (std::shared_ptr<kumamp::ParamStore> eqStore) { showEqOverlay (std::move (eqStore)); };
    panel.onBrowseTone3000 = [this] (std::shared_ptr<kumamp::ParamStore> ampStore, juce::Component& source)
    {
        browseTone3000 (std::move (ampStore), source);
    };
    panel.onTone3000Variant = [this] (std::shared_ptr<kumamp::ParamStore> ampStore, const kumamp::t3k::Tone& tone,
                                      const kumamp::t3k::Model& variant)
    {
        downloadTone3000Variant (std::move (ampStore), tone, variant);
    };
    content.addAndMakeVisible (panel);

    tunerView.readingSource      = [this] { return kumProcessor.getTuner().getLatest(); };
    tunerView.onModeChanged      = [this] (int mode) { setParameter ("tuner_mode", (float) mode); };
    tunerView.onMuteChanged      = [this] (bool mute) { setParameter ("tuner_mute", mute ? 1.0f : 0.0f); };
    tunerView.onReferenceChanged = [this] (float hz) { setParameter ("tuner_reference", hz); };
    tunerView.onClose            = [this] { showTuner (false); };
    content.addChildComponent (tunerView);
    content.addChildComponent (placeholder);
    settings.noteSource     = [this] { return kumProcessor.getMidiOutNote(); };
    settings.midiInSource   = [this] { return kumProcessor.getLastMidiIn(); };
    settings.midiInCountSource = [this] { return kumProcessor.getMidiInCount(); };
    settings.velocitySource = [this] { return kumProcessor.getMidiOutVelocity(); };
    settings.bendSource     = [this] { return kumProcessor.getMidiOutBend(); };
    content.addChildComponent (settings);
    presetBrowser.onMessage = [this] (const juce::String& message) { showToast (message); };
    presetBrowser.onLoaded  = [this] (const juce::String& name)
    {
        kumProcessor.selectScene (0);
        grid.selectNode (kumamp::kInvalidNode);
        showToast ("Preset \"" + name + "\" cargado");
    };
    content.addChildComponent (presetBrowser);
    tone3000Picker.onChosen = [this] (const kumamp::t3k::Tone& tone, const kumamp::t3k::Model& model) { downloadTone3000Model (tone, model); };
    tone3000Picker.onClosed = [] { kumamp::t3k::log ("picker: canceled"); };
    content.addChildComponent (tone3000Picker);

    toast.setJustificationType (juce::Justification::centred);
    toast.setColour (juce::Label::backgroundColourId, kumamp::theme::panelRaised);
    toast.setColour (juce::Label::outlineColourId, kumamp::theme::border);
    toast.setFont (juce::FontOptions (15.0f));
    content.addChildComponent (toast);

    // The compact tuner in the sidebar is always live while the editor is open.
    kumProcessor.getTuner().setListening (true);

    setResizable (true, true);
    uiScale = fittingScale (loadSavedScale());
    applyScaleLimits();
    setSize (juce::roundToInt (kDefaultWidth * uiScale), juce::roundToInt (kDefaultHeight * uiScale));
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
    content.addAndMakeVisible (*eqOverlay);
    eqOverlay->toFront (false);
}

void KumAmpAudioProcessorEditor::showTab (int tab)
{
    header.setActiveTab (tab);
    const char* titles[] = { "", "PRESETS", "SNAPSHOTS", "IRs", "SETTINGS" };
    placeholder.title = titles[juce::jlimit (0, 4, tab)];
    placeholder.setVisible (tab != 0 && tab != 1 && tab != 4);
    settings.setVisible (tab == 4);
    presetBrowser.setVisible (tab == 1);
    if (tab == 4)
        settings.toFront (false);
    if (tab == 1)
        presetBrowser.toFront (false);
    if (tab != 0)
    {
        tunerView.setVisible (false);
        kumProcessor.setTunerViewOpen (false);
        if (tab != 1 && tab != 4)
            placeholder.toFront (false);
        placeholder.repaint();
    }
}

void KumAmpAudioProcessorEditor::showToast (const juce::String& message)
{
    toast.setText (message, juce::dontSendNotification);
    toast.setVisible (true);
    toast.toFront (false);
    toastUntil = juce::Time::getMillisecondCounter() + (message.startsWith ("Error") ? 7000u : 3500u);
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
    menu.addSeparator();
    juce::PopupMenu sizes;
    for (int percent : { 60, 70, 80, 90, 100, 110, 125, 150 })
        sizes.addItem (100 + percent, juce::String (percent) + " %", true, juce::roundToInt (uiScale * 100.0f) == percent);
    sizes.addSeparator();
    sizes.addItem (99, "Ajustar a la pantalla");
    menu.addSubMenu (juce::CharPointer_UTF8 ("Tama\xc3\xb1o de la interfaz"), sizes);
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
                                case 99: safe->setUiScale (fittingScale (1.5f)); break;
                                default:
                                    if (result > 100)
                                        safe->setUiScale ((float) (result - 100) / 100.0f);
                                    break;
                            }
                        });
}

void KumAmpAudioProcessorEditor::browseTone3000 (std::shared_ptr<kumamp::ParamStore> ampStore, juce::Component&)
{
    tone3000Target = ampStore;
    tone3000Picker.setVisible (false);
    showToast (juce::String (juce::CharPointer_UTF8 ("Eleg\xc3\xad un tono en el navegador (TONE3000)\xe2\x80\xa6")));

    juce::Component::SafePointer<KumAmpAudioProcessorEditor> safe (this);
    kumProcessor.getTone3000().selectTone ([safe] (juce::Result result, const kumamp::t3k::Tone& tone,
                                                   const juce::Array<kumamp::t3k::Model>& models)
    {
        if (safe == nullptr)
        {
            kumamp::t3k::log ("editor closed before the tone arrived");
            return;
        }
        // The browser is in front: bring the host (and this window) back.
        juce::Process::makeForegroundProcess();
        if (auto* window = safe->getTopLevelComponent())
            window->toFront (true);

        if (result.failed())
        {
            if (result.getErrorMessage() != "cancelado")
                safe->showToast ("Error: " + result.getErrorMessage());
            return;
        }
        safe->tone3000Variants = models;
        if (models.size() == 1)
        {
            safe->downloadTone3000Model (tone, models.getFirst());
            return;
        }
        // Several captures in one tone: an in-window chooser (a PopupMenu of a
        // background window would close itself immediately).
        kumamp::t3k::log ("picker: " + juce::String (models.size()) + " models shown");
        safe->tone3000Picker.setBounds (safe->centreColumn);
        safe->tone3000Picker.show (tone, models);
    });
}

void KumAmpAudioProcessorEditor::downloadTone3000Model (const kumamp::t3k::Tone& tone, const kumamp::t3k::Model& model)
{
    kumamp::t3k::log ("picker: chose \"" + model.name + "\"");
    showToast ("Descargando \"" + model.name + "\"" + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6")));
    juce::Component::SafePointer<KumAmpAudioProcessorEditor> safe (this);
    auto target = tone3000Target;
    kumProcessor.getTone3000().downloadModel (tone, tone3000Variants, model, [safe, target, tone] (juce::Result result, const juce::File& file)
    {
        if (safe == nullptr)
            return;
        if (result.failed())
        {
            safe->showToast ("Error: " + result.getErrorMessage());
            return;
        }
        auto locked = target.lock();
        auto* block = dynamic_cast<kumamp::DSPBlock*> (locked.get());
        const bool loaded = block != nullptr && block->loadFile (0, file.getFullPathName().toStdString());
        kumamp::t3k::log ("load into amp: " + juce::String (loaded ? "ok " : "FAILED ") + file.getFileName());
        if (! loaded)
        {
            safe->showToast ("Descargado en " + file.getParentDirectory().getFileName() + ", pero el bloque Amp ya no existe");
            return;
        }
        juce::String credit = tone.title;
        if (tone.creator.isNotEmpty()) credit << " de " << tone.creator;
        if (tone.license.isNotEmpty()) credit << " (" << tone.license.toUpperCase() << ")";
        safe->showToast ("Cargado: " + credit);
    });
}

void KumAmpAudioProcessorEditor::downloadTone3000Variant (std::shared_ptr<kumamp::ParamStore> ampStore, const kumamp::t3k::Tone& tone,
                                                          const kumamp::t3k::Model& variant)
{
    tone3000Target = ampStore;
    kumamp::t3k::log ("variant: \"" + variant.name + "\" of tone " + juce::String (tone.id));
    showToast ("Buscando la variante en TONE3000" + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6")));

    // Fresh model URLs (they need the session token); the browser opens only if the session expired.
    juce::Component::SafePointer<KumAmpAudioProcessorEditor> safe (this);
    kumProcessor.getTone3000().openTone (tone.id, [safe, variant] (juce::Result result, const kumamp::t3k::Tone& fresh,
                                                                   const juce::Array<kumamp::t3k::Model>& models)
    {
        if (safe == nullptr)
            return;
        juce::Process::makeForegroundProcess();   // in case the browser had to confirm the session
        if (result.failed())
        {
            if (result.getErrorMessage() != "cancelado")
                safe->showToast ("Error: " + result.getErrorMessage());
            return;
        }
        safe->tone3000Variants = models;
        for (const auto& m : models)
            if (m.id == variant.id || (variant.id == 0 && m.name == variant.name))
            {
                safe->downloadTone3000Model (fresh, m);
                return;
            }
        safe->showToast ("Esa variante ya no existe en TONE3000");
    });
}

void KumAmpAudioProcessorEditor::loadPreset (const juce::File& file)
{
    const auto result = kumProcessor.getPresets().load (file);
    if (result.failed())
    {
        showToast ("Error: " + result.getErrorMessage());
        return;
    }
    kumProcessor.selectScene (0);
    grid.selectNode (kumamp::kInvalidNode);
    if (presetBrowser.isVisible())
        presetBrowser.refresh();
    showToast ("Preset \"" + kumProcessor.getPresets().getCurrentName() + "\" cargado");
}

void KumAmpAudioProcessorEditor::saveCurrentPreset()
{
    auto& presets = kumProcessor.getPresets();
    const auto file = presets.getCurrentFile();
    if (! file.existsAsFile())
    {
        showTab (1);   // no preset yet: the save form
        showToast (juce::String (juce::CharPointer_UTF8 ("Escrib\xc3\xad un nombre y toc\xc3\xa1 Guardar preset")));
        return;
    }
    const auto info = kumamp::PresetManager::readInfo (file);
    const auto folder = presets.getFolder();
    const auto parent = file.getParentDirectory();
    const auto category = parent == folder ? juce::String() : parent.getRelativePathFrom (folder);
    auto options = juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon, "Guardar preset",
                                                                 juce::String (juce::CharPointer_UTF8 ("\xc2\xbf")) + "Guardar los cambios en \"" + info.name + "\"?",
                                                                 "Guardar", "Cancelar", this);
    juce::AlertWindow::showAsync (options, [safe = juce::Component::SafePointer<KumAmpAudioProcessorEditor> (this), info, category] (int r)
    {
        if (safe == nullptr || r != 1)
            return;
        const auto result = safe->kumProcessor.getPresets().save (info.name, info.author, category, info.comment);
        safe->showToast (result.wasOk() ? "Preset \"" + info.name + "\" guardado" : "Error: " + result.getErrorMessage());
        if (safe->presetBrowser.isVisible())
            safe->presetBrowser.refresh();
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
    presetBar.setUndoState (engine.canUndo(), engine.canRedo());
    presetBar.refresh();

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
}

void KumAmpAudioProcessorEditor::paintContent (juce::Graphics& g)
{
    g.fillAll (kumamp::theme::background);
    kumamp::theme::drawCard (g, centreCard.toFloat());
}

void KumAmpAudioProcessorEditor::resized()
{
    // Logical size = window size / scale; the transform draws it back at window size.
    content.setTransform ({});
    content.setBounds (0, 0, juce::roundToInt ((float) getWidth() / uiScale), juce::roundToInt ((float) getHeight() / uiScale));
    content.setTransform (juce::AffineTransform::scale (uiScale));
}

//==============================================================================
namespace
{
    constexpr float kMinScale = 0.6f, kMaxScale = 1.5f;
    using kumamp::openUserSettings;
} // namespace

float KumAmpAudioProcessorEditor::loadSavedScale()
{
    return juce::jlimit (kMinScale, kMaxScale, (float) openUserSettings()->getDoubleValue ("uiScale", 1.0));
}

void KumAmpAudioProcessorEditor::saveScale (float scale)
{
    auto settings = openUserSettings();
    settings->setValue ("uiScale", (double) scale);
    settings->saveIfNeeded();
}

float KumAmpAudioProcessorEditor::fittingScale (float wanted)
{
    // Leave room for the host's window frame and the OS taskbar / menu bar.
    const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    if (display == nullptr)
        return juce::jlimit (kMinScale, kMaxScale, wanted);
    const auto area = display->userArea;
    const float fit = std::min (((float) area.getWidth() - 40.0f) / (float) kDefaultWidth,
                                ((float) area.getHeight() - 110.0f) / (float) kDefaultHeight);
    const float steps = std::floor (std::min (wanted, fit) * 20.0f) / 20.0f;   // 5 % steps
    return juce::jlimit (kMinScale, kMaxScale, steps);
}

void KumAmpAudioProcessorEditor::applyScaleLimits()
{
    setResizeLimits (juce::roundToInt (kMinWidth * uiScale), juce::roundToInt (kMinHeight * uiScale),
                     juce::roundToInt (kMaxWidth * uiScale), juce::roundToInt (kMaxHeight * uiScale));
}

void KumAmpAudioProcessorEditor::setUiScale (float newScale, bool remember)
{
    newScale = juce::jlimit (kMinScale, kMaxScale, newScale);
    // Keep the current logical size (the layout the user had), just drawn bigger or smaller.
    const auto logical = content.getBounds();
    uiScale = newScale;
    applyScaleLimits();
    setSize (juce::roundToInt ((float) logical.getWidth() * uiScale), juce::roundToInt ((float) logical.getHeight() * uiScale));
    resized();
    if (! remember)
        return;
    saveScale (uiScale);
    showToast ("Interfaz al " + juce::String (juce::roundToInt (uiScale * 100.0f)) + " %");
}

bool KumAmpAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    // Ctrl (Cmd on Mac) + / - / 0, like a browser.
    if (! key.getModifiers().isCommandDown())
        return false;
    const auto c = key.getTextCharacter();
    if (c == '+' || c == '=') { setUiScale (std::round (uiScale * 10.0f + 1.0f) / 10.0f); return true; }
    if (c == '-')             { setUiScale (std::round (uiScale * 10.0f - 1.0f) / 10.0f); return true; }
    if (c == '0')             { setUiScale (1.0f); return true; }
    return false;
}

void KumAmpAudioProcessorEditor::layoutContent()
{
    auto area = content.getLocalBounds();
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
    presetBar.setBounds (area.removeFromTop (62));
    sceneBar.setBounds (area.removeFromBottom (58));
    grid.setBounds (area.reduced (2));

    centreColumn = centre;
    tunerView.setBounds (centre);
    if (eqOverlay != nullptr)
        eqOverlay->setBounds (centre);
    placeholder.setBounds (centre);
    settings.setBounds (centre);
    presetBrowser.setBounds (centre);
    tone3000Picker.setBounds (centre);
    toast.setBounds (centreCard.withSizeKeepingCentre (420, 40).withY (centreCard.getBottom() - 56));
}
