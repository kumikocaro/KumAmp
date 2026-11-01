#pragma once

#include <array>
#include <atomic>

namespace kumamp
{
/**
    Lock-free "latest samples" ring for visualisation (spectrum analysers).
    One writer (audio thread) continuously overwrites; readers copy the most
    recent N samples. A reader racing the writer may see a few samples from
    the next block at the oldest end of its window: harmless for a display,
    and it means neither side ever waits.
*/
template <int Capacity>
class SampleRing
{
public:
    static_assert ((Capacity & (Capacity - 1)) == 0, "capacity must be a power of two");

    void push (const float* samples, int numSamples) noexcept
    {
        int w = writeIndex.load (std::memory_order_relaxed);
        for (int i = 0; i < numSamples; ++i)
            buffer[(size_t) ((w + i) & (Capacity - 1))].store (samples[i], std::memory_order_relaxed);
        writeIndex.store (w + numSamples, std::memory_order_release);
    }

    /** Copies the latest `numSamples` (<= Capacity), oldest first. */
    void readLatest (float* destination, int numSamples) const noexcept
    {
        const int w = writeIndex.load (std::memory_order_acquire);
        for (int i = 0; i < numSamples; ++i)
            destination[i] = buffer[(size_t) ((w - numSamples + i) & (Capacity - 1))].load (std::memory_order_relaxed);
    }

    int getWriteCount() const noexcept { return writeIndex.load (std::memory_order_acquire); }

private:
    std::array<std::atomic<float>, Capacity> buffer {};
    std::atomic<int> writeIndex { 0 };
};
} // namespace kumamp
