#pragma once

#include "PolyNoteDetector.h"
#include "../../Core/DspPrimitives.h"
#include "../../Core/LockFree.h"
#include "../../Core/RealtimePitchTracker.h"
#include "../../Core/Smoothing.h"
#include "../../Graph/DSPBlock.h"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace kumamp
{
/** A decoded sound, immutable once published. Peak-normalised to -6 dB. Each
    extra level is the previous one low-passed and decimated by 2, so playing
    far above the original pitch does not alias. */
struct SamplerSound
{
    static constexpr int kNumLevels = 4;
    std::array<std::vector<float>, kNumLevels> left, right;   // right is empty when mono
    double rate = 48000.0;
    int length = 0;   // samples of level 0
    bool truncated = false;
    std::string fileName;

    double seconds() const noexcept { return length / rate; }
};

/**
    Sampler: any sound, loaded from a file, trimmed and played at the pitch of
    the guitar. Like any sampler it plays faster to go higher, so duration and
    character change with the note (no time stretching).

    Una nota: the real-time pitch tracker fires one voice per pick and keeps it
          on the guitar's pitch while it rings (bends and slides, snapped to
          semitones or free). ~10-30 ms from the pick.
    Acordes: the polyphonic note detector fires one voice per note. Adds the
          detector's 40-80 ms.

    Una vez plays the trimmed sound to its end; Sostener stops it when the
    note ends; Loop repeats it (with a crossfade) while the note rings.

    The base note is detected from the trimmed region (Auto) or set by hand. A
    sound with no clear pitch plays as if it were C4. The trim and the base
    note describe the sound, so they do not change with the scene.

    Loading runs on the shared loader thread; all voice memory is fixed, and
    processBlock() is RT-safe.
*/
class SamplerBlock final : public DSPBlock
{
public:
    enum Param { Playback, Notes, Follow, Transpose, AttackMs, ReleaseMs, Dynamics, Crossfade, Mix, LevelDb,
                 Start, End, RootNote, RootOctave, Tune, NumParams };
    enum class Play { Once, Hold, Loop };
    static constexpr int kMaxVoices = 8;
    static constexpr float kNoPitch = -1.0f;
    static constexpr int kDefaultRoot = 60;   // C4: what a sound with no clear pitch plays as

    explicit SamplerBlock (StoreId id);
    ~SamplerBlock() override;

    int getNumFileSlots() const override { return 1; }
    std::string getFileSlotName (int) const override { return "Sonido"; }
    std::string getFileSlotPattern (int) const override { return "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.m4a"; }
    bool loadFile (int slot, const std::string& path) override;
    std::string getFilePath (int slot) const override;
    std::string getStatusText() const override;
    std::vector<std::string> getInfoChips() const override;
    bool isParamRelevant (int param) const noexcept override;
    bool isSceneParam (int param) const noexcept override { return param < Start; }
    void collectGarbage() override;

    //== Editor (message thread) ==================================================
    std::shared_ptr<const SamplerSound> getSound() const;
    /** Detects the base note of the current trim again (after editing it). */
    void detectRoot();
    /** Moves the trim to where the sound starts and ends, past any silence. */
    void trimSilence();
    /** The MIDI note the sound plays at unchanged (fractional when detected). Any thread. */
    float getRootNote() const noexcept;
    /** Detected base note, or kNoPitch. Any thread. */
    float getDetectedRoot() const noexcept { return shared->detectedRoot.load (std::memory_order_relaxed); }
    std::string getRootText() const;

    void setPreviewing (bool shouldPlay) noexcept { previewCommand.store (shouldPlay ? 1 : 2, std::memory_order_relaxed); }
    bool isPreviewing() const noexcept { return previewing.load (std::memory_order_relaxed); }
    /** Where the newest voice is reading, in seconds; negative when silent. Any thread. */
    float getPlayheadSeconds() const noexcept { return playhead.load (std::memory_order_relaxed); }

    /** Offline: the pitch of a sound (MIDI note), or kNoPitch. */
    static float detectPitch (const SamplerSound& sound, double startSeconds, double endSeconds);

protected:
    void prepareBlock (double sampleRate, int maxBlockSize) override;
    void resetBlock() noexcept override;
    void processBlock (float* left, float* right, int numSamples) noexcept override;

private:
    struct SoundHolder { std::shared_ptr<const SamplerSound> sound; };
    struct Shared
    {
        RTObjectPublisher<SoundHolder> slot;
        std::atomic<int> generation { 0 }, detection { 0 };
        std::atomic<float> detectedRoot { kNoPitch };
        std::mutex lock;   // loader <-> message thread only, never the audio thread
        std::string requestedPath, status;
        std::shared_ptr<const SamplerSound> sound;
    };
    struct Voice
    {
        bool active = false, held = false, preview = false, releasing = false;
        int note = -1, level = 0;
        double position = 0.0, speed = 1.0, targetSpeed = 1.0;
        float gain = 1.0f, envelope = 0.0f;
        std::uint32_t age = 0;
    };

    void startLoad();
    void startVoice (float midiNote, float gain, int note) noexcept;
    void startPreview() noexcept;
    void renderVoice (Voice& voice, float& outLeft, float& outRight) noexcept;
    float readSample (const std::vector<float>& data, double position) const noexcept;
    double speedFor (float midiNote) const noexcept;
    void trackMono (float x) noexcept;
    void trackPoly (float x) noexcept;
    void onNotes (const PolyNoteDetector::NoteSet& set) noexcept;
    float pickGain() const noexcept;
    void releaseHeld() noexcept;
    void chokeAll() noexcept;
    void killVoices() noexcept;

    std::shared_ptr<Shared> shared;
    juce::SharedResourcePointer<FileLoaderPool> loader;
    std::string startedPath;

    const SamplerSound* sound = nullptr;   // audio thread: the current one

    std::array<Voice, kMaxVoices> voices;
    std::uint32_t voiceCounter = 0;
    RealtimePitchTracker tracker;
    PolyNoteDetector detector;
    EnvelopeFollower fastEnvelope, slowEnvelope;
    float pickPeak = 0.0f, trackedNote = 0.0f, heldNote = 0.0f;
    bool hasPitch = false, pending = false, noteOn = false;
    int sinceOnset = 1 << 20, sinceGate = 0, strumWait = 0;
    int currentNotes = -1;

    // Per-block snapshots of the parameters.
    double regionStart = 0.0, regionEnd = 0.0, crossfadeSamples = 0.0, regionFade = 1.0;
    float attackStep = 1.0f, releaseCoefficient = 0.0f, chokeCoefficient = 0.0f, glide = 1.0f, gate = 0.0f;
    int playback = 1;
    bool snapToNote = true;

    LinearSmoother mix, level;

    std::atomic<int> previewCommand { 0 };
    std::atomic<bool> previewing { false };
    std::atomic<float> playhead { -1.0f };
};
} // namespace kumamp
