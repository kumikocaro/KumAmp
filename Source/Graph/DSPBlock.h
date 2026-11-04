#pragma once

#include "ParamStore.h"
#include "../Core/DelayLine.h"

#include <vector>

namespace kumamp
{
/**
    Base class of every audio block in the grid. Blocks are always stereo and
    process in place.

    The base class owns the bypass logic so every block gets the same
    click-free behaviour:
      - bypass toggles crossfade wet/dry over ~10 ms,
      - a fully bypassed block is not processed (zero CPU) but its dry signal
        is still delayed by getLatencySamples() so parallel paths stay aligned,
      - when re-enabled after a full bypass, resetBlock() clears stale state.

    Contract for subclasses:
      - prepareBlock()  : may allocate. Called off the audio thread.
      - resetBlock()    : RT-safe (no allocation, no locks).
      - processBlock()  : RT-safe; numSamples <= maxBlockSize.
      - getLatencySamples() must be constant between prepare() calls.
*/
class DSPBlock : public ParamStore
{
public:
    using ParamStore::ParamStore;

    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void process (float* left, float* right, int numSamples) noexcept;

    virtual int getLatencySamples() const noexcept { return 0; }

    //== File slots (NAM models, IRs...). Message thread only. ===================
    virtual int getNumFileSlots() const { return 0; }
    virtual std::string getFileSlotName (int /*slot*/) const { return {}; }
    virtual std::string getFileSlotPattern (int /*slot*/) const { return "*"; }
    /** Starts loading asynchronously; the audio path swaps it in click-free. */
    virtual bool loadFile (int /*slot*/, const std::string& /*path*/) { return false; }
    virtual std::string getFilePath (int /*slot*/) const { return {}; }
    virtual std::string getStatusText() const { return {}; }
    /** Short tags shown as chips in the block panel ("Amp-Only", "NAM 0.7"...). */
    virtual std::vector<std::string> getInfoChips() const { return {}; }
    /** Replaces a parameter's displayed value (e.g. a tempo-synced time:
        "1/4 · 500 ms"); empty = show the value normally. The UI also locks the
        control while an override is active. */
    virtual std::string getDisplayOverride (int /*param*/) const { return {}; }
    /** False hides a parameter that does nothing in the current mode (the panel
        lays out only the relevant ones). Any thread. */
    virtual bool isParamRelevant (int /*param*/) const noexcept { return true; }

    /** Audio this block sends straight to the grid output, around every block after it
        (a backing drone the chain must not colour), faded with the bypass. Null = none.
        Audio thread, valid after process(). */
    const float* getDirectOutput (int channel) noexcept { return directActive ? directBuffer (channel) : nullptr; }

    //== Host tempo (shared by the engine; any thread) ============================
    void setTempoSource (const std::atomic<double>* bpm) noexcept { tempoSource = bpm; }
    double getTempoBpm() const noexcept
    {
        const double bpm = tempoSource != nullptr ? tempoSource->load (std::memory_order_relaxed) : 120.0;
        return bpm > 1.0 ? bpm : 120.0;
    }

    //== Note detection sensitivity (shared by the engine; any thread) =============
    /** Linear gain on what this block's note detectors hear, never on its sound. */
    void setDetectionSource (const std::atomic<float>* gain) noexcept { detectionSource = gain; }
    float getDetectionGain() const noexcept
    {
        return detectionSource != nullptr ? detectionSource->load (std::memory_order_relaxed) : 1.0f;
    }

    /** Message thread: delete objects retired by the audio thread. */
    virtual void collectGarbage() {}

    bool   isPrepared() const noexcept    { return prepared; }
    double getSampleRate() const noexcept { return currentSampleRate; }
    int    getMaxBlockSize() const noexcept { return maxBlock; }

protected:
    /** Return true to receive processBlock() even while bypassed and implement
        bypass yourself (delays / reverbs: mute the input, let the tail ring). */
    virtual bool handlesBypassInternally() const noexcept { return false; }

    /** The block's direct-to-output buffer for this process() call (numSamples long), or
        nullptr. See getDirectOutput(). */
    virtual float* directBuffer (int /*channel*/) noexcept { return nullptr; }

    virtual void prepareBlock (double sampleRate, int maxBlockSize) = 0;
    virtual void resetBlock() noexcept = 0;
    virtual void processBlock (float* left, float* right, int numSamples) noexcept = 0;

private:
    const std::atomic<double>* tempoSource = nullptr;
    const std::atomic<float>* detectionSource = nullptr;
    std::vector<float> dryLeft, dryRight;
    FixedDelay dryDelay;
    double currentSampleRate = 48000.0;
    int maxBlock = 0;
    float bypassMix = 0.0f;   // 0 = active, 1 = bypassed
    float bypassStep = 0.0f;
    bool fullyBypassed = false;
    bool directActive = false;
    bool prepared = false;
};
} // namespace kumamp
