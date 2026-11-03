// End-to-end check of the *built* VST3, loaded like a DAW would: inject a .nam
// path into the plugin state and verify the model actually processes audio.
// Catches packaging problems (e.g. NAM architectures stripped by the linker)
// that in-process tests cannot see.

#include <juce_audio_processors/juce_audio_processors.h>

#include <cstdio>

namespace
{
int failures = 0;
#define CHECK(cond) do { if (! (cond)) { ++failures; std::printf ("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

/** The JUCE VST3 host wraps the plugin's own state as base64 in
    <VST3PluginState><IComponent>. Unwrap, set file_0 on the first store of
    `type`, and wrap it back. */
bool injectModel (juce::AudioPluginInstance& plugin, const char* type, const juce::File& file)
{
    juce::MemoryBlock hostData;
    plugin.getStateInformation (hostData);
    auto hostXml = juce::AudioProcessor::getXmlFromBinary (hostData.getData(), (int) hostData.getSize());
    auto* component = hostXml != nullptr ? hostXml->getChildByName ("IComponent") : nullptr;
    if (component == nullptr)
        return false;

    juce::MemoryBlock pluginData;
    pluginData.fromBase64Encoding (component->getAllSubText());
    auto pluginXml = juce::AudioProcessor::getXmlFromBinary (pluginData.getData(), (int) pluginData.getSize());
    if (pluginXml == nullptr)
        return false;

    auto state = juce::ValueTree::fromXml (*pluginXml);
    auto stores = state.getChildWithName ("KumAmpGrid").getChildWithName ("Stores");
    // Bypass every other block so the amp is the only thing that changes the signal.
    bool found = false;
    for (auto store : stores)
    {
        if (! found && store.getProperty ("type").toString() == type)
        {
            if (file != juce::File())
                store.setProperty ("file_0", file.getFullPathName(), nullptr);
            store.setProperty ("bypass", false, nullptr);
            found = true;
        }
        else
        {
            // Scenes may switch blocks back on (the cab), so also drop their files
            // (the default cab IR): an empty IR loader is transparent.
            store.setProperty ("bypass", true, nullptr);
            store.removeProperty ("file_0", nullptr);
            store.removeProperty ("file_1", nullptr);
        }
    }
    if (! found)
        return false;

    juce::MemoryBlock newPluginData;
    juce::AudioProcessor::copyXmlToBinary (*state.createXml(), newPluginData);
    component->deleteAllTextElements();
    component->addTextElement (newPluginData.toBase64Encoding());

    juce::MemoryBlock newHostData;
    juce::AudioProcessor::copyXmlToBinary (*hostXml, newHostData);
    plugin.setStateInformation (newHostData.getData(), (int) newHostData.getSize());
    return true;
}

/** Mean |output - input| over one block of a 110 Hz sine, with the input
    delayed by the latency the plugin reports (what a host compensates). */
double processAndDiff (juce::AudioPluginInstance& plugin, double& phase)
{
    const int latency = plugin.getLatencySamples();
    juce::AudioBuffer<float> buffer (2, kBlock);
    juce::MidiBuffer midi;
    std::vector<float> input (kBlock);
    for (int i = 0; i < kBlock; ++i)
    {
        input[(size_t) i] = 0.3f * (float) std::sin (phase);
        phase += 2.0 * juce::MathConstants<double>::pi * 110.0 / kRate;
        buffer.setSample (0, i, input[(size_t) i]);
        buffer.setSample (1, i, input[(size_t) i]);
    }
    plugin.processBlock (buffer, midi);

    // The sine is analytic, so the delayed input is just the same sine shifted back.
    const double step = 2.0 * juce::MathConstants<double>::pi * 110.0 / kRate;
    const double start = phase - kBlock * step;
    double diff = 0.0;
    for (int i = 0; i < kBlock; ++i)
        diff += std::abs (buffer.getSample (0, i) - 0.3f * (float) std::sin (start + (i - latency) * step));
    return diff / kBlock;
}

void testModel (juce::AudioPluginFormat& format, const juce::PluginDescription& desc, const char* modelName)
{
    std::printf ("host: VST3 + %s\n", modelName);
    juce::String error;
    auto plugin = format.createInstanceFromDescription (desc, kRate, kBlock, error);
    CHECK (plugin != nullptr);
    if (plugin == nullptr) { std::printf ("  %s\n", error.toRawUTF8()); return; }

    plugin->prepareToPlay (kRate, kBlock);
    double phase = 0.0;

    // Everything bypassed and no model: the chain must be transparent.
    CHECK (injectModel (*plugin, "nam_amp", juce::File()));
    double before = 0.0;
    for (int b = 0; b < 40; ++b) before = processAndDiff (*plugin, phase);

    CHECK (injectModel (*plugin, "nam_amp", juce::File (KUMAMP_NAM_EXAMPLES).getChildFile (modelName)));

    // Loading state recompiles the grid (a ~5 ms fade dip), so require a
    // *sustained* difference: 100 consecutive blocks (~0.5 s) of processed audio.
    double after = 0.0;
    int sustained = 0;
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 20000.0;
    while (sustained < 100 && juce::Time::getMillisecondCounterHiRes() < deadline)
    {
        const double d = processAndDiff (*plugin, phase);
        sustained = d > 1.0e-3 ? sustained + 1 : 0;
        after = sustained > 0 ? after + (d - after) / sustained : 0.0;
        if (sustained == 0)
            juce::Thread::sleep (1);
    }
    std::printf ("  mean |out-in|: sin modelo %.6f, con modelo %.6f\n", before, after);
    CHECK (before < 1.0e-4);
    CHECK (sustained >= 100);
    plugin->releaseResources();
}
} // namespace

