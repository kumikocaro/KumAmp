#include "GridEngine.h"

#include "../Graph/BlockFactory.h"
#include "../Graph/DSPBlock.h"
#include "../DSP/Amp/NamAmpBlock.h"
#include "../DSP/Cab/FactoryCabs.h"
#include "../DSP/Cab/IrLoaderBlock.h"
#include "../DSP/JuceBlocks.h"
#include "../DSP/Dynamics/DynamicsBlocks.h"
#include "../DSP/Modulation/ModulationBlocks.h"
#include "../DSP/Pitch/GuitarSynthBlock.h"
#include "../DSP/Pitch/PitchShifterBlock.h"
#include "../DSP/Pitch/PreFxBlocks.h"
#include "../DSP/Time/DelayBlocks.h"
#include "../DSP/Time/ReverbBlocks.h"
#include "../DSP/Routing/RoutingParams.h"
#include "../DSP/Utility/VolumeBlock.h"

#include <algorithm>
#include <set>

namespace kumamp
{
namespace
{
    constexpr int kMaxChannels = 8;

    const juce::Identifier kGridTag    { "KumAmpGrid" };
    const juce::Identifier kStoresTag  { "Stores" };
    const juce::Identifier kStoreTag   { "Store" };
    const juce::Identifier kNodesTag   { "Nodes" };
    const juce::Identifier kNodeTag    { "Node" };
    const juce::Identifier kScenesTag  { "Scenes" };
    const juce::Identifier kSceneTag   { "Scene" };
    const juce::Identifier kSnapTag    { "Snap" };
    const juce::Identifier kMacrosTag  { "Macros" };
    const juce::Identifier kMacroTag   { "Macro" };

    juce::Identifier paramKey (const ParamInfo& info) { return juce::Identifier (juce::String ("p_") + info.id); }
} // namespace

GridEngine::GridEngine()
{
    registerJuceBlocks();
}
GridEngine::~GridEngine() = default;

//==============================================================================
void GridEngine::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;
    maxBlock   = std::max (1, maxBlockSize);
    prepared   = true;

    for (auto& [id, store] : stores)
        if (auto* block = dynamic_cast<DSPBlock*> (store.get()))
            block->prepare (sampleRate, maxBlock);

    fadeLength = std::max (1, (int) (sampleRate * 0.005));
    fade = Fade::Running;
    fadePos = 0;

    auto result = RenderPlan::compile (model, makeResolver(), maxBlock);
    jassert (result.plan != nullptr);
    latency.store (result.plan != nullptr ? result.plan->getLatencySamples() : 0);

    plans.resetImmediately (std::move (result.plan));
    sceneBanks.resetImmediately (std::make_unique<SceneBank> (editedScenes));

    appliedScene = requestedScene.load();
    activeScene.store (appliedScene);
}

//==============================================================================
void GridEngine::setRequestedScene (int sceneIndex) noexcept
{
    requestedScene.store (std::clamp (sceneIndex, 0, kNumScenes - 1), std::memory_order_relaxed);
}

void GridEngine::setMacroValue (int macro, float normalisedValue) noexcept
{
    if (macro >= 0 && macro < kNumMacros)
        macroValues[(size_t) macro] = std::clamp (normalisedValue, 0.0f, 1.0f);
}

void GridEngine::applyScene (int sceneIndex, const RenderPlan* plan) noexcept
{
    const SceneBank* bank = sceneBanks.acquire();
    if (bank != nullptr && plan != nullptr)
        SceneBank::apply (bank->scenes[(size_t) sceneIndex], [plan] (StoreId id) { return plan->findStore (id); });

    appliedScene = sceneIndex;
    activeScene.store (sceneIndex, std::memory_order_relaxed);
    events.push ({ EngineEvent::Type::SceneChanged, sceneIndex });
}

