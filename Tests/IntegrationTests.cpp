// Integration tests with real JUCE + NeuralAmpModelerCore:
// NAM loading (A1 / A2 / LSTM), resampling, End-to-End -> IR auto-bypass,
// IR convolution, state round-trip and "no operator new on the audio path".

#include "DSP/Amp/NamAmpBlock.h"
#include "DSP/Cab/IrLoaderBlock.h"
#include "Engine/GridEngine.h"
#include "Tuner/Tuner.h"
#include "UI/TunerView.h"
#include "UI/GridView.h"
#include "UI/BlockPanel.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <cstdio>
#include <fstream>
#include <new>

using namespace kumamp;

//== Allocation tracking: counts operator new while the "audio" flag is set ===
static thread_local bool gTrackAllocations = false;
static std::atomic<int> gAudioAllocations { 0 };

void* operator new (std::size_t size)
{
    if (gTrackAllocations)
        ++gAudioAllocations;
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

static int failures = 0;
#define CHECK(cond) do { if (! (cond)) { ++failures; std::printf ("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

namespace
{
constexpr int kBlock = 256;
const juce::File kExamples { KUMAMP_NAM_EXAMPLES };

struct AudioScope
{
    AudioScope()  { gTrackAllocations = true; }
    ~AudioScope() { gTrackAllocations = false; }
};

/** Process `block` with a sine until `done()` or timeout. Returns false on timeout. */
template <typename Done>
bool runUntil (DSPBlock& block, double sampleRate, Done&& done, double seconds = 20.0)
{
    std::vector<float> l (kBlock), r (kBlock);
    double phase = 0.0;
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + seconds * 1000.0;

    while (juce::Time::getMillisecondCounterHiRes() < deadline)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            l[(size_t) i] = r[(size_t) i] = 0.3f * (float) std::sin (phase);
            phase += 2.0 * juce::MathConstants<double>::pi * 110.0 / sampleRate;
        }
        block.process (l.data(), r.data(), kBlock);
        block.collectGarbage();
        if (done())
            return true;
        juce::Thread::sleep (2);
    }
    return false;
}

/** Returns RMS and checks output finiteness over ~1 s of audio. */
double measure (DSPBlock& block, double sampleRate, bool& finite, int& allocations)
{
    std::vector<float> l (kBlock), r (kBlock);
    double acc = 0.0, phase = 0.0;
    finite = true;
    const int before = gAudioAllocations.load();
    const int blocks = (int) (sampleRate / kBlock);

    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            l[(size_t) i] = r[(size_t) i] = 0.3f * (float) std::sin (phase);
            phase += 2.0 * juce::MathConstants<double>::pi * 110.0 / sampleRate;
        }
        {
            AudioScope scope;
            block.process (l.data(), r.data(), kBlock);
        }
        for (int i = 0; i < kBlock; ++i)
        {
            finite = finite && std::isfinite (l[(size_t) i]) && std::isfinite (r[(size_t) i]);
            acc += (double) l[(size_t) i] * l[(size_t) i];
        }
    }
    allocations = gAudioAllocations.load() - before;
    return std::sqrt (acc / (double) (blocks * kBlock));
}

juce::File writeVariant (const juce::File& source, const std::string& gearType)
{
    auto json = nlohmann::json::parse (source.loadFileAsString().toStdString());
    json["metadata"]["gear_type"] = gearType;
    const auto out = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("kumamp_" + juce::String (gearType) + ".nam");
    out.replaceWithText (juce::String (json.dump()));
    return out;
}

void testClassification()
{
    std::puts ("profile classification");
    CHECK (classifyGearType ("amp") == ProfileKind::AmpOnly);
    CHECK (classifyGearType ("pedal_amp") == ProfileKind::AmpOnly);
    CHECK (classifyGearType ("amp_cab") == ProfileKind::EndToEnd);
    CHECK (classifyGearType ("AMP_PEDAL_CAB") == ProfileKind::EndToEnd);
    CHECK (classifyGearType ("full_rig") == ProfileKind::EndToEnd);
    CHECK (classifyGearType ("") == ProfileKind::Unknown);
}

