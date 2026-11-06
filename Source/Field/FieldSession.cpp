#include "FieldSession.h"

#include "../DSP/Pitch/SitarBlock.h"
#include "../Graph/DSPBlock.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace kumamp
{
namespace
{
    constexpr float kMorphSpan = 0.75f;      // of a parameter's range, for a full field of travel
    constexpr float kOscillationSpan = 0.28f;
    constexpr float kMaxSend = 0.6f;
    constexpr int kFormatVersion = 1;

    /** Same inputs, same genome: a tone sounds the same when it comes back. */
    float genome (juce::uint32 seed, StoreId store, int param, int gene) noexcept
    {
        juce::uint32 h = seed * 2654435761u ^ (store * 40503u) ^ ((juce::uint32) param * 2147483647u) ^ ((juce::uint32) gene * 16777619u);
        h ^= h >> 15; h *= 2246822519u;
        h ^= h >> 13; h *= 3266489917u;
        h ^= h >> 16;
        return (float) (h & 0xffffff) / 16777215.0f;
    }

    /** Levels and the block's own setup stay out of the morph: exploring must not jump in volume. */
    bool isMorphable (const ParamStore& store, int param)
    {
        const auto& info = store.getParamInfo (param);
        if (info.isDiscrete || ! store.isSceneParam (param) || info.maxValue <= info.minValue)
            return false;
        const juce::String id (info.id);
        return ! (id.startsWith ("level") || id.startsWith ("pan") || id == "makeup" || id == "output"
                  || id == "input" || id == "volume" || id == "gain");
    }

    /** The sitar's block is shared by two forms: each one owns its side of the parameters. */
    bool ownsParam (const FieldSession::Blob& blob, const ParamStore& store, int param)
    {
        return juce::String (store.getParamInfo (param).id).startsWith ("tanpura") == blob.tanpura;
    }

    int blobKey (StoreId store, bool tanpura) noexcept { return (int) store * 2 + (tanpura ? 1 : 0); }

    juce::String basesText (const std::vector<float>& bases)
    {
        juce::StringArray values;
        for (const auto v : bases)
            values.add (juce::String (v, 5));
        return values.joinIntoString (" ");
    }

    std::vector<float> basesFrom (const juce::String& text)
    {
        std::vector<float> bases;
        for (const auto& token : juce::StringArray::fromTokens (text, " ", {}))
            bases.push_back (token.getFloatValue());
        return bases;
    }
} // namespace

FieldSession::FieldSession (GridEngine& gridEngine)
    : grid (gridEngine), toneFolder (getDefaultToneFolder())
{
    engine.setFusionBus (&bus);
}

FieldSession::~FieldSession() = default;

//==============================================================================
void FieldSession::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;
    attackCoef = 1.0f - std::exp (-1.0f / (0.004f * (float) sampleRate));
    releaseCoef = 1.0f - std::exp (-1.0f / (0.22f * (float) sampleRate));
    bus.prepare (sampleRate, maxBlockSize);
    engine.prepare (sampleRate, maxBlockSize);
}

void FieldSession::resetAudio() noexcept
{
    engine.pushCommand ({ EngineCommand::Type::ResetAllBlocks });
}