void GridEngine::applyMacros (const RenderPlan* plan) noexcept
{
    for (size_t m = 0; m < (size_t) kNumMacros; ++m)
    {
        const auto& target = macros[m];
        const StoreId id = target.store.load (std::memory_order_relaxed);
        if (id == kInvalidStore)
            continue;

        const float value = macroValues[m];
        const int version = target.version.load (std::memory_order_acquire);

        if (version != lastMacroVersions[m])
        {
            // Newly assigned: arm without jumping, the pedal takes over on its next move.
            lastMacroVersions[m] = version;
            lastMacroValues[m] = value;
            continue;
        }

        if (exactlyEqual (value, lastMacroValues[m]))
            continue;

        lastMacroValues[m] = value;
        if (ParamStore* store = plan != nullptr ? plan->findStore (id) : nullptr)
        {
            const float lo = target.minValue.load (std::memory_order_relaxed);
            const float hi = target.maxValue.load (std::memory_order_relaxed);
            store->setParam (target.param.load (std::memory_order_relaxed), lo + value * (hi - lo));
        }
    }
}

void GridEngine::applyFade (float* const* outputs, int numOutputs, int numSamples) noexcept
{
    if (fade == Fade::Running)
        return;

    const float inv = 1.0f / (float) fadeLength;
    for (int i = 0; i < numSamples; ++i)
    {
        float g = 1.0f;
        switch (fade)
        {
            case Fade::FadingOut:
                g = 1.0f - (float) fadePos * inv;
                if (++fadePos >= fadeLength) fade = Fade::Muted;
                break;
            case Fade::Muted:
                g = 0.0f;
                break;
            case Fade::FadingIn:
                g = (float) fadePos * inv;
                if (++fadePos >= fadeLength) fade = Fade::Running;
                break;
            case Fade::Running:
                break;
        }

        for (int ch = 0; ch < numOutputs; ++ch)
            outputs[ch][i] *= g;
    }
}

void GridEngine::process (const float* const* inputs, int numInputs,
                          float* const* outputs, int numOutputs, int numSamples) noexcept
{
    numInputs  = std::min (numInputs, kMaxChannels);
    numOutputs = std::min (numOutputs, kMaxChannels);

    RenderPlan* plan = plans.getCurrent();
    if (plan == nullptr)
        plan = plans.acquire();   // very first plan: no fade needed

    EngineCommand command;
    while (commands.pop (command))
        if (command.type == EngineCommand::Type::ResetAllBlocks && plan != nullptr)
            plan->resetState();

    if (const int requested = requestedScene.load (std::memory_order_relaxed); requested != appliedScene)
        applyScene (requested, plan);

    applyMacros (plan);

    const float* in[kMaxChannels] {};
    float* out[kMaxChannels] {};

    for (int offset = 0; offset < numSamples; offset += maxBlock)
    {
        const int n = std::min (maxBlock, numSamples - offset);
        for (int ch = 0; ch < numInputs; ++ch)  in[ch]  = inputs[ch] + offset;
        for (int ch = 0; ch < numOutputs; ++ch) out[ch] = outputs[ch] + offset;

        if (fade == Fade::Running && plans.hasPending())
        {
            fade = Fade::FadingOut;
            fadePos = 0;
        }

        if (plan != nullptr)
        {
            plan->process (in, numInputs, out, numOutputs, n);
        }
        else
        {
            for (int ch = 0; ch < numOutputs; ++ch)
                if (numInputs > 0 && out[ch] != in[std::min (ch, numInputs - 1)])
                    std::copy (in[std::min (ch, numInputs - 1)], in[std::min (ch, numInputs - 1)] + n, out[ch]);
        }

        applyFade (out, numOutputs, n);

        if (fade == Fade::Muted)
        {
            RenderPlan* next = plans.acquire();
            if (next != plan)
            {
                plan = next;
                events.push ({ EngineEvent::Type::PlanActivated, plan != nullptr ? plan->getLatencySamples() : 0 });
            }
            fade = Fade::FadingIn;
            fadePos = 0;
        }
    }
}

