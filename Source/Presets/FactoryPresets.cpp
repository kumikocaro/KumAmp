#include "FactoryPresets.h"

#include "PresetManager.h"
#include "../DSP/Cab/FactoryCabs.h"
#include "../Graph/GridModel.h"

#include <map>
#include <string>
#include <vector>

namespace kumamp::factorypresets
{
namespace
{
    using Values = std::vector<std::pair<const char*, float>>;

    struct Block
    {
        BlockType type;
        const char* name;
        bool on;
        Values params;
        std::string file;   // a model file name, or a factory cabinet path
    };

    /** Scene B: what changes for a solo. A parameter of "on" switches the block (value 0 / 1). */
    struct Change { const char* block; const char* param; float value; };

    struct Preset
    {
        const char* category;
        const char* name;
        const char* comment;
        float gateDb;
        std::vector<Block> blocks;
        std::vector<Change> solo;
    };

    // Models the presets were written around, with their authors on TONE3000.
    #define KUMAMP_T3K "tone3000.com/tones/"
    const char* const kAc30      = "'65 AC306 - The Iconic Cleanish.nam";
    const char* const kAc30Tb    = "RR AC30 TB 421.nam";
    const char* const kJmp50     = "MRSH JM50LD I Crunch2 FAT CAB.nam";
    const char* const kPlexi     = "Marshall 1959BJA SUPER BOWL SETTINGS.nam";
    const char* const kRecto     = "FR MBDR MW Red Mdn - 2 - 4FB LL SM57a.nam";
    const char* const kPeavey    = "Full Rig Peavey 5150 Maxon Mesa OS SM57 - jp_is_out_of_tune.nam";
    const char* const kTerror    = "ODN-DT15-HIGHGAIN-03-CH1-T4G7_XS.nam";
    const char* const kSvtClean  = "SVT CLEAN.nam";
    const char* const kSvtDirty  = "SVT DIRTY.nam";
    const char* const kSvtGlass  = "SVT DARKGLASS SHRED MY D.I.nam";

    #define KUMAMP_FROM_AC30   "NAM: 1965 VOX AC30 Top Boost, de claybennett. " KUMAMP_T3K "1965-vox-ac30-top-boost-66682"
    #define KUMAMP_FROM_AC30TB "NAM: RR AC30 TB, de robyrocks. " KUMAMP_T3K "rr-ac30-tb-75774"
    #define KUMAMP_FROM_JMP50  "NAM: Marshall JMP-50 Lead, de amalgamaudio. " KUMAMP_T3K "marshall-jmp-50-lead-1969-plexi-a2-65578"
    #define KUMAMP_FROM_PLEXI  "NAM: Marshall 1959BJA, de rjcproductions. " KUMAMP_T3K "marshall-1959bja-76884"
    #define KUMAMP_FROM_RECTO  "NAM: Mesa Dual Rectifier, de outmodedelectronics. " KUMAMP_T3K "mesa-dual-rectifier-mw-red-modern-mesa-4x12-full-rig-69206"
    #define KUMAMP_FROM_PEAVEY "NAM: Peavey 5150, de jpisoutoftune. " KUMAMP_T3K "full-rig-peavey-5150-mesa-4x12-32868"
    #define KUMAMP_FROM_TERROR "NAM: Dark Terror, de davidaoverdriven. " KUMAMP_T3K "dark-terror-43274"
    #define KUMAMP_FROM_SVT    "NAM: Ampeg SVT-CL, de deathblossomaudio. " KUMAMP_T3K "ampeg-svt-cl-bass-head-45809"

