// Integration tests with real JUCE + NeuralAmpModelerCore:
// NAM loading (A1 / A2 / LSTM), resampling, End-to-End -> IR auto-bypass,
// IR convolution, state round-trip and "no operator new on the audio path".

#include "DSP/Amp/NamAmpBlock.h"
#include "DSP/Cab/FactoryCabs.h"
#include "DSP/Cab/IrLoaderBlock.h"
#include "DSP/Drive/OverdriveBlock.h"
#include "DSP/Pitch/GuitarSynthBlock.h"
#include "DSP/Pitch/SamplerBlock.h"
#include "DSP/JuceBlocks.h"
#include "Graph/BlockFactory.h"
#include "Engine/GridEngine.h"
#include "Presets/PresetManager.h"
#include "Presets/FactoryPresets.h"
#include "Field/FieldSession.h"
#include "DSP/Pitch/SitarBlock.h"
#include "UI/SimpView.h"
#include "Tone3000/Tone3000.h"
#include "DSP/Time/DelayBlocks.h"
#include "Tuner/Tuner.h"
#include "UI/TunerView.h"
#include "UI/GridView.h"
#include "UI/BlockPanel.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <cstdio>
#include <fstream>
#include <new>
#include <thread>

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
#define CHECK_NEAR(a, b, tol) CHECK (std::abs ((double) (a) - (double) (b)) <= (tol))

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
namespace
{
    struct Tone
    {
        double rms = 0.0;
        double harmonics = 0.0;   // energy at 2f..10f relative to f (dB)
        double aliases = 0.0;     // energy off the harmonic series relative to f (dB)
    };

    /** Overdrive steady-state analysis of a sine (DFT on 0.5 s after 0.25 s of settling). */
    Tone analyseOverdrive (OverdriveBlock& od, double hz, float amplitude)
    {
        constexpr double sr = 48000.0;
        const int settle = 12000, length = 24000;
        std::vector<float> out;
        std::vector<float> l (kBlock), r (kBlock);
        od.reset();
        for (int pos = 0; pos < settle + length; pos += kBlock)
        {
            for (int i = 0; i < kBlock; ++i)
                l[(size_t) i] = r[(size_t) i] = amplitude * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * (pos + i) / sr);
            od.process (l.data(), r.data(), kBlock);
            for (int i = 0; i < kBlock; ++i)
                if (pos + i >= settle && (int) out.size() < length)
                    out.push_back (l[(size_t) i]);
        }
        auto energyAt = [&] (double f)
        {
            double re = 0, im = 0;
            for (size_t n = 0; n < out.size(); ++n)
            {
                const double w = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * (double) n / (double) out.size());
                re += w * out[n] * std::cos (2.0 * juce::MathConstants<double>::pi * f * (double) n / sr);
                im -= w * out[n] * std::sin (2.0 * juce::MathConstants<double>::pi * f * (double) n / sr);
            }
            return re * re + im * im;
        };
        Tone t;
        for (float x : out) t.rms += (double) x * x;
        t.rms = std::sqrt (t.rms / (double) out.size());
        const double fundamental = energyAt (hz) + 1e-30;
        double harm = 0, alias = 0;
        for (int k = 2; k <= 10 && k * hz < sr / 2; ++k)
            harm += energyAt (k * hz);
        // Aliases: every FFT bin away from the harmonic series (folded products land in between).
        constexpr int order = 14, size = 1 << order;
        std::vector<float> spectrum (2 * size, 0.0f);
        for (int n = 0; n < size; ++n)
            spectrum[(size_t) n] = out[(size_t) n] * (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * n / size));
        juce::dsp::FFT (order).performFrequencyOnlyForwardTransform (spectrum.data());
        double fundBin = 0.0;
        for (int b = 1; b < size / 2; ++b)
        {
            const double f = b * sr / size, nearest = std::round (f / hz) * hz;
            const double e = (double) spectrum[(size_t) b] * spectrum[(size_t) b];
            if (std::abs (f - hz) < 4 * sr / size) fundBin += e;
            else if (std::abs (f - nearest) > 6 * sr / size && f > 30.0) alias += e;
        }
        alias /= fundBin + 1e-30;
        t.harmonics = 10.0 * std::log10 (harm / fundamental + 1e-30);
        t.aliases = 10.0 * std::log10 (alias + 1e-30);
        return t;
    }
} // namespace

void testOverdrive()
{
    std::puts ("Overdrive (Tube Screamer model)");
    OverdriveBlock od (1);
    od.prepare (48000.0, kBlock);
    const float in = 0.25f;   // -12 dBFS peak guitar
    const double inRms = in / std::sqrt (2.0);

    for (float drive : { 0.0f, 50.0f, 100.0f })
    {
        od.setParam (OverdriveBlock::Drive, drive);
        const auto t = analyseOverdrive (od, 220.0, in);
        std::printf ("  drive %3.0f%%: level %+5.1f dB  harmonics %+6.1f dB\n", drive,
                     20.0 * std::log10 (t.rms / inRms), t.harmonics);
        CHECK (std::isfinite (t.rms));
        CHECK (std::abs (20.0 * std::log10 (t.rms / inRms)) < 9.0);   // usable around unity
    }
    od.setParam (OverdriveBlock::Drive, 10.0f);
    const auto low = analyseOverdrive (od, 220.0, in);
    od.setParam (OverdriveBlock::Drive, 100.0f);
    const auto high = analyseOverdrive (od, 220.0, in);
    CHECK (high.harmonics > low.harmonics + 3.0);   // more drive = more distortion

    // Aliasing: a high note at full drive (4x oversampling keeps folded products down).
    const auto alias = analyseOverdrive (od, 2489.0, in);
    std::printf ("  2489 Hz full drive: aliases %.1f dB re fundamental\n", alias.aliases);
    CHECK (alias.aliases < -45.0);

    for (int model = 0; model < 3; ++model)
    {
        od.setParam (OverdriveBlock::Model, (float) model);
        const auto t = analyseOverdrive (od, 110.0, in);
        CHECK (std::isfinite (t.rms) && t.rms > 0.01);
    }

    // Reported latency matches the oversampling filters: a click comes out at that delay.
    od.setParam (OverdriveBlock::Model, 0.0f);
    od.setParam (OverdriveBlock::Drive, 0.0f);
    od.reset();
    std::vector<float> l (256, 0.0f), r (256, 0.0f);
    l[0] = r[0] = 0.02f;
    od.process (l.data(), r.data(), kBlock);
    int peak = 0;
    for (int i = 1; i < kBlock; ++i)
        if (std::abs (l[(size_t) i]) > std::abs (l[(size_t) peak])) peak = i;
    std::printf ("  latency %d samples, impulse peak at %d\n", od.getLatencySamples(), peak);
    CHECK (std::abs (peak - od.getLatencySamples()) <= 3);

    // RT safety and CPU.
    const int before = gAudioAllocations.load();
    const auto start = juce::Time::getMillisecondCounterHiRes();
    for (int b = 0; b < 48000 * 5 / kBlock; ++b)
    {
        for (int i = 0; i < kBlock; ++i)
            l[(size_t) i] = r[(size_t) i] = 0.3f * (float) std::sin (0.05 * (b * kBlock + i));
        AudioScope scope;
        od.process (l.data(), r.data(), kBlock);
    }
    const double cpu = (juce::Time::getMillisecondCounterHiRes() - start) / 5000.0 * 100.0;
    std::printf ("  CPU %.2f %% of one core, audio-thread allocations %d\n", cpu, gAudioAllocations.load() - before);
    CHECK (gAudioAllocations.load() == before);
}

