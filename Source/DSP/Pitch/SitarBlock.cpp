#include "SitarBlock.h"
#include "../../Core/DspMath.h"

#include <cmath>
#include <iterator>
#include <string>

namespace kumamp
{
namespace
{
    constexpr ParamInfo kParams[] = {
        { "mode",      "Modo",          0.0f,   1.0f,  1.0f, "Procesado|Modelado", true },
        { "dry",       "Seco",          0.0f, 100.0f,  0.0f, "%",  false },
        { "lead",      "Lead",          0.0f, 100.0f, 80.0f, "%",  false },
        { "buzz",      "Zumbido",       0.0f, 100.0f, 60.0f, "%",  false },
        { "lead_tone", "Tono lead", -100.0f, 100.0f,  0.0f, "%",  false },
        { "symp",      "Simpaticas",    0.0f, 100.0f, 50.0f, "%",  false },
        { "symp_tone", "Tono simp", -100.0f, 100.0f,  0.0f, "%",  false },
        { "decay",     "Cola",          0.5f,  10.0f,  3.0f, "s",  false },
        { "key",       "Tonalidad",     0.0f,  11.0f,  4.0f, SitarBlock::kKeyChoices, true },
        { "scale",     "Escala",        0.0f,   8.0f,  2.0f, SitarBlock::kScaleChoices, true },
        { "bend",      "Bend",          0.0f,  12.0f,  0.0f, "st", false },
        { "freeze",    "Congelar",      0.0f,   1.0f,  0.0f, "Off|On", true },
        { "tanpura",        "Tampura",       0.0f,   1.0f,  0.0f, "Off|On", true },
        { "tanpura_level",  "Drone",         0.0f, 100.0f, 50.0f, "%",  false },
        { "tanpura_key",    "Tono tampura",  0.0f,  12.0f,  0.0f, SitarBlock::kTanpuraKeyChoices, true },
        { "tanpura_string", "1ra cuerda",    0.0f,   2.0f,  0.0f, "Pa|Ma|Ni", true },
        { "tanpura_cycle",  "Movimiento",    2.0f,  30.0f, 10.0f, "s",  false },
        { "tanpura_texture","Textura",       0.0f, 100.0f, 60.0f, "%",  false },
        { "tanpura_out",    "Salida tampura", 0.0f,  1.0f,  0.0f, "Directo|Con la cadena", true },
        { "tanpura_detune", "Desafinar",     0.0f,  10.0f,  3.0f, "cents", false },
        { "tanpura_breath", "Respiracion",   0.0f,  50.0f, 12.0f, "%",  false },
        { "tanpura_drift",  "Deriva",        0.0f, 100.0f, 100.0f, "%", false },
    };
    static_assert (std::size (kParams) == SitarBlock::NumParams);

    struct ScaleDef { int count; int degrees[7]; const char* shortName; };
    constexpr ScaleDef kScales[] = {
        { 7, { 0, 2, 4, 5, 7, 9, 11 }, "Bilawal" },
        { 7, { 0, 2, 4, 5, 7, 9, 10 }, "Khamaj" },
        { 7, { 0, 2, 3, 5, 7, 9, 10 }, "Kafi" },
        { 7, { 0, 2, 3, 5, 7, 8, 10 }, "Asavari" },
        { 7, { 0, 1, 4, 5, 7, 8, 11 }, "Bhairav" },
        { 7, { 0, 2, 4, 6, 7, 9, 11 }, "Yaman" },
        { 7, { 0, 1, 3, 6, 7, 8, 11 }, "Todi" },
        { 5, { 0, 2, 4, 7, 9 },        "Bhupali" },
        { 2, { 0, 7 },                 "Sa-Pa" },
    };
    constexpr const char* kKeyNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    constexpr float kGateDb = -55.0f;
    constexpr float kExcite = 0.05f;      // guitar into the sympathetic strings
    constexpr float kSympTrim = 1.0f;
    constexpr float kLeadTrim = 0.7f;
    constexpr float kMaxBuzzMs = 1.0f;    // deepest bridge "wrap"
    constexpr float kTanpuraTrim = 0.1f;

