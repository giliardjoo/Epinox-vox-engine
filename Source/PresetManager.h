#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace epinox
{
// Presets are JSON files holding every automatable parameter plus the phrase text.
class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState& state);

    juce::File getPresetDirectory() const;
    juce::Array<juce::File> listPresets() const;

    bool savePreset (const juce::File& file);
    bool loadPreset (const juce::File& file);
    bool loadRelative (int delta);

    // Previous / next step through the factory presets of the genre selected in the list, wrapping at its ends.
    bool loadFactory (int index);
    bool isFactoryCurrent() const { return currentFactory >= 0; }

    juce::String getCurrentName() const { return currentName; }

private:
    juce::AudioProcessorValueTreeState& apvts;
    juce::File currentFile;
    int currentFactory = -1;
    juce::String currentName { "Init" };
};
} // namespace epinox