//==============================================================================
RenderPlan::StoreResolver GridEngine::makeResolver() const
{
    return [this] (StoreId id) -> std::shared_ptr<ParamStore>
    {
        const auto it = stores.find (id);
        return it != stores.end() ? it->second : nullptr;
    };
}

std::shared_ptr<ParamStore> GridEngine::createStore (BlockType type, const juce::String& name)
{
    auto store = kumamp::createStore (type, nextStoreId);
    if (store == nullptr)
        return nullptr;

    ++nextStoreId;
    store->setName ((name.isNotEmpty() ? name : juce::String (getBlockTypeInfo (type).displayName)).toStdString());

    if (auto* block = dynamic_cast<DSPBlock*> (store.get()))
    {
        block->setTempoSource (&tempoBpm);
        block->setDetectionSource (&detectionGain);
        if (prepared)
            block->prepare (sampleRate, maxBlock);
    }

    stores[store->getId()] = store;
    return store;
}

std::shared_ptr<ParamStore> GridEngine::getStore (StoreId id) const
{
    const auto it = stores.find (id);
    return it != stores.end() ? it->second : nullptr;
}

bool GridEngine::commitModel (GridModel newModel, juce::String* error)
{
    newModel.autoLayout();

    if (! prepared)
    {
        std::string message;
        if (! newModel.validate (&message))
        {
            if (error != nullptr) *error = message;
            return false;
        }
    }
    else
    {
        auto result = RenderPlan::compile (newModel, makeResolver(), maxBlock);
        if (result.plan == nullptr)
        {
            if (error != nullptr) *error = result.error;
            return false;
        }

        latency.store (result.plan->getLatencySamples());
        plans.publish (std::move (result.plan));
    }

    model = std::move (newModel);
    ++modelVersion;
    pruneUnusedStores();
    return true;
}

NodeId GridEngine::insertBlock (PortRef from, NodeId to, BlockType type, bool parallel, juce::String* error)
{
    GridModel m = model;
    const auto* destination = m.findNode (to);
    if (destination == nullptr || std::find (destination->inputs.begin(), destination->inputs.end(), from) == destination->inputs.end())
    {
        if (error != nullptr) *error = "Conexion inexistente";
        return kInvalidNode;
    }

    auto store = createStore (type);
    if (store == nullptr)
    {
        if (error != nullptr) *error = "Tipo de bloque no disponible";
        return kInvalidNode;
    }

    const NodeId block = m.addNode (NodeKind::Block, store->getId());
    const auto before = snapshot();

    if (! parallel)
    {
        m.findNode (block)->inputs = { from };
        m.rewire (from, { block, 0 }, to);
    }
    else
    {
        auto split = createStore (BlockType::Splitter, "Split");
        auto mixer = createStore (BlockType::Mixer, "Mix");
        // Two correlated branches summed at 0 dB would jump +6 dB: start at -6 dB each.
        mixer->setParam (MixerParam::inputLevelDb (0), -6.0f);
        mixer->setParam (MixerParam::inputLevelDb (1), -6.0f);

        const NodeId s = m.addNode (NodeKind::Split, split->getId(), 0, 0, 2);
        const NodeId x = m.addNode (NodeKind::Mix, mixer->getId());
        // Dry branch on port 0 (stays on the main row), the new block on port 1 (own row).
        m.findNode (s)->inputs = { from };
        m.findNode (block)->inputs = { { s, 1 } };
        m.findNode (x)->inputs = { { s, 0 }, { block, 0 } };
        m.rewire (from, { x, 0 }, to);
    }

    if (! commitModel (std::move (m), error))
    {
        pruneUnusedStores();   // drop the stores created for the failed edit
        return kInvalidNode;
    }
    pushUndo (before);
    return block;
}

bool GridEngine::removeBlock (NodeId node, juce::String* error)
{
    GridModel m = model;
    if (! m.removeBlockAndBridge (node))
    {
        if (error != nullptr) *error = "Solo se pueden quitar bloques de audio";
        return false;
    }
    m.collapseEmptyParallels();

    auto before = snapshot();
    if (! commitModel (std::move (m), error))
        return false;
    pushUndo (std::move (before));
    return true;
}

