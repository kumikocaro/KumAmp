#include "SamplerBlock.h"
#include "../../Core/DspMath.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>

namespace kumamp
{
namespace
{
    constexpr ParamInfo kParams[] = {
        { "playback",  "Disparo",     0.0f,   2.0f,   1.0f, "Una vez|Sostener|Loop", true },
        { "notes",     "Notas",       0.0f,   1.0f,   0.0f, "Una nota|Acordes", true },
        { "follow",    "Seguimiento", 0.0f,   1.0f,   0.0f, "Semitono|Libre", true },
        { "transpose", "Octava",      0.0f,   4.0f,   2.0f, "-2|-1|0|+1|+2", true },
        { "attack",    "Ataque",      1.0f, 500.0f,   2.0f, "ms", false },
        { "release",   "Release",     5.0f, 2000.0f, 150.0f, "ms", false },
        { "dynamics",  "Dinamica",    0.0f, 100.0f,  70.0f, "%",  false },
        { "crossfade", "Cruce loop",  1.0f, 500.0f,  30.0f, "ms", false },
        { "mix",       "Mezcla",      0.0f, 100.0f, 100.0f, "%",  false },
        { "level",     "Nivel",     -24.0f,  12.0f,   0.0f, "dB", false },
        { "start",     "Inicio",      0.0f, 100.0f,   0.0f, "%",  false },
        { "end",       "Fin",         0.0f, 100.0f, 100.0f, "%",  false },
        { "root",      "Nota base",   0.0f,  12.0f,   0.0f, "Auto|C|C#|D|D#|E|F|F#|G|G#|A|A#|B", true },
        { "root_oct",  "Octava base", 0.0f,   5.0f,   3.0f, "1|2|3|4|5|6", true },
        { "tune",      "Afinar",   -100.0f, 100.0f,   0.0f, "cents", false },
    };
    static_assert (std::size (kParams) == SamplerBlock::NumParams);

    constexpr const char* kNoteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    constexpr double kMaxSeconds = 60.0;
    constexpr float kTargetPeak = 0.5f;          // -6 dB: one voice leaves room for a chord
    constexpr float kSilenceDb = -50.0f;         // relative to the normalised peak
    constexpr double kMinRegionSeconds = 0.005;
    constexpr double kRegionFadeSeconds = 0.003;
    constexpr float kChokeMs = 12.0f;
    constexpr float kGateDb = -55.0f;
    constexpr float kClarity = 0.85f;
    constexpr float kDetectClarity = 0.9f;
    constexpr double kDetectSeconds = 4.0;

    /** Half-band low-pass (windowed sinc), then keep every second sample; stays centred. */
    std::vector<float> decimate (const std::vector<float>& in)
    {
        static constexpr std::array<float, 15> kTaps { -0.0026f, 0.0f, 0.0126f, 0.0f, -0.0448f, 0.0f, 0.2905f, 0.5f,
                                                        0.2905f, 0.0f, -0.0448f, 0.0f, 0.0126f, 0.0f, -0.0026f };
        std::vector<float> out (in.size() / 2);
        const int size = (int) in.size();
        for (int i = 0; i < (int) out.size(); ++i)
        {
            float sum = 0.0f;
            for (int t = 0; t < (int) kTaps.size(); ++t)
                sum += kTaps[(size_t) t] * in[(size_t) std::clamp (2 * i + t - 7, 0, size - 1)];
            out[(size_t) i] = sum;
        }
        return out;
    }

    std::shared_ptr<SamplerSound> decode (const juce::File& file, juce::String& error)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        {
            error = "no se pudo leer el archivo";
            return nullptr;
        }

        auto sound = std::make_shared<SamplerSound>();
        sound->rate = reader->sampleRate;
        sound->fileName = file.getFileName().toStdString();
        const auto limit = (juce::int64) (kMaxSeconds * reader->sampleRate);
        sound->truncated = reader->lengthInSamples > limit;
        sound->length = (int) std::min (reader->lengthInSamples, limit);

        const bool stereo = reader->numChannels > 1;
        juce::AudioBuffer<float> buffer (stereo ? 2 : 1, sound->length);
        reader->read (&buffer, 0, sound->length, 0, true, stereo);

        const float peak = buffer.getMagnitude (0, sound->length);
        if (peak > 1.0e-6f)
            buffer.applyGain (kTargetPeak / peak);

