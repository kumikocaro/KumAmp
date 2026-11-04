// Core tests (no JUCE): grid compiler, latency compensation, scenes, bypass and
// the WSOLA pitch shifter. Run: ctest, or ./KumAmpTests

#include "Graph/BlockFactory.h"
#include "Graph/DSPBlock.h"
#include "Graph/RenderPlan.h"
#include "DSP/Pitch/PitchShifterBlock.h"
#include "DSP/Pitch/WsolaPitchShifter.h"
#include "DSP/Routing/RoutingParams.h"
#include "DSP/Utility/VolumeBlock.h"
#include "Scenes/SceneBank.h"
#include "Core/RateBridge.h"
#include "Core/PitchDetector.h"
#include "Core/RealtimePitchTracker.h"
#include "DSP/Midi/GuitarToMidi.h"
#include "DSP/Pitch/CharangoBlock.h"
#include "DSP/Pitch/GuitarSynthBlock.h"
#include "DSP/Pitch/SitarBlock.h"
#include "DSP/Pitch/PolyNoteDetector.h"
#include "DSP/Dynamics/DynamicsBlocks.h"
#include "DSP/Modulation/ModulationBlocks.h"
#include "DSP/Pitch/PreFxBlocks.h"
#include "DSP/Time/DelayBlocks.h"
#include "DSP/Time/ReverbBlocks.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <new>
#include <random>

#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace kumamp;

//== operator new tracking while processing audio ==============================
static thread_local bool gTrackAllocations = false;
static std::atomic<int> gAudioAllocations { 0 };
void* operator new (std::size_t size)
{
    if (gTrackAllocations) ++gAudioAllocations;
    if (void* p = std::malloc (size == 0 ? 1 : size)) return p;
    throw std::bad_alloc();
}
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