bool GridEngine::moveBlock (NodeId node, PortRef from, NodeId to, juce::String* error)
{
    const auto* original = model.findNode (node);
    if (original == nullptr || original->kind != NodeKind::Block)
    {
        if (error != nullptr) *error = "Solo se pueden mover bloques de audio";
        return false;
    }

    // Dropping a block on its own input or output cable changes nothing.
    if (from.node == node || to == node)
        return true;

    GridModel m = model;
    GridNode moved = *original;
    if (! m.removeBlockAndBridge (node))
        return false;

    const auto* destination = m.findNode (to);
    if (destination == nullptr || std::find (destination->inputs.begin(), destination->inputs.end(), from) == destination->inputs.end())
    {
        if (error != nullptr) *error = "Destino no valido";
        return false;
    }

    moved.inputs = { from };
    m.addNodeWithId (moved);
    m.rewire (from, { node, 0 }, to);
    m.collapseEmptyParallels();

    auto before = snapshot();
    if (! commitModel (std::move (m), error))
        return false;
    pushUndo (std::move (before));
    return true;
}

bool GridEngine::replaceBlock (NodeId node, BlockType type, juce::String* error)
{
    const auto* original = model.findNode (node);
    if (original == nullptr || original->kind != NodeKind::Block)
    {
        if (error != nullptr) *error = "Solo se pueden cambiar bloques de audio";
        return false;
    }
    const StoreId oldId = original->store;   // `original` points into the model replaced below
    const auto oldStore = getStore (oldId);
    if (oldStore != nullptr && oldStore->getType() == type)
        return true;

    auto store = createStore (type);
    if (store == nullptr)
    {
        if (error != nullptr) *error = "Tipo de bloque no disponible";
        return false;
    }
    if (oldStore != nullptr)
        store->setBypassed (oldStore->isBypassed());

    GridModel m = model;
    m.findNode (node)->store = store->getId();

    auto before = snapshot();
    if (! commitModel (std::move (m), error))
    {
        pruneUnusedStores();
        return false;
    }

    // Scenes: the new block follows the old one's on / off in each scene. The old entry is
    // kept (undo brings that block back).
    for (auto& scene : editedScenes.scenes)
    {
        if (! scene.valid)
            continue;
        for (int i = 0; i < scene.numStores && scene.numStores < kMaxStores; ++i)
            if (scene.stores[(size_t) i].id == oldId)
            {
                StoreSnapshot snap;
                snap.id = store->getId();
                snap.bypassed = scene.stores[(size_t) i].bypassed;
                snap.numParams = std::min (store->getNumParams(), kMaxParams);
                for (int p = 0; p < snap.numParams; ++p)
                    snap.values[(size_t) p] = store->getParam (p);
                scene.stores[(size_t) scene.numStores++] = snap;
                break;
            }
    }
    publishSceneBank();
    pushUndo (std::move (before));
    return true;
}

GridEngine::UndoEntry GridEngine::snapshot() const
{
    UndoEntry entry { model, {} };
    for (const auto& [id, store] : stores)
        entry.stores.push_back (store);
    return entry;
}

bool GridEngine::restore (UndoEntry entry)
{
    for (auto& store : entry.stores)
        stores[store->getId()] = store;
    nextStoreId = std::max (nextStoreId, stores.empty() ? 1u : stores.rbegin()->first + 1);
    return commitModel (std::move (entry.model));
}

bool GridEngine::undo()
{
    if (undoStack.empty())
        return false;
    auto current = snapshot();
    auto previous = std::move (undoStack.back());
    undoStack.pop_back();
    if (! restore (std::move (previous)))
        return false;
    redoStack.push_back (std::move (current));
    return true;
}

bool GridEngine::redo()
{
    if (redoStack.empty())
        return false;
    auto current = snapshot();
    auto next = std::move (redoStack.back());
    redoStack.pop_back();
    if (! restore (std::move (next)))
        return false;
    undoStack.push_back (std::move (current));
    return true;
}