void FieldSession::process (float* const* channels, int numChannels, int numSamples, double bpm, float detectionGain) noexcept
{
    // The guitar's level: it pushes the oscillations and makes the forms breathe.
    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        peak = std::max (peak, std::abs (channels[0][i]));
    const float coef = peak > follower ? attackCoef : releaseCoef;
    follower += (1.0f - std::pow (1.0f - coef, (float) numSamples)) * (peak - follower);
    const float level = std::min (1.0f, follower * 3.0f);
    envelope.store (level, std::memory_order_relaxed);

    // Move the parameters: atomics, read by the blocks once per audio block.
    if (const auto* patch = patches.acquire())
    {
        const float tideNow = tide.load (std::memory_order_relaxed);
        const double speed = 0.15 + 3.85 * (double) (tideNow * tideNow);
        const double elapsed = (double) numSamples / sampleRate;
        const int count = std::min ((int) patch->blobs.size(), kMaxBlobs);
        for (int b = 0; b < count; ++b)
        {
            const auto& blob = patch->blobs[(size_t) b];
            const auto& state = live[(size_t) b];
            const float stretch = state.stretch.load (std::memory_order_relaxed);
            const float dx = state.x.load (std::memory_order_relaxed) - blob.homeX;
            const float dy = state.y.load (std::memory_order_relaxed) - blob.homeY;
            phases[(size_t) b] += elapsed * speed * (double) state.rate.load (std::memory_order_relaxed);
            phases[(size_t) b] -= std::floor (phases[(size_t) b]);
            const float depth = stretch * (0.55f + 0.45f * level) * kOscillationSpan;
            const float phase = (float) phases[(size_t) b];

            for (const auto& e : blob.entries)
            {
                float offset = kMorphSpan * (e.weightX * dx + e.weightY * dy);
                if (depth > 0.0f)
                    offset += depth * e.weightMod * std::sin (6.2831853f * (phase + e.phase));
                blob.store->setParam (e.param, juce::jlimit (e.min, e.min + e.range, e.base + e.range * offset));
            }
        }
    }

    engine.setTempo (bpm);
    engine.setDetectionGain (detectionGain);
    engine.process (channels, std::min (numChannels, 2), channels, numChannels, numSamples);

    // Limiter: fused blocks can add up to a lot.
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = channels[ch];
        for (int i = 0; i < numSamples; ++i)
        {
            const float x = data[i], a = std::abs (x);
            if (a > 0.8f)
                data[i] = std::copysign (0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f), x);
        }
    }
}

//==============================================================================
juce::String FieldSession::chainSignature() const
{
    std::vector<const GridNode*> nodes;
    for (const auto& node : grid.getModel().getNodes())
        if (node.kind == NodeKind::Block)
            nodes.push_back (&node);
    std::stable_sort (nodes.begin(), nodes.end(), [] (const GridNode* a, const GridNode* b)
    {
        return a->column != b->column ? a->column < b->column : a->row < b->row;
    });
    juce::String text;
    for (const auto* node : nodes)
        if (const auto store = grid.getStore (node->store))
            text << (int) node->store << ":" << getBlockTypeInfo (store->getType()).id << " ";
    return text;
}

void FieldSession::syncWithGrid()
{
    if (pendingState.isValid() || blobs.empty() || chainSignature() != signature)
        rebuild (true);
    refreshTones();
}

void FieldSession::resetFromGrid()
{
    rebuild (false);
}

