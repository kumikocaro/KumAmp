#include "CharangoBlock.h"
#include "../../Core/DspMath.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace kumamp
{
namespace
{
    constexpr ParamInfo kParams[] = {
        { "mode",     "Modo",      0.0f,   1.0f,   0.0f, "Procesado|Modelado", true },
        { "register", "Registro",  0.0f,   1.0f,   0.0f, "+1 octava|Guitarra", true },
        { "courses",  "Ordenes",   0.0f, 100.0f,  60.0f, "%",  false },
        { "octavado", "Octavado",  0.0f, 100.0f,  35.0f, "%",  false },
        { "body",     "Cuerpo",    0.0f, 100.0f,  70.0f, "%",  false },
        { "sustain",  "Sustain",   0.0f, 100.0f,  40.0f, "%",  false },
        { "tone",     "Tono",   -100.0f, 100.0f,   0.0f, "%",  false },
        { "strum",    "Rasgueo",   0.0f,  40.0f,  12.0f, "ms", false },
        { "mix",      "Mezcla",    0.0f, 100.0f, 100.0f, "%",  false },
        { "level",    "Nivel",   -24.0f,  12.0f,   0.0f, "dB", false },
    };
    static_assert (std::size (kParams) == CharangoBlock::NumParams);

    constexpr const char* kNoteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    constexpr float kGateDb = -55.0f;
    constexpr float kProcessedTrim = 1.25f;
    constexpr float kModelledTrim = 2.2f;
    constexpr float kExcite = 0.5f;
} // namespace

CharangoBlock::CharangoBlock (StoreId id) : DSPBlock (id, BlockType::Charango, kParams, NumParams) {}

bool CharangoBlock::isParamRelevant (int param) const noexcept
{
    const bool modelled = std::lround (getParam (Mode)) == (long) Engine::Modelled;
    switch (param)
    {
        case Register: case Courses: return ! modelled;
        case Strum:                  return modelled;
        default:                     return true;
    }
}

std::string CharangoBlock::getHeldNotesText() const
{
    const auto packed = voicingForUi.load (std::memory_order_relaxed);
    std::string text;
    for (int i = 0; i < (int) (packed & 0xff); ++i)
    {
        const int note = (int) ((packed >> (8 * (i + 1))) & 0xff);
        if (! text.empty())
            text += ' ';
        text += kNoteNames[note % 12] + std::to_string (note / 12 - 1);
    }
    return text;
}

std::vector<std::string> CharangoBlock::getInfoChips() const
{
    std::vector<std::string> chips { "Temple natural" };
    if (std::lround (getParam (Mode)) == (long) Engine::Modelled)
    {
        const auto notes = getHeldNotesText();
        chips.push_back (notes.empty() ? "Modelado" : notes);
    }
    else
    {
        chips.emplace_back (std::lround (getParam (Register)) == 0 ? "+1 octava" : "Registro guitarra");
    }
    return chips;
}

int CharangoBlock::foldVoicing (const std::uint8_t* notes, int count, std::uint8_t* out) noexcept
{
    int n = 0;
    for (int i = 0; i < count; ++i)
    {
        const auto folded = (std::uint8_t) (kLowestFold + (((int) notes[i] - kLowestFold) % 12 + 12) % 12);
        int at = 0;
        while (at < n && out[at] < folded)
            ++at;
        if (at < n && out[at] == folded)
            continue;
        for (int j = n; j > at; --j)
            out[j] = out[j - 1];
        out[at] = folded;
        ++n;
    }
    return n;
}

void CharangoBlock::prepareBlock (double sampleRate, int maxBlockSize)
{
    for (auto* b : { &mono, &shifted, &scratch, &outLeft, &outRight })
        b->assign ((size_t) maxBlockSize, 0.0f);
    shifter.prepare (sampleRate);
    courseLine.prepare ((int) (0.01 * sampleRate) + 8);
    sustainFast.setTimes (sampleRate, 1.0f, 60.0f);
    sustainPeak.setTimes (sampleRate, 0.5f, 2500.0f);
    detector.prepare (sampleRate);
    fastEnvelope.setTimes (sampleRate, 0.3f, 25.0f);
    slowEnvelope.setTimes (sampleRate, 80.0f, 300.0f);
    for (auto& c : courses)
        for (auto& s : c.strings)
            s.line.prepare ((int) (sampleRate / 300.0) + 8);   // lowest string: E4 (330 Hz)
    for (auto* s : { &mix, &level, &courseAmount, &octaveAmount })
        s->prepare (sampleRate, 0.03);
    resetBlock();
}

void CharangoBlock::resetBlock() noexcept
{
    shifter.reset();
    shifter.setRatio (2.0f, 0.0f);
    courseLine.reset();
    coursePhase = 0.0;
    sustainFast.reset();
    sustainPeak.reset();
    detector.reset();
    for (auto& c : courses)
    {
        c.note = -1;
        c.held = false;
        c.pluckIn = -1;
        for (auto& s : c.strings)
            s.reset();
    }
    voicingCount = 0;
    voicingForUi.store (0, std::memory_order_relaxed);
    fastEnvelope.reset();
    slowEnvelope.reset();
    pickPeak = 0.0f;
    sinceOnset = sinceStrum = 1 << 20;
    strumWait = 0;
    strumPending = strumUp = false;
    for (auto& b : body) b.reset();
    currentBody = currentTone = currentSustain = -1.0f;
    currentEngine = -1;

    mix.setCurrentAndTarget (getParam (Mix) * 0.01f);
    level.setCurrentAndTarget (dbToGain (getParam (LevelDb)));
    courseAmount.setCurrentAndTarget (getParam (Courses) * 0.01f);
    octaveAmount.setCurrentAndTarget (getParam (Octavado) * 0.01f);
}

float CharangoBlock::nextNoise() noexcept
{
    noiseState ^= noiseState << 13;
    noiseState ^= noiseState >> 17;
    noiseState ^= noiseState << 5;
    return (float) noiseState * (2.0f / 4294967296.0f) - 1.0f;
}

//==============================================================================
void CharangoBlock::renderProcessed (int numSamples) noexcept
{
    const auto sr = (float) getSampleRate();
    shifter.process (mono.data(), mono.data(), shifted.data(), scratch.data(), numSamples);

    const bool octaveUp = std::lround (getParam (Register)) == 0;
    // Nylon and a small body: the decay is pulled down relative to the pick.
    const float shorten = 1.5f * (1.0f - getParam (Sustain) * 0.01f);
    const double rate = 1.0 / getSampleRate();

    for (int i = 0; i < numSamples; ++i)
    {
        const float x = mono[(size_t) i], up = shifted[(size_t) i];
        const float main = octaveUp ? up : x, other = octaveUp ? x : up;

        // The course's second string: a slightly detuned (~3 cents), later copy on each side.
        courseLine.push (main);
        coursePhase += rate;
        const auto t = (float) coursePhase;
        const float tapLeft  = courseLine.read (sr * (0.005f + 0.00035f * std::sin (2.0f * kPi * 0.83f * t)));
        const float tapRight = courseLine.read (sr * (0.007f + 0.00035f * std::sin (2.0f * kPi * 1.13f * t + 1.0f)));
        const float c = courseAmount.next(), o = 0.7f * octaveAmount.next();
        const float norm = 1.0f / (1.0f + 0.5f * c);

        const float fast = sustainFast.process (x), peak = sustainPeak.process (x) + 1.0e-6f;
        const float gain = shorten > 0.0f ? std::pow (std::min (1.0f, fast / peak), shorten) : 1.0f;

        outLeft[(size_t) i]  = ((main + c * tapLeft)  * norm + o * other) * gain * kProcessedTrim;
        outRight[(size_t) i] = ((main + c * tapRight) * norm + o * other) * gain * kProcessedTrim;
    }
    if (coursePhase > 1000.0)
        coursePhase -= 1000.0;
}

//==============================================================================
void CharangoBlock::tuneCourse (Course& c, int note) noexcept
{
    const auto sr = (float) getSampleRate();
    const float tone = getParam (Tone) * 0.01f;
    const float sustain = getParam (Sustain) * 0.01f;
    const float t60 = 1.0f + 3.0f * sustain;
    const float damp = std::clamp (0.35f - 0.25f * tone, 0.08f, 0.7f);
    const int index = (int) (&c - courses.data());

    c.note = note;
    for (int k = 0; k < 2; ++k)
    {
        auto& s = c.strings[(size_t) k];
        // Unison pair a few cents apart; the E course is in octaves (E4 + E5), as on a charango.
        const bool octaveString = k == 1 && note % 12 == 4;
        const int stringNote = octaveString ? note + 12 : note;
        const float hz = 440.0f * std::exp2 ((float) (stringNote - 69) / 12.0f + (k == 0 ? -1.5f : 1.5f) / 1200.0f);
        s.length = std::max (1.0f, sr / hz - 1.0f - damp / (1.0f - damp));
        s.damp = damp;
        // The loop low-pass already loses some of the fundamental each round trip: the
        // feedback makes up for it, so the note's T60 is the one asked for.
        const float w = 2.0f * kPi * hz / sr;
        const float lowPassGain = (1.0f - damp) / std::sqrt (1.0f - 2.0f * damp * std::cos (w) + damp * damp);
        s.holdFeedback = std::min (0.9999f, std::pow (10.0f, -3.0f / (hz * t60)) / lowPassGain);
        s.releaseFeedback = std::pow (10.0f, -3.0f / (hz * 0.12f));
        s.level = octaveString ? std::max (0.2f, getParam (Octavado) * 0.01f * 1.5f) : 1.0f;
        const float position = -0.6f + 1.2f * (float) index / (float) (kMaxCourses - 1) + (k == 0 ? -0.15f : 0.15f);
        const float angle = 0.25f * kPi * (1.0f + std::clamp (position, -1.0f, 1.0f));
        s.panLeft = std::cos (angle);
        s.panRight = std::sin (angle);
    }
}

void CharangoBlock::pluck (Course& c) noexcept
{
    for (auto& s : c.strings)
    {
        s.exciteRemaining = (int) s.length + 1;   // one round trip of noise: the classic plucked string
        s.exciteLevel = kExcite * c.velocity;
        s.exciteState = 0.0f;
        s.state *= 0.3f;                          // the finger / pick stops the old vibration
    }
}

void CharangoBlock::strum() noexcept
{
    // Fast picks alternate the stroke (rasgueo): down = low to high, up = high to low.
    strumUp = sinceStrum < (int) (0.3 * getSampleRate()) ? ! strumUp : false;
    sinceStrum = 0;
    const int spacing = (int) (getParam (Strum) * 0.001f * (float) getSampleRate());

    std::array<int, kMaxCourses> order {};
    int count = 0;
    for (int i = 0; i < kMaxCourses; ++i)
        if (courses[(size_t) i].held)
            order[(size_t) count++] = i;
    std::sort (order.begin(), order.begin() + count,
               [this] (int a, int b) { return courses[(size_t) a].note < courses[(size_t) b].note; });
    for (int k = 0; k < count; ++k)
    {
        auto& c = courses[(size_t) order[(size_t) (strumUp ? count - 1 - k : k)]];
        c.velocity = pickVelocity * (strumUp ? 0.85f : 1.0f);
        c.pluckIn = k * spacing;
    }
}

void CharangoBlock::onNotes (const std::uint8_t* notes, int count) noexcept
{
    std::array<std::uint8_t, kMaxCourses> next {};
    const int n = foldVoicing (notes, count, next.data());
    if (n == voicingCount && std::equal (next.begin(), next.begin() + n, voicing.begin()))
        return;

    for (auto& c : courses)
        c.held = c.held && std::find (next.begin(), next.begin() + n, (std::uint8_t) c.note) != next.begin() + n;

    for (int i = 0; i < n; ++i)
    {
        const int note = next[(size_t) i];
        Course* course = nullptr;
        for (auto& c : courses)
            if (c.note == note)
                course = &c;
        if (course != nullptr && course->held)
            continue;
        const bool fresh = course == nullptr;
        if (fresh)
        {
            for (auto& c : courses)   // a free course, else one that is only ringing out
                if (! c.held && (course == nullptr || c.note < 0))
                    course = &c;
            if (course == nullptr)
                continue;
            tuneCourse (*course, note);
        }
        course->held = true;
        // A note that shows up between picks (a late low string, a hammer-on) is plucked on
        // its own; a pending strum takes care of the rest.
        if (fresh && ! strumPending)
        {
            course->velocity = pickVelocity * (sinceStrum < (int) (0.15 * getSampleRate()) ? 1.0f : 0.6f);
            course->pluckIn = 0;
        }
    }

    voicing = next;
    voicingCount = n;
    std::uint64_t packed = (std::uint64_t) n;
    for (int i = 0; i < n && i < 7; ++i)
        packed |= (std::uint64_t) next[(size_t) i] << (8 * (i + 1));
    voicingForUi.store (packed, std::memory_order_relaxed);
}

void CharangoBlock::renderModelled (int numSamples) noexcept
{
    const auto srf = (float) getSampleRate();
    const float gate = dbToGain (kGateDb);
    const int refractory = (int) (0.06f * srf);
    const float pickPeakDecay = EnvelopeFollower::coefficient (getSampleRate(), 300.0f);
    const float brightness = std::clamp (0.5f - 0.3f * getParam (Tone) * 0.01f, 0.1f, 0.9f);
    const float detection = getDetectionGain();

    for (int i = 0; i < numSamples; ++i)
    {
        const float x = mono[(size_t) i] * detection;

        const float fast = fastEnvelope.process (x), slow = slowEnvelope.process (fast);
        pickPeak = sinceOnset < refractory ? std::max (pickPeak, fast) : pickPeak * pickPeakDecay;
        if (++sinceOnset > refractory && fast > gate && fast > 1.5f * std::max (slow, pickPeak) + 1.0e-4f)
        {
            sinceOnset = 0;
            pickPeak = fast;
            pickVelocity = std::clamp ((20.0f * std::log10 (fast) + 42.0f) / 36.0f, 0.25f, 1.0f);
            strumPending = true;
            strumWait = 0;
        }

        detector.process (&x, 1, [&] (int, const PolyNoteDetector::NoteSet& set)
        {
            onNotes (set.notes.data(), set.count);
            // Strum once the pick has settled (or after ~60 ms).
            ++strumWait;
            if (strumPending && ((! detector.isSettling() && strumWait >= 2) || strumWait > 15))
            {
                strumPending = false;
                strum();
            }
        });
        ++sinceStrum;

        float l = 0.0f, r = 0.0f;
        for (auto& c : courses)
        {
            if (c.note < 0)
                continue;
            if (c.pluckIn >= 0 && c.pluckIn-- == 0)
                pluck (c);
            for (auto& s : c.strings)
            {
                float excite = 0.0f;
                if (s.exciteRemaining > 0)
                {
                    --s.exciteRemaining;
                    const float noise = nextNoise();
                    s.exciteState = noise + brightness * (s.exciteState - noise);
                    excite = s.exciteLevel * s.exciteState;
                }
                const float out = s.line.read (s.length);
                s.state = out + s.damp * (s.state - out);
                s.line.push (excite + (c.held ? s.holdFeedback : s.releaseFeedback) * s.state);
                l += out * s.level * s.panLeft;
                r += out * s.level * s.panRight;
            }
        }
        outLeft[(size_t) i] = l * kModelledTrim;
        outRight[(size_t) i] = r * kModelledTrim;
    }
}

//==============================================================================
void CharangoBlock::processBlock (float* left, float* right, int numSamples) noexcept
{
    const double sr = getSampleRate();
    const int engine = (int) std::lround (getParam (Mode));
    if (engine != currentEngine)
    {
        // Switching engines starts from silence (no stale strings or splices).
        if (currentEngine >= 0)
        {
            for (auto& c : courses) { c.note = -1; c.held = false; c.pluckIn = -1; for (auto& s : c.strings) s.reset(); }
            voicingCount = 0;
            voicingForUi.store (0, std::memory_order_relaxed);
            detector.reset();
        }
        currentEngine = engine;
    }

    const float bodyAmount = getParam (Body) * 0.01f, tone = getParam (Tone) * 0.01f, sustain = getParam (Sustain);
    if (! exactlyEqual (bodyAmount, currentBody) || ! exactlyEqual (tone, currentTone) || ! exactlyEqual (sustain, currentSustain))
    {
        currentBody = bodyAmount;
        currentTone = tone;
        currentSustain = sustain;
        for (auto& b : body)
        {
            b.highPass.setHighPass (sr, 60.0f + 130.0f * bodyAmount, 0.8f);
            b.air.setPeak (sr, 420.0f, 2.5f, 5.0f * bodyAmount);
            b.plate.setPeak (sr, 1100.0f, 1.8f, 3.5f * bodyAmount);
            b.presence.setPeak (sr, 3000.0f, 1.2f, 3.0f * bodyAmount);
            b.top.setHighShelf (sr, 4500.0f, 7.0f * tone);
            b.lowPass.setLowPass (sr, 11000.0f);
        }
        for (auto& c : courses)   // the strings' damping and decay follow tone and sustain
            if (c.note >= 0)
                tuneCourse (c, c.note);
    }

    mix.setTarget (getParam (Mix) * 0.01f);
    level.setTarget (dbToGain (getParam (LevelDb)));
    courseAmount.setTarget (getParam (Courses) * 0.01f);
    octaveAmount.setTarget (getParam (Octavado) * 0.01f);

    for (int i = 0; i < numSamples; ++i)
        mono[(size_t) i] = 0.5f * (left[i] + right[i]);

    if (engine == (int) Engine::Modelled)
        renderModelled (numSamples);
    else
        renderProcessed (numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        const float m = mix.next(), g = level.next();
        const float wetLeft = body[0].process (outLeft[(size_t) i]), wetRight = body[1].process (outRight[(size_t) i]);
        left[i]  = (left[i]  + (wetLeft  - left[i])  * m) * g;
        right[i] = (right[i] + (wetRight - right[i]) * m) * g;
    }
}
} // namespace kumamp