    /** masterDb evens out the presets: without it, a clean one sits some 25 dB under a metal one. */
    Block amp (const char* model, float masterDb, float inputDb = 0.0f)
    {
        return { BlockType::NamAmp, "Amp", true, { { "input", inputDb }, { "output", masterDb } }, model };
    }
    Block cab (const char* cabId, const char* micId)        { return { BlockType::IrLoader, "Cab", true, {}, cabs::pathFor (cabId, micId) }; }
    Block drive (bool on, float amount, float tone, float levelDb, float model)
    {
        return { BlockType::Overdrive, "Drive", on, { { "drive", amount }, { "tone", tone }, { "level", levelDb }, { "model", model } }, {} };
    }
    Block comp (float thresholdDb, float ratio, float attackMs)
    {
        return { BlockType::Compressor, "Comp", true, { { "threshold", thresholdDb }, { "ratio", ratio }, { "attack", attackMs } }, {} };
    }
    Block chorus (bool on, float rate, float mix)           { return { BlockType::Chorus, "Chorus", on, { { "rate", rate }, { "mix", mix } }, {} }; }
    Block tremolo (float rate, float depth)                 { return { BlockType::Tremolo, "Trem", true, { { "rate", rate }, { "depth", depth } }, {} }; }
    Block phaser (bool on, float rate, float mix)           { return { BlockType::Phaser, "Phaser", on, { { "rate", rate }, { "mix", mix } }, {} }; }
    Block repeat (bool on, float rate, float decay)         { return { BlockType::RepeatPercussion, "Repeat", on, { { "rate", rate }, { "decay", decay } }, {} }; }
    Block analog (bool on, float ms, float feedback, float mix)
    {
        return { BlockType::AnalogDelay, "Delay", on, { { "time", ms }, { "feedback", feedback }, { "mix", mix } }, {} };
    }
    Block tape (bool on, float ms, float feedback, float mix)
    {
        return { BlockType::TapeDelay, "Delay", on, { { "time", ms }, { "feedback", feedback }, { "mix", mix } }, {} };
    }
    Block plate (float decay, float mix)                    { return { BlockType::PlateReverb, "Reverb", true, { { "decay", decay }, { "mix", mix } }, {} }; }
    Block spring (float decay, float drip, float mix)       { return { BlockType::SpringReverb, "Reverb", true, { { "decay", decay }, { "drip", drip }, { "mix", mix } }, {} }; }
    Block cloud (float decay, float mix)                    { return { BlockType::CloudReverb, "Reverb", true, { { "decay", decay }, { "mix", mix } }, {} }; }
    Block shimmer (float decay, float mix)                  { return { BlockType::ShimmerReverb, "Reverb", true, { { "decay", decay }, { "mix", mix } }, {} }; }

