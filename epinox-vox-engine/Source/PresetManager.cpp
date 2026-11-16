#include "PresetManager.h"
#include "Parameters.h"

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
    currentName = file.getFileNameWithoutExtension();
    return true;
}

bool PresetManager::loadRelative (int delta)
{
    auto files = listPresets();
    const int n = files.size();

    if (n == 0)
        return false;

    const int idx = files.indexOf (currentFile);
    int next = 0;

    if (idx < 0)
        next = delta > 0 ? 0 : n - 1;
    else
        next = (idx + delta % n + n) % n;

    return loadPreset (files.getReference (next));
}
} // namespace epinox