        sound->left[0].assign (buffer.getReadPointer (0), buffer.getReadPointer (0) + sound->length);
        if (stereo)
            sound->right[0].assign (buffer.getReadPointer (1), buffer.getReadPointer (1) + sound->length);
        for (size_t k = 1; k < SamplerSound::kNumLevels; ++k)
        {
            sound->left[k] = decimate (sound->left[k - 1]);
            if (stereo)
                sound->right[k] = decimate (sound->right[k - 1]);
        }
        return sound;
    }

    juce::String secondsText (double seconds)
    {
        return juce::String (seconds, seconds < 10.0 ? 2 : 1).replaceCharacter ('.', ',') + " s";
    }
} // namespace

SamplerBlock::SamplerBlock (StoreId id)
    : DSPBlock (id, BlockType::Sampler, kParams, NumParams),
      shared (std::make_shared<Shared>())
{
    shared->status = "Sin sonido";
}

SamplerBlock::~SamplerBlock()
{
    ++shared->generation;
    ++shared->detection;
}

bool SamplerBlock::isParamRelevant (int param) const noexcept
{
    const auto play = (Play) std::lround (getParam (Playback));
    switch (param)
    {
        case Follow:    return std::lround (getParam (Notes)) == 0;
        case Crossfade: return play == Play::Loop;
        case ReleaseMs: return play != Play::Once;
        case Start: case End: case RootNote: case RootOctave: case Tune:
            return false;   // in the sound editor
        default:        return true;
    }
}

bool SamplerBlock::loadFile (int slot, const std::string& path)
{
    if (slot != 0 || path.empty())
        return false;

    {
        const std::lock_guard<std::mutex> guard (shared->lock);
        // Another sound: its trim starts over. (A preset restores its trim before its path.)
        if (! shared->requestedPath.empty() && shared->requestedPath != path)
        {
            setParam (Start, kParams[Start].defaultValue);
            setParam (End, kParams[End].defaultValue);
        }
        shared->requestedPath = path;
        shared->status = "Cargando...";
    }

    if (isPrepared())
        startLoad();
    return true;
}

std::string SamplerBlock::getFilePath (int slot) const
{
    if (slot != 0)
        return {};
    const std::lock_guard<std::mutex> guard (shared->lock);
    return shared->requestedPath;
}

std::string SamplerBlock::getStatusText() const
{
    const std::lock_guard<std::mutex> guard (shared->lock);
    return shared->status;
}

std::vector<std::string> SamplerBlock::getInfoChips() const
{
    if (getSound() == nullptr)
        return { "Sin sonido" };
    return { "Base " + getRootText(), std::lround (getParam (Notes)) == 0 ? "Una nota" : "Acordes" };
}

std::shared_ptr<const SamplerSound> SamplerBlock::getSound() const
{
    const std::lock_guard<std::mutex> guard (shared->lock);
    return shared->sound;
}

float SamplerBlock::getRootNote() const noexcept
{
    const int choice = (int) std::lround (getParam (RootNote));
    const float tune = getParam (Tune) * 0.01f;
    if (choice > 0)
        return (float) (12 * ((int) std::lround (getParam (RootOctave)) + 2) + choice - 1) - tune;
    const float detected = getDetectedRoot();
    return (detected >= 0.0f ? detected : (float) kDefaultRoot) - tune;
}

std::string SamplerBlock::getRootText() const
{
    const bool automatic = std::lround (getParam (RootNote)) == 0;
    if (automatic && getDetectedRoot() < 0.0f)
        return "sin tono";
    const int note = (int) std::lround (getRootNote());
    return kNoteNames[note % 12] + std::to_string (note / 12 - 1);
}

float SamplerBlock::detectPitch (const SamplerSound& sound, double startSeconds, double endSeconds)
{
    const int first = std::clamp ((int) (startSeconds * sound.rate), 0, sound.length);
    const int last = std::clamp ((int) (std::min (endSeconds, startSeconds + kDetectSeconds) * sound.rate), first, sound.length);
    if (last - first < 2048)
        return kNoPitch;

    RealtimePitchTracker tracker;
    tracker.prepare (sound.rate);

    const bool stereo = ! sound.right[0].empty();
    std::vector<float> frequencies;
    int hops = 0;
    for (int i = first; i < last; ++i)
    {
        const float x = stereo ? 0.5f * (sound.left[0][(size_t) i] + sound.right[0][(size_t) i]) : sound.left[0][(size_t) i];
        tracker.process (&x, 1, [&] (int, const RealtimePitchTracker::Estimate& e)
        {
            ++hops;
            if (e.valid && e.clarity > kDetectClarity)
                frequencies.push_back (e.frequency);
        });
    }

    // Most of the sound has to agree on it: speech, noise and hits have no base note.
    if (hops == 0 || (int) frequencies.size() * 2 < hops)
        return kNoPitch;
    std::nth_element (frequencies.begin(), frequencies.begin() + (std::ptrdiff_t) (frequencies.size() / 2), frequencies.end());
    return 69.0f + 12.0f * std::log2 (frequencies[frequencies.size() / 2] / 440.0f);
}

