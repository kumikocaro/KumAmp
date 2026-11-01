#pragma once

#include "WsolaPitchShifter.h"
#include "../../Core/Biquad.h"
#include "../../Core/DspPrimitives.h"
#include "../../Core/Smoothing.h"
#include "../../Graph/DSPBlock.h"

#include <array>
#include <vector>

namespace kumamp
{
/** Pedal wah: resonant TPT state-variable filter swept exponentially between
    two corner frequencies. "Posicion" is meant for an expression pedal (macro). */
class WahBlock final : public DSPBlock
{
public:
    enum Param { Position, Resonance, LowHz, HighHz, LevelDb, NumParams };
    explicit WahBlock (StoreId id);

protected:
    void prepareBlock (double, int) override;
    void resetBlock() noexcept override;
    void processBlock (float*, float*, int) noexcept override;

private:
    std::array<StateVariableFilter, 2> filters;
    float position = 0.5f, positionCoefficient = 0.0f;
    LinearSmoother level;
};

/** Envelope-controlled wah (up or down sweep). */
class AutoWahBlock final : public DSPBlock
{
public:
    enum Param { Sensitivity, AttackMs, ReleaseMs, LowHz, HighHz, Resonance, Direction, Mix, NumParams };
    explicit AutoWahBlock (StoreId id);

protected:
    void prepareBlock (double, int) override;
    void resetBlock() noexcept override;
    void processBlock (float*, float*, int) noexcept override;

private:
    std::array<StateVariableFilter, 2> filters;
    EnvelopeFollower envelope;
    LinearSmoother mix;
};

/** Polyphonic octaver (POG-style): -1 and +1 octave voices from two WSOLA
    shifters, so chords track cleanly. Voices at -60 dB are not computed. */
class OctaverBlock final : public DSPBlock
{
public:
    enum Param { DryDb, SubDb, UpDb, ToneHz, NumParams };
    explicit OctaverBlock (StoreId id);

protected:
    void prepareBlock (double, int) override;
    void resetBlock() noexcept override;
    void processBlock (float*, float*, int) noexcept override;

private:
    WsolaPitchShifter down, up;
    std::vector<float> downL, downR, upL, upR;
    std::array<Biquad, 2> tone;
    LinearSmoother dry, sub, octUp;
};
} // namespace kumamp