static int failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (! (cond)) { ++failures; std::printf ("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                    \
    do {                                                                         \
        const double va = (a), vb = (b);                                         \
        if (std::abs (va - vb) > (tol)) { ++failures; std::printf ("  FAIL %s:%d  %s = %g, expected %g\n", __FILE__, __LINE__, #a, va, vb); } \
    } while (0)

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

/** Test block: pure delay that reports its latency. */
class LatencyBlock final : public DSPBlock
{
public:
    LatencyBlock (StoreId id, int samples) : DSPBlock (id, BlockType::Unknown, nullptr, 0), latency (samples) {}
    int getLatencySamples() const noexcept override { return latency; }

protected:
    void prepareBlock (double, int) override { line.prepare (latency); }
    void resetBlock() noexcept override       { line.reset(); }
    void processBlock (float* l, float* r, int n) noexcept override { line.process (l, r, n); }

private:
    int latency;
    FixedDelay line;
};

struct Rig
{
    std::map<StoreId, std::shared_ptr<ParamStore>> stores;
    StoreId nextId = 1;

    std::shared_ptr<ParamStore> add (std::shared_ptr<ParamStore> s)
    {
        if (auto* b = dynamic_cast<DSPBlock*> (s.get()))
            b->prepare (kRate, kBlock);
        stores[s->getId()] = s;
        return s;
    }
    std::shared_ptr<ParamStore> make (BlockType type) { return add (createStore (type, nextId++)); }

    RenderPlan::CompileResult compile (const GridModel& m)
    {
        return RenderPlan::compile (m, [this] (StoreId id) { auto it = stores.find (id); return it == stores.end() ? nullptr : it->second; }, kBlock);
    }
};

/** Run a stereo signal through a plan block by block (in place). */
void render (RenderPlan& plan, std::vector<float>& l, std::vector<float>& r)
{
    for (size_t offset = 0; offset < l.size(); offset += kBlock)
    {
        const int n = (int) std::min<size_t> (kBlock, l.size() - offset);
        const float* in[2] { l.data() + offset, r.data() + offset };
        float* out[2] { l.data() + offset, r.data() + offset };
        plan.process (in, 2, out, 2, n);
    }
}

double estimateFrequency (const std::vector<float>& x, size_t start, size_t end)
{
    // Positive-going zero crossings with linear interpolation.
    double first = -1.0, last = -1.0;
    int crossings = 0;
    for (size_t i = start + 1; i < end; ++i)
    {
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
        {
            const double t = (double) (i - 1) + x[i - 1] / (double) (x[i - 1] - x[i]);
            if (first < 0.0) first = t; else ++crossings;
            last = t;
        }
    }
    return crossings > 0 ? kRate * crossings / (last - first) : 0.0;
}

void testSerialChainInPlace()
{
    std::puts ("serial chain");
    Rig rig;
    GridModel m;
    const auto in = m.addNode (NodeKind::Input);
    NodeId prev = in;
    std::shared_ptr<ParamStore> first;
    for (int i = 0; i < 5; ++i)
    {
        auto vol = rig.make (BlockType::Volume);
        if (i == 0) { first = vol; vol->setParam (VolumeBlock::LevelDb, -6.0f); }
        static_cast<DSPBlock*> (vol.get())->prepare (kRate, kBlock); // re-sync smoother with new value
        const auto n = m.addNode (NodeKind::Block, vol->getId());
        m.connect ({ prev, 0 }, n);
        prev = n;
    }
    const auto out = m.addNode (NodeKind::Output);
    m.connect ({ prev, 0 }, out);

    auto res = rig.compile (m);
    CHECK (res.plan != nullptr);
    if (! res.plan) return;
    CHECK (res.plan->getNumBuffers() == 1);   // everything processed in place
    CHECK (res.plan->getLatencySamples() == 0);

    std::vector<float> l (1024, 1.0f), r (1024, 1.0f);
    render (*res.plan, l, r);
    CHECK_NEAR (l[1000], std::pow (10.0, -6.0 / 20.0), 1e-4);
}

void testParallelSplitMix()
{
    std::puts ("parallel split / mix");
    Rig rig;
    GridModel m;
    auto split = rig.make (BlockType::Splitter);
    auto mix   = rig.make (BlockType::Mixer);
    auto a     = rig.make (BlockType::Volume);
    auto b     = rig.make (BlockType::Volume);

    const auto in = m.addNode (NodeKind::Input);
    const auto s  = m.addNode (NodeKind::Split, split->getId(), 0, 1, 2);
    const auto na = m.addNode (NodeKind::Block, a->getId());
    const auto nb = m.addNode (NodeKind::Block, b->getId());
    const auto mx = m.addNode (NodeKind::Mix, mix->getId());
    const auto out = m.addNode (NodeKind::Output);
    m.connect ({ in, 0 }, s);
    m.connect ({ s, 0 }, na);
    m.connect ({ s, 1 }, nb);
    m.connect ({ na, 0 }, mx);
    m.connect ({ nb, 0 }, mx);
    m.connect ({ mx, 0 }, out);

    auto res = rig.compile (m);
    CHECK (res.plan != nullptr);
    if (! res.plan) return;

    std::vector<float> l (2048, 1.0f), r (2048, 1.0f);
    render (*res.plan, l, r);
    CHECK_NEAR (l[2000], 2.0, 1e-4);   // A + B at unity

    split->setParam (SplitterParam::branchLevel (1), 0.0f);           // mute branch B (scene-style)
    mix->setParam (MixerParam::inputPan (0), 1.0f);                  // pan A hard right
    std::fill (l.begin(), l.end(), 1.0f);
    std::fill (r.begin(), r.end(), 1.0f);
    render (*res.plan, l, r);
    CHECK_NEAR (l[2000], 0.0, 1e-4);
    CHECK_NEAR (r[2000], 1.0, 1e-4);
}

void testLatencyCompensation()
{
    std::puts ("latency compensation");
    Rig rig;
    GridModel m;
    auto split = rig.make (BlockType::Splitter);
    auto mix   = rig.make (BlockType::Mixer);
    auto lat   = rig.add (std::make_shared<LatencyBlock> (rig.nextId++, 37));

    const auto in  = m.addNode (NodeKind::Input);
    const auto s   = m.addNode (NodeKind::Split, split->getId(), 0, 1, 2);
    const auto nl  = m.addNode (NodeKind::Block, lat->getId());
    const auto mx  = m.addNode (NodeKind::Mix, mix->getId());
    const auto out = m.addNode (NodeKind::Output);
    m.connect ({ in, 0 }, s);
    m.connect ({ s, 0 }, nl);
    m.connect ({ nl, 0 }, mx);
    m.connect ({ s, 1 }, mx);   // dry branch, must be delayed by 37
    m.connect ({ mx, 0 }, out);

    auto res = rig.compile (m);
    CHECK (res.plan != nullptr);
    if (! res.plan) return;
    CHECK (res.plan->getLatencySamples() == 37);

    std::vector<float> l (512, 0.0f), r (512, 0.0f);
    l[10] = r[10] = 1.0f;
    render (*res.plan, l, r);
    CHECK_NEAR (l[47], 2.0, 1e-5);   // both branches coincide
    CHECK_NEAR (l[10], 0.0, 1e-5);
}

void testValidationAndPruning()
{
    std::puts ("validation / pruning");
    Rig rig;
    auto a = rig.make (BlockType::Volume);
    auto b = rig.make (BlockType::Volume);
    auto orphan = rig.make (BlockType::Volume);

    GridModel cyc;
    const auto in = cyc.addNode (NodeKind::Input);
    const auto na = cyc.addNode (NodeKind::Block, a->getId());
    const auto nb = cyc.addNode (NodeKind::Block, b->getId());
    const auto out = cyc.addNode (NodeKind::Output);
    cyc.connect ({ nb, 0 }, na);
    cyc.connect ({ na, 0 }, nb);
    cyc.connect ({ in, 0 }, out);
    std::string error;
    CHECK (! cyc.validate (&error));

    GridModel pruned;
    const auto pin = pruned.addNode (NodeKind::Input);
    const auto pout = pruned.addNode (NodeKind::Output);
    const auto po = pruned.addNode (NodeKind::Block, orphan->getId());
    pruned.connect ({ pin, 0 }, pout);
    pruned.connect ({ pin, 0 }, po);   // dead end: never reaches Output
    auto res = rig.compile (pruned);
    CHECK (res.plan != nullptr);
    CHECK (res.plan && res.plan->findStore (orphan->getId()) == nullptr);
}

void testBypassCrossfade()
{
    std::puts ("bypass");
    VolumeBlock vol (1);
    vol.setParam (VolumeBlock::LevelDb, -60.0f);   // silence when active
    vol.prepare (kRate, kBlock);

    std::vector<float> l (kBlock, 1.0f), r (kBlock, 1.0f);
    vol.process (l.data(), r.data(), kBlock);
    CHECK_NEAR (l.back(), 0.0, 1e-6);

    vol.setBypassed (true);
    for (int i = 0; i < 10; ++i)   // > 10 ms ramp
    {
        std::fill (l.begin(), l.end(), 1.0f);
        std::fill (r.begin(), r.end(), 1.0f);
        vol.process (l.data(), r.data(), kBlock);
    }
    CHECK_NEAR (l.back(), 1.0, 1e-6);
}

void testScenes()
{
    std::puts ("scenes");
    Rig rig;
    auto p = rig.make (BlockType::PitchShifter);
    auto v = rig.make (BlockType::Volume);
    std::vector<const ParamStore*> all { p.get(), v.get() };

    SceneBank bank;
    p->setParam (PitchShifterBlock::Semitones, -2.0f);
    bank.scenes[0] = SceneBank::capture (all);

    p->setParam (PitchShifterBlock::Semitones, 7.0f);
    v->setBypassed (true);
    bank.scenes[1] = SceneBank::capture (all);

    auto find = [&] (StoreId id) -> ParamStore* { auto it = rig.stores.find (id); return it == rig.stores.end() ? nullptr : it->second.get(); };
    SceneBank::apply (bank.scenes[0], find);
    CHECK_NEAR (p->getParam (PitchShifterBlock::Semitones), -2.0, 0.0);
    CHECK (! v->isBypassed());
    SceneBank::apply (bank.scenes[1], find);
    CHECK_NEAR (p->getParam (PitchShifterBlock::Semitones), 7.0, 0.0);
    CHECK (v->isBypassed());
}

void testPitchShifter (float semitones)
{
    std::printf ("pitch shifter %+.0f st\n", semitones);
    WsolaPitchShifter ps;
    ps.prepare (kRate);
    ps.setRatio (std::exp2 (semitones / 12.0f), 0.0f);

    const double f0 = 220.0;
    const size_t n = (size_t) kRate * 2;
    std::vector<float> l (n), r (n);
    for (size_t i = 0; i < n; ++i)
        l[i] = r[i] = (float) (0.5 * std::sin (2.0 * 3.141592653589793 * f0 * (double) i / kRate));

    for (size_t off = 0; off < n; off += kBlock)
    {
        const int len = (int) std::min<size_t> (kBlock, n - off);
        ps.process (l.data() + off, r.data() + off, l.data() + off, r.data() + off, len);
    }

    const double expected = f0 * std::exp2 (semitones / 12.0);
    const double measured = estimateFrequency (l, n / 2, n);
    CHECK_NEAR (measured, expected, expected * 0.01);

    // Amplitude stability: 5 ms RMS windows must not dip (no splice dropouts).
    const size_t w = (size_t) (kRate * 0.005);
    double minRms = 1e9, maxRms = 0.0;
    for (size_t s = n / 2; s + w < n; s += w)
    {
        double acc = 0.0;
        for (size_t i = s; i < s + w; ++i) acc += (double) l[i] * l[i];
        const double rms = std::sqrt (acc / (double) w);
        minRms = std::min (minRms, rms);
        maxRms = std::max (maxRms, rms);
    }
    std::printf ("  f=%.2f Hz (exp %.2f)  rms %.3f..%.3f  delay %.1f smp\n",
                 measured, expected, minRms, maxRms, ps.getCurrentDelaySamples());
    CHECK (minRms > 0.8 * maxRms);
    CHECK (ps.getCurrentDelaySamples() < kRate * 0.030);   // stays low-latency
}

// Bass notes have periods (24 ms for a low E) longer than the guitar-sized
// splice geometry: the shifter must track the period and still splice in phase.
void testPitchShifterBass (double f0, float semitones)
{
    std::printf ("pitch shifter bass %.2f Hz %+.0f st\n", f0, semitones);
    WsolaPitchShifter ps;
    ps.prepare (kRate);
    ps.setInstrument (WsolaPitchShifter::Instrument::Bass);
    ps.setRatio (std::exp2 (semitones / 12.0f), 0.0f);

    const size_t n = (size_t) kRate * 3;
    std::vector<float> l (n), r (n);
    for (size_t i = 0; i < n; ++i)
    {
        double s = 0.0;
        for (int k = 1; k <= 10; ++k)
            s += std::sin (2.0 * 3.141592653589793 * f0 * k * (double) i / kRate + 0.7 * k) / k;
        l[i] = r[i] = (float) (0.25 * s);
    }

    for (size_t off = 0; off < n; off += kBlock)
    {
        const int len = (int) std::min<size_t> (kBlock, n - off);
        ps.process (l.data() + off, r.data() + off, l.data() + off, r.data() + off, len);
    }

    const double expected = f0 * std::exp2 (semitones / 12.0);
    PitchDetector detector;
    detector.prepare (kRate, 8192, 18.0f, 1500.0f);
    double worstCents = 0.0;
    for (size_t s = n / 3; s + 8192 < n; s += 2400)
    {
        const auto res = detector.analyse (l.data() + s);
        worstCents = std::max (worstCents, res.valid ? std::abs (1200.0 * std::log2 (res.frequency / expected)) : 999.0);
    }

    // Envelope over two output periods: splice dropouts / phase cancellations show up as dips.
    const size_t w = (size_t) (2.0 * kRate / expected);
    double minRms = 1e9, maxRms = 0.0;
    for (size_t s = n / 3; s + w < n; s += 240)
    {
        double acc = 0.0;
        for (size_t i = s; i < s + w; ++i) acc += (double) l[i] * l[i];
        const double rms = std::sqrt (acc / (double) w);
        minRms = std::min (minRms, rms);
        maxRms = std::max (maxRms, rms);
    }
    std::printf ("  worst %.2f ct  rms %.3f..%.3f  delay %.0f smp\n",
                 worstCents, minRms, maxRms, ps.getCurrentDelaySamples());
    CHECK (worstCents < 2.0);
    CHECK (minRms > 0.95 * maxRms);
    CHECK (ps.getCurrentDelaySamples() < kRate * 0.060);
}

// Splice noise: share (dB) of the output energy that is NOT at the expected
// (shifted) partials, measured with a DFT at 1 Hz steps up to 2 kHz.
double spliceNoiseDb (const std::vector<float>& y, size_t start, size_t length, const std::vector<double>& partials)
{
    double total = 0.0, onPartials = 0.0;
    std::vector<double> w (length);
    for (size_t i = 0; i < length; ++i)
        w[i] = y[start + i] * (0.5 - 0.5 * std::cos (2.0 * 3.141592653589793 * (double) i / (double) (length - 1)));
    for (double f = 20.0; f < 2000.0; f += 1.0)
    {
        double re = 0.0, im = 0.0;
        const double step = 2.0 * 3.141592653589793 * f / kRate;
        for (size_t i = 0; i < length; ++i)
        {
            re += w[i] * std::cos (step * (double) i);
            im -= w[i] * std::sin (step * (double) i);
        }
        const double e = re * re + im * im;
        total += e;
        for (double p : partials)
            if (std::abs (f - p) <= 4.0) { onPartials += e; break; }
    }
    return 10.0 * std::log10 (std::max (1.0e-12, (total - onPartials) / total));
}

// Chords only repeat at the common period of their notes (24 ms for an open E):
// the adaptive splice must find it, so the chord stays clean, at low latency.
void testPitchShifterGuitarChord (float semitones)
{
    std::printf ("pitch shifter open E chord %+.0f st\n", semitones);
    WsolaPitchShifter ps;
    ps.prepare (kRate);
    ps.setRatio (std::exp2 (semitones / 12.0f), 0.0f);

    const double chord[] = { 82.41, 123.47, 164.81, 207.65, 246.94, 329.63 };
    const size_t n = (size_t) kRate * 2;
    std::vector<float> l (n, 0.0f), r;
    std::vector<double> shifted;
    for (double f0 : chord)
        for (int k = 1; k <= 6; ++k)
        {
            shifted.push_back (f0 * k * std::exp2 (semitones / 12.0));
            for (size_t i = 0; i < n; ++i)
                l[i] += (float) (0.1 / k * std::sin (2.0 * 3.141592653589793 * f0 * k * (double) i / kRate + 0.3 * k));
        }
    r = l;

    double delaySum = 0.0;
    int delayCount = 0;
    for (size_t off = 0; off < n; off += kBlock)
    {
        const int len = (int) std::min<size_t> (kBlock, n - off);
        ps.process (l.data() + off, r.data() + off, l.data() + off, r.data() + off, len);
        if (off > n / 4) { delaySum += ps.getCurrentDelaySamples(); ++delayCount; }
    }
    const double noise = spliceNoiseDb (l, n / 2, (size_t) (kRate * 0.5), shifted);
    const double latencyMs = delaySum / delayCount / kRate * 1000.0;
    std::printf ("  splice noise %.1f dB  average latency %.1f ms\n", noise, latencyMs);
    CHECK (noise < -18.0);       // the fixed-grain shifter (<= 0.1.4) measured -5 to -8 dB here
    CHECK (latencyMs < 20.0);
}

void testRateBridge (double hostRate)
{
    std::printf ("rate bridge %.0f -> 48000 -> %.0f\n", hostRate, hostRate);
    RateBridge bridge;
    bridge.prepare (hostRate, 48000.0, kBlock);

    const double f0 = 1000.0;
    const size_t n = (size_t) hostRate;
    std::vector<float> x (n), y (n);
    for (size_t i = 0; i < n; ++i)
        x[i] = (float) std::sin (2.0 * 3.141592653589793 * f0 * (double) i / hostRate);

    int maxSeen = 0;
    for (size_t off = 0; off < n; off += kBlock)
    {
        const int len = (int) std::min<size_t> (kBlock, n - off);
        bridge.process (x.data() + off, y.data() + off, len, [&] (float*, int m) { maxSeen = std::max (maxSeen, m); });
    }

    CHECK (maxSeen <= bridge.getMaxModelBlock());
    double peak = 0.0, err = 0.0;
    int zeros = 0;
    for (size_t i = n / 2; i < n; ++i)
    {
        peak = std::max (peak, (double) std::abs (y[i]));
        zeros += y[i] == 0.0f && x[i] != 0.0f;
    }
    // Compare against the analytic sine delayed by the bridge latency (fractional lag search).
    double best = 1e9;
    for (double lag = 0.0; lag < 16.0; lag += 0.01)
    {
        err = 0.0;
        for (size_t i = n / 2; i < n; ++i)
            err += std::abs (y[i] - std::sin (2.0 * 3.141592653589793 * f0 * ((double) i - lag) / hostRate));
        best = std::min (best, err / (double) (n / 2));
    }
    std::printf ("  peak %.4f  mean abs err %.5f  max model block %d\n", peak, best, maxSeen);
    CHECK (zeros == 0);
    CHECK_NEAR (peak, 1.0, 0.01);
    CHECK (best < 0.01);
}

void testPitchDetector()
{
    std::puts ("pitch detector (MPM @ 24 kHz)");
    constexpr double rate = 24000.0;
    PitchDetector detector;
    detector.prepare (rate, 2048);
    std::vector<float> w (2048);

    // Bass low B, guitar strings, high harmonic; weak fundamental + strong 2nd harmonic (string-like).
    double worstCents = 0.0;
    for (double f : { 30.87, 41.20, 82.41, 110.0, 146.83, 196.0, 246.94, 329.63, 659.26, 1046.5 })
    {
        for (int shape = 0; shape < 2; ++shape)
        {
            for (size_t i = 0; i < w.size(); ++i)
            {
                const double t = 2.0 * 3.141592653589793 * f * (double) i / rate;
                w[i] = shape == 0 ? (float) (0.5 * std::sin (t))
                                  : (float) (0.15 * std::sin (t) + 0.5 * std::sin (2 * t + 0.3) + 0.3 * std::sin (3 * t + 1.1) + 0.2 * std::sin (4 * t + 2.0));
            }
            const auto r = detector.analyse (w.data());
            const double cents = r.valid ? 1200.0 * std::log2 (r.frequency / f) : 9999.0;
            worstCents = std::max (worstCents, std::abs (cents));
            if (! r.valid || std::abs (cents) > 1.0)
                std::printf ("  f=%.2f shape=%d -> %.3f Hz (%.2f ct, clarity %.2f)\n", f, shape, r.frequency, cents, r.clarity);
            CHECK (r.valid);
            CHECK (std::abs (cents) < 1.0);   // includes no octave errors
        }
    }
    std::printf ("  worst error %.3f cents\n", worstCents);

    std::mt19937 rng (1);
    std::uniform_real_distribution<float> noise (-0.5f, 0.5f);
    for (auto& v : w) v = noise (rng);
    CHECK (! detector.analyse (w.data()).valid);       // noise: no pitch

    std::fill (w.begin(), w.end(), 0.0f);
    CHECK (! detector.analyse (w.data()).valid);       // silence

    const auto a = frequencyToNote (446.0f, 440.0f);
    CHECK (a.midiNote == 69 && std::abs (a.cents - 23.45f) < 0.1f);
    CHECK (frequencyToNote (82.41f, 440.0f).midiNote == 40);   // low E = E2
    CHECK (std::string (noteName (40)) == "E" && noteOctave (40) == 2);
}

//== Block helpers ==============================================================
struct BlockRig
{
    std::shared_ptr<ParamStore> store;
    DSPBlock* block = nullptr;
    explicit BlockRig (BlockType type, double rate = kRate)
    {
        store = createStore (type, 1);
        block = dynamic_cast<DSPBlock*> (store.get());
        if (block != nullptr) block->prepare (rate, kBlock);
    }
    void set (int param, float value) { store->setParam (param, value); }

    /** Run a mono signal (duplicated to stereo) through the block, in place. */
    void run (std::vector<float>& l, std::vector<float>& r)
    {
        for (size_t off = 0; off < l.size(); off += kBlock)
        {
            const int n = (int) std::min<size_t> (kBlock, l.size() - off);
            block->process (l.data() + off, r.data() + off, n);
        }
    }
};

std::vector<float> sine (double hz, double seconds, float amplitude = 0.5f)
{
    std::vector<float> x ((size_t) (kRate * seconds));
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = amplitude * (float) std::sin (2.0 * 3.141592653589793 * hz * (double) i / kRate);
    return x;
}

double rmsDb (const std::vector<float>& x, size_t from, size_t to)
{
    double acc = 0.0;
    for (size_t i = from; i < to; ++i) acc += (double) x[i] * x[i];
    return 10.0 * std::log10 (acc / (double) (to - from) + 1e-20);
}

void testAllBlocksStress()
{
    std::puts ("all blocks: random params, extremes, bypass (finite, bounded, no allocations)");
    std::mt19937 rng (7);
    std::uniform_real_distribution<float> unit (0.0f, 1.0f);

    for (int t = 1; t < (int) BlockType::NumTypes; ++t)
    {
        const auto type = (BlockType) t;
        if (! isImplemented (type) || type == BlockType::Splitter || type == BlockType::Mixer)
            continue;

        BlockRig rig (type);
        if (rig.block == nullptr) continue;
        rng.seed (7u + (unsigned) t);   // per block: adding a param elsewhere doesn't reshuffle this one

        // Test signal: guitar-ish sine, noise bursts and full-scale impulses.
        std::vector<float> l ((size_t) kRate / 2), r;
        bool finite = true;
        float peak = 0.0f;
        int allocations = 0;
        double seconds = 0.0;
        const auto start = std::chrono::steady_clock::now();

        for (int trial = 0; trial < 8; ++trial)
        {
            for (int p = 0; p < rig.store->getNumParams(); ++p)
            {
                const auto& info = rig.store->getParamInfo (p);
                const float u = trial == 0 ? -1.0f : trial == 1 ? 0.0f : trial == 2 ? 1.0f : unit (rng);
                rig.set (p, u < 0.0f ? info.defaultValue : info.minValue + u * (info.maxValue - info.minValue));
            }
            if (trial == 6) rig.store->setBypassed (true);
            if (trial == 7) rig.store->setBypassed (false);

            for (size_t i = 0; i < l.size(); ++i)
            {
                const float noise = (i / 4000) % 3 == 1 ? (unit (rng) - 0.5f) * 0.6f : 0.0f;
                l[i] = 0.4f * (float) std::sin (2.0 * 3.141592653589793 * 110.0 * (double) i / kRate) + noise
                     + (i % 9000 == 0 ? 1.0f : 0.0f);
            }
            r = l;

            const int before = gAudioAllocations.load();
            gTrackAllocations = true;
            rig.run (l, r);
            gTrackAllocations = false;
            allocations += gAudioAllocations.load() - before;
            seconds += (double) l.size() / kRate;

            for (size_t i = 0; i < l.size(); ++i)
            {
                finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
                peak = std::max ({ peak, std::abs (l[i]), std::abs (r[i]) });
            }
        }

        const double elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
        std::printf ("  %-16s peak %6.2f  allocs %d  CPU %5.2f %% of one core\n",
                     getBlockTypeInfo (type).displayName, peak, allocations, 100.0 * elapsed / seconds);
        CHECK (finite);
        CHECK (peak < 200.0f);   // extreme EQ/makeup settings legitimately add > 30 dB
        CHECK (allocations == 0);
    }
}

void testBlockBehaviour()
{
    std::puts ("block behaviour");

    { // Compressor: 0 dBFS-peak sine, -20 dB threshold, 4:1 hard knee -> about -15 dB peak
        BlockRig rig (BlockType::Compressor);
        rig.set (CompressorBlock::KneeDb, 0.0f);
        auto l = sine (440.0, 1.0, 1.0f), r = l;
        rig.run (l, r);
        double peakDb = -200.0;
        for (size_t i = l.size() / 2; i < l.size(); ++i) peakDb = std::max (peakDb, 20.0 * std::log10 (std::abs ((double) l[i]) + 1e-12));
        std::printf ("  compressor 0 dB in, -20 thr, 4:1 -> %.2f dB peak (expect ~-15)\n", peakDb);
        CHECK (std::abs (peakDb + 15.0) < 1.0);
    }
    { // Gate: -80 dB is closed, -20 dB passes
        BlockRig rig (BlockType::NoiseGate);
        auto quiet = sine (220.0, 0.5, 1e-4f), q2 = quiet;
        rig.run (quiet, q2);
        auto loud = sine (220.0, 0.5, 0.1f), l2 = loud;
        rig.run (loud, l2);
        CHECK (rmsDb (quiet, quiet.size() / 2, quiet.size()) < -150.0);
        CHECK (std::abs (rmsDb (loud, loud.size() / 2, loud.size()) - rmsDb (sine (220.0, 0.5, 0.1f), 12000, 24000)) < 0.5);
    }
    { // Parametric EQ: +12 dB peak at 1 kHz
        using EQ = ParametricEqBlock;
        BlockRig rig (BlockType::ParametricEq);
        rig.set (EQ::bandParam (3, EQ::BandOn), 1.0f);
        rig.set (EQ::bandParam (3, EQ::BandFreq), 1000.0f);
        rig.set (EQ::bandParam (3, EQ::BandGain), 12.0f);
        rig.block->prepare (kRate, kBlock);
        auto l = sine (1000.0, 0.5), r = l;
        rig.run (l, r);
        const double gain = rmsDb (l, 12000, 24000) - rmsDb (sine (1000.0, 0.5), 12000, 24000);
        std::printf ("  parametric EQ +12 dB @ 1 kHz -> %.2f dB\n", gain);
        CHECK (std::abs (gain - 12.0) < 0.5);
    }
    { // EQ low cut 100 Hz, 48 dB/oct: -3 dB at the corner, ~-48 dB one octave below
        using EQ = ParametricEqBlock;
        auto gainAt = [] (double hz)
        {
            BlockRig rig (BlockType::ParametricEq);
            rig.set (EQ::bandParam (0, EQ::BandOn), 1.0f);
            rig.set (EQ::bandParam (0, EQ::BandFreq), 100.0f);
            rig.set (EQ::bandParam (0, EQ::BandSlope), 3.0f);
            rig.block->prepare (kRate, kBlock);
            auto l = sine (hz, 1.0), r = l;
            rig.run (l, r);
            return rmsDb (l, 24000, 48000) - rmsDb (sine (hz, 1.0), 24000, 48000);
        };
        const double atCorner = gainAt (100.0), octaveBelow = gainAt (50.0);
        std::printf ("  EQ low cut 48 dB/oct @100 Hz: %.2f dB at 100 Hz, %.1f dB at 50 Hz\n", atCorner, octaveBelow);
        CHECK (std::abs (atCorner + 3.0) < 0.5);
        CHECK (octaveBelow < -44.0 && octaveBelow > -52.0);

        // The display curve is computed from the same design as the DSP.
        EqBand band;
        band.on = true; band.type = EqBandType::LowCut; band.frequency = 100.0f; band.slope = 3; band.q = 0.7071f;
        CHECK (std::abs (eqBandMagnitudeDb (band, kRate, 100.0f) - atCorner) < 0.3);
    }
    { // Tempo sync: 1/4 at 120 BPM -> echo at 500 ms; dotted 1/8 at 100 BPM -> 450 ms
        std::atomic<double> bpm { 120.0 };
        BlockRig rig (BlockType::DigitalDelay);
        auto* delay = static_cast<DigitalDelayBlock*> (rig.block);
        delay->setTempoSource (&bpm);
        rig.set (DigitalDelayBlock::Sync, 1.0f);
        rig.set (DigitalDelayBlock::Division, 6.0f);   // 1/4
        rig.set (DigitalDelayBlock::Feedback, 0.0f);
        rig.set (DigitalDelayBlock::Mix, 100.0f);
        rig.block->prepare (kRate, kBlock);
        std::vector<float> l ((size_t) kRate, 0.0f), r;
        l[0] = 1.0f; r = l;
        rig.run (l, r);
        std::printf ("  sync 1/4 @120 BPM: %s, echo %.3f at 500 ms\n", delay->getDisplayOverride (DigitalDelayBlock::TimeMs).c_str(), l[24000]);
        CHECK (std::abs (l[24000] - 1.0f) < 0.02f);
        bpm = 100.0;
        rig.set (DigitalDelayBlock::Division, 10.0f);  // 1/8.
        CHECK (std::abs (delay->effectiveTimeMs() - 450.0f) < 0.01f);

        BlockRig trem (BlockType::Tremolo);
        trem.block->setTempoSource (&bpm);
        trem.set (TremoloBlock::Sync, 1.0f);
        trem.set (TremoloBlock::Division, 9.0f);       // 1/8 at 100 BPM = 3.33 Hz
        CHECK (std::abs (static_cast<TremoloBlock*> (trem.block)->effectiveRateHz() - 100.0f / 60.0f * 2.0f) < 0.01f);
        std::printf ("  tremolo sync: %s\n", trem.block->getDisplayOverride (TremoloBlock::RateHz).c_str());
    }
    { // Graphic EQ: 1 kHz band +12
        BlockRig rig (BlockType::GraphicEq);
        rig.set (GraphicEqBlock::Band0 + 5, 12.0f);
        rig.block->prepare (kRate, kBlock);
        auto l = sine (1000.0, 0.5), r = l;
        rig.run (l, r);
        const double gain = rmsDb (l, 12000, 24000) - rmsDb (sine (1000.0, 0.5), 12000, 24000);
        std::printf ("  graphic EQ 1k +12 dB -> %.2f dB\n", gain);
        CHECK (gain > 11.0 && gain < 13.5);
    }
    { // Digital delay: echo exactly at 100 ms
        BlockRig rig (BlockType::DigitalDelay);
        rig.set (DigitalDelayBlock::TimeMs, 100.0f);
        rig.set (DigitalDelayBlock::Feedback, 0.0f);
        rig.set (DigitalDelayBlock::Mix, 100.0f);
        rig.block->prepare (kRate, kBlock);
        std::vector<float> l ((size_t) kRate / 2, 0.0f), r;
        l[0] = 1.0f; r = l;
        rig.run (l, r);
        CHECK (std::abs (l[4800] - 1.0f) < 0.02f);
        CHECK (std::abs (l[2400]) < 1e-4f);
    }
    { // Trails: after bypass, the echo already in the line still sounds; new input is not delayed
        BlockRig rig (BlockType::DigitalDelay);
        rig.set (DigitalDelayBlock::TimeMs, 200.0f);
        rig.set (DigitalDelayBlock::Feedback, 0.0f);
        rig.set (DigitalDelayBlock::Mix, 100.0f);
        rig.block->prepare (kRate, kBlock);
        std::vector<float> l (2048, 0.0f), r;
        l[0] = 1.0f; r = l;
        rig.run (l, r);                       // impulse enters the line
        rig.store->setBypassed (true);
        std::vector<float> l2 ((size_t) kRate / 2, 0.0f), r2;
        l2[4000] = 1.0f; r2 = l2;             // new hit while bypassed
        rig.run (l2, r2);
        const size_t echo = 9600 - 2048;
        std::printf ("  trails: echo after bypass %.3f, new hit echoed %.4f\n", l2[echo], l2[4000 + 9600]);
        CHECK (std::abs (l2[echo]) > 0.9f);
        CHECK (std::abs (l2[4000 + 9600]) < 1e-3f);
    }
    { // Reverbs: impulse tail exists and decays
        for (auto type : { BlockType::HallReverb, BlockType::PlateReverb, BlockType::SpringReverb, BlockType::ShimmerReverb, BlockType::CloudReverb })
        {
            BlockRig rig (type);
            rig.set (rig.store->findParam ("mix"), 100.0f);
            std::vector<float> l ((size_t) kRate * 6, 0.0f), r;
            l[100] = 1.0f; r = l;
            rig.run (l, r);
            const double early = rmsDb (l, (size_t) (0.05 * kRate), (size_t) (0.5 * kRate));
            const double late  = rmsDb (l, (size_t) (5.0 * kRate), (size_t) (6.0 * kRate));
            std::printf ("  %-15s tail %.1f dB -> %.1f dB after 5 s\n", getBlockTypeInfo (type).displayName, early, late);
            CHECK (early > -70.0);
            CHECK (late < early - 15.0);
        }
    }
    { // Hall RT60 roughly follows the Decay setting
        BlockRig rig (BlockType::HallReverb);
        rig.set (HallReverbBlock::DecaySeconds, 2.0f);
        rig.set (HallReverbBlock::Mix, 100.0f);
        rig.set (HallReverbBlock::Damping, 0.0f);
        std::vector<float> l ((size_t) kRate * 3, 0.0f), r;
        for (int i = 0; i < 480; ++i) l[(size_t) i] = (float) std::sin (i * 0.3) * 0.5f;
        r = l;
        rig.run (l, r);
        const double a = rmsDb (l, (size_t) (0.4 * kRate), (size_t) (0.6 * kRate));
        const double b = rmsDb (l, (size_t) (1.4 * kRate), (size_t) (1.6 * kRate));
        const double rt60 = 60.0 / (a - b);
        std::printf ("  hall decay 2.0 s -> measured RT60 %.2f s\n", rt60);
        CHECK (rt60 > 1.4 && rt60 < 2.8);
    }
    { // Tremolo: 100 % depth fully modulates
        BlockRig rig (BlockType::Tremolo);
        rig.set (TremoloBlock::Depth, 100.0f);
        rig.block->prepare (kRate, kBlock);
        std::vector<float> l ((size_t) kRate, 0.5f), r = l;
        rig.run (l, r);
        float lo = 1.0f, hi = 0.0f;
        for (size_t i = l.size() / 2; i < l.size(); ++i) { lo = std::min (lo, l[i]); hi = std::max (hi, l[i]); }
        CHECK (lo < 0.01f && hi > 0.49f);
    }
    { // Octaver: only the -1 octave voice -> 110 Hz from 220 Hz
        BlockRig rig (BlockType::Octaver);
        rig.set (OctaverBlock::DryDb, -60.0f);
        rig.set (OctaverBlock::SubDb, 0.0f);
        rig.block->prepare (kRate, kBlock);
        auto l = sine (220.0, 2.0), r = l;
        rig.run (l, r);
        const double f = estimateFrequency (l, l.size() / 2, l.size());
        std::printf ("  octaver 220 Hz -> %.2f Hz\n", f);
        CHECK (std::abs (f - 110.0) < 1.5);
    }
    { // Wah: heel lets lows through, toe favours highs
        auto bandGain = [] (float position, double hz)
        {
            BlockRig rig (BlockType::Wah);
            rig.set (WahBlock::Position, position);
            rig.block->prepare (kRate, kBlock);
            auto l = sine (hz, 0.5), r = l;
            rig.run (l, r);
            return rmsDb (l, 12000, 24000);
        };
        const double heelHigh = bandGain (0.0f, 2000.0), toeHigh = bandGain (100.0f, 2000.0);
        std::printf ("  wah @ 2 kHz: heel %.1f dB, toe %.1f dB\n", heelHigh, toeHigh);
        CHECK (toeHigh > heelHigh + 12.0);
    }
}

/** String-like note: weak fundamental, decaying; frequency may glide (bend). */
void addNote (std::vector<float>& x, double start, double length, double hzStart, double hzEnd, double amplitude)
{
    double phase = 0.0;
    const auto s0 = (size_t) (start * kRate), n = (size_t) (length * kRate);
    for (size_t i = 0; i < n && s0 + i < x.size(); ++i)
    {
        const double t = (double) i / kRate;
        const double hz = hzStart + (hzEnd - hzStart) * std::min (1.0, t / std::max (1e-9, length * 0.6));
        phase += 2.0 * 3.141592653589793 * hz / kRate;
        const double env = amplitude * std::exp (-t / 0.7) * std::min (1.0, t / 0.002);
        x[s0 + i] += (float) (env * (0.25 * std::sin (phase) + 0.6 * std::sin (2 * phase + 0.3) + 0.3 * std::sin (3 * phase + 0.9)));
    }
}

void testRealtimeTracker()
{
    std::puts ("real-time pitch tracker");
    RealtimePitchTracker tracker;
    tracker.prepare (kRate);
    std::vector<float> x ((size_t) kRate, 0.0f);
    addNote (x, 0.1, 0.8, 110.0, 110.0, 0.5);

    double firstValid = -1.0, worstCents = 0.0;
    for (size_t off = 0; off < x.size(); off += kBlock)
        tracker.process (x.data() + off, (int) std::min<size_t> (kBlock, x.size() - off), [&] (int i, const RealtimePitchTracker::Estimate& e)
        {
            const double t = (double) (off + (size_t) i) / kRate;
            if (e.valid && firstValid < 0.0) firstValid = t;
            if (e.valid && t > 0.2 && t < 0.8) worstCents = std::max (worstCents, std::abs (1200.0 * std::log2 (e.frequency / 110.0)));
        });
    std::printf ("  first estimate %.1f ms after the pick, worst error %.2f cents\n", (firstValid - 0.1) * 1000.0, worstCents);
    CHECK (firstValid > 0.1 && firstValid - 0.1 < 0.035);
    CHECK (worstCents < 3.0);
}

void testGuitarToMidi()
{
    std::puts ("guitar -> MIDI");
    RealtimePitchTracker tracker;
    tracker.prepare (kRate);
    GuitarToMidi midi;
    midi.setBendRange (2);

    // E2 (decays out), silence, then A2 bent up one semitone.
    std::vector<float> x ((size_t) (kRate * 2.6), 0.0f);
    addNote (x, 0.10, 0.80, 82.41, 82.41, 0.5);
    addNote (x, 1.40, 1.00, 110.0, 116.54, 0.5);

    struct Logged { double t; GuitarToMidi::Event e; };
    std::vector<Logged> log;
    for (size_t off = 0; off < x.size(); off += kBlock)
    {
        const int n = (int) std::min<size_t> (kBlock, x.size() - off);
        midi.beginBlock();
        tracker.process (x.data() + off, n, [&] (int i, const RealtimePitchTracker::Estimate& e) { midi.onEstimate (i, e); });
        for (int k = 0; k < midi.getNumEvents(); ++k)
            log.push_back ({ (double) (off + (size_t) midi.getEvent (k).sampleOffset) / kRate, midi.getEvent (k) });
    }

    int noteOns = 0, lastBend = 8192;
    double firstOn = -1.0, secondOn = -1.0, firstOff = -1.0;
    for (const auto& l : log)
    {
        using T = GuitarToMidi::Event::Type;
        if (l.e.type == T::NoteOn)
        {
            ++noteOns;
            std::printf ("  %.3f s  note on  %d  vel %d\n", l.t, l.e.note, l.e.velocity);
            if (noteOns == 1) { firstOn = l.t; CHECK (l.e.note == 40); }
            if (noteOns == 2) { secondOn = l.t; CHECK (l.e.note == 45); }
        }
        else if (l.e.type == T::NoteOff)
        {
            std::printf ("  %.3f s  note off %d\n", l.t, l.e.note);
            if (firstOff < 0.0) firstOff = l.t;
        }
        else if (noteOns == 2 && l.e.bend != 8192)
            lastBend = l.e.bend;   // bend reached on the A2 (the note-off then recentres it)
    }
    const double bendSemis = (lastBend - 8192) / 8191.0 * 2.0;
    std::printf ("  latency E2 %.1f ms, A2 %.1f ms; final bend %+.2f st\n", (firstOn - 0.10) * 1000.0, (secondOn - 1.40) * 1000.0, bendSemis);
    CHECK (noteOns == 2);
    CHECK (firstOn - 0.10 < 0.045);
    CHECK (secondOn - 1.40 < 0.045);
    CHECK (firstOff > firstOn && firstOff < 1.40);
    CHECK (std::abs (bendSemis - 1.0) < 0.1);
}

void testGuitarSynth()
{
    std::puts ("guitar synth (mono)");
    for (int octave : { 2, 1 })   // 0 and -1 octave
    {
        BlockRig rig (BlockType::GuitarSynth);
        rig.set (GuitarSynthBlock::Octave, (float) octave);
        rig.set (GuitarSynthBlock::Sub, 0.0f);
        rig.set (GuitarSynthBlock::Wave, 1.0f);   // square: clean zero crossings
        rig.block->prepare (kRate, kBlock);
        std::vector<float> l ((size_t) kRate, 0.0f);
        addNote (l, 0.05, 0.95, 110.0, 110.0, 0.5);
        auto r = l;
        rig.run (l, r);
        const double expected = octave == 2 ? 110.0 : 55.0;
        const double f = estimateFrequency (l, (size_t) (0.3 * kRate), (size_t) (0.7 * kRate));
        std::printf ("  octave %s: %.2f Hz (expected %.0f)\n", octave == 2 ? " 0" : "-1", f, expected);
        CHECK (std::abs (f - expected) < 1.5);
    }
}

void testGuitarSynthTexture()
{
    std::puts ("guitar synth (textura: pluck layer, stereo)");
    BlockRig rig (BlockType::GuitarSynth);
    rig.set (GuitarSynthBlock::Mode, 2.0f);
    rig.set (GuitarSynthBlock::DecayMs, 200.0f);
    rig.block->prepare (kRate, kBlock);

    // Two picks of a chord-like tone that rings for the whole second.
    std::vector<float> l ((size_t) kRate, 0.0f);
    for (double pick : { 0.1, 0.6 })
        for (double hz : { 110.0, 164.8, 220.0 })
            for (size_t i = (size_t) (pick * kRate); i < l.size(); ++i)
            {
                const double t = (double) i / kRate - pick;
                l[i] += (float) (0.15 * std::exp (-t / 1.5) * std::sin (2.0 * 3.141592653589793 * hz * t));
            }
    auto r = l;
    rig.run (l, r);
    bool finite = true;
    for (float x : l) finite = finite && std::isfinite (x);
    const double attack = rmsDb (l, (size_t) (0.12 * kRate), (size_t) (0.22 * kRate));
    const double tail   = rmsDb (l, (size_t) (0.45 * kRate), (size_t) (0.58 * kRate));
    const double second = rmsDb (l, (size_t) (0.62 * kRate), (size_t) (0.72 * kRate));
    double diff = 0.0;
    for (size_t i = 0; i < l.size(); ++i) diff += std::abs (l[i] - r[i]);
    std::printf ("  pluck %.1f dB -> tail %.1f dB, second pick %.1f dB, L/R difference %.3f\n", attack, tail, second, diff / (double) l.size());
    CHECK (finite);
    CHECK (attack > tail + 8.0);    // each pick decays like a pluck...
    CHECK (second > tail + 8.0);    // ...and the next pick fires it again
    CHECK (diff / (double) l.size() > 1.0e-3);   // stereo width
}

/** Plucked-string chord: 12 stretched harmonics per note, random phases, decaying. */
void addPluckedChord (std::vector<float>& x, double start, std::initializer_list<int> notes, float amplitude = 0.04f)
{
    std::mt19937 rng (3);
    std::uniform_real_distribution<double> phase (0.0, 6.283185307179586);
    for (const int n : notes)
    {
        const double f = 440.0 * std::pow (2.0, (n - 69) / 12.0);
        for (int h = 1; h <= 12; ++h)
        {
            const double fh = f * h * std::sqrt (1.0 + 0.0001 * h * h), ph = phase (rng);
            const double a = amplitude * (h == 1 ? 0.6 : 1.0) / std::pow (h, 1.1), tau = 1.5 / (1.0 + 0.4 * h);
            for (size_t i = (size_t) (start * kRate); i < x.size(); ++i)
            {
                const double t = (double) i / kRate - start;
                x[i] += (float) (a * std::exp (-t / tau) * std::sin (2.0 * 3.141592653589793 * fh * t + ph));
            }
        }
    }
}

void testPolyNoteDetector()
{
    std::puts ("poly note detector: chords from a single guitar signal");
    struct Case { std::initializer_list<int> notes; std::initializer_list<int> mustHave; };
    const Case cases[] {
        { { 45 }, { 45 } },                              // A2
        { { 64 }, { 64 } },                              // E4
        { { 48, 52, 55, 60, 64 }, { 48, 52, 55 } },      // C major (open)
        { { 40, 47, 52, 56, 59, 64 }, { 40, 47, 56 } },  // E major (open): octaves fold into the root
        { { 50, 57, 62, 66 }, { 50, 57, 66 } },          // D major
    };
    for (const auto& c : cases)
    {
        PolyNoteDetector detector;
        detector.prepare (kRate);
        std::vector<float> x ((size_t) kRate, 0.0f);
        addPluckedChord (x, 0.0, c.notes);

        std::array<bool, 12> classes {};
        for (const int n : c.notes) classes[(size_t) (n % 12)] = true;
        double first = -1.0;
        bool onlyChordTones = true;
        PolyNoteDetector::NoteSet at500;
        detector.process (x.data(), (int) x.size(), [&] (int offset, const PolyNoteDetector::NoteSet& set)
        {
            const double t = offset / kRate;
            if (set.count > 0 && first < 0.0) first = t;
            for (int i = 0; i < set.count; ++i)
                onlyChordTones = onlyChordTones && classes[(size_t) (set.notes[(size_t) i] % 12)];
            if (t < 0.5) at500 = set;
        });
        std::string found;
        for (int i = 0; i < at500.count; ++i) found += " " + std::to_string (at500.notes[(size_t) i]);
        std::printf ("  %zu notes -> first after %.0f ms, at 0.5 s:%s\n", c.notes.size(), first * 1000.0, found.c_str());
        CHECK (first > 0.0 && first < 0.1);
        CHECK (onlyChordTones);   // never a note outside the chord, not even during the pick
        for (const int n : c.mustHave)
            CHECK (std::find (at500.notes.begin(), at500.notes.begin() + at500.count, (std::uint8_t) n) != at500.notes.begin() + at500.count);
    }
}

void testInstrumentSynth()
{
    std::puts ("guitar synth (instrumento: bank levels, re-pick, chord change, sequencer)");
    using GS = GuitarSynthBlock;
    auto makeRig = [] (int sound)
    {
        BlockRig rig (BlockType::GuitarSynth);
        rig.set (GS::Mode, (float) GS::Voice::Instrument);
        rig.set (GS::Sound, (float) sound);
        rig.set (GS::Mix, 100.0f);
        rig.block->prepare (kRate, kBlock);
        return rig;
    };

    // Every sound: finite and level-matched on the same chord.
    for (int sound = 0; sound < InstrumentSynth::kNumSounds; ++sound)
    {
        auto rig = makeRig (sound);
        std::vector<float> l ((size_t) kRate, 0.0f);
        addPluckedChord (l, 0.0, { 48, 52, 55, 60, 64 });
        auto r = l;
        rig.run (l, r);
        bool finite = true;
        float peak = 0.0f;
        for (size_t i = 0; i < l.size(); ++i)
        {
            finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
            peak = std::max ({ peak, std::abs (l[i]), std::abs (r[i]) });
        }
        const double level = 0.5 * (rmsDb (l, (size_t) (0.1 * kRate), (size_t) (0.9 * kRate)) + rmsDb (r, (size_t) (0.1 * kRate), (size_t) (0.9 * kRate)));
        std::printf ("  %-15s %6.1f dB rms, peak %5.1f dB\n", InstrumentSynth::soundName (sound), level, 20.0 * std::log10 (peak + 1e-9));
        CHECK (finite);
        CHECK (level > -27.0 && level < -16.0);
        CHECK (peak < 0.7f);
    }

    // Piano: a second pick of the same ringing chord strikes it again.
    {
        auto rig = makeRig (0);
        std::vector<float> l ((size_t) (2.0 * kRate), 0.0f);
        addPluckedChord (l, 0.1, { 48, 52, 55 });
        addPluckedChord (l, 1.2, { 48, 52, 55 });
        auto r = l;
        rig.run (l, r);
        const double before = rmsDb (l, (size_t) (1.05 * kRate), (size_t) (1.2 * kRate));
        const double after  = rmsDb (l, (size_t) (1.25 * kRate), (size_t) (1.4 * kRate));
        std::printf ("  piano re-pick: %.1f dB -> %.1f dB\n", before, after);
        CHECK (after > before + 4.0);
    }

    // Chord change: C major, then G major; the held notes follow.
    {
        auto rig = makeRig (5);
        std::vector<float> l ((size_t) (2.0 * kRate), 0.0f);
        addPluckedChord (l, 0.1, { 48, 52, 55 }, 0.03f);
        addPluckedChord (l, 1.0, { 43, 47, 50, 55 }, 0.06f);
        auto r = l;
        std::string atC, atG;
        for (size_t off = 0; off < l.size(); off += kBlock)
        {
            rig.block->process (l.data() + off, r.data() + off, (int) std::min<size_t> (kBlock, l.size() - off));
            const auto chips = rig.block->getInfoChips();
            if (off == (size_t) (0.8 * kRate) / kBlock * kBlock && chips.size() > 1) atC = chips[1];
            if (off == (size_t) (1.8 * kRate) / kBlock * kBlock && chips.size() > 1) atG = chips[1];
        }
        std::printf ("  held: \"%s\" then \"%s\"\n", atC.c_str(), atG.c_str());
        CHECK (atC.find ("C3") != std::string::npos && atC.find ("E3") != std::string::npos);
        CHECK (atG.find ("G2") != std::string::npos && atG.find ("B2") != std::string::npos);
        CHECK (atG.find ("C3") == std::string::npos && atG.find ("E3") == std::string::npos);
    }

    // Sequencer: arpeggio at 1/16 of 120 BPM = a note every 125 ms.
    {
        auto rig = makeRig (2);   // drawbar organ: notes start and stop with the gate
        rig.set (GS::Sequence, 1.0f);
        rig.set (GS::SeqRate, 3.0f);
        rig.set (GS::SeqGate, 50.0f);
        std::vector<float> l ((size_t) (2.0 * kRate), 0.0f);
        addPluckedChord (l, 0.1, { 48, 52, 55 });
        auto r = l;
        rig.run (l, r);
        // Note starts: the 5 ms frame energy crossing a threshold upwards.
        const size_t frame = (size_t) (0.005 * kRate);
        std::vector<double> starts;
        bool on = false;
        for (size_t f = 0; (f + 1) * frame < l.size(); ++f)
        {
            const double db = rmsDb (l, f * frame, (f + 1) * frame);
            if (! on && db > -40.0) { starts.push_back ((double) (f * frame) / kRate); on = true; }
            if (on && db < -50.0) on = false;
        }
        double meanInterval = 0.0, worst = 0.0;
        for (size_t i = 1; i < starts.size(); ++i)
            meanInterval += starts[i] - starts[i - 1];
        meanInterval /= std::max<size_t> (1, starts.size() - 1);
        for (size_t i = 1; i < starts.size(); ++i)
            worst = std::max (worst, std::abs (starts[i] - starts[i - 1] - 0.125));
        std::printf ("  arpeggio: %zu notes, first at %.0f ms, every %.1f ms (worst deviation %.1f ms)\n",
                     starts.size(), starts.empty() ? 0.0 : starts[0] * 1000.0, meanInterval * 1000.0, worst * 1000.0);
        CHECK (starts.size() >= 10);
        CHECK (std::abs (meanInterval - 0.125) < 0.003);
        CHECK (worst < 0.011);
    }

    // Single-note mode follows the pitch tracker.
    {
        auto rig = makeRig (14);
        rig.set (GS::Notes, 1.0f);
        std::vector<float> l ((size_t) kRate, 0.0f);
        addPluckedChord (l, 0.05, { 57 }, 0.1f);   // A3
        auto r = l;
        std::string held;
        for (size_t off = 0; off < l.size(); off += kBlock)
        {
            rig.block->process (l.data() + off, r.data() + off, (int) std::min<size_t> (kBlock, l.size() - off));
            const auto chips = rig.block->getInfoChips();
            if (off == (size_t) (0.5 * kRate) / kBlock * kBlock && chips.size() > 1) held = chips[1];
        }
        std::printf ("  single note: \"%s\"\n", held.c_str());
        CHECK (held == "A3");
    }
}

/** A sine burst with 10 ms fades (a short, clean excitation). */
std::vector<float> burst (double hz, double seconds, double total, float amplitude)
{
    std::vector<float> x ((size_t) (kRate * total), 0.0f);
    const auto n = (size_t) (kRate * seconds), fade = (size_t) (0.01 * kRate);
    for (size_t i = 0; i < n; ++i)
    {
        const float env = (float) std::min ({ 1.0, (double) i / fade, (double) (n - i) / fade });
        x[i] = amplitude * env * (float) std::sin (2.0 * 3.141592653589793 * hz * (double) i / kRate);
    }
    return x;
}

double highBandRatioDb (const std::vector<float>& x, size_t from, size_t to, float hz)
{
    Biquad hp1, hp2;
    hp1.setHighPass (kRate, hz);
    hp2.setHighPass (kRate, hz);
    std::vector<float> high (x.size());
    for (size_t i = 0; i < x.size(); ++i)
        high[i] = hp2.process (hp1.process (x[i]));
    return rmsDb (high, from, to) - rmsDb (x, from, to);
}

/** Level (dB) of one frequency in x[from, to) (Hann-windowed DFT bin). */
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

void testSitar()
{
    std::puts ("sitar (sympathetic strings, freeze, jawari, bend, level)");
    using S = SitarBlock;
    auto makeRig = [] (float dry, float lead, float symp, int mode = 0)
    {
        BlockRig rig (BlockType::Sitar);
        rig.set (S::Mode, (float) mode);
        rig.set (S::Dry, dry);
        rig.set (S::Lead, lead);
        rig.set (S::Sympathetic, symp);
        rig.block->prepare (kRate, kBlock);
        return rig;
    };
    const auto seconds = [] (double s) { return (size_t) (s * kRate); };

    // The strings (E Kafi by default) answer E4 and stay quiet for F4, which is not in the scale.
    double ringIn = 0.0, ringOut = 0.0;
    for (const int note : { 64, 65 })
    {
        auto rig = makeRig (0.0f, 0.0f, 100.0f);
        auto l = burst (440.0 * std::pow (2.0, (note - 69) / 12.0), 0.3, 1.6, 0.2f), r = l;
        rig.run (l, r);
        (note == 64 ? ringIn : ringOut) = rmsDb (l, seconds (0.5), seconds (1.5));
    }
    std::printf ("  sympathetic ring after the note: E4 %.1f dB, F4 %.1f dB\n", ringIn, ringOut);
    CHECK (ringIn > ringOut + 10.0);
    CHECK (ringIn > -60.0);

    // Freeze holds the drone; without it the strings die away.
    double held = 0.0, free = 0.0;
    for (const bool freeze : { true, false })
    {
        auto rig = makeRig (0.0f, 0.0f, 100.0f);
        auto l = burst (329.63, 0.3, 3.0, 0.2f), r = l;
        // Freeze while the strings ring (after the note).
        std::vector<float> l1 (l.begin(), l.begin() + (long) seconds (0.35)), r1 (l1);
        rig.run (l1, r1);
        rig.set (S::Freeze, freeze ? 1.0f : 0.0f);
        std::vector<float> l2 (l.begin() + (long) seconds (0.35), l.end()), r2 (l2);
        rig.run (l2, r2);
        std::copy (l1.begin(), l1.end(), l.begin());
        std::copy (l2.begin(), l2.end(), l.begin() + (long) l1.size());
        (freeze ? held : free) = rmsDb (l, seconds (2.5), seconds (3.0));
        if (freeze)
        {
            float peak = 0.0f;
            for (float v : l) peak = std::max (peak, std::abs (v));
            CHECK (peak < 1.0f);
        }
    }
    std::printf ("  2.5 s after: frozen %.1f dB, free %.1f dB\n", held, free);
    CHECK (held > free + 20.0);

    // The bridge contact adds the high harmonics a plain note (a pure, decaying E3) does not have.
    // Modelado: the string is in tune, near the guitar's level, and its jawari brightens it.
    for (const int mode : { 0, 1 })
    {
        double plain = 0.0, buzzing = 0.0, level = 0.0, pitch = 0.0;
        for (const float buzz : { 0.0f, 100.0f })
        {
            auto rig = makeRig (0.0f, 100.0f, 0.0f, mode);
            rig.set (S::Buzz, buzz);
            std::vector<float> l ((size_t) kRate, 0.0f);
            for (size_t i = seconds (0.05); i < l.size(); ++i)
            {
                const double t = (double) i / kRate - 0.05;
                l[i] = (float) (0.2 * std::exp (-t / 0.8) * std::min (1.0, t / 0.002) * std::sin (2.0 * 3.141592653589793 * 164.81 * t));
            }
            auto r = l;
            rig.run (l, r);
            (buzz > 0.0f ? buzzing : plain) = highBandRatioDb (l, seconds (0.1), seconds (0.9), 2000.0f);
            if (buzz > 0.0f)
                level = rmsDb (l, seconds (0.1), seconds (0.9));
            else
            {
                double best = -300.0;
                for (double f = 150.0; f < 180.0; f *= 1.0005)
                    if (const double v = toneLevelDb (l, seconds (0.2), seconds (0.9), f); v > best) { best = v; pitch = f; }
            }
        }
        std::printf ("  %s: energy above 2 kHz: plain %.1f dB, jawari %.1f dB; level %.1f dB (input -20 dB)",
                     mode ? "modelado" : "procesado", plain, buzzing, level);
        if (mode)
            std::printf ("; string %.2f Hz (E3 164.81)", pitch);
        std::printf ("\n");
        CHECK (buzzing > plain + (mode ? 3.0 : 15.0));
        CHECK (level > -28.0 && level < -12.0);
        if (mode)
            CHECK (std::abs (1200.0 * std::log2 (pitch / 164.81)) < 6.0);
    }

    // Bend: an octave up on the lead (WSOLA when processed; the string itself when modelled).
    for (const int mode : { 0, 1 })
    {
        auto rig = makeRig (0.0f, 100.0f, 0.0f, mode);
        rig.set (S::Buzz, 0.0f);
        rig.set (S::Bend, 12.0f);
        rig.block->reset();
        auto l = sine (220.0, 0.6, 0.2f), r = l;
        rig.run (l, r);
        const double octave = toneLevelDb (l, seconds (0.2), seconds (0.6), 440.0);
        const double unison = toneLevelDb (l, seconds (0.2), seconds (0.6), 220.0);
        std::printf ("  bend +12 (%s): 440 Hz %.1f dB, 220 Hz %.1f dB\n", mode ? "modelado" : "procesado", octave, unison);
        CHECK (octave > unison + 20.0);
    }

    // Tampura: a sustained drone with no input, on the sitar's key or its own; silent when off.
    for (const int tanpuraKey : { -1, 0, 1 })
    {
        auto rig = makeRig (0.0f, 0.0f, 0.0f);
        rig.set (S::Tanpura, tanpuraKey < 0 ? 0.0f : 1.0f);
        rig.set (S::TanpuraLevel, 100.0f);
        rig.set (S::TanpuraKey, tanpuraKey < 0 ? 0.0f : (float) tanpuraKey);   // 0 = sitar key (E), 1 = C
        rig.set (S::TanpuraOutput, 1.0f);                                        // with the chain: in the block's output
        rig.block->reset();
        std::vector<float> l ((size_t) (8.0 * kRate), 0.0f), r (l);
        rig.run (l, r);
        const double level = rmsDb (l, seconds (2.0), seconds (8.0));
        if (tanpuraKey < 0)
        {
            std::printf ("  tampura off: %.1f dB\n", level);
            CHECK (level < -100.0);
            continue;
        }
        // Sustained: no gaps or plucks, every 100 ms window close to the average.
        double quietest = 0.0, loudest = -200.0;
        for (double t = 2.0; t < 7.95; t += 0.1)
        {
            const double w = rmsDb (l, seconds (t), seconds (t + 0.1));
            quietest = std::min (quietest, w);
            loudest = std::max (loudest, w);
        }
        const double sa = tanpuraKey == 0 ? 164.81 : 130.81;   // E3 / C3
        const double atSa = toneLevelDb (l, seconds (2.0), seconds (3.0), sa);
        const double offSa = toneLevelDb (l, seconds (2.0), seconds (3.0), sa * std::pow (2.0, 1.0 / 12.0));
        float peak = 0.0f;
        bool finite = true;
        for (float v : l) { peak = std::max (peak, std::abs (v)); finite = finite && std::isfinite (v); }
        std::printf ("  tampura %s: %.1f dB rms (100 ms windows %.1f .. %.1f), peak %.1f dB, Sa %.1f dB vs a semitone up %.1f dB\n",
                     tanpuraKey == 0 ? "E (sitar key)" : "C", level, quietest, loudest, 20.0 * std::log10 (peak + 1e-9), atSa, offSa);
        CHECK (finite);
        CHECK (level > -30.0 && level < -18.0);
        CHECK (loudest - quietest < 6.0);
        CHECK (peak < 0.5f);
        CHECK (atSa > offSa + 12.0);
    }

    // Tampura "Directo": it reaches the output around the blocks after the sitar.
    for (const bool direct : { true, false })
    {
        Rig rig;
        GridModel m;
        auto sitar = rig.make (BlockType::Sitar);
        auto volume = rig.make (BlockType::Volume);
        for (int p : { (int) S::Dry, (int) S::Lead, (int) S::Sympathetic })
            sitar->setParam (p, 0.0f);
        sitar->setParam (S::Tanpura, 1.0f);
        sitar->setParam (S::TanpuraLevel, 100.0f);
        sitar->setParam (S::TanpuraOutput, direct ? 0.0f : 1.0f);
        volume->setParam (VolumeBlock::LevelDb, -60.0f);   // the "chain" after the sitar
        const auto in = m.addNode (NodeKind::Input);
        const auto a = m.addNode (NodeKind::Block, sitar->getId());
        const auto b = m.addNode (NodeKind::Block, volume->getId());
        const auto out = m.addNode (NodeKind::Output);
        m.connect ({ in, 0 }, a);
        m.connect ({ a, 0 }, b);
        m.connect ({ b, 0 }, out);
        auto res = rig.compile (m);
        CHECK (res.plan != nullptr);
        if (! res.plan) return;
        std::vector<float> l ((size_t) (3.0 * kRate), 0.0f), r (l);
        render (*res.plan, l, r);
        const double level = rmsDb (l, seconds (2.0), seconds (3.0));
        double bypassed = 0.0;
        if (direct)
        {
            sitar->setBypassed (true);
            std::vector<float> l2 ((size_t) (0.5 * kRate), 0.0f), r2 (l2);
            render (*res.plan, l2, r2);
            bypassed = rmsDb (l2, seconds (0.2), seconds (0.5));
        }
        std::printf ("  tampura %s, volume -60 dB after it: %.1f dB%s\n", direct ? "directo" : "con la cadena", level,
                     direct ? (" (sitar bypassed: " + std::to_string ((int) bypassed) + " dB)").c_str() : "");
        if (direct)
        {
            CHECK (level > -30.0);
            CHECK (bypassed < -100.0);
        }
        else
            CHECK (level < -75.0);
    }

    // Default settings on a chord (processed: the chord mode): level close to the guitar, finite, bounded.
    {
        auto rig = makeRig (0.0f, 80.0f, 50.0f);
        std::vector<float> l ((size_t) (2.0 * kRate), 0.0f);
        addPluckedChord (l, 0.05, { 40, 47, 52, 56, 59, 64 });
        const double in = rmsDb (l, seconds (0.1), seconds (1.5));
        auto r = l;
        rig.run (l, r);
        bool finite = true;
        float peak = 0.0f;
        for (size_t i = 0; i < l.size(); ++i)
        {
            finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
            peak = std::max ({ peak, std::abs (l[i]), std::abs (r[i]) });
        }
        const double out = 0.5 * (rmsDb (l, seconds (0.1), seconds (1.5)) + rmsDb (r, seconds (0.1), seconds (1.5)));
        std::printf ("  E chord: in %.1f dB, out %.1f dB rms, peak %.1f dB\n", in, out, 20.0 * std::log10 (peak + 1e-9));
        CHECK (finite);
        CHECK (std::abs (out - in) < 6.0);
        CHECK (peak < 0.7f);
    }
}

void testSitarModelled()
{
    std::puts ("sitar modelado (pluck follows the notes, slide, bounded)");
    using S = SitarBlock;
    const auto seconds = [] (double s) { return (size_t) (s * kRate); };
    BlockRig rig (BlockType::Sitar);   // default settings: Modelado
    rig.block->reset();
    std::vector<float> l ((size_t) (3.0 * kRate), 0.0f);
    addPluckedChord (l, 0.05, { 52 }, 0.2f);   // E3
    std::vector<float> next ((size_t) (3.0 * kRate), 0.0f);
    addPluckedChord (next, 1.5, { 57 }, 0.2f); // A3, picked over the ringing E3
    for (size_t i = seconds (1.5); i < l.size(); ++i)
        l[i] = next[i] + 0.1f * l[i];
    float inPeak = 0.0f;
    for (float v : l) inPeak = std::max (inPeak, std::abs (v));
    auto r = l;
    rig.run (l, r);
    const double e3 = toneLevelDb (l, seconds (0.5), seconds (1.4), 164.81), a3 = toneLevelDb (l, seconds (1.8), seconds (2.7), 220.0);
    const double e3late = toneLevelDb (l, seconds (1.8), seconds (2.7), 164.81);
    // The pluck lands within ~20 ms of the pick.
    size_t onset = 0;
    for (size_t i = seconds (1.5); i < seconds (1.6) && onset == 0; ++i)
        if (rmsDb (l, i, i + 48) > rmsDb (l, seconds (1.45), seconds (1.5)) + 6.0)
            onset = i;
    bool finite = true;
    float peak = 0.0f;
    for (size_t i = 0; i < l.size(); ++i) { finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]); peak = std::max ({ peak, std::abs (l[i]), std::abs (r[i]) }); }
    std::printf ("  E3 %.1f dB, then A3 %.1f dB (E3 left %.1f dB), re-pluck %.1f ms after the pick, peak %.1f dB (input %.1f dB)\n",
                 e3, a3, e3late, onset > 0 ? 1000.0 * (double) (onset - seconds (1.5)) / kRate : -1.0,
                 20.0 * std::log10 (peak + 1e-9), 20.0 * std::log10 (inPeak + 1e-9));
    CHECK (finite);
    CHECK (peak < 2.0f * inPeak && peak < 1.0f);
    CHECK (e3 > -40.0);
    CHECK (a3 > e3late + 15.0);
    CHECK (onset > 0 && onset < seconds (1.5 + 0.03));
    CHECK (std::lround (rig.block->getParam (S::Mode)) == 1);
}