void SamplerBlock::detectRoot()
{
    const auto current = getSound();
    if (current == nullptr)
        return;
    const double start = getParam (Start) * 0.01 * current->seconds();
    const double end = getParam (End) * 0.01 * current->seconds();
    const int ticket = ++shared->detection;
    loader->pool.addJob ([s = shared, current, start, end, ticket]
    {
        const float root = detectPitch (*current, start, end);
        if (ticket == s->detection.load())
            s->detectedRoot.store (root);
    });
}

void SamplerBlock::trimSilence()
{
    const auto current = getSound();
    if (current == nullptr || current->length == 0)
        return;

    const float threshold = kTargetPeak * dbToGain (kSilenceDb);
    const bool stereo = ! current->right[0].empty();
    const auto loud = [&] (int i)
    {
        return std::abs (current->left[0][(size_t) i]) > threshold
            || (stereo && std::abs (current->right[0][(size_t) i]) > threshold);
    };

    int first = 0, last = current->length - 1;
    while (first < last && ! loud (first)) ++first;
    while (last > first && ! loud (last))  --last;

    const int margin = (int) (0.001 * current->rate);
    setParam (Start, 100.0f * (float) std::max (0, first - margin) / (float) current->length);
    setParam (End, 100.0f * (float) std::min (current->length, last + margin) / (float) current->length);
    detectRoot();
}

void SamplerBlock::startLoad()
{
    std::string path;
    {
        const std::lock_guard<std::mutex> guard (shared->lock);
        path = shared->requestedPath;
    }
    startedPath = path;
    const int generation = ++shared->generation;
    const int ticket = ++shared->detection;
    const float start = getParam (Start) * 0.01f, end = getParam (End) * 0.01f;

    loader->pool.addJob ([s = shared, path, generation, ticket, start, end]
    {
        if (generation != s->generation.load())
            return;

        const juce::File file { juce::String (juce::CharPointer_UTF8 (path.c_str())) };
        juce::String error;
        std::shared_ptr<SamplerSound> decoded;
        if (! file.existsAsFile())
            error = "no encontrado";
        else
            decoded = decode (file, error);

        const float root = decoded != nullptr ? detectPitch (*decoded, start * decoded->seconds(), end * decoded->seconds()) : kNoPitch;

        const std::lock_guard<std::mutex> guard (s->lock);
        if (generation != s->generation.load())
            return;

        if (decoded == nullptr)
        {
            s->status = "Error: " + error.toStdString();
            return;
        }

        if (ticket == s->detection.load())
            s->detectedRoot.store (root);

        s->status = decoded->fileName + "  |  " + secondsText (decoded->seconds()).toStdString()
                  + "  |  " + std::to_string ((int) decoded->rate) + " Hz  |  "
                  + (decoded->right[0].empty() ? "mono" : "estereo")
                  + (decoded->truncated ? "  |  se usan los primeros 60 s" : "");
        s->sound = decoded;

        auto holder = std::make_unique<SoundHolder>();
        holder->sound = std::move (decoded);
        s->slot.publish (std::move (holder));
    });
}

void SamplerBlock::collectGarbage()
{
    shared->slot.collectGarbage();
}

void SamplerBlock::prepareBlock (double sampleRate, int /*maxBlockSize*/)
{
    tracker.prepare (sampleRate);
    detector.prepare (sampleRate);
    fastEnvelope.setTimes (sampleRate, 0.3f, 25.0f);
    slowEnvelope.setTimes (sampleRate, 80.0f, 300.0f);
    mix.prepare (sampleRate, 0.02);
    level.prepare (sampleRate, 0.02);
    chokeCoefficient = EnvelopeFollower::coefficient (sampleRate, kChokeMs / 5.0f);
    glide = 1.0f - EnvelopeFollower::coefficient (sampleRate, 4.0f);
    resetBlock();

    // The sound does not depend on the session rate: only load what is not loaded yet.
    if (const auto path = getFilePath (0); ! path.empty() && path != startedPath)
        startLoad();
}