    // Modelled string.
    constexpr float kStringExcite = 0.3f;   // pluck amplitude at full velocity
    constexpr float kJawariRef = 0.5f;      // string amplitude of the deepest contact (harder picks buzz more)
    constexpr float kJawariLength = 0.08f;  // deepest contact: 8 % of the string over the bridge
    constexpr float kStringDcHz = 10.0f;
    constexpr float kHoldT60 = 6.0f, kReleaseT60 = 0.15f, kChokeT60 = 0.03f;
    constexpr float kModelTrim = 2.2f;
    constexpr float kAttackMix = 1.5f;
    constexpr float kPluckPoint = 0.15f;
} // namespace

SitarBlock::SitarBlock (StoreId id) : DSPBlock (id, BlockType::Sitar, kParams, NumParams) {}

SitarBlock::Tuning SitarBlock::tuningFor (int key, int scale) noexcept
{
    const auto& def = kScales[std::clamp (scale, 0, (int) std::size (kScales) - 1)];
    // Sa between A3 and G#4 (the taraf sits in the middle register), strings over two octaves.
    const int sa = 57 + (std::clamp (key, 0, 11) - 9 + 12) % 12;
    Tuning t;
    int count = 0;
    for (int octave = 0; octave <= 2 && count < kNumStrings; ++octave)
        for (int d = 0; d < def.count && count < kNumStrings; ++d)
        {
            const int note = sa + 12 * octave + def.degrees[d];
            if (note > sa + 24)
                break;
            t.notes[(size_t) count++] = note;
        }
    // Few notes (pentatonic, drone): the rest double the strings in slightly detuned unisons.
    for (int i = count, source = 0; i < kNumStrings; ++i, ++source)
    {
        t.notes[(size_t) i] = t.notes[(size_t) (source % count)];
        t.cents[(size_t) i] = (i % 2 == 0 ? 1.0f : -1.0f) * (3.0f + (float) (source / count));
    }
    return t;
}

std::array<int, 4> SitarBlock::tanpuraNotes (int key, int firstString) noexcept
{
    const int sa = 48 + std::clamp (key, 0, 11);   // C3 .. B3
    constexpr int firstOffset[] = { -5, -7, -1 };   // Pa, Ma (below Sa) or Ni
    return { sa + firstOffset[std::clamp (firstString, 0, 2)], sa, sa, sa - 12 };
}

bool SitarBlock::isParamRelevant (int param) const noexcept
{
    switch (param)
    {
        case TanpuraLevel: case TanpuraKey: case TanpuraString: case TanpuraMovement: case TanpuraTexture: case TanpuraOutput:
        case TanpuraDetune: case TanpuraBreath: case TanpuraDrift:
            return std::lround (getParam (Tanpura)) != 0;
        default:
            return true;
    }
}

std::vector<std::string> SitarBlock::getInfoChips() const
{
    const int key = std::clamp ((int) std::lround (getParam (Key)), 0, 11);
    const int scale = std::clamp ((int) std::lround (getParam (Scale)), 0, (int) std::size (kScales) - 1);
    const bool modelled = std::lround (getParam (Mode)) != 0;
    std::vector<std::string> chips { std::string (kKeyNames[key]) + " " + kScales[scale].shortName,
                                     modelled ? "Modelado" : "Sin latencia" };
    if (std::lround (getParam (Freeze)) != 0)
        chips.emplace_back ("Drone congelado");
    if (std::lround (getParam (Tanpura)) != 0)
    {
        const int choice = (int) std::lround (getParam (TanpuraKey));
        const int tanpuraKey = choice == 0 ? key : std::clamp (choice - 1, 0, 11);
        static const char* first[] { "Pa", "Ma", "Ni" };
        chips.push_back (std::string ("Tampura ") + kKeyNames[tanpuraKey] + " "
                         + first[std::clamp ((int) std::lround (getParam (TanpuraString)), 0, 2)]);
    }
    return chips;
}

void SitarBlock::prepareBlock (double sampleRate, int maxBlockSize)
{
    mono.assign ((size_t) maxBlockSize, 0.0f);
    leadLeft.assign ((size_t) maxBlockSize, 0.0f);
    leadRight.assign ((size_t) maxBlockSize, 0.0f);
    bender.prepare (sampleRate);
    tracker.prepare (sampleRate);
    jawari.line.prepare ((int) (0.002 * sampleRate) + 8);
    jawari.peak.setTimes (sampleRate, 0.5f, 1200.0f);
    jawari.dc.setCutoff (sampleRate, 20.0f);
    string.line.prepare ((int) (sampleRate / 40.0) + 8);
    string.dc.setCutoff (sampleRate, kStringDcHz);
    attackFilter.setHighPass (sampleRate, 1500.0f, 0.7071f);
    body.highPass.setHighPass (sampleRate, 110.0f, 0.7071f);
    body.gourd.setPeak (sampleRate, 230.0f, 1.4f, 3.0f);
    body.nasal.setPeak (sampleRate, 1150.0f, 1.1f, 4.0f);
    body.presence.setPeak (sampleRate, 3000.0f, 1.3f, 3.0f);
    body.lowPass.setLowPass (sampleRate, 10000.0f, 0.7071f);
    for (auto& s : strings)
        s.line.prepare ((int) (sampleRate / 200.0) + 8);   // lowest string: A3 (220 Hz), detuned
    drone.prepare (sampleRate);
    droneLeft.assign ((size_t) maxBlockSize, 0.0f);
    droneRight.assign ((size_t) maxBlockSize, 0.0f);
    tanpuraGain.prepare (sampleRate, 1.5);   // the drone swells in and fades out
    exciteHighPass.setHighPass (sampleRate, 150.0f, 0.7071f);
    fastEnvelope.setTimes (sampleRate, 0.3f, 25.0f);
    slowEnvelope.setTimes (sampleRate, 80.0f, 300.0f);
    for (auto* g : { &dryGain, &leadGain, &sympGain, &buzzDepth })
        g->prepare (sampleRate, 0.03);
    exciteGain.prepare (sampleRate, 0.2);
    resetBlock();
}

void SitarBlock::resetBlock() noexcept
{
    bender.reset();
    tracker.reset();
    jawari.reset();
    sweep.reset();
    leadShelf.reset();
    body.reset();
    string.reset();
    attackFilter.reset();
    attack = 0.0f;
    trackedHz = 0.0f;
    hasPitch = pluckPending = freshEstimate = false;
    pendingAge = silentFor = 0;
    noiseState = 0x9e3779b9u;
    exciteHighPass.reset();
    for (auto& s : strings)
    {
        s.line.reset();
        s.state = 0.0f;
    }
    fastEnvelope.reset();
    slowEnvelope.reset();
    noteHz = 220.0f;
    harmonic = 3.0f;
    pickPeak = 0.0f;
    sinceOnset = 1 << 20;
    drone.reset();
    droneDirect = false;
    tanpuraGain.setCurrentAndTarget (0.0f);   // swells in
    currentKey = currentScale = -1;
    currentDecay = currentTone = currentLeadTone = -1.0f;
    frozen = std::lround (getParam (Freeze)) != 0;
    retune();

    dryGain.setCurrentAndTarget (getParam (Dry) * 0.01f);
    leadGain.setCurrentAndTarget (getParam (Lead) * 0.01f * kLeadTrim);
    sympGain.setCurrentAndTarget (getParam (Sympathetic) * 0.01f * kSympTrim);
    buzzDepth.setCurrentAndTarget (getParam (Buzz) * 0.01f);
    exciteGain.setCurrentAndTarget (frozen ? 0.0f : kExcite);
    bender.setRatio (std::exp2 (getParam (Bend) / 12.0f), 0.0f);
}

void SitarBlock::retune() noexcept
{
    const int key = (int) std::lround (getParam (Key)), scale = (int) std::lround (getParam (Scale));
    const float decay = getParam (Decay), tone = getParam (SympTone) * 0.01f, leadTone = getParam (LeadTone) * 0.01f;
    const bool freeze = std::lround (getParam (Freeze)) != 0;
    const double sr = getSampleRate();

    if (! exactlyEqual (leadTone, currentLeadTone))
    {
        currentLeadTone = leadTone;
        leadShelf.setHighShelf (sr, 2500.0f, 9.0f * leadTone);
    }
    if (key == currentKey && scale == currentScale && exactlyEqual (decay, currentDecay)
        && exactlyEqual (tone, currentTone) && freeze == frozen)
        return;
    currentKey = key;
    currentScale = scale;
    currentDecay = decay;
    currentTone = tone;
    frozen = freeze;

    const auto tuning = tuningFor (key, scale);
    // Loop low-pass: brighter strings lose less of their highs per round trip.
    const float damp = std::clamp (0.35f - 0.3f * tone, 0.03f, 0.7f);
    for (int i = 0; i < kNumStrings; ++i)
    {
        auto& s = strings[(size_t) i];
        const float hz = 440.0f * std::exp2 ((float) (tuning.notes[(size_t) i] - 69) / 12.0f + tuning.cents[(size_t) i] / 1200.0f);
        // Round trip = read delay + 1 (push) + the low-pass's own delay at the fundamental.
        s.length = std::max (1.0f, (float) sr / hz - 1.0f - damp / (1.0f - damp));
        s.damp = damp;
        s.feedback = freeze ? 0.99998f : std::pow (10.0f, -3.0f / (hz * decay));
        // Spread the ring across the stereo field, alternating sides.
        const float position = (i % 2 == 0 ? -1.0f : 1.0f) * (0.25f + 0.6f * (float) (i / 2) / (kNumStrings / 2));
        const float angle = 0.25f * kPi * (1.0f + std::clamp (position, -1.0f, 1.0f));
        s.panLeft = std::cos (angle);
        s.panRight = std::sin (angle);
    }
}

float* SitarBlock::directBuffer (int channel) noexcept
{
    return droneDirect ? (channel == 0 ? droneLeft.data() : droneRight.data()) : nullptr;
}

float SitarBlock::nextNoise() noexcept
{
    noiseState ^= noiseState << 13;
    noiseState ^= noiseState >> 17;
    noiseState ^= noiseState << 5;
    return (float) noiseState * (2.0f / 4294967296.0f) - 1.0f;
}

float SitarBlock::velocityOf (float peak) noexcept
{
    // The pick's peak (-42 .. -6 dBFS) -> 0.25 .. 1.
    return std::clamp ((20.0f * std::log10 (peak + 1.0e-6f) + 42.0f) / 36.0f, 0.25f, 1.0f);
}

void SitarBlock::pluckString (float velocity) noexcept
{
    auto& s = string;
    // One round trip: the string's shape when the wire plectrum (mizrab) lets it go.
    s.exciteRemaining = s.exciteLength = std::max (2, (int) (getSampleRate() / s.hz));
    s.exciteLevel = kStringExcite * velocity;
    s.exciteState = 0.0f;
    s.shortenMean = 0.0f;
}

float SitarBlock::renderString (bool released, float depth) noexcept
{
    auto& s = string;
    const auto sr = (float) getSampleRate();
    // Round trip = read + 1 (push) + the loss filter's delay - the DC blocker's phase lead.
    const float lead = std::atan (kStringDcHz / s.hz) / (2.0f * kPi * s.hz) * sr;
    const float length = std::max (2.0f, sr / s.hz - 1.0f - s.damp / (1.0f - s.damp) + lead);

    // Jawari: while the string swings over the curved bridge (one side of the swing) it
    // touches the bridge further along, so the vibrating length shortens with the
    // displacement. Big at the pick, smaller as the note decays: the buzz sinks.
    const float u = std::min (1.5f, std::max (0.0f, s.last / kJawariRef));
    const float shorten = depth * kJawariLength * length * u;
    s.shortenMean += 0.002f * (shorten - s.shortenMean);
    // Most of the average shortening is given back, so the note stays in tune.
    const float out = s.line.read (length - shorten + 0.8f * s.shortenMean);
    s.last = out;

    float excite = 0.0f;
    if (s.exciteRemaining > 0)
    {
        // The plucked shape (a triangle peaking near the bridge, where the mizrab strikes)
        // with a little wire noise on top.
        const float p = 1.0f - (float) s.exciteRemaining / (float) s.exciteLength;
        --s.exciteRemaining;
        const float shape = (p < kPluckPoint ? p / kPluckPoint : (1.0f - p) / (1.0f - kPluckPoint)) - 0.5f;
        const float noise = nextNoise();
        s.exciteState = noise + 0.5f * (s.exciteState - noise);
        excite = s.exciteLevel * (shape + 0.3f * s.exciteState);
    }

    // Loop loss (bright steel), compensated at the fundamental so the T60 holds.
    s.state = out + s.damp * (s.state - out);
    const float w = 2.0f * kPi * s.hz / sr;
    const float lossGain = (1.0f - s.damp) / std::sqrt (1.0f - 2.0f * s.damp * std::cos (w) + s.damp * s.damp);
    // The bridge contact itself drains the string; the hold time gives that back.
    const float t60 = pluckPending ? kChokeT60 : released ? kReleaseT60 : kHoldT60 * (1.0f + 2.5f * depth);
    const float feedback = std::min (0.99995f, std::pow (10.0f, -3.0f / (s.hz * t60)) / lossGain);

    // Grazing contact: the excursion past the bridge is flattened (one-sided).
    float v = s.state;
    const float threshold = kJawariRef * (1.0f - 0.6f * depth);
    if (v > threshold)
        v = threshold + 0.7f * (v - threshold);
    v = s.dc.highPass (v);
    s.line.push (excite + feedback * v);
    return out;
}

float SitarBlock::Jawari::process (float x, float depth, float maxDelay) noexcept
{
    // Normalised by the attack peak (long release): the contact is hardest on the pick
    // and eases off as the string decays.
    const float env = peak.process (x) + 1.0e-5f;
    line.push (x);
    const float u = std::max (0.0f, x / env);   // the string wraps over the bridge on one side only
    const float y = line.read (1.0f + depth * maxDelay * u * u);

    // Grazing contact: the excursion past the bridge is flattened.
    float v = y / env;
    const float threshold = 1.0f - 0.8f * depth;
    if (v > threshold)
        v = threshold + 0.05f * (v - threshold);
    return dc.highPass (v * env);
}

void SitarBlock::processBlock (float* left, float* right, int numSamples) noexcept
{
    retune();
    const double sr = getSampleRate();
    const auto srf = (float) sr;
    dryGain.setTarget (getParam (Dry) * 0.01f);
    leadGain.setTarget (getParam (Lead) * 0.01f * kLeadTrim);
    sympGain.setTarget (getParam (Sympathetic) * 0.01f * kSympTrim);
    buzzDepth.setTarget (getParam (Buzz) * 0.01f);
    exciteGain.setTarget (frozen ? 0.0f : kExcite);
    const bool modelled = std::lround (getParam (Mode)) != 0;
    string.damp = std::clamp (0.07f - 0.06f * getParam (LeadTone) * 0.01f, 0.01f, 0.2f);
    const float bendRatio = std::exp2 (getParam (Bend) / 12.0f);
    // Tampura: rendered for the block, then sent down the chain or straight to the output.
    const bool tanpuraOn = std::lround (getParam (Tanpura)) != 0;
    tanpuraGain.setTarget (tanpuraOn ? getParam (TanpuraLevel) * 0.01f * kTanpuraTrim : 0.0f);
    const bool renderDrone = tanpuraOn || tanpuraGain.getCurrent() > 0.0f;
    droneDirect = renderDrone && std::lround (getParam (TanpuraOutput)) == 0;
    if (renderDrone)
    {
        const int choice = (int) std::lround (getParam (TanpuraKey));
        const int key = choice == 0 ? (int) std::lround (getParam (Key)) : choice - 1;
        drone.setNotes (tanpuraNotes (key, (int) std::lround (getParam (TanpuraString))));
        drone.process (droneLeft.data(), droneRight.data(), numSamples, getParam (TanpuraTexture) * 0.01f, getParam (TanpuraMovement),
                       { getParam (TanpuraDetune), getParam (TanpuraBreath) * 0.01f, getParam (TanpuraDrift) * 0.01f });
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = tanpuraGain.next();
            droneLeft[(size_t) i] *= g;
            droneRight[(size_t) i] *= g;
        }
    }