void GridEngine::pushUndo (UndoEntry entry)
{
    undoStack.push_back (std::move (entry));
    if (undoStack.size() > 50)
        undoStack.erase (undoStack.begin());
    redoStack.clear();
}

juce::String GridEngine::getSceneName (int scene) const
{
    return scene >= 0 && scene < kNumScenes ? sceneNames[(size_t) scene] : juce::String();
}

void GridEngine::setSceneName (int scene, const juce::String& name)
{
    if (scene >= 0 && scene < kNumScenes)
        sceneNames[(size_t) scene] = name;
}

void GridEngine::pruneUnusedStores()
{
    std::set<StoreId> used;
    for (const auto& node : model.getNodes())
        if (node.store != kInvalidStore)
            used.insert (node.store);

    // Blocks still referenced by a live plan stay alive through its shared_ptrs.
    for (auto it = stores.begin(); it != stores.end();)
        it = used.count (it->first) == 0 ? stores.erase (it) : std::next (it);
}

void GridEngine::publishSceneBank()
{
    sceneBanks.publish (std::make_unique<SceneBank> (editedScenes));
}

void GridEngine::storeScene (int sceneIndex)
{
    sceneIndex = std::clamp (sceneIndex, 0, kNumScenes - 1);

    std::vector<const ParamStore*> current;
    for (const auto& node : model.getNodes())
        if (auto it = stores.find (node.store); it != stores.end())
            current.push_back (it->second.get());

    editedScenes.scenes[(size_t) sceneIndex] = SceneBank::capture (current);
    publishSceneBank();
}

void GridEngine::assignMacro (int macro, StoreId store, int param, float minValue, float maxValue)
{
    if (macro < 0 || macro >= kNumMacros)
        return;

    auto& target = macros[(size_t) macro];
    target.store.store (store);
    target.param.store (param);
    target.minValue.store (minValue);
    target.maxValue.store (maxValue);
    target.version.fetch_add (1, std::memory_order_release);
}

void GridEngine::runMessageThreadTasks()
{
    plans.collectGarbage();
    sceneBanks.collectGarbage();

    for (auto& [id, store] : stores)
        if (auto* block = dynamic_cast<DSPBlock*> (store.get()))
            block->collectGarbage();

    updateCabLinks();
}

bool GridEngine::hasEndToEndUpstream (const GridNode& start) const
{
    std::vector<const GridNode*> stack { &start };
    std::set<NodeId> visited;

    while (! stack.empty())
    {
        const auto* node = stack.back();
        stack.pop_back();

        for (const auto& in : node->inputs)
        {
            const auto* source = model.findNode (in.node);
            if (source == nullptr || ! visited.insert (source->id).second)
                continue;

            if (source->kind == NodeKind::Block)
                if (auto* nam = dynamic_cast<NamAmpBlock*> (getStore (source->store).get()))
                    if (! nam->isBypassed() && nam->getEffectiveProfileKind() == ProfileKind::EndToEnd)
                        return true;

            stack.push_back (source);
        }
    }
    return false;
}

void GridEngine::updateCabLinks()
{
    for (const auto& node : model.getNodes())
        if (node.kind == NodeKind::Block)
            if (auto* ir = dynamic_cast<IrLoaderBlock*> (getStore (node.store).get()))
                ir->setAutoBypassed (hasEndToEndUpstream (node));
}