void SamplerBlock::killVoices() noexcept
{
    for (auto& v : voices)
        v.active = false;
    previewing.store (false, std::memory_order_relaxed);
    playhead.store (-1.0f, std::memory_order_relaxed);
}

void SamplerBlock::resetBlock() noexcept
{
    killVoices();
    tracker.reset();
    detector.reset();
    fastEnvelope.reset();
    slowEnvelope.reset();
    pickPeak = 0.0f;
    hasPitch = pending = noteOn = false;
    sinceOnset = 1 << 20;
    sinceGate = strumWait = 0;
    mix.setCurrentAndTarget (getParam (Mix) * 0.01f);
    level.setCurrentAndTarget (dbToGain (getParam (LevelDb)));
}

double SamplerBlock::speedFor (float midiNote) const noexcept
{
    const float octaves = (float) std::lround (getParam (Transpose)) - 2.0f;
    const float semitones = midiNote + 12.0f * octaves - getRootNote();
    return sound->rate / getSampleRate() * std::pow (2.0, (double) semitones / 12.0);
}

float SamplerBlock::pickGain() const noexcept
{
    const float velocity = std::clamp ((20.0f * std::log10 (std::max (pickPeak, 1.0e-6f)) + 42.0f) / 36.0f, 0.25f, 1.0f);
    const float amount = getParam (Dynamics) * 0.01f;
    return 1.0f - amount + amount * velocity;
}

void SamplerBlock::startVoice (float midiNote, float gain, int note) noexcept
{
    if (sound == nullptr)
        return;

    // A free voice, else the one furthest along its fade, else the oldest.
    Voice* chosen = &voices[0];
    for (auto& v : voices)
    {
        if (! v.active) { chosen = &v; break; }
        if (v.releasing == chosen->releasing ? v.age < chosen->age : v.releasing)
            chosen = &v;
    }

    auto& v = *chosen;
    v = Voice {};
    v.active = v.held = true;
    v.note = note;
    v.gain = gain;
    v.position = regionStart;
    v.speed = v.targetSpeed = speedFor (midiNote);
    // Above the original pitch, read a decimated level: it has nothing left to alias.
    v.level = std::clamp ((int) std::ceil (std::log2 (std::max (v.speed, 1.0e-3) / 1.4)), 0, SamplerSound::kNumLevels - 1);
    v.age = ++voiceCounter;
}

void SamplerBlock::startPreview() noexcept
{
    if (sound == nullptr)
        return;
    for (auto& v : voices)
        if (v.preview)
            v.active = false;
    startVoice (getRootNote() - 12.0f * ((float) std::lround (getParam (Transpose)) - 2.0f), 1.0f, -1);
    for (auto& v : voices)
        if (v.active && v.age == voiceCounter)
            v.preview = true;
    previewing.store (true, std::memory_order_relaxed);
}

void SamplerBlock::releaseHeld() noexcept
{
    for (auto& v : voices)
        if (v.active && v.held && ! v.preview)
        {
            v.held = false;
            if (playback != (int) Play::Once)
                v.releasing = true;
        }
}

void SamplerBlock::chokeAll() noexcept
{
    for (auto& v : voices)
        if (v.active && ! v.preview)
        {
            v.held = false;
            v.note = -2;       // chokes, whatever the release is
            v.releasing = true;
        }
}

float SamplerBlock::readSample (const std::vector<float>& data, double position) const noexcept
{
    const int size = (int) data.size();
    const int i = (int) position;
    const float t = (float) (position - i);
    const auto at = [&] (int k) { return data[(size_t) std::clamp (k, 0, size - 1)]; };
    const float a = at (i - 1), b = at (i), c = at (i + 1), d = at (i + 2);
    return b + 0.5f * t * (c - a + t * (2.0f * a - 5.0f * b + 4.0f * c - d + t * (3.0f * (b - c) + d - a)));
}