void FieldSession::rebuild (bool keepValues)
{
    // What was here: from this session, or from the project just opened.
    std::map<int, Blob> previous;
    if (keepValues)
    {
        if (pendingState.isValid())
        {
            seed = (juce::uint32) (juce::int64) pendingState.getProperty ("seed", (juce::int64) seed);
            fusion = pendingState.getProperty ("fusion", fusion);
            tide.store (pendingState.getProperty ("tide", tide.load()));
            for (const auto& b : pendingState)
            {
                Blob blob;
                blob.store = (StoreId) (int) b.getProperty ("store");
                blob.type = blockTypeFromId (b.getProperty ("type").toString().toStdString());
                blob.x = b.getProperty ("x", 0.5f);
                blob.y = b.getProperty ("y", 0.5f);
                blob.homeX = b.getProperty ("homeX", blob.x);
                blob.homeY = b.getProperty ("homeY", blob.y);
                blob.stretch = b.getProperty ("stretch", 0.0f);
                blob.angle = b.getProperty ("angle", 0.0f);
                blob.on = b.getProperty ("on", true);
                blob.tanpura = b.getProperty ("tanpura", false);
                blob.bases = basesFrom (b.getProperty ("bases").toString());
                previous[blobKey (blob.store, blob.tanpura)] = std::move (blob);
            }
            pendingState = {};
        }
        else
        {
            for (const auto& blob : blobs)
                previous[blobKey (blob.store, blob.tanpura)] = blob;
        }
    }

    // Same elements as the grid; its scenes and macros are not part of this.
    auto state = grid.saveState();
    state.removeChild (state.getChildWithName ("Scenes"), nullptr);
    state.removeChild (state.getChildWithName ("Macros"), nullptr);
    bus.clearGains();
    engine.loadState (state);
    signature = chainSignature();

    std::vector<const GridNode*> nodes;
    for (const auto& node : engine.getModel().getNodes())
        if (node.kind == NodeKind::Block)
            nodes.push_back (&node);
    std::stable_sort (nodes.begin(), nodes.end(), [] (const GridNode* a, const GridNode* b)
    {
        return a->column != b->column ? a->column < b->column : a->row < b->row;
    });

    blobs.clear();
    for (const auto* node : nodes)
    {
        const auto store = engine.getStore (node->store);
        if (store == nullptr || (int) blobs.size() >= kMaxBlobs)
            continue;

        Blob blob;
        blob.homeX = -1.0f;   // no place yet
        blob.store = store->getId();
        blob.type = store->getType();
        blob.name = juce::String (store->getName());
        blob.on = ! store->isBypassed();
        for (int p = 0; p < store->getNumParams(); ++p)
            blob.bases.push_back (store->getParam (p));

        const auto restore = [&previous, &store] (Blob& target)
        {
            if (const auto it = previous.find (blobKey (target.store, target.tanpura)); it != previous.end() && it->second.type == target.type)
            {
                auto bases = it->second.bases;
                auto name = target.name;
                if ((int) bases.size() != store->getNumParams())
                    bases = target.bases;
                target = it->second;
                target.name = name;
                target.bases = std::move (bases);
            }
        };

        // The sitar's drone gets a form of its own, right after it.
        const bool isSitar = blob.type == BlockType::Sitar && (int) blobs.size() + 1 < kMaxBlobs;
        Blob drone = blob;
        restore (blob);
        blobs.push_back (std::move (blob));
        if (isSitar)
        {
            drone.tanpura = true;
            drone.name = "Tampura";
            drone.on = store->getParam (SitarBlock::Tanpura) > 0.5f;
            restore (drone);
            blobs.push_back (std::move (drone));
        }
    }

    layoutHomes();
    writeBasesToStores();
    for (int b = 0; b < (int) blobs.size(); ++b)
        publishLive (b);
    publishPatch();
    updateFusion();
}

void FieldSession::layoutHomes()
{
    // Rows across the field, snaking in chain order and spaced so that nothing overlaps (and
    // fuses) on arrival. Blocks already here keep their place: it is part of how they sound.
    const int n = (int) blobs.size();
    const int columns = columnsFor (n);
    const int rows = (n + columns - 1) / std::max (1, columns);
    for (int i = 0; i < n; ++i)
    {
        auto& blob = blobs[(size_t) i];
        if (blob.homeX >= 0.0f)
            continue;
        const int row = i / columns;
        const int column = row % 2 == 0 ? i % columns : columns - 1 - i % columns;
        const float shift = row % 2 == 0 ? 0.0f : 0.25f / (float) columns;
        blob.x = blob.homeX = 0.05f + 0.90f * (((float) column + 0.5f) / (float) columns + shift * 0.5f - 0.06f / (float) columns);
        blob.y = blob.homeY = 0.20f + 0.70f * ((float) row + 0.5f) / (float) rows;
    }
}

int FieldSession::columnsFor (int n) const
{
    return std::max (1, (int) std::ceil (std::sqrt ((float) std::max (1, n) * aspect * 0.6f)));
}

std::vector<FieldSession::PatchEntry> FieldSession::entriesFor (const Blob& blob) const
{
    std::vector<PatchEntry> entries;
    const auto store = engine.getStore (blob.store);
    if (store == nullptr)
        return entries;

    for (int p = 0; p < store->getNumParams() && (int) entries.size() < kMaxMorphedParams; ++p)
    {
        if (! isMorphable (*store, p) || ! ownsParam (blob, *store, p))
            continue;
        const auto& info = store->getParamInfo (p);
        PatchEntry e {};
        e.param = p;
        e.base = p < (int) blob.bases.size() ? blob.bases[(size_t) p] : info.defaultValue;
        e.min = info.minValue;
        e.range = info.maxValue - info.minValue;
        e.weightX = 2.0f * genome (seed, blob.store, p, 0) - 1.0f;
        e.weightY = 2.0f * genome (seed, blob.store, p, 1) - 1.0f;
        e.weightMod = 0.3f + 0.7f * genome (seed, blob.store, p, 2);
        e.phase = genome (seed, blob.store, p, 3);
        entries.push_back (e);
    }
    return entries;
}