//==============================================================================
void GridEngine::loadDefaultPreset()
{
    stores.clear();
    nextStoreId = 1;
    editedScenes = {};
    undoStack.clear();
    redoStack.clear();
    sceneNames = { "Crunch Ambient", "Lead", "Chorus Clean", "Octave Fuzz" };

    auto synth  = createStore (BlockType::GuitarSynth, "Synth");
    auto wah    = createStore (BlockType::Wah, "Wah");
    auto oct    = createStore (BlockType::Octaver, "Oct");
    auto drive  = createStore (BlockType::Overdrive, "Drive");
    auto amp    = createStore (BlockType::NamAmp, "Amp");
    auto cab    = createStore (BlockType::IrLoader, "Cab");
    auto eq     = createStore (BlockType::ParametricEq, "EQ");
    auto chorus = createStore (BlockType::Chorus, "Chorus");
    auto delay  = createStore (BlockType::DigitalDelay, "Delay");
    auto reverb = createStore (BlockType::PlateReverb, "Reverb");

    // IN -> Synth -> Wah -> Oct -> Drive -> Amp -> Cab -> EQ -> Chorus -> Delay -> Reverb -> OUT
    // (the synth first: its note detection needs the clean guitar)
    GridModel m;
    NodeId previous = m.addNode (NodeKind::Input);
    for (const auto& store : { synth, wah, oct, drive, amp, cab, eq, chorus, delay, reverb })
    {
        const NodeId node = m.addNode (NodeKind::Block, store->getId());
        m.connect ({ previous, 0 }, node);
        previous = node;
    }
    m.connect ({ previous, 0 }, m.addNode (NodeKind::Output));
    commitModel (std::move (m));

    // The cab has KumAmp's own 4x12 ready but starts off, like the drive (it also steps aside
    // by itself for End-to-End NAM profiles once switched on).
    if (auto* ir = dynamic_cast<DSPBlock*> (cab.get()))
        ir->loadFile (0, cabs::pathFor ("4x12-moderno", "din-centro"));

    // The synth waits as an instrument layer (strings under the guitar) for when it is switched on.
    synth->setParam (GuitarSynthBlock::Mode, (float) GuitarSynthBlock::Voice::Instrument);
    synth->setParam (GuitarSynthBlock::Sound, 5.0f);   // Cuerdas
    synth->setParam (GuitarSynthBlock::Mix, 40.0f);

    // Synth, drive, cab, delay and reverb start bypassed in every scene (dry starting point);
    // their settings are already dialled in per scene for when they are switched on.
    auto setScene = [&] (bool wahOn, bool octOn, bool chorusOn, float delayMix, float delayMs,
                         float reverbMix, float midBoostDb)
    {
        synth->setBypassed (true);
        wah->setBypassed (! wahOn);
        oct->setBypassed (! octOn);
        chorus->setBypassed (! chorusOn);
        drive->setBypassed (true);
        cab->setBypassed (true);
        delay->setBypassed (true);
        reverb->setBypassed (true);
        delay->setParam (DigitalDelayBlock::Mix, delayMix);
        delay->setParam (DigitalDelayBlock::TimeMs, delayMs);
        delay->setParam (DigitalDelayBlock::Feedback, 30.0f);
        reverb->setParam (PlateReverbBlock::Mix, reverbMix);
        using EQ = ParametricEqBlock;
        eq->setParam (EQ::bandParam (0, EQ::BandOn), 1.0f);          // low cut 80 Hz, 24 dB/oct
        eq->setParam (EQ::bandParam (2, EQ::BandOn), 1.0f);          // mid bell for the lead push
        eq->setParam (EQ::bandParam (2, EQ::BandFreq), 800.0f);
        eq->setParam (EQ::bandParam (2, EQ::BandGain), midBoostDb);
        oct->setParam (OctaverBlock::SubDb, -3.0f);
        oct->setParam (OctaverBlock::UpDb, -12.0f);
    };

    setScene (false, false, false, 25.0f, 420.0f, 22.0f, 0.0f);   // A: Crunch Ambient
    storeScene (0);
    setScene (false, false, false, 32.0f, 480.0f, 18.0f, 3.0f);   // B: Lead (mid push)
    storeScene (1);
    setScene (false, false, true, 25.0f, 420.0f, 30.0f, 0.0f);    // C: Chorus Clean
    storeScene (2);
    setScene (false, true, false, 20.0f, 380.0f, 20.0f, 0.0f);    // D: Octave Fuzz
    storeScene (3);

    SceneBank::apply (editedScenes.scenes[0], [this] (StoreId id) { return getStore (id).get(); });

    // Macro 1 = expression pedal on the wah.
    assignMacro (0, wah->getId(), WahBlock::Position, 0.0f, 100.0f);
    juce::ignoreUnused (amp);
}

