#pragma once

#include "../Core/LockFree.h"
#include "../Graph/GridModel.h"
#include "../Graph/RenderPlan.h"
#include "../Scenes/SceneBank.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>

namespace kumamp
{
/** GUI / message thread -> audio thread. */
struct EngineCommand
{
    enum class Type : std::uint8_t { ResetAllBlocks };
    Type type = Type::ResetAllBlocks;
    int value = 0;
};

/** Audio thread -> GUI. */
struct EngineEvent
{
    enum class Type : std::uint8_t { SceneChanged, PlanActivated };
    Type type = Type::SceneChanged;
    int value = 0;
};

/** Expression-pedal style mapping of a host parameter onto any block parameter. */
struct MacroTarget
{
    std::atomic<StoreId> store { kInvalidStore };
    std::atomic<int> param { -1 };
    std::atomic<float> minValue { 0.0f }, maxValue { 1.0f };
    std::atomic<int> version { 0 };
};

/**
    Owns the pedalboard: block instances, the routing model, scenes and the
    real-time execution of the compiled RenderPlan.

    Threading model
    ---------------
    Message thread : edits the GridModel, creates/prepares blocks, stores
                     scenes, (de)serialises. Every change is compiled into a
                     new immutable RenderPlan / SceneBank and published.
    Audio thread   : process(). Picks up published objects via atomic pointer
                     exchange, never allocates, never locks, never deletes.
    Retired objects travel back through a juce::AbstractFifo and are deleted
    by collectGarbage() on the message thread (called from a Timer).

    Topology changes: the old plan fades out and the new one fades in (~5 ms
    each). Blocks are shared between plans, so their state (delay lines,
    reverb tails) survives the edit.
    Scene changes: parameter/bypass targets only -> fully gapless.
*/
class GridEngine
{
public:
    GridEngine();
    ~GridEngine();

    //== Lifecycle (message thread, audio stopped) =============================
    void prepare (double sampleRate, int maxBlockSize);

    //== Audio thread ===========================================================
    void process (const float* const* inputs, int numInputs,
                  float* const* outputs, int numOutputs, int numSamples) noexcept;

    /** Any thread. Applied at the start of the next audio block. */
    void setRequestedScene (int sceneIndex) noexcept;
    int  getActiveScene() const noexcept { return activeScene.load (std::memory_order_relaxed); }

    /** Audio thread: host macro values (0..1). */
    void setMacroValue (int macro, float normalisedValue) noexcept;

    bool pushCommand (const EngineCommand& command) noexcept { return commands.push (command); }
    bool popEvent (EngineEvent& event) noexcept              { return events.pop (event); }

    //== Message thread editing =================================================
    std::shared_ptr<ParamStore> createStore (BlockType type, const juce::String& name = {});
    std::shared_ptr<ParamStore> getStore (StoreId id) const;

    const GridModel& getModel() const noexcept { return model; }
    int getModelVersion() const noexcept       { return modelVersion; }

    /** Validates + compiles + publishes. On failure nothing changes. */
    bool commitModel (GridModel newModel, juce::String* error = nullptr);

    void storeScene (int sceneIndex);
    void assignMacro (int macro, StoreId store, int param, float minValue, float maxValue);

    void loadDefaultPreset();

    /** Message thread timer: frees retired objects and re-evaluates the
        NAM End-to-End -> IR Loader auto-bypass links. */
    void runMessageThreadTasks();

    juce::ValueTree saveState() const;
    void loadState (const juce::ValueTree& state);

    int getLatencySamples() const noexcept { return latency.load (std::memory_order_relaxed); }

private:
    void publishSceneBank();
    void pruneUnusedStores();
    void updateCabLinks();
    bool hasEndToEndUpstream (const GridNode& node) const;
    void applyScene (int sceneIndex, const RenderPlan* plan) noexcept;
    void applyMacros (const RenderPlan* plan) noexcept;
    void applyFade (float* const* outputs, int numOutputs, int numSamples) noexcept;
    RenderPlan::StoreResolver makeResolver() const;

    // Message-thread state ------------------------------------------------------
    std::map<StoreId, std::shared_ptr<ParamStore>> stores;
    StoreId nextStoreId = 1;
    GridModel model;
    int modelVersion = 0;
    SceneBank editedScenes;
    double sampleRate = 48000.0;
    int maxBlock = 512;
    bool prepared = false;

    // Shared -------------------------------------------------------------------
    RTObjectPublisher<RenderPlan> plans;
    RTObjectPublisher<SceneBank> sceneBanks;
    SpscQueue<EngineCommand, 64> commands;
    SpscQueue<EngineEvent, 64> events;
    std::array<MacroTarget, kNumMacros> macros;
    std::atomic<int> requestedScene { 0 }, activeScene { 0 };
    std::atomic<int> latency { 0 };

    // Audio-thread state ----------------------------------------------------------
    enum class Fade { Running, FadingOut, Muted, FadingIn };
    Fade fade = Fade::Running;
    int fadePos = 0, fadeLength = 240;
    int appliedScene = -1;
    std::array<float, kNumMacros> macroValues {}, lastMacroValues {};
    std::array<int, kNumMacros> lastMacroVersions {};

    JUCE_DECLARE_NON_COPYABLE (GridEngine)
};
} // namespace kumamp