void testFactoryCabs()
{
    std::puts ("KumAmp factory cabinets");
    const auto& cabList = cabs::cabinets();
    CHECK (cabList.size() >= 10);
    std::vector<std::vector<float>> rendered;
    for (int c = 0; c < (int) cabList.size(); ++c)
        for (int m = 0; m < (int) cabs::mics().size(); ++m)
        {
            const auto path = cabs::pathFor (c, m);
            int pc = -1, pm = -1;
            CHECK (cabs::parsePath (path, pc, pm) && pc == c && pm == m);
            CHECK (! cabs::displayName (path).empty());
            const auto ir = cabs::render (c, m);
            CHECK (ir.size() > 2000);
            float peak = 0.0f;
            bool finite = true;
            for (float x : ir) { peak = std::max (peak, std::abs (x)); finite = finite && std::isfinite (x); }
            CHECK (finite);
            CHECK_NEAR (peak, 0.5f, 1e-4);
            for (const auto& other : rendered)
                CHECK (other != ir);   // every option is a different IR
            rendered.push_back (ir);
        }
    CHECK (cabs::render (0, 0) == cabs::render (0, 0));   // deterministic
    int c, m;
    CHECK (! cabs::parsePath ("kumamp:cab/nope/din-centro", c, m));
    CHECK (! cabs::isFactoryPath ("/Users/x/cab.wav"));
    CHECK (cabs::pathFor ("4x12-moderno", "nope").empty());

    // Through the IR loader: the convolution kicks in and the status names the cab.
    IrLoaderBlock block (1);
    block.prepare (48000.0, kBlock);
    const auto path = cabs::pathFor ("4x12-moderno", "din-centro");
    CHECK (block.loadFile (0, path));
    CHECK (block.getFilePath (0) == path);
    std::vector<float> l (kBlock), r (kBlock);
    bool filtered = false;
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 10000.0;
    while (! filtered && juce::Time::getMillisecondCounterHiRes() < deadline)
    {
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        l[0] = r[0] = 1.0f;
        block.process (l.data(), r.data(), kBlock);
        filtered = std::abs (l[0] - 1.0f) > 0.05f;
        juce::Thread::sleep (5);
    }
    std::printf ("  status: %s\n", block.getStatusText().c_str());
    CHECK (filtered);
    CHECK (juce::String (block.getStatusText()).contains ("4x12"));
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

// Minimal processor so PresetManager gets a real APVTS with the global parameter ids.
struct PresetTestProcessor final : public juce::AudioProcessor
{
    PresetTestProcessor() : parameters (*this, nullptr, "Params", makeLayout()) {}
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
    {
        using namespace juce;
        AudioProcessorValueTreeState::ParameterLayout layout;
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "gate_on", 1 }, "Gate", true));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "gate_threshold", 1 }, "Thr", NormalisableRange<float> { -90.0f, 0.0f, 0.5f }, -60.0f));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "gate_release", 1 }, "Rel", NormalisableRange<float> { 5.0f, 1000.0f, 1.0f }, 120.0f));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "pitch_on", 1 }, "Pitch", false));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "pitch_semitones", 1 }, "St", NormalisableRange<float> { -12.0f, 12.0f, 1.0f }, 0.0f));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "pitch_instrument", 1 }, "Instr", StringArray { "Guitarra", "Bajo" }, 0));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "output_gain", 1 }, "Out", NormalisableRange<float> { -24.0f, 12.0f, 0.1f }, 0.0f));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "input_gain", 1 }, "In", NormalisableRange<float> { -24.0f, 24.0f, 0.1f }, 0.0f));
        return layout;
    }
    const juce::String getName() const override { return "test"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
    juce::AudioProcessorValueTreeState parameters;
};

void testDefaultPresetDryEffects()
{
    std::puts ("default preset: delay and reverb start bypassed in every scene");
    GridEngine engine;
    engine.prepare (48000.0, kBlock);
    engine.loadDefaultPreset();
    std::vector<StoreId> timeFx;
    for (const auto& node : engine.getModel().getNodes())
        if (auto s = engine.getStore (node.store))
            if (s->getType() == BlockType::DigitalDelay || s->getType() == BlockType::PlateReverb)
            {
                CHECK (s->isBypassed());
                timeFx.push_back (s->getId());
            }
    CHECK (timeFx.size() == 2);

    const auto state = engine.saveState();
    int snapshots = 0;
    for (const auto& sc : state.getChildWithName ("Scenes"))
        for (const auto& sn : sc)
            if (std::find (timeFx.begin(), timeFx.end(), (StoreId) (int) sn.getProperty ("store")) != timeFx.end())
            {
                CHECK ((bool) sn.getProperty ("bypass"));
                ++snapshots;
            }
    CHECK (snapshots == 8);   // 2 blocks x 4 scenes

    // Overdrive right before the amp, off in every scene; the cab starts on a KumAmp 4x12.
    StoreId driveStore = 0;
    for (const auto& node : engine.getModel().getNodes())
        if (auto s = engine.getStore (node.store))
        {
            if (s->getType() == BlockType::Overdrive)
            {
                driveStore = s->getId();
                CHECK (s->isBypassed());
            }
            if (s->getType() == BlockType::IrLoader)
                if (auto* ir = dynamic_cast<DSPBlock*> (s.get()))
                {
                    CHECK (cabs::isFactoryPath (ir->getFilePath (0)));
                    CHECK (s->isBypassed());   // ready, but off until switched on
                }
            if (s->getType() == BlockType::NamAmp)
                for (const auto& in : node.inputs)
                    if (const auto* source = engine.getModel().findNode (in.node))
                        CHECK (source->store == driveStore && driveStore != 0);
        }
    CHECK (driveStore != 0);
    for (const auto& sc : state.getChildWithName ("Scenes"))
        for (const auto& sn : sc)
            if ((StoreId) (int) sn.getProperty ("store") == driveStore)
                CHECK ((bool) sn.getProperty ("bypass"));

    // Guitar synth first in the chain (clean guitar for its note detection), set up as an
    // instrument layer and off in every scene.
    StoreId synthStore = 0;
    for (const auto& node : engine.getModel().getNodes())
        if (auto s = engine.getStore (node.store))
            if (s->getType() == BlockType::GuitarSynth)
            {
                synthStore = s->getId();
                CHECK (s->isBypassed());
                CHECK (std::lround (s->getParam (GuitarSynthBlock::Mode)) == (long) GuitarSynthBlock::Voice::Instrument);
                for (const auto& in : node.inputs)
                    if (const auto* source = engine.getModel().findNode (in.node))
                        CHECK (source->kind == NodeKind::Input);
            }
    CHECK (synthStore != 0);
    int synthSnapshots = 0;
    for (const auto& sc : state.getChildWithName ("Scenes"))
        for (const auto& sn : sc)
            if ((StoreId) (int) sn.getProperty ("store") == synthStore)
            {
                CHECK ((bool) sn.getProperty ("bypass"));
                ++synthSnapshots;
            }
    CHECK (synthSnapshots == 4);
}

namespace sampler_test
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;
size_t seconds (double s) { return (size_t) (s * kRate); }

struct BlockRig
{
    std::shared_ptr<ParamStore> store;
    DSPBlock* block = nullptr;
    explicit BlockRig (BlockType type)
    {
        store = createStore (type, 1);
        block = dynamic_cast<DSPBlock*> (store.get());
        block->prepare (kRate, kBlock);
    }
    void set (int param, float value) { store->setParam (param, value); }
    void run (std::vector<float>& l, std::vector<float>& r)
    {
        for (size_t off = 0; off < l.size(); off += kBlock)
        {
            const int n = (int) std::min<size_t> (kBlock, l.size() - off);
            block->process (l.data() + off, r.data() + off, n);
            block->collectGarbage();
        }
    }
};

std::vector<float> sine (double hz, double length, float amplitude = 0.5f)
{
    std::vector<float> x (seconds (length));
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = amplitude * (float) std::sin (2.0 * 3.141592653589793 * hz * (double) i / kRate);
    return x;
}

