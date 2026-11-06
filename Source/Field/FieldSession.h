#pragma once

#include "../Core/LockFree.h"
#include "../Engine/GridEngine.h"
#include "../Graph/FusionBus.h"

#include <juce_data_structures/juce_data_structures.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace kumamp
{
/**
    The sound of the CAMPO view: its own engine with a copy of the grid's chain.

    The grid is never touched while playing here (only "pasar al Grid" writes to it). On
    entering, the copy sounds like the grid; from there each block is a form on the field:

    - its position morphs all of the block's continuous parameters at once, each with its
      own weights drawn from a seed (the home position is the copied value, unchanged),
    - stretching it makes those parameters oscillate; the guitar's level pushes them too,
    - overlapping two forms fuses their blocks: each one's output goes into the other's
      input through the FusionBus. That is signal, not only a drawing,
    - a sitar brings a second form, its tampura: it moves only the drone's parameters,
      switches only the drone, and overlapping it sends the drone through that block.

    Levels are left out of the morph, and the field's output has a limiter, so discovering
    does not mean blasting.

    Threads: everything is message thread except process() and the telemetry getters.
*/
class FieldSession
{
public:
    static constexpr int kMaxBlobs = 32;
    static constexpr int kMaxMorphedParams = 16;

    struct Blob
    {
        StoreId store = kInvalidStore;
        BlockType type = BlockType::Unknown;
        juce::String name;
        float x = 0.5f, y = 0.5f;           // field position, 0..1
        float homeX = 0.5f, homeY = 0.5f;   // where the morph is zero
        float stretch = 0.0f, angle = 0.0f; // oscillation depth, and its speed by direction
        bool on = true;
        bool tanpura = false;               // the sitar's drone, as a form of its own on the same block
        std::vector<float> bases;           // the block's parameters before the morph
    };

    struct Tone
    {
        juce::File file;
        juce::String name;
        juce::ValueTree data;
    };

    explicit FieldSession (GridEngine& gridEngine);
    ~FieldSession();

    //== Audio ==================================================================
    /** Message thread, audio stopped. */
    void prepare (double sampleRate, int maxBlockSize);
    /** In place; numChannels <= 2 of the grid, like GridEngine::process. */
    void process (float* const* channels, int numChannels, int numSamples, double bpm, float detectionGain) noexcept;
    void resetAudio() noexcept;

    /** Level of what comes in (0..1), for the view. Any thread. */
    float getEnvelope() const noexcept { return envelope.load (std::memory_order_relaxed); }

    //== The chain ==============================================================
    /** Follows the grid's elements; blocks still there keep what they had here. */
    void syncWithGrid();
    /** Starts over from the grid's current values. */
    void resetFromGrid();
    /** False while a model is still loading. */
    bool isChainReady() const;
    void runMessageThreadTasks();

    GridEngine& getEngine() noexcept { return engine; }
    FusionBus& getBus() noexcept { return bus; }

    //== The forms ==============================================================
    const std::vector<Blob>& getBlobs() const noexcept { return blobs; }
    void setBlobPosition (int blob, float x, float y);
    void setBlobStretch (int blob, float stretch, float angle);
    void toggleBlob (int blob);

    /** Radius in field heights. */
    float getBlobRadius (int blob) const;
    /** 0..1 how much two forms overlap; both directions of the fusion follow it. */
    float getOverlap (int a, int b) const;
    /** Width / height of the field, for the overlaps; the view keeps it current. */
    void setAspect (float widthOverHeight);

    /** Oscillation speed of a stretched form, before the tide. */
    static float rateHz (float angle) noexcept;

    float getTide() const noexcept   { return tide.load (std::memory_order_relaxed); }
    void setTide (float value);      // 0..1: how fast everything moves
    float getFusion() const noexcept { return fusion; }
    void setFusion (float value);    // 0..1: how much overlaps feed each other

    /** A new seed, and the forms thrown a little around where they are. */
    void mutate();

    /** Name and the parameters this form is moving most, with their current values. */
    juce::String describeBlob (int blob) const;
    /** The parameter's value with the morph, without the oscillation. */
    float getMorphedValue (int blob, int param) const;
    /** Copies the morphed values and on/off to the grid. Fusions have no grid equivalent. */
    int pushToGrid();

    //== Tones ==================================================================
    static juce::File getDefaultToneFolder();
    void setToneFolder (const juce::File& folder);
    const std::vector<Tone>& getTones() const noexcept { return tones; }
    void refreshTones();
    bool saveTone();
    /** Number of this chain's blocks it could not find in the tone (0 = same chain). */
    int loadTone (int tone);
    bool removeTone (int tone);
    /** What the field is now, restorable with applyState. */
    juce::ValueTree captureState() const;
    void applyState (const juce::ValueTree& state);
    /** Sets the field between two tones; amount 0 = a, 1 = b. */
    void blendTones (int a, int b, float amount);

    //== Project ================================================================
    juce::ValueTree saveState() const;
    void loadState (const juce::ValueTree& state);

private:
    struct PatchEntry { int param; float base, min, range, weightX, weightY, weightMod, phase; };
    struct PatchBlob
    {
        std::shared_ptr<ParamStore> store;
        float homeX, homeY, rate = 0.0f;
        std::vector<PatchEntry> entries;
    };
    struct Patch { std::vector<PatchBlob> blobs; };

    struct LiveBlob { std::atomic<float> x { 0.5f }, y { 0.5f }, stretch { 0.0f }, rate { 0.1f }; };

    juce::String chainSignature() const;
    void rebuild (bool keepValues);
    void layoutHomes();
    int columnsFor (int numBlobs) const;
    void publishPatch();
    void publishLive (int blob);
    void updateFusion();
    void writeBasesToStores();
    std::vector<PatchEntry> entriesFor (const Blob&) const;

    GridEngine& grid;
    GridEngine engine;
    FusionBus bus;
    RTObjectPublisher<Patch> patches;

    std::vector<Blob> blobs;
    juce::String signature;
    juce::ValueTree pendingState;
    std::vector<Tone> tones;
    juce::File toneFolder;
    juce::uint32 seed = 1;
    float fusion = 0.7f, aspect = 2.0f;
    std::atomic<float> tide { 0.4f };

    // Audio thread.
    std::array<LiveBlob, kMaxBlobs> live;
    std::array<double, kMaxBlobs> phases {};
    double sampleRate = 48000.0;
    float follower = 0.0f, attackCoef = 0.0f, releaseCoef = 0.0f;
    std::atomic<float> envelope { 0.0f };
};
} // namespace kumamp
