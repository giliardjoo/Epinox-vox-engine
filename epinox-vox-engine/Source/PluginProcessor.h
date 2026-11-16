#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include "Parameters.h"
#include "PresetManager.h"
#include "GenerationService.h"

class VoxProcessor : public juce::AudioProcessor
{
public:
    static constexpr int scopeSize = 256;

    VoxProcessor();
    ~VoxProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "EPINOX VOX ENGINE"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    epinox::PresetManager presets;
    epinox::GenerationService generation;

    // Written by the audio thread, read by the UI. Lock-free.
    std::atomic<float> inPeak { 0.0f }, inRms { 0.0f }, outPeak { 0.0f }, outRms { 0.0f };
    std::atomic<bool> clipped { false };
    std::array<std::atomic<float>, scopeSize> scope;
    std::atomic<int> scopeWrite { 0 };

private:
    std::atomic<float>* powerParam = nullptr;
    std::atomic<float>* gainParam = nullptr;

    float lastGain = 1.0f;
    int binSamples = 512;
    int binCount = 0;
    float binAcc = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxProcessor)
};
