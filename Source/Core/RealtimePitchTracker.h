#pragma once

#include "Biquad.h"
#include "DspPrimitives.h"
#include "PitchDetector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace kumamp
{
/**
    Low-latency monophonic pitch tracker that runs ON the audio thread
    (guitar synth, guitar-to-MIDI).

    Input is low-passed and decimated to ~16 kHz; a McLeod detector analyses a
    40 ms window every 4 ms. 40 ms holds two periods of a 50 Hz note, so the
    whole guitar range (and drop tunings) is covered with ~20-30 ms latency on
    the low strings. Fast/slow envelopes are tracked per input sample for
    onset (re-pick) detection.

    Cost: ~150 k multiply-adds per 4 ms hop (well under 2 % of an M1 core).
    All memory is allocated in prepare(); process() is RT-safe.
*/
class RealtimePitchTracker
{
public:
    struct Estimate
    {
        bool valid = false;
        float frequency = 0.0f;
        float clarity = 0.0f;
        float fastDb = -120.0f;   // ~1 ms attack / 40 ms release peak envelope
        float slowDb = -120.0f;   // ~30 ms attack / 300 ms release
    };

    void prepare (double sampleRate)
    {
        decimation = std::max (1, (int) std::lround (sampleRate / 16000.0));
        const double rate = sampleRate / decimation;
        for (auto& f : antiAlias)
            f.setLowPass (sampleRate, (float) (0.4 * rate));
        detector.prepare (rate, kWindow, 50.0f, 1500.0f);
        history.assign (kWindow, 0.0f);
        window.assign (kWindow, 0.0f);
        fast.setTimes (sampleRate, 1.0f, 40.0f);
        slow.setTimes (sampleRate, 30.0f, 300.0f);
        reset();
    }

    void reset() noexcept
    {
        for (auto& f : antiAlias) f.reset();
        std::fill (history.begin(), history.end(), 0.0f);
        historyPos = sinceHop = phase = 0;
        fast.reset();
        slow.reset();
        latest = {};
    }

    /** Feeds mono input; calls onEstimate (sampleOffset, const Estimate&) every hop. */
    template <typename Callback>
    void process (const float* mono, int numSamples, Callback&& onEstimate) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float x = mono[i];
            fast.process (x);
            slow.process (x);

            const float filtered = antiAlias[1].process (antiAlias[0].process (x));
            if (++phase < decimation)
                continue;
            phase = 0;

            history[(size_t) historyPos] = filtered;
            historyPos = (historyPos + 1) % kWindow;
            if (++sinceHop < kHop)
                continue;
            sinceHop = 0;

            std::copy (history.begin() + historyPos, history.end(), window.begin());
            std::copy (history.begin(), history.begin() + historyPos, window.begin() + (kWindow - historyPos));
            const auto r = detector.analyse (window.data());

            latest.valid = r.valid;
            latest.frequency = r.frequency;
            latest.clarity = r.clarity;
            latest.fastDb = toDb (fast.getValue());
            latest.slowDb = toDb (slow.getValue());
            onEstimate (i, latest);
        }
    }

    const Estimate& getLatest() const noexcept { return latest; }

    static float toDb (float linear) noexcept { return 20.0f * std::log10 (std::max (linear, 1.0e-6f)); }

private:
    static constexpr int kWindow = 640;   // 40 ms at 16 kHz
    static constexpr int kHop = 64;       // 4 ms

    std::array<Biquad, 2> antiAlias;
    PitchDetector detector;
    std::vector<float> history, window;
    int decimation = 3, phase = 0, historyPos = 0, sinceHop = 0;
    EnvelopeFollower fast, slow;
    Estimate latest;
};
} // namespace kumamp