float peakOf (const std::vector<float>& x, size_t from, size_t to)
{
    float peak = 0.0f;
    for (size_t i = from; i < to; ++i)
        peak = std::max (peak, std::abs (x[i]));
    return peak;
}

double toneLevelDb (const std::vector<float>& x, size_t from, size_t to, double hz)
{
    double re = 0.0, im = 0.0;
    const double n = (double) (to - from);
    for (size_t i = from; i < to; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * 3.141592653589793 * (double) (i - from) / n);
        const double phase = 2.0 * 3.141592653589793 * hz * (double) i / kRate;
        re += w * x[i] * std::cos (phase);
        im += w * x[i] * std::sin (phase);
    }
    return 20.0 * std::log10 (4.0 * std::sqrt (re * re + im * im) / n + 1e-12);
}
} // namespace sampler_test

void testSampler()
{
    std::puts ("sampler (load, base note, follows the guitar, trim, play modes, scenes)");
    using S = SamplerBlock;
    using namespace sampler_test;
    registerJuceBlocks();

    // A sound on disk: 0.5 s of silence, then 0.5 s of A3.
    const auto file = juce::File::createTempFile (".wav");
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (file.createOutputStream().release(), sampler_test::kRate, 1, 24, {}, 0));
        std::vector<float> samples ((size_t) seconds (0.5), 0.0f);
        const auto tone = sine (220.0, 0.5, 0.25f);
        samples.insert (samples.end(), tone.begin(), tone.end());
        const float* channels[] { samples.data() };
        CHECK (writer->writeFromFloatArrays (channels, 1, (int) samples.size()));
    }

    const auto waitForSound = [] (BlockRig& rig, S& sampler)
    {
        for (int i = 0; i < 2000 && sampler.getSound() == nullptr; ++i)
            juce::Thread::sleep (2);
        std::vector<float> l (256, 0.0f), r (256, 0.0f);
        rig.run (l, r);   // the audio thread takes it
    };

    // Nothing loaded: the guitar goes through untouched.
    {
        BlockRig rig (BlockType::Sampler);
        auto l = sine (165.0, 0.1), r = l;
        const auto dry = l;
        rig.run (l, r);
        CHECK (l == dry);
    }

    BlockRig rig (BlockType::Sampler);
    auto* sampler = dynamic_cast<S*> (rig.block);
    CHECK (sampler->loadFile (0, file.getFullPathName().toStdString()));
    waitForSound (rig, *sampler);
    const auto sound = sampler->getSound();
    CHECK (sound != nullptr);
    CHECK (std::abs (sound->seconds() - 1.0) < 0.001);

    // "Quitar silencio" finds the tone, and the base note comes from it.
    sampler->trimSilence();
    CHECK (std::abs (rig.block->getParam (S::Start) - 50.0f) < 0.5f);
    CHECK (rig.block->getParam (S::End) > 99.0f);
    for (int i = 0; i < 2000 && sampler->getDetectedRoot() < 0.0f; ++i)
        juce::Thread::sleep (2);
    CHECK (std::abs (sampler->getDetectedRoot() - 57.0f) < 0.2f);
    CHECK (sampler->getRootText() == "A3");

    // E3 on the guitar plays the A3 sound at E3.
    {
        auto l = sine (164.81, 0.4), r = l;
        rig.run (l, r);
        const double played = toneLevelDb (l, seconds (0.1), seconds (0.3), 164.81);
        const double original = toneLevelDb (l, seconds (0.1), seconds (0.3), 220.0);
        CHECK (played > -30.0);
        CHECK (played > original + 15.0);
    }

    // Sostener: it stops with the note, well before the sound ends.
    {
        std::vector<float> l ((size_t) seconds (0.4), 0.0f), r = l;
        rig.block->setParam (S::ReleaseMs, 20.0f);
        rig.run (l, r);
        CHECK (peakOf (l, seconds (0.3), seconds (0.4)) < 1.0e-3f);
    }

    // Una vez plays on after the note, up to the end of the trim, and then stops.
    {
        BlockRig once (BlockType::Sampler);
        auto* block = dynamic_cast<S*> (once.block);
        once.set (S::Playback, (float) S::Play::Once);
        once.set (S::Start, 50.0f);
        block->loadFile (0, file.getFullPathName().toStdString());
        waitForSound (once, *block);
        for (int i = 0; i < 2000 && block->getDetectedRoot() < 0.0f; ++i)
            juce::Thread::sleep (2);

        auto l = sine (220.0, 0.1);
        l.resize ((size_t) seconds (1.2), 0.0f);
        auto r = l;
        once.run (l, r);
        CHECK (peakOf (l, seconds (0.3), seconds (0.45)) > 0.1f);    // the guitar has stopped: still the sound
        CHECK (peakOf (l, seconds (0.8), seconds (1.2)) < 1.0e-3f);  // 0.5 s of sound, done
    }

    // Loop keeps going past the end of the sound, as long as the note rings.
    {
        BlockRig loop (BlockType::Sampler);
        auto* block = dynamic_cast<S*> (loop.block);
        loop.set (S::Playback, (float) S::Play::Loop);
        loop.set (S::Start, 50.0f);
        block->loadFile (0, file.getFullPathName().toStdString());
        waitForSound (loop, *block);
        for (int i = 0; i < 2000 && block->getDetectedRoot() < 0.0f; ++i)
            juce::Thread::sleep (2);

        auto l = sine (220.0, 2.0), r = l;
        loop.run (l, r);
        CHECK (peakOf (l, seconds (1.5), seconds (1.95)) > 0.1f);
        CHECK (toneLevelDb (l, seconds (1.0), seconds (1.9), 220.0) > -30.0);
    }

    // Chords: one voice per note.
    {
        BlockRig chords (BlockType::Sampler);
        auto* block = dynamic_cast<S*> (chords.block);
        chords.set (S::Notes, 1.0f);
        chords.set (S::Start, 50.0f);
        block->loadFile (0, file.getFullPathName().toStdString());
        waitForSound (chords, *block);
        for (int i = 0; i < 2000 && block->getDetectedRoot() < 0.0f; ++i)
            juce::Thread::sleep (2);

        auto l = sine (196.0, 0.5, 0.25f);
        const auto second = sine (293.66, 0.5, 0.25f);
        for (size_t i = 0; i < l.size(); ++i)
            l[i] += second[i];
        auto r = l;
        chords.run (l, r);
        CHECK (toneLevelDb (l, seconds (0.2), seconds (0.45), 196.0) > -35.0);
        CHECK (toneLevelDb (l, seconds (0.2), seconds (0.45), 293.66) > -35.0);
    }

    // A guitar too quiet for the detectors: silent until the sensitivity goes up.
    {
        const auto weak = [&] (float gain)
        {
            std::atomic<float> detection { gain };
            rig.block->reset();
            rig.block->setDetectionSource (&detection);
            auto l = sine (164.81, 0.4, 0.0008f), r = l;
            rig.run (l, r);
            rig.block->setDetectionSource (nullptr);
            return peakOf (l, seconds (0.1), seconds (0.35));
        };
        CHECK (weak (1.0f) < 1.0e-3f);
        CHECK (weak (16.0f) > 0.02f);
    }

    // Nothing allocates on the audio path while voices play.
    {
        auto l = sine (164.81, 0.5), r = l;
        gAudioAllocations = 0;
        gTrackAllocations = true;
        rig.run (l, r);
        gTrackAllocations = false;
        CHECK (gAudioAllocations.load() == 0);
    }

    // The trim belongs to the sound, not to the scene.
    CHECK (! rig.block->isSceneParam (S::Start));
    CHECK (! rig.block->isSceneParam (S::RootNote));
    CHECK (rig.block->isSceneParam (S::Mix));

    // Another sound resets the trim.
    rig.block->setParam (S::Start, 30.0f);
    sampler->loadFile (0, file.getFullPathName().toStdString() + "x");
    CHECK (exactlyEqual (rig.block->getParam (S::Start), 0.0f));

    file.deleteFile();
}