    const std::vector<Preset>& presets()
    {
        static const std::vector<Preset> list {
            // Limpios
            { "Limpios", "Limpio Cristalino", KUMAMP_FROM_AC30, -65.0f,
              { comp (-24.0f, 3.0f, 15.0f), amp (kAc30, 12.0f), cab ("2x12-abierto", "condensador"), chorus (true, 0.6f, 30.0f), analog (false, 380.0f, 25.0f, 20.0f), plate (40.0f, 18.0f) },
              { { "Delay", "on", 1.0f } } },
            { "Limpios", "Funk Seco", KUMAMP_FROM_AC30TB, -62.0f,
              { comp (-30.0f, 5.0f, 4.0f), amp (kAc30Tb, 4.0f), cab ("1x12-britanico", "din-centro"), phaser (false, 0.5f, 45.0f), spring (30.0f, 30.0f, 8.0f) },
              { { "Phaser", "on", 1.0f } } },
            { "Limpios", "Surf Repeat", KUMAMP_FROM_AC30, -65.0f,
              { amp (kAc30, 12.0f), cab ("2x12-abierto", "din-centro"), repeat (false, 8.0f, 45.0f), tape (true, 130.0f, 12.0f, 18.0f), spring (65.0f, 85.0f, 35.0f) },
              { { "Repeat", "on", 1.0f } } },
            // Blues y crunch
            { "Blues y Crunch", "Blues de Caja", KUMAMP_FROM_JMP50, -60.0f,
              { drive (false, 30.0f, 55.0f, 3.0f, 0.0f), amp (kJmp50, -5.0f), spring (40.0f, 40.0f, 18.0f) },
              { { "Drive", "on", 1.0f } } },
            { "Blues y Crunch", "Plexi Crunch", KUMAMP_FROM_PLEXI, -58.0f,
              { drive (false, 15.0f, 55.0f, 6.0f, 1.0f), amp (kPlexi, 0.0f), cab ("4x12-vintage", "din-centro"), plate (35.0f, 12.0f) },
              { { "Drive", "on", 1.0f } } },
            { "Blues y Crunch", "Crunch Percusivo", KUMAMP_FROM_AC30TB, -60.0f,
              { amp (kAc30Tb, 12.0f, 4.0f), cab ("2x12-abierto", "din-centro"), repeat (true, 6.0f, 35.0f), tape (false, 300.0f, 25.0f, 20.0f), plate (30.0f, 12.0f) },
              { { "Repeat", "on", 0.0f }, { "Delay", "on", 1.0f } } },
            // Rock
            { "Rock", "Rock Clasico", KUMAMP_FROM_PLEXI, -55.0f,
              { drive (true, 15.0f, 55.0f, 6.0f, 1.0f), amp (kPlexi, 2.0f), cab ("4x12-vintage", "din-borde"), analog (false, 380.0f, 28.0f, 22.0f), plate (30.0f, 14.0f) },
              { { "Delay", "on", 1.0f } } },
            { "Rock", "Hard Rock", KUMAMP_FROM_JMP50, -52.0f,
              { drive (true, 40.0f, 50.0f, 3.0f, 2.0f), amp (kJmp50, -5.0f), analog (false, 420.0f, 30.0f, 22.0f), plate (35.0f, 12.0f) },
              { { "Delay", "on", 1.0f } } },
            { "Rock", "Lead Cantante", KUMAMP_FROM_PLEXI, -52.0f,
              { drive (true, 35.0f, 60.0f, 4.0f, 0.0f), amp (kPlexi, 2.0f), cab ("4x12-vintage", "mezcla"), analog (true, 440.0f, 35.0f, 28.0f), plate (50.0f, 22.0f) },
              { { "Delay", "mix", 38.0f } } },
            // Metal
            { "Metal", "Rectifier Moderno", KUMAMP_FROM_RECTO, -48.0f,
              { amp (kRecto, -8.0f), analog (false, 400.0f, 25.0f, 18.0f), plate (25.0f, 8.0f) },
              { { "Delay", "on", 1.0f } } },
            { "Metal", "5150 Ritmico", KUMAMP_FROM_PEAVEY, -48.0f,
              { amp (kPeavey, -7.0f), analog (false, 380.0f, 25.0f, 18.0f), plate (25.0f, 6.0f) },
              { { "Delay", "on", 1.0f } } },
            { "Metal", "Terror Britanico", KUMAMP_FROM_TERROR, -48.0f,
              { drive (true, 5.0f, 60.0f, 6.0f, 0.0f), amp (kTerror, 0.0f), cab ("4x12-moderno", "din-centro"), analog (false, 400.0f, 25.0f, 18.0f), plate (25.0f, 8.0f) },
              { { "Delay", "on", 1.0f } } },
            // Ambient
            { "Ambient", "Nube", KUMAMP_FROM_AC30, -65.0f,
              { comp (-24.0f, 3.0f, 15.0f), amp (kAc30, 5.0f), cab ("2x12-abierto", "cinta"), tape (true, 480.0f, 45.0f, 30.0f), cloud (10.0f, 50.0f) },
              { { "Reverb", "mix", 70.0f } } },
            { "Ambient", "Shimmer Largo", KUMAMP_FROM_AC30, -65.0f,
              { amp (kAc30, 10.0f), cab ("2x12-abierto", "condensador"), analog (true, 450.0f, 50.0f, 30.0f), shimmer (12.0f, 45.0f) },
              { { "Reverb", "mix", 65.0f } } },
            { "Ambient", "Trem y Cinta", KUMAMP_FROM_AC30TB, -62.0f,
              { amp (kAc30Tb, 4.0f), cab ("1x12-britanico", "cinta"), tremolo (4.0f, 50.0f), tape (true, 360.0f, 50.0f, 32.0f), spring (55.0f, 50.0f, 28.0f) },
              { { "Delay", "feedback", 70.0f } } },
            // Bajo
            { "Bajo", "Bajo Limpio", KUMAMP_FROM_SVT, -60.0f,
              { comp (-22.0f, 4.0f, 20.0f), amp (kSvtClean, 6.0f), cab ("8x10-bajo", "din-centro") },
              { { "Comp", "ratio", 6.0f } } },
            { "Bajo", "Bajo Sucio", KUMAMP_FROM_SVT, -55.0f,
              { comp (-20.0f, 4.0f, 20.0f), amp (kSvtDirty, 4.0f), cab ("8x10-bajo", "din-centro") },
              { { "Comp", "ratio", 6.0f } } },
            { "Bajo", "Bajo Moderno", KUMAMP_FROM_SVT, -50.0f,
              { comp (-20.0f, 4.0f, 15.0f), amp (kSvtGlass, -4.0f) },
              { { "Comp", "ratio", 6.0f } } },
        };
        return list;
    }