void SamplerBlock::renderVoice (Voice& v, float& outLeft, float& outRight) noexcept
{
    if (v.releasing)
    {
        v.envelope *= v.note == -2 ? chokeCoefficient : releaseCoefficient;
        if (v.envelope < 1.0e-4f)
        {
            v.active = false;
            return;
        }
    }
    else if (v.envelope < 1.0f)
    {
        v.envelope = std::min (1.0f, v.envelope + attackStep);
    }

    const bool looping = playback == (int) Play::Loop && ! v.preview && (regionEnd - regionStart) > 2.0;

    if (looping && v.position >= regionEnd)
        v.position -= regionEnd - regionStart - crossfadeSamples;
    else if (! looping && v.position >= regionEnd)
    {
        v.active = false;
        return;
    }

    const double scale = 1.0 / (double) (1 << v.level);
    const auto& l = sound->left[(size_t) v.level];
    const auto& r = sound->right[(size_t) v.level];
    const auto read = [&] (double position, float gain)
    {
        const float a = readSample (l, position * scale);
        outLeft += a * gain;
        outRight += (r.empty() ? a : readSample (r, position * scale)) * gain;
    };

    const float amp = v.gain * v.envelope;
    if (looping)
    {
        const double fadeFrom = regionEnd - crossfadeSamples;
        if (v.position > fadeFrom)
        {
            const float t = (float) ((v.position - fadeFrom) / crossfadeSamples);
            read (v.position, amp * std::sqrt (1.0f - t));
            read (v.position - (regionEnd - regionStart - crossfadeSamples), amp * std::sqrt (t));
        }
        else
        {
            read (v.position, amp);
        }
    }
    else
    {
        read (v.position, amp * (float) std::min (1.0, (regionEnd - v.position) / regionFade));
    }

    v.speed += (v.targetSpeed - v.speed) * (double) glide;
    v.position += v.speed;
}

void SamplerBlock::trackMono (float x) noexcept
{
    const int minWait = (int) (0.010 * getSampleRate()), maxWait = (int) (0.060 * getSampleRate());

    tracker.process (&x, 1, [&] (int, const RealtimePitchTracker::Estimate& e)
    {
        if (! e.valid || e.clarity < kClarity)
            return;
        trackedNote = 69.0f + 12.0f * std::log2 (e.frequency / 440.0f);
        hasPitch = true;

        // Semitone snap, with some hysteresis so vibrato does not flip between notes.
        if (std::abs (trackedNote - heldNote) > 0.6f)
            heldNote = std::round (trackedNote);
        const float note = snapToNote ? heldNote : trackedNote;

        if (pending && sinceOnset >= minWait)
        {
            pending = false;
            chokeAll();
            startVoice (note, pickGain(), (int) std::lround (note));
        }
        else if (! pending && ! noteOn && fastEnvelope.getValue() > gate)
        {
            // A swell or a note with no pick transient to detect.
            noteOn = true;
            chokeAll();
            startVoice (note, pickGain(), (int) std::lround (note));
        }

        for (auto& v : voices)
            if (v.active && v.held && ! v.preview)
                v.targetSpeed = speedFor (note);
    });

    if (pending && sinceOnset > maxWait)
    {
        // No clear pitch in this pick (a muted hit, a noise): the last note, or the sound as it is.
        pending = false;
        chokeAll();
        const float note = hasPitch ? (snapToNote ? heldNote : trackedNote) : getRootNote();
        startVoice (note, pickGain(), (int) std::lround (note));
    }
}

void SamplerBlock::onNotes (const PolyNoteDetector::NoteSet& set) noexcept
{
    const auto contains = [&] (int note)
    {
        for (int i = 0; i < set.count; ++i)
            if ((int) set.notes[(size_t) i] == note)
                return true;
        return false;
    };

    // Notes that stopped ringing.
    for (auto& v : voices)
        if (v.active && v.held && ! v.preview && ! contains (v.note))
        {
            v.held = false;
            if (playback != (int) Play::Once)
                v.releasing = true;
        }

    if (pending)
        return;   // the pick is settling: it fires them all together

    // Notes that started with no pick (hammer-ons, swells).
    for (int i = 0; i < set.count; ++i)
    {
        const int note = (int) set.notes[(size_t) i];
        const bool sounding = std::any_of (voices.begin(), voices.end(),
                                           [&] (const Voice& v) { return v.active && v.held && v.note == note; });
        if (! sounding)
            startVoice ((float) note, pickGain() / std::sqrt ((float) set.count), note);
    }
}

void SamplerBlock::trackPoly (float x) noexcept
{
    detector.process (&x, 1, [&] (int, const PolyNoteDetector::NoteSet& set)
    {
        ++strumWait;
        if (pending && ((! detector.isSettling() && strumWait >= 2) || strumWait > 15) && set.count > 0)
        {
            pending = false;
            chokeAll();
            for (int i = 0; i < set.count; ++i)
                startVoice ((float) set.notes[(size_t) i], pickGain() / std::sqrt ((float) set.count), (int) set.notes[(size_t) i]);
            return;
        }
        if (pending && strumWait > 25)
            pending = false;   // a hit with no notes in it
        onNotes (set);
    });
}