//==============================================================================
juce::ValueTree GridEngine::saveState() const
{
    juce::ValueTree root (kGridTag);
    root.setProperty ("version", 1, nullptr);

    juce::ValueTree storesTree (kStoresTag);
    for (const auto& [id, store] : stores)
    {
        juce::ValueTree s (kStoreTag);
        s.setProperty ("id", (int) id, nullptr);
        s.setProperty ("type", getBlockTypeInfo (store->getType()).id, nullptr);
        s.setProperty ("name", juce::String (store->getName()), nullptr);
        s.setProperty ("bypass", store->isBypassed(), nullptr);
        for (int p = 0; p < store->getNumParams(); ++p)
            s.setProperty (paramKey (store->getParamInfo (p)), store->getParam (p), nullptr);
        if (auto* block = dynamic_cast<const DSPBlock*> (store.get()))
            for (int f = 0; f < block->getNumFileSlots(); ++f)
                if (const auto path = block->getFilePath (f); ! path.empty())
                    s.setProperty ("file_" + juce::String (f), juce::String (path), nullptr);
        storesTree.appendChild (s, nullptr);
    }
    root.appendChild (storesTree, nullptr);

    juce::ValueTree nodesTree (kNodesTag);
    for (const auto& node : model.getNodes())
    {
        juce::ValueTree n (kNodeTag);
        n.setProperty ("id", (int) node.id, nullptr);
        n.setProperty ("kind", (int) node.kind, nullptr);
        n.setProperty ("store", (int) node.store, nullptr);
        n.setProperty ("row", node.row, nullptr);
        n.setProperty ("col", node.column, nullptr);
        n.setProperty ("outputs", node.numOutputs, nullptr);

        juce::StringArray inputs;
        for (const auto& in : node.inputs)
            inputs.add (juce::String ((int) in.node) + ":" + juce::String (in.port));
        n.setProperty ("inputs", inputs.joinIntoString (" "), nullptr);
        nodesTree.appendChild (n, nullptr);
    }
    root.appendChild (nodesTree, nullptr);

    juce::ValueTree scenesTree (kScenesTag);
    for (int i = 0; i < kNumScenes; ++i)
    {
        const auto& scene = editedScenes.scenes[(size_t) i];
        juce::ValueTree sc (kSceneTag);
        sc.setProperty ("index", i, nullptr);
        sc.setProperty ("valid", scene.valid, nullptr);
        sc.setProperty ("name", sceneNames[(size_t) i], nullptr);
        for (int k = 0; k < scene.numStores; ++k)
        {
            const auto& snap = scene.stores[(size_t) k];
            juce::StringArray values;
            for (int p = 0; p < snap.numParams; ++p)
                values.add (juce::String (snap.values[(size_t) p]));

            juce::ValueTree sn (kSnapTag);
            sn.setProperty ("store", (int) snap.id, nullptr);
            sn.setProperty ("bypass", snap.bypassed, nullptr);
            sn.setProperty ("values", values.joinIntoString (" "), nullptr);
            sc.appendChild (sn, nullptr);
        }
        scenesTree.appendChild (sc, nullptr);
    }
    root.appendChild (scenesTree, nullptr);

    juce::ValueTree macrosTree (kMacrosTag);
    for (int i = 0; i < kNumMacros; ++i)
    {
        const auto& t = macros[(size_t) i];
        juce::ValueTree mt (kMacroTag);
        mt.setProperty ("index", i, nullptr);
        mt.setProperty ("store", (int) t.store.load(), nullptr);
        mt.setProperty ("param", t.param.load(), nullptr);
        mt.setProperty ("min", t.minValue.load(), nullptr);
        mt.setProperty ("max", t.maxValue.load(), nullptr);
        macrosTree.appendChild (mt, nullptr);
    }
    root.appendChild (macrosTree, nullptr);

    return root;
}