void testNamModel (const char* fileName, double sampleRate)
{
    std::printf ("NAM %s @ %.0f Hz\n", fileName, sampleRate);
    NamAmpBlock block (1);
    block.prepare (sampleRate, kBlock);
    CHECK (block.loadFile (0, kExamples.getChildFile (fileName).getFullPathName().toStdString()));

    const bool loaded = runUntil (block, sampleRate, [&] { return block.hasModel(); });
    CHECK (loaded);
    std::printf ("  status: %s\n", block.getStatusText().c_str());
    if (! loaded)
        return;

    runUntil (block, sampleRate, [] { return false; }, 0.1);   // let the 50 ms fade-in finish

    bool finite = false;
    int allocations = 0;
    const double rms = measure (block, sampleRate, finite, allocations);
    std::printf ("  rms %.4f  audio-thread allocations %d\n", rms, allocations);
    CHECK (finite);
    CHECK (rms > 1.0e-4);
    CHECK (allocations == 0);
}

void testEndToEndDecouplesIr()
{
    std::puts ("engine: E2E profile auto-bypasses IR loader");
    GridEngine engine;
    engine.prepare (48000.0, kBlock);
    engine.loadDefaultPreset();

    std::shared_ptr<ParamStore> ampStore, cabStore;
    for (const auto& node : engine.getModel().getNodes())
        if (auto s = engine.getStore (node.store))
        {
            if (s->getType() == BlockType::NamAmp)   ampStore = s;
            if (s->getType() == BlockType::IrLoader) cabStore = s;
        }
    CHECK (ampStore != nullptr && cabStore != nullptr);
    if (! ampStore || ! cabStore) return;

    auto* amp = static_cast<NamAmpBlock*> (ampStore.get());
    auto* cab = static_cast<IrLoaderBlock*> (cabStore.get());

    const auto e2eFile = writeVariant (kExamples.getChildFile ("wavenet.nam"), "amp_cab");
    amp->loadFile (0, e2eFile.getFullPathName().toStdString());

    juce::AudioBuffer<float> buffer (2, kBlock);
    auto pump = [&] (double seconds, auto&& done)
    {
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + seconds * 1000.0;
        while (juce::Time::getMillisecondCounterHiRes() < deadline)
        {
            buffer.clear();
            engine.process (buffer.getArrayOfReadPointers(), 2, buffer.getArrayOfWritePointers(), 2, kBlock);
            engine.runMessageThreadTasks();
            if (done()) return true;
            juce::Thread::sleep (2);
        }
        return false;
    };

    CHECK (pump (20.0, [&] { return amp->hasModel(); }));
    CHECK (amp->getEffectiveProfileKind() == ProfileKind::EndToEnd);
    CHECK (pump (1.0, [&] { return cab->isAutoBypassActive(); }));
    std::printf ("  amp: %s\n  cab: %s\n", amp->getStatusText().c_str(), cab->getStatusText().c_str());

    amp->setParam (NamAmpBlock::ProfileMode, NamAmpBlock::ModeAmpOnly);   // manual override
    CHECK (pump (1.0, [&] { return ! cab->isAutoBypassActive(); }));

    amp->setParam (NamAmpBlock::ProfileMode, NamAmpBlock::ModeAuto);
    cab->setParam (IrLoaderBlock::AutoE2E, 0.0f);                          // user opts out
    pump (0.1, [] { return false; });
    CHECK (! cab->isAutoBypassActive());

    // State round-trip keeps the model path.
    const auto saved = engine.saveState();
    GridEngine restored;
    restored.prepare (48000.0, kBlock);
    restored.loadState (saved);
    bool pathRestored = false;
    for (const auto& node : restored.getModel().getNodes())
        if (auto* b = dynamic_cast<NamAmpBlock*> (restored.getStore (node.store).get()))
            pathRestored = b->getFilePath (0) == e2eFile.getFullPathName().toStdString();
    CHECK (pathRestored);
}