void SamplerBlock::processBlock (float* left, float* right, int numSamples) noexcept
{
    const auto* holder = shared->slot.acquire();
    const auto* latest = holder != nullptr ? holder->sound.get() : nullptr;
    if (latest != sound)
    {
        killVoices();   // they read the old one, which is about to be deleted
        sound = latest;
    }
    if (sound == nullptr)
        return;   // no sound yet: transparent

    const double sr = getSampleRate();
    const double length = (double) sound->length;
    const double minimum = std::min (length, kMinRegionSeconds * sound->rate);
    regionStart = std::clamp ((double) getParam (Start) * 0.01 * length, 0.0, length - minimum);
    regionEnd = std::clamp ((double) getParam (End) * 0.01 * length, regionStart + minimum, length);
    crossfadeSamples = std::clamp ((double) getParam (Crossfade) * 0.001 * sound->rate, 1.0, (regionEnd - regionStart) * 0.5);
    regionFade = std::max (1.0, kRegionFadeSeconds * sound->rate);
    attackStep = 1.0f / std::max (1.0f, getParam (AttackMs) * 0.001f * (float) sr);
    releaseCoefficient = EnvelopeFollower::coefficient (sr, getParam (ReleaseMs) / 5.0f);
    gate = dbToGain (kGateDb);
    playback = (int) std::lround (getParam (Playback));
    snapToNote = std::lround (getParam (Follow)) == 0;

    const int notes = (int) std::lround (getParam (Notes));
    if (notes != currentNotes)
    {
        currentNotes = notes;
        tracker.reset();
        detector.reset();
        pending = noteOn = false;
    }

    if (const int command = previewCommand.exchange (0, std::memory_order_relaxed); command == 1)
    {
        startPreview();
    }
    else if (command == 2)
    {
        for (auto& v : voices)
            if (v.preview)
            {
                v.note = -2;
                v.releasing = true;
            }
    }

    mix.setTarget (getParam (Mix) * 0.01f);
    level.setTarget (dbToGain (getParam (LevelDb)));

    const int refractory = (int) (0.05 * sr);
    const float peakDecay = EnvelopeFollower::coefficient (sr, 300.0f);
    const int gateHold = (int) (0.03 * sr);
    const float detection = getDetectionGain();

    for (int i = 0; i < numSamples; ++i)
    {
        const float x = 0.5f * (left[i] + right[i]) * detection;
        const float fast = fastEnvelope.process (x), slow = slowEnvelope.process (fast);

        pickPeak = sinceOnset < refractory ? std::max (pickPeak, fast) : pickPeak * peakDecay;
        if (++sinceOnset > refractory && fast > gate && fast > 1.5f * std::max (slow, pickPeak) + 1.0e-4f)
        {
            sinceOnset = 0;
            pickPeak = fast;
            pending = noteOn = true;
            strumWait = 0;
        }

        // The note ended.
        sinceGate = fast > gate * 0.7f ? 0 : sinceGate + 1;
        if (noteOn && sinceGate > gateHold && ! pending)
        {
            noteOn = false;
            if (notes == 0)
                releaseHeld();
        }

        if (notes == 0)
            trackMono (x);
        else
            trackPoly (x);

        float wetLeft = 0.0f, wetRight = 0.0f, auditionLeft = 0.0f, auditionRight = 0.0f;
        for (auto& v : voices)
            if (v.active)
            {
                if (v.preview)
                    renderVoice (v, auditionLeft, auditionRight);
                else
                    renderVoice (v, wetLeft, wetRight);
            }

        const float m = mix.next(), g = level.next();
        left[i]  = left[i] * (1.0f - m) + wetLeft * g * m + auditionLeft * g;
        right[i] = right[i] * (1.0f - m) + wetRight * g * m + auditionRight * g;
    }

    const Voice* newest = nullptr;
    bool anyPreview = false;
    for (const auto& v : voices)
        if (v.active)
        {
            anyPreview = anyPreview || v.preview;
            if (newest == nullptr || v.age > newest->age)
                newest = &v;
        }
    previewing.store (anyPreview, std::memory_order_relaxed);
    playhead.store (newest != nullptr ? (float) (newest->position / sound->rate) : -1.0f, std::memory_order_relaxed);
}
} // namespace kumamp
