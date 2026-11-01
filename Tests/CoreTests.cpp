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

#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <vector>

using namespace kumamp;

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
    testRateBridge (44100.0);
    testRateBridge (96000.0);
    for (float st : { 12.0f, 7.0f, 3.0f, -5.0f, -12.0f, 0.5f })
        testPitchShifter (st);

    std::printf (failures == 0 ? "\nALL TESTS PASSED\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