void testIrConvolution()
{
    std::puts ("IR loader convolution");
    // IR = single tap at sample 10.
    const auto irFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("kumamp_test_ir.wav");
    {
        juce::AudioBuffer<float> ir (1, 64);
        ir.clear();
        ir.setSample (0, 10, 1.0f);
        irFile.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (irFile.createOutputStream().release());
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), 48000.0, 1, 24, {}, 0));
        CHECK (writer != nullptr);
        if (writer == nullptr) return;
        stream.release();
        writer->writeFromAudioSampleBuffer (ir, 0, ir.getNumSamples());
    }

    IrLoaderBlock block (1);
    block.prepare (48000.0, kBlock);
    CHECK (block.loadFile (0, irFile.getFullPathName().toStdString()));

    // Wait until an impulse comes out 10 samples late (JUCE swaps the IR asynchronously).
    std::vector<float> l (kBlock), r (kBlock);
    bool delayed = false;
    int allocations = 0;
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 10000.0;
    while (! delayed && juce::Time::getMillisecondCounterHiRes() < deadline)
    {
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        l[5] = r[5] = 1.0f;
        const int before = gAudioAllocations.load();
        {
            AudioScope scope;
            block.process (l.data(), r.data(), kBlock);
        }
        allocations += gAudioAllocations.load() - before;
        delayed = std::abs (l[15]) > 0.05f && std::abs (l[5]) < 1.0e-3f;
        juce::Thread::sleep (5);
    }
    std::printf ("  status: %s  audio-thread allocations %d\n", block.getStatusText().c_str(), allocations);
    CHECK (delayed);
    CHECK (allocations == 0);

    block.setAutoBypassed (true);   // E2E upstream -> dry after the 30 ms fade
    for (int i = 0; i < 20; ++i)
    {
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        l[5] = r[5] = 1.0f;
        block.process (l.data(), r.data(), kBlock);
    }
    CHECK (std::abs (l[5] - 1.0f) < 1.0e-4f && std::abs (l[15]) < 1.0e-4f);
}
void testTunerPipeline()
{
    std::puts ("tuner: audio thread -> fifo -> analysis thread");
    Tuner tuner;
    tuner.prepare (48000.0);
    tuner.setListening (true);

    // Low E, 5 cents sharp, string-like spectrum (weak fundamental).
    const double f = 82.4069 * std::exp2 (5.0 / 1200.0);
    std::vector<float> l (kBlock), r (kBlock);
    double phase = 0.0;
    Tuner::Reading reading;
    int allocations = 0;
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 3000.0;
    while (juce::Time::getMillisecondCounterHiRes() < deadline)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            const float v = (float) (0.1 * std::sin (phase) + 0.3 * std::sin (2 * phase + 0.4) + 0.15 * std::sin (3 * phase + 1.0));
            l[(size_t) i] = r[(size_t) i] = v;
            phase += 2.0 * juce::MathConstants<double>::pi * f / 48000.0;
        }
        const float* ch[] { l.data(), r.data() };
        const int before = gAudioAllocations.load();
        {
            AudioScope scope;
            tuner.pushAudio (ch, 2, kBlock);
        }
        allocations += gAudioAllocations.load() - before;
        juce::Thread::sleep (5);   // ~real time
        reading = tuner.getLatest();
    }

    std::printf ("  note %s%d  %.2f Hz  %+.2f ct  clarity %.2f  audio-thread allocations %d\n",
                 noteName (reading.midiNote), noteOctave (reading.midiNote), reading.frequency, reading.cents, reading.clarity, allocations);
    CHECK (reading.valid);
    CHECK (reading.midiNote == 40);
    CHECK (std::abs (reading.cents - 5.0f) < 0.5f);
    CHECK (allocations == 0);

    // Signal stops -> reading becomes invalid.
    juce::Thread::sleep (400);
    CHECK (! tuner.getLatest().valid);
}

