#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>
#include "Parameters.h"
#include "PresetManager.h"
#include "GenerationService.h"
#include "DSP/VoxEngine.h"

class VoxProcessor : public juce::AudioProcessor
{
public:
    static constexpr int scopeSize = 256;

    VoxProcessor();
    ~VoxProcessor() override;

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
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    // Vocal generation (message thread only)

    // Starts generating a vocal from the request. The sample rate is filled in here.
    // Returns false when a generation is already running.
    bool requestGeneration (epinox::GenerationRequest request);
    bool isGenerating() const { return generation.isBusy(); }
    juce::uint32 nextVariation() { return variationCounter++; }

    // Last message about generation, and a counter that changes whenever it does.
    juce::String getGenerationStatus() const { return generationStatus; }
    bool generationStatusIsWarning() const { return generationWarning; }
    int getGenerationStatusVersion() const { return generationStatusVersion; }

    // Tempo reported by the host, or 0 when the host does not provide one.
    double getHostBpm() const { return hostBpm.load(); }

    juce::AudioProcessorValueTreeState apvts;
    epinox::PresetManager presets;

    // Written by the audio thread, read by the UI. Lock-free.
    std::atomic<float> inPeak { 0.0f }, inRms { 0.0f }, outPeak { 0.0f }, outRms { 0.0f };
    std::atomic<bool> clipped { false };
    std::array<std::atomic<float>, scopeSize> scope;
    std::atomic<int> scopeWrite { 0 };

private:
    epinox::dsp::EngineParams readParams() const;

    void setClip (std::shared_ptr<epinox::GeneratedClip> newClip, const juce::String& key);
    void setStatus (const juce::String& text, bool warning);
    // Rebuilds the vocal from the settings saved in the project state. With clearIfNone, a state without a
    // vocal also removes the one that is currently loaded.
    void rebuildGeneratedVocal (bool clearIfNone = false);

    epinox::dsp::VoxEngine engine;
    std::atomic<float>* gainParam = nullptr;

    float lastGain = 1.0f;
    int binSamples = 512;
    int binCount = 0;
    float binAcc = 0.0f;

    // Generated vocal. The audio thread only reads `clip`; clips are kept alive for a few seconds after
    // being replaced so a block that is still reading one never sees it freed.
    std::atomic<const epinox::GeneratedClip*> clip { nullptr };
    std::shared_ptr<epinox::GeneratedClip> ownedClip;
    juce::String ownedClipKey;   // identifies the settings the loaded clip was made from
    std::vector<std::pair<std::shared_ptr<epinox::GeneratedClip>, juce::uint32>> retiredClips;
    const epinox::GeneratedClip* lastClipSeen = nullptr;
    int freeRunPosition = 0;
    double currentSampleRate = 44100.0;
    std::atomic<double> hostBpm { 0.0 };

    juce::uint32 variationCounter = 0;
    juce::String generationStatus;
    bool generationWarning = false;
    int generationStatusVersion = 0;
    std::shared_ptr<std::atomic<bool>> alive { std::make_shared<std::atomic<bool>> (true) };

    epinox::GenerationService generation;   // declared last: destroyed first, it waits for any running job

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxProcessor)
};
