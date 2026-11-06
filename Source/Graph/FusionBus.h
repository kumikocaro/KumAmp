#pragma once

#include "../Core/Constants.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace kumamp
{
/**
    Lets blocks of a RenderPlan feed each other outside the graph: the last output of one
    block is added to the input of another, in either direction. A send that goes backwards
    in the chain is feedback through everything in between, one audio block later.

    Every send is high-passed and soft-clipped, so a closed loop can ring but cannot run
    away. Only the Campo's engine has one; a plan without a bus does nothing extra.
*/
class FusionBus
{
public:
    static constexpr int kSlots = 64;
    /** Source slot for what blocks send straight to the output (the tampura drone). */
    static constexpr StoreId kDirectSlot = kSlots - 1;

    /** Message thread, audio stopped. */
    void prepare (double sampleRate, int maxBlockSize)
    {
        maxBlock = std::max (1, maxBlockSize);
        outputs.assign ((size_t) (kSlots * kGraphChannels * maxBlock), 0.0f);
        send.assign ((size_t) (kGraphChannels * maxBlock), 0.0f);
        highPassCoef = std::exp (-2.0f * 3.14159265f * 90.0f / (float) sampleRate);
        highPass.fill ({});
    }

    /** Any thread. Linear gain of the from -> to send; the audio side ramps to it. */
    void setGain (StoreId from, StoreId to, float gain) noexcept
    {
        if (from < (StoreId) kSlots && to < (StoreId) kSlots && from != to)
            gains[index (from, to)].store (gain, std::memory_order_relaxed);
    }

    float getGain (StoreId from, StoreId to) const noexcept
    {
        return from < (StoreId) kSlots && to < (StoreId) kSlots ? gains[index (from, to)].load (std::memory_order_relaxed) : 0.0f;
    }

    void clearGains() noexcept
    {
        for (auto& g : gains)
            g.store (0.0f, std::memory_order_relaxed);
    }

    /** Audio thread: adds what the other blocks send to this one. */
    void beforeBlock (StoreId id, float* left, float* right, int numSamples) noexcept
    {
        if (id >= kDirectSlot || outputs.empty())
            return;

        bool any = false;
        float* sendLeft = send.data();
        float* sendRight = send.data() + maxBlock;

        for (int from = 0; from < kSlots; ++from)
        {
            const auto link = index ((StoreId) from, id);
            const float target = gains[link].load (std::memory_order_relaxed);
            float& gain = current[link];
            if (target <= 0.0f && gain <= 0.0f)
                continue;

            if (! any)
            {
                std::fill_n (sendLeft, numSamples, 0.0f);
                std::fill_n (sendRight, numSamples, 0.0f);
                any = true;
            }
            const float step = (target - gain) / (float) numSamples;
            const float* l = channel (from, 0);
            const float* r = channel (from, 1);
            for (int i = 0; i < numSamples; ++i)
            {
                gain += step;
                sendLeft[i] += l[i] * gain;
                sendRight[i] += r[i] * gain;
            }
            gain = target;
        }

        if (! any)
            return;

        auto& state = highPass[id];
        for (int i = 0; i < numSamples; ++i)
        {
            state[0] = highPassCoef * state[0] + (1.0f - highPassCoef) * sendLeft[i];
            state[1] = highPassCoef * state[1] + (1.0f - highPassCoef) * sendRight[i];
            left[i] += std::tanh (sendLeft[i] - state[0]);
            right[i] += std::tanh (sendRight[i] - state[1]);
        }
    }

    /** Audio thread: keeps this block's output for the next sends. */
    void afterBlock (StoreId id, const float* left, const float* right, int numSamples) noexcept
    {
        if (id >= (StoreId) kSlots || outputs.empty())
            return;
        std::copy_n (left, numSamples, channel ((int) id, 0));
        std::copy_n (right, numSamples, channel ((int) id, 1));
    }

private:
    static size_t index (StoreId from, StoreId to) noexcept { return (size_t) from * kSlots + (size_t) to; }
    float* channel (int slot, int ch) noexcept { return outputs.data() + ((size_t) slot * kGraphChannels + (size_t) ch) * (size_t) maxBlock; }

    std::array<std::atomic<float>, kSlots * kSlots> gains {};
    std::array<float, kSlots * kSlots> current {};
    std::array<std::array<float, 2>, kSlots> highPass {};
    std::vector<float> outputs, send;
    float highPassCoef = 0.99f;
    int maxBlock = 0;
};
} // namespace kumamp
