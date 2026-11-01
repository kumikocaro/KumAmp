#pragma once

#include "DspMath.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace kumamp
{
/** Circular mono delay with fractional (Hermite) reads. Allocated in prepare(). */
class DelayLine
{
public:
    void prepare (int maxDelaySamples)
    {
        int size = 1;
        while (size < maxDelaySamples + 4)
            size <<= 1;
        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        writePos = 0;
    }

    void reset() noexcept { std::fill (buffer.begin(), buffer.end(), 0.0f); writePos = 0; }

    void push (float x) noexcept
    {
        buffer[(size_t) writePos] = x;
        writePos = (writePos + 1) & mask;
    }

    /** Fractional delay in samples (>= 1); 0 would be the sample just pushed. */
    float read (float delay) const noexcept
    {
        delay = std::clamp (delay, 1.0f, (float) (mask - 3));
        const int whole = (int) delay;
        const float t = delay - (float) whole;
        const float pm1 = tap (whole - 1), p0 = tap (whole), p1 = tap (whole + 1), p2 = tap (whole + 2);
        const float c1 = 0.5f * (p1 - pm1);
        const float c2 = pm1 - 2.5f * p0 + 2.0f * p1 - 0.5f * p2;
        const float c3 = 0.5f * (p2 - pm1) + 1.5f * (p0 - p1);
        return ((c3 * t + c2) * t + c1) * t + p0;
    }

    /** Integer delay; 0 = the sample just pushed. */
    float readInteger (int delay) const noexcept { return tap (std::clamp (delay, 0, mask)); }

    int getCapacity() const noexcept { return mask + 1; }

private:
    float tap (int delay) const noexcept { return buffer[(size_t) ((writePos - 1 - delay) & mask)]; }

    std::vector<float> buffer { 0.0f };
    int mask = 0, writePos = 0;
};

/** Sine/triangle LFO with phase offset, frequency updatable per block. */
class Lfo
{
public:
    void prepare (double sampleRate) noexcept { rate = sampleRate; }
    void setFrequency (float hz) noexcept     { increment = (double) hz / rate; }
    void reset (double startPhase = 0.0) noexcept { phase = startPhase; }

    /** Advances one sample; returns phase in [0, 1). */
    double advance() noexcept
    {
        const double p = phase;
        phase += increment;
        if (phase >= 1.0) phase -= 1.0;
        return p;
    }

    static float sine (double phase) noexcept     { return std::sin (2.0f * kPi * (float) phase); }
    static float triangle (double phase) noexcept { return 4.0f * std::abs ((float) phase - 0.5f) - 1.0f; }

private:
    double rate = 48000.0, phase = 0.0, increment = 0.0;
};

/** Attack/release envelope follower on |x| (peak). */
class EnvelopeFollower
{
public:
    void setTimes (double sampleRate, float attackMs, float releaseMs) noexcept
    {
        attack  = coefficient (sampleRate, attackMs);
        release = coefficient (sampleRate, releaseMs);
    }
    void reset() noexcept { value = 0.0f; }

    float process (float x) noexcept
    {
        const float level = std::abs (x);
        const float c = level > value ? attack : release;
        value = level + c * (value - level);
        return value;
    }

    float getValue() const noexcept { return value; }

    static float coefficient (double sampleRate, float ms) noexcept
    {
        return ms <= 0.0f ? 0.0f : std::exp (-1.0f / (0.001f * ms * (float) sampleRate));
    }

private:
    float attack = 0.0f, release = 0.0f, value = 0.0f;
};

/** One-pole low-pass (also gives a high-pass as x - lp). */
class OnePole
{
public:
    void setCutoff (double sampleRate, float hz) noexcept
    {
        coefficient = std::exp (-2.0f * kPi * std::clamp (hz, 1.0f, 0.49f * (float) sampleRate) / (float) sampleRate);
    }
    void reset() noexcept { state = 0.0f; }
    float lowPass (float x) noexcept  { state = x + coefficient * (state - x); return state; }
    float highPass (float x) noexcept { return x - lowPass (x); }

private:
    float coefficient = 0.0f, state = 0.0f;
};

/** Topology-preserving state variable filter (Zavalishin). Stable under fast
    modulation, which makes it the right core for wahs. */
class StateVariableFilter
{
public:
    struct Outputs { float low, band, high; };

    void set (double sampleRate, float frequency, float q) noexcept
    {
        g = std::tan (kPi * std::clamp (frequency, 10.0f, 0.45f * (float) sampleRate) / (float) sampleRate);
        k = 1.0f / std::max (q, 0.1f);
        a1 = 1.0f / (1.0f + g * (g + k));
    }
    void reset() noexcept { s1 = s2 = 0.0f; }

    Outputs process (float x) noexcept
    {
        const float v3 = x - s2;
        const float v1 = a1 * (s1 + g * v3);
        const float v2 = s2 + g * v1;
        s1 = 2.0f * v1 - s1;
        s2 = 2.0f * v2 - s2;
        return { v2, v1, x - k * v1 - v2 };
    }

private:
    float g = 0.0f, k = 1.0f, a1 = 1.0f, s1 = 0.0f, s2 = 0.0f;
};

/** Schroeder allpass with a fixed integer delay (diffusers, springs). */
class AllpassDiffuser
{
public:
    void prepare (int delaySamples) { line.prepare (delaySamples + 2); delay = std::max (1, delaySamples); }
    void reset() noexcept { line.reset(); }
    void setDelay (int samples) noexcept { delay = std::clamp (samples, 1, line.getCapacity() - 4); }

    float process (float x, float gain) noexcept
    {
        const float delayed = line.readInteger (delay - 1);
        const float v = x + gain * delayed;
        line.push (v);
        return delayed - gain * v;
    }

private:
    DelayLine line;
    int delay = 1;
};

inline float softClip (float x) noexcept
{
    // Smooth, cheap saturation; ~unity gain for small signals.
    const float c = std::clamp (x, -3.0f, 3.0f);
    return c * (27.0f + c * c) / (27.0f + 9.0f * c * c);
}
} // namespace kumamp
