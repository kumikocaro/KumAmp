#include "Tuner.h"

#include <algorithm>
#include <cmath>

namespace kumamp
{
namespace
{
    constexpr int kWindowSize = 2048;   // ~85 ms at 24 kHz: two periods of a 30 Hz bass B
}

Tuner::Tuner() : juce::Thread ("KumAmp tuner")
{
    prepare (48000.0);
}

Tuner::~Tuner()
{
    stopThread (2000);
}

void Tuner::prepare (double sampleRate)
{
    stopThread (2000);

    decimation = std::max (1, (int) std::lround (sampleRate / 24000.0));
    analysisRate = sampleRate / decimation;
    for (auto& f : antiAlias)
    {
        f.setLowPass (sampleRate, (float) (0.2 * analysisRate));   // 4.8 kHz at 24 kHz: well below the new Nyquist
        f.reset();
    }
    decimationPhase = 0;
    numPending = 0;
    fifo.reset();

    detector.prepare (analysisRate, kWindowSize);
    history.assign (kWindowSize, 0.0f);
    window.assign (kWindowSize, 0.0f);
    historyPos = samplesSinceAnalysis = 0;
    hopSize = std::max (64, (int) (analysisRate * 0.010));
    numRecent = 0;
    smoothedMidi = -1.0f;

    startThread (juce::Thread::Priority::normal);   // "low" maps to a throttled QoS class on macOS
}

void Tuner::pushAudio (const float* const* channels, int numChannels, int numSamples) noexcept
{
    if (numChannels <= 0)
        return;

    const float scale = 1.0f / (float) numChannels;
    for (int i = 0; i < numSamples; ++i)
    {
        float mono = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            mono += channels[ch][i];

        const float filtered = antiAlias[1].process (antiAlias[0].process (mono * scale));
        if (++decimationPhase < decimation)
            continue;

        decimationPhase = 0;
        pending[(size_t) numPending++] = filtered;
        if (numPending == (int) pending.size())
        {
            fifo.write (pending.data(), numPending);
            numPending = 0;
        }
    }

    if (numPending > 0)
    {
        fifo.write (pending.data(), numPending);
        numPending = 0;
    }
}

void Tuner::run()
{
    std::array<float, 1024> chunk {};

    while (! threadShouldExit())
    {
        const int got = fifo.read (chunk.data(), (int) chunk.size());
        if (got == 0)
        {
            // No input for ~150 ms (tuner closed or host stopped): signal lost.
            if (++silentReads == 15)
                publish ({});
            wait (10);
            continue;
        }
        silentReads = 0;

        for (int i = 0; i < got; ++i)
        {
            history[(size_t) historyPos] = chunk[(size_t) i];
            historyPos = (historyPos + 1) % kWindowSize;
        }

        samplesSinceAnalysis += got;
        if (samplesSinceAnalysis < hopSize)
            continue;
        samplesSinceAnalysis = 0;

        // Oldest sample first.
        std::copy (history.begin() + historyPos, history.end(), window.begin());
        std::copy (history.begin(), history.begin() + historyPos, window.begin() + (kWindowSize - historyPos));
        publish (detector.analyse (window.data()));
    }
}

void Tuner::publish (const PitchDetector::Result& result)
{
    Reading reading;
    reading.levelDb = result.levelDb;
    reading.clarity = result.clarity;

    if (result.valid)
    {
        // Median of the last 3 estimates kills single-frame glitches (pick attack).
        recentFrequencies[(size_t) (numRecent++ % 3)] = result.frequency;
        auto sorted = recentFrequencies;
        const int count = std::min (numRecent, 3);
        std::sort (sorted.begin(), sorted.begin() + count);
        const float frequency = sorted[(size_t) (count / 2)];

        const float midi = 69.0f + 12.0f * std::log2 (frequency / getReferenceHz());
        // Smooth within a note, jump immediately on a new note.
        smoothedMidi = (smoothedMidi < 0.0f || std::abs (midi - smoothedMidi) > 0.5f)
                         ? midi
                         : smoothedMidi + 0.35f * (midi - smoothedMidi);

        reading.valid = true;
        reading.frequency = getReferenceHz() * std::exp2 ((smoothedMidi - 69.0f) / 12.0f);
        reading.midiNote = (int) std::lround (smoothedMidi);
        reading.cents = (smoothedMidi - (float) reading.midiNote) * 100.0f;
    }
    else
    {
        numRecent = 0;
        smoothedMidi = -1.0f;
    }

    const auto s = sequence.load (std::memory_order_relaxed);
    sequence.store (s + 1, std::memory_order_relaxed);   // odd: writing
    std::atomic_thread_fence (std::memory_order_release);
    outValid.store (reading.valid, std::memory_order_relaxed);
    outFrequency.store (reading.frequency, std::memory_order_relaxed);
    outCents.store (reading.cents, std::memory_order_relaxed);
    outClarity.store (reading.clarity, std::memory_order_relaxed);
    outLevel.store (reading.levelDb, std::memory_order_relaxed);
    outNote.store (reading.midiNote, std::memory_order_relaxed);
    sequence.store (s + 2, std::memory_order_release);
}

Tuner::Reading Tuner::getLatest() const noexcept
{
    Reading r;
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        const auto s1 = sequence.load (std::memory_order_acquire);
        r.valid     = outValid.load (std::memory_order_relaxed);
        r.frequency = outFrequency.load (std::memory_order_relaxed);
        r.cents     = outCents.load (std::memory_order_relaxed);
        r.clarity   = outClarity.load (std::memory_order_relaxed);
        r.levelDb   = outLevel.load (std::memory_order_relaxed);
        r.midiNote  = outNote.load (std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_acquire);
        const auto s2 = sequence.load (std::memory_order_relaxed);

        if (s1 == s2 && (s1 & 1u) == 0)
        {
            r.sequence = s1 / 2;
            break;
        }
    }
    return r;
}
} // namespace kumamp