void FieldSession::publishPatch()
{
    auto patch = std::make_unique<Patch>();
    for (const auto& blob : blobs)
        if (auto store = engine.getStore (blob.store))
        {
            PatchBlob entry;
            entry.store = std::move (store);
            entry.homeX = blob.homeX;
            entry.homeY = blob.homeY;
            entry.entries = entriesFor (blob);
            patch->blobs.push_back (std::move (entry));
        }
    patches.publish (std::move (patch));
}

void FieldSession::publishLive (int b)
{
    const auto& blob = blobs[(size_t) b];
    auto& state = live[(size_t) b];
    state.x.store (blob.x, std::memory_order_relaxed);
    state.y.store (blob.y, std::memory_order_relaxed);
    state.stretch.store (blob.stretch, std::memory_order_relaxed);
    state.rate.store (rateHz (blob.angle), std::memory_order_relaxed);
}

void FieldSession::writeBasesToStores()
{
    for (const auto& blob : blobs)
        if (const auto store = engine.getStore (blob.store))
        {
            if (! blob.tanpura)
                store->setBypassed (! blob.on);
            for (int p = 0; p < (int) blob.bases.size() && p < store->getNumParams(); ++p)
                if (ownsParam (blob, *store, p))
                    store->setParam (p, blob.bases[(size_t) p]);
            if (blob.tanpura)
            {
                store->setParam (SitarBlock::Tanpura, blob.on ? 1.0f : 0.0f);
                store->setParam (SitarBlock::TanpuraOutput, 0.0f);   // straight out: that is what the fusions can tap
            }
        }
}

void FieldSession::updateFusion()
{
    // The drone only sends: a form's own block does not get it back, and it needs its sitar on.
    const auto isOn = [this] (const Blob& blob)
    {
        if (! blob.on)
            return false;
        if (blob.tanpura)
            for (const auto& other : blobs)
                if (other.store == blob.store && ! other.tanpura)
                    return other.on;
        return true;
    };
    const int n = (int) blobs.size();
    for (int a = 0; a < n; ++a)
        for (int b = 0; b < n; ++b)
        {
            const auto& from = blobs[(size_t) a];
            const auto& to = blobs[(size_t) b];
            if (a == b || to.tanpura || from.store == to.store)
                continue;
            const float amount = isOn (from) && to.on ? getOverlap (a, b) * fusion : 0.0f;
            bus.setGain (from.tanpura ? FusionBus::kDirectSlot : from.store, to.store, kMaxSend * amount);
        }
}

bool FieldSession::isChainReady() const
{
    for (const auto& blob : blobs)
        if (auto* block = dynamic_cast<DSPBlock*> (engine.getStore (blob.store).get()))
            if (juce::String (block->getStatusText()).startsWith ("Cargando"))
                return false;
    return true;
}

void FieldSession::runMessageThreadTasks()
{
    engine.runMessageThreadTasks();
    patches.collectGarbage();
}

//==============================================================================
float FieldSession::rateHz (float angle) noexcept
{
    // Around the circle from under one cycle every 10 s to 5 per second.
    const float turn = std::fmod (angle / 6.2831853f + 1.0f, 1.0f);
    return 0.1f * std::pow (50.0f, turn);
}

void FieldSession::setBlobPosition (int b, float x, float y)
{
    auto& blob = blobs[(size_t) b];
    blob.x = juce::jlimit (0.0f, 1.0f, x);
    blob.y = juce::jlimit (0.0f, 1.0f, y);
    publishLive (b);
    updateFusion();
}

void FieldSession::setBlobStretch (int b, float stretch, float angle)
{
    auto& blob = blobs[(size_t) b];
    blob.stretch = juce::jlimit (0.0f, 1.0f, stretch);
    blob.angle = angle;
    publishLive (b);
    updateFusion();
}

