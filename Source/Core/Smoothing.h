#pragma once

#include <algorithm>

namespace kumamp
{
/** Linear ramp smoother. Allocation-free; setTarget() and next() are RT-safe. */
class LinearSmoother
{
public:
    void prepare (double sampleRate, double rampSeconds) noexcept
    {
        rampLength = std::max (1, static_cast<int> (sampleRate * rampSeconds));
    }

    void setCurrentAndTarget (float value) noexcept
    {
        current = target = value;
        remaining = 0;
    }

    void setTarget (float value) noexcept
    {
        if (value == target)
            return;

        target    = value;
        remaining = rampLength;
        step      = (target - current) / static_cast<float> (rampLength);
    }

    float next() noexcept
    {
        if (remaining > 0)
        {
            current += step;
            if (--remaining == 0)
                current = target;
        }
        return current;
    }

    bool  isSmoothing() const noexcept { return remaining > 0; }
    float getCurrent() const noexcept  { return current; }
    float getTarget() const noexcept   { return target; }

private:
    float current = 0.0f, target = 0.0f, step = 0.0f;
    int remaining = 0, rampLength = 1;
};
} // namespace kumamp
