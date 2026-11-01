#pragma once

#include "NamProfileInfo.h"
#include "../../Core/Biquad.h"
#include "../../Core/LockFree.h"
#include "../../Core/RateBridge.h"
#include "../../Core/Smoothing.h"
#include "../../Graph/DSPBlock.h"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace nam { class DSP; }

namespace kumamp
{
/** A loaded, fully prepared NAM model. Built on the loader thread, then
    handed to the audio thread; never modified after publication. */
struct NamModel
{
    NamModel();
    ~NamModel();

    /** Non-RT: Reset() + prewarm, sizes the resampling bridge. */
    void prepare (double hostRate, int maxHostBlock);
    /** RT-safe. numSamples <= the maxHostBlock given to prepare(). */
    void process (const float* input, float* output, int numSamples) noexcept;

    std::unique_ptr<nam::DSP> dsp;
    NamProfileInfo info;
    std::string path, version, architecture;
    double trainedRate = -1.0, runRate = 48000.0;
    float normalisationGain = 1.0f;   // brings the model to -18 dB loudness
    RateBridge bridge;
    std::vector<float> modelOutput;
};

/**
    Neural amp block (NAM / Tone3000 captures, .nam 0.5 - 0.7 incl. A2).

    - Models load on a background thread and swap in with a 50 ms crossfade.
    - Mono inference (as every NAM capture), output duplicated to stereo.
    - Models trained at another rate run through a RateBridge.
    - Metadata decides Amp-Only vs End-to-End; the "Perfil" parameter can
      override it. The engine uses getEffectiveProfileKind() to auto-bypass
      IR loaders placed after an End-to-End profile.
*/
class NamAmpBlock final : public DSPBlock
{
public:
    enum Param { InputDb, Bass, Mid, Treble, Presence, OutputDb, Normalize, ProfileMode, NumParams };
    enum ProfileModeValue { ModeAuto = 0, ModeAmpOnly = 1, ModeEndToEnd = 2 };

    explicit NamAmpBlock (StoreId id);
    ~NamAmpBlock() override;

    int getNumFileSlots() const override { return 1; }
    std::string getFileSlotName (int) const override    { return "Modelo NAM"; }
    std::string getFileSlotPattern (int) const override { return "*.nam"; }
    bool loadFile (int slot, const std::string& path) override;
    std::string getFilePath (int slot) const override;
    std::string getStatusText() const override;
    std::vector<std::string> getInfoChips() const override;
    void collectGarbage() override;

    /** Any thread. Unknown metadata counts as Amp-Only. */
    ProfileKind getEffectiveProfileKind() const noexcept;
    bool hasModel() const noexcept { return modelLoaded.load (std::memory_order_relaxed); }

protected:
    void prepareBlock (double sampleRate, int maxBlockSize) override;
    void resetBlock() noexcept override;
    void processBlock (float* left, float* right, int numSamples) noexcept override;

private:
    struct Shared
    {
        RTCrossfadeSlot<NamModel> slot;
        std::atomic<int> generation { 0 };
        std::atomic<int> detectedKind { (int) ProfileKind::Unknown };
        std::mutex lock;          // loader <-> message thread only, never the audio thread
        std::string requestedPath, status, version, architecture;
        bool resampled = false;
    };

    void startLoad();

    std::shared_ptr<Shared> shared;
    juce::SharedResourcePointer<FileLoaderPool> loader;

    void updateToneStack (bool force) noexcept;

    std::vector<float> mono, wetCurrent, wetPrevious, dryLeft, dryRight;
    std::array<Biquad, 4> toneStack;            // bass, mid, treble, presence (post-model)
    std::array<float, 4> toneValues { -1.0f, -1.0f, -1.0f, -1.0f };
    LinearSmoother inputGain, outputGain;
    int fadePos = 0, fadeLength = 1;
    bool fading = false;
    std::atomic<bool> modelLoaded { false };
};
} // namespace kumamp