void testReplaceBlock()
{
    std::puts ("replace block (Chorus -> Phaser in place, scenes, undo)");
    GridEngine engine;
    engine.prepare (48000.0, 256);
    engine.loadDefaultPreset();

    NodeId chorusNode = kInvalidNode;
    StoreId chorusStore = kInvalidStore;
    std::vector<PortRef> inputs;
    for (const auto& node : engine.getModel().getNodes())
        if (auto s = engine.getStore (node.store); s != nullptr && s->getType() == BlockType::Chorus)
        {
            chorusNode = node.id;
            chorusStore = s->getId();
            inputs = node.inputs;
        }
    CHECK (chorusNode != kInvalidNode);
    if (chorusNode == kInvalidNode)
        return;
    const auto bypassPerScene = [&engine] (StoreId id)
    {
        std::vector<bool> bypass;
        for (const auto& sc : engine.saveState().getChildWithName ("Scenes"))
            for (const auto& sn : sc)
                if ((StoreId) (int) sn.getProperty ("store") == id)
                    bypass.push_back ((bool) sn.getProperty ("bypass"));
        return bypass;
    };
    const auto chorusScenes = bypassPerScene (chorusStore);
    const bool chorusBypassed = engine.getStore (chorusStore)->isBypassed();

    CHECK (engine.replaceBlock (chorusNode, BlockType::Phaser));
    const auto* node = engine.getModel().findNode (chorusNode);
    CHECK (node != nullptr);
    if (node == nullptr)
        return;
    auto phaser = engine.getStore (node->store);
    CHECK (phaser != nullptr && phaser->getType() == BlockType::Phaser);
    CHECK (node->inputs == inputs);                       // same place in the chain
    CHECK (phaser->isBypassed() == chorusBypassed);
    CHECK (bypassPerScene (phaser->getId()) == chorusScenes);   // on / off follows it in every scene
    CHECK (chorusScenes.size() == 4);

    CHECK (engine.undo());
    const auto* back = engine.getModel().findNode (chorusNode);
    CHECK (back != nullptr && engine.getStore (back->store) != nullptr
           && engine.getStore (back->store)->getType() == BlockType::Chorus);
}

void renderField (FieldSession& field, std::vector<float>& left, std::vector<float>& right, double seconds, float gain = 0.25f)
{
    juce::AudioBuffer<float> buffer (2, kBlock);
    const int blocks = (int) (seconds * 48000.0 / kBlock);
    double phase = 0.0;
    left.clear(); right.clear();
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            phase += 220.0 / 48000.0;
            const float x = gain * std::sin ((float) phase * 6.2831853f) * std::exp (-3.0f * std::fmod ((float) (b * kBlock + i) / 48000.0f, 0.5f));
            buffer.setSample (0, i, x);
            buffer.setSample (1, i, x);
        }
        field.process (buffer.getArrayOfWritePointers(), 2, kBlock, 120.0, 1.0f);
        left.insert (left.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + kBlock);
        right.insert (right.end(), buffer.getReadPointer (1), buffer.getReadPointer (1) + kBlock);
    }
}