void testCharango()
{
    std::puts ("charango (fold voicing, processed octave, modelled strum, level)");
    using C = CharangoBlock;
    const auto seconds = [] (double s) { return (size_t) (s * kRate); };

    // Re-voicing: an open E chord on the guitar becomes a close charango voicing.
    {
        const std::uint8_t notes[] { 40, 45, 50, 55, 59, 64 };
        std::uint8_t out[6] {};
        const int n = C::foldVoicing (notes, 6, out);
        CHECK (n == 5);
        const std::uint8_t expected[] { 64, 67, 69, 71, 74 };
        CHECK (std::equal (out, out + 5, expected));
    }

    // Procesado: the guitar an octave up.
    {
        BlockRig rig (BlockType::Charango);
        rig.set (C::Octavado, 0.0f);
        rig.set (C::Courses, 0.0f);
        rig.set (C::Body, 0.0f);
        rig.set (C::Sustain, 100.0f);
        rig.block->reset();
        auto l = sine (220.0, 0.6, 0.2f), r = l;
        rig.run (l, r);
        const double f = estimateFrequency (l, seconds (0.2), seconds (0.6));
        std::printf ("  procesado, A3 in: %.1f Hz out\n", f);
        CHECK_NEAR (f, 440.0, 6.0);
    }

    for (const int mode : { 0, 1 })
    {
        BlockRig rig (BlockType::Charango);
        rig.set (C::Mode, (float) mode);
        rig.block->reset();
        auto* charango = dynamic_cast<CharangoBlock*> (rig.block);
        std::vector<float> l ((size_t) (2.2 * kRate), 0.0f);
        addPluckedChord (l, 0.1, { 48, 52, 55, 60, 64 });
        addPluckedChord (l, 1.2, { 48, 52, 55, 60, 64 });
        const double in = rmsDb (l, seconds (0.1), seconds (1.1));
        auto r = l;

        // First half, then read the voicing while the chord rings.
        std::vector<float> l1 (l.begin(), l.begin() + (long) seconds (0.6)), r1 (l1);
        rig.run (l1, r1);
        const auto voicing = charango->getHeldNotesText();
        std::vector<float> l2 (l.begin() + (long) seconds (0.6), l.end()), r2 (l2);
        rig.run (l2, r2);
        std::copy (l1.begin(), l1.end(), l.begin());
        std::copy (l2.begin(), l2.end(), l.begin() + (long) l1.size());
        std::copy (r1.begin(), r1.end(), r.begin());
        std::copy (r2.begin(), r2.end(), r.begin() + (long) r1.size());

        bool finite = true;
        float peak = 0.0f;
        size_t first = l.size();
        for (size_t i = 0; i < l.size(); ++i)
        {
            finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
            peak = std::max ({ peak, std::abs (l[i]), std::abs (r[i]) });
            if (first == l.size() && std::abs (l[i]) > 0.005f)
                first = i;
        }
        const double out = 0.5 * (rmsDb (l, seconds (0.1), seconds (1.1)) + rmsDb (r, seconds (0.1), seconds (1.1)));
        const double before = rmsDb (l, seconds (1.0), seconds (1.2)), after = rmsDb (l, seconds (1.25), seconds (1.45));
        std::printf ("  %s: in %.1f dB, out %.1f dB rms, peak %.1f dB, first sound %.0f ms after the pick, "
                     "re-pick %.1f -> %.1f dB, voicing \"%s\"\n",
                     mode == 0 ? "procesado" : "modelado", in, out, 20.0 * std::log10 (peak + 1e-9),
                     1000.0 * ((double) first / kRate - 0.1), before, after, voicing.c_str());
        CHECK (finite);
        CHECK (std::abs (out - in) < 6.0);
        CHECK (peak < 0.7f);
        if (mode == 1)
        {
            CHECK (voicing == "E4 G4 C5");
            CHECK ((double) first / kRate - 0.1 < 0.12);
            CHECK (after > before + 4.0);
        }
    }
}