    struct SimpPreset { Preset preset; const char* modelFile; const char* modelTitle; };

    const std::vector<SimpPreset>& simpPresets()
    {
        static const std::vector<SimpPreset> list {
            { { "Simp", "Clean", KUMAMP_FROM_AC30, -65.0f,
                { comp (-24.0f, 3.0f, 15.0f), amp (kAc30, -2.0f), plate (35.0f, 14.0f) }, {} },
              kAc30, "1965 VOX AC30 Top Boost" },
            { { "Simp", "High Gain", KUMAMP_FROM_PEAVEY, -48.0f,
                { amp (kPeavey, -7.0f), plate (25.0f, 6.0f) }, {} },
              kPeavey, "Peavey 5150" },
            { { "Simp", "Crunch", KUMAMP_FROM_JMP50, -58.0f,
                { amp (kJmp50, -4.0f), plate (30.0f, 10.0f) }, {} },
              kJmp50, "Marshall JMP-50 Lead" },
            { { "Simp", "Synth", "", -60.0f,
                { { BlockType::GuitarSynth, "Synth", true, { { "mode", 1.0f }, { "cutoff", 2400.0f }, { "mix", 100.0f }, { "level", -7.0f } }, {} },
                  chorus (true, 0.5f, 30.0f), plate (45.0f, 22.0f) }, {} },
              "", "" },
            { { "Simp", "Blues", KUMAMP_FROM_AC30TB, -60.0f,
                { drive (true, 30.0f, 55.0f, 3.0f, 0.0f), amp (kAc30Tb, -3.0f), spring (40.0f, 40.0f, 18.0f) }, {} },
              kAc30Tb, "RR AC30 TB" },
            { { "Simp", "Sitar", "", -60.0f,
                { { BlockType::Sitar, "Sitar", true, {}, {} }, plate (50.0f, 18.0f) }, {} },
              "", "" },
        };
        return list;
    }

    bool setParam (ParamStore& store, const char* id, float value)
    {
        for (int p = 0; p < store.getNumParams(); ++p)
            if (std::string_view (store.getParamInfo (p).id) == id)
            {
                store.setParam (p, value);
                return true;
            }
        jassertfalse;   // a typo in the table above
        return false;
    }

