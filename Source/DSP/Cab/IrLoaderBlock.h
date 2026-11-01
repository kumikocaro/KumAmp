#pragma once

#include "../../Core/Biquad.h"
#include "../../Core/Smoothing.h"
#include "../../Graph/DSPBlock.h"

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace kumamp
{
/**
    Dual stereo cabinet IR loader (A + B), each with level and pan, followed by
    low/high cut and a dry/wet mix. Convolution is juce::dsp::Convolution
    (zero latency, IRs loaded and crossfaded on JUCE's background queue).

    Conditional decoupling: when "Auto E2E" is on and the engine reports an
    End-to-End NAM profile upstream (setAutoBypassed), the block fades itself
    out to dry and stops convolving, independently of the user/scene bypass.
*/
class IrLoaderBlock final : public DSPBlock
{
public:
    enum Param { LevelA, PanA, LevelB, PanB, EnableB, LowCut, HighCut, Mix, AutoE2E, NumParams };

    explicit IrLoaderBlock (StoreId id);

    int getNumFileSlots() const override { return 2; }
    std::string getFileSlotName (int slot) const override { return slot == 0 ? "IR A" : "IR B"; }
    std::string getFileSlotPattern (int) const override   { return "*.wav;*.aif;*.aiff;*.flac"; }
    bool loadFile (int slot, const std::string& path) override;
    std::string getFilePath (int slot) const override;
    std::string getStatusText() const override;
    std::vector<std::string> getInfoChips() const override
    {
        if (isAutoBypassActive()) return { "Desacoplado (NAM E2E)" };
        return { getParam (EnableB) > 0.5f ? "Dual A+B" : "IR A" };
    }

    /** Any thread (engine). */
    void setAutoBypassed (bool shouldBypass) noexcept { autoBypassed.store (shouldBypass, std::memory_order_relaxed); }
    /** True while the block is actually stepping aside for an E2E profile. */
    bool isAutoBypassActive() const noexcept;

protected:
    void prepareBlock (double sampleRate, int maxBlockSize) override;
    void resetBlock() noexcept override;
    void processBlock (float* left, float* right, int numSamples) noexcept override;

private:
    struct QueueHolder { juce::dsp::ConvolutionMessageQueue queue { 64 }; };

    void loadImpulse (int slot);
    void updateFilters() noexcept;

    juce::SharedResourcePointer<QueueHolder> queueHolder;
    std::array<juce::dsp::Convolution, 2> convolutions {
        juce::dsp::Convolution { juce::dsp::Convolution::Latency { 0 }, queueHolder->queue },
        juce::dsp::Convolution { juce::dsp::Convolution::Latency { 0 }, queueHolder->queue }
    };

    std::array<std::atomic<bool>, 2> loaded {};
    mutable std::mutex pathLock;   // message thread only
    std::array<std::string, 2> paths, statuses;

    std::atomic<bool> autoBypassed { false };

    std::vector<float> dryL, dryR, aL, aR, bL, bR;
    std::array<LinearSmoother, 4> gains;   // A left/right, B left/right
    LinearSmoother mix, autoWet;
    std::array<Biquad, 2> highPass, lowPass;
    float lastLowCut = -1.0f, lastHighCut = -1.0f;
};
} // namespace kumamp