void FieldSession::toggleBlob (int b)
{
    auto& blob = blobs[(size_t) b];
    blob.on = ! blob.on;
    if (const auto store = engine.getStore (blob.store))
    {
        if (blob.tanpura)
        {
            blob.bases[(size_t) SitarBlock::Tanpura] = blob.on ? 1.0f : 0.0f;
            store->setParam (SitarBlock::Tanpura, blob.bases[(size_t) SitarBlock::Tanpura]);

            // The drone comes out of the sitar's block: that has to be running.
            if (blob.on)
                for (int i = 0; i < (int) blobs.size(); ++i)
                    if (blobs[(size_t) i].store == blob.store && ! blobs[(size_t) i].tanpura && ! blobs[(size_t) i].on)
                        toggleBlob (i);
        }
        else
        {
            store->setBypassed (! blob.on);
        }
    }
    updateFusion();
}

float FieldSession::getBlobRadius (int b) const
{
    const int n = (int) blobs.size();
    const int columns = columnsFor (n);
    const int rows = (n + columns - 1) / std::max (1, columns);
    const float cell = std::min (0.90f * aspect / (float) columns, 0.70f / (float) std::max (1, rows));
    const float size = blobs[(size_t) b].tanpura ? 1.3f : 1.0f;   // it is the show
    return std::min (0.17f, 0.42f * cell) * size * (1.0f + 0.35f * blobs[(size_t) b].stretch);
}

float FieldSession::getOverlap (int a, int b) const
{
    const auto& p = blobs[(size_t) a];
    const auto& q = blobs[(size_t) b];
    const float reach = getBlobRadius (a) + getBlobRadius (b);
    const float distance = std::hypot ((p.x - q.x) * aspect, p.y - q.y);
    return juce::jlimit (0.0f, 1.0f, 1.0f - distance / reach);
}

void FieldSession::setAspect (float widthOverHeight)
{
    if (widthOverHeight > 0.1f && ! juce::approximatelyEqual (aspect, widthOverHeight))
    {
        aspect = widthOverHeight;
        updateFusion();
    }
}

void FieldSession::setTide (float value)
{
    tide.store (juce::jlimit (0.0f, 1.0f, value), std::memory_order_relaxed);
}

void FieldSession::setFusion (float value)
{
    fusion = juce::jlimit (0.0f, 1.0f, value);
    updateFusion();
}

void FieldSession::mutate()
{
    auto& random = juce::Random::getSystemRandom();
    seed = (juce::uint32) random.nextInt();
    for (int b = 0; b < (int) blobs.size(); ++b)
    {
        auto& blob = blobs[(size_t) b];
        blob.x = juce::jlimit (0.05f, 0.95f, blob.x + 0.16f * (random.nextFloat() - 0.5f));
        blob.y = juce::jlimit (0.12f, 0.88f, blob.y + 0.30f * (random.nextFloat() - 0.5f));
        blob.stretch = juce::jlimit (0.0f, 1.0f, random.nextFloat() * 0.7f);
        blob.angle = random.nextFloat() * 6.2831853f;
        publishLive (b);
    }
    publishPatch();
    updateFusion();
}

float FieldSession::getMorphedValue (int b, int param) const
{
    const auto& blob = blobs[(size_t) b];
    const float dx = blob.x - blob.homeX, dy = blob.y - blob.homeY;
    for (const auto& e : entriesFor (blob))
        if (e.param == param)
            return juce::jlimit (e.min, e.min + e.range, e.base + e.range * kMorphSpan * (e.weightX * dx + e.weightY * dy));
    return param < (int) blob.bases.size() ? blob.bases[(size_t) param] : 0.0f;
}

