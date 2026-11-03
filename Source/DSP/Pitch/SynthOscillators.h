#pragma once

namespace kumamp::synth
{
/** PolyBLEP residual: removes the aliasing of naive saw / square edges. */
inline float polyBlep (float t, float dt) noexcept
{
    if (t < dt)        { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

/** Band-limited 0 = saw, 1 = square, 2 = 25 % pulse; t = phase [0, 1), dt = f / sr. */
inline float oscillator (int wave, float t, float dt) noexcept
{
    switch (wave)
    {
        case 0:  return 2.0f * t - 1.0f - polyBlep (t, dt);                                    // saw
        case 1:
        {
            float t2 = t + 0.5f; if (t2 >= 1.0f) t2 -= 1.0f;
            return (t < 0.5f ? 1.0f : -1.0f) + polyBlep (t, dt) - polyBlep (t2, dt);          // square
        }
        default:
        {
            float t2 = t + 0.75f; if (t2 >= 1.0f) t2 -= 1.0f;
            return (t < 0.25f ? 1.0f : -1.0f) + polyBlep (t, dt) - polyBlep (t2, dt) - 0.5f;  // 25 % pulse
        }
    }
}
} // namespace kumamp::synth