void testAdvancedGate()
{
    std::puts ("noise gate: advanced (multiband) vs classic on a ringing note with hiss");
    // A note whose fundamental rings while its highs die fast, over constant hiss.
    std::vector<float> input ((size_t) (2.2 * kRate), 0.0f);
    std::mt19937 rng (7);
    std::normal_distribution<float> noise (0.0f, 0.0008f);   // ~-62 dBFS hiss
    for (size_t i = 0; i < input.size(); ++i)
    {
        input[i] = noise (rng);
        const double t = (double) i / kRate - 0.1;
        if (t >= 0.0)
        {
            input[i] += (float) (0.3 * std::exp (-t / 1.2) * std::sin (2.0 * 3.141592653589793 * 110.0 * t));
            for (double hz : { 2200.0, 3300.0, 4400.0 })
                input[i] += (float) (0.04 * std::exp (-t / 0.12) * std::sin (2.0 * 3.141592653589793 * hz * t));
        }
    }
    auto band = [] (std::vector<float> x, bool high)
    {
        std::array<Biquad, 2> f;
        for (auto& b : f) high ? b.setHighPass (kRate, 5000.0f) : b.setLowPass (kRate, 400.0f);
        for (auto& v : x) v = f[1].process (f[0].process (v));
        return x;
    };
    auto runGate = [&] (float mode)
    {
        BlockRig rig (BlockType::NoiseGate);
        rig.set (NoiseGateBlock::ThresholdDb, -48.0f);
        rig.set (NoiseGateBlock::Mode, mode);
        auto l = input, r = input;
        rig.run (l, r);
        return l;
    };
    const auto classic = runGate (0.0f), advanced = runGate (1.0f);
    const size_t from = (size_t) (1.0 * kRate), to = (size_t) (2.0 * kRate);   // the ringing tail
    const double hissClassic = rmsDb (band (classic, true), from, to), hissAdvanced = rmsDb (band (advanced, true), from, to);
    const double bodyClassic = rmsDb (band (classic, false), from, to), bodyAdvanced = rmsDb (band (advanced, false), from, to);
    std::printf ("  tail hiss >5 kHz: classic %.1f dB, advanced %.1f dB   body <400 Hz: %.1f / %.1f dB\n",
                 hissClassic, hissAdvanced, bodyClassic, bodyAdvanced);
    CHECK (hissAdvanced < hissClassic - 15.0);
    CHECK (std::abs (bodyAdvanced - bodyClassic) < 1.0);

    // Hiss alone: closed in both modes.
    std::vector<float> hiss ((size_t) (0.5 * kRate));
    for (auto& v : hiss) v = noise (rng);
    for (float mode : { 0.0f, 1.0f })
    {
        BlockRig rig (BlockType::NoiseGate);
        rig.set (NoiseGateBlock::ThresholdDb, -48.0f);
        rig.set (NoiseGateBlock::Mode, mode);
        auto l = hiss, r = hiss;
        rig.run (l, r);
        CHECK (rmsDb (l, l.size() / 2, l.size()) < -120.0);
    }

    // Loud, bright signal: the advanced split sums back exactly (transparent while open).
    {
        BlockRig rig (BlockType::NoiseGate);
        rig.set (NoiseGateBlock::Mode, 1.0f);
        auto l = sine (110.0, 0.5, 0.3f);
        const auto bright = sine (3300.0, 0.5, 0.2f);
        for (size_t i = 0; i < l.size(); ++i) l[i] += bright[i];
        const auto ref = l;
        auto r = l;
        rig.run (l, r);
        double err = 0.0;
        for (size_t i = l.size() / 2; i < l.size(); ++i) err = std::max (err, (double) std::abs (l[i] - ref[i]));
        std::printf ("  open advanced gate: max error %.2e\n", err);
        CHECK (err < 1.0e-4);
    }
}

