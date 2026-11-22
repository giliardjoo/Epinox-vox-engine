#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include "PluginProcessor.h"
#include "UI/NeonLookAndFeel.h"

class VoxContent;

class VoxEditor : public juce::AudioProcessorEditor
{
public:
    explicit VoxEditor (VoxProcessor&);
    ~VoxEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    static constexpr int designWidth = 1100;
    static constexpr int designHeight = 998;

    epinox::ui::NeonLookAndFeel laf;   // declared before content: destroyed after it
    std::unique_ptr<VoxContent> content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxEditor)
};
