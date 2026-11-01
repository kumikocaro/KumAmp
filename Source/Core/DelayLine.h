#pragma once

#include <algorithm>
#include <array>
#include <vector>

namespace kumamp
{
/** Fixed-length stereo delay used for latency compensation.
    prepare() allocates (message thread); process() is RT-safe. */
class FixedDelay
{
public:
    void prepare (int delaySamples)
    {
        delay = std::max (0, delaySamples);
        for (auto& b : buffers)
            b.assign (static_cast<size_t> (delay), 0.0f);
        position = 0;
    }

    void reset() noexcept
    {
        for (auto& b : buffers)
            std::fill (b.begin(), b.end(), 0.0f);
        position = 0;
    }

    int getDelay() const noexcept { return delay; }

    void process (float* left, float* right, int numSamples) noexcept
    {
        if (delay == 0)
            return;

        float* bl = buffers[0].data();
        float* br = buffers[1].data();

        for (int i = 0; i < numSamples; ++i)
        {
            const float l = bl[position], r = br[position];
            bl[position] = left[i];
            br[position] = right[i];
            left[i]  = l;
            right[i] = r;

            if (++position == delay)
                position = 0;
        }
    }

private:
    std::array<std::vector<float>, 2> buffers;
    int delay = 0, position = 0;
};
} // namespace kumamp