void testEqListen()
{
    std::puts ("parametric EQ: band listen (FabFilter style)");
    using EQ = ParametricEqBlock;
    BlockRig rig (BlockType::ParametricEq);
    auto* eq = dynamic_cast<EQ*> (rig.block);
    rig.set (EQ::bandParam (2, EQ::BandOn), 1.0f);
    rig.set (EQ::bandParam (2, EQ::BandFreq), 1000.0f);
    rig.set (EQ::bandParam (2, EQ::BandGain), 6.0f);
    rig.set (EQ::bandParam (2, EQ::BandQ), 4.0f);
    rig.block->prepare (kRate, kBlock);
    auto gainAt = [&] (double hz)
    {
        auto l = sine (hz, 0.5), r = l;
        rig.run (l, r);
        return rmsDb (l, 12000, 24000) - rmsDb (sine (hz, 0.5), 12000, 24000);
    };
    const double normal150 = gainAt (150.0);
    eq->setListenBand (2);
    const double centre = gainAt (1000.0), off = gainAt (150.0);
    eq->setListenBand (-1);
    const double back = gainAt (150.0);
    std::printf ("  bell 1 kHz listen: %.1f dB at 1 kHz, %.1f dB at 150 Hz; off again: %.2f dB (was %.2f)\n", centre, off, back, normal150);
    CHECK (std::abs (centre) < 1.0);   // the band region at unity (its boost is not applied)
    CHECK (off < -20.0);
    CHECK (std::abs (back - normal150) < 0.2);

    // Low cut: listening plays what it removes (below the corner).
    rig.set (EQ::bandParam (2, EQ::BandType), (float) EqBandType::LowCut);
    rig.set (EQ::bandParam (2, EQ::BandFreq), 300.0f);
    eq->setListenBand (2);
    const double below = gainAt (80.0), above = gainAt (3000.0);
    eq->setListenBand (-1);
    std::printf ("  low cut 300 Hz listen: %.1f dB at 80 Hz, %.1f dB at 3 kHz\n", below, above);
    CHECK (below > -2.0);
    CHECK (above < -40.0);
}

