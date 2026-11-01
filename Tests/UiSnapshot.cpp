// Instantiates the real processor + editor, feeds audio (meters, tuner, NAM
// model), selects the amp block and writes the window to PNG.

#include "PluginEditor.h"
#include "DSP/Time/DelayBlocks.h"

#include <cstdio>

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
    editor->setSize (1500, 880);

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
    for (auto* child : editor->getChildren())
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
    for (auto* child : editor->getChildren())
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

    for (auto* child : editor->getChildren())
        if (auto* panelView = dynamic_cast<kumamp::BlockPanel*> (child))
            if (panelView->onExpandEq)
                panelView->onExpandEq (engine.getStore (engine.getModel().findNode (eqNode)->store));
    pump (300);
    // Click band 3's node in the large EQ so the edit strip shows.
    for (auto* child : editor->getChildren())
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

    // Delay with tempo sync on.
    for (const auto& node : engine.getModel().getNodes())
        if (auto store = engine.getStore (node.store); store != nullptr && store->getType() == kumamp::BlockType::DigitalDelay)
        {
            store->setParam (kumamp::DigitalDelayBlock::Sync, 1.0f);
            store->setParam (kumamp::DigitalDelayBlock::Division, 10.0f);   // 1/8.
            for (auto* child : editor->getChildren())
            {
                if (auto* eqView = dynamic_cast<kumamp::EqView*> (child))
                    eqView->setVisible (false);
                if (auto* gridView = dynamic_cast<kumamp::GridView*> (child))
                    gridView->selectNode (node.id);
            }
        }
    pump (200);
    allOk = save ("editor_delay_sync.png") && allOk;

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
    for (auto* child : editor->getChildren())
        if (auto* header = dynamic_cast<kumamp::HeaderBar*> (child))
            if (header->onTabChanged) header->onTabChanged (4);
    pump (60);
    allOk = save ("editor_settings.png") && allOk;

    editor.reset();
    return allOk ? 0 : 1;
}