void testFieldSession()
{
    std::puts ("field session: independent of the grid, sounds like it on arrival, fusion is signal, tones come back");
    GridEngine grid;
    grid.prepare (48000.0, kBlock);
    grid.loadDefaultPreset();

    const auto storeOf = [] (GridEngine& engine, BlockType type) -> std::shared_ptr<ParamStore>
    {
        for (const auto& node : engine.getModel().getNodes())
            if (auto s = engine.getStore (node.store); s != nullptr && s->getType() == type)
                return s;
        return nullptr;
    };
    const auto blobOf = [] (FieldSession& f, BlockType type)
    {
        for (int i = 0; i < (int) f.getBlobs().size(); ++i)
            if (f.getBlobs()[(size_t) i].type == type)
                return i;
        return -1;
    };
    const auto snapshot = [] (GridEngine& engine)
    {
        std::vector<float> values;
        for (const auto& node : engine.getModel().getNodes())
            if (auto s = engine.getStore (node.store))
            {
                values.push_back (s->isBypassed() ? 1.0f : 0.0f);
                for (int p = 0; p < s->getNumParams(); ++p)
                    values.push_back (s->getParam (p));
            }
        return values;
    };

    // Delay and reverb on, in the grid's own values: what the field copies.
    storeOf (grid, BlockType::DigitalDelay)->setBypassed (false);
    storeOf (grid, BlockType::PlateReverb)->setBypassed (false);
    grid.storeScene (0);

    FieldSession field (grid);
    const auto toneFolder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("kumamp_field_test");
    toneFolder.deleteRecursively();
    field.setToneFolder (toneFolder);
    field.prepare (48000.0, kBlock);
    field.syncWithGrid();
    const auto gridBefore = snapshot (grid);

    // Same elements, nothing fused on arrival, and it sounds like the grid.
    int blocks = 0;
    for (const auto& node : grid.getModel().getNodes())
        blocks += node.kind == NodeKind::Block ? 1 : 0;
    CHECK ((int) field.getBlobs().size() == blocks);
    for (int a = 0; a < blocks; ++a)
        for (int b = a + 1; b < blocks; ++b)
            CHECK (juce::exactlyEqual (field.getOverlap (a, b), 0.0f));

    std::vector<float> fieldL, fieldR, gridL, gridR;
    renderField (field, fieldL, fieldR, 1.0);
    {
        juce::AudioBuffer<float> buffer (2, kBlock);
        double phase = 0.0;
        for (int b = 0; b < (int) (1.0 * 48000.0 / kBlock); ++b)
        {
            for (int i = 0; i < kBlock; ++i)
            {
                phase += 220.0 / 48000.0;
                const float x = 0.25f * std::sin ((float) phase * 6.2831853f) * std::exp (-3.0f * std::fmod ((float) (b * kBlock + i) / 48000.0f, 0.5f));
                buffer.setSample (0, i, x);
                buffer.setSample (1, i, x);
            }
            grid.process (buffer.getArrayOfReadPointers(), 2, buffer.getArrayOfWritePointers(), 2, kBlock);
            gridL.insert (gridL.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + kBlock);
        }
    }
    double difference = 0.0, energy = 0.0;
    for (size_t i = std::min (fieldL.size(), gridL.size()) / 2; i < std::min (fieldL.size(), gridL.size()); ++i)   // past the grid's parameter ramps
    {
        difference += (fieldL[i] - gridL[i]) * (fieldL[i] - gridL[i]);
        energy += gridL[i] * gridL[i];
    }
    std::printf ("  field vs grid on arrival: %.1f dB of difference\n", 10.0 * std::log10 (difference / std::max (energy, 1e-12) + 1e-12));
    CHECK (difference < energy * 0.01);

    // Moving a form changes several parameters of the field's block and nothing of the grid.
    const int delay = blobOf (field, BlockType::DigitalDelay), reverb = blobOf (field, BlockType::PlateReverb);
    CHECK (delay >= 0 && reverb >= 0);
    const auto fieldDelay = storeOf (field.getEngine(), BlockType::DigitalDelay);
    const float timeBefore = fieldDelay->getParam (DigitalDelayBlock::TimeMs);
    const float feedbackBefore = fieldDelay->getParam (DigitalDelayBlock::Feedback);
    const auto home = field.getBlobs()[(size_t) delay];
    field.setBlobPosition (delay, home.x - 0.04f, home.y + 0.25f);
    renderField (field, fieldL, fieldR, 0.05);
    CHECK (! juce::approximatelyEqual (fieldDelay->getParam (DigitalDelayBlock::TimeMs), timeBefore));
    CHECK (! juce::approximatelyEqual (fieldDelay->getParam (DigitalDelayBlock::Feedback), feedbackBefore));
    CHECK (snapshot (grid) == gridBefore);

    // Stretching makes it move by itself; the grid still does not.
    field.setBlobStretch (delay, 1.0f, 4.0f);
    float low = 1.0e9f, high = -1.0e9f;
    for (int i = 0; i < 40; ++i)
    {
        renderField (field, fieldL, fieldR, 0.05);
        low = std::min (low, fieldDelay->getParam (DigitalDelayBlock::Feedback));
        high = std::max (high, fieldDelay->getParam (DigitalDelayBlock::Feedback));
    }
    CHECK (high - low > 0.5f);
    CHECK (snapshot (grid) == gridBefore);
    field.setBlobStretch (delay, 0.0f, 0.0f);
    field.setBlobPosition (delay, home.x, home.y);

    // Fusing: real sends both ways, a different sound, and bounded even at full feedback.
    std::vector<float> apartL, apartR, fusedL, fusedR;
    renderField (field, apartL, apartR, 2.0);
    const auto& r = field.getBlobs()[(size_t) reverb];
    const auto delayStore = field.getBlobs()[(size_t) delay].store, reverbStore = r.store;
    CHECK (juce::exactlyEqual (field.getBus().getGain (delayStore, reverbStore), 0.0f));
    field.setBlobPosition (reverb, field.getBlobs()[(size_t) delay].x, field.getBlobs()[(size_t) delay].y);
    field.setFusion (1.0f);
    CHECK (field.getOverlap (delay, reverb) > 0.9f);
    CHECK (field.getBus().getGain (delayStore, reverbStore) > 0.3f && field.getBus().getGain (reverbStore, delayStore) > 0.3f);
    renderField (field, fusedL, fusedR, 2.0);
    double apartEnergy = 0.0, fusedDiff = 0.0;
    float peak = 0.0f;
    for (size_t i = fusedL.size() / 2; i < fusedL.size(); ++i)
    {
        apartEnergy += apartL[i] * apartL[i];
        fusedDiff += (fusedL[i] - apartL[i]) * (fusedL[i] - apartL[i]);
        peak = std::max (peak, std::abs (fusedL[i]));
        CHECK (std::isfinite (fusedL[i]));
    }
    std::printf ("  fused vs apart: %.1f dB of difference, peak %.2f\n", 10.0 * std::log10 (fusedDiff / std::max (apartEnergy, 1e-12) + 1e-12), peak);
    CHECK (fusedDiff > apartEnergy * 0.05);
    CHECK (peak <= 1.0f);

    // Worst case: everything on, everything stacked, full stretch, loud input, for 5 s.
    for (int b = 0; b < (int) field.getBlobs().size(); ++b)
    {
        if (! field.getBlobs()[(size_t) b].on) field.toggleBlob (b);
        field.setBlobPosition (b, 0.5f, 0.5f);
        field.setBlobStretch (b, 1.0f, 5.0f);
    }
    field.setTide (1.0f);
    renderField (field, fusedL, fusedR, 5.0, 0.9f);
    peak = 0.0f;
    bool finite = true;
    for (const auto v : fusedL) { peak = std::max (peak, std::abs (v)); finite = finite && std::isfinite (v); }
    std::printf ("  everything fused, 5 s: peak %.2f\n", peak);
    CHECK (finite && peak <= 1.0f);
    CHECK (snapshot (grid) == gridBefore);

    // Tones: save, wander off, come back; blend halfway; mutate really changes it.
    field.resetFromGrid();
    const int reverb2 = blobOf (field, BlockType::PlateReverb);
    field.setBlobPosition (reverb2, 0.2f, 0.3f);
    field.setBlobStretch (reverb2, 0.4f, 1.0f);
    CHECK (field.saveTone());
    field.setBlobPosition (reverb2, 0.8f, 0.7f);
    field.setBlobStretch (reverb2, 0.8f, 1.0f);
    CHECK (field.saveTone());
    CHECK (field.getTones().size() == 2);
    CHECK (field.getTones()[0].name == "Tono 01");
    CHECK (toneFolder.getChildFile ("Tono 02.kumcampo").existsAsFile());

    CHECK (field.loadTone (0) == 0);
    CHECK_NEAR (field.getBlobs()[(size_t) reverb2].x, 0.2, 0.001);
    CHECK_NEAR (field.getBlobs()[(size_t) reverb2].stretch, 0.4, 0.001);
    field.blendTones (0, 1, 0.5f);
    CHECK_NEAR (field.getBlobs()[(size_t) reverb2].x, 0.5, 0.001);
    CHECK_NEAR (field.getBlobs()[(size_t) reverb2].stretch, 0.6, 0.001);
    CHECK_NEAR (field.getTones()[0].data.getChild (reverb2).getProperty ("x"), 0.2, 0.001);   // the saved tones stay as they were

    const auto beforeMutation = field.captureState();
    field.mutate();
    CHECK (! juce::approximatelyEqual ((float) field.getBlobs()[(size_t) reverb2].y, (float) beforeMutation.getChild (reverb2).getProperty ("y")));
    field.applyState (beforeMutation);
    CHECK_NEAR (field.getBlobs()[(size_t) reverb2].x, 0.5, 0.001);

    // A new block in the grid appears in the field; the others keep what they had.
    const auto output = std::find_if (grid.getModel().getNodes().begin(), grid.getModel().getNodes().end(), [] (const GridNode& n) { return n.kind == NodeKind::Output; });
    CHECK (grid.insertBlock (output->inputs.front(), output->id, BlockType::Tremolo, false) != kInvalidNode);
    field.syncWithGrid();
    CHECK ((int) field.getBlobs().size() == blocks + 1);
    CHECK (blobOf (field, BlockType::Tremolo) >= 0);
    CHECK_NEAR (field.getBlobs()[(size_t) blobOf (field, BlockType::PlateReverb)].x, 0.5, 0.001);

    // A sitar brings a second form, its tampura: its own parameters, its own switch, and it fuses.
    {
        const auto outputNode = std::find_if (grid.getModel().getNodes().begin(), grid.getModel().getNodes().end(), [] (const GridNode& n) { return n.kind == NodeKind::Output; });
        CHECK (grid.insertBlock (outputNode->inputs.front(), outputNode->id, BlockType::Sitar, false) != kInvalidNode);
        field.syncWithGrid();
        const auto tanpuraIndex = [&field] (bool wanted)
        {
            for (int i = 0; i < (int) field.getBlobs().size(); ++i)
                if (field.getBlobs()[(size_t) i].type == BlockType::Sitar && field.getBlobs()[(size_t) i].tanpura == wanted)
                    return i;
            return -1;
        };
        const int sitarBlob = tanpuraIndex (false), droneBlob = tanpuraIndex (true);
        CHECK (sitarBlob >= 0 && droneBlob == sitarBlob + 1);
        const auto fieldSitar = storeOf (field.getEngine(), BlockType::Sitar);
        const auto gridSitar = storeOf (grid, BlockType::Sitar);

        // Its switch: only the drone, and it brings the sitar along if it was off.
        if (field.getBlobs()[(size_t) sitarBlob].on)
            field.toggleBlob (sitarBlob);
        CHECK (! field.getBlobs()[(size_t) droneBlob].on);
        field.toggleBlob (droneBlob);
        CHECK (field.getBlobs()[(size_t) sitarBlob].on);
        CHECK (fieldSitar->getParam (SitarBlock::Tanpura) > 0.5f);
        CHECK (gridSitar->getParam (SitarBlock::Tanpura) < 0.5f);

        // It moves the drone's parameters and not the sitar's; the sitar's form the other way round.
        const auto paramsBefore = [&fieldSitar] (int p) { return fieldSitar->getParam (p); };
        const float buzz = paramsBefore (SitarBlock::Buzz), cycle = paramsBefore (SitarBlock::TanpuraMovement);
        const auto dronePlace = field.getBlobs()[(size_t) droneBlob];
        field.setBlobPosition (droneBlob, dronePlace.x + 0.2f, dronePlace.y - 0.3f);
        std::vector<float> silenceL, silenceR;
        renderField (field, silenceL, silenceR, 0.1, 0.0f);
        CHECK (juce::approximatelyEqual (fieldSitar->getParam (SitarBlock::Buzz), buzz));
        CHECK (! juce::approximatelyEqual (fieldSitar->getParam (SitarBlock::TanpuraMovement), cycle));
        field.setBlobPosition (droneBlob, dronePlace.x, dronePlace.y);

        // With nothing played it sounds, and fusing sends it through the other block.
        const int delayBlob = blobOf (field, BlockType::DigitalDelay);
        if (! field.getBlobs()[(size_t) delayBlob].on)
            field.toggleBlob (delayBlob);
        const auto fieldDelay2 = storeOf (field.getEngine(), BlockType::DigitalDelay);
        renderField (field, silenceL, silenceR, 3.0, 0.0f);
        double droneAlone = 0.0;
        for (size_t i = silenceL.size() / 2; i < silenceL.size(); ++i)
            droneAlone += silenceL[i] * silenceL[i];
        CHECK (droneAlone > 1.0e-3);
        CHECK (juce::exactlyEqual (field.getBus().getGain (FusionBus::kDirectSlot, fieldDelay2->getId()), 0.0f));

        const auto delayPlace = field.getBlobs()[(size_t) delayBlob];
        field.setBlobPosition (droneBlob, delayPlace.x, delayPlace.y);
        CHECK (field.getBus().getGain (FusionBus::kDirectSlot, fieldDelay2->getId()) > 0.1f);
        std::vector<float> fusedDroneL, fusedDroneR;
        renderField (field, fusedDroneL, fusedDroneR, 3.0, 0.0f);
        double droneFused = 0.0;
        float dronePeak = 0.0f;
        for (size_t i = fusedDroneL.size() / 2; i < fusedDroneL.size(); ++i)
        {
            droneFused += fusedDroneL[i] * fusedDroneL[i];
            dronePeak = std::max (dronePeak, std::abs (fusedDroneL[i]));
        }
        std::printf ("  tampura through the delay: %+.1f dB compared to alone, peak %.2f\n", 10.0 * std::log10 (droneFused / droneAlone), dronePeak);
        CHECK (std::abs (10.0 * std::log10 (droneFused / droneAlone)) > 0.3);
        CHECK (dronePeak <= 1.0f);

        // A tone keeps the two forms apart; pushing to the grid does not change how the drone leaves it.
        const auto saved2 = field.captureState();
        field.setBlobPosition (droneBlob, 0.3f, 0.3f);
        field.applyState (saved2);
        CHECK_NEAR (field.getBlobs()[(size_t) droneBlob].x, delayPlace.x, 0.001);
        CHECK (field.getBlobs()[(size_t) droneBlob].tanpura && ! field.getBlobs()[(size_t) sitarBlob].tanpura);

        gridSitar->setParam (SitarBlock::TanpuraOutput, 1.0f);
        const int pushed = field.pushToGrid();
        CHECK (pushed == blocks + 2);
        CHECK (gridSitar->getParam (SitarBlock::Tanpura) > 0.5f);
        CHECK (gridSitar->getParam (SitarBlock::TanpuraOutput) > 0.5f);
        gridSitar->setParam (SitarBlock::Tanpura, 0.0f);
        field.setBlobPosition (droneBlob, dronePlace.x, dronePlace.y);
        if (field.getBlobs()[(size_t) droneBlob].on) field.toggleBlob (droneBlob);
        blocks += 1;
    }

    // Saved with the project.
    const auto saved = field.saveState();
    FieldSession reopened (grid);
    reopened.setToneFolder (toneFolder);
    reopened.prepare (48000.0, kBlock);
    reopened.loadState (saved);
    reopened.syncWithGrid();
    CHECK_NEAR (reopened.getBlobs()[(size_t) blobOf (reopened, BlockType::PlateReverb)].x, 0.5, 0.001);

    // Only "pasar al Grid" writes to the grid.
    const auto gridBeforePush = snapshot (grid);
    field.setBlobPosition (reverb2, 0.1f, 0.1f);
    CHECK (snapshot (grid) == gridBeforePush);
    CHECK (field.pushToGrid() == blocks + 1);
    CHECK (snapshot (grid) != gridBeforePush);

    CHECK (field.removeTone (0));
    CHECK (field.getTones().size() == 1);
    toneFolder.deleteRecursively();
}

