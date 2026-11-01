#pragma once

#include "../Core/Biquad.h"
#include "../Core/LockFree.h"
#include "../Core/PitchDetector.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace kumamp
{
/**
    Input tuner. Taps the plugin input (before the grid), like the tuner of a
    hardware pedalboard.

    Audio thread : pushAudio() — mono sum, anti-alias low-pass, decimation to
                   ~24 kHz, then a lock-free AudioSampleFifo. Only while a
                   tuner view is listening, so it costs nothing otherwise.
    Analysis     : own thread; McLeod pitch detection on an 85 ms window every
                   ~10 ms, median + smoothing, published with a seqlock.
    GUI          : getLatest() at display rate.
*/
class Tuner final : private juce::Thread
{
public:
    struct Reading
    {
        bool valid = false;
        float frequency = 0.0f;
        float cents = 0.0f;        // -50..+50 relative to midiNote
        float clarity = 0.0f;
        float levelDb = -120.0f;
        int midiNote = -1;
        std::uint32_t sequence = 0; // increments with every analysis
    };

    Tuner();
    ~Tuner() override;

    /** Message thread, audio stopped. */
    void prepare (double sampleRate);

    /** Audio thread, RT-safe. */
    void pushAudio (const float* const* channels, int numChannels, int numSamples) noexcept;

    void setListening (bool shouldListen) noexcept { listening.store (shouldListen, std::memory_order_relaxed); }
    bool isListening() const noexcept               { return listening.load (std::memory_order_relaxed); }

    void setReferenceHz (float hz) noexcept { reference.store (hz, std::memory_order_relaxed); }
    float getReferenceHz() const noexcept   { return reference.load (std::memory_order_relaxed); }

    /** Any thread. */
    Reading getLatest() const noexcept;

private:
    void run() override;
    void publish (const PitchDetector::Result& result);

    // Audio side
    int decimation = 2, decimationPhase = 0;
    std::array<Biquad, 2> antiAlias;
    std::array<float, 256> pending {};
    int numPending = 0;
    AudioSampleFifo<1 << 15> fifo;

    // Analysis side
    PitchDetector detector;
    std::vector<float> history, window;
    int historyPos = 0, samplesSinceAnalysis = 0, hopSize = 240;
    std::array<float, 3> recentFrequencies {};
    int numRecent = 0;
    float smoothedMidi = -1.0f;
    double analysisRate = 24000.0;
    int silentReads = 0;

    // Published (seqlock)
    std::atomic<std::uint32_t> sequence { 0 };
    std::atomic<bool> outValid { false };
    std::atomic<float> outFrequency { 0.0f }, outCents { 0.0f }, outClarity { 0.0f }, outLevel { -120.0f };
    std::atomic<int> outNote { -1 };

    std::atomic<bool> listening { false };
    std::atomic<float> reference { 440.0f };
};
} // namespace kumamp
