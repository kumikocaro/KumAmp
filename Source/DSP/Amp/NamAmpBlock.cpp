#include "NamAmpBlock.h"
#include "../../Core/DspMath.h"

#include "NAM/dsp.h"
#include "NAM/get_dsp.h"

#include <filesystem>
#include <iterator>
#include <utility>

namespace kumamp
{
namespace
{
    constexpr ParamInfo kParams[] = {
        { "input",     "Gain",        -24.0f, 24.0f, 0.0f, "dB", false },
        { "bass",      "Bass",          0.0f, 10.0f, 5.0f, "",   false },
        { "mid",       "Mid",           0.0f, 10.0f, 5.0f, "",   false },
        { "treble",    "Treble",        0.0f, 10.0f, 5.0f, "",   false },
        { "presence",  "Presence",      0.0f, 10.0f, 5.0f, "",   false },
        { "output",    "Master",      -40.0f, 12.0f, 0.0f, "dB", false },
        { "normalize", "Normalizar",    0.0f,  1.0f, 1.0f, "Off|On", true },
        { "profile",   "Perfil",        0.0f,  2.0f, 0.0f, "Auto|Amp-Only|End-to-End", true },
    };
    static_assert (std::size (kParams) == NamAmpBlock::NumParams);

    constexpr float kTargetLoudnessDb = -18.0f;

    /** UTF-8 path -> std::filesystem::path. On Windows a narrow string would be
        read in the ANSI code page and break paths with accents (C:/Users/Música/...). */
    std::filesystem::path toFsPath (const std::string& utf8)
    {
       #if JUCE_WINDOWS
        return std::filesystem::path (juce::String::fromUTF8 (utf8.c_str()).toWideCharPointer());
       #else
        return std::filesystem::path (utf8);
       #endif
    }