    for (int i = 0; i < numSamples; ++i)
        mono[(size_t) i] = 0.5f * (left[i] + right[i]);
    if (! modelled)
        bender.process (mono.data(), mono.data(), leadLeft.data(), leadRight.data(), numSamples);

    const float maxDelay = kMaxBuzzMs * 0.001f * srf;
    const float gate = dbToGain (kGateDb);
    const int refractory = (int) (0.06f * srf);
    const float sweepCoefficient = EnvelopeFollower::coefficient (sr, 350.0f);
    const float pickPeakDecay = EnvelopeFollower::coefficient (sr, 300.0f);
    const int minWait = (int) (0.010f * srf), maxWait = (int) (0.060f * srf), silence = (int) (0.2f * srf);
    const float attackDecay = EnvelopeFollower::coefficient (sr, 15.0f);
    const float detection = getDetectionGain();
    const float slide = EnvelopeFollower::coefficient (sr, 12.0f), jump = EnvelopeFollower::coefficient (sr, 3.0f);

    for (int i = 0; i < numSamples; ++i)
    {
        const float x = mono[(size_t) i];
        const float droneL = renderDrone ? droneLeft[(size_t) i] : 0.0f, droneR = renderDrone ? droneRight[(size_t) i] : 0.0f;

        const float heard = x * detection;
        tracker.process (&heard, 1, [&] (int, const RealtimePitchTracker::Estimate& e)
        {
            if (e.valid && e.fastDb > kGateDb)
            {
                noteHz = e.frequency * bendRatio;
                trackedHz = e.frequency;
                freshEstimate = true;
            }
        });

        // A pick restarts the sweep of the bridge "formant" from a high harmonic.
        const float fast = fastEnvelope.process (heard), slow = slowEnvelope.process (fast);
        pickPeak = sinceOnset < refractory ? std::max (pickPeak, fast) : pickPeak * pickPeakDecay;
        if (++sinceOnset > refractory && fast > gate && fast > 1.5f * std::max (slow, pickPeak) + 1.0e-4f)
        {
            sinceOnset = 0;
            pickPeak = fast;
            harmonic = 14.0f;
            // Modelled: the pluck waits for the tracker to read the new note.
            pluckPending = true;
            pendingAge = 0;
            attack = 1.0f;
        }

        if (modelled)
        {
            silentFor = fast < gate ? silentFor + 1 : 0;
            if (silentFor > silence)
                hasPitch = false;
            if (pluckPending)
            {
                ++pendingAge;
                if (freshEstimate && pendingAge >= minWait)
                {
                    string.hz = string.targetHz = trackedHz * bendRatio;
                    hasPitch = true;
                    pluckPending = false;
                    pluckString (velocityOf (pickPeak));
                }
                else if (pendingAge >= maxWait)
                {
                    pluckPending = false;
                    if (hasPitch)
                        pluckString (velocityOf (pickPeak));
                }
            }
            else if (freshEstimate && hasPitch)
                string.targetHz = trackedHz * bendRatio;
            // Slides and bends glide (meend); a jump to another note is quick.
            const float ratio = string.targetHz / string.hz;
            const float c = (ratio > 1.09f || ratio < 0.917f) ? jump : slide;
            string.hz = std::clamp (string.targetHz + c * (string.hz - string.targetHz), 40.0f, 0.2f * srf);
            noteHz = string.hz;
        }
        freshEstimate = false;
        harmonic = 2.5f + sweepCoefficient * (harmonic - 2.5f);
        if ((i & 15) == 0)
            sweep.set (sr, std::clamp (noteHz * harmonic, 200.0f, 0.4f * srf), 4.0f);

        // Lead: bent guitar -> bridge contact -> sweeping resonance -> thin, bright body.
        const float depth = buzzDepth.next();
        float lead;
        if (modelled)
        {
            // The pick itself (the guitar's attack, thin and bright) bridges the few ms the
            // tracker takes to read the note: the click of the mizrab.
            attack *= attackDecay;
            lead = kModelTrim * renderString (fast < gate, depth) + kAttackMix * attack * attackFilter.process (x);
            lead += 1.0f * depth * sweep.process (lead).band;
        }
        else
        {
            lead = jawari.process (leadLeft[(size_t) i], depth, maxDelay);
            lead += 2.5f * depth * sweep.process (lead).band;
        }
        lead = leadShelf.process (body.process (lead));

        // Sympathetic strings, excited by the dry guitar.
        // The drone rings the sympathetic strings a little, as in a room.
        const float excite = exciteHighPass.process (x + 0.25f * (droneL + droneR)) * exciteGain.next();
        float sympLeft = 0.0f, sympRight = 0.0f;
        for (auto& s : strings)
        {
            const float out = s.line.read (s.length);
            s.state = out + s.damp * (s.state - out);
            s.line.push (excite + s.feedback * s.state);
            sympLeft += out * s.panLeft;
            sympRight += out * s.panRight;
        }

        const float d = dryGain.next(), l = leadGain.next() * lead, g = sympGain.next();
        left[i]  = d * left[i]  + l + g * sympLeft  + (droneDirect ? 0.0f : droneL);
        right[i] = d * right[i] + l + g * sympRight + (droneDirect ? 0.0f : droneR);
    }
}
} // namespace kumamp