void testSimpView()
{
    std::puts ("simp view: every block is explained, one knob, the grid's own values");

    // Every block the palette offers says what it does, and its knob exists.
    for (const auto& group : theme::paletteGroups())
        for (const auto type : group.types)
        {
            const auto info = simpInfoFor (type);
            CHECK (juce::String (juce::CharPointer_UTF8 (info.description)).isNotEmpty());
            if (info.paramId != nullptr)
            {
                const auto store = kumamp::createStore (type, 1);
                CHECK (store != nullptr && store->findParam (info.paramId) >= 0);
            }
        }

    PresetTestProcessor processor;
    GridEngine engine;
    engine.prepare (48000.0, kBlock);
    engine.loadDefaultPreset();
    SimpView view (engine, processor.parameters);
    view.setBounds (0, 0, 1500, 796);

    int blocks = 0;
    for (const auto& node : engine.getModel().getNodes())
        blocks += node.kind == NodeKind::Block ? 1 : 0;
    CHECK (view.getNumCards() == blocks);

    // The card writes to the grid's store, and follows it (scenes, the block panel).
    int card = -1;
    for (int i = 0; i < view.getNumCards(); ++i)
        if (view.getCardStore (i)->getType() == BlockType::DigitalDelay)
            card = i;
    CHECK (card >= 0);
    auto* store = view.getCardStore (card);
    const int mix = store->findParam ("mix");
    view.setCardValue (card, 77.0f);
    CHECK_NEAR (store->getParam (mix), 77.0, 0.01);
    store->setParam (mix, 12.0f);
    view.refresh();
    CHECK_NEAR (view.getCardKnobValue (card), 12.0, 0.01);
    const bool wasBypassed = store->isBypassed();
    view.toggleCard (card);
    CHECK (store->isBypassed() != wasBypassed);

    // The icons: each preset is written once, loads, and lights its icon.
    const auto presetFolder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("kumamp_simp_test");
    presetFolder.deleteRecursively();
    PresetManager manager (engine, processor.parameters);
    manager.setFolder (presetFolder);
    view.presetNameSource = [&manager] { return manager.getCurrentName(); };
    int chosen = -1;
    view.onPresetChosen = [&chosen] (int preset) { chosen = preset; };
    CHECK (factorypresets::getNumSimpPresets() == 6);
    CHECK (view.getActivePreset() == -1);
    const auto hasType = [&engine] (BlockType type)
    {
        for (const auto& node : engine.getModel().getNodes())
            if (auto s = engine.getStore (node.store); s != nullptr && s->getType() == type)
                return true;
        return false;
    };
    for (int i = 0; i < factorypresets::getNumSimpPresets(); ++i)
    {
        const auto file = factorypresets::writeSimpPreset (presetFolder, i);
        CHECK (file.existsAsFile());
        CHECK (manager.load (file).wasOk());
        view.choosePreset (i);
        CHECK (chosen == i);
        CHECK (view.getActivePreset() == i);
        CHECK (view.getNumCards() >= 2);
        CHECK (hasType (BlockType::NamAmp) == factorypresets::getSimpModelFile (i).isNotEmpty());
    }
    CHECK (hasType (BlockType::Sitar));   // the last one

    // Yours once written: a click does not overwrite your changes.
    const auto clean = factorypresets::writeSimpPreset (presetFolder, 0);
    CHECK (clean.replaceWithText ("mine"));
    CHECK (factorypresets::writeSimpPreset (presetFolder, 0).loadFileAsString() == "mine");
    presetFolder.deleteRecursively();
    engine.loadDefaultPreset();
    view.refresh();
    blocks = 0;
    for (const auto& node : engine.getModel().getNodes())
        blocks += node.kind == NodeKind::Block ? 1 : 0;

    // The sitar's card switches the tampura, on the block's own parameter.
    {
        const auto outputNode = std::find_if (engine.getModel().getNodes().begin(), engine.getModel().getNodes().end(), [] (const GridNode& n) { return n.kind == NodeKind::Output; });
        CHECK (engine.insertBlock (outputNode->inputs.front(), outputNode->id, BlockType::Sitar, false) != kInvalidNode);
        view.refresh();
        int sitarCard = -1;
        for (int i = 0; i < view.getNumCards(); ++i)
            if (view.getCardStore (i)->getType() == BlockType::Sitar)
                sitarCard = i;
        CHECK (sitarCard >= 0);
        auto* sitar = view.getCardStore (sitarCard);
        CHECK (sitar->getParam (SitarBlock::Tanpura) < 0.5f);
        view.toggleCardOption (sitarCard);
        CHECK (sitar->getParam (SitarBlock::Tanpura) > 0.5f);
        sitar->setParam (SitarBlock::Tanpura, 0.0f);
        view.refresh();
        view.toggleCardOption (sitarCard);
        CHECK (sitar->getParam (SitarBlock::Tanpura) > 0.5f);   // it had followed the block back to off
        engine.loadDefaultPreset();
        view.refresh();
    }

    // A new block in the grid gets its card.
    const auto output = std::find_if (engine.getModel().getNodes().begin(), engine.getModel().getNodes().end(), [] (const GridNode& n) { return n.kind == NodeKind::Output; });
    CHECK (engine.insertBlock (output->inputs.front(), output->id, BlockType::Tremolo, false) != kInvalidNode);
    view.refresh();
    CHECK (view.getNumCards() == blocks + 1);
}