void testTunerSnapshots()
{
    std::puts ("tuner view snapshots");
    const juce::File dir (KUMAMP_SNAPSHOT_DIR);
    dir.createDirectory();

    for (int mode : { TunerView::Traditional, TunerView::Trace })
    {
        TunerView view;
        view.setBounds (0, 0, 1000, 600);
        view.setSettings (mode, true, 440.0f);

        // Simulated playing: E2 attack sharp, settling with slight vibrato; pause; A2 arriving flat.
        double t = 0.0;
        auto push = [&] (bool valid, int note, float cents)
        {
            Tuner::Reading r;
            r.valid = valid;
            r.midiNote = note;
            r.cents = cents;
            r.frequency = valid ? 440.0f * std::exp2 (((float) note + cents / 100.0f - 69.0f) / 12.0f) : 0.0f;
            view.pushReading (r, t);
            t += 1.0 / 60.0;
        };
        for (int i = 0; i < 150; ++i) push (true, 40, 22.0f * std::exp (-i / 35.0f) + 1.5f * (float) std::sin (i * 0.25));
        for (int i = 0; i < 40; ++i)  push (false, -1, 0.0f);
        for (int i = 0; i < 200; ++i) push (true, 45, -14.0f * std::exp (-i / 60.0f) + 0.8f * (float) std::sin (i * 0.3) - 0.6f);

        const auto image = view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f);
        const auto file = dir.getChildFile (mode == TunerView::Traditional ? "tuner_traditional.png" : "tuner_trace.png");
        file.deleteFile();
        juce::FileOutputStream out (file);
        CHECK (juce::PNGImageFormat().writeImageToStream (image, out));
        std::printf ("  %s\n", file.getFullPathName().toRawUTF8());
    }
}
void testGridEditing()
{
    std::puts ("grid editor: insert serial / parallel, remove, collapse");
    GridEngine engine;
    engine.prepare (48000.0, kBlock);
    engine.loadDefaultPreset();

    auto findNodeOfType = [&] (BlockType type) -> const GridNode*
    {
        for (const auto& n : engine.getModel().getNodes())
            if (auto s = engine.getStore (n.store); s != nullptr && s->getType() == type)
                return &n;
        return nullptr;
    };
    auto processSome = [&]
    {
        juce::AudioBuffer<float> buffer (2, kBlock);
        bool finite = true;
        for (int b = 0; b < 200; ++b)
        {
            for (int i = 0; i < kBlock; ++i)
                buffer.setSample (0, i, 0.3f * std::sin ((float) (b * kBlock + i) * 0.02f)), buffer.setSample (1, i, buffer.getSample (0, i));
            engine.process (buffer.getArrayOfReadPointers(), 2, buffer.getArrayOfWritePointers(), 2, kBlock);
            engine.runMessageThreadTasks();
            for (int i = 0; i < kBlock; ++i) finite = finite && std::isfinite (buffer.getSample (0, i));
        }
        return finite;
    };

    const auto baseCount = engine.getModel().getNodes().size();
    const auto* cab = findNodeOfType (BlockType::IrLoader);
    CHECK (cab != nullptr);
    if (cab == nullptr) return;

    // Series: compressor between Mix and Amp (on the Amp node's input cable).
    const auto* amp = findNodeOfType (BlockType::NamAmp);
    const NodeId comp = engine.insertBlock (amp->inputs[0], amp->id, BlockType::Compressor, false);
    CHECK (comp != kInvalidNode);
    CHECK (engine.getModel().getNodes().size() == baseCount + 1);
    CHECK (processSome());

    // Parallel: hall reverb A/B branch after the cab.
    cab = findNodeOfType (BlockType::IrLoader);
    const GridNode* afterCab = nullptr;
    for (const auto& n : engine.getModel().getNodes())
        for (const auto& in : n.inputs)
            if (in.node == cab->id) afterCab = &n;
    const NodeId hall = engine.insertBlock ({ cab->id, 0 }, afterCab->id, BlockType::HallReverb, true);
    CHECK (hall != kInvalidNode);
    CHECK (engine.getModel().getNodes().size() == baseCount + 4);   // + block, split, mix
    CHECK (processSome());

    const auto* hallNode = engine.getModel().findNode (hall);
    std::printf ("  hall reverb placed at row %d, col %d; grid has %zu nodes\n", hallNode->row, hallNode->column, engine.getModel().getNodes().size());
    CHECK (hallNode->row > 0);   // its own lane

    // Remove the reverb: the empty split/mix collapse away.
    CHECK (engine.removeBlock (hall));
    CHECK (engine.getModel().getNodes().size() == baseCount + 1);
    CHECK (engine.removeBlock (comp));
    CHECK (engine.getModel().getNodes().size() == baseCount);
    CHECK (! engine.removeBlock (engine.getModel().getInputNode()));   // routing/IO nodes are protected
    CHECK (processSome());

    // Snapshot of the editing UI with a delay in a parallel branch selected.
    cab = findNodeOfType (BlockType::IrLoader);
    for (const auto& n : engine.getModel().getNodes())
        for (const auto& in : n.inputs)
            if (in.node == cab->id) afterCab = &n;
    const NodeId delay = engine.insertBlock ({ cab->id, 0 }, afterCab->id, BlockType::DigitalDelay, true);
    engine.insertBlock (findNodeOfType (BlockType::NamAmp)->inputs[0], findNodeOfType (BlockType::NamAmp)->id, BlockType::NoiseGate, false);

    // Drag & drop reorder: move the gate after the cab, then undo / redo.
    {
        const auto* gate = findNodeOfType (BlockType::NoiseGate);
        const auto gateId = gate->id;
        const auto originalInput = gate->inputs[0];
        cab = findNodeOfType (BlockType::IrLoader);
        const GridNode* consumer = nullptr;
        for (const auto& n : engine.getModel().getNodes())
            for (const auto& in : n.inputs)
                if (in.node == cab->id) consumer = &n;
        const auto countBefore = engine.getModel().getNodes().size();
        CHECK (engine.moveBlock (gateId, { cab->id, 0 }, consumer->id));
        CHECK (engine.getModel().findNode (gateId)->inputs[0].node == findNodeOfType (BlockType::IrLoader)->id);
        CHECK (engine.getModel().getNodes().size() == countBefore);
        CHECK (processSome());
        CHECK (engine.undo());
        CHECK (engine.getModel().findNode (gateId)->inputs[0] == originalInput);
        CHECK (engine.redo());
        CHECK (engine.getModel().findNode (gateId)->inputs[0].node == findNodeOfType (BlockType::IrLoader)->id);
        CHECK (engine.undo());
        std::puts ("  move + undo/redo ok");
    }

    juce::Component root;
    root.setBounds (0, 0, 1180, 600);
    GridView grid (engine);
    BlockPanel panel;
    root.addAndMakeVisible (grid);
    root.addAndMakeVisible (panel);
    grid.setBounds (0, 0, 1180, 340);
    panel.setBounds (0, 350, 1180, 250);
    grid.onNodeSelected = [&] (StoreId id) { panel.setStore (engine.getStore (id)); };
    grid.selectNode (delay);
    panel.refresh();

    const juce::File dir (KUMAMP_SNAPSHOT_DIR);
    dir.createDirectory();
    const auto file = dir.getChildFile ("grid_editor.png");
    file.deleteFile();
    juce::FileOutputStream out (file);
    CHECK (juce::PNGImageFormat().writeImageToStream (root.createComponentSnapshot (root.getLocalBounds()), out));
    std::printf ("  %s\n", file.getFullPathName().toRawUTF8());
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;   // JUCE singletons / threads

    testClassification();
    testNamModel ("wavenet.nam", 48000.0);
    testNamModel ("lstm.nam", 48000.0);
    testNamModel ("A2.nam", 48000.0);
    testNamModel ("wavenet_a1_standard.nam", 48000.0);
    testNamModel ("A2.nam", 44100.0);           // resampled through RateBridge
    testEndToEndDecouplesIr();
    testIrConvolution();
    testTunerPipeline();
    testTunerSnapshots();
    testGridEditing();

    std::printf (failures == 0 ? "\nALL INTEGRATION TESTS PASSED\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
