// Instantiates the real processor + editor, feeds audio (meters, tuner, NAM
// model), selects the amp block and writes the window to PNG.

#include "DSP/Pitch/PitchShifterBlock.h"
#include "DSP/Pitch/CharangoBlock.h"
#include "DSP/Pitch/SamplerBlock.h"
#include "UI/SampleEditorView.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include "DSP/Pitch/GuitarSynthBlock.h"
#include "DSP/Pitch/SitarBlock.h"
#include "PluginEditor.h"
#include "DSP/Time/DelayBlocks.h"
#include "DSP/Cab/FactoryCabs.h"

#include <cstdio>
#include <cstdlib>

// setenv / unsetenv do not exist on Windows.
static void setTestEnv (const char* name, const juce::String& value)
{
   #if JUCE_WINDOWS
    _putenv_s (name, value.toRawUTF8());
   #else
    if (value.isEmpty()) unsetenv (name);
    else                 setenv (name, value.toRawUTF8(), 1);
   #endif
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    KumAmpAudioProcessor processor;
    processor.setRateAndBufferSizeDetails (48000.0, 64);
    processor.prepareToPlay (48000.0, 64);

    // Load an example NAM model into the default preset's amp block.
    auto& engine = processor.getEngine();
    kumamp::NodeId ampNode = kumamp::kInvalidNode;
    for (const auto& node : engine.getModel().getNodes())
        if (auto store = engine.getStore (node.store); store != nullptr && store->getType() == kumamp::BlockType::NamAmp)
        {
            ampNode = node.id;
            if (auto* block = dynamic_cast<kumamp::DSPBlock*> (store.get()))
                block->loadFile (0, juce::File (KUMAMP_NAM_EXAMPLES).getChildFile ("wavenet.nam").getFullPathName().toStdString());
        }

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    auto* kumEditor = dynamic_cast<KumAmpAudioProcessorEditor*> (editor.get());
    if (kumEditor != nullptr)
        kumEditor->setUiScale (1.0f, false);   // deterministic, and don't touch the user's saved size
    editor->setSize (1500, 880);

    // All sections live in the editor's scaled content component.
    auto sections = [&editor]
    {
        juce::Component* content = editor.get();
        for (auto* child : editor->getChildren())
            if (child->getNumChildComponents() > content->getNumChildComponents())
                content = child;
        return content->getChildren();
    };

    // Run ~1.5 s of a low E (slightly flat) so meters, tuner and the model are live.
    juce::AudioBuffer<float> buffer (2, 64);
    juce::MidiBuffer midi;
    double phase = 0.0;
    const double f = 82.4069 * std::exp2 (-3.0 / 1200.0);
    for (int b = 0; b < 1500; ++b)
    {
        for (int i = 0; i < 64; ++i)
        {
            const float v = (float) (0.05 * std::sin (phase) + 0.15 * std::sin (2 * phase + 0.4) + 0.08 * std::sin (3 * phase));
            buffer.setSample (0, i, v);
            buffer.setSample (1, i, v);
            phase += 2.0 * juce::MathConstants<double>::pi * f / 48000.0;
        }
        processor.processBlock (buffer, midi);
        if (b % 25 == 0)
        {
            engine.runMessageThreadTasks();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        }
    }

    // Select the amp so the block panel shows the NAM controls.
    for (auto* child : sections())
        if (auto* gridView = dynamic_cast<kumamp::GridView*> (child))
            gridView->selectNode (ampNode);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (200);

    const juce::File dir (KUMAMP_SNAPSHOT_DIR);
    dir.createDirectory();
    const auto file = dir.getChildFile ("editor.png");
    file.deleteFile();
    juce::FileOutputStream out (file);
    const bool ok = juce::PNGImageFormat().writeImageToStream (editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f), out);
    std::printf ("%s %s\n", ok ? "wrote" : "FAILED", file.getFullPathName().toRawUTF8());
    // Graphic EQ: shape a curve, select the EQ, snapshot compact + large views.
    kumamp::NodeId eqNode = kumamp::kInvalidNode;
    for (const auto& node : engine.getModel().getNodes())
        if (auto store = engine.getStore (node.store); store != nullptr && store->getType() == kumamp::BlockType::ParametricEq)
        {
            using EQ = kumamp::ParametricEqBlock;
            eqNode = node.id;
            store->setParam (EQ::bandParam (2, EQ::BandGain), 4.5f);
            store->setParam (EQ::bandParam (2, EQ::BandQ), 1.4f);
            store->setParam (EQ::bandParam (4, EQ::BandOn), 1.0f);
            store->setParam (EQ::bandParam (4, EQ::BandFreq), 3200.0f);
            store->setParam (EQ::bandParam (4, EQ::BandGain), -6.0f);
            store->setParam (EQ::bandParam (4, EQ::BandQ), 2.5f);
            store->setParam (EQ::bandParam (6, EQ::BandOn), 1.0f);
            store->setParam (EQ::bandParam (6, EQ::BandGain), 3.0f);
            store->setParam (EQ::bandParam (7, EQ::BandOn), 1.0f);
            store->setParam (EQ::bandParam (7, EQ::BandSlope), 2.0f);
        }
    for (auto* child : sections())
        if (auto* gridView = dynamic_cast<kumamp::GridView*> (child))
            gridView->selectNode (eqNode);

    auto pump = [&] (int blocks)
    {
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < 64; ++i)
            {
                // Pink-ish test signal: chord-like partials + noise, so the analyser has content.
                const float noise = (float) (juce::Random::getSystemRandom().nextFloat() - 0.5f) * 0.05f;
                const float v = (float) (0.05 * std::sin (phase) + 0.15 * std::sin (2 * phase + 0.4) + 0.08 * std::sin (3 * phase)
                                       + 0.05 * std::sin (5.04 * phase) + 0.03 * std::sin (8.0 * phase)) + noise;
                buffer.setSample (0, i, v);
                buffer.setSample (1, i, v);
                phase += 2.0 * juce::MathConstants<double>::pi * f / 48000.0;
            }
            processor.processBlock (buffer, midi);
            if (b % 25 == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil (15);
        }
    };
    pump (1500);

    auto save = [&] (const char* name)
    {
        const auto out = dir.getChildFile (name);
        out.deleteFile();
        juce::FileOutputStream stream (out);
        const bool written = juce::PNGImageFormat().writeImageToStream (editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f), stream);
        std::printf ("%s %s\n", written ? "wrote" : "FAILED", out.getFullPathName().toRawUTF8());
        return written;
    };
    bool allOk = ok && save ("editor_eq.png");

    for (auto* child : sections())
        if (auto* panelView = dynamic_cast<kumamp::BlockPanel*> (child))
            if (panelView->onExpandEq)
                panelView->onExpandEq (engine.getStore (engine.getModel().findNode (eqNode)->store));
    pump (300);
    // Click band 3's node in the large EQ so the edit strip shows.
    for (auto* child : sections())
        if (auto* eqView = dynamic_cast<kumamp::EqView*> (child))
            if (eqView->getWidth() > 900)
            {
                const auto area = eqView->getLocalBounds().toFloat().withTrimmedBottom (96.0f).reduced (1.0f);
                const float x = area.getX() + area.getWidth() * std::log (800.0f / 10.0f) / std::log (3000.0f);
                const auto plot = area.reduced (0.0f, 14.0f);
                const float y = plot.getCentreY() - 4.5f / 24.0f * plot.getHeight() * 0.5f;
                const juce::MouseEvent click (juce::Desktop::getInstance().getMainMouseSource(), { x, y }, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                              eqView, eqView, juce::Time::getCurrentTime(), { x, y }, juce::Time::getCurrentTime(), 1, false);
                eqView->mouseDown (click);
                eqView->mouseUp (click);
            }
    pump (300);
    allOk = save ("editor_eq_large.png") && allOk;

    // Band listen on the clicked band (3): shaded region + badge, then off again.
    if (auto* eqBlock = dynamic_cast<kumamp::ParametricEqBlock*> (engine.getStore (engine.getModel().findNode (eqNode)->store).get()))
    {
        eqBlock->setListenBand (2);
        pump (300);
        allOk = save ("editor_eq_listen.png") && allOk;
        eqBlock->setListenBand (-1);
    }

    // Delay with tempo sync on.
    for (const auto& node : engine.getModel().getNodes())
        if (auto store = engine.getStore (node.store); store != nullptr && store->getType() == kumamp::BlockType::DigitalDelay)
        {
            store->setParam (kumamp::DigitalDelayBlock::Sync, 1.0f);
            store->setParam (kumamp::DigitalDelayBlock::Division, 10.0f);   // 1/8.
            for (auto* child : sections())
            {
                if (auto* eqView = dynamic_cast<kumamp::EqView*> (child))
                    eqView->setVisible (false);
                if (auto* gridView = dynamic_cast<kumamp::GridView*> (child))
                    gridView->selectNode (node.id);
            }
        }
    pump (200);
    allOk = save ("editor_delay_sync.png") && allOk;

    // Pitch: global switch on "Bajo" and a Pitch Shifter block in bass mode before the amp.
    if (auto* param = processor.getParameters().getParameter ("pitch_on"))
        param->setValueNotifyingHost (1.0f);
    if (auto* param = processor.getParameters().getParameter ("pitch_instrument"))
        param->setValueNotifyingHost (1.0f);
    if (const auto* amp = engine.getModel().findNode (ampNode))
    {
        const auto pitchNode = engine.insertBlock (amp->inputs[0], amp->id, kumamp::BlockType::PitchShifter, false);
        if (const auto* node = engine.getModel().findNode (pitchNode))
            if (auto store = engine.getStore (node->store))
            {
                store->setParam (kumamp::PitchShifterBlock::Semitones, -5.0f);
                store->setParam (kumamp::PitchShifterBlock::Instrument, 1.0f);
            }
        pump (100);
        for (auto* child : sections())
            if (auto* gridView = dynamic_cast<kumamp::GridView*> (child))
                gridView->selectNode (pitchNode);
    }
    pump (200);
    allOk = save ("editor_pitch_bass.png") && allOk;

    // Guitar Synth in "Textura" mode, with the input gate in "Avanzado".
    if (auto* param = processor.getParameters().getParameter ("gate_mode"))
        param->setValueNotifyingHost (1.0f);
    std::shared_ptr<kumamp::ParamStore> synthStore;
    if (const auto* amp = engine.getModel().findNode (ampNode))
    {
        const auto synthNode = engine.insertBlock (amp->inputs[0], amp->id, kumamp::BlockType::GuitarSynth, false);
        if (const auto* node = engine.getModel().findNode (synthNode))
            if (auto store = engine.getStore (node->store))
            {
                store->setParam (kumamp::GuitarSynthBlock::Mode, 2.0f);
                store->setParam (kumamp::GuitarSynthBlock::Mix, 35.0f);
                synthStore = store;
            }
        pump (100);
        for (auto* child : sections())
            if (auto* gridView = dynamic_cast<kumamp::GridView*> (child))
                gridView->selectNode (synthNode);
    }
    pump (200);
    allOk = save ("editor_synth_texture.png") && allOk;

    // Same block as an instrument with the arpeggiator: only the relevant controls show.
    if (synthStore != nullptr)
    {
        synthStore->setParam (kumamp::GuitarSynthBlock::Mode, (float) kumamp::GuitarSynthBlock::Voice::Instrument);
        synthStore->setParam (kumamp::GuitarSynthBlock::Sound, 7.0f);
        synthStore->setParam (kumamp::GuitarSynthBlock::Sequence, 1.0f);
        pump (300);
        allOk = save ("editor_synth_instrument.png") && allOk;
    }

    // Sitar and Charango (Modelado) panels.
    for (const auto type : { kumamp::BlockType::Sitar, kumamp::BlockType::Charango })
        if (const auto* amp = engine.getModel().findNode (ampNode))
        {
            const auto node = engine.insertBlock (amp->inputs[0], amp->id, type, false);
            if (const auto* inserted = engine.getModel().findNode (node))
                if (auto store = engine.getStore (inserted->store))
                    store->setParam (type == kumamp::BlockType::Charango ? (int) kumamp::CharangoBlock::Mode
                                                                         : (int) kumamp::SitarBlock::Tanpura, 1.0f);
            pump (100);
            for (auto* child : sections())
                if (auto* gridView = dynamic_cast<kumamp::GridView*> (child))
                    gridView->selectNode (node);
            pump (300);
            allOk = save (type == kumamp::BlockType::Sitar ? "editor_sitar.png" : "editor_charango.png") && allOk;
        }
    // Sampler: panel and sound editor, with a sound in a temporary file.
    if (const auto* amp = engine.getModel().findNode (ampNode))
    {
        const auto soundFile = juce::File::createTempFile (".wav");
        {
            juce::WavAudioFormat wavFormat;
            std::unique_ptr<juce::AudioFormatWriter> writer (wavFormat.createWriterFor (soundFile.createOutputStream().release(), 48000.0, 1, 24, {}, 0));
            juce::AudioBuffer<float> tone (1, 72000);
            for (int i = 0; i < tone.getNumSamples(); ++i)
            {
                const float t = (float) (i - 12000) / 48000.0f;
                tone.setSample (0, i, t < 0.0f ? 0.0f : std::exp (-3.0f * t) * std::sin (juce::MathConstants<float>::twoPi * 220.0f * t));
            }
            writer->writeFromAudioSampleBuffer (tone, 0, tone.getNumSamples());
        }
        const auto node = engine.insertBlock (amp->inputs[0], amp->id, kumamp::BlockType::Sampler, false);
        pump (100);
        for (auto* child : sections())
            if (auto* gridView = dynamic_cast<kumamp::GridView*> (child))
                gridView->selectNode (node);
        pump (200);
        allOk = save ("editor_sampler_panel.png") && allOk;
        if (const auto* inserted = engine.getModel().findNode (node))
            if (auto store = engine.getStore (inserted->store))
                if (auto* block = dynamic_cast<kumamp::SamplerBlock*> (store.get()))
                {
                    block->loadFile (0, soundFile.getFullPathName().toStdString());
                    pump (1200);   // decoded, and the panel opens the editor
                    store->setParam (kumamp::SamplerBlock::Start, 15.0f);
                    store->setParam (kumamp::SamplerBlock::End, 80.0f);
                    pump (200);
                    allOk = save ("editor_sampler.png") && allOk;
                    for (auto* child : sections())
                        if (auto* overlay = dynamic_cast<kumamp::SampleEditorView*> (child))
                            overlay->onClose();
                    pump (60);
                }
        soundFile.deleteFile();
    }
    if (auto* param = processor.getParameters().getParameter ("gate_mode"))
        param->setValueNotifyingHost (0.0f);

    // PRESETS tab with a few presets in a temporary folder (never the user's Documents).
    {
        auto& presets = processor.getPresets();
        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("kumamp_snapshot_presets");
        folder.deleteRecursively();
        presets.setFolder (folder);
        presets.save ("Crunch Plexi", "Kumo", "Kumo Signature", "Les Paul, pastilla puente");
        presets.save ("Lead Ambient", "Kumo", "Kumo Signature", "");
        presets.save ("Clean Funk", "Kumo", "", "Strat, posicion 4");
        for (auto* child : sections())
            if (auto* header = dynamic_cast<kumamp::HeaderBar*> (child))
                if (header->onTabChanged) header->onTabChanged (1);
        pump (60);
        allOk = save ("editor_presets.png") && allOk;
        for (auto* child : sections())
            if (auto* header = dynamic_cast<kumamp::HeaderBar*> (child))
                if (header->onTabChanged) header->onTabChanged (0);
        pump (30);
        folder.deleteRecursively();
    }

    // Amp block showing the "Variantes" row of a downloaded TONE3000 tone, in a temporary library.
    {
        const auto library = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("kumamp_snapshot_library");
        library.deleteRecursively();
        setTestEnv ("KUMAMP_TONE3000_DIR", library.getFullPathName());
        const auto toneDir = library.getChildFile ("Deluxe Reverb 65 Reissue Clean - kumo");
        library.getChildFile ("Plexi Crunch - otro").createDirectory();
        juce::File (KUMAMP_NAM_EXAMPLES).getChildFile ("lstm.nam").copyFileTo (library.getChildFile ("Plexi Crunch - otro").getChildFile ("Plexi 3.nam"));
        kumamp::t3k::Tone tone;
        tone.id = 51649;
        tone.title = "Deluxe Reverb '65 Reissue Clean";
        tone.creator = "kumo";
        tone.license = "t3k";
        juce::Array<kumamp::t3k::Model> variants;
        int id = 1;
        for (const char* name : { "Fender DRRI | Clean | Room Only | Full Rig", "Fender DRRI | Clean | SM57 + Royer R-121 | Full Rig",
                                  "Fender DRRI | Clean | SM57 + Royer R-121 + Room | Full Rig" })
        {
            kumamp::t3k::Model m;
            m.id = id++;
            m.name = name;
            variants.add (m);
        }
        kumamp::t3k::writeToneFolder (toneDir, tone, variants);
        const auto modelFile = toneDir.getChildFile (kumamp::t3k::modelFileName (variants[1]));
        juce::File (KUMAMP_NAM_EXAMPLES).getChildFile ("wavenet.nam").copyFileTo (modelFile);
        if (const auto* node = engine.getModel().findNode (ampNode))
            if (auto* block = dynamic_cast<kumamp::DSPBlock*> (engine.getStore (node->store).get()))
                block->loadFile (0, modelFile.getFullPathName().toStdString());
        for (auto* child : sections())
            if (auto* gridView = dynamic_cast<kumamp::GridView*> (child))
            {
                gridView->selectNode (kumamp::kInvalidNode);
                pump (20);
                gridView->selectNode (ampNode);
            }
        pump (400);
        allOk = save ("editor_amp_variants.png") && allOk;

        // The model selector lists the library: "Mis tonos (TONE3000)" with one submenu per tone.
        juce::ComboBox* modelCombo = nullptr;
        std::function<void (juce::Component&)> find = [&] (juce::Component& c)
        {
            for (auto* child : c.getChildren())
            {
                if (modelCombo == nullptr)
                    if (auto* combo = dynamic_cast<juce::ComboBox*> (child); combo != nullptr && combo->getText().endsWith (".nam"))
                        modelCombo = combo;
                find (*child);
            }
        };
        find (*editor);
        int libraryItems = 0, toneMenus = 0;
        bool heading = false;
        if (modelCombo != nullptr)
            for (juce::PopupMenu::MenuItemIterator it (*modelCombo->getRootMenu(), true); it.next();)
            {
                const auto& item = it.getItem();
                heading = heading || (item.isSectionHeader && item.text.startsWith ("Mis tonos"));
                toneMenus += item.subMenu != nullptr ? 1 : 0;
                libraryItems += item.itemID >= 20000 ? 1 : 0;
            }
        std::printf ("library in model selector: heading %d, %d tone menus, %d models\n", (int) heading, toneMenus, libraryItems);
        allOk = allOk && modelCombo != nullptr && heading && toneMenus == 2 && libraryItems == 2;

        library.deleteRecursively();
        setTestEnv ("KUMAMP_TONE3000_DIR", {});
    }

    // Overdrive panel and the IR loader with KumAmp's own cabinets.
    {
        kumamp::NodeId driveNode = kumamp::kInvalidNode, cabNode = kumamp::kInvalidNode;
        for (const auto& node : engine.getModel().getNodes())
            if (auto store = engine.getStore (node.store))
            {
                if (store->getType() == kumamp::BlockType::Overdrive) driveNode = node.id;
                if (store->getType() == kumamp::BlockType::IrLoader)  cabNode = node.id;
            }
        auto select = [&] (kumamp::NodeId id)
        {
            for (auto* child : sections())
                if (auto* gridView = dynamic_cast<kumamp::GridView*> (child))
                {
                    gridView->selectNode (kumamp::kInvalidNode);
                    pump (20);
                    gridView->selectNode (id);
                }
            pump (300);
        };
        select (driveNode);
        allOk = save ("editor_drive.png") && driveNode != kumamp::kInvalidNode && allOk;
        select (cabNode);
        allOk = save ("editor_cab.png") && allOk;

        int factoryMenus = 0, factoryItems = 0;
        bool headings = false;
        std::function<void (juce::Component&)> scan = [&] (juce::Component& c)
        {
            for (auto* child : c.getChildren())
            {
                if (auto* combo = dynamic_cast<juce::ComboBox*> (child); combo != nullptr && combo->getText().contains ("4x12") && factoryMenus == 0)
                    for (juce::PopupMenu::MenuItemIterator it (*combo->getRootMenu(), true); it.next();)
                    {
                        const auto& item = it.getItem();
                        headings = headings || (item.isSectionHeader && item.text.startsWith ("Gabinetes KumAmp"));
                        factoryMenus += item.subMenu != nullptr ? 1 : 0;
                        factoryItems += item.itemID >= 30000 ? 1 : 0;
                    }
                scan (*child);
            }
        };
        scan (*editor);
        std::printf ("factory cabs in IR selector: headings %d, %d cab menus, %d IRs\n", (int) headings, factoryMenus, factoryItems);
        allOk = allOk && headings && factoryMenus == (int) kumamp::cabs::cabinets().size()
                && factoryItems == (int) (kumamp::cabs::cabinets().size() * kumamp::cabs::mics().size());
    }

    // TONE3000 model chooser (fake tone, nothing is downloaded).
    for (auto* child : sections())
        if (auto* picker = dynamic_cast<kumamp::Tone3000Picker*> (child))
        {
            kumamp::t3k::Tone tone;
            tone.title = "Deluxe Reverb '65 Reissue Clean";
            tone.creator = "kumo";
            tone.license = "t3k";
            juce::Array<kumamp::t3k::Model> models;
            for (const char* name : { "Fender DRRI | Clean | Room Only | Full Rig", "Fender DRRI | Clean | SM57 + Royer R-121 | Full Rig",
                                      "Fender DRRI | Clean | SM57 + Royer R-121 + Room | Full Rig" })
            {
                kumamp::t3k::Model m;
                m.name = name;
                m.size = "standard";
                models.add (m);
            }
            picker->show (tone, models);
            pump (60);
            allOk = save ("editor_tone3000_picker.png") && allOk;
            picker->setVisible (false);
        }

    // Small screens: the whole interface at 70 %.
    if (kumEditor != nullptr)
    {
        kumEditor->setUiScale (0.7f, false);
        pump (60);
        allOk = save ("editor_scale70.png") && allOk;
        std::printf ("scale 70%%: window %dx%d\n", editor->getWidth(), editor->getHeight());
        allOk = allOk && editor->getWidth() == 1050 && editor->getHeight() == 616;
        kumEditor->setUiScale (1.0f, false);
        pump (60);
    }

    // CAMPO view: arrival, a form dragged and stretched, a fusion, tokens being blended.
    if (kumEditor != nullptr)
    {
        auto& field = kumEditor->getFieldView();
        auto& session = processor.getField();
        const auto tones = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("kumamp_snapshot_tones");
        tones.deleteRecursively();
        session.setToneFolder (tones);
        kumEditor->showField (true);
        session.setToneFolder (tones);
        for (int i = 0; i < 12; ++i)
        {
            pump (2);
            field.advanceAnimation (1.0 / 30.0);
        }
        allOk = save ("editor_field.png") && allOk;

        const auto blobNamed = [&session] (kumamp::BlockType type)
        {
            for (int i = 0; i < (int) session.getBlobs().size(); ++i)
                if (session.getBlobs()[(size_t) i].type == type)
                    return i;
            return 0;
        };
        const int amp = blobNamed (kumamp::BlockType::NamAmp), delay = blobNamed (kumamp::BlockType::DigitalDelay),
                  reverb = blobNamed (kumamp::BlockType::PlateReverb), chorus = blobNamed (kumamp::BlockType::Chorus);
        for (const int i : { amp, delay, reverb, chorus })
            if (! session.getBlobs()[(size_t) i].on)
                session.toggleBlob (i);

        auto c = field.getBlobCentre (chorus);
        field.press (c);
        field.dragTo (c.translated (-30.0f, -130.0f));
        pump (2);
        allOk = save ("editor_field_drag.png") && allOk;
        field.release();

        const auto handle = field.getBlobHandle (amp);
        field.press (handle);
        field.dragTo (handle.translated (60.0f, 110.0f));
        pump (2);
        allOk = save ("editor_field_stretch.png") && allOk;
        field.release();

        c = field.getBlobCentre (reverb);
        const auto target = field.getBlobCentre (delay).translated (34.0f, 16.0f);
        field.press (c);
        field.dragTo (target);
        field.release();
        for (int i = 0; i < 6; ++i)
            field.advanceAnimation (1.0 / 30.0);
        session.saveTone();
        session.mutate();
        session.saveTone();
        pump (2);
        allOk = save ("editor_field_fusion.png") && allOk;

        const auto a = field.getTokenCentre (0), b = field.getTokenCentre (1);
        field.press (a);
        field.dragTo (a + (b - a) * 0.5f);
        pump (2);
        allOk = save ("editor_field_blend.png") && allOk;
        field.cancelDrag();

        // The tampura: its mandala lit, alone and then sent through the delay.
        session.resetFromGrid();
        for (const int i : { amp, delay, reverb })
            if (! session.getBlobs()[(size_t) i].on)
                session.toggleBlob (i);
        int drone = -1;
        for (int i = 0; i < (int) session.getBlobs().size(); ++i)
            if (session.getBlobs()[(size_t) i].tanpura)
                drone = i;
        if (drone >= 0)
        {
            if (! session.getBlobs()[(size_t) drone].on)
                session.toggleBlob (drone);
            session.setBlobStretch (drone, 0.2f, 2.2f);
            session.setBlobPosition (drone, 0.5f, 0.45f);
            for (int i = 0; i < 9; ++i)
                field.advanceAnimation (1.0 / 24.0);
            pump (2);
            allOk = save ("editor_field_tampura.png") && allOk;

            const auto& d = session.getBlobs()[(size_t) delay];
            session.setBlobPosition (drone, d.x + 0.06f, d.y - 0.06f);
            for (int i = 0; i < 5; ++i)
                field.advanceAnimation (1.0 / 24.0);
            pump (2);
            allOk = save ("editor_field_tampura_fused.png") && allOk;
        }

        // What the animation costs on this machine.
        juce::Image frame (juce::Image::RGB, field.getWidth(), field.getHeight(), true);
        const auto start = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 20; ++i)
        {
            juce::Graphics g (frame);
            field.advanceAnimation (1.0 / 30.0);
            field.paint (g);
        }
        std::printf ("field view: %.1f ms per frame\n", (juce::Time::getMillisecondCounterHiRes() - start) / 20.0);

        kumEditor->showField (false);
        pump (5);
        allOk = save ("editor_field_back.png") && allOk;
        tones.deleteRecursively();
    }

    // SIMP view and ACERCA DE.
    if (kumEditor != nullptr)
    {
        kumEditor->showSimp (true);
        pump (5);
        allOk = save ("editor_simp.png") && allOk;
        kumEditor->getSimpView().presetNameSource = [] { return juce::String ("High Gain"); };
        kumEditor->getSimpView().refresh();
        pump (5);
        allOk = save ("editor_simp_icons.png") && allOk;
        kumEditor->showSimp (false);
        kumEditor->showAbout (true);
        pump (5);
        allOk = save ("editor_about.png") && allOk;
        kumEditor->showAbout (false);
        pump (5);
    }

    // Settings tab with guitar -> MIDI on: check MIDI notes come out of processBlock.
    if (auto* param = processor.getParameters().getParameter ("midi_out_on"))
        param->setValueNotifyingHost (1.0f);
    int noteOns = 0, firstNote = -1;
    for (int b = 0; b < 1500; ++b)
    {
        for (int i = 0; i < 64; ++i)
        {
            const float v = (float) (0.05 * std::sin (phase) + 0.15 * std::sin (2 * phase + 0.4) + 0.08 * std::sin (3 * phase));
            buffer.setSample (0, i, v);
            buffer.setSample (1, i, v);
            phase += 2.0 * juce::MathConstants<double>::pi * f / 48000.0;
        }
        midi.clear();
        processor.processBlock (buffer, midi);
        for (const auto m : midi)
            if (m.getMessage().isNoteOn()) { ++noteOns; if (firstNote < 0) firstNote = m.getMessage().getNoteNumber(); }
        if (b % 25 == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil (15);
    }
    std::printf ("MIDI out: %d note-on(s), first note %d (expected 40)\n", noteOns, firstNote);
    allOk = allOk && noteOns >= 1 && firstNote == 40;
    for (auto* child : sections())
        if (auto* header = dynamic_cast<kumamp::HeaderBar*> (child))
            if (header->onTabChanged) header->onTabChanged (4);
    pump (60);
    allOk = save ("editor_settings.png") && allOk;

    editor.reset();
    return allOk ? 0 : 1;
}