void testFactoryPresets()
{
    std::puts ("factory presets: written once, every one loads, models named but not shipped");
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("kumamp_factory_test");
    root.deleteRecursively();

    PresetTestProcessor processor;
    GridEngine engine;
    engine.prepare (48000.0, kBlock);
    engine.loadDefaultPreset();
    PresetManager manager (engine, processor.parameters);
    manager.setFolder (root);

    const int written = factorypresets::install (root);
    const auto all = manager.scan();
    juce::StringArray categories;
    for (const auto& p : all)
        categories.addIfNotAlreadyThere (p.category);
    std::printf ("  %d presets in %d categories\n", written, categories.size());
    CHECK (written == (int) all.size() && written >= 15);
    CHECK (categories.size() >= 5);
    CHECK (factorypresets::install (root) == 0);    // never written twice

    juce::AudioBuffer<float> buffer (2, kBlock);
    for (const auto& info : all)
    {
        CHECK (info.comment.contains ("tone3000.com/tones/"));
        CHECK (manager.load (info.file).wasOk());
        CHECK (engine.getSceneName (1) == "Solo");

        const auto xml = juce::XmlDocument::parse (info.file);
        CHECK (xml != nullptr && xml->toString().contains (".nam"));

        for (int scene = 0; scene < 4; ++scene)
        {
            engine.setRequestedScene (scene);
            float peak = 0.0f;
            for (int b = 0; b < 100; ++b)
            {
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < kBlock; ++i)
                        buffer.setSample (c, i, 0.2f * std::sin ((float) (b * kBlock + i) * 0.03f));
                engine.process (buffer.getArrayOfReadPointers(), 2, buffer.getArrayOfWritePointers(), 2, kBlock);
                peak = std::max (peak, buffer.getMagnitude (0, kBlock));
            }
            CHECK (std::isfinite (peak) && peak > 0.0001f && peak < 8.0f);
        }
    }
    root.deleteRecursively();
}

void testPresets()
{
    std::puts ("presets: save / scan / load (missing model relocated) / trash");
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("kumamp_preset_test");
    root.deleteRecursively();
    const auto models = root.getChildFile ("models");
    models.createDirectory();
    const auto model = models.getChildFile ("my_amp.nam");
    CHECK (kExamples.getChildFile ("lstm.nam").copyFileTo (model));

    PresetTestProcessor processor;
    GridEngine engine;
    engine.prepare (48000.0, kBlock);
    engine.loadDefaultPreset();

    PresetManager manager (engine, processor.parameters);
    manager.setFolder (root.getChildFile ("Presets"));

    auto findType = [&engine] (BlockType type) -> std::shared_ptr<ParamStore>
    {
        for (const auto& node : engine.getModel().getNodes())
            if (auto s = engine.getStore (node.store); s != nullptr && s->getType() == type)
                return s;
        return {};
    };
    auto amp = findType (BlockType::NamAmp);
    auto delay = findType (BlockType::DigitalDelay);
    CHECK (amp != nullptr && delay != nullptr);
    if (amp == nullptr || delay == nullptr) return;
    static_cast<DSPBlock*> (amp.get())->loadFile (0, model.getFullPathName().toStdString());
    delay->setBypassed (false);
    delay->setParam (DigitalDelayBlock::TimeMs, 333.0f);
    auto* threshold = processor.parameters.getParameter ("gate_threshold");
    threshold->setValueNotifyingHost (threshold->convertTo0to1 (-42.0f));

    const auto saved = manager.save (juce::String (juce::CharPointer_UTF8 ("Lead Ca\xc3\xb1\xc3\xb3n")), "Kumo", "Pack Test", "Les Paul, puente");
    CHECK (saved.wasOk());
    const auto presets = manager.scan();
    CHECK (presets.size() == 1);
    if (presets.size() != 1) return;
    CHECK (presets[0].category == "Pack Test");
    CHECK (presets[0].author == "Kumo");
    CHECK (presets[0].name == juce::String (juce::CharPointer_UTF8 ("Lead Ca\xc3\xb1\xc3\xb3n")));
    std::printf ("  saved %s\n", presets[0].file.getFullPathName().toRawUTF8());

    // Share the pack: the model now lives next to the preset, the original path is gone.
    const auto packModel = presets[0].file.getParentDirectory().getChildFile ("nam").getChildFile ("my_amp.nam");
    packModel.getParentDirectory().createDirectory();
    CHECK (model.moveFileTo (packModel));

    engine.loadDefaultPreset();
    threshold->setValueNotifyingHost (threshold->convertTo0to1 (-60.0f));

    CHECK (manager.load (presets[0].file).wasOk());
    CHECK_NEAR (processor.parameters.getRawParameterValue ("gate_threshold")->load(), -42.0, 0.01);
    delay = findType (BlockType::DigitalDelay);
    amp = findType (BlockType::NamAmp);
    CHECK (delay != nullptr && ! delay->isBypassed());
    if (delay != nullptr) CHECK_NEAR (delay->getParam (DigitalDelayBlock::TimeMs), 333.0, 0.01);
    const auto path = amp != nullptr ? juce::String (static_cast<DSPBlock*> (amp.get())->getFilePath (0)) : juce::String();
    std::printf ("  model resolved to %s\n", path.toRawUTF8());
    CHECK (juce::File (path) == packModel);

    CHECK (! manager.remove (packModel));   // only .kumamp files inside the preset folder
    if (juce::SystemStats::getEnvironmentVariable ("CI", {}).isNotEmpty())
    {
        // Moves to the real trash: only on CI, so local runs don't fill the user's trash.
        CHECK (manager.remove (presets[0].file));
        CHECK (manager.scan().empty());
    }
    root.deleteRecursively();
}