void GridEngine::loadState (const juce::ValueTree& state)
{
    if (! state.hasType (kGridTag))
        return;

    stores.clear();
    nextStoreId = 1;
    editedScenes = {};
    undoStack.clear();
    redoStack.clear();
    sceneNames = {};

    for (const auto& s : state.getChildWithName (kStoresTag))
    {
        const auto id = (StoreId) (int) s.getProperty ("id");
        const auto type = blockTypeFromId (s.getProperty ("type").toString().toStdString());
        auto store = kumamp::createStore (type, id);
        if (id == kInvalidStore || store == nullptr)
            continue;

        store->setName (s.getProperty ("name").toString().toStdString());
        store->setBypassed ((bool) s.getProperty ("bypass"));
        for (int p = 0; p < store->getNumParams(); ++p)
            if (const auto key = paramKey (store->getParamInfo (p)); s.hasProperty (key))
                store->setParam (p, (float) s.getProperty (key));

        if (auto* block = dynamic_cast<DSPBlock*> (store.get()))
        {
            block->setTempoSource (&tempoBpm);
            block->setDetectionSource (&detectionGain);
            for (int f = 0; f < block->getNumFileSlots(); ++f)
                if (const auto key = "file_" + juce::String (f); s.hasProperty (key))
                    block->loadFile (f, s.getProperty (key).toString().toStdString());

            if (prepared)
                block->prepare (sampleRate, maxBlock);   // starts the async loads
        }

        stores[id] = store;
        nextStoreId = std::max (nextStoreId, id + 1);
    }

    GridModel m;
    for (const auto& n : state.getChildWithName (kNodesTag))
    {
        GridNode node;
        node.id = (NodeId) (int) n.getProperty ("id");
        node.kind = (NodeKind) (int) n.getProperty ("kind");
        node.store = (StoreId) (int) n.getProperty ("store");
        node.row = n.getProperty ("row");
        node.column = n.getProperty ("col");
        node.numOutputs = n.getProperty ("outputs", 1);

        for (const auto& token : juce::StringArray::fromTokens (n.getProperty ("inputs").toString(), " ", {}))
            if (token.containsChar (':'))
                node.inputs.push_back ({ (NodeId) token.upToFirstOccurrenceOf (":", false, false).getIntValue(),
                                         token.fromFirstOccurrenceOf (":", false, false).getIntValue() });

        m.addNodeWithId (node);
    }

    for (const auto& sc : state.getChildWithName (kScenesTag))
    {
        const int index = sc.getProperty ("index", -1);
        if (index < 0 || index >= kNumScenes)
            continue;

        auto& scene = editedScenes.scenes[(size_t) index];
        scene.valid = sc.getProperty ("valid");
        sceneNames[(size_t) index] = sc.getProperty ("name").toString();
        for (const auto& sn : sc)
        {
            if (scene.numStores >= kMaxStores)
                break;

            auto& snap = scene.stores[(size_t) scene.numStores++];
            snap.id = (StoreId) (int) sn.getProperty ("store");
            snap.bypassed = sn.getProperty ("bypass");
            const auto values = juce::StringArray::fromTokens (sn.getProperty ("values").toString(), " ", {});
            snap.numParams = std::min (values.size(), kMaxParams);
            for (int p = 0; p < snap.numParams; ++p)
                snap.values[(size_t) p] = values[p].getFloatValue();
        }
    }

    if (! commitModel (std::move (m)))
    {
        loadDefaultPreset();
        return;
    }

    publishSceneBank();

    for (const auto& mt : state.getChildWithName (kMacrosTag))
        assignMacro (mt.getProperty ("index", -1), (StoreId) (int) mt.getProperty ("store"),
                     mt.getProperty ("param", -1), mt.getProperty ("min", 0.0f), mt.getProperty ("max", 1.0f));

    // The active scene itself is a host parameter (restored by the processor).
}
} // namespace kumamp