    std::string fileNameOf (const std::string& utf8)
    {
        return juce::File (juce::String::fromUTF8 (utf8.c_str())).getFileName().toStdString();
    }
} // namespace

//==============================================================================
NamModel::NamModel() = default;
NamModel::~NamModel() = default;

void NamModel::prepare (double hostRate, int maxHostBlock)
{
    const bool anyRate = dsp->SupportsArbitrarySampleRate() || trainedRate <= 0.0;
    runRate = anyRate ? hostRate : trainedRate;

    bridge.prepare (hostRate, runRate, maxHostBlock);
    modelOutput.assign ((size_t) bridge.getMaxModelBlock(), 0.0f);
    dsp->Reset (runRate, bridge.getMaxModelBlock());   // allocates + prewarms

    normalisationGain = dsp->HasLoudness() ? dbToGain (kTargetLoudnessDb - (float) dsp->GetLoudness()) : 1.0f;
}

void NamModel::process (const float* input, float* output, int numSamples) noexcept
{
    bridge.process (input, output, numSamples, [this] (float* data, int n)
    {
        float* in = data;
        float* out = modelOutput.data();
        dsp->process (&in, &out, n);
        std::memcpy (data, out, sizeof (float) * (size_t) n);
    });
}

//==============================================================================
NamAmpBlock::NamAmpBlock (StoreId id)
    : DSPBlock (id, BlockType::NamAmp, kParams, NumParams),
      shared (std::make_shared<Shared>())
{
    shared->status = "Sin modelo";
}

NamAmpBlock::~NamAmpBlock()
{
    ++shared->generation;   // in-flight loads become no-ops
}

bool NamAmpBlock::loadFile (int slot, const std::string& path)
{
    if (slot != 0 || path.empty())
        return false;

    {
        const std::lock_guard<std::mutex> guard (shared->lock);
        shared->requestedPath = path;
        shared->status = "Cargando " + fileNameOf (path) + "...";
    }

    if (isPrepared())
        startLoad();
    return true;
}

std::string NamAmpBlock::getFilePath (int slot) const
{
    if (slot != 0)
        return {};
    const std::lock_guard<std::mutex> guard (shared->lock);
    return shared->requestedPath;
}

std::string NamAmpBlock::getStatusText() const
{
    const std::lock_guard<std::mutex> guard (shared->lock);
    return shared->status;
}

std::vector<std::string> NamAmpBlock::getInfoChips() const
{
    std::vector<std::string> chips { profileKindName (getEffectiveProfileKind()) };
    const std::lock_guard<std::mutex> guard (shared->lock);
    if (! shared->version.empty())      chips.push_back ("NAM " + shared->version);
    if (! shared->architecture.empty()) chips.push_back (shared->architecture == "SlimmableContainer" ? "A2" : shared->architecture);
    if (shared->resampled)              chips.push_back ("Resample");
    return chips;
}

void NamAmpBlock::updateToneStack (bool force) noexcept
{
    // 5 = flat. Bass/Mid/Treble +-12 dB, Presence +-10 dB (post-model, like an amp's tone stack).
    const float values[4] = { getParam (Bass), getParam (Mid), getParam (Treble), getParam (Presence) };
    const double sr = getSampleRate();
    for (int k = 0; k < 4; ++k)
    {
        if (! force && exactlyEqual (values[k], toneValues[(size_t) k]))
            continue;
        toneValues[(size_t) k] = values[k];
        const float db = (values[k] - 5.0f) * (k == 3 ? 2.0f : 2.4f);
        switch (k)
        {
            case 0: toneStack[0].setLowShelf (sr, 120.0f, db); break;
            case 1: toneStack[1].setPeak (sr, 700.0f, 0.8f, db); break;
            case 2: toneStack[2].setHighShelf (sr, 2500.0f, db); break;
            default: toneStack[3].setHighShelf (sr, 6000.0f, db); break;
        }
    }
}

void NamAmpBlock::collectGarbage()
{
    shared->slot.collectGarbage();
}

ProfileKind NamAmpBlock::getEffectiveProfileKind() const noexcept
{
    switch ((int) std::lround (getParam (ProfileMode)))
    {
        case ModeAmpOnly:  return ProfileKind::AmpOnly;
        case ModeEndToEnd: return ProfileKind::EndToEnd;
        default: break;
    }

    if (! hasModel())
        return ProfileKind::AmpOnly;

    const auto detected = (ProfileKind) shared->detectedKind.load (std::memory_order_relaxed);
    return detected == ProfileKind::EndToEnd ? ProfileKind::EndToEnd : ProfileKind::AmpOnly;
}

void NamAmpBlock::startLoad()
{
    const double sampleRate = getSampleRate();
    const int maxBlockSize = getMaxBlockSize();
    const int generation = ++shared->generation;

    std::string path;
    {
        const std::lock_guard<std::mutex> guard (shared->lock);
        path = shared->requestedPath;
    }

    loader->pool.addJob ([s = shared, generation, sampleRate, maxBlockSize, path]
    {
        auto setStatus = [&] (std::string text)
        {
            const std::lock_guard<std::mutex> guard (s->lock);
            if (generation == s->generation.load())
                s->status = std::move (text);
        };

        auto model = std::make_unique<NamModel>();
        model->path = path;

        try
        {
            nam::dspData data;
            model->dsp = nam::get_dsp (toFsPath (path), data);
            if (model->dsp == nullptr)
                throw std::runtime_error ("modelo vacio");
            if (model->dsp->NumInputChannels() != 1 || model->dsp->NumOutputChannels() != 1)
                throw std::runtime_error ("solo se admiten modelos mono (1 entrada / 1 salida)");

            model->info = readNamProfileInfo (data.metadata);
            model->version = data.version;
            model->architecture = data.architecture;
            model->trainedRate = model->dsp->GetExpectedSampleRate();
            model->prepare (sampleRate, maxBlockSize);
        }
        catch (const std::exception& e)
        {
            setStatus (std::string ("Error: ") + e.what());
            return;
        }

        std::string status = fileNameOf (path)
                           + "  |  " + profileKindName (model->info.kind);
        if (! model->info.gearType.empty())
            status += " (" + model->info.gearType + ")";
        if (std::abs (model->runRate - sampleRate) > 0.5)
            status += "  |  resample " + std::to_string ((int) model->runRate) + " Hz";

        const auto kind = model->info.kind;

        // Publish under the lock so prepareBlock() can't interleave with a stale result.
        const std::lock_guard<std::mutex> guard (s->lock);
        if (generation != s->generation.load())
            return;

        s->status = std::move (status);
        s->version = model->version;
        s->architecture = model->architecture;
        s->resampled = std::abs (model->runRate - sampleRate) > 0.5;
        s->detectedKind.store ((int) kind);
        s->slot.publish (std::move (model));
    });
}

//==============================================================================
void NamAmpBlock::prepareBlock (double sampleRate, int maxBlockSize)
{
    for (auto* v : { &mono, &wetCurrent, &wetPrevious, &dryLeft, &dryRight })
        v->assign ((size_t) maxBlockSize, 0.0f);

    inputGain.prepare (sampleRate, 0.02);
    outputGain.prepare (sampleRate, 0.02);
    fadeLength = std::max (1, (int) (sampleRate * 0.050));
    fading = false;

    std::string requested;
    {
        // The audio thread is stopped: we may touch the slot's audio-side state.
        const std::lock_guard<std::mutex> guard (shared->lock);
        ++shared->generation;
        shared->slot.dropPendingAndPreviousWhileStopped();
        requested = shared->requestedPath;
    }

    NamModel* current = shared->slot.getCurrent();
    if (current != nullptr)
        current->prepare (sampleRate, maxBlockSize);

    if (! requested.empty() && (current == nullptr || current->path != requested))
        startLoad();

    resetBlock();
}

void NamAmpBlock::resetBlock() noexcept
{
    for (auto& f : toneStack) f.reset();
    updateToneStack (true);
    inputGain.setCurrentAndTarget (dbToGain (getParam (InputDb)));
    outputGain.setCurrentAndTarget (dbToGain (getParam (OutputDb)));
}

void NamAmpBlock::processBlock (float* left, float* right, int numSamples) noexcept
{
    auto& slot = shared->slot;

    if (slot.beginSwap())
    {
        fading = true;
        fadePos = 0;
        modelLoaded.store (true, std::memory_order_relaxed);
    }

    NamModel* current = slot.getCurrent();
    if (current == nullptr)
        return;   // no model yet: transparent

    inputGain.setTarget (dbToGain (getParam (InputDb)));
    outputGain.setTarget (dbToGain (getParam (OutputDb)));
    const bool normalise = getParam (Normalize) > 0.5f;
    updateToneStack (false);

    const auto bytes = sizeof (float) * (size_t) numSamples;
    std::memcpy (dryLeft.data(), left, bytes);
    std::memcpy (dryRight.data(), right, bytes);

    for (int i = 0; i < numSamples; ++i)
        mono[(size_t) i] = 0.5f * (left[i] + right[i]) * inputGain.next();

    current->process (mono.data(), wetCurrent.data(), numSamples);
    for (int i = 0; i < numSamples; ++i)
    {
        float v = wetCurrent[(size_t) i];
        for (auto& f : toneStack) v = f.process (v);
        wetCurrent[(size_t) i] = v;
    }
    const float gainCurrent = normalise ? current->normalisationGain : 1.0f;

    NamModel* previous = slot.getPrevious();
    if (previous != nullptr)
        previous->process (mono.data(), wetPrevious.data(), numSamples);
    const float gainPrevious = previous != nullptr && normalise ? previous->normalisationGain : 1.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const float out = outputGain.next();
        const float y = wetCurrent[(size_t) i] * gainCurrent * out;

        if (! fading)
        {
            left[i] = right[i] = y;
            continue;
        }

        // Crossfade from the previous model, or from the dry signal on first load.
        const float t = (float) fadePos / (float) fadeLength;
        if (previous != nullptr)
        {
            const float yp = wetPrevious[(size_t) i] * gainPrevious * out;
            left[i] = right[i] = yp + (y - yp) * t;
        }
        else
        {
            left[i]  = dryLeft[(size_t) i]  + (y - dryLeft[(size_t) i])  * t;
            right[i] = dryRight[(size_t) i] + (y - dryRight[(size_t) i]) * t;
        }

        if (++fadePos >= fadeLength)
            fading = false;
    }

    if (! fading && previous != nullptr)
        slot.endCrossfade();
}
} // namespace kumamp