void testMidiOut (juce::AudioPluginFormat& format, const juce::PluginDescription& desc)
{
    std::puts ("host: VST3 guitar -> MIDI output");
    juce::String error;
    auto plugin = format.createInstanceFromDescription (desc, kRate, kBlock, error);
    CHECK (plugin != nullptr);
    if (plugin == nullptr) return;
    CHECK (plugin->producesMidi());

    for (auto* p : plugin->getParameters())
        if (p->getName (64) == "Guitar MIDI Out")
            p->setValueNotifyingHost (1.0f);

    plugin->prepareToPlay (kRate, kBlock);
    juce::AudioBuffer<float> buffer (2, kBlock);
    juce::MidiBuffer midi;
    double phase = 0.0;
    int noteOns = 0, firstNote = -1;
    for (int b = 0; b < (int) (kRate / kBlock); ++b)   // 1 s of A2 (110 Hz)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            const float v = (float) (0.1 * std::sin (phase) + 0.25 * std::sin (2 * phase + 0.4));
            buffer.setSample (0, i, v);
            buffer.setSample (1, i, v);
            phase += 2.0 * juce::MathConstants<double>::pi * 110.0 / kRate;
        }
        midi.clear();
        plugin->processBlock (buffer, midi);
        for (const auto m : midi)
            if (m.getMessage().isNoteOn()) { ++noteOns; if (firstNote < 0) firstNote = m.getMessage().getNoteNumber(); }
    }
    std::printf ("  %d note-on(s) from the VST3, first note %d (A2 = 45)\n", noteOns, firstNote);
    CHECK (noteOns >= 1);
    CHECK (firstNote == 45);
    plugin->releaseResources();
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile (types, KUMAMP_VST3_PATH);
    CHECK (types.size() == 1);
    if (types.isEmpty())
        return 1;

    for (const char* model : { "wavenet.nam", "A2.nam", "lstm.nam" })
        testModel (format, *types[0], model);
    testMidiOut (format, *types[0]);

    std::printf (failures == 0 ? "\nHOST TEST PASSED\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