juce::String FieldSession::describeBlob (int b) const
{
    const auto& blob = blobs[(size_t) b];
    const auto store = engine.getStore (blob.store);
    juce::String text = blob.name.toUpperCase();
    if (store == nullptr)
        return text;

    // The two parameters this position moves the most.
    auto entries = entriesFor (blob);
    const float dx = blob.x - blob.homeX, dy = blob.y - blob.homeY;
    std::stable_sort (entries.begin(), entries.end(), [dx, dy] (const PatchEntry& p, const PatchEntry& q)
    {
        return std::abs (p.weightX * dx + p.weightY * dy) > std::abs (q.weightX * dx + q.weightY * dy);
    });
    for (size_t i = 0; i < std::min<size_t> (2, entries.size()); ++i)
    {
        const auto& info = store->getParamInfo (entries[i].param);
        const float value = store->getParam (entries[i].param);
        text << "  " << juce::String (info.name).toUpperCase() << " "
             << juce::String (value, std::abs (value) < 10.0f ? 2 : 0) << " " << info.unit;
    }
    return text;
}

int FieldSession::pushToGrid()
{
    int written = 0;
    for (int b = 0; b < (int) blobs.size(); ++b)
    {
        const auto& blob = blobs[(size_t) b];
        const auto target = grid.getStore (blob.store);
        if (target == nullptr || target->getType() != blob.type)
            continue;
        if (! blob.tanpura)
            target->setBypassed (! blob.on);
        for (int p = 0; p < target->getNumParams(); ++p)
            if (target->isSceneParam (p) && ownsParam (blob, *target, p) && ! (blob.tanpura && p == SitarBlock::TanpuraOutput))
                target->setParam (p, getMorphedValue (b, p));
        if (! blob.tanpura)
            ++written;
    }
    return written;
}

//==============================================================================
juce::ValueTree FieldSession::captureState() const
{
    juce::ValueTree state ("FieldSession");
    state.setProperty ("version", kFormatVersion, nullptr);
    state.setProperty ("seed", (juce::int64) seed, nullptr);
    state.setProperty ("fusion", fusion, nullptr);
    state.setProperty ("tide", tide.load(), nullptr);
    for (const auto& blob : blobs)
    {
        juce::ValueTree b ("Blob");
        b.setProperty ("store", (int) blob.store, nullptr);
        b.setProperty ("type", getBlockTypeInfo (blob.type).id, nullptr);
        b.setProperty ("x", blob.x, nullptr);
        b.setProperty ("y", blob.y, nullptr);
        b.setProperty ("homeX", blob.homeX, nullptr);
        b.setProperty ("homeY", blob.homeY, nullptr);
        b.setProperty ("stretch", blob.stretch, nullptr);
        b.setProperty ("angle", blob.angle, nullptr);
        b.setProperty ("on", blob.on, nullptr);
        b.setProperty ("tanpura", blob.tanpura, nullptr);
        b.setProperty ("bases", basesText (blob.bases), nullptr);
        state.appendChild (b, nullptr);
    }
    return state;
}

void FieldSession::applyState (const juce::ValueTree& state)
{
    seed = (juce::uint32) (juce::int64) state.getProperty ("seed", (juce::int64) seed);
    fusion = state.getProperty ("fusion", fusion);
    tide.store (state.getProperty ("tide", tide.load()));
    for (const auto& b : state)
        for (auto& blob : blobs)
            if ((int) blob.store == (int) b.getProperty ("store") && blob.tanpura == (bool) b.getProperty ("tanpura", false)
                && blob.type == blockTypeFromId (b.getProperty ("type").toString().toStdString()))
            {
                blob.x = b.getProperty ("x", blob.x);
                blob.y = b.getProperty ("y", blob.y);
                blob.homeX = b.getProperty ("homeX", blob.homeX);
                blob.homeY = b.getProperty ("homeY", blob.homeY);
                blob.stretch = b.getProperty ("stretch", blob.stretch);
                blob.angle = b.getProperty ("angle", blob.angle);
                blob.on = b.getProperty ("on", blob.on);
                if (auto bases = basesFrom (b.getProperty ("bases").toString()); bases.size() == blob.bases.size())
                    blob.bases = std::move (bases);
            }
    writeBasesToStores();
    for (int b = 0; b < (int) blobs.size(); ++b)
        publishLive (b);
    publishPatch();
    updateFusion();
}

