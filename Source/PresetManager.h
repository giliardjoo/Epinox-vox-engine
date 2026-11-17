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

    juce::String getCurrentName() const { return currentName; }

private:
    juce::AudioProcessorValueTreeState& apvts;
    juce::File currentFile;
    juce::String currentName { "Init" };
};
} // namespace epinox