    /** The grid state of a preset: a chain with its scenes A Base, B Solo, C Mas espacio, D Seco. */
    juce::ValueTree buildGrid (const Preset& preset)
    {
        GridEngine engine;
        engine.prepare (48000.0, 512);

        std::map<std::string, std::shared_ptr<ParamStore>> stores;
        GridModel model;
        NodeId previous = model.addNode (NodeKind::Input);
        for (const auto& block : preset.blocks)
        {
            auto store = engine.createStore (block.type, block.name);
            stores[block.name] = store;
            const NodeId node = model.addNode (NodeKind::Block, store->getId());
            model.connect ({ previous, 0 }, node);
            previous = node;
        }
        model.connect ({ previous, 0 }, model.addNode (NodeKind::Output));
        engine.commitModel (std::move (model));

        const auto applyBase = [&]
        {
            for (const auto& block : preset.blocks)
            {
                auto& store = *stores[block.name];
                store.setBypassed (! block.on);
                for (const auto& [id, value] : block.params)
                    setParam (store, id, value);
            }
        };

        const auto isTimeBlock = [] (BlockType type) { return getBlockTypeInfo (type).category == BlockCategory::Time; };

        applyBase();
        engine.setSceneName (0, "Base");
        engine.storeScene (0);

        for (const auto& change : preset.solo)
        {
            if (std::string_view (change.param) == "on")
                stores[change.block]->setBypassed (change.value < 0.5f);
            else
                setParam (*stores[change.block], change.param, change.value);
        }
        engine.setSceneName (1, "Solo");
        engine.storeScene (1);

        applyBase();
        for (const auto& block : preset.blocks)
            if (const auto type = block.type; type == BlockType::PlateReverb || type == BlockType::SpringReverb
                                              || type == BlockType::CloudReverb || type == BlockType::ShimmerReverb)
                for (const auto& [id, value] : block.params)
                    if (std::string_view (id) == "mix")
                        setParam (*stores[block.name], "mix", std::min (80.0f, value * 1.6f));
        engine.setSceneName (2, "Mas espacio");
        engine.storeScene (2);

        applyBase();
        for (const auto& block : preset.blocks)
            if (isTimeBlock (block.type) && block.type != BlockType::RepeatPercussion)
                stores[block.name]->setBypassed (true);
        engine.setSceneName (3, "Seco");
        engine.storeScene (3);

        applyBase();
        auto grid = engine.saveState();

        // Named here instead of loaded: the cabinets render on load and the models are the player's files.
        for (const auto& block : preset.blocks)
            if (! block.file.empty())
                for (auto store : grid.getChildWithName ("Stores"))
                    if (store.getProperty ("name").toString() == block.name)
                        store.setProperty ("file_0", juce::String (block.file), nullptr);
        return grid;
    }
} // namespace

static bool writePreset (const juce::File& file, const Preset& preset)
{
    juce::ValueTree globals ("Globals");
    globals.setProperty ("gate_on", 1.0f, nullptr);
    globals.setProperty ("gate_threshold", preset.gateDb, nullptr);
    globals.setProperty ("gate_release", 120.0f, nullptr);
    globals.setProperty ("gate_mode", 0.0f, nullptr);
    globals.setProperty ("pitch_on", 0.0f, nullptr);
    globals.setProperty ("pitch_semitones", 0.0f, nullptr);
    globals.setProperty ("output_gain", 0.0f, nullptr);

    return file.getParentDirectory().createDirectory()
        && PresetManager::write (file, preset.name, "KumAmp", preset.comment, globals, buildGrid (preset)).wasOk();
}

int getNumSimpPresets() noexcept                 { return (int) simpPresets().size(); }
juce::String getSimpPresetName (int preset)      { return simpPresets()[(size_t) preset].preset.name; }
juce::String getSimpModelFile (int preset)       { return simpPresets()[(size_t) preset].modelFile; }
juce::String getSimpModelTitle (int preset)      { return simpPresets()[(size_t) preset].modelTitle; }

juce::File writeSimpPreset (const juce::File& presetsFolder, int preset)
{
    const auto& entry = simpPresets()[(size_t) preset].preset;
    const auto file = presetsFolder.getChildFile (entry.category).getChildFile (juce::String (entry.name) + PresetManager::kExtension);
    // Once there it is the player's: editing it does not get undone by the next click.
    if (! file.existsAsFile())
        writePreset (file, entry);
    return file;
}

int install (const juce::File& presetsFolder)
{
    const auto root = presetsFolder.getChildFile ("Referencia");
    const auto marker = root.getChildFile (".instalados");
    if (marker.existsAsFile() || ! root.createDirectory())
        return 0;

    int written = 0;
    for (const auto& preset : presets())
    {
        const auto file = root.getChildFile (preset.category).getChildFile (juce::String (preset.name) + PresetManager::kExtension);
        if (writePreset (file, preset))
            ++written;
    }
    marker.replaceWithText ("1\n");
    return written;
}
} // namespace kumamp::factorypresets