void testTone3000Helpers()
{
    std::puts ("TONE3000: PKCE S256, redirect parsing, library folder names");
    // Expected value computed with: printf %s VERIFIER | openssl dgst -sha256 -binary | base64url
    CHECK (t3k::codeChallengeFor ("testverifier1234567890abcdefghijklmnopqrstuv") == "iD5RdzKnlgiKI9e921_tHV9XuAvxKG3TbELKaFkPN28");

    const auto q = t3k::parseQuery ("/callback?code=ab%2Bc&state=xyz&tone_id=4521#frag");
    CHECK (q["code"] == "ab+c");
    CHECK (q["state"] == "xyz");
    CHECK (q["tone_id"] == "4521");
    CHECK (t3k::parseQuery ("canceled=true&state=s")["canceled"] == "true");

    t3k::Tone tone;
    tone.id = 7;
    tone.title = "Plexi 1959: Super Lead / Crunch?";
    tone.creator = "kumo";
    const auto folder = t3k::folderNameFor (tone);
    std::printf ("  folder \"%s\"\n", folder.toRawUTF8());
    CHECK (folder.isNotEmpty() && ! folder.containsAnyOf ("/\\:?*\"<>|"));
    CHECK (folder.endsWith ("kumo"));
    CHECK (juce::String (t3k::Session::publishableKey()).startsWith ("t3k_pub_"));

    // /api/v1/models reply, same shape as the live API (seen in the diagnostics log).
    {
        juce::String reply = "{\"data\":[";
        for (int i = 0; i < 6; ++i)
            reply << (i ? "," : "") << "{\"id\":" << (289460 + i) << ",\"tone_id\":51649,\"user_id\":1,\"created_at\":\"x\","
                  << "\"updated_at\":\"x\",\"name\":\"Fender DRRI | Clean | Mic " << i << "\","
                  << "\"model_url\":\"https://www.tone3000.com/api/v1/models/" << (289460 + i) << "/download/a.nam\","
                  << "\"size\":\"standard\",\"architecture_version\":\"2\"}";
        reply << "],\"page\":1,\"page_size\":100,\"total\":6,\"total_pages\":1}";
        for (int repeat = 0; repeat < 50; ++repeat)   // the old dangling read failed only sometimes
        {
            const auto models = t3k::parseModelList (reply);
            CHECK (models.size() == 6);
            if (models.size() != 6) break;
            CHECK (models[5].id == 289465 && models[5].name.endsWith ("Mic 5") && models[5].url.contains ("/download/"));
        }
        CHECK (t3k::parseModelList ("{\"data\":[]}").isEmpty());
        CHECK (t3k::parseModelList ("not json").isEmpty());
    }

    // tone.json round trip (the amp block's "Variantes" row reads it back).
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("kumamp_t3k_tone");
    dir.deleteRecursively();
    juce::Array<t3k::Model> variants;
    for (int i = 0; i < 3; ++i)
    {
        t3k::Model m;
        m.id = 100 + i;
        m.name = "DRRI | Clean | Mic " + juce::String (i + 1);
        m.size = "standard";
        m.url = "https://example.invalid/never-saved";
        variants.add (m);
    }
    tone.license = "cc-by";
    tone.url = "https://www.tone3000.com/tones/7";
    CHECK (t3k::writeToneFolder (dir, tone, variants));
    t3k::Tone readTone;
    juce::Array<t3k::Model> readVariants;
    CHECK (t3k::readToneFolder (dir, readTone, readVariants));
    CHECK (readTone.id == 7 && readTone.creator == "kumo" && readTone.license == "cc-by");
    CHECK (readVariants.size() == 3 && readVariants[2].id == 102 && readVariants[2].name == variants[2].name);
    CHECK (readVariants[0].url.isEmpty());   // download URLs are never stored
    CHECK (! dir.getChildFile ("tone.json").loadFileAsString().contains ("example.invalid"));
    CHECK (t3k::modelFileName (variants[0]) == "DRRI  Clean  Mic 1.nam" || t3k::modelFileName (variants[0]).endsWith (".nam"));
    CHECK (dir.getChildFile ("tone.txt").loadFileAsString().contains ("cc-by"));
    dir.deleteRecursively();
}

// Live checks against tone3000.com / httpbin.org: only with KUMAMP_NETWORK_TESTS=1.
void testTone3000Network()
{
    if (juce::SystemStats::getEnvironmentVariable ("KUMAMP_NETWORK_TESTS", {}).isEmpty())
        return;
    std::puts ("TONE3000 (network): loopback redirect -> state check -> real token endpoint");

    // Simulates the browser coming back to the loopback receiver with `query`.
    auto runFlow = [] (std::function<juce::String (const juce::StringPairArray& authorizeParams)> makeQuery)
    {
        t3k::Session session;
        juce::URL opened;
        session.openBrowser = [&opened] (const juce::URL& url) { opened = url; return true; };
        juce::String error = "<no result>";
        bool done = false;
        session.selectTone ([&] (juce::Result r, const t3k::Tone&, const juce::Array<t3k::Model>&)
        {
            error = r.wasOk() ? juce::String() : r.getErrorMessage();
            done = true;
        });
        const auto params = t3k::parseQuery (opened.toString (true));
        const auto redirect = params["redirect_uri"] + "?" + makeQuery (params);
        std::thread browser ([redirect]
        {
            int status = 0;
            auto stream = juce::URL (redirect).createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                                      .withConnectionTimeoutMs (5000).withStatusCode (&status));
            if (stream != nullptr) stream->readEntireStreamAsString();
        });
        for (int i = 0; i < 300 && ! done; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        browser.join();
        return error;
    };

    const auto fakeCode = runFlow ([] (const juce::StringPairArray& p) { return "code=not-a-real-code&state=" + p["state"] + "&tone_id=1"; });
    std::printf ("  fake code -> %s\n", fakeCode.toRawUTF8());
    CHECK (fakeCode.contains ("inicio de sesion") && fakeCode.contains ("HTTP 4"));   // reached the real token endpoint

    const auto badState = runFlow ([] (const juce::StringPairArray&) { return juce::String ("code=x&state=forged&tone_id=1"); });
    std::printf ("  forged state -> %s\n", badState.toRawUTF8());
    CHECK (badState.contains ("state"));

    const auto canceled = runFlow ([] (const juce::StringPairArray& p) { return "canceled=true&state=" + p["state"]; });
    std::printf ("  canceled -> %s\n", canceled.toRawUTF8());
    CHECK (canceled.contains ("sin elegir"));

    {
        juce::MemoryBlock echo;
        int echoStatus = 0;
        t3k::fetchAuthorized (juce::URL ("https://httpbin.org/get").withParameter ("tone_id", "51649").withParameter ("page_size", "100"),
                              "tok", echo, echoStatus);
        std::printf ("  query echo: %s\n", echo.toString().replaceCharacters ("\n", " ").substring (0, 300).toRawUTF8());
    }
    std::puts ("TONE3000 (network): storage redirect drops the Authorization header");
    juce::MemoryBlock body;
    int status = 0;
    const auto r = t3k::fetchAuthorized (juce::URL ("https://httpbin.org/redirect-to?url=https%3A%2F%2Fhttpbin.org%2Fheaders"),
                                         "secret-token", body, status);
    std::printf ("  HTTP %d, %d bytes\n", status, (int) body.getSize());
    CHECK (r.wasOk());
    CHECK (body.toString().contains ("\"Host\"") && ! body.toString().contains ("secret-token"));
}

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
    testOverdrive();
    testFactoryCabs();
    testTunerPipeline();
    testTunerSnapshots();
    testGridEditing();
    testDefaultPresetDryEffects();
    testReplaceBlock();
    testSampler();
    testPresets();
    testFactoryPresets();
    testFieldSession();
    testSimpView();
    testTone3000Helpers();
    testTone3000Network();

    std::printf (failures == 0 ? "\nALL INTEGRATION TESTS PASSED\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
