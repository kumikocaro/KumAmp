#include "PresetManager.h"

namespace kumamp
{
namespace
{
    const juce::Identifier kPresetTag  { "KumAmpPreset" };
    const juce::Identifier kGlobalsTag { "Globals" };
    const juce::Identifier kGridTag    { "KumAmpGrid" };
    const juce::Identifier kStoresTag  { "Stores" };
    constexpr int kFormatVersion = 1;
} // namespace

PresetManager::PresetManager (GridEngine& e, juce::AudioProcessorValueTreeState& p)
    : engine (e), parameters (p), folder (getDefaultFolder())
{
}

juce::File PresetManager::getDefaultFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("KumAmp").getChildFile ("Presets");
}

const juce::StringArray& PresetManager::globalParameterIds()
{
    static const juce::StringArray ids { "gate_on", "gate_threshold", "gate_release", "gate_mode",
                                         "pitch_on", "pitch_semitones", "pitch_instrument",
                                         "output_gain" };
    return ids;
}

juce::File PresetManager::fileFor (const juce::String& name, const juce::String& category) const
{
    auto dir = folder;
    if (category.trim().isNotEmpty())
        dir = dir.getChildFile (juce::File::createLegalFileName (category.trim()));
    return dir.getChildFile (juce::File::createLegalFileName (name.trim()) + kExtension);
}

PresetManager::Info PresetManager::readInfo (const juce::File& file)
{
    Info info;
    info.file = file;
    info.name = file.getFileNameWithoutExtension();
    if (auto xml = juce::XmlDocument::parse (file); xml != nullptr && xml->hasTagName (kPresetTag.toString()))
    {
        info.name    = xml->getStringAttribute ("name", info.name);
        info.author  = xml->getStringAttribute ("author");
        info.comment = xml->getStringAttribute ("comment");
        info.created = juce::Time::fromISO8601 (xml->getStringAttribute ("created"));
    }
    return info;
}

std::vector<PresetManager::Info> PresetManager::scan() const
{
    std::vector<Info> presets;
    if (! folder.isDirectory())
        return presets;

    for (const auto& entry : juce::RangedDirectoryIterator (folder, true, juce::String ("*") + kExtension))
    {
        auto info = readInfo (entry.getFile());
        const auto parent = entry.getFile().getParentDirectory();
        info.category = parent == folder ? juce::String() : parent.getRelativePathFrom (folder).replaceCharacter ('\\', '/');
        presets.push_back (std::move (info));
    }

    std::sort (presets.begin(), presets.end(), [] (const Info& a, const Info& b)
    {
        if (a.category != b.category)
            return a.category.compareNatural (b.category, false) < 0;
        return a.name.compareNatural (b.name, false) < 0;
    });
    return presets;
}

juce::Result PresetManager::save (const juce::String& name, const juce::String& author,
                                  const juce::String& category, const juce::String& comment)
{
    if (name.trim().isEmpty())
        return juce::Result::fail ("El preset necesita un nombre");

    const auto file = fileFor (name, category);
    if (const auto dir = file.getParentDirectory(); ! dir.createDirectory())
        return juce::Result::fail ("No se pudo crear la carpeta " + dir.getFullPathName());

    juce::ValueTree globals (kGlobalsTag);
    for (const auto& id : globalParameterIds())
        if (auto* value = parameters.getRawParameterValue (id))
            globals.setProperty (id, value->load(), nullptr);

    // Model / IR paths: keep the absolute path and add one relative to the preset.
    auto grid = engine.saveState();
    for (auto store : grid.getChildWithName (kStoresTag))
        for (int i = 0; i < store.getNumProperties(); ++i)
        {
            const auto key = store.getPropertyName (i).toString();
            if (! key.startsWith ("file_") || ! juce::File::isAbsolutePath (store.getProperty (key).toString()))
                continue;   // factory cabinets ("kumamp:cab/...") are not files
            const juce::File model (store.getProperty (key).toString());
            store.setProperty ("rel_" + key, model.getRelativePathFrom (file.getParentDirectory()).replaceCharacter ('\\', '/'), nullptr);
        }

    const auto result = write (file, name, author, comment, globals, grid);
    if (result.wasOk())
    {
        currentName = name.trim();
        currentFile = file;
    }
    return result;
}