void testPitchBlockUnityIsTransparent()
{
    std::puts ("pitch block unity");
    PitchShifterBlock block (1);
    block.prepare (kRate, kBlock);
    std::vector<float> l (kBlock), r (kBlock);
    for (int i = 0; i < kBlock; ++i) l[(size_t) i] = r[(size_t) i] = std::sin ((float) i * 0.05f);
    const auto ref = l;
    block.process (l.data(), r.data(), kBlock);
    CHECK_NEAR (l[100], ref[100], 1e-6);   // zero latency at 0 st
}
} // namespace



int main()
{
    testSerialChainInPlace();
    testParallelSplitMix();
    testLatencyCompensation();
    testValidationAndPruning();
    testBypassCrossfade();
    testScenes();
    testPitchBlockUnityIsTransparent();
    testPitchDetector();
    testRealtimeTracker();
    testGuitarToMidi();
    testGuitarSynth();
    testGuitarSynthTexture();
    testPolyNoteDetector();
    testInstrumentSynth();
    testSitar();
    testSitarModelled();
    testCharango();
    testAdvancedGate();
    testEqListen();
    testAllBlocksStress();
    testBlockBehaviour();
    testRateBridge (44100.0);
    testRateBridge (96000.0);
    for (float st : { 12.0f, 7.0f, 3.0f, -5.0f, -12.0f, 0.5f })
        testPitchShifter (st);
    for (double f0 : { 30.87, 41.20, 46.25 })
        for (float st : { -2.0f, -5.0f })
            testPitchShifterBass (f0, st);
    for (float st : { -2.0f, -5.0f, 5.0f })
        testPitchShifterGuitarChord (st);

    std::printf (failures == 0 ? "\nALL TESTS PASSED\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