void FieldSession::blendTones (int a, int b, float amount)
{
    auto from = tones[(size_t) a].data.createCopy();
    const auto& to = tones[(size_t) b].data;
    const auto mix = [amount] (float p, float q) { return p + (q - p) * amount; };

    from.setProperty ("fusion", mix (from.getProperty ("fusion"), to.getProperty ("fusion")), nullptr);
    from.setProperty ("tide", mix (from.getProperty ("tide"), to.getProperty ("tide")), nullptr);
    if (amount > 0.5f)
        from.setProperty ("seed", to.getProperty ("seed"), nullptr);

    for (auto blob : from)
        for (const auto& other : to)
            if ((int) blob.getProperty ("store") == (int) other.getProperty ("store")
                && (bool) blob.getProperty ("tanpura", false) == (bool) other.getProperty ("tanpura", false)
                && blob.getProperty ("type") == other.getProperty ("type"))
            {
                for (const char* key : { "x", "y", "stretch" })
                    blob.setProperty (key, mix (blob.getProperty (key), other.getProperty (key)), nullptr);

                // The shorter way around the circle.
                const float p = blob.getProperty ("angle"), q = other.getProperty ("angle");
                const float delta = std::remainder (q - p, 6.2831853f);
                blob.setProperty ("angle", p + delta * amount, nullptr);
                if (amount > 0.5f)
                    blob.setProperty ("on", other.getProperty ("on"), nullptr);

                auto bases = basesFrom (blob.getProperty ("bases").toString());
                const auto others = basesFrom (other.getProperty ("bases").toString());
                if (bases.size() == others.size())
                {
                    const auto store = engine.getStore ((StoreId) (int) blob.getProperty ("store"));
                    for (size_t i = 0; i < bases.size(); ++i)
                        if (store != nullptr && (int) i < store->getNumParams())
                            bases[i] = store->getParamInfo ((int) i).isDiscrete ? (amount > 0.5f ? others[i] : bases[i]) : mix (bases[i], others[i]);
                    blob.setProperty ("bases", basesText (bases), nullptr);
                }
            }
    applyState (from);
}

juce::File FieldSession::getDefaultToneFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("KumAmp").getChildFile ("Campo");
}

void FieldSession::setToneFolder (const juce::File& folder)
{
    toneFolder = folder;
    refreshTones();
}

void FieldSession::refreshTones()
{
    tones.clear();
    if (! toneFolder.isDirectory())
        return;
    auto files = toneFolder.findChildFiles (juce::File::findFiles, false, "*.kumcampo");
    files.sort();
    for (const auto& file : files)
        if (auto xml = juce::XmlDocument::parse (file))
        {
            const auto data = juce::ValueTree::fromXml (*xml);
            if (data.hasType ("FieldSession"))
                tones.push_back ({ file, file.getFileNameWithoutExtension(), data });
        }
}

bool FieldSession::saveTone()
{
    if (! toneFolder.createDirectory())
        return false;

    int number = 1;
    auto file = toneFolder.getChildFile ("Tono 01.kumcampo");
    while (file.exists())
        file = toneFolder.getChildFile ("Tono " + juce::String (++number).paddedLeft ('0', 2) + ".kumcampo");

    const auto xml = captureState().createXml();
    if (xml == nullptr || ! xml->writeTo (file))
        return false;
    refreshTones();
    return true;
}

int FieldSession::loadTone (int tone)
{
    const auto& data = tones[(size_t) tone].data;
    int missing = 0;
    for (const auto& blob : blobs)
    {
        bool found = false;
        for (const auto& b : data)
            found = found || ((int) b.getProperty ("store") == (int) blob.store
                              && (bool) b.getProperty ("tanpura", false) == blob.tanpura
                              && b.getProperty ("type").toString() == getBlockTypeInfo (blob.type).id);
        missing += found ? 0 : 1;
    }
    applyState (data);
    return missing;
}

bool FieldSession::removeTone (int tone)
{
    const bool removed = tones[(size_t) tone].file.moveToTrash();
    refreshTones();
    return removed;
}

//==============================================================================
juce::ValueTree FieldSession::saveState() const
{
    return pendingState.isValid() ? pendingState.createCopy() : captureState();
}

void FieldSession::loadState (const juce::ValueTree& state)
{
    pendingState = state.createCopy();
    blobs.clear();
}
} // namespace kumamp
