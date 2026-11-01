#include "GridEngine.h"

#include "../Graph/BlockFactory.h"
#include "../Graph/DSPBlock.h"
#include "../DSP/Pitch/PitchShifterBlock.h"
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

GridEngine::GridEngine() = default;
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

        if (value == lastMacroValues[m])
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

    if (prepared)
        if (auto* block = dynamic_cast<DSPBlock*> (store.get()))
            block->prepare (sampleRate, maxBlock);

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

void GridEngine::collectGarbage()
{
    plans.collectGarbage();
    sceneBanks.collectGarbage();
}

//==============================================================================
void GridEngine::loadDefaultPreset()
{
    stores.clear();
    nextStoreId = 1;
    editedScenes = {};

    auto transpose = createStore (BlockType::PitchShifter, "Transpose");
    auto split     = createStore (BlockType::Splitter, "Split");
    auto dry       = createStore (BlockType::Volume, "Dry");
    auto harmony   = createStore (BlockType::PitchShifter, "Harmony");
    auto mixer     = createStore (BlockType::Mixer, "Mix");
    auto output    = createStore (BlockType::Volume, "Output");

    // Input -> Transpose -> Split -> { Dry | Harmony } -> Mix -> Output volume -> Output
    GridModel m;
    const auto in  = m.addNode (NodeKind::Input, kInvalidStore, 0, 0);
    const auto tr  = m.addNode (NodeKind::Block, transpose->getId(), 0, 1);
    const auto sp  = m.addNode (NodeKind::Split, split->getId(), 0, 2, 2);
    const auto dr  = m.addNode (NodeKind::Block, dry->getId(), 0, 3);
    const auto ha  = m.addNode (NodeKind::Block, harmony->getId(), 1, 3);
    const auto mx  = m.addNode (NodeKind::Mix, mixer->getId(), 0, 4);
    const auto ov  = m.addNode (NodeKind::Block, output->getId(), 0, 5);
    const auto out = m.addNode (NodeKind::Output, kInvalidStore, 0, 6);
    m.connect ({ in, 0 }, tr);
    m.connect ({ tr, 0 }, sp);
    m.connect ({ sp, 0 }, dr);
    m.connect ({ sp, 1 }, ha);
    m.connect ({ dr, 0 }, mx);
    m.connect ({ ha, 0 }, mx);
    m.connect ({ mx, 0 }, ov);
    m.connect ({ ov, 0 }, out);

    // Scene A: clean, harmony off (bypassed = no CPU, and muted at the mixer).
    harmony->setParam (PitchShifterBlock::Semitones, 7.0f);
    harmony->setBypassed (true);
    mixer->setParam (MixerParam::inputLevelDb (1), -60.0f);

    commitModel (std::move (m));

    storeScene (0);

    // Scene B: whole step down.
    transpose->setParam (PitchShifterBlock::Semitones, -2.0f);
    storeScene (1);

    // Scene C: standard tuning + fifth harmony, spread in stereo.
    transpose->setParam (PitchShifterBlock::Semitones, 0.0f);
    harmony->setBypassed (false);
    mixer->setParam (MixerParam::inputLevelDb (1), -4.0f);
    mixer->setParam (MixerParam::inputPan (0), -0.35f);
    mixer->setParam (MixerParam::inputPan (1), 0.35f);
    storeScene (2);

    // Scene D: octave up.
    harmony->setBypassed (true);
    mixer->setParam (MixerParam::inputLevelDb (1), -60.0f);
    mixer->setParam (MixerParam::inputPan (0), 0.0f);
    mixer->setParam (MixerParam::inputPan (1), 0.0f);
    transpose->setParam (PitchShifterBlock::Semitones, 12.0f);
    storeScene (3);

    // Back to scene A values.
    SceneBank::apply (editedScenes.scenes[0], [this] (StoreId id) { return getStore (id).get(); });

    // Macro 1 = whammy pedal on the transposer (0..+12 st).
    assignMacro (0, transpose->getId(), PitchShifterBlock::Semitones, 0.0f, 12.0f);
    juce::ignoreUnused (dry, output);
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

        if (prepared)
            if (auto* block = dynamic_cast<DSPBlock*> (store.get()))
                block->prepare (sampleRate, maxBlock);

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