juce::Result PresetManager::write (const juce::File& file, const juce::String& name, const juce::String& author,
                                   const juce::String& comment, const juce::ValueTree& globals, const juce::ValueTree& grid)
{
    juce::ValueTree preset (kPresetTag);
    preset.setProperty ("version", kFormatVersion, nullptr);
    preset.setProperty ("name", name.trim(), nullptr);
    preset.setProperty ("author", author.trim(), nullptr);
    preset.setProperty ("comment", comment.trim(), nullptr);
    preset.setProperty ("created", juce::Time::getCurrentTime().toISO8601 (true), nullptr);
    preset.appendChild (globals.createCopy(), nullptr);
    preset.appendChild (grid.createCopy(), nullptr);

    auto xml = preset.createXml();
    if (xml == nullptr || ! xml->writeTo (file))
        return juce::Result::fail ("No se pudo escribir " + file.getFullPathName());
    return juce::Result::ok();
}

juce::File PresetManager::findMissing (const juce::String& absolute, const juce::String& relative,
                                       const juce::File& presetFile) const
{
    if (juce::File::isAbsolutePath (absolute) && juce::File (absolute).existsAsFile())
        return juce::File (absolute);

    const auto presetDir = presetFile.getParentDirectory();
    if (relative.isNotEmpty())
        if (const auto f = presetDir.getChildFile (relative); f.existsAsFile())
            return f;

    // By file name: the preset's own folder tree, then the whole KumAmp folder.
    const auto fileName = absolute.replaceCharacter ('\\', '/').fromLastOccurrenceOf ("/", false, false);
    if (fileName.isEmpty())
        return {};
    for (const auto& root : { presetDir, getDefaultFolder().getParentDirectory() })
        if (root.isDirectory())
            for (const auto& entry : juce::RangedDirectoryIterator (root, true, fileName))
                return entry.getFile();
    return {};
}

void PresetManager::relocateFiles (juce::ValueTree grid, const juce::File& presetFile) const
{
    for (auto store : grid.getChildWithName (kStoresTag))
        for (int i = store.getNumProperties(); --i >= 0;)
        {
            const auto key = store.getPropertyName (i).toString();
            if (! key.startsWith ("file_") || store.getProperty (key).toString().startsWith ("kumamp:"))
                continue;
            const auto found = findMissing (store.getProperty (key).toString(), store.getProperty ("rel_" + key).toString(), presetFile);
            if (found != juce::File())
                store.setProperty (key, found.getFullPathName(), nullptr);
        }
}

juce::Result PresetManager::load (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr)
        return juce::Result::fail ("No se pudo leer " + file.getFileName());

    const auto preset = juce::ValueTree::fromXml (*xml);
    if (! preset.hasType (kPresetTag))
        return juce::Result::fail (file.getFileName() + " no es un preset de KumAmp");

    auto grid = preset.getChildWithName (kGridTag).createCopy();
    if (! grid.isValid())
        return juce::Result::fail (file.getFileName() + " no tiene cadena de efectos");

    relocateFiles (grid, file);
    engine.loadState (grid);

    const auto globals = preset.getChildWithName (kGlobalsTag);
    for (const auto& id : globalParameterIds())
        if (auto* param = parameters.getParameter (id); param != nullptr && globals.hasProperty (id))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 ((float) globals.getProperty (id)));
            param->endChangeGesture();
        }

    currentName = preset.getProperty ("name", file.getFileNameWithoutExtension()).toString();
    currentFile = file;
    return juce::Result::ok();
}

bool PresetManager::remove (const juce::File& file)
{
    if (! file.hasFileExtension (kExtension) || ! file.isAChildOf (folder))
        return false;
    const bool ok = file.moveToTrash();
    if (ok && file == currentFile)
        currentFile = juce::File();
    return ok;
}
} // namespace kumamp
