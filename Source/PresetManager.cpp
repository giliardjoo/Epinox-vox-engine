#include "PresetManager.h"
#include "Parameters.h"
#include "FactoryPresets.h"

namespace epinox
{
PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state) : apvts (state) {}

juce::File PresetManager::getPresetDirectory() const
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("EPINOX")
                   .getChildFile ("VOX ENGINE")
                   .getChildFile ("Presets");
    dir.createDirectory();
    return dir;
}

juce::Array<juce::File> PresetManager::listPresets() const
{
    auto files = getPresetDirectory().findChildFiles (juce::File::findFiles, false, "*.json");
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getFileName().compareNatural (b.getFileName()) < 0; });
    return files;
}

bool PresetManager::savePreset (const juce::File& file)
{
    auto* params = new juce::DynamicObject();

    for (auto* p : apvts.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            params->setProperty (juce::Identifier (ranged->paramID), ranged->convertFrom0to1 (ranged->getValue()));

    auto* root = new juce::DynamicObject();
    root->setProperty ("plugin", "EPINOX VOX ENGINE");
    root->setProperty ("version", 1);
    root->setProperty ("name", file.getFileNameWithoutExtension());
    root->setProperty ("phrase", apvts.state.getProperty ("phrase").toString());
    root->setProperty ("params", juce::var (params));

    if (! file.replaceWithText (juce::JSON::toString (juce::var (root))))
        return false;

    currentFile = file;
    currentName = file.getFileNameWithoutExtension();
    return true;
}

bool PresetManager::loadPreset (const juce::File& file)
{
    auto parsed = juce::JSON::parse (file);
    auto* root = parsed.getDynamicObject();

    if (root == nullptr || root->getProperty ("plugin").toString() != "EPINOX VOX ENGINE")
        return false;

    if (auto* params = root->getProperty ("params").getDynamicObject())
    {
        for (const auto& nv : params->getProperties())
        {
            const auto idString = nv.name.toString();

            if (idString == pid::power)   // a preset never switches the plugin off
                continue;

            if (auto* p = apvts.getParameter (idString))
                p->setValueNotifyingHost (p->convertTo0to1 (static_cast<float> (nv.value)));
        }
    }

    apvts.state.setProperty ("phrase", root->getProperty ("phrase").toString(), nullptr);

    currentFile = file;
    currentFactory = -1;
    currentName = file.getFileNameWithoutExtension();
    return true;
}

bool PresetManager::loadFactory (int index)
{
    const auto& list = factoryPresets();

    if (index < 0 || index >= static_cast<int> (list.size()))
        return false;

    const auto& preset = list[static_cast<size_t> (index)];

    // Start from the defaults (except Power), then apply what the preset sets.
    for (auto* p : apvts.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (ranged->paramID != juce::String (pid::power))
                ranged->setValueNotifyingHost (ranged->getDefaultValue());

    for (const auto& kv : preset.values)
        if (auto* p = apvts.getParameter (kv.first))
            p->setValueNotifyingHost (p->convertTo0to1 (kv.second));

    apvts.state.setProperty ("phrase", preset.phrase, nullptr);

    currentFile = juce::File();
    currentFactory = index;
    currentName = preset.name;
    return true;
}

bool PresetManager::loadRelative (int delta)
{
    const auto files = listPresets();
    const int nf = static_cast<int> (factoryPresets().size());
    const int total = nf + files.size();

    int current = -1;

    if (currentFactory >= 0)
        current = currentFactory;
    else if (currentFile != juce::File() && files.indexOf (currentFile) >= 0)
        current = nf + files.indexOf (currentFile);

    int next = 0;

    if (current < 0)
        next = delta > 0 ? 0 : total - 1;
    else
        next = (current + delta % total + total) % total;

    return next < nf ? loadFactory (next) : loadPreset (files.getReference (next - nf));
}
} // namespace epinox
